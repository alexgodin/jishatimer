#include "time_manager.h"
#include <RTClib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_sntp.h>

const char* POSIX_TZ = "EST5EDT,M3.2.0,M11.1.0";

// Forward declaration
void getCurrentTime(String &hour, String &minute);
RTC_PCF8523 rtc;
static bool rtcAvailable = false;
RTC_DATA_ATTR time_t lastNtpSync = 0;  // Persists across deep sleep
const time_t SYNC_INTERVAL = 48 * 60 * 60;  // 48 hours

// Fault injection flags for testing
bool debugWifiFail = false;
bool debugNtpTimeout = false;

static volatile bool ntpSyncReceived = false;
static void ntpSyncCallback(struct timeval *tv) {
    ntpSyncReceived = true;
}

bool syncTimeFromNtp() {
    Serial.println("NTP sync: connecting to WiFi...");

    // Connect to WiFi (tries each known network, picks strongest)
    WiFiMulti wifiMulti;
    wifiMulti.addAP("stellas world", "lovedeeply");
    wifiMulti.addAP("108nyzccc", "Dharma13");
    WiFi.mode(WIFI_STA);
    if (debugWifiFail) {
        debugWifiFail = false;
        Serial.println("NTP sync: WiFi failed [INJECTED]");
        WiFi.disconnect(true);
        return false;
    }
    if (wifiMulti.run(5000) != WL_CONNECTED) {
        Serial.println("NTP sync: WiFi failed");
        WiFi.disconnect(true);
        return false;
    }
    Serial.printf("NTP sync: WiFi connected to %s\n", WiFi.SSID().c_str());

    // Clear any stale SNTP state from before reboot
    if (sntp_enabled()) {
        sntp_stop();
    }

    // Register callback and reset flag before starting SNTP
    ntpSyncReceived = false;
    esp_sntp_set_time_sync_notification_cb(ntpSyncCallback);

    // Start NTP sync
    time_t timeBefore = time(nullptr);
    Serial.printf("NTP sync: time before = %ld\n", (long)timeBefore);
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

    // Wait for the callback to fire (confirms a real NTP packet was received)
    Serial.println("NTP sync: waiting for SNTP callback...");
    if (debugNtpTimeout) {
        debugNtpTimeout = false;
        ntpSyncReceived = false;
        Serial.println("NTP sync: timeout waiting for callback [INJECTED]");
        sntp_stop();
        WiFi.disconnect(true);
        return false;
    }
    for (int i = 0; i < 100; i++) {  // 10s timeout
        if (ntpSyncReceived) {
            Serial.printf("NTP sync: callback fired after %dms\n", (i + 1) * 100);
            break;
        }
        delay(100);
    }
    if (!ntpSyncReceived) {
        Serial.println("NTP sync: timeout waiting for callback");
        sntp_stop();
        WiFi.disconnect(true);
        return false;
    }

    time_t utcNow = time(nullptr);
    Serial.printf("NTP sync: time after = %ld (delta=%lds)\n",
                  (long)utcNow, (long)(utcNow - timeBefore));

    // Measure RTC drift before overwriting, and auto-calibrate
    time_t rtcNow = rtc.now().unixtime();
    Serial.printf("NTP sync: NTP=%ld, RTC=%ld, diff=%lds\n",
                  (long)utcNow, (long)rtcNow, (long)(rtcNow - utcNow));

    if (lastNtpSync > 0) {
        long drift = (long)(rtcNow - utcNow);
        long elapsed = (long)(utcNow - lastNtpSync);
        if (elapsed > 3600) {  // Need enough time to measure meaningful drift
            float driftPpm = (float)drift / (float)elapsed * 1000000.0f;
            int8_t offset = constrain((int)(-driftPpm / 4.069f), -64, 63);
            rtc.calibrate(PCF8523_OneMinute, offset);
            Serial.printf("NTP sync: calibration drift=%lds over %lds (%.1f ppm), offset=%d\n",
                          drift, elapsed, driftPpm, offset);
        }
    } else {
        Serial.println("NTP sync: first sync, skipping calibration");
    }

    // Write UTC to RTC
    rtc.adjust(DateTime(utcNow));
    lastNtpSync = utcNow;
    Serial.printf("NTP sync: RTC set to %ld\n", (long)utcNow);

    WiFi.disconnect(true);

    // Restore timezone (configTime overwrites the TZ env var)
    setenv("TZ", POSIX_TZ, 1);
    tzset();

    // Log times for verification
    struct tm utcTm, localTm;
    gmtime_r(&utcNow, &utcTm);
    localtime_r(&utcNow, &localTm);
    Serial.printf("NTP sync: UTC = %d:%02d:%02d, local = %d:%02d:%02d (isDST=%d)\n",
                  utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec,
                  localTm.tm_hour, localTm.tm_min, localTm.tm_sec,
                  localTm.tm_isdst);

    // Log what the display will show
    String hour, minute;
    getCurrentTime(hour, minute);
    Serial.printf("NTP sync: display will show %s:%s\n", hour.c_str(), minute.c_str());

    return true;
}

bool syncTimeWithRetry(int maxAttempts /* = 3 */) {
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        if (attempt > 1) {
            Serial.printf("NTP sync: retry %d/%d...\n", attempt, maxAttempts);
            delay(1000);
        }
        if (syncTimeFromNtp()) return true;
    }
    return false;
}

bool isNtpSyncDue() {
    if (!rtcAvailable) return false;
    if (lastNtpSync == 0) return true;  // First boot
    time_t now = rtc.now().unixtime();
    return (now - lastNtpSync) >= SYNC_INTERVAL;
}

void setupTime() {
    if (!rtc.begin()) {
        Serial.println("ERROR: RTC not found - running without RTC");
        return;
    }
    rtcAvailable = true;
    Serial.println("RTC found");

    // If RTC lost power, set to compile time as fallback
    if (!rtc.initialized()) {
        Serial.println("RTC lost power, setting to compile time");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    time_t rtcNow = rtc.now().unixtime();
    Serial.printf("RTC reads: %ld (lastNtpSync=%ld)\n", (long)rtcNow, (long)lastNtpSync);

    // POSIX timezone: EST5EDT with US DST rules
    setenv("TZ", POSIX_TZ, 1);
    tzset();

    // Sync from NTP if due
    if (isNtpSyncDue()) {
        Serial.println("NTP sync due, starting...");
        bool ok = syncTimeWithRetry();
        Serial.printf("NTP sync %s\n", ok ? "succeeded" : "FAILED");
    } else {
        Serial.println("NTP sync not due, skipping");
    }
}

// Returns current time in 12-hour format
void getCurrentTime(String &hour, String &minute) {
    if (!rtcAvailable) {
        hour = "--";
        minute = "--";
        return;
    }
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

void resetElapsedTimer() {
    lastTimerUpdate = millis();
}

int getElapsedSeconds() {
    return (millis() - lastTimerUpdate) / 1000;
}

void debugCorruptTime() {
    rtc.adjust(DateTime(2000, 1, 1, 0, 0, 0));
    lastNtpSync = 0;
    Serial.println("RTC set to 2000-01-01 00:00, rebooting to sync...");
    Serial.flush();
    ESP.restart();
}

void debugSetElapsed(int seconds) {
    lastTimerUpdate = millis() - ((unsigned long)seconds * 1000);
}

void debugSetLastNtpSync(time_t epoch) {
    lastNtpSync = epoch;
}
