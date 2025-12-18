/*
 * LIS3DH X-Axis Orientation Detection Test
 * Detects when device is flipped along X-axis using 6D orientation detection
 * Prints orientation changes to serial monitor
 */

#include <Wire.h>
#include <Arduino.h>

// LIS3DH Configuration
#define LIS3DH_I2C_ADDR       0x18  // I2C address
#define LIS3DH_INT_PIN        2     // GPIO pin connected to LIS3DH INT1 (adjust if needed)

// Movement threshold (for accelerometer.cpp compatibility, not used here)
int MOVEMENT_THRESHOLD = 2000;

// LIS3DH Register Addresses
#define LIS3DH_REG_WHO_AM_I   0x0F  // Device identification
#define LIS3DH_REG_CTRL_REG1  0x20  // Power mode, data rate, axis enable
#define LIS3DH_REG_CTRL_REG2  0x21  // High-pass filter
#define LIS3DH_REG_CTRL_REG3  0x22  // Interrupt control
#define LIS3DH_REG_CTRL_REG4  0x23  // Full-scale selection
#define LIS3DH_REG_CTRL_REG5  0x24  // Interrupt latching
#define LIS3DH_REG_CTRL_REG6  0x25  // Interrupt polarity
#define LIS3DH_REG_INT1_CFG   0x30  // Interrupt 1 configuration
#define LIS3DH_REG_INT1_SRC   0x31  // Interrupt 1 source (read to clear)
#define LIS3DH_REG_INT1_THS   0x32  // Interrupt 1 threshold
#define LIS3DH_REG_INT1_DUR   0x33  // Interrupt 1 duration

// Interrupt flag
volatile bool orientationChanged = false;

// ISR for orientation change
void IRAM_ATTR orientationISR() {
  orientationChanged = true;
}

// Write a single byte to a LIS3DH register
void lis3dh_writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(LIS3DH_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

// Read a single byte from a LIS3DH register
uint8_t lis3dh_readRegister(uint8_t reg) {
  Wire.beginTransmission(LIS3DH_I2C_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom((uint8_t)LIS3DH_I2C_ADDR, (uint8_t)1);
  return Wire.read();
}

// Initialize LIS3DH for 6D orientation detection on all axes
void lis3dh_init6DOrientation() {
  Serial.println("\n=== Initializing LIS3DH for 6D Orientation Detection (All Axes) ===\n");

  // 0. Verify I2C communication by reading WHO_AM_I register
  Serial.println("[0/8] Verifying I2C communication...");
  uint8_t whoami = lis3dh_readRegister(LIS3DH_REG_WHO_AM_I);
  Serial.printf("      WHO_AM_I = 0x%02X (expected 0x33)\n", whoami);
  if (whoami != 0x33) {
    Serial.println("      ERROR: Wrong device ID! Check I2C address and wiring.");
  }

  // 1. CTRL_REG1: Power on, 10Hz data rate, enable all axes
  Serial.println("[1/8] CTRL_REG1: Power on, 10Hz ODR, all axes enabled");
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG1, 0x27);  // 0b00100111
  delay(10);

  // 2. CTRL_REG2: Disable high-pass filter
  Serial.println("[2/8] CTRL_REG2: High-pass filter disabled");
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG2, 0x00);

  // 3. CTRL_REG3: Route IA1 interrupt to INT1 pin
  Serial.println("[3/8] CTRL_REG3: Route IA1 interrupt to INT1 pin");
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG3, 0x40);  // 0b01000000

  // 4. CTRL_REG4: ±2g full scale, high resolution mode
  Serial.println("[4/8] CTRL_REG4: ±2g scale, high resolution");
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG4, 0x08);  // 0b00001000

  // 5. CTRL_REG5: Enable interrupt latching (interrupt stays high until cleared)
  Serial.println("[5/8] CTRL_REG5: Interrupt latching enabled");
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG5, 0x08);  // 0b00001000

  // 5b. CTRL_REG6: Configure interrupt polarity (active high)
  Serial.println("[5b/8] CTRL_REG6: INT1 polarity active-high (default)");
  lis3dh_writeRegister(LIS3DH_REG_CTRL_REG6, 0x00);  // 0x00 = active-high

  // 6. INT1_THS: Set threshold for 6D orientation detection
  //    At ±2g scale: 1 LSb = 16mg
  //    Higher threshold for reliable orientation change detection
  //    0x30 = 48 * 16mg = 768mg (optimal for 6D orientation detection)
  Serial.println("[6/8] INT1_THS: Threshold set to ~768mg (reliable detection)");
  lis3dh_writeRegister(LIS3DH_REG_INT1_THS, 0x30);

  // 7. INT1_DURATION: Minimum duration (set to 0 for immediate response)
  Serial.println("[7/8] INT1_DURATION: Set to 0 (immediate)");
  lis3dh_writeRegister(LIS3DH_REG_INT1_DUR, 0x00);

  // 8. INT1_CFG: Enable 6D orientation detection
  //    Different configurations to try:
  //    0xC0 = 0b11000000 (AOI=1, 6D=1) - 6D detection with AND logic
  //    0x80 = 0b10000000 (AOI=0, 6D=1) - 6D detection with OR logic
  //    0x95 = 0b10010101 (6D + ZHIE + YLIE + XLIE) - 6D with specific axes
  //
  //    TRYING: 0x80 (6D mode without AOI - OR logic for any axis change)
  Serial.println("[8/8] INT1_CFG: 6D orientation detection (OR logic)");
  lis3dh_writeRegister(LIS3DH_REG_INT1_CFG, 0x80);

  // Clear any existing interrupts by reading INT1_SRC
  uint8_t intSrc = lis3dh_readRegister(LIS3DH_REG_INT1_SRC);

  Serial.println("\n=== Verifying Configuration ===");
  Serial.printf("CTRL_REG1 readback: 0x%02X (expected 0x27)\n", lis3dh_readRegister(LIS3DH_REG_CTRL_REG1));
  Serial.printf("CTRL_REG3 readback: 0x%02X (expected 0x40)\n", lis3dh_readRegister(LIS3DH_REG_CTRL_REG3));
  Serial.printf("CTRL_REG5 readback: 0x%02X (expected 0x08)\n", lis3dh_readRegister(LIS3DH_REG_CTRL_REG5));
  Serial.printf("CTRL_REG6 readback: 0x%02X (expected 0x00)\n", lis3dh_readRegister(LIS3DH_REG_CTRL_REG6));
  Serial.printf("INT1_THS readback:  0x%02X (expected 0x30)\n", lis3dh_readRegister(LIS3DH_REG_INT1_THS));
  Serial.printf("INT1_DUR readback:  0x%02X (expected 0x00)\n", lis3dh_readRegister(LIS3DH_REG_INT1_DUR));
  Serial.printf("INT1_CFG readback:  0x%02X (expected 0x80)\n", lis3dh_readRegister(LIS3DH_REG_INT1_CFG));
  Serial.printf("INT1_SRC initial:   0x%02X (should be cleared)\n", intSrc);
  Serial.printf("\nPIN STATE CHECK: INT1 pin = %s\n", digitalRead(LIS3DH_INT_PIN) == HIGH ? "HIGH" : "LOW");

  Serial.println("\n=== LIS3DH Configuration Complete ===");
  Serial.println("Ready to detect orientation changes!");
  Serial.println("Threshold: ~768mg - Flip the device to test...\n");
}

