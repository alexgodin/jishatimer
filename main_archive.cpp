#include <Wire.h>
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include "accelerometer.h"
#include "display.h"
#include "time_manager.h"
#include "battery.h"

const int SLEEP_THRESHOLD_SECONDS = 600; // 10 minutes
int MOVEMENT_THRESHOLD = 2000; // Accelerometer change threshold

void setup()
{
  Serial.begin(115200);
  delay(1000);

  connect_display();

  connect_accelerometer();

  // Initialize time from RTC
  Serial.println("[SETUP] About to call setupTime()");
  setupTime();
  Serial.println("[SETUP] setupTime() completed");

  // Setup elapsed timer
  Serial.println("[SETUP] About to call setupElapsedTimer()");
  setupElapsedTimer();
  Serial.println("[SETUP] setupElapsedTimer() completed");

  // Display current time on boot
  Serial.println("[SETUP] About to get current time");
  String hour, minute;
  getCurrentTime(hour, minute);
  Serial.println("[SETUP] About to display time");
  displayTime(hour, minute);
  Serial.println("[SETUP] displayTime() completed");

  // Setup interrupt pin
  pinMode(INTERRUPT_PIN, INPUT);
}

void loop()
{
  // sync ez time

  // Check if we should sleep
  if (getElapsedSeconds() >= SLEEP_THRESHOLD_SECONDS) {
    Serial.println("10 minutes elapsed - going to sleep");
    esp_deep_sleep_start();
  }

  // Update display with elapsed seconds

}