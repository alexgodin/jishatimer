#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>

// Initialize time system from RTC
void setupTime();

// Get current hour and minute separately (12-hour format)
void getCurrentTime(String &hour, String &minute);

// Setup elapsed time timer
void setupElapsedTimer();

// Reset elapsed timer to 0
void resetElapsedTimer();

// Get current elapsed seconds
int getElapsedSeconds();

#endif
