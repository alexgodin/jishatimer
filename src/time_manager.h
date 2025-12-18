#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>

// Initialize time system from RTC
void setupTime();

// Sync ezTime from RTC (call on boot or periodically)
void syncEzTimeFromRTC();

// Get current hour and minute separately (military time)
void getCurrentTime(String &hour, String &minute);

// Setup elapsed time timer
void setupElapsedTimer();

// Reset elapsed timer to 0
void resetElapsedTimer();

// Get current elapsed seconds
int getElapsedSeconds();

#endif
