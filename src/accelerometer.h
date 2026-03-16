#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <Arduino.h>

// LIS3DH I2C address (SDO pin grounded = 0x18, SDO high = 0x19)
#define LIS3DH_I2C_ADDR       0x18

// LIS3DH Register Map
#define LIS3DH_REG_WHO_AM_I   0x0F
#define LIS3DH_REG_CTRL_REG1  0x20
#define LIS3DH_REG_CTRL_REG2  0x21
#define LIS3DH_REG_CTRL_REG3  0x22
#define LIS3DH_REG_CTRL_REG4  0x23
#define LIS3DH_REG_CTRL_REG5  0x24
#define LIS3DH_REG_CTRL_REG6  0x25
#define LIS3DH_REG_INT1_CFG   0x30
#define LIS3DH_REG_INT1_SRC   0x31
#define LIS3DH_REG_INT1_THS   0x32
#define LIS3DH_REG_INT1_DUR   0x33

// Initialize the LIS3DH for 6D orientation detection
bool lis3dh_init();

// Read a single register
uint8_t lis3dh_readRegister(uint8_t reg);

// Write a single register
bool lis3dh_writeRegister(uint8_t reg, uint8_t value);

// Get current orientation from INT1_SRC register
uint8_t lis3dh_getOrientation();

// Decode orientation bits to human-readable string
String lis3dh_decodeOrientation(uint8_t orientation);

// Mask specific orientation to prevent interrupt triggering
void lis3dh_maskOrientation(uint8_t orientationToMask);

// Clear interrupt latch by reading INT1_SRC
void lis3dh_clearInterrupt();

// Complete accelerometer setup including initialization and interrupt configuration
// Returns the initial orientation detected at startup
// On fatal error, prints message and enters infinite loop (never returns)
uint8_t lis3dh_setupInterrupt(uint8_t interruptPin, void (*isr)(void));

// Process a pending orientation change interrupt
// Must be called when orientationChanged flag is true
// Resets the ISR flag and tracks orientation state internally
// Returns true if orientation actually changed (not just noise/debounce)
// Optionally fills in previous and current orientation values if pointers are not NULL
bool lis3dh_processOrientationChange(volatile bool& orientationChanged,
                                      uint8_t* previous,
                                      uint8_t* current);

#endif
