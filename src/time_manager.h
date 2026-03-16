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

// Debug/test functions
void debugSetElapsed(int seconds);
void debugSetLastNtpSync(time_t epoch);

// Fault injection flags (one-shot, reset after use)
extern bool debugWifiFail;
extern bool debugNtpTimeout;

#endif
