#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Initialize the top IS31FL3731 LED matrix at 0x76
void connect_display();

// Display time on LED matrix
void displayTime(String hour, String minute, int elapsedSeconds);

// Display minutes with a progress bar
void displayElapsedMinutes(int elapsedSeconds);

//clear display
void clearDisplay();

// Display blinking low-battery icon
void displayBatteryLow();

// Display the logo splash screen with battery percentage. Shown whenever
// the device is charging (not just at boot). Draws a border when
// syncedRecently is true (debug signal, not shown to distract users when
// false). forceRedraw bypasses the dedupe cache (use on state entry).
void displaySplash(float batteryPercent, bool syncedRecently, bool forceRedraw);

#endif
