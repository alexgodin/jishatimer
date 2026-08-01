#ifndef CALIBRATION_H
#define CALIBRATION_H

// RTC drift -> PCF8523 offset-register math.
//
// Deliberately free of Arduino/RTClib includes so it builds and tests on the
// host (see test/test_calibration). Callers do the I2C and the logging.

// PCF8523 offset register, one-minute mode: 4.069 ppm per LSB,
// 7-bit two's complement so the usable range is -64..+63.
const float PPM_PER_LSB = 4.069f;
const int OFFSET_MIN = -64;
const int OFFSET_MAX = 63;

// The RTC counts whole seconds, so any comparison against NTP carries +/-1s
// of quantization regardless of how good the crystal is. That one second is
// worth wildly different ppm depending on the window:
//
//     1 hour   -> 278 ppm      1 day    -> 11.6 ppm
//     2 hours  -> 139 ppm      7 days   -> 1.65 ppm
//
// A real 32.768kHz crystal is off by maybe 5-20 ppm, so anything shorter than
// a day is measuring its own rounding error. The old 3600s gate let a single
// second of quantization write a 138 ppm correction — observed on hardware
// 2026-08-01, where drift=-1s over 7208s produced offset=34 and would have
// made a healthy RTC gain ~12s/day.
//
// Note this is a floor, not a target: even at exactly one day the reading is
// only good to ~11.6 ppm. Longer gaps between syncs calibrate better.
const long MIN_ELAPSED_SECONDS = 86400;

// A 32.768kHz crystal is specified well inside this; anything beyond it means
// the RTC held a wrong value for some reason other than crystal error (lost
// power, missed sync window, manual adjust) and the ratio is meaningless.
const float MAX_PLAUSIBLE_PPM = 200.0f;

struct CalibrationResult {
    bool applied;         // true -> write `offset` to the offset register
    float ppm;            // measured error rate (valid whenever elapsed > 0)
    int offset;           // register value; 0 when !applied
    const char* skipReason;  // nullptr when applied
};

// drift = rtcTime - trueTime (positive when the RTC runs fast)
// elapsed = trueTime - lastSyncTime
CalibrationResult computeCalibration(long driftSeconds, long elapsedSeconds);

#endif