// Read and decode the interrupt source
void lis3dh_handleOrientationChange() {
  uint8_t intSrc = lis3dh_readRegister(LIS3DH_REG_INT1_SRC);

  Serial.println("\n--- Orientation Changed ---");
  Serial.printf("INT1_SRC: 0x%02X\n", intSrc);

  // Check if interrupt is active
  if (intSrc & 0x40) {
    Serial.println("Status: Interrupt Active (IA)");
  }

  // Decode which face is now pointing up/down
  if (intSrc & 0x20) Serial.println("Z-axis: ZH face is UP");
  if (intSrc & 0x10) Serial.println("Z-axis: ZL face is UP");
  if (intSrc & 0x08) Serial.println("Y-axis: YH face is UP");
  if (intSrc & 0x04) Serial.println("Y-axis: YL face is UP");
  if (intSrc & 0x02) Serial.println("X-axis: XH face is UP");
  if (intSrc & 0x01) Serial.println("X-axis: XL face is UP");

  Serial.println("----------------------------\n");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n========================================");
  Serial.println("LIS3DH 6D Orientation Test (Low Threshold)");
  Serial.println("========================================\n");

  // Initialize I2C
  Serial.println("Initializing I2C...");
  Wire.begin();
  Wire.setClock(100000);  // 100kHz
  Serial.println("I2C initialized at 100kHz\n");

  // Initialize LIS3DH for 6D orientation detection
  lis3dh_init6DOrientation();

  // Setup GPIO interrupt on INT1 pin
  Serial.printf("Setting up GPIO interrupt on pin %d...\n", LIS3DH_INT_PIN);
  pinMode(LIS3DH_INT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(LIS3DH_INT_PIN), orientationISR, CHANGE);
  Serial.println("GPIO interrupt configured (CHANGE mode - detects both edges)\n");

  Serial.println("=== Setup Complete ===");
  Serial.println("Waiting for orientation changes...\n");
}

void loop() {
  // Check if orientation changed
  if (orientationChanged) {
    orientationChanged = false;  // Reset flag

    // Read INT1 pin state for debugging
    int pinState = digitalRead(LIS3DH_INT_PIN);
    Serial.printf("[DEBUG] Interrupt triggered! INT1 pin state: %s\n",
                  pinState == HIGH ? "HIGH" : "LOW");

    // Verify INT1 pin is actually HIGH (ignore spurious interrupts)
    if (pinState == HIGH) {
      // Read interrupt source and print details
      lis3dh_handleOrientationChange();
    } else {
      Serial.println("[DEBUG] Interrupt cleared before read (normal with CHANGE mode)");
    }
  }

  // Small delay to prevent busy-waiting
  delay(10);
}
