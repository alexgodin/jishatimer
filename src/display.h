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

// Display "NY" / "ZC" splash screen
void displaySplash();

#endif
