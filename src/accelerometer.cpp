#include "accelerometer.h"
#include <Wire.h>

// Track last detected orientation for debouncing
static uint8_t lastOrientation = 0;

bool lis3dh_writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(LIS3DH_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  uint8_t error = Wire.endTransmission();

  if (error != 0) {
    Serial.printf("ERROR: I2C write failed (reg=0x%02X, err=%d)\n", reg, error);
    return false;
  }
  return true;
}

uint8_t lis3dh_readRegister(uint8_t reg) {
  Wire.beginTransmission(LIS3DH_I2C_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom((uint8_t)LIS3DH_I2C_ADDR, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0xFF;
}

bool lis3dh_init() {
  // Verify device ID
  uint8_t whoami = lis3dh_readRegister(LIS3DH_REG_WHO_AM_I);
  if (whoami != 0x33) {
    Serial.printf("ERROR: Device ID 0x%02X != 0x33\n", whoami);
    return false;
  }

  // Configure power mode: 10Hz, normal mode, all axes enabled
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG1, 0x27);
  delay(10);

  // Disable high-pass filter
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG2, 0x00);

  // Route interrupt 1 to INT1 pin
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG3, 0x40);

  // Set scale to ±2g, high resolution mode
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG4, 0x08);

  // Latch interrupt requests
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG5, 0x08);

  // INT1 pin active-high
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG6, 0x00);

  // Set interrupt threshold (0x30 ≈ 0.75g)
  lis3dh_writeRegister(LIS3DH_REG_INT1_THS, 0x30);

  // Interrupt duration: immediate
  lis3dh_writeRegister(LIS3DH_REG_INT1_DUR, 0x00);

  // Enable 6D orientation detection on all axes
  lis3dh_writeRegister(LIS3DH_REG_INT1_CFG, 0xFF);

  // Clear any pending interrupts
  lis3dh_readRegister(LIS3DH_REG_INT1_SRC);

  Serial.println("LIS3DH initialized");
  return true;
}

uint8_t lis3dh_getOrientation() {
  uint8_t intSrc = lis3dh_readRegister(LIS3DH_REG_INT1_SRC);
  return intSrc & 0x3F;  // Mask to orientation bits only
}

String lis3dh_decodeOrientation(uint8_t orientation) {
  if (orientation & 0x20) return "Z+ (face up)";
  if (orientation & 0x10) return "Z- (face down)";
  if (orientation & 0x08) return "Y+ (top up)";
  if (orientation & 0x04) return "Y- (bottom up)";
  if (orientation & 0x02) return "X+ (right up)";
  if (orientation & 0x01) return "X- (left up)";
  return "none";
}

void lis3dh_maskOrientation(uint8_t orientationToMask) {
  uint8_t cfg = 0xFF & ~orientationToMask;
  lis3dh_writeRegister(LIS3DH_REG_INT1_CFG, cfg);
  delay(100);
  lis3dh_clearInterrupt();
}

void lis3dh_clearInterrupt() {
  lis3dh_readRegister(LIS3DH_REG_INT1_SRC);
}

uint8_t lis3dh_setupInterrupt(uint8_t interruptPin, void (*isr)(void)) {
  // Initialize accelerometer
  if (!lis3dh_init()) {
    Serial.println("FATAL: LIS3DH init failed");
    while (1) delay(1000);
  }

  // Read and mask current orientation
  uint8_t initialOrientation = lis3dh_getOrientation();
  lastOrientation = initialOrientation;
  lis3dh_maskOrientation(initialOrientation);

  // Configure interrupt pin
  pinMode(interruptPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(interruptPin), isr, RISING);

  return initialOrientation;
}

bool lis3dh_processOrientationChange(volatile bool& orientationChanged,
                                      uint8_t* previous,
                                      uint8_t* current) {
  // Reset ISR flag
  orientationChanged = false;

  // Read current orientation
  uint8_t currentOrientation = lis3dh_getOrientation();

  // Fill in previous orientation if requested
  if (previous != NULL) {
    *previous = lastOrientation;
  }

  // Fill in current orientation if requested
  if (current != NULL) {
    *current = currentOrientation;
  }

  // Check if orientation actually changed (debounce)
  if (currentOrientation != lastOrientation) {
    lastOrientation = currentOrientation;

    // Mask current orientation to prevent re-triggering
    lis3dh_maskOrientation(currentOrientation);

    return true;
  }

  return false;
}
