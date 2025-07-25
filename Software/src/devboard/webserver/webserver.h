#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Preferences.h>
#include <WiFi.h>
#include "../../lib/ESP32Async-ESPAsyncWebServer/src/ESPAsyncWebServer.h"
#include "../../lib/YiannisBourkelis-Uptime-Library/src/uptime_formatter.h"
#include "../../lib/ayushsharma82-ElegantOTA/src/ElegantOTA.h"
#include "../../lib/mathieucarbou-AsyncTCPSock/src/AsyncTCP.h"

extern bool webserver_enabled;

extern const char* version_number;  // The current software version, shown on webserver

// Common charger parameters
extern float charger_stat_HVcur;
extern float charger_stat_HVvol;
extern float charger_stat_ACcur;
extern float charger_stat_ACvol;
extern float charger_stat_LVcur;
extern float charger_stat_LVvol;

//LEAF charger
extern uint16_t OBC_Charge_Power;

/**
 * @brief Initialization function for the webserver.
 *
 * @param[in] void
 *
 * @return void
 */
void init_webserver();

// /**
//  * @brief Function to handle WiFi reconnection.
//  *
//  * @param[in] void
//  *
//  * @return void
//  */
// void handle_WiFi_reconnection(unsigned long currentMillis);

/**
 * @brief Initialization function for ElegantOTA.
 *
 * @param[in] void
 * 
 * @return void
 */
void init_ElegantOTA();

/**
 * @brief Replaces placeholder with content section in web page
 *
 * @param[in] var
 *
 * @return String
 */
String processor(const String& var);
String get_firmware_info_processor(const String& var);

/**
 * @brief Executes on OTA start 
 *
 * @param[in] void
 *
 * @return void
 */
void onOTAStart();

/**
 * @brief Executes on OTA progress 
 * 
 * @param[in] current Current bytes
 * @param[in] final Final bytes
 *
 * @return void
 */
void onOTAProgress(size_t current, size_t final);

/**
 * @brief Executes on OTA end 
 *
 * @param[in] void
 * 
 * @return bool success: success = true, failed = false
 */
void onOTAEnd(bool success);

/**
 * @brief Formats power values 
 *
 * @param[in] float or uint16_t 
 * 
 * @return string: values
 */
template <typename T>
String formatPowerValue(String label, T value, String unit, int precision, String color = "white");

template <typename T>  // This function makes power values appear as W when under 1000, and kW when over
String formatPowerValue(T value, String unit, int precision);

extern void store_settings();

void ota_monitor();

#endif
