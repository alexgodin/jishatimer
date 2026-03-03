#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>

// Initialize RTC and timezone
void setupTime();

// Get current hour and minute separately (12-hour format)
void getCurrentTime(String &hour, String &minute);

// Setup elapsed time timer
void startElapsedTimer();

// Reset elapsed timer to 0
void resetElapsedTimer();

// Get current elapsed seconds
int getElapsedSeconds();

// Test: set RTC to obviously wrong time and reset sync counter
void debugCorruptTime();

#endif
