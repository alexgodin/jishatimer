/*
 * LIS3DH Orientation Detection with Deep Sleep
 *
 * This program uses the LIS3DH 3-axis accelerometer to detect device orientation
 * changes and enter deep sleep mode when inactive. The device wakes from sleep
 * when the orientation changes.
 *
 * Key Features:
 * - 6D orientation detection (detects which face is up)
 * - Hardware interrupt-based orientation change detection
 * - Auto deep sleep after 5 seconds of inactivity
 * - Wake on orientation change using GPIO interrupt
 */

#include <Wire.h>
#include <Arduino.h>
#include <esp_sleep.h>
#include "accelerometer.h"
#include "time_manager.h"
#include "display.h"
#include "battery.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// GPIO pin connected to LIS3DH INT1 output
#define LIS3DH_INT_PIN        D1

// Time of inactivity before entering deep sleep (minutes)
const int SLEEP_TIMEOUT_MINUTES = 15;

// Flag set by ISR when orientation change interrupt fires
volatile bool orientationChanged = false;

// Low battery display state
bool showBattery = false;

// Cached orientation (updated only on change)
uint8_t currentOrientation = 0;


void IRAM_ATTR orientationISR() {
  orientationChanged = true;
}

void enterDeepSleep() {
  Serial.println("\n=== Entering deep sleep ===");

  // Detach interrupt handler before sleep
  detachInterrupt(digitalPinToInterrupt(LIS3DH_INT_PIN));

  // Read current orientation and mask it to prevent immediate re-wake
  currentOrientation = lis3dh_getOrientation();
  Serial.printf("Current: %s\n", lis3dh_decodeOrientation(currentOrientation).c_str());

  lis3dh_maskOrientation(currentOrientation);

  delay(500);

  // Configure wake on GPIO HIGH level (when LIS3DH triggers interrupt)
  esp_deep_sleep_enable_gpio_wakeup(BIT(LIS3DH_INT_PIN), ESP_GPIO_WAKEUP_GPIO_HIGH);

  Serial.println("Sleeping now...\n");
  Serial.flush();
  delay(50);

  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);

  // Initialize I2C
  Wire.begin();
  delay(100);

  // Initialize display first and show splash while WiFi connects
  connect_display();
  displaySplash();

  // Initialize RTC and time management (may block for WiFi/NTP)
  setupTime();

  // Initialize accelerometer and configure interrupt
  currentOrientation = lis3dh_setupInterrupt(LIS3DH_INT_PIN, orientationISR);
  resetElapsedTimer();
  Serial.println("Activity timer started\n");
}

// ============================================================================
// DISPLAY STATE HELPERS
// ============================================================================

const char* getDisplayStateName(int elapsed) {
  if (showBattery && elapsed < 15) return "BATTERY";
  if (elapsed < 60) return "TIME";
  if (elapsed < SLEEP_TIMEOUT_MINUTES * 60) return "ELAPSED_MINUTES";
  return "SLEEPING";
}

// ============================================================================
// MAIN LOOP
// ============================================================================

// Track previous state for transition logging
static const char* prevStateName = nullptr;

void loop() {
  unsigned long now = millis();

  // Serial debug commands
  while (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'T') { debugCorruptTime(); break; }
    if (cmd == 'S') {
      bool ok = syncTimeWithRetry();
      Serial.printf("NTP sync %s\n", ok ? "succeeded" : "FAILED");
      break;
    }
    if (cmd == 'E') {
      int e = getElapsedSeconds();
      Serial.printf("STATE: %s elapsed=%ds\n", getDisplayStateName(e), e);
      break;
    }
    if (cmd == 'R') {
      resetElapsedTimer();
      int e = getElapsedSeconds();
      Serial.printf("RESET: timer reset, state=%s elapsed=%ds\n", getDisplayStateName(e), e);
      break;
    }
    if (cmd == 'F') {
      // Read number from serial (e.g. "F65\n")
      delay(50); // wait for digits to arrive
      String numStr = "";
      while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') break;
        numStr += c;
      }
      int secs = numStr.toInt();
      debugSetElapsed(secs);
      int e = getElapsedSeconds();
      Serial.printf("FASTFWD: elapsed=%ds state=%s\n", e, getDisplayStateName(e));
      break;
    }
    if (cmd == 'B') {
      float pct = getBatteryPercent();
      Serial.printf("BATTERY: %.0f%%\n", pct);
      break;
    }
    if (cmd == 'Q') {
      String hour, minute;
      getCurrentTime(hour, minute);
      Serial.printf("TIME: %s:%s\n", hour.c_str(), minute.c_str());
      break;
    }
    if (cmd == 'D') {
      int sleepAt = SLEEP_TIMEOUT_MINUTES * 60;
      Serial.printf("DRYSLEEP: would sleep at elapsed=%ds\n", sleepAt);
      break;
    }
    if (cmd == 'W') {
      debugWifiFail = true;
      bool ok = syncTimeFromNtp();
      Serial.printf("NTP sync %s\n", ok ? "succeeded" : "FAILED");
      break;
    }
    if (cmd == 'N') {
      debugNtpTimeout = true;
      bool ok = syncTimeFromNtp();
      Serial.printf("NTP sync %s\n", ok ? "succeeded" : "FAILED");
      break;
    }
    if (cmd == 'L') {
      // Backdate lastNtpSync so calibration path runs (elapsed > 3600s)
      debugSetLastNtpSync(time(nullptr) - 7200);
      bool ok = syncTimeWithRetry();
      Serial.printf("NTP sync %s\n", ok ? "succeeded" : "FAILED");
      break;
    }
  }

  // Track and display elapsed time
  int elapsed = getElapsedSeconds();

  // Log state transitions
  const char* currentStateName = getDisplayStateName(elapsed);
  if (prevStateName == nullptr) {
    prevStateName = currentStateName;
    Serial.printf("TRANSITION: BOOT -> %s at elapsed=%ds\n", currentStateName, elapsed);
  } else if (prevStateName != currentStateName) {
    Serial.printf("TRANSITION: %s -> %s at elapsed=%ds\n", prevStateName, currentStateName, elapsed);
    prevStateName = currentStateName;
  }

  if (showBattery && elapsed < 15) {
    displayBatteryLow(currentOrientation);
  } else {
    showBattery = false;
    if (elapsed < 60) {
      String hour, minute;
      getCurrentTime(hour, minute);
      displayTime(hour, minute, elapsed, currentOrientation);
    } else {
      displayElapsedMinutes(elapsed, currentOrientation);
    }
  }

  // Process orientation changes
  if (orientationChanged) {
    uint8_t prev, curr;
    if (lis3dh_processOrientationChange(orientationChanged, &prev, &curr)) {
      Serial.printf("\n[%lu ms] Orientation change\n", now);
      Serial.printf("  From: %s\n", lis3dh_decodeOrientation(prev).c_str());
      Serial.printf("  To:   %s\n", lis3dh_decodeOrientation(curr).c_str());
      Serial.println("  Status: Valid - sleep timer reset\n");
      currentOrientation = curr;
      resetElapsedTimer();
      if (getBatteryPercent() < 30.0f) {
        showBattery = true;
      }
      clearInactiveDisplay(curr);  // Clear the inactive display
    }
  }

  // Check for sleep timeout
  if (elapsed >= (SLEEP_TIMEOUT_MINUTES * 60)) {
    Serial.printf("\n[%lu ms] No activity for %d seconds - entering deep sleep\n",
                  now, elapsed);
    clearDisplay();
    enterDeepSleep();
    // Never returns - device will wake and reboot
  }

  delay(10);
}
