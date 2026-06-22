#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * Pure S21 wire-format codec: encode/decode helpers with no ESP-IDF or hardware
 * dependencies, so they can be unit-tested on the host (see v2/tests/). The
 * transport layer (UART framing, query/set ops) lives in s21_protocol.c.
 */

/* Additive checksum over the bytes between STX and ETX (exclusive). */
uint8_t s21_checksum(const uint8_t *data, size_t len);

/* Target temperature <-> S21 encoded byte: temp = (byte - '@') * 0.5 + 18. */
uint8_t s21_encode_target_temp(float temp);
float   s21_decode_target_temp(uint8_t encoded);

/* Decode a 4-byte ASCII signed sensor value: payload[0..2] are the ones, tens
 * and hundreds digits (little-endian), payload[3] is '+' or '-'. */
int s21_decode_int_sensor(const uint8_t *payload);

/* Fan speed (AC_FAN_*) <-> S21 character. */
char    s21_fan_speed_to_char(uint8_t fan_speed);
uint8_t s21_char_to_fan_speed(char c);
