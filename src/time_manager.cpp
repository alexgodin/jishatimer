#include "time_manager.h"
#include "battery.h"
#include "calibration.h"
#include <RTClib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_sntp.h>

const char* POSIX_TZ = "EST5EDT,M3.2.0,M11.1.0";

// Forward declarations
void getCurrentTime(String &hour, String &minute);
static void refreshTimeValidity();
RTC_PCF8523 rtc;
static bool rtcAvailable = false;
RTC_DATA_ATTR time_t lastNtpSync = 0;  // Persists across deep sleep
RTC_DATA_ATTR bool wasCharging = false;     // Persists across deep sleep
RTC_DATA_ATTR bool lastSyncFailed = false;  // Persists across deep sleep
RTC_DATA_ATTR bool recoverySyncTried = false;  // Cleared by power-on, not sleep

// Any time before this is not a clock, it is a reset default: the PCF8523
// powers up at 2000-01-01, and the pre-2026 firmware seeded __DATE__. Also
// what distinguishes a configTime()-set system clock from seconds-since-boot.
static const time_t MIN_PLAUSIBLE_EPOCH = 1767225600;  // 2026-01-01 UTC

// PCF8523 offset register: RTClib inherits RTC_I2C privately, so
// read_register() isn't reachable from here.
static const uint8_t PCF8523_I2C_ADDR = 0x68;
static const uint8_t PCF8523_REG_OFFSET = 0x0E;

// Fault injection flags for testing
bool debugWifiFail = false;
bool debugNtpTimeout = false;
bool debugCalibrationDryRun = false;

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

    // Everything below touches the RTC over I2C. With no part on the bus
    // RTClib's read_register() returns uninitialized stack rather than an
    // error, so an unguarded lostPower() would invent a hardware fault.
    // The sync itself is still worth doing: configTime() has set the system
    // clock, which getCurrentTime() can fall back to.
    if (rtcAvailable) {
        // Measure RTC drift before overwriting, and auto-calibrate
        time_t rtcNow = rtc.now().unixtime();
        Serial.printf("NTP sync: NTP=%ld, RTC=%ld, diff=%lds\n",
                      (long)utcNow, (long)rtcNow, (long)(rtcNow - utcNow));

        if (lastNtpSync > 0) {
            long drift = (long)(rtcNow - utcNow);
            long elapsed = (long)(utcNow - lastNtpSync);
            CalibrationResult cal = computeCalibration(drift, elapsed);
            if (cal.applied) {
                if (!debugCalibrationDryRun) {
                    rtc.calibrate(PCF8523_OneMinute, (int8_t)cal.offset);
                }
                Serial.printf("NTP sync: calibration%s drift=%lds over %lds (%.1f ppm), offset=%d\n",
                              debugCalibrationDryRun ? " DRYRUN" : "",
                              drift, elapsed, cal.ppm, cal.offset);
            } else {
                Serial.printf("NTP sync: calibration skipped drift=%lds over %lds (%.1f ppm) - %s\n",
                              drift, elapsed, cal.ppm, cal.skipReason);
            }
        } else {
            Serial.println("NTP sync: first sync, skipping calibration");
        }

        // Write UTC to RTC. adjust() also clears the oscillator-stop flag and
        // enables battery switchover, but per the datasheet it cannot clear OS
        // until the crystal has stabilized (up to 2s after power is applied).
        // A sync that lands inside that window would leave the part reporting
        // lost time forever, so confirm and retry once.
        rtc.adjust(DateTime(utcNow));
        if (rtc.lostPower()) {
            Serial.println("NTP sync: OS flag still set after adjust, retrying...");
            delay(2000);
            // time(nullptr) is trustworthy here specifically because configTime()
            // above has set the system clock; it is not before the first sync.
            rtc.adjust(DateTime(time(nullptr)));
            if (rtc.lostPower()) {
                Serial.println("NTP sync: WARNING oscillator-stop flag will not "
                               "clear - RTC crystal or backup power is faulty");
            }
        }
    }
    lastNtpSync = utcNow;
    refreshTimeValidity();
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

