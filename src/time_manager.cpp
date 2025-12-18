#include <Arduino.h>
#include <ezTime.h>
#include <RTClib.h>
#include <Wire.h>
#include "accelerometer.h"

// Create RTC and timezone objects
RTC_PCF8523 rtc;
Timezone myTZ;

// Timer management
static volatile int elapsedSeconds = 0;
static unsigned long lastTimerUpdate = 0;

void syncEzTimeFromRTC() {
    // Read RTC and update ezTime
    DateTime rtcNow = rtc.now();
    setTime(rtcNow.unixtime());
    Serial.println("ezTime synced from RTC");
}

void setupTime() {
    // Initialize I2C and RTC
    Wire.begin();

    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC");
        return;
    }

    // Check if RTC has been initialized
    if (!rtc.initialized() || rtc.lostPower()) {
        Serial.println("RTC is NOT initialized or lost power!");
        // Set to compile time as fallback
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    setInterval(0);  // Disable NTP sync - we're using RTC

    // Configure timezone (change to your location)
    myTZ.setLocation("America/New_York");

    syncEzTimeFromRTC();


    Serial.println("Time initialized from RTC");
    Serial.print("Current time: ");
    Serial.println(myTZ.dateTime());
}

void getCurrentTime(String &hour, String &minute) {
    // Return hour and minute in military time (24-hour format)
    hour = myTZ.dateTime("H");    // Hour in 24-hour format (00-23)
    minute = myTZ.dateTime("i");  // Minute with leading zero (00-59)
}


void setupElapsedTimer() {
    elapsedSeconds = 0;
    lastTimerUpdate = millis();
    Serial.println("Elapsed timer initialized (millis-based)");
}

void resetElapsedTimer() {
    elapsedSeconds = 0;
    lastTimerUpdate = millis();
    Serial.println("Timer reset");
}

int getElapsedSeconds() {
    return elapsedSeconds;
}
