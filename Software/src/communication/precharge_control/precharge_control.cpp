#include "precharge_control.h"
#include "../../datalayer/datalayer.h"
#include "../../datalayer/datalayer_extended.h"
#include "../../devboard/hal/hal.h"

#ifdef PRECHARGE_CONTROL
const bool precharge_control_enabled_default = true;
#else
const bool precharge_control_enabled_default = false;
#endif

bool precharge_control_enabled = precharge_control_enabled_default;

// Parameters
#define MAX_PRECHARGE_TIME_MS 15000  // Maximum time precharge may be enabled
#define Precharge_default_PWM_Freq 11000
#define Precharge_min_PWM_Freq 5000
#define Precharge_max_PWM_Freq 34000
#define Precharge_PWM_Res 8
#define PWM_Freq 20000  // 20 kHz frequency, beyond audible range
#define PWM_Precharge_Channel 0
#ifndef INVERTER_DISCONNECT_CONTACTOR_IS_NORMALLY_OPEN
#define ON 0   //Normally closed contactors use inverted logic
#define OFF 1  //Normally closed contactors use inverted logic
#else
#define ON 1
#define OFF 0
#endif
static unsigned long prechargeStartTime = 0;
static uint32_t freq = Precharge_default_PWM_Freq;
static uint16_t delta_freq = 1;
static int32_t prev_external_voltage = 20000;

// Initialization functions

bool init_precharge_control() {
  if (!precharge_control_enabled) {
    return true;
  }

  // Setup PWM Channel Frequency and Resolution
#ifdef DEBUG_LOG
  logging.printf("Precharge control initialised\n");
#endif

  auto hia4v1_pin = esp32hal->HIA4V1_PIN();
  auto inverter_disconnect_contactor_pin = esp32hal->INVERTER_DISCONNECT_CONTACTOR_PIN();

  if (!esp32hal->alloc_pins("Precharge control", hia4v1_pin, inverter_disconnect_contactor_pin)) {
    return false;
  }

  pinMode(hia4v1_pin, OUTPUT);
  digitalWrite(hia4v1_pin, LOW);
  pinMode(inverter_disconnect_contactor_pin, OUTPUT);
  digitalWrite(inverter_disconnect_contactor_pin, LOW);

  return true;
}