void checkChargeSync(bool charging) {
    if (charging && !wasCharging && rtcAvailable) {
        Serial.println("Charge started, syncing NTP...");
        bool ok = syncTimeWithRetry();
        Serial.printf("NTP sync %s\n", ok ? "succeeded" : "FAILED");
        lastSyncFailed = !ok;
    }
    wasCharging = charging;
}

bool didLastSyncFail() {
    return lastSyncFailed;
}

const time_t SYNC_FRESHNESS_WINDOW = 24 * 60 * 60;  // 24 hours

bool syncedRecently() {
    if (!rtcAvailable || lastNtpSync == 0) return false;
    time_t now = rtc.now().unixtime();
    return (now - lastNtpSync) < SYNC_FRESHNESS_WINDOW;
}

// Cached rather than probed per call: getCurrentTime() runs every display
// frame, and validity only changes at setup and after a successful sync.
static bool rtcTimeValid = false;

// Chip state as found at boot, before the first adjust() zeroes CONTROL_3
// and makes initialized() read true regardless — the diagnostic has to
// report what we saw first.
static bool bootOsStopped = false;
static bool bootConfigured = false;

bool rtcIsPresent() {
    return rtcAvailable;
}

void getRtcBootState(bool &osStopped, bool &configured) {
    osStopped = bootOsStopped;
    configured = bootConfigured;
}

static void refreshTimeValidity() {
    // OS set means the oscillator stopped and the count is meaningless.
    //
    // Past that, the only thing that separates a real clock from a reset
    // default is the value itself. rtc.initialized() cannot help: it reads
    // CONTROL_3, which adjust() zeroes, so after the first sync it is true
    // unconditionally. Without the epoch bound, a part sitting at 2000-01-01
    // (power-on default, or debugCorruptTime) reads as valid and the display
    // shows a confident 7:00.
    if (!rtcAvailable || rtc.lostPower()) {
        rtcTimeValid = false;
        return;
    }
    rtcTimeValid = rtc.now().unixtime() >= (uint32_t)MIN_PLAUSIBLE_EPOCH;
}

bool timeIsValid() {
    return rtcTimeValid;
}

int readCalibrationOffset() {
    if (!rtcAvailable) return 0;
    Wire.beginTransmission(PCF8523_I2C_ADDR);
    Wire.write(PCF8523_REG_OFFSET);
    if (Wire.endTransmission() != 0) return 0;
    if (Wire.requestFrom(PCF8523_I2C_ADDR, (uint8_t)1) != 1) return 0;
    uint8_t reg = Wire.read();
    // Bit 7 is the mode flag; bits 6:0 are a 7-bit two's complement offset.
    int8_t offset = reg & 0x7F;
    if (offset & 0x40) offset |= 0x80;  // sign-extend
    return offset;
}

void clearCalibration() {
    if (!rtcAvailable) return;
    rtc.calibrate(PCF8523_OneMinute, 0);
}

