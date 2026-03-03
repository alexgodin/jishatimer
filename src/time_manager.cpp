#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_sntp.h>

const char* POSIX_TZ = "EST5EDT,M3.2.0,M11.1.0";
RTC_PCF8523 rtc;
RTC_DATA_ATTR time_t lastNtpSync = 0;  // Persists across deep sleep
const time_t SYNC_INTERVAL = 48 * 60 * 60;  // 48 hours

bool syncTimeFromNtp() {
    // Connect to WiFi (tries each known network, picks strongest)
    WiFiMulti wifiMulti;
    wifiMulti.addAP("stellas world", "lovedeeply");
    wifiMulti.addAP("108nyzccc", "Dharma13");
    WiFi.mode(WIFI_STA);
    if (wifiMulti.run(5000) != WL_CONNECTED) {
        WiFi.disconnect(true);
        return false;
    }

    // Fetch UTC from NTP (offsets 0,0 = no timezone adjustment, we want raw UTC)
    configTime(0, 0, "pool.ntp.org");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {  // 5s timeout
        WiFi.disconnect(true);
        return false;
    }

    // Measure RTC drift before overwriting, and auto-calibrate
    time_t utcNow = time(nullptr);
    if (lastNtpSync > 0) {
        time_t rtcNow = rtc.now().unixtime();
        long drift = (long)(rtcNow - utcNow);
        long elapsed = (long)(utcNow - lastNtpSync);
        if (elapsed > 3600) {  // Need enough time to measure meaningful drift
            float driftPpm = (float)drift / (float)elapsed * 1000000.0f;
            int8_t offset = constrain((int)(-driftPpm / 4.34f), -64, 63);
            rtc.calibrate(PCF8523_OneMinute, offset);
            Serial.printf("NTP calibration: drift=%lds over %lds (%.1f ppm), offset=%d\n",
                          drift, elapsed, driftPpm, offset);
        }
    }

    // Write UTC to RTC
    rtc.adjust(DateTime(utcNow));
    lastNtpSync = utcNow;

    WiFi.disconnect(true);

    // Restore timezone (configTime overwrites the TZ env var)
    setenv("TZ", POSIX_TZ, 1);
    tzset();

    return true;
}

bool isNtpSyncDue() {
    if (lastNtpSync == 0) return true;  // First boot
    time_t now = rtc.now().unixtime();
    return (now - lastNtpSync) >= SYNC_INTERVAL;
}

void setupTime() {
    if (!rtc.begin()) {
        Serial.println("ERROR: RTC not found");
        return;
    }

    // If RTC lost power, set to compile time as fallback
    if (!rtc.initialized()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // POSIX timezone: EST5EDT with US DST rules
    setenv("TZ", POSIX_TZ, 1);
    tzset();

    // Sync from NTP if due (blocks up to 5s if no WiFi, 10s if WiFi but NTP slow)
    if (isNtpSyncDue()) {
        syncTimeFromNtp();
    }
}

// Returns current time in 12-hour format
void getCurrentTime(String &hour, String &minute) {
    time_t utc = rtc.now().unixtime();
    struct tm local;
    localtime_r(&utc, &local);

    int h = local.tm_hour % 12;
    hour = String(h == 0 ? 12 : h);

    int m = local.tm_min;
    if (m < 10) {
        minute = "0" + String(m);
    } else {
        minute = String(m);
    }
}

// Timer functions
static unsigned long lastTimerUpdate = 0;

void startElapsedTimer() {
    lastTimerUpdate = millis();
}

void resetElapsedTimer() {
    lastTimerUpdate = millis();
}

int getElapsedSeconds() {
    unsigned long currentTime = millis();
    unsigned long elapsedMillis = currentTime - lastTimerUpdate;
    return elapsedMillis / 1000;
}

void debugCorruptTime() {
    rtc.adjust(DateTime(2000, 1, 1, 0, 0, 0));
    lastNtpSync = 0;
    Serial.println("RTC set to 2000-01-01 00:00, sync counter reset");
}

