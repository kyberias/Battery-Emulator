#include "mqtt.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <src/communication/nvm/comm_nvm.h>
#include <list>
#include "../../../USER_SECRETS.h"
#include "../../../USER_SETTINGS.h"
#include "../../battery/BATTERIES.h"
#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../../datalayer/datalayer.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../utils/events.h"
#include "../utils/timer.h"
#include "mqtt.h"
#include "mqtt_client.h"

#ifdef MQTT
const bool mqtt_enabled_default = true;
#else
const bool mqtt_enabled_default = false;
#endif

bool mqtt_enabled = mqtt_enabled_default;

#ifdef HA_AUTODISCOVERY
const bool ha_autodiscovery_enabled_default = true;
#else
const bool ha_autodiscovery_enabled_default = false;
#endif

bool ha_autodiscovery_enabled = ha_autodiscovery_enabled_default;

#ifdef COMMON_IMAGE
const int mqtt_port_default = 0;
const char* mqtt_server_default = "";
#else
const int mqtt_port_default = MQTT_PORT;
const char* mqtt_server_default = MQTT_SERVER;
#endif

int mqtt_port = mqtt_port_default;
std::string mqtt_server = mqtt_server_default;

#ifdef MQTT_MANUAL_TOPIC_OBJECT_NAME
const bool mqtt_manual_topic_object_name_default = true;
#else
const bool mqtt_manual_topic_object_name_default = false;
#endif

bool mqtt_manual_topic_object_name = mqtt_manual_topic_object_name_default;

esp_mqtt_client_config_t mqtt_cfg;
esp_mqtt_client_handle_t client;
char mqtt_msg[MQTT_MSG_BUFFER_SIZE];
MyTimer publish_global_timer(5000);  //publish timer
MyTimer check_global_timer(800);     // check timmer - low-priority MQTT checks, where responsiveness is not critical.
bool client_started = false;
static String lwt_topic = "";

static String topic_name = "";
static String object_id_prefix = "";
static String device_name = "";
static String device_id = "";

static bool publish_common_info(void);
static bool publish_cell_voltages(void);
static bool publish_cell_balancing(void);
static bool publish_events(void);

/** Publish global values and call callbacks for specific modules */
static void publish_values(void) {

  if (mqtt_publish((topic_name + "/status").c_str(), "online", false) == false) {
    return;
  }

  if (publish_events() == false) {
    return;
  }

  if (publish_common_info() == false) {
    return;
  }

#ifdef MQTT_PUBLISH_CELL_VOLTAGES
  if (publish_cell_voltages() == false) {
    return;
  }
#endif

#ifdef MQTT_PUBLISH_CELL_VOLTAGES
  if (publish_cell_balancing() == false) {
    return;
  }
#endif
}

static bool ha_common_info_published = false;
static bool ha_cell_voltages_published = false;
static bool ha_events_published = false;
static bool ha_buttons_published = false;
struct SensorConfig {
  const char* object_id;
  const char* name;
  const char* value_template;
  const char* unit;
  const char* device_class;

  // A function that returns true for the battery if it supports this config
  std::function<bool(Battery*)> condition;
};

static std::function<bool(Battery*)> always = [](Battery* b) {
  return true;
};
static std::function<bool(Battery*)> supports_charged = [](Battery* b) {
  return b->supports_charged_energy();
};