void setupTime() {
    if (!rtc.begin()) {
        Serial.println("ERROR: RTC not found - running without RTC");
        return;
    }
    rtcAvailable = true;
    Serial.println("RTC found");

    bootOsStopped = rtc.lostPower();
    bootConfigured = rtc.initialized();
    Serial.printf("RTC state: oscillator_stopped=%d configured=%d\n",
                  bootOsStopped, bootConfigured);

    if (!bootConfigured) {
        // Deliberately NOT seeding compile time here. That produced a clock
        // confidently wrong by however long since the build (7 days, at one
        // point) and, because __TIME__ is the build machine's local time
        // written into a field everything else reads as UTC, wrong again by
        // the UTC offset on top. A blank display is honest; a plausible
        // wrong time is not.
        //
        // Battery switchover needs no action here: RTClib enables it inside
        // adjust(), and adjust() is on every path that puts a real time into
        // the part. Until one lands there is no count worth protecting.
        Serial.println("RTC came up unconfigured - time left unset until NTP sync");
    }

    // Clear the STOP bit in case the counter was halted.
    rtc.start();
    refreshTimeValidity();

    time_t rtcNow = rtc.now().unixtime();
    Serial.printf("RTC reads: %ld (lastNtpSync=%ld, valid=%d)\n",
                  (long)rtcNow, (long)lastNtpSync, timeIsValid());

    // POSIX timezone: EST5EDT with US DST rules
    setenv("TZ", POSIX_TZ, 1);
    tzset();

    // Sync from NTP on a charge-start edge (covers "already plugged in at boot")
    bool chargeSyncRan = isCharging() && !wasCharging && rtcAvailable;
    checkChargeSync(isCharging());

    // With no valid time there is nothing to display, so spend the radio once
    // to try to recover it. recoverySyncTried is RTC_DATA_ATTR: it survives
    // deep sleep but is cleared by a real power-on, which is exactly when the
    // RTC would have lost its count. That keeps this to one attempt per
    // power cycle rather than one per orientation wake.
    //
    // Skipped if the charge edge just ran a sync: a charge-start and a missing
    // clock are two reasons to want the same thing, not two retry budgets.
    // Running both back to back stalls setup() for ~95s with no WiFi.
    if (!chargeSyncRan && !timeIsValid() && !recoverySyncTried) {
        recoverySyncTried = true;
        Serial.println("RTC has no valid time, attempting recovery sync...");
        bool ok = syncTimeWithRetry();
        Serial.printf("Recovery sync %s\n", ok ? "succeeded" : "FAILED");
        lastSyncFailed = !ok;
    }
}

// Returns current time in 12-hour format
void getCurrentTime(String &hour, String &minute) {
    time_t utc;
    if (timeIsValid()) {
        utc = rtc.now().unixtime();
    } else if (lastNtpSync > 0 && time(nullptr) >= MIN_PLAUSIBLE_EPOCH) {
        // The RTC is untrustworthy but SNTP set the system clock this boot,
        // so we do know the time — a faulty crystal shouldn't blank a display
        // the device can still fill. The epoch bound is what proves the clock
        // was set: unset, time() reads as seconds-since-boot.
        utc = time(nullptr);
    } else {
        // Nothing worth rendering. Placeholders rather than whatever the
        // counter happens to hold.
        hour = "--";
        minute = "--";
        return;
    }
    struct tm local;
    localtime_r(&utc, &local);

    int h = local.tm_hour % 12;
    h = (h == 0) ? 12 : h;
    if (h < 10) {
        hour = "0" + String(h);
    } else {
        hour = String(h);
    }

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
    wasCharging = false;
    Serial.println("RTC set to 2000-01-01 00:00, rebooting to sync...");
    Serial.flush();
    ESP.restart();
}

void debugSetElapsed(int seconds) {
    lastTimerUpdate = millis() - ((unsigned long)seconds * 1000);
}

static time_t savedLastNtpSync = 0;
static bool lastNtpSyncBackdated = false;

void debugBackdateLastNtpSync(long seconds) {
    if (!rtcAvailable) {
        Serial.println("DEBUG: no RTC, cannot backdate lastNtpSync");
        return;
    }
    // Must come off the RTC, not time(nullptr): the ESP32 system clock is only
    // ever set by configTime() during a sync, so before the first sync of a
    // boot it reads as seconds-since-boot and backdating it goes negative.
    savedLastNtpSync = lastNtpSync;
    lastNtpSyncBackdated = true;
    lastNtpSync = rtc.now().unixtime() - seconds;
    Serial.printf("DEBUG: lastNtpSync backdated to %ld\n", (long)lastNtpSync);
}

void debugRestoreLastNtpSync() {
    // A backdate that no sync consumed stays armed: lastNtpSync is
    // RTC_DATA_ATTR, so the next real charge-triggered sync would calibrate
    // off the fabricated window and write a bogus offset to the part.
    if (!lastNtpSyncBackdated) return;
    lastNtpSync = savedLastNtpSync;
    lastNtpSyncBackdated = false;
    Serial.printf("DEBUG: lastNtpSync restored to %ld\n", (long)lastNtpSync);
}
