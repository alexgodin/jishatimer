#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

RTC_PCF8523 rtc;

void setupTime() {
    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC");
        return;
    }

    // Set to compile time if not initialized
    // if (!rtc.initialized()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        Serial.println("RTC set to compile time");
    // }

    DateTime now = rtc.now();
    Serial.printf("Time: %02d:%02d:%02d\n", now.hour(), now.minute(), now.second());
}

void getCurrentTime(String &hour, String &minute) {
    DateTime now = rtc.now();
    int h = now.hour() % 12;
    hour = String(h == 0 ? 12 : h);

    // Add leading zero for single-digit minutes
    int m = now.minute();
    if (m < 10) {
        minute = "0" + String(m);
    } else {
        minute = String(m);
    }
}

// Timer functions
static unsigned long lastTimerUpdate = 0;

void setupElapsedTimer() {
    lastTimerUpdate = millis();
    Serial.println("Elapsed timer initialized");
}

void resetElapsedTimer() {
    lastTimerUpdate = millis();
    Serial.println("Timer reset");
}

int getElapsedSeconds() {
    unsigned long currentTime = millis();
    unsigned long elapsedMillis = currentTime - lastTimerUpdate;
    return elapsedMillis / 1000;
}