SensorConfig batterySensorConfigTemplate[] = {
    {"SOC", "SOC (Scaled)", "", "%", "battery", always},
    {"SOC_real", "SOC (real)", "", "%", "battery", always},
    {"state_of_health", "State Of Health", "", "%", "battery", always},
    {"temperature_min", "Temperature Min", "", "°C", "temperature", always},
    {"temperature_max", "Temperature Max", "", "°C", "temperature", always},
    {"cpu_temp", "CPU Temperature", "", "°C", "temperature", always},
    {"stat_batt_power", "Stat Batt Power", "", "W", "power", always},
    {"battery_current", "Battery Current", "", "A", "current", always},
    {"cell_max_voltage", "Cell Max Voltage", "", "V", "voltage", always},
    {"cell_min_voltage", "Cell Min Voltage", "", "V", "voltage", always},
    {"cell_voltage_delta", "Cell Voltage Delta", "", "mV", "voltage", always},
    {"battery_voltage", "Battery Voltage", "", "V", "voltage", always},
    {"total_capacity", "Battery Total Capacity", "", "Wh", "energy", always},
    {"remaining_capacity", "Battery Remaining Capacity (scaled)", "", "Wh", "energy", always},
    {"remaining_capacity_real", "Battery Remaining Capacity (real)", "", "Wh", "energy", always},
    {"max_discharge_power", "Battery Max Discharge Power", "", "W", "power", always},
    {"max_charge_power", "Battery Max Charge Power", "", "W", "power", always},
    {"charged_energy", "Battery Charged Energy", "", "Wh", "energy", supports_charged},
    {"discharged_energy", "Battery Discharged Energy", "", "Wh", "energy", supports_charged},
    {"balancing_active_cells", "Balancing Active Cells", "", "", "", always}};

SensorConfig globalSensorConfigTemplate[] = {{"bms_status", "BMS Status", "", "", "", always},
                                             {"pause_status", "Pause Status", "", "", "", always}};

static std::list<SensorConfig> sensorConfigs;

void create_battery_sensor_configs() {
  for (auto& config : batterySensorConfigTemplate) {
    config.value_template = strdup(("{{ value_json." + std::string(config.object_id) + " }}").c_str());

    sensorConfigs.push_back(config);

    if (battery2) {
      config.value_template = strdup(("{{ value_json." + std::string(config.object_id) + "_2 }}").c_str());
      config.name = strdup(String(config.name + String(" 2")).c_str());
      config.object_id = strdup(String(config.object_id + String("_2")).c_str());

      sensorConfigs.push_back(config);
    }
  }
}

void create_global_sensor_configs() {
  for (auto& config : globalSensorConfigTemplate) {
    config.value_template = strdup(("{{ value_json." + std::string(config.object_id) + " }}").c_str());
    sensorConfigs.push_back(config);
  }
}

SensorConfig buttonConfigs[] = {{"BMSRESET", "Reset BMS"},
                                {"PAUSE", "Pause charge/discharge"},
                                {"RESUME", "Resume charge/discharge"},
                                {"RESTART", "Restart Battery Emulator"},
                                {"STOP", "Open Contactors"}};

static String generateCommonInfoAutoConfigTopic(const char* object_id) {
  return "homeassistant/sensor/" + topic_name + "/" + String(object_id) + "/config";
}

static String generateCellVoltageAutoConfigTopic(int cell_number, String battery_suffix) {
  return "homeassistant/sensor/" + topic_name + "/cell_voltage" + battery_suffix + String(cell_number) + "/config";
}

static String generateEventsAutoConfigTopic(const char* object_id) {
  return "homeassistant/sensor/" + topic_name + "/" + String(object_id) + "/config";
}

static String generateButtonAutoConfigTopic(const char* subtype) {
  return "homeassistant/button/" + topic_name + "/" + String(subtype) + "/config";
}

void set_common_discovery_attributes(JsonDocument& doc) {
  doc["device"]["identifiers"][0] = device_id;
  doc["device"]["manufacturer"] = "DalaTech";
  doc["device"]["model"] = "BatteryEmulator";
  doc["device"]["name"] = device_name;
  doc["availability"][0]["topic"] = lwt_topic;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  doc["enabled_by_default"] = true;
}

void set_battery_voltage_attributes(JsonDocument& doc, int i, int cellNumber, const String& state_topic,
                                    const String& object_id_prefix, const String& battery_name_suffix) {
  doc["name"] = "Battery" + battery_name_suffix + " Cell Voltage " + String(cellNumber);
  doc["object_id"] = object_id_prefix + "battery_voltage_cell" + String(cellNumber);
  doc["unique_id"] = topic_name + object_id_prefix + "_battery_voltage_cell" + String(cellNumber);
  doc["device_class"] = "voltage";
  doc["state_class"] = "measurement";
  doc["state_topic"] = state_topic;
  doc["unit_of_measurement"] = "V";
  doc["value_template"] = "{{ value_json.cell_voltages[" + String(i) + "] }}";
}

