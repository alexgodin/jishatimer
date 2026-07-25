#ifndef BATTERY_H
#define BATTERY_H

float getBatteryPercent();

// Throttled battery read for continuously-redrawn displays: caches the
// result and only re-checks every couple minutes, since battery % can't
// meaningfully change faster than that.
float getDisplayBatteryPercent();

// Configure the charge-status pin (call once from setup())
void setupChargeDetection();

// Read whether the device is currently charging
bool isCharging();

#endif
