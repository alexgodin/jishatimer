#include "calibration.h"
#include <math.h>

CalibrationResult computeCalibration(long driftSeconds, long elapsedSeconds) {
    CalibrationResult r = {false, 0.0f, 0, nullptr};

    if (elapsedSeconds <= 0) {
        r.skipReason = "elapsed not positive";
        return r;
    }

    r.ppm = (float)driftSeconds / (float)elapsedSeconds * 1000000.0f;

    if (elapsedSeconds <= MIN_ELAPSED_SECONDS) {
        r.skipReason = "window too short to measure drift";
        return r;
    }

    if (fabsf(r.ppm) > MAX_PLAUSIBLE_PPM) {
        r.skipReason = "drift implausible for a crystal";
        return r;
    }

    // Round rather than truncate: (int) truncates toward zero, which biases
    // every correction toward under-correcting by up to a full 4.069 ppm LSB.
    int offset = (int)lroundf(-r.ppm / PPM_PER_LSB);

    // Unreachable while MAX_PLAUSIBLE_PPM is 200 (-> +/-49), kept so the
    // register bound holds if that limit is ever widened.
    if (offset < OFFSET_MIN) offset = OFFSET_MIN;
    if (offset > OFFSET_MAX) offset = OFFSET_MAX;

    r.applied = true;
    r.offset = offset;
    return r;
}
