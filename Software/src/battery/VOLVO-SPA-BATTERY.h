#ifndef VOLVO_SPA_BATTERY_H
#define VOLVO_SPA_BATTERY_H
#include <Arduino.h>
#include "../include.h"

#define BATTERY_SELECTED
#define MAX_PACK_VOLTAGE_108S_DV 4540
#define MIN_PACK_VOLTAGE_108S_DV 2938
#define MAX_PACK_VOLTAGE_96S_DV 4030
#define MIN_PACK_VOLTAGE_96S_DV 2620
#define MAX_CELL_DEVIATION_MV 250
#define MAX_CELL_VOLTAGE_MV 4210  //Battery is put into emergency stop if one cell goes over this value
#define MIN_CELL_VOLTAGE_MV 2700  //Battery is put into emergency stop if one cell goes below this value

class VolvoSpaBattery : public CanBattery {
 public:
  VolvoSpaBattery() : CanBattery(VolvoSpa) {}
  virtual const char* name() { return Name; };
  static constexpr char* Name = "Volvo / Polestar 69/78kWh SPA battery";
  virtual void setup();
  virtual void update_values();
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void transmit_can();
};

#endif
