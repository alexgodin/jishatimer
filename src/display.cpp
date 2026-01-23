#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_IS31FL3731.h>
#include <Fonts/Picopixel.h>
#include "display.h"

Adafruit_IS31FL3731 ledmatrix_top = Adafruit_IS31FL3731();
Adafruit_IS31FL3731 ledmatrix_bottom = Adafruit_IS31FL3731();

// Frame tracking for double buffering (separate for each display)
static uint8_t currentFrame_top = 0;
static uint8_t currentFrame_bottom = 0;

// Helper functions for flicker-free drawing
void beginDraw(Adafruit_IS31FL3731& display, uint8_t& frameCounter) {
  uint8_t drawFrame = (frameCounter + 1) % 2;
  display.setFrame(drawFrame);
  display.clear();
}

void endDraw(Adafruit_IS31FL3731& display, uint8_t& frameCounter) {
  uint8_t drawFrame = (frameCounter + 1) % 2;
  display.displayFrame(drawFrame);
  frameCounter = drawFrame;
}

// Determine which display should be active based on Y-axis orientation
// Returns: 1 = top only, 2 = bottom only, 3 = both
uint8_t getActiveDisplays(uint8_t orientation) {
  bool isYPlus = orientation & 0x08;   // Y+ (top up)
  bool isYMinus = orientation & 0x04;  // Y- (bottom up)

  if (isYPlus && !isYMinus) {
    return 1;  // Top display only
  } else if (isYMinus && !isYPlus) {
    return 2;  // Bottom display only
  } else {
    // Both bits set OR neither bit set: use both displays (safe fallback)
    return 3;
  }
}

void clearDisplay() {
  ledmatrix_top.clear();
  ledmatrix_bottom.clear();
}

void clearInactiveDisplay(uint8_t orientation) {
  uint8_t activeDisplays = getActiveDisplays(orientation);

  // Clear the inactive display based on orientation
  if (activeDisplays == 1) {
    // Top only active, clear bottom
    ledmatrix_bottom.clear();
    ledmatrix_bottom.displayFrame(currentFrame_bottom);
  } else if (activeDisplays == 2) {
    // Bottom only active, clear top
    ledmatrix_top.clear();
    ledmatrix_top.displayFrame(currentFrame_top);
  }
  // If activeDisplays == 3 (both), don't clear anything
}

void drawProgressBar(Adafruit_IS31FL3731& display, int elapsedSeconds) {
  int progressLevel = (elapsedSeconds % 60) * 8 / 59;
  display.drawLine(0, 0, progressLevel, 0, 150);
}

void initializeDisplay(Adafruit_IS31FL3731& display, uint8_t address, const char* name) {
  if (!display.begin(address)) {
    Serial.print("IS31 ");
    Serial.print(name);
    Serial.println(" not found");
  } else {
    Serial.print("IS31 ");
    Serial.print(name);
    Serial.println(" found!");
  }
  display.setRotation(1);
  display.setTextColor(150);
  display.setTextWrap(false);
}

void connect_display() {
  initializeDisplay(ledmatrix_top, 0x76, "Top");      // SWAPPED: was 0x74
  initializeDisplay(ledmatrix_bottom, 0x74, "Bottom"); // SWAPPED: was 0x76
}

void drawTime(Adafruit_IS31FL3731& display, uint8_t& frameCounter, String hour, String minute, int elapsedSeconds) {
  beginDraw(display, frameCounter);
  display.setFont(&Picopixel);
  display.setCursor(1, 6);
  display.print(hour);

  display.setCursor(1, 13);
  display.print(minute);

  drawProgressBar(display, elapsedSeconds);
  endDraw(display, frameCounter);
}

void displayTime(String hour, String minute, int elapsedSeconds, uint8_t orientation) {
  static String lastHour = "";
  static String lastMinute = "";
  static int lastElapsedSeconds = -1;
  static uint8_t lastOrientation = 0xFF;

  // Redraw if time OR orientation changed
  if (lastHour == hour && lastMinute == minute &&
      lastElapsedSeconds == elapsedSeconds &&
      lastOrientation == orientation) {
    return;
  }

  uint8_t activeDisplays = getActiveDisplays(orientation);

  if (activeDisplays == 1 || activeDisplays == 3) {
    drawTime(ledmatrix_top, currentFrame_top, hour, minute, elapsedSeconds);
  }
  if (activeDisplays == 2 || activeDisplays == 3) {
    drawTime(ledmatrix_bottom, currentFrame_bottom, hour, minute, elapsedSeconds);
  }

  lastHour = hour;
  lastMinute = minute;
  lastElapsedSeconds = elapsedSeconds;
  lastOrientation = orientation;
}

void drawElapsedMinutes(Adafruit_IS31FL3731& display, uint8_t& frameCounter, int elapsedMinutes, int elapsedSeconds) {
  beginDraw(display, frameCounter);

  // Use Picopixel for double-digit numbers, default font for single digits
  if (elapsedMinutes >= 10) {
    display.setFont(&Picopixel);
    display.setCursor(1, 10);  // Picopixel uses baseline (text goes up)
  } else {
    display.setFont();  // default font uses top-left (text goes down)
    display.setCursor(2, 5);  // Adjust Y for top-left positioning
  }

  display.print(elapsedMinutes);

  drawProgressBar(display, elapsedSeconds);
  endDraw(display, frameCounter);
}

void displayElapsedMinutes(int elapsedSeconds, uint8_t orientation) {
  static int lastElapsedSeconds = -1;
  static uint8_t lastOrientation = 0xFF;

  // Redraw if elapsed time OR orientation changed
  if (lastElapsedSeconds == elapsedSeconds && lastOrientation == orientation) {
    return;
  }

  int elapsedMinutes = elapsedSeconds / 60;
  uint8_t activeDisplays = getActiveDisplays(orientation);

  if (activeDisplays == 1 || activeDisplays == 3) {
    drawElapsedMinutes(ledmatrix_top, currentFrame_top, elapsedMinutes, elapsedSeconds);
  }
  if (activeDisplays == 2 || activeDisplays == 3) {
    drawElapsedMinutes(ledmatrix_bottom, currentFrame_bottom, elapsedMinutes, elapsedSeconds);
  }

  lastElapsedSeconds = elapsedSeconds;
  lastOrientation = orientation;
}