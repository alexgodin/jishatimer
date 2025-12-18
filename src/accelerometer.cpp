#include <Arduino.h>
#include "accelerometer.h"

Adafruit_LIS3DH accelerometer = Adafruit_LIS3DH();
int INTERRUPT_PIN = D1;
volatile bool dataReady = false;

// Store previous values
int16_t prev_x = 0;
int16_t prev_y = 0;
int16_t prev_z = 0;
bool first_reading = true;

extern const int MOVEMENT_THRESHOLD;

void accelerometerISR() {
  dataReady = true;
}

void connect_accelerometer()
{
  if (!accelerometer.begin(0x18)) {
    Serial.println("Failed to find LIS3DH chip");
    return;
  }
  Serial.println("LIS3DH found!");

  accelerometer.setRange(LIS3DH_RANGE_2_G);                // ±2G is plenty for gravity detection
  accelerometer.setPerformanceMode(LIS3DH_MODE_LOW_POWER); // Save battery, 8-bit is sufficient
  accelerometer.setDataRate(LIS3DH_DATARATE_10_HZ);        // 10 readings/second is fast enough

  // Configure motion detection interrupt on INT1
  // NOTE: Interrupt configuration moved to main.cpp for direct register access
  // The Adafruit library doesn't expose writeRegister8() for this board version

  // uint8_t c = 0;
  // c |= 0x20; // Enable X high
  // c |= 0x08; // Enable Y high
  // c |= 0x02; // Enable Z high
  // accelerometer.writeRegister8(0x30, c); // INT1_CFG - configure interrupt conditions
  // accelerometer.writeRegister8(0x32, 16); // INT1_THS - threshold (16 = ~250mg)
  // accelerometer.writeRegister8(0x33, 1);  // INT1_DURATION - duration in samples
  // accelerometer.writeRegister8(0x22, 0x40); // CTRL_REG3 - route INT1 to INT1 pin

  Serial.println("Basic accelerometer configured (interrupt setup in main.cpp)");
  Serial.print("Range = "); Serial.println(2 << accelerometer.getRange());
}

bool hasSignificantChange() {
  if (first_reading) return true;

  return abs(accelerometer.x - prev_x) > MOVEMENT_THRESHOLD ||
         abs(accelerometer.y - prev_y) > MOVEMENT_THRESHOLD ||
         abs(accelerometer.z - prev_z) > MOVEMENT_THRESHOLD;
}

void updatePreviousValues() {
  prev_x = accelerometer.x;
  prev_y = accelerometer.y;
  prev_z = accelerometer.z;
  first_reading = false;
}

bool checkForMovement() {
  if (!dataReady) {
    return false;
  }

  // Read data to clear DRDY flag
  accelerometer.read();

  bool movementDetected = hasSignificantChange();

  if (movementDetected) {
    Serial.print("X: "); Serial.print(accelerometer.x);
    Serial.print(" Y: "); Serial.print(accelerometer.y);
    Serial.print(" Z: "); Serial.println(accelerometer.z);
  }

  updatePreviousValues();
  dataReady = false;

  return movementDetected;
}