static String generateButtonTopic(const char* subtype) {
  return topic_name + "/command/" + String(subtype);
}

void set_battery_attributes(JsonDocument& doc, const DATALAYER_BATTERY_TYPE& battery, const String& suffix,
                            bool supports_charged) {
  doc["SOC" + suffix] = ((float)battery.status.reported_soc) / 100.0;
  doc["SOC_real" + suffix] = ((float)battery.status.real_soc) / 100.0;
  doc["state_of_health" + suffix] = ((float)battery.status.soh_pptt) / 100.0;
  doc["temperature_min" + suffix] = ((float)((int16_t)battery.status.temperature_min_dC)) / 10.0;
  doc["temperature_max" + suffix] = ((float)((int16_t)battery.status.temperature_max_dC)) / 10.0;
  doc["cpu_temp" + suffix] = datalayer.system.info.CPU_temperature;
  doc["stat_batt_power" + suffix] = ((float)((int32_t)battery.status.active_power_W));
  doc["battery_current" + suffix] = ((float)((int16_t)battery.status.current_dA)) / 10.0;
  doc["battery_voltage" + suffix] = ((float)battery.status.voltage_dV) / 10.0;
  if (battery.info.number_of_cells != 0u && battery.status.cell_voltages_mV[battery.info.number_of_cells - 1] != 0u) {
    doc["cell_max_voltage" + suffix] = ((float)battery.status.cell_max_voltage_mV) / 1000.0;
    doc["cell_min_voltage" + suffix] = ((float)battery.status.cell_min_voltage_mV) / 1000.0;
    doc["cell_voltage_delta" + suffix] =
        ((float)battery.status.cell_max_voltage_mV) - ((float)battery.status.cell_min_voltage_mV);
  }
  doc["total_capacity" + suffix] = ((float)battery.info.total_capacity_Wh);
  doc["remaining_capacity_real" + suffix] = ((float)battery.status.remaining_capacity_Wh);
  doc["remaining_capacity" + suffix] = ((float)battery.status.reported_remaining_capacity_Wh);
  doc["max_discharge_power" + suffix] = ((float)battery.status.max_discharge_power_W);
  doc["max_charge_power" + suffix] = ((float)battery.status.max_charge_power_W);

  if (supports_charged) {
    if (datalayer.battery.status.total_charged_battery_Wh != 0 &&
        datalayer.battery.status.total_discharged_battery_Wh != 0) {
      doc["charged_energy" + suffix] = ((float)datalayer.battery.status.total_charged_battery_Wh);
      doc["discharged_energy" + suffix] = ((float)datalayer.battery.status.total_discharged_battery_Wh);
    }
  }

  // Add balancing data
  uint16_t active_cells = 0;
  if (battery.info.number_of_cells != 0u) {
    for (size_t i = 0; i < battery.info.number_of_cells; ++i) {
      if (battery.status.cell_balancing_status[i]) {
        active_cells++;
      }
    }
  }
  doc["balancing_active_cells" + suffix] = active_cells;
}

static std::vector<EventData> order_events;