// Main functions
void handle_precharge_control(unsigned long currentMillis) {
  auto hia4v1_pin = esp32hal->HIA4V1_PIN();
  auto inverter_disconnect_contactor_pin = esp32hal->INVERTER_DISCONNECT_CONTACTOR_PIN();

  int32_t target_voltage = datalayer.battery.status.voltage_dV;
  int32_t external_voltage = datalayer_extended.meb.BMS_voltage_intermediate_dV;

  switch (datalayer.system.status.precharge_status) {
    case AUTO_PRECHARGE_IDLE:
      if (datalayer.system.settings.start_precharging) {
        datalayer.system.status.precharge_status = AUTO_PRECHARGE_START;
      }
      break;
    case AUTO_PRECHARGE_START:
      freq = Precharge_default_PWM_Freq;
      ledcAttachChannel(hia4v1_pin, freq, Precharge_PWM_Res, PWM_Precharge_Channel);
      ledcWriteTone(hia4v1_pin, freq);  // Set frequency and set dutycycle to 50%
      prechargeStartTime = currentMillis;
      datalayer.system.status.precharge_status = AUTO_PRECHARGE_PRECHARGING;
#ifdef DEBUG_LOG
      logging.printf("Precharge: Starting sequence\n");
#endif
      digitalWrite(inverter_disconnect_contactor_pin, OFF);
      break;

    case AUTO_PRECHARGE_PRECHARGING:
      //  Check if external voltage measurement changed, for instance with the MEB batteries, the external voltage is only updated every 100ms.
      if (prev_external_voltage != external_voltage && external_voltage != 0) {
        prev_external_voltage = external_voltage;

        if (labs(target_voltage - external_voltage) > 150) {
          delta_freq = 2000;
        } else if (labs(target_voltage - external_voltage) > 80) {
          delta_freq = labs(target_voltage - external_voltage) * 6;
        } else {
          delta_freq = labs(target_voltage - external_voltage) * 3;
        }
        if (target_voltage > external_voltage) {
          freq += delta_freq;
        } else {
          freq -= delta_freq;
        }
        if (freq > Precharge_max_PWM_Freq)
          freq = Precharge_max_PWM_Freq;
        if (freq < Precharge_min_PWM_Freq)
          freq = Precharge_min_PWM_Freq;
#ifdef DEBUG_LOG
        logging.printf("Precharge: Target: %d V  Extern: %d V  Frequency: %u\n", target_voltage / 10,
                       external_voltage / 10, freq);
#endif
        ledcWriteTone(hia4v1_pin, freq);
      }

      if ((datalayer.battery.status.real_bms_status != BMS_STANDBY &&
           datalayer.battery.status.real_bms_status != BMS_ACTIVE) ||
          datalayer.battery.status.bms_status != ACTIVE || datalayer.system.settings.equipment_stop_active) {
        pinMode(hia4v1_pin, OUTPUT);
        digitalWrite(hia4v1_pin, LOW);
        digitalWrite(inverter_disconnect_contactor_pin, ON);
        datalayer.system.status.precharge_status = AUTO_PRECHARGE_IDLE;
#ifdef DEBUG_LOG
        logging.printf("Precharge: Disabling Precharge bms not standby/active or equipment stop\n");
#endif
      } else if (currentMillis - prechargeStartTime >= MAX_PRECHARGE_TIME_MS ||
                 datalayer.battery.status.real_bms_status == BMS_FAULT) {
        pinMode(hia4v1_pin, OUTPUT);
        digitalWrite(hia4v1_pin, LOW);
        digitalWrite(inverter_disconnect_contactor_pin, ON);
        datalayer.system.status.precharge_status = AUTO_PRECHARGE_OFF;
#ifdef DEBUG_LOG
        logging.printf("Precharge: Disabled (timeout reached / BMS fault) -> AUTO_PRECHARGE_OFF\n");
#endif
        set_event(EVENT_AUTOMATIC_PRECHARGE_FAILURE, 0);

        // Add event
      } else if (datalayer.system.status.battery_allows_contactor_closing) {
        pinMode(hia4v1_pin, OUTPUT);
        digitalWrite(hia4v1_pin, LOW);
        digitalWrite(inverter_disconnect_contactor_pin, ON);
        datalayer.system.status.precharge_status = AUTO_PRECHARGE_COMPLETED;
#ifdef DEBUG_LOG
        logging.printf("Precharge: Disabled (contacts closed) -> COMPLETED\n");
#endif
      }
      break;

    case AUTO_PRECHARGE_COMPLETED:
      if (datalayer.system.settings.equipment_stop_active || datalayer.battery.status.bms_status != ACTIVE) {
        datalayer.system.status.precharge_status = AUTO_PRECHARGE_IDLE;
#ifdef DEBUG_LOG
        logging.printf("Precharge: equipment stop activated -> IDLE\n");
#endif
      }
      break;

    case AUTO_PRECHARGE_OFF:
      if (!datalayer.system.status.battery_allows_contactor_closing ||
          !datalayer.system.status.inverter_allows_contactor_closing ||
          datalayer.system.settings.equipment_stop_active || datalayer.battery.status.bms_status != FAULT) {
        datalayer.system.status.precharge_status = AUTO_PRECHARGE_IDLE;
        pinMode(hia4v1_pin, OUTPUT);
        digitalWrite(hia4v1_pin, LOW);
#ifdef DEBUG_LOG
        logging.printf("Precharge: equipment stop activated -> IDLE\n");
#endif
      }
      break;

    default:
      break;
  }
}
