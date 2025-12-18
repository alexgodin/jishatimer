#include <Arduino.h>
#include "battery.h"

float getBatteryPercent() {
  uint32_t Vbatt = 0;
  for(int i = 0; i < 16; i++) {
    Vbatt = Vbatt + analogReadMilliVolts(A0); // ADC with correction
  }
  float Vbattf = 2 * Vbatt / 16 / 1000.0;     // attenuation ratio 1/2, mV --> V

  // Convert voltage to battery percentage
  // Typical Li-ion battery: 4.2V = 100%, 3.0V = 0%
  float batteryPercent = ((Vbattf - 3.0) / (4.2 - 3.0)) * 100.0;

  // Constrain percentage between 0 and 100
  if (batteryPercent > 100) batteryPercent = 100;
  if (batteryPercent < 0) batteryPercent = 0;

  return batteryPercent;
}