static bool publish_common_info(void) {
  static JsonDocument doc;
  static String state_topic = topic_name + "/info";

  //  if(ha_autodiscovery_enabled) {

  if (ha_autodiscovery_enabled && !ha_common_info_published) {
    for (auto& config : sensorConfigs) {
      if (!config.condition(battery)) {
        continue;
      }

      doc["name"] = config.name;
      doc["state_topic"] = state_topic;
      doc["unique_id"] = topic_name + "_" + String(config.object_id);
      doc["object_id"] = object_id_prefix + String(config.object_id);
      doc["value_template"] = config.value_template;
      if (config.unit != nullptr && strlen(config.unit) > 0) {
        doc["unit_of_measurement"] = config.unit;
      }
      if (config.device_class != nullptr && strlen(config.device_class) > 0) {
        doc["device_class"] = config.device_class;
        doc["state_class"] = "measurement";
      }
      set_common_discovery_attributes(doc);
      serializeJson(doc, mqtt_msg);
      if (mqtt_publish(generateCommonInfoAutoConfigTopic(config.object_id).c_str(), mqtt_msg, true)) {
        ha_common_info_published = true;
      } else {
        return false;
      }
      doc.clear();
    }

  } else {
    doc["bms_status"] = getBMSStatus(datalayer.battery.status.bms_status);
    doc["pause_status"] = get_emulator_pause_status();

    //only publish these values if BMS is active and we are comunication  with the battery (can send CAN messages to the battery)
    if (datalayer.battery.status.CAN_battery_still_alive && allowed_to_send_CAN && esp32hal->system_booted_up()) {
      set_battery_attributes(doc, datalayer.battery, "", battery->supports_charged_energy());
    }

    if (battery2) {
      //only publish these values if BMS is active and we are comunication  with the battery (can send CAN messages to the battery)
      if (datalayer.battery2.status.CAN_battery_still_alive && allowed_to_send_CAN && esp32hal->system_booted_up()) {
        set_battery_attributes(doc, datalayer.battery2, "_2", battery2->supports_charged_energy());
      }
    }
    serializeJson(doc, mqtt_msg);
    if (mqtt_publish(state_topic.c_str(), mqtt_msg, false) == false) {
#ifdef DEBUG_LOG
      logging.println("Common info MQTT msg could not be sent");
#endif  // DEBUG_LOG
      return false;
    }
    doc.clear();
  }
  return true;
}

static bool publish_cell_voltages(void) {
  static JsonDocument doc;
  static String state_topic = topic_name + "/spec_data";
  static String state_topic_2 = topic_name + "/spec_data_2";

  if (ha_autodiscovery_enabled) {
    bool failed_to_publish = false;
    if (ha_cell_voltages_published == false) {

      // If the cell voltage number isn't initialized...
      if (datalayer.battery.info.number_of_cells != 0u) {

        for (int i = 0; i < datalayer.battery.info.number_of_cells; i++) {
          int cellNumber = i + 1;
          set_battery_voltage_attributes(doc, i, cellNumber, state_topic, object_id_prefix, "");
          set_common_discovery_attributes(doc);

          serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));
          if (mqtt_publish(generateCellVoltageAutoConfigTopic(cellNumber, "").c_str(), mqtt_msg, true) == false) {
            failed_to_publish = true;
            return false;
          }
        }
        doc.clear();  // clear after sending autoconfig
      }

      if (battery2) {
        // TODO: Combine this identical block with the previous one.
        // If the cell voltage number isn't initialized...
        if (datalayer.battery2.info.number_of_cells != 0u) {

          for (int i = 0; i < datalayer.battery.info.number_of_cells; i++) {
            int cellNumber = i + 1;
            set_battery_voltage_attributes(doc, i, cellNumber, state_topic_2, object_id_prefix + "2_", " 2");
            set_common_discovery_attributes(doc);

            serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));
            if (mqtt_publish(generateCellVoltageAutoConfigTopic(cellNumber, "_2_").c_str(), mqtt_msg, true) == false) {
              failed_to_publish = true;
              return false;
            }
          }
          doc.clear();  // clear after sending autoconfig
        }
      }
    }
    if (failed_to_publish == false) {
      ha_cell_voltages_published = true;
    }
  }

  // If cell voltages have been populated...
  if (datalayer.battery.info.number_of_cells != 0u &&
      datalayer.battery.status.cell_voltages_mV[datalayer.battery.info.number_of_cells - 1] != 0u) {

    JsonArray cell_voltages = doc["cell_voltages"].to<JsonArray>();
    for (size_t i = 0; i < datalayer.battery.info.number_of_cells; ++i) {
      cell_voltages.add(((float)datalayer.battery.status.cell_voltages_mV[i]) / 1000.0);
    }

    serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

    if (!mqtt_publish(state_topic.c_str(), mqtt_msg, false)) {
#ifdef DEBUG_LOG
      logging.println("Cell voltage MQTT msg could not be sent");
#endif  // DEBUG_LOG
      return false;
    }
    doc.clear();
  }

  if (battery2) {
    // If cell voltages have been populated...
    if (datalayer.battery2.info.number_of_cells != 0u &&
        datalayer.battery2.status.cell_voltages_mV[datalayer.battery2.info.number_of_cells - 1] != 0u) {

      JsonArray cell_voltages = doc["cell_voltages"].to<JsonArray>();
      for (size_t i = 0; i < datalayer.battery2.info.number_of_cells; ++i) {
        cell_voltages.add(((float)datalayer.battery2.status.cell_voltages_mV[i]) / 1000.0);
      }

      serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

      if (!mqtt_publish(state_topic_2.c_str(), mqtt_msg, false)) {
#ifdef DEBUG_LOG
        logging.println("Cell voltage MQTT msg could not be sent");
#endif  // DEBUG_LOG
        return false;
      }
      doc.clear();
    }
  }
  return true;
}

