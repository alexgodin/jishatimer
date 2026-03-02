#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Initialize the LED matrix displays (default address and 0x76)
void connect_display();

// Display time on LED matrix
void displayTime(String hour, String minute, int elapsedSeconds, uint8_t orientation);

// Display minutes with a progress bar
void displayElapsedMinutes(int elapsedSeconds, uint8_t orientation);

//clear display
void clearDisplay();

// Clear inactive display based on orientation
void clearInactiveDisplay(uint8_t orientation);

// Display blinking low-battery icon
void displayBatteryLow(uint8_t orientation);

#endif
