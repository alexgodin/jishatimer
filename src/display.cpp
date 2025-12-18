#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_IS31FL3731.h>
#include <Fonts/Picopixel.h>
#include "display.h"

Adafruit_IS31FL3731 ledmatrix = Adafruit_IS31FL3731();

void connect_display() {
  if (!ledmatrix.begin()) {
    Serial.println("IS31 not found");
  } else {
    Serial.println("IS31 found!");
  }
  ledmatrix.setRotation(1);
  ledmatrix.setTextColor(100);
  ledmatrix.setTextWrap(false);
  ledmatrix.setFont(&Picopixel);

}

void displayTime(String hour, String minute) {
  static String lastHour = "";
  static String lastMinute = "";

  if (lastHour == hour && lastMinute == minute){
    return;
  }

  ledmatrix.clear();

  ledmatrix.setCursor(1, 4);
  ledmatrix.print(hour);

  ledmatrix.setCursor(1, 11);
  ledmatrix.print(minute);

  lastHour = hour;
  lastMinute = minute;
}

void displayCountUp(int seconds) {
  ledmatrix.setRotation(0);
  ledmatrix.setCursor(1,1);
  ledmatrix.print(seconds);
}

void displayElapsedSeconds(int elapsedSeconds) {
  static int lastElapsedSeconds = -1;

  if (lastElapsedSeconds == elapsedSeconds) {
    return;
  }

  ledmatrix.clear();
  ledmatrix.setRotation(0);
  ledmatrix.setCursor(1, 4);
  ledmatrix.print(elapsedSeconds);

  lastElapsedSeconds = elapsedSeconds;
}