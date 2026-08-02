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

  // Charge detection must be configured before setupTime() (which reads
  // isCharging() to decide whether to sync)
  setupChargeDetection();

  connect_display();

  // Splash only shows while charging (D7 high). If already charging at
  // boot, draw it now so it's visible during setupTime()'s blocking sync
  // below; loop() takes over redrawing it every tick after that.
  if (isCharging()) {
    displaySplash(getDisplayBatteryPercent(), syncedRecently(), false);
  }

  // Before setupTime(), which blocks for up to ~47s on a charge-start or
  // recovery NTP sync. Flips arriving during that window were dropped on the
  // floor with the interrupt attached afterwards; nothing here needs the
  // clock, so there is no reason to wait.
  currentOrientation = lis3dh_setupInterrupt(LIS3DH_INT_PIN, orientationISR);

  // Initialize RTC and time management (may block for WiFi/NTP)
  setupTime();

  resetElapsedTimer();
  Serial.println("Activity timer started\n");
}

// ============================================================================
// DISPLAY STATE HELPERS
// ============================================================================

const char* getDisplayStateName(int elapsed, bool charging) {
  if (charging) return "CHARGING";
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
      Serial.printf("STATE: %s elapsed=%ds\n", getDisplayStateName(e, isCharging()), e);
      break;
    }
    if (cmd == 'R') {
      resetElapsedTimer();
      int e = getElapsedSeconds();
      Serial.printf("RESET: timer reset, state=%s elapsed=%ds\n", getDisplayStateName(e, isCharging()), e);
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
      Serial.printf("FASTFWD: elapsed=%ds state=%s\n", e, getDisplayStateName(e, isCharging()));
      break;
    }
    if (cmd == 'B') {
      float pct = getBatteryPercent();
      Serial.printf("BATTERY: %.0f%%\n", pct);
      break;
    }
    if (cmd == 'C') {
      Serial.printf("CHARGING: %s SYNC: %s\n", isCharging() ? "yes" : "no",
                     didLastSyncFail() ? "failed" : "ok");
      break;
    }
    if (cmd == 'X') {
      // Probe SH1106 OLED on I2C at 0x3C and 0x3D.
      // Catches "screen shows nothing" caused by wiring/address/power.
      bool found = false;
      for (uint8_t addr : {(uint8_t)0x3C, (uint8_t)0x3D}) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
          Serial.printf("DISPLAY: ok addr=0x%02X\n", addr);
          found = true;
          break;
        }
      }
      if (!found) {
        Serial.println("DISPLAY: not_found addr=0x3C,0x3D");
      }
      break;
    }
    if (cmd == 'A') {
      // Probe the LIS3DH. Its init failure is non-fatal — the firmware warns
      // once on the boot log and carries on — so without a queryable status
      // a device whose orientation input is dead passes every other test
      // here while silently never resetting the timer on a flip.
      if (lis3dh_isPresent()) {
        Serial.printf("ACCEL: ok addr=0x%02X\n", lis3dh_address());
      } else {
        Serial.printf("ACCEL: not_found addr=0x%02X,0x%02X\n",
                      LIS3DH_I2C_ADDR_SDO_LOW, LIS3DH_I2C_ADDR_SDO_HIGH);
      }
      break;
    }
    if (cmd == 'V') {
      // RTC health. Distinguishes "crystal drifting" from "part lost its
      // count", which the calibration path must never confuse.
      bool osStopped, configured;
      getRtcBootState(osStopped, configured);
      Serial.printf("RTCHEALTH: present=%s valid=%s boot_oscillator_stopped=%d "
                    "boot_configured=%d offset=%d\n",
                    rtcIsPresent() ? "yes" : "no",
                    timeIsValid() ? "yes" : "no", osStopped, configured,
                    readCalibrationOffset());
      break;
    }
    if (cmd == 'Z') {
      // Discard a stored correction. Needed after a bad calibration is
      // written, since nothing recomputes the offset until a sync lands with
      // a long enough window.
      clearCalibration();
      Serial.printf("CALCLEAR: offset=%d\n", readCalibrationOffset());
      break;
    }
    if (cmd == 'I') {
      // Full I2C bus scan: prints every address that ACKs.
      Serial.println("I2C: scanning 0x08..0x77");
      int count = 0;
      for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          Serial.printf("I2C:   0x%02X ACK\n", addr);
          count++;
        }
      }
      Serial.printf("I2C: %d device(s) found\n", count);
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
      // Backdate lastNtpSync past MIN_ELAPSED_SECONDS so the calibration path
      // runs. Seven days, not the old two hours: at 2h the RTC's 1s counting
      // resolution alone reads as 139 ppm, so the gate now rejects it.
      //
      // Dry run, because the window is fabricated: the RTC's real error
      // accumulated over however long since its last actual sync, so dividing
      // it by 7 days yields a number that must not reach the offset register.
      // Without this the suite miscalibrated the part it was testing.
      debugCalibrationDryRun = true;
      debugBackdateLastNtpSync(7L * 24 * 3600);
      bool ok = syncTimeWithRetry();
      debugCalibrationDryRun = false;
      // On success the sync already overwrote lastNtpSync with the real time.
      if (!ok) debugRestoreLastNtpSync();
      Serial.printf("NTP sync %s\n", ok ? "succeeded" : "FAILED");
      break;
    }
  }

  // Track and display elapsed time
  int elapsed = getElapsedSeconds();
  bool charging = isCharging();

  // Log state transitions
  const char* currentStateName = getDisplayStateName(elapsed, charging);
  bool enteringCharging = (prevStateName != currentStateName && currentStateName == "CHARGING");
  if (prevStateName == nullptr) {
    prevStateName = currentStateName;
    Serial.printf("TRANSITION: BOOT -> %s at elapsed=%ds\n", currentStateName, elapsed);
  } else if (prevStateName != currentStateName) {
    Serial.printf("TRANSITION: %s -> %s at elapsed=%ds\n", prevStateName, currentStateName, elapsed);
    prevStateName = currentStateName;
  }

  if (charging) {
    displaySplash(getDisplayBatteryPercent(), syncedRecently(), enteringCharging);
  } else if (showBattery && elapsed < 15) {
    displayBatteryLow();
  } else {
    showBattery = false;
    if (elapsed < 60) {
      String hour, minute;
      getCurrentTime(hour, minute);
      displayTime(hour, minute, elapsed);
    } else {
      displayElapsedMinutes(elapsed);
    }
  }

  // Charge-triggered NTP sync can block for tens of seconds (WiFi connect
  // + NTP wait, up to 3 retries) — run it after the display update above,
  // not before, so a charge-start edge shows the splash immediately
  // instead of leaving the previous screen up for the whole sync.
  checkChargeSync(charging);

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
