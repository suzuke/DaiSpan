#include "s21/s21_codec.h"
#include "common/types.h"

uint8_t s21_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

uint8_t s21_encode_target_temp(float temp)
{
    /* S21: temperature = (encoded - '@') * 0.5 + 18 */
    /* So encoded = (temp - 18) * 2 + '@' */
    /* Round symmetrically: the naive "+0.5 then truncate" rounds toward zero,
     * which is wrong for sub-18C setpoints (negative offset) and encodes them
     * one half-step too high (e.g. 16.0C -> 16.5C). Round away from zero. */
    float steps = (temp - 18.0f) * 2.0f;
    int offset = (int)(steps >= 0.0f ? steps + 0.5f : steps - 0.5f);
    return (uint8_t)('@' + offset);
}

float s21_decode_target_temp(uint8_t encoded)
{
    return 18.0f + (float)(encoded - '@') * 0.5f;
}

int s21_decode_int_sensor(const uint8_t *payload)
{
    /* payload[0..2] = ASCII digits (little-endian: ones, tens, hundreds)
     * payload[3] = '+' or '-' */
    int val = (payload[0] - '0') + (payload[1] - '0') * 10 + (payload[2] - '0') * 100;
    if (payload[3] == '-') {
        val = -val;
    }
    return val;
}

char s21_fan_speed_to_char(uint8_t fan_speed)
{
    switch (fan_speed) {
        case AC_FAN_AUTO:  return 'A';
        case AC_FAN_QUIET: return 'B';
        case AC_FAN_1:     return '3';
        case AC_FAN_2:     return '4';
        case AC_FAN_3:     return '5';
        case AC_FAN_4:     return '6';
        case AC_FAN_5:     return '7';
        default:           return 'A';
    }
}

uint8_t s21_char_to_fan_speed(char c)
{
    switch (c) {
        case 'A': return AC_FAN_AUTO;
        case 'B': return AC_FAN_QUIET;
        case '3': return AC_FAN_1;
        case '4': return AC_FAN_2;
        case '5': return AC_FAN_3;
        case '6': return AC_FAN_4;
        case '7': return AC_FAN_5;
        default:  return AC_FAN_AUTO;
    }
}
