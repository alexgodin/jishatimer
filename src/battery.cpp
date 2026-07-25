#include <Arduino.h>
#include "battery.h"

#define CHARGE_STATUS_PIN D7

void setupChargeDetection() {
  pinMode(CHARGE_STATUS_PIN, INPUT);
}

bool isCharging() {
  return digitalRead(CHARGE_STATUS_PIN) == HIGH;
}

float getBatteryPercent() {
  uint32_t Vbatt = 0;
  for(int i = 0; i < 16; i++) {
    Vbatt = Vbatt + analogReadMilliVolts(A0); // ADC with correction
  }
  float Vbattf = 2 * Vbatt / 16 / 1000.0;     // attenuation ratio 1/2, mV --> V

  // Convert voltage to battery percentage
  // Typical Li-ion battery: 4.2V = 100%, 3.0V = 0%
  float batteryPercent = ((Vbattf - 3.0) / (4.2 - 3.0)) * 100.0;

  batteryPercent = constrain(batteryPercent, 0.0f, 100.0f);

  return batteryPercent;
}

float getDisplayBatteryPercent() {
  static float cached = -1.0f;
  static unsigned long lastCheck = 0;
  const unsigned long CHECK_INTERVAL_MS = 2UL * 60 * 1000;  // 2 minutes

  unsigned long now = millis();
  if (cached < 0.0f || (now - lastCheck) >= CHECK_INTERVAL_MS) {
    cached = getBatteryPercent();
    lastCheck = now;
  }

  return cached;
}
