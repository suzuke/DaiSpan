#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "common/types.h"

#define S21_UART_NUM        UART_NUM_1
#define S21_BAUD_RATE       2400
#define S21_RX_PIN          4   /* ESP32-C3 SuperMini */
#define S21_TX_PIN          3
#define S21_BUF_SIZE        256
#define S21_ACK_TIMEOUT_MS  150
#define S21_FRAME_STX       0x02
#define S21_FRAME_ETX       0x03
#define S21_FRAME_ACK       0x06
#define S21_FRAME_NAK       0x15

/**
 * Initialize UART for S21 protocol (2400 baud, 8E2).
 */
esp_err_t s21_init(void);

/**
 * Send a command and wait for ACK.
 * Returns true on ACK received.
 */
bool s21_send_command(char cmd0, char cmd1, const uint8_t *payload, size_t payload_len);

/**
 * Read response after command. Fills cmd0/cmd1 and payload buffer.
 * Returns payload length, or -1 on error.
 */
int s21_read_response(uint8_t *cmd0, uint8_t *cmd1, uint8_t *payload, size_t max_payload_len);

/**
 * High-level: query device status (F1 -> G1).
 */
bool s21_query_status(ac_status_t *status);

/**
 * High-level: query room temperature (RH -> SH).
 */
bool s21_query_temperature(float *temperature);

/**
 * High-level: set power, mode, temp, fan (D1 command).
 */
bool s21_set_power_and_mode(bool power, uint8_t mode, float temp, uint8_t fan_speed);

/**
 * High-level: query swing state (F5 -> G5).
 */
bool s21_query_swing(bool *vertical, bool *horizontal);

/**
 * High-level: set swing (D5 command).
 */
bool s21_set_swing(bool vertical, bool horizontal);

/* Debug: get last raw G1 payload */
int s21_get_last_g1_raw(uint8_t *out, size_t max_len);

/* Encoding helpers */
uint8_t s21_encode_target_temp(float temp);
float s21_decode_target_temp(uint8_t encoded);
int s21_decode_int_sensor(const uint8_t *payload);
char s21_fan_speed_to_char(uint8_t fan_speed);
uint8_t s21_char_to_fan_speed(char c);
