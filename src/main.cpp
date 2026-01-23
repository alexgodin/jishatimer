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

// ============================================================================
// CONFIGURATION
// ============================================================================

// GPIO pin connected to LIS3DH INT1 output
#define LIS3DH_INT_PIN        D1

// Minimum time between orientation changes (milliseconds)
const unsigned long ORIENTATION_DEBOUNCE_MS = 200;

// Time of inactivity before entering deep sleep (minutes)
const int SLEEP_TIMEOUT_MINUTES = 15;

// Flag set by ISR when orientation change interrupt fires
volatile bool orientationChanged = false;


void IRAM_ATTR orientationISR() {
  orientationChanged = true;
}

void enterDeepSleep() {
  Serial.println("\n=== Entering deep sleep ===");

  // Detach interrupt handler before sleep
  detachInterrupt(digitalPinToInterrupt(LIS3DH_INT_PIN));

  // Read current orientation and mask it to prevent immediate re-wake
  uint8_t currentOrientation = lis3dh_getOrientation();
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

  // Initialize RTC and time management
  setupTime();

  // Initialize accelerometer and configure interrupt
  lis3dh_setupInterrupt(LIS3DH_INT_PIN, orientationISR);

  // Initialize display and activity timer
  connect_display();
  setupElapsedTimer();
  Serial.println("Activity timer started - will sleep after 20 seconds of inactivity\n");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  unsigned long now = millis();

  // Get current orientation for display selection
  uint8_t currentOrientation = lis3dh_getOrientation();

  // Track and display elapsed time
  int elapsed = getElapsedSeconds();
  if (elapsed < 60) {
    String hour, minute;
    getCurrentTime(hour, minute);
    displayTime(hour, minute, elapsed, currentOrientation);
  } else {
    displayElapsedMinutes(elapsed, currentOrientation);
  }

  // Process orientation changes
  if (orientationChanged) {
    uint8_t prev, curr;
    if (lis3dh_processOrientationChange(orientationChanged, &prev, &curr)) {
      Serial.printf("\n[%lu ms] Orientation change\n", now);
      Serial.printf("  From: %s\n", lis3dh_decodeOrientation(prev).c_str());
      Serial.printf("  To:   %s\n", lis3dh_decodeOrientation(curr).c_str());
      Serial.println("  Status: Valid - sleep timer reset\n");
      resetElapsedTimer();
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