static bool publish_cell_balancing(void) {
  static JsonDocument doc;
  static String state_topic = topic_name + "/balancing_data";
  static String state_topic_2 = topic_name + "/balancing_data_2";

  // If cell balancing data is available...
  if (datalayer.battery.info.number_of_cells != 0u) {

    JsonArray cell_balancing = doc["cell_balancing"].to<JsonArray>();
    for (size_t i = 0; i < datalayer.battery.info.number_of_cells; ++i) {
      cell_balancing.add(datalayer.battery.status.cell_balancing_status[i]);
    }

    serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

    if (!mqtt_publish(state_topic.c_str(), mqtt_msg, false)) {
#ifdef DEBUG_LOG
      logging.println("Cell balancing MQTT msg could not be sent");
#endif  // DEBUG_LOG
      return false;
    }
    doc.clear();
  }

  // Handle second battery if available
  if (battery2) {
    if (datalayer.battery2.info.number_of_cells != 0u) {

      JsonArray cell_balancing = doc["cell_balancing"].to<JsonArray>();
      for (size_t i = 0; i < datalayer.battery2.info.number_of_cells; ++i) {
        cell_balancing.add(datalayer.battery2.status.cell_balancing_status[i]);
      }

      serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

      if (!mqtt_publish(state_topic_2.c_str(), mqtt_msg, false)) {
#ifdef DEBUG_LOG
        logging.println("Cell balancing MQTT msg could not be sent");
#endif  // DEBUG_LOG
        return false;
      }
      doc.clear();
    }
  }
  return true;
}

bool publish_events() {
  static JsonDocument doc;
  static String state_topic = topic_name + "/events";
  if (ha_autodiscovery_enabled && !ha_events_published) {

    doc["name"] = "Event";
    doc["state_topic"] = state_topic;
    doc["unique_id"] = topic_name + "_event";
    doc["object_id"] = object_id_prefix + "event";
    doc["value_template"] =
        "{{ value_json.event_type ~ ' (c:' ~ value_json.count ~ ',m:' ~  value_json.millis ~ ') ' ~ value_json.message "
        "}}";
    doc["json_attributes_topic"] = state_topic;
    doc["json_attributes_template"] = "{{ value_json | tojson }}";
    set_common_discovery_attributes(doc);
    serializeJson(doc, mqtt_msg);
    if (mqtt_publish(generateEventsAutoConfigTopic("event").c_str(), mqtt_msg, true)) {
      ha_events_published = true;
    } else {
      return false;
    }

    doc.clear();
  } else {
    const EVENTS_STRUCT_TYPE* event_pointer;

    //clear the vector
    order_events.clear();
    // Collect all events
    for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
      event_pointer = get_event_pointer((EVENTS_ENUM_TYPE)i);
      if (event_pointer->occurences > 0 && !event_pointer->MQTTpublished) {
        order_events.push_back({static_cast<EVENTS_ENUM_TYPE>(i), event_pointer});
      }
    }
    // Sort events by timestamp
    std::sort(order_events.begin(), order_events.end(), compareEventsByTimestampAsc);

    for (const auto& event : order_events) {

      EVENTS_ENUM_TYPE event_handle = event.event_handle;
      event_pointer = event.event_pointer;

      doc["event_type"] = String(get_event_enum_string(event_handle));
      doc["severity"] = String(get_event_level_string(event_handle));
      doc["count"] = String(event_pointer->occurences);
      doc["data"] = String(event_pointer->data);
      doc["message"] = get_event_message_string(event_handle);
      doc["millis"] = String(event_pointer->timestamp);

      serializeJson(doc, mqtt_msg);
      if (!mqtt_publish(state_topic.c_str(), mqtt_msg, false)) {
#ifdef DEBUG_LOG
        logging.println("Common info MQTT msg could not be sent");
#endif  // DEBUG_LOG
        return false;
      } else {
        set_event_MQTTpublished(event_handle);
      }
      doc.clear();
      //clear the vector
      order_events.clear();
    }
  }
  return true;
}

