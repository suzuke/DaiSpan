/*
 * Host unit tests for the pure S21 codec (no ESP-IDF, no hardware needed).
 * Build & run:  cd v2/tests && make
 *
 * These exercise exactly the class of boundary bug that shipped silently before
 * (sub-18C target-temp rounding), plus round-trips for fan/temp and the
 * checksum and sensor decoders.
 */
#include "s21/s21_codec.h"
#include "common/types.h"
#include <stdio.h>
#include <math.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond, ...) do {                                  \
    checks++;                                                  \
    if (!(cond)) {                                             \
        failures++;                                            \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);           \
        printf(__VA_ARGS__);                                   \
        printf("\n");                                          \
    }                                                          \
} while (0)

/* Every selectable HomeKit setpoint (16.0..30.0, step 0.5) must round-trip. */
static void test_temp_roundtrip(void)
{
    for (float t = 16.0f; t <= 30.0f + 0.01f; t += 0.5f) {
        uint8_t enc = s21_encode_target_temp(t);
        float dec = s21_decode_target_temp(enc);
        CHECK(fabsf(dec - t) < 0.01f,
              "temp %.1f -> 0x%02X -> %.1f (expected %.1f)", t, enc, dec, t);
    }
}

/* Regression for the sub-18C off-by-0.5 bug: low setpoints must NOT round up. */
static void test_temp_sub18_regression(void)
{
    CHECK(s21_decode_target_temp(s21_encode_target_temp(16.0f)) == 16.0f, "16.0C must stay 16.0");
    CHECK(s21_decode_target_temp(s21_encode_target_temp(16.5f)) == 16.5f, "16.5C must stay 16.5");
    CHECK(s21_decode_target_temp(s21_encode_target_temp(17.0f)) == 17.0f, "17.0C must stay 17.0");
    CHECK(s21_decode_target_temp(s21_encode_target_temp(17.5f)) == 17.5f, "17.5C must stay 17.5");
    CHECK(s21_decode_target_temp(s21_encode_target_temp(18.0f)) == 18.0f, "18.0C must stay 18.0");
    CHECK(s21_decode_target_temp(s21_encode_target_temp(25.0f)) == 25.0f, "25.0C must stay 25.0");
}

static void test_fan_roundtrip(void)
{
    uint8_t fans[] = { AC_FAN_AUTO, AC_FAN_QUIET, AC_FAN_1, AC_FAN_2, AC_FAN_3, AC_FAN_4, AC_FAN_5 };
    for (unsigned i = 0; i < sizeof(fans) / sizeof(fans[0]); i++) {
        char c = s21_fan_speed_to_char(fans[i]);
        uint8_t back = s21_char_to_fan_speed(c);
        CHECK(back == fans[i], "fan %u -> '%c' -> %u", fans[i], c, back);
    }
}

static void test_checksum(void)
{
    uint8_t data[] = { 'D', '1', '1', '3' };
    uint8_t expect = (uint8_t)('D' + '1' + '1' + '3');
    CHECK(s21_checksum(data, sizeof(data)) == expect, "checksum mismatch");
    CHECK(s21_checksum(data, 0) == 0, "empty checksum must be 0");
}

static void test_int_sensor(void)
{
    /* digits are little-endian: ones, tens, hundreds, then sign */
    uint8_t pos[] = { '0', '5', '2', '+' };  /* 0 + 50 + 200 = 250 -> 25.0C */
    CHECK(s21_decode_int_sensor(pos) == 250, "expected 250, got %d", s21_decode_int_sensor(pos));
    uint8_t neg[] = { '0', '5', '2', '-' };
    CHECK(s21_decode_int_sensor(neg) == -250, "expected -250, got %d", s21_decode_int_sensor(neg));
    uint8_t zero[] = { '0', '0', '0', '+' };
    CHECK(s21_decode_int_sensor(zero) == 0, "expected 0, got %d", s21_decode_int_sensor(zero));
}

int main(void)
{
    test_temp_roundtrip();
    test_temp_sub18_regression();
    test_fan_roundtrip();
    test_checksum();
    test_int_sensor();

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
