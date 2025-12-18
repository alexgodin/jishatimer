#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <Adafruit_LIS3DH.h>

// Initialize and configure the accelerometer
void connect_accelerometer();

// Check if accelerometer values changed significantly
bool hasSignificantChange();

// Update the stored previous values
void updatePreviousValues();

// ISR for data ready interrupt
void accelerometerISR();

// Check for movement and return true if significant change detected
bool checkForMovement();

// External references
extern Adafruit_LIS3DH accelerometer;
extern int INTERRUPT_PIN;
extern volatile bool dataReady;

#endif
