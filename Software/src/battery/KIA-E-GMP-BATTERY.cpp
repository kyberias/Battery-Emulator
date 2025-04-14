#include "../include.h"
#ifdef KIA_E_GMP_BATTERY
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"
#include "../lib/pierremolinaro-ACAN2517FD/ACAN2517FD.h"
#include "KIA-E-GMP-BATTERY.h"

/* Do not change code below unless you are sure what you are doing */
static unsigned long previousMillis200ms = 0;  // will store last time a 200ms CAN Message was send
static unsigned long previousMillis10s = 0;    // will store last time a 10s CAN Message was send

const unsigned char crc8_table[256] =
    {  // CRC8_SAE_J1850_ZER0 formula,0x1D Poly,initial value 0x3F,Final XOR value varies
        0x00, 0x1D, 0x3A, 0x27, 0x74, 0x69, 0x4E, 0x53, 0xE8, 0xF5, 0xD2, 0xCF, 0x9C, 0x81, 0xA6, 0xBB, 0xCD, 0xD0,
        0xF7, 0xEA, 0xB9, 0xA4, 0x83, 0x9E, 0x25, 0x38, 0x1F, 0x02, 0x51, 0x4C, 0x6B, 0x76, 0x87, 0x9A, 0xBD, 0xA0,
        0xF3, 0xEE, 0xC9, 0xD4, 0x6F, 0x72, 0x55, 0x48, 0x1B, 0x06, 0x21, 0x3C, 0x4A, 0x57, 0x70, 0x6D, 0x3E, 0x23,
        0x04, 0x19, 0xA2, 0xBF, 0x98, 0x85, 0xD6, 0xCB, 0xEC, 0xF1, 0x13, 0x0E, 0x29, 0x34, 0x67, 0x7A, 0x5D, 0x40,
        0xFB, 0xE6, 0xC1, 0xDC, 0x8F, 0x92, 0xB5, 0xA8, 0xDE, 0xC3, 0xE4, 0xF9, 0xAA, 0xB7, 0x90, 0x8D, 0x36, 0x2B,
        0x0C, 0x11, 0x42, 0x5F, 0x78, 0x65, 0x94, 0x89, 0xAE, 0xB3, 0xE0, 0xFD, 0xDA, 0xC7, 0x7C, 0x61, 0x46, 0x5B,
        0x08, 0x15, 0x32, 0x2F, 0x59, 0x44, 0x63, 0x7E, 0x2D, 0x30, 0x17, 0x0A, 0xB1, 0xAC, 0x8B, 0x96, 0xC5, 0xD8,
        0xFF, 0xE2, 0x26, 0x3B, 0x1C, 0x01, 0x52, 0x4F, 0x68, 0x75, 0xCE, 0xD3, 0xF4, 0xE9, 0xBA, 0xA7, 0x80, 0x9D,
        0xEB, 0xF6, 0xD1, 0xCC, 0x9F, 0x82, 0xA5, 0xB8, 0x03, 0x1E, 0x39, 0x24, 0x77, 0x6A, 0x4D, 0x50, 0xA1, 0xBC,
        0x9B, 0x86, 0xD5, 0xC8, 0xEF, 0xF2, 0x49, 0x54, 0x73, 0x6E, 0x3D, 0x20, 0x07, 0x1A, 0x6C, 0x71, 0x56, 0x4B,
        0x18, 0x05, 0x22, 0x3F, 0x84, 0x99, 0xBE, 0xA3, 0xF0, 0xED, 0xCA, 0xD7, 0x35, 0x28, 0x0F, 0x12, 0x41, 0x5C,
        0x7B, 0x66, 0xDD, 0xC0, 0xE7, 0xFA, 0xA9, 0xB4, 0x93, 0x8E, 0xF8, 0xE5, 0xC2, 0xDF, 0x8C, 0x91, 0xB6, 0xAB,
        0x10, 0x0D, 0x2A, 0x37, 0x64, 0x79, 0x5E, 0x43, 0xB2, 0xAF, 0x88, 0x95, 0xC6, 0xDB, 0xFC, 0xE1, 0x5A, 0x47,
        0x60, 0x7D, 0x2E, 0x33, 0x14, 0x09, 0x7F, 0x62, 0x45, 0x58, 0x0B, 0x16, 0x31, 0x2C, 0x97, 0x8A, 0xAD, 0xB0,
        0xE3, 0xFE, 0xD9, 0xC4};