static bool publish_buttons_discovery(void) {
  if (ha_autodiscovery_enabled) {
    if (ha_buttons_published == false) {
#ifdef DEBUG_LOG
      logging.println("Publishing buttons discovery");
#endif  // DEBUG_LOG

      static JsonDocument doc;
      for (int i = 0; i < sizeof(buttonConfigs) / sizeof(buttonConfigs[0]); i++) {
        SensorConfig& config = buttonConfigs[i];
        doc["name"] = config.name;
        doc["unique_id"] = object_id_prefix + config.object_id;
        doc["command_topic"] = generateButtonTopic(config.object_id);
        set_common_discovery_attributes(doc);
        serializeJson(doc, mqtt_msg);
        if (mqtt_publish(generateButtonAutoConfigTopic(config.object_id).c_str(), mqtt_msg, true)) {
          ha_buttons_published = true;
        } else {
          return false;
        }
        doc.clear();
      }
    }
  }
  return true;
}

static void subscribe() {
  esp_mqtt_client_subscribe(client, (topic_name + "/command/+").c_str(), 1);
}

void mqtt_message_received(char* topic_raw, int topic_len, char* data, int data_len) {

  char* topic = strndup(topic_raw, topic_len);

#ifdef DEBUG_LOG
  logging.printf("MQTT message arrived: [%.*s]\n", topic_len, topic);
#endif  // DEBUG_LOG

#ifdef REMOTE_BMS_RESET
  const char* bmsreset_topic = generateButtonTopic("BMSRESET").c_str();
  if (strcmp(topic, bmsreset_topic) == 0) {
#ifdef DEBUG_LOG
    logging.println("Triggering BMS reset");
#endif  // DEBUG_LOG
    start_bms_reset();
  }
#endif  // REMOTE_BMS_RESET

  if (strcmp(topic, generateButtonTopic("PAUSE").c_str()) == 0) {
    setBatteryPause(true, false);
  }

  if (strcmp(topic, generateButtonTopic("RESUME").c_str()) == 0) {
    setBatteryPause(false, false, false);
  }

  if (strcmp(topic, generateButtonTopic("RESTART").c_str()) == 0) {
    setBatteryPause(true, true, true, false);
    delay(1000);
    ESP.restart();
  }

  if (strcmp(topic, generateButtonTopic("STOP").c_str()) == 0) {
    setBatteryPause(true, false, true);
  }
  free(topic);
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      clear_event(EVENT_MQTT_DISCONNECT);
      set_event(EVENT_MQTT_CONNECT, 0);

      publish_buttons_discovery();
      subscribe();
#ifdef DEBUG_LOG
      logging.println("MQTT connected");
#endif  // DEBUG_LOG
      break;
    case MQTT_EVENT_DISCONNECTED:
      set_event(EVENT_MQTT_DISCONNECT, 0);
#ifdef DEBUG_LOG
      logging.println("MQTT disconnected!");
#endif  // DEBUG_LOG
      break;
    case MQTT_EVENT_DATA:
      mqtt_message_received(event->topic, event->topic_len, event->data, event->data_len);
      break;
    case MQTT_EVENT_ERROR:
#ifdef DEBUG_LOG
      logging.println("MQTT_ERROR");
      logging.print("reported from esp-tls");
      logging.println(event->error_handle->esp_tls_last_esp_err);
      logging.print("reported from tls stack");
      logging.println(event->error_handle->esp_tls_stack_err);
      logging.print("captured as transport's socket errno");
      logging.println(strerror(event->error_handle->esp_transport_sock_errno));
#endif  // DEBUG_LOG
      break;
  }
}

