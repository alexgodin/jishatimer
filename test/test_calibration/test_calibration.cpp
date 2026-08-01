#include <unity.h>
#include "calibration.h"

// 30 days — a realistic gap between charge-triggered syncs, and long enough
// that single-second drift resolves to a sane ppm.
static const long MONTH = 30L * 24 * 3600;

void setUp(void) {}
void tearDown(void) {}

// --- gating ---------------------------------------------------------------

void test_short_window_is_skipped(void) {
    // The gate is exclusive (elapsed <= MIN_ELAPSED_SECONDS skips), so exactly
    // one day is still rejected. At that window 1s of quantization reads as
    // 11.6 ppm -- plausible enough to be written, which is why the floor is a
    // floor and not a target. See the table in calibration.h.
    CalibrationResult r = computeCalibration(1, MIN_ELAPSED_SECONDS);
    TEST_ASSERT_FALSE(r.applied);
    TEST_ASSERT_NOT_NULL(r.skipReason);
}

void test_just_past_threshold_is_measured(void) {
    // 0 drift keeps ppm inside the plausibility bound so we isolate the gate.
    CalibrationResult r = computeCalibration(0, MIN_ELAPSED_SECONDS + 1);
    TEST_ASSERT_TRUE(r.applied);
}

void test_zero_or_negative_elapsed_is_skipped(void) {
    TEST_ASSERT_FALSE(computeCalibration(5, 0).applied);
    TEST_ASSERT_FALSE(computeCalibration(5, -100).applied);
}

// --- magnitude and sign ---------------------------------------------------

void test_rtc_running_fast_gets_negative_offset(void) {
    // +52s over 30d = +20.06 ppm -> -20.06/4.069 = -4.93 -> -5
    CalibrationResult r = computeCalibration(52, MONTH);
    TEST_ASSERT_TRUE(r.applied);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 20.06f, r.ppm);
    TEST_ASSERT_EQUAL_INT(-5, r.offset);
}

void test_rtc_running_slow_gets_positive_offset(void) {
    CalibrationResult r = computeCalibration(-52, MONTH);
    TEST_ASSERT_TRUE(r.applied);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -20.06f, r.ppm);
    TEST_ASSERT_EQUAL_INT(5, r.offset);
}

void test_no_drift_gives_zero_offset(void) {
    CalibrationResult r = computeCalibration(0, MONTH);
    TEST_ASSERT_TRUE(r.applied);
    TEST_ASSERT_EQUAL_INT(0, r.offset);
}

// Regression: (int) truncation toward zero under-corrected by up to one full
// 4.069 ppm LSB. 20.06 ppm -> -4.93, which truncates to -4 but rounds to -5.
void test_offset_rounds_rather_than_truncates(void) {
    // +52s over 30d -> -4.93: truncation gives -4, rounding gives -5.
    TEST_ASSERT_EQUAL_INT(-5, computeCalibration(52, MONTH).offset);
    // -63s over 30d -> +5.97: truncation gives 5, rounding gives 6.
    TEST_ASSERT_EQUAL_INT(6, computeCalibration(-63, MONTH).offset);
}

// --- plausibility bound ---------------------------------------------------

void test_at_plausibility_bound_is_applied(void) {
    // 200 ppm over 30 days = 518.4s
    CalibrationResult r = computeCalibration(518, MONTH);
    TEST_ASSERT_TRUE(r.applied);
    TEST_ASSERT_EQUAL_INT(-49, r.offset);
}

void test_beyond_plausibility_bound_is_skipped(void) {
    CalibrationResult r = computeCalibration(600, MONTH);  // ~231 ppm
    TEST_ASSERT_FALSE(r.applied);
    TEST_ASSERT_EQUAL_INT(0, r.offset);
    TEST_ASSERT_NOT_NULL(r.skipReason);
}

// Regression for the offset=34 written on the bench 2026-08-01: a single
// second of RTC quantization over the old 2h test window read as -138.7 ppm,
// passed the plausibility bound, and burned a 12s/day error into the part.
void test_quantization_at_short_window_is_skipped(void) {
    CalibrationResult r = computeCalibration(-1, 7208);
    TEST_ASSERT_FALSE(r.applied);
    TEST_ASSERT_EQUAL_INT(0, r.offset);
}

// The same +/-1s is harmless once the window is long enough to dilute it.
void test_quantization_over_a_week_is_negligible(void) {
    CalibrationResult r = computeCalibration(1, 7 * 24 * 3600);
    TEST_ASSERT_TRUE(r.applied);
    TEST_ASSERT_EQUAL_INT(0, r.offset);  // 1.65 ppm rounds to no correction
}

// Regression for the state seen on the bench 2026-08-01: the RTC had been
// reseeded from firmware compile time, so it read 7.19 days behind while the
// test claimed a 2h window. The old code turned that into -86,000,000 ppm and
// clamped it to +63, writing a maximum bogus correction to the register.
void test_reseeded_rtc_does_not_calibrate(void) {
    CalibrationResult r = computeCalibration(-621400, 7200);
    TEST_ASSERT_FALSE(r.applied);
    TEST_ASSERT_EQUAL_INT(0, r.offset);
}

// --- register bounds ------------------------------------------------------

void test_offset_stays_in_register_range(void) {
    // Sweep the whole plausible domain; nothing may exceed the 7-bit field.
    for (long drift = -520; drift <= 520; drift += 5) {
        CalibrationResult r = computeCalibration(drift, MONTH);
        if (r.applied) {
            TEST_ASSERT_TRUE(r.offset >= OFFSET_MIN && r.offset <= OFFSET_MAX);
        }
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_short_window_is_skipped);
    RUN_TEST(test_just_past_threshold_is_measured);
    RUN_TEST(test_zero_or_negative_elapsed_is_skipped);
    RUN_TEST(test_rtc_running_fast_gets_negative_offset);
    RUN_TEST(test_rtc_running_slow_gets_positive_offset);
    RUN_TEST(test_no_drift_gives_zero_offset);
    RUN_TEST(test_offset_rounds_rather_than_truncates);
    RUN_TEST(test_at_plausibility_bound_is_applied);
    RUN_TEST(test_beyond_plausibility_bound_is_skipped);
    RUN_TEST(test_quantization_at_short_window_is_skipped);
    RUN_TEST(test_quantization_over_a_week_is_negligible);
    RUN_TEST(test_reseeded_rtc_does_not_calibrate);
    RUN_TEST(test_offset_stays_in_register_range);
    return UNITY_END();
}
