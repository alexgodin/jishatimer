#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>

// Initialize RTC and timezone
void setupTime();

// Get current hour and minute separately (12-hour format)
void getCurrentTime(String &hour, String &minute);

// Reset elapsed timer to 0 (also used for initial start)
void resetElapsedTimer();

// Get current elapsed seconds
int getElapsedSeconds();

// Test: set RTC to obviously wrong time and reset sync counter
void debugCorruptTime();

// Force an NTP sync now (without corrupting RTC)
bool syncTimeFromNtp();

// Sync with retry (default 3 attempts)
bool syncTimeWithRetry(int maxAttempts = 3);

// Run an NTP sync exactly on a charge-start edge (charging && !wasCharging)
void checkChargeSync(bool charging);

// Whether the last charge-triggered sync attempt failed
bool didLastSyncFail();

// Whether the RTC was successfully NTP-synced within the last 24 hours
// (debug indicator, not tied to the charge-trigger logic)
bool syncedRecently();

// Whether the RTC holds a time worth believing. False when the oscillator
// stopped, or when the count is a reset default rather than a real clock —
// getCurrentTime() then falls back to the system clock or reports "--".
bool timeIsValid();

// Whether rtc.begin() found the part on the bus. Without this the boot-state
// flags below are indistinguishable from a genuine all-zero reading, and an
// unwired RTC gets diagnosed as a dead backup cell.
bool rtcIsPresent();

// RTC chip state as found at boot, before setup reconfigures it. Diagnostic:
// distinguishes "backup power not holding" from "oscillator faulty".
// Only meaningful when rtcIsPresent().
void getRtcBootState(bool &osStopped, bool &configured);

// Current value of the PCF8523 offset register (7-bit two's complement).
int readCalibrationOffset();

// Reset the offset register to 0, discarding any stored correction.
void clearCalibration();

// Debug/test functions
void debugSetElapsed(int seconds);

// Backdate the last-sync marker by `seconds`, relative to the RTC, so the
// next sync takes the calibration path.
void debugBackdateLastNtpSync(long seconds);

// Undo the most recent backdate. Must be called if the sync that was supposed
// to consume it failed, or the fabricated window is left armed for the next
// real sync to calibrate from.
void debugRestoreLastNtpSync();

// Fault injection flags (one-shot, reset after use)
extern bool debugWifiFail;
extern bool debugNtpTimeout;

// Measure and log the calibration but do not write the offset register. The
// backdated window a debug sync runs on is fabricated, so its ppm is not a
// real crystal measurement and must never reach hardware. Caller clears it.
extern bool debugCalibrationDryRun;

#endif