static uint16_t inverterVoltageFrameHigh = 0;
static uint16_t inverterVoltage = 0;
static uint16_t soc_calculated = 0;
static uint16_t SOC_BMS = 0;
static uint16_t SOC_Display = 0;
static uint16_t SOC_estimated_lowest = 0;
static uint16_t SOC_estimated_highest = 0;
static uint16_t batterySOH = 1000;
static uint16_t CellVoltMax_mV = 3700;
static uint16_t CellVoltMin_mV = 3700;
static uint16_t batteryVoltage = 6700;
static int16_t leadAcidBatteryVoltage = 120;
static int16_t batteryAmps = 0;
static int16_t temperatureMax = 0;
static int16_t temperatureMin = 0;
static int16_t allowedDischargePower = 0;
static int16_t allowedChargePower = 0;
static int16_t poll_data_pid = 0;
static uint8_t CellVmaxNo = 0;
static uint8_t CellVminNo = 0;
static uint8_t batteryManagementMode = 0;
static uint8_t BMS_ign = 0;
static uint8_t batteryRelay = 0;
static uint8_t waterleakageSensor = 164;
static bool startedUp = false;
static bool ok_start_polling_battery = false;
static uint8_t counter_200 = 0;
static uint8_t KIA_7E4_COUNTER = 0x01;
static int8_t temperature_water_inlet = 0;
static int8_t powerRelayTemperature = 0;
static int8_t heatertemp = 0;
static bool set_voltage_limits = false;
static uint8_t ticks_200ms_counter = 0;
static uint8_t EGMP_1CF_counter = 0;
static uint8_t EGMP_3XF_counter = 0;

// Define the data points for %SOC depending on cell voltage
const uint8_t numPoints = 100;

const uint16_t SOC[] = {
    0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000, 2100, 2200, 2300, 2400,
    2500, 2600, 2700, 2800, 2900, 3000, 3100, 3200, 3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200, 4300, 4400, 4500, 4600, 4700, 4800, 4900,
    5000, 5100, 5200, 5300, 5400, 5500, 5600, 5700, 5800, 5900, 6000, 6100, 6200, 6300, 6400, 6500, 6600, 6700, 6800, 6900, 7000, 7100, 7200, 7300, 7400,
    7500, 7600, 7700, 7800, 7900, 8000, 8100, 8200, 8300, 8400, 8500, 8600, 8700, 8800, 8900, 9000, 9100, 9200, 9300, 9400, 9500, 9600, 9700, 9800, 9900,
    10000
};

const uint16_t voltage[] = {
    3000, 3090, 3127, 3156, 3180, 3202, 3221, 3239, 3255, 3271, 3285, 3299, 3313, 3325, 3338, 3350, 3358, 3367, 3375, 3384, 3392, 3401, 3410, 3418, 3427,
    3435, 3444, 3452, 3461, 3470, 3478, 3487, 3495, 3504, 3512, 3521, 3530, 3538, 3547, 3555, 3564, 3572, 3581, 3590, 3598, 3607, 3615, 3624, 3632, 3641,
    3650, 3658, 3667, 3675, 3684, 3692, 3701, 3710, 3718, 3727, 3735, 3744, 3752, 3761, 3770, 3778, 3787, 3795, 3804, 3812, 3821, 3830, 3838, 3847, 3855,
    3864, 3872, 3881, 3890, 3898, 3907, 3915, 3924, 3932, 3941, 3950, 3953, 3959, 3969, 3980, 3993, 4007, 4023, 4041, 4060, 4080, 4102, 4124, 4148, 4173,
    4200
};

// Function to estimate SOC based on cell voltage
uint16_t estimateSOCFromCell(uint16_t cellVoltage) {
  if (cellVoltage >= voltage[0]) {
    return SOC[0];
  }
  if (cellVoltage <= voltage[numPoints - 1]) {
    return SOC[numPoints - 1];
  }

  for (int i = 1; i < numPoints; ++i) {
    if (cellVoltage >= voltage[i]) {
      // Cast to float for proper division
      float t = (float)(cellVoltage - voltage[i]) / (float)(voltage[i - 1] - voltage[i]);
      
      // Calculate interpolated SOC value
      uint16_t socDiff = SOC[i - 1] - SOC[i];
      uint16_t interpolatedValue = SOC[i] + (uint16_t)(t * socDiff);
      
      return interpolatedValue;
    }
  }
  return 0;  // Default return for safety, should never reach here
}

// Simplified version of the pack-based SOC estimation with compensation
uint16_t estimateSOC(uint16_t packVoltage, uint16_t cellCount, int16_t currentAmps) {
  if (cellCount == 0) return 0;
  
  // Convert pack voltage (decivolts) to millivolts
  uint32_t packVoltageMv = packVoltage * 100;
  
  // Apply internal resistance compensation
  // Current is in deciamps (-150 = -15.0A, 150 = 15.0A)
  // Resistance is in milliohms
  int32_t voltageDrop = (currentAmps * PACK_INTERNAL_RESISTANCE_MOHM) / 10;
  
  // Compensate the pack voltage (add the voltage drop)
  uint32_t compensatedPackVoltageMv = packVoltageMv + voltageDrop;
  
  // Calculate average cell voltage in millivolts
  uint16_t avgCellVoltage = compensatedPackVoltageMv / cellCount;
  
#ifdef DEBUG_LOG
  logging.print("Pack: ");
  logging.print(packVoltage / 10.0);
  logging.print("V, Current: ");
  logging.print(currentAmps / 10.0);
  logging.print("A, Drop: ");
  logging.print(voltageDrop / 1000.0);
  logging.print("V, Comp Pack: ");
  logging.print(compensatedPackVoltageMv / 1000.0);
  logging.print("V, Avg Cell: ");
  logging.print(avgCellVoltage);
  logging.println("mV");
#endif

  // Use the cell voltage lookup table to estimate SOC
  return estimateSOCFromCell(avgCellVoltage);
}

