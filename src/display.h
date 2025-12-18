#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Initialize the LED matrix display
void connect_display();

// Display orientation based on accelerometer Z value
void displayOrientation(int16_t z_value);

// Display time on LED matrix
void displayTime(String hour, String minute);

// Display elapsed seconds with different formats based on duration
void displayElapsedSeconds(int elapsedSeconds);

#endif