bool init_mqtt(void) {
  if (ha_autodiscovery_enabled) {
    create_battery_sensor_configs();
    create_global_sensor_configs();
  }

  if (mqtt_manual_topic_object_name) {
#ifdef COMMON_IMAGE
    BatteryEmulatorSettingsStore settings;
    topic_name = settings.getString("MQTTTOPIC", mqtt_topic_name);
    object_id_prefix = settings.getString("MQTTOBJIDPREFIX", mqtt_object_id_prefix);
    device_name = settings.getString("MQTTDEVICENAME", mqtt_device_name);
    device_id = settings.getString("HADEVICEID", ha_device_id);

    if (topic_name.length() == 0) {
      topic_name = mqtt_topic_name;
    }

    if (object_id_prefix.length() == 0) {
      object_id_prefix = mqtt_object_id_prefix;
    }

    if (device_name.length() == 0) {
      device_name = mqtt_device_name;
    }

    if (device_id.length() == 0) {
      device_id = ha_device_id;
    }

#else
    // Use custom topic name, object ID prefix, and device name from user settings
    topic_name = mqtt_topic_name;
    object_id_prefix = mqtt_object_id_prefix;
    device_name = mqtt_device_name;
    device_id = ha_device_id;
#endif
  } else {
    // Use default naming based on WiFi hostname for topic, object ID prefix, and device name
    topic_name = "battery-emulator_" + String(WiFi.getHostname());
    object_id_prefix = String(WiFi.getHostname()) + String("_");
    device_name = "BatteryEmulator_" + String(WiFi.getHostname());
    device_id = "battery-emulator";
  }

  String clientId = String("BatteryEmulatorClient-") + WiFi.getHostname();

  mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  mqtt_cfg.broker.address.hostname = mqtt_server.c_str();
  mqtt_cfg.broker.address.port = mqtt_port;
  mqtt_cfg.credentials.client_id = clientId.c_str();
  mqtt_cfg.credentials.username = mqtt_user.c_str();
  mqtt_cfg.credentials.authentication.password = mqtt_password.c_str();
  lwt_topic = topic_name + "/status";
  mqtt_cfg.session.last_will.topic = lwt_topic.c_str();
  mqtt_cfg.session.last_will.qos = 1;
  mqtt_cfg.session.last_will.retain = true;
  mqtt_cfg.session.last_will.msg = "offline";
  mqtt_cfg.session.last_will.msg_len = strlen(mqtt_cfg.session.last_will.msg);
  mqtt_cfg.network.timeout_ms = MQTT_TIMEOUT;
  client = esp_mqtt_client_init(&mqtt_cfg);

  if (client == nullptr) {
    return false;
  }

  if (esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, client) != ESP_OK) {
    return false;
  }

  return true;
}

void mqtt_loop(void) {
  // Only attempt to publish/reconnect MQTT if Wi-Fi is connectedand checkTimmer is elapsed
  if (check_global_timer.elapsed() && WiFi.status() == WL_CONNECTED) {

    if (client_started == false) {
      esp_mqtt_client_start(client);
      client_started = true;
#ifdef DEBUG_LOG
      logging.println("MQTT initialized");
#endif  // DEBUG_LOG
      return;
    }

    if (publish_global_timer.elapsed())  // Every 5s
    {
      publish_values();
    }
  }
}

bool mqtt_publish(const char* topic, const char* mqtt_msg, bool retain) {
  int msg_id = esp_mqtt_client_publish(client, topic, mqtt_msg, strlen(mqtt_msg), MQTT_QOS, retain);
  return msg_id > -1;
}