// Fix: Change parameter types to uint16_t to match SOC values
uint16_t selectSOC(uint16_t SOC_low, uint16_t SOC_high) {
  if (SOC_low == 0 || SOC_high == 0) {
    return 0;  // If either value is 0, return 0
  }
  if (SOC_low == 10000 || SOC_high == 10000) {
    return 10000;  // If either value is 100%, return 100%
  }
  return (SOC_low < SOC_high) ? SOC_low : SOC_high;  // Otherwise, return the lowest value
}

/* These messages are needed for contactor closing */
unsigned long startMillis;
uint8_t messageIndex = 0;
uint8_t messageDelays[63] = {0,   0,   5,   10,  10,  15,  19,  19,  20,  20,  25,  30,  30,  35,  40,  40,
                             45,  49,  49,  50,  50,  52,  53,  53,  54,  55,  60,  60,  65,  67,  67,  70,
                             70,  75,  77,  77,  80,  80,  85,  90,  90,  95,  100, 100, 105, 110, 110, 115,
                             119, 119, 120, 120, 125, 130, 130, 135, 140, 140, 145, 149, 149, 150, 150};
CAN_frame message_1 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x62, 0x36, 0x8c, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_2 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0xd4, 0x1b, 0x8c, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_3 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x24, 0x9b, 0x7b, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_4 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x24, 0x6f, 0x8d, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_5 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x92, 0x42, 0x8d, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_6 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0xd7, 0x05, 0x7c, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_7 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x30A,
    .data = {0xb1, 0xe0, 0x26, 0x08, 0x54, 0x01, 0x04, 0x15, 0x00, 0x1a, 0x76, 0x00, 0x25, 0x01, 0x10, 0x27,
             0x4f, 0x06, 0x18, 0x04, 0x33, 0x15, 0x34, 0x28, 0x00, 0x00, 0x10, 0x06, 0x21, 0x00, 0x4b, 0x06}};

CAN_frame message_8 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x320,
    .data = {0xc6, 0xab, 0x26, 0x41, 0x00, 0x00, 0x01, 0x3c, 0xac, 0x0d, 0x40, 0x20, 0x05, 0xc8, 0xa0, 0x03,
             0x40, 0x20, 0x2b, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_9 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0xee, 0x84, 0x8e, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_10 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x58, 0xa9, 0x8e, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};
CAN_frame message_11 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x91, 0x5c, 0x7d, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_12 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0xa8, 0xdd, 0x8f, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_13 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x1e, 0xf0, 0x8f, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_14 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x5b, 0xb7, 0x7e, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_15 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0xec, 0x6d, 0x90, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_16 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x5a, 0x40, 0x90, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_17 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x1d, 0xee, 0x7f, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_18 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x2B5,
    .data = {0xbd, 0xb2, 0x42, 0x00, 0x00, 0x00, 0x00, 0x80, 0x59, 0x00, 0x2b, 0x00, 0x00, 0x04, 0x00, 0x00,
             0xfa, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_19 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x2E0,
    .data = {0xc1, 0xf2, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x70, 0x01, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_20 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0xaa, 0x34, 0x91, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};
CAN_frame message_21 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x1c, 0x19, 0x91, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_22 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x2D5,
    .data = {0x79, 0xfb, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_23 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x2EA,
    .data = {0x6e, 0xbb, 0xa0, 0x0d, 0x04, 0x01, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0xc7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_24 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x306,
    .data = {0x00, 0x00, 0x00, 0xd2, 0x06, 0x92, 0x05, 0x34, 0x07, 0x8e, 0x08, 0x73, 0x05, 0x80, 0x05, 0x83,
             0x05, 0x73, 0x05, 0x80, 0x05, 0xed, 0x01, 0xdd, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_25 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x308,
    .data = {0xbe, 0x84, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
             0x75, 0x6c, 0x86, 0x0d, 0xfb, 0xdf, 0x03, 0x36, 0xc3, 0x86, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_26 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x6b, 0xa2, 0x80, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_27 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x60, 0xdf, 0x92, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_28 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0xd6, 0xf2, 0x92, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_29 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x2d, 0xfb, 0x81, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_30 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x33A,
    .data = {0x1a, 0x23, 0x26, 0x10, 0x27, 0x4f, 0x06, 0x00, 0xf8, 0x1b, 0x19, 0x04, 0x30, 0x01, 0x00, 0x06,
             0x00, 0x00, 0x00, 0x2e, 0x2d, 0x81, 0x25, 0x20, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
CAN_frame message_31 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x350,
    .data = {0x26, 0x82, 0x26, 0xf4, 0x01, 0x00, 0x00, 0x50, 0x90, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_32 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x26, 0x86, 0x93, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_33 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x90, 0xab, 0x93, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_34 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0xe7, 0x10, 0x82, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_35 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x2E5,
    .data = {0x69, 0x8a, 0x3f, 0x01, 0x00, 0x00, 0x00, 0x15, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_36 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x3B5,
    .data = {0xa3, 0xc8, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x36, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0xc7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x6a, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_37 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0xd5, 0x18, 0x94, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_38 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x63, 0x35, 0x94, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_39 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0xa1, 0x49, 0x83, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_40 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x93, 0x41, 0x95, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};
CAN_frame message_41 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x25, 0x6c, 0x95, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_42 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x52, 0xd7, 0x84, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_43 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x59, 0xaa, 0x96, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_44 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0xef, 0x87, 0x96, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_45 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x14, 0x8e, 0x85, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_46 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x1f, 0xf3, 0x97, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_47 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0xa9, 0xde, 0x97, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_48 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0xde, 0x65, 0x86, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_49 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x30A,
    .data = {0xd3, 0x11, 0x27, 0x08, 0x54, 0x01, 0x04, 0x15, 0x00, 0x1a, 0x76, 0x00, 0x25, 0x01, 0x10, 0x27,
             0x4f, 0x06, 0x19, 0x04, 0x33, 0x15, 0x34, 0x28, 0x00, 0x00, 0x10, 0x06, 0x21, 0x00, 0x4b, 0x06}};

CAN_frame message_50 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x320,
    .data = {0x80, 0xf2, 0x27, 0x41, 0x00, 0x00, 0x01, 0x3c, 0xac, 0x0d, 0x40, 0x20, 0x05, 0xc8, 0xa0, 0x03,
             0x40, 0x20, 0x2b, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
CAN_frame message_51 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x9e, 0x87, 0x98, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_52 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x28, 0xaa, 0x98, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_53 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x98, 0x3c, 0x87, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_54 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0xd8, 0xde, 0x99, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_55 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0x6e, 0xf3, 0x99, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_56 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x19, 0x48, 0x88, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_57 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x12, 0x35, 0x9a, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_58 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x120,
    .data = {0xa4, 0x18, 0x9a, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x37, 0x35, 0x37, 0x37,
             0xc9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_59 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x19A,
    .data = {0x5f, 0x11, 0x89, 0x55, 0x44, 0x64, 0xd8, 0x1b, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x52,
             0x00, 0x12, 0x02, 0x64, 0x00, 0x00, 0x00, 0x08, 0x13, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00}};

CAN_frame message_60 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x2B5,
    .data = {0xfb, 0xeb, 0x43, 0x00, 0x00, 0x00, 0x00, 0x80, 0x59, 0x00, 0x2b, 0x00, 0x00, 0x04, 0x00, 0x00,
             0xfa, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_61 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x2C0,
    .data = {0xcc, 0xcd, 0xa2, 0x21, 0x00, 0xa1, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7d, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_62 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x2E0,
    .data = {0x87, 0xab, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x70, 0x01, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

CAN_frame message_63 = {
    .FD = true,
    .ext_ID = false,
    .DLC = 32,
    .ID = 0x10A,
    .data = {0x54, 0x6c, 0x9b, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x01, 0x00, 0x00, 0x36, 0x39, 0x35, 0x35,
             0xc9, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x35, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}};
CAN_frame* messages[] = {&message_1,  &message_2,  &message_3,  &message_4,  &message_5,  &message_6,  &message_7,
                         &message_8,  &message_9,  &message_10, &message_11, &message_12, &message_13, &message_14,
                         &message_15, &message_16, &message_17, &message_18, &message_19, &message_20, &message_21,
                         &message_22, &message_23, &message_24, &message_25, &message_26, &message_27, &message_28,
                         &message_29, &message_30, &message_31, &message_32, &message_33, &message_34, &message_35,
                         &message_36, &message_37, &message_38, &message_39, &message_40, &message_41, &message_42,
                         &message_43, &message_44, &message_45, &message_46, &message_47, &message_48, &message_49,
                         &message_50, &message_51, &message_52, &message_53, &message_54, &message_55, &message_56,
                         &message_57, &message_58, &message_59, &message_60, &message_61, &message_62, &message_63};
/* PID polling messages */
CAN_frame EGMP_7E4 = {.FD = true,
                      .ext_ID = false,
                      .DLC = 8,
                      .ID = 0x7E4,
                      .data = {0x03, 0x22, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00}};  //Poll PID 03 22 01 01
CAN_frame EGMP_7E4_ack = {
    .FD = true,
    .ext_ID = false,
    .DLC = 8,
    .ID = 0x7E4,
    .data = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};  //Ack frame, correct PID is returned

void set_cell_voltages(CAN_frame rx_frame, int start, int length, int startCell) {
  for (size_t i = 0; i < length; i++) {
    if ((rx_frame.data.u8[start + i] * 20) > 1000) {
      datalayer.battery.status.cell_voltages_mV[startCell + i] = (rx_frame.data.u8[start + i] * 20);
    }
  }
}

void set_voltage_minmax_limits() {

  uint8_t valid_cell_count = 0;
  for (int i = 0; i < MAX_AMOUNT_CELLS; ++i) {
    if (datalayer.battery.status.cell_voltages_mV[i] > 0) {
      ++valid_cell_count;
    }
  }
  if (valid_cell_count == 144) {
    datalayer.battery.info.number_of_cells = valid_cell_count;
    datalayer.battery.info.max_design_voltage_dV = 6048;
    datalayer.battery.info.min_design_voltage_dV = 4320;
  } else if (valid_cell_count == 180) {
    datalayer.battery.info.number_of_cells = valid_cell_count;
    datalayer.battery.info.max_design_voltage_dV = 7560;
    datalayer.battery.info.min_design_voltage_dV = 5400;
  } else if (valid_cell_count == 192) {
    datalayer.battery.info.number_of_cells = valid_cell_count;
    datalayer.battery.info.max_design_voltage_dV = 8064;
    datalayer.battery.info.min_design_voltage_dV = 5760;
  } else {
    // We are still starting up? Not all cells available.
    set_voltage_limits = false;
  }
}

static uint8_t calculateCRC(CAN_frame rx_frame, uint8_t length, uint8_t initial_value) {
  uint8_t crc = initial_value;
  for (uint8_t j = 1; j < length; j++) {  //start at 1, since 0 is the CRC
    crc = crc8_table[(crc ^ static_cast<uint8_t>(rx_frame.data.u8[j])) % 256];
  }
  return crc;
}

void update_values_battery() {  //This function maps all the values fetched via CAN to the correct parameters used for modbus

#ifdef ESTIMATE_SOC_FROM_CELLVOLTAGE
  // Use the simplified pack-based SOC estimation with proper compensation
  datalayer.battery.status.real_soc = estimateSOC(batteryVoltage, datalayer.battery.info.number_of_cells, batteryAmps);
  
  // For comparison or fallback, we can still calculate from min/max cell voltages
  SOC_estimated_lowest = estimateSOCFromCell(CellVoltMin_mV);
  SOC_estimated_highest = estimateSOCFromCell(CellVoltMax_mV);
#else
  datalayer.battery.status.real_soc = (SOC_Display * 10);  //increase SOC range from 0-100.0 -> 100.00
#endif

  datalayer.battery.status.soh_pptt = (batterySOH * 10);  //Increase decimals from 100.0% -> 100.00%

  datalayer.battery.status.voltage_dV = batteryVoltage;  //value is *10 (3700 = 370.0)

  datalayer.battery.status.current_dA = -batteryAmps;  //value is *10 (150 = 15.0)

  datalayer.battery.status.remaining_capacity_Wh = static_cast<uint32_t>(
      (static_cast<double>(datalayer.battery.status.real_soc) / 10000) * datalayer.battery.info.total_capacity_Wh);

  //datalayer.battery.status.max_charge_power_W = (uint16_t)allowedChargePower * 10;  //From kW*100 to Watts
  //The allowed charge power is not available. We estimate this value for now
  if (datalayer.battery.status.real_soc > 9900) {
    datalayer.battery.status.max_charge_power_W = 0;
  } else if (datalayer.battery.status.real_soc >
             RAMPDOWN_SOC) {  // When real SOC is between 90-99%, ramp the value between Max<->0
    datalayer.battery.status.max_charge_power_W =
        RAMPDOWNPOWERALLOWED * (1 - (datalayer.battery.status.real_soc - RAMPDOWN_SOC) / (10000.0 - RAMPDOWN_SOC));
  } else {  // No limits, max charging power allowed
    datalayer.battery.status.max_charge_power_W = MAXCHARGEPOWERALLOWED;
  }

  //datalayer.battery.status.max_discharge_power_W = (uint16_t)allowedDischargePower * 10;  //From kW*100 to Watts
  //The allowed discharge power is not available. We hardcode this value for now
  datalayer.battery.status.max_discharge_power_W = MAXDISCHARGEPOWERALLOWED;

  datalayer.battery.status.temperature_min_dC = (int8_t)temperatureMin * 10;  //Increase decimals, 17C -> 17.0C

  datalayer.battery.status.temperature_max_dC = (int8_t)temperatureMax * 10;  //Increase decimals, 18C -> 18.0C

  datalayer.battery.status.cell_max_voltage_mV = CellVoltMax_mV;

  datalayer.battery.status.cell_min_voltage_mV = CellVoltMin_mV;

  if ((millis() > INTERVAL_60_S) && !set_voltage_limits) {
    set_voltage_limits = true;
    set_voltage_minmax_limits();  // Count cells, and set voltage limits accordingly
  }

  if (waterleakageSensor == 0) {
    set_event(EVENT_WATER_INGRESS, 0);
  }

  if (leadAcidBatteryVoltage < 110) {
    set_event(EVENT_12V_LOW, leadAcidBatteryVoltage);
  }

  /* Safeties verified. Perform USB serial printout if configured to do so */

#ifdef DEBUG_LOG
  logging.println();  //sepatator
  logging.println("Values from battery: ");
  logging.print("SOC BMS: ");
  logging.print((uint16_t)SOC_BMS / 10.0, 1);
  logging.print("%  |  SOC Display: ");
  logging.print((uint16_t)SOC_Display / 10.0, 1);
  logging.print("%  |  SOH ");
  logging.print((uint16_t)batterySOH / 10.0, 1);
  logging.println("%");
  logging.print((int16_t)batteryAmps / 10.0, 1);
  logging.print(" Amps  |  ");
  logging.print((uint16_t)batteryVoltage / 10.0, 1);
  logging.print(" Volts  |  ");
  logging.print((int16_t)datalayer.battery.status.active_power_W);
  logging.println(" Watts");
  logging.print("Allowed Charge ");
  logging.print((uint16_t)allowedChargePower * 10);
  logging.print(" W  |  Allowed Discharge ");
  logging.print((uint16_t)allowedDischargePower * 10);
  logging.println(" W");
  logging.print("MaxCellVolt ");
  logging.print(CellVoltMax_mV);
  logging.print(" mV  No  ");
  logging.print(CellVmaxNo);
  logging.print("  |  MinCellVolt ");
  logging.print(CellVoltMin_mV);
  logging.print(" mV  No  ");
  logging.println(CellVminNo);
  logging.print("TempHi ");
  logging.print((int16_t)temperatureMax);
  logging.print("°C  TempLo ");
  logging.print((int16_t)temperatureMin);
  logging.print("°C  WaterInlet ");
  logging.print((int8_t)temperature_water_inlet);
  logging.print("°C  PowerRelay ");
  logging.print((int8_t)powerRelayTemperature * 2);
  logging.println("°C");
  logging.print("Aux12volt: ");
  logging.print((int16_t)leadAcidBatteryVoltage / 10.0, 1);
  logging.println("V  |  ");
  logging.print("BmsManagementMode ");
  logging.print((uint8_t)batteryManagementMode, BIN);
  if (bitRead((uint8_t)BMS_ign, 2) == 1) {
    logging.print("  |  BmsIgnition ON");
  } else {
    logging.print("  |  BmsIgnition OFF");
  }

  if (bitRead((uint8_t)batteryRelay, 0) == 1) {
    logging.print("  |  PowerRelay ON");
  } else {
    logging.print("  |  PowerRelay OFF");
  }
  logging.print("  |  Inverter ");
  logging.print(inverterVoltage);
  logging.println(" Volts");
#endif
}

void handle_incoming_can_frame_battery(CAN_frame rx_frame) {
  startedUp = true;
  switch (rx_frame.ID) {
    case 0x055:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x150:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x1F5:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x215:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x21A:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x235:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x245:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x25A:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x275:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x2FA:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x325:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x330:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x335:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x360:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x365:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x3BA:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x3F5:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x7EC:
      // print_canfd_frame(frame);
      switch (rx_frame.data.u8[0]) {
        case 0x10:  //"PID Header"
          // logging.println ("Send ack");
          poll_data_pid = rx_frame.data.u8[4];
          // if (rx_frame.data.u8[4] == poll_data_pid) {
          transmit_can_frame(&EGMP_7E4_ack, can_config.battery);  //Send ack to BMS if the same frame is sent as polled
          // }
          break;
        case 0x21:  //First frame in PID group
          if (poll_data_pid == 1) {
            allowedChargePower = ((rx_frame.data.u8[3] << 8) + rx_frame.data.u8[4]);
            allowedDischargePower = ((rx_frame.data.u8[5] << 8) + rx_frame.data.u8[6]);
            SOC_BMS = rx_frame.data.u8[2] * 5;  //100% = 200 ( 200 * 5 = 1000 )

          } else if (poll_data_pid == 2) {
            // set cell voltages data, start bite, data length from start, start cell
            set_cell_voltages(rx_frame, 2, 6, 0);
          } else if (poll_data_pid == 3) {
            set_cell_voltages(rx_frame, 2, 6, 32);
          } else if (poll_data_pid == 4) {
            set_cell_voltages(rx_frame, 2, 6, 64);
          } else if (poll_data_pid == 0x0A) {
            set_cell_voltages(rx_frame, 2, 6, 96);
          } else if (poll_data_pid == 0x0B) {
            set_cell_voltages(rx_frame, 2, 6, 128);
          } else if (poll_data_pid == 0x0C) {
            set_cell_voltages(rx_frame, 2, 6, 160);
          }
          break;
        case 0x22:  //Second datarow in PID group
          if (poll_data_pid == 1) {
            batteryVoltage = (rx_frame.data.u8[3] << 8) + rx_frame.data.u8[4];
            batteryAmps = (rx_frame.data.u8[1] << 8) + rx_frame.data.u8[2];
            temperatureMax = rx_frame.data.u8[5];
            temperatureMin = rx_frame.data.u8[6];
            // temp1 = rx_frame.data.u8[7];
          } else if (poll_data_pid == 2) {
            set_cell_voltages(rx_frame, 1, 7, 6);
          } else if (poll_data_pid == 3) {
            set_cell_voltages(rx_frame, 1, 7, 38);
          } else if (poll_data_pid == 4) {
            set_cell_voltages(rx_frame, 1, 7, 70);
          } else if (poll_data_pid == 0x0A) {
            set_cell_voltages(rx_frame, 1, 7, 102);
          } else if (poll_data_pid == 0x0B) {
            set_cell_voltages(rx_frame, 1, 7, 134);
          } else if (poll_data_pid == 0x0C) {
            set_cell_voltages(rx_frame, 1, 7, 166);
          } else if (poll_data_pid == 6) {
            batteryManagementMode = rx_frame.data.u8[5];
          }
          break;
        case 0x23:  //Third datarow in PID group
          if (poll_data_pid == 1) {
            temperature_water_inlet = rx_frame.data.u8[6];
            CellVoltMax_mV = (rx_frame.data.u8[7] * 20);  //(volts *50) *20 =mV
            // temp2 = rx_frame.data.u8[1];
            // temp3 = rx_frame.data.u8[2];
            // temp4 = rx_frame.data.u8[3];
          } else if (poll_data_pid == 2) {
            set_cell_voltages(rx_frame, 1, 7, 13);
          } else if (poll_data_pid == 3) {
            set_cell_voltages(rx_frame, 1, 7, 45);
          } else if (poll_data_pid == 4) {
            set_cell_voltages(rx_frame, 1, 7, 77);
          } else if (poll_data_pid == 0x0A) {
            set_cell_voltages(rx_frame, 1, 7, 109);
          } else if (poll_data_pid == 0x0B) {
            set_cell_voltages(rx_frame, 1, 7, 141);
          } else if (poll_data_pid == 0x0C) {
            set_cell_voltages(rx_frame, 1, 7, 173);
          } else if (poll_data_pid == 5) {
            // ac = rx_frame.data.u8[3];
            // Vdiff = rx_frame.data.u8[4];

            // airbag = rx_frame.data.u8[6];
            heatertemp = rx_frame.data.u8[7];
          }
          break;
        case 0x24:  //Fourth datarow in PID group
          if (poll_data_pid == 1) {
            CellVmaxNo = rx_frame.data.u8[1];
            CellVoltMin_mV = (rx_frame.data.u8[2] * 20);  //(volts *50) *20 =mV
            CellVminNo = rx_frame.data.u8[3];
            // fanMod = rx_frame.data.u8[4];
            // fanSpeed = rx_frame.data.u8[5];
            leadAcidBatteryVoltage = rx_frame.data.u8[6];  //12v Battery Volts
            //cumulative_charge_current[0] = rx_frame.data.u8[7];
          } else if (poll_data_pid == 2) {
            set_cell_voltages(rx_frame, 1, 7, 20);
          } else if (poll_data_pid == 3) {
            set_cell_voltages(rx_frame, 1, 7, 52);
          } else if (poll_data_pid == 4) {
            set_cell_voltages(rx_frame, 1, 7, 84);
          } else if (poll_data_pid == 0x0A) {
            set_cell_voltages(rx_frame, 1, 7, 116);
          } else if (poll_data_pid == 0x0B) {
            set_cell_voltages(rx_frame, 1, 7, 148);
          } else if (poll_data_pid == 0x0C) {
            set_cell_voltages(rx_frame, 1, 7, 180);
          } else if (poll_data_pid == 5) {
            batterySOH = ((rx_frame.data.u8[2] << 8) + rx_frame.data.u8[3]);
            // maxDetCell = rx_frame.data.u8[4];
            // minDet = (rx_frame.data.u8[5] << 8) + rx_frame.data.u8[6];
            // minDetCell = rx_frame.data.u8[7];
          }
          break;
        case 0x25:  //Fifth datarow in PID group
          if (poll_data_pid == 1) {
            //cumulative_charge_current[1] = rx_frame.data.u8[1];
            //cumulative_charge_current[2] = rx_frame.data.u8[2];
            //cumulative_charge_current[3] = rx_frame.data.u8[3];
            //cumulative_discharge_current[0] = rx_frame.data.u8[4];
            //cumulative_discharge_current[1] = rx_frame.data.u8[5];
            //cumulative_discharge_current[2] = rx_frame.data.u8[6];
            //cumulative_discharge_current[3] = rx_frame.data.u8[7];
            //set_cumulative_charge_current();
            //set_cumulative_discharge_current();
          } else if (poll_data_pid == 2) {
            set_cell_voltages(rx_frame, 1, 5, 27);
          } else if (poll_data_pid == 3) {
            set_cell_voltages(rx_frame, 1, 5, 59);
          } else if (poll_data_pid == 4) {
            set_cell_voltages(rx_frame, 1, 5, 91);
          } else if (poll_data_pid == 0x0A) {
            set_cell_voltages(rx_frame, 1, 5, 123);
          } else if (poll_data_pid == 0x0B) {
            set_cell_voltages(rx_frame, 1, 5, 155);
          } else if (poll_data_pid == 0x0C) {
            set_cell_voltages(rx_frame, 1, 5, 187);
            //set_cell_count();
          } else if (poll_data_pid == 5) {
            // datalayer.battery.info.number_of_cells = 98;
            SOC_Display = rx_frame.data.u8[1] * 5;
          }
          break;
        case 0x26:  //Sixth datarow in PID group
          if (poll_data_pid == 1) {
            //cumulative_energy_charged[0] = rx_frame.data.u8[1];
            // cumulative_energy_charged[1] = rx_frame.data.u8[2];
            //cumulative_energy_charged[2] = rx_frame.data.u8[3];
            //cumulative_energy_charged[3] = rx_frame.data.u8[4];
            //cumulative_energy_discharged[0] = rx_frame.data.u8[5];
            //cumulative_energy_discharged[1] = rx_frame.data.u8[6];
            //cumulative_energy_discharged[2] = rx_frame.data.u8[7];
            // set_cumulative_energy_charged();
          }
          break;
        case 0x27:  //Seventh datarow in PID group
          if (poll_data_pid == 1) {
            //cumulative_energy_discharged[3] = rx_frame.data.u8[1];

            //opTimeBytes[0] = rx_frame.data.u8[2];
            //opTimeBytes[1] = rx_frame.data.u8[3];
            //opTimeBytes[2] = rx_frame.data.u8[4];
            //opTimeBytes[3] = rx_frame.data.u8[5];

            BMS_ign = rx_frame.data.u8[6];
            inverterVoltageFrameHigh = rx_frame.data.u8[7];  // BMS Capacitoir

            // set_cumulative_energy_discharged();
            // set_opTime();
          }
          break;
        case 0x28:  //Eighth datarow in PID group
          if (poll_data_pid == 1) {
            inverterVoltage = (inverterVoltageFrameHigh << 8) + rx_frame.data.u8[1];  // BMS Capacitoir
          }
          break;
      }
      break;
    default:
      break;
  }
}

void transmit_can_battery() {
  unsigned long currentMillis = millis();
  if (startedUp) {
    //Send Contactor closing message loop
    // Check if we still have messages to send
    if (messageIndex < sizeof(messageDelays) / sizeof(messageDelays[0])) {

      // Check if it's time to send the next message
      if (currentMillis - startMillis >= messageDelays[messageIndex]) {

        // Transmit the current message
        transmit_can_frame(messages[messageIndex], can_config.battery);

        // Move to the next message
        messageIndex++;
      }
    }

    if (messageIndex >= 63) {
      startMillis = millis();  // Start over!
      messageIndex = 0;
    }

    //Send 200ms CANFD message
    if (currentMillis - previousMillis200ms >= INTERVAL_200_MS) {
      previousMillis200ms = currentMillis;
      // Check if sending of CAN messages has been delayed too much.
      if ((currentMillis - previousMillis200ms >= INTERVAL_200_MS_DELAYED) && (currentMillis > BOOTUP_TIME)) {
        set_event(EVENT_CAN_OVERRUN, (currentMillis - previousMillis200ms));
      } else {
        clear_event(EVENT_CAN_OVERRUN);
      }
      previousMillis200ms = currentMillis;

      EGMP_7E4.data.u8[3] = KIA_7E4_COUNTER;

      if (ok_start_polling_battery) {
        transmit_can_frame(&EGMP_7E4, can_config.battery);
      }

      KIA_7E4_COUNTER++;
      if (KIA_7E4_COUNTER > 0x0D) {  // gets up to 0x010C before repeating
        KIA_7E4_COUNTER = 0x01;
      }
    }
    //Send 10s CANFD message
    if (currentMillis - previousMillis10s >= INTERVAL_10_S) {
      previousMillis10s = currentMillis;

      ok_start_polling_battery = true;
    }
  }
}

void setup_battery(void) {  // Performs one time setup at startup
  strncpy(datalayer.system.info.battery_protocol, "Kia/Hyundai EGMP platform", 63);
  datalayer.system.info.battery_protocol[63] = '\0';

  startMillis = millis();  // Record the starting time

  datalayer.system.status.battery_allows_contactor_closing = true;
  datalayer.battery.info.number_of_cells = 192;  // TODO: will vary depending on battery
  datalayer.battery.info.max_design_voltage_dV = MAX_PACK_VOLTAGE_DV;
  datalayer.battery.info.min_design_voltage_dV = MIN_PACK_VOLTAGE_DV;
  datalayer.battery.info.max_cell_voltage_mV = MAX_CELL_VOLTAGE_MV;
  datalayer.battery.info.min_cell_voltage_mV = MIN_CELL_VOLTAGE_MV;
  datalayer.battery.info.max_cell_voltage_deviation_mV = MAX_CELL_DEVIATION_MV;
}

#endif
