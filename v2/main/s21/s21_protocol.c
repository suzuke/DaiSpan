#include "s21/s21_protocol.h"
#include "common/types.h"
#include "common/debug.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = LOG_TAG_S21;

/* Debug: last raw G1 payload bytes */
static uint8_t last_g1_raw[8] = {0};
static int last_g1_len = 0;

esp_err_t s21_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = S21_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(S21_UART_NUM, S21_BUF_SIZE * 2, S21_BUF_SIZE * 2, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(S21_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(S21_UART_NUM, S21_TX_PIN, S21_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Flush RX buffer */
    uart_flush(S21_UART_NUM);
    ESP_LOGI(TAG, "UART initialized: %d baud, 8E2, RX=%d TX=%d", S21_BAUD_RATE, S21_RX_PIN, S21_TX_PIN);
    return ESP_OK;
}

static uint8_t s21_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

bool s21_send_command(char cmd0, char cmd1, const uint8_t *payload, size_t payload_len)
{
    /* Build frame: STX + cmd0 + cmd1 + payload + checksum + ETX */
    uint8_t frame[32];
    size_t idx = 0;

    frame[idx++] = S21_FRAME_STX;
    frame[idx++] = (uint8_t)cmd0;
    frame[idx++] = (uint8_t)cmd1;

    if (payload && payload_len > 0) {
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }

    /* Checksum: sum of everything between STX and ETX (exclusive) */
    uint8_t cksum = s21_checksum(&frame[1], idx - 1);
    frame[idx++] = cksum;
    frame[idx++] = S21_FRAME_ETX;

    /* Flush RX before sending */
    uart_flush_input(S21_UART_NUM);

    /* Send frame */
    int written = uart_write_bytes(S21_UART_NUM, frame, idx);
    if (written != (int)idx) {
        ESP_LOGE(TAG, "UART write failed: wrote %d/%zu", written, idx);
        return false;
    }

    /* Wait for ACK */
    uint8_t ack;
    int len = uart_read_bytes(S21_UART_NUM, &ack, 1, pdMS_TO_TICKS(S21_ACK_TIMEOUT_MS));
    if (len != 1) {
        ESP_LOGW(TAG, "ACK timeout for %c%c", cmd0, cmd1);
        return false;
    }

    if (ack == S21_FRAME_NAK) {
        ESP_LOGW(TAG, "NAK received for %c%c", cmd0, cmd1);
        return false;
    }

    if (ack != S21_FRAME_ACK) {
        ESP_LOGW(TAG, "Unexpected response 0x%02X for %c%c", ack, cmd0, cmd1);
        return false;
    }

    return true;
}

int s21_read_response(uint8_t *cmd0, uint8_t *cmd1, uint8_t *payload, size_t max_payload_len)
{
    uint8_t buf[64];
    int total = 0;

    /* Read with timeout until ETX found */
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(500);

    while (total < (int)sizeof(buf)) {
        int len = uart_read_bytes(S21_UART_NUM, &buf[total], 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            if ((xTaskGetTickCount() - start) > timeout) {
                ESP_LOGW(TAG, "Response timeout after %d bytes", total);
                return -1;
            }
            continue;
        }
        total++;
        if (buf[total - 1] == S21_FRAME_ETX) {
            break;
        }
    }

    /* Minimum frame: STX + cmd0 + cmd1 + checksum + ETX = 5 bytes */
    if (total < 5 || buf[0] != S21_FRAME_STX || buf[total - 1] != S21_FRAME_ETX) {
        ESP_LOGW(TAG, "Invalid frame: len=%d", total);
        return -1;
    }

    /* Verify checksum */
    uint8_t expected_cksum = s21_checksum(&buf[1], total - 3); /* exclude STX, checksum, ETX */
    uint8_t actual_cksum = buf[total - 2];
    if (expected_cksum != actual_cksum) {
        ESP_LOGW(TAG, "Checksum error: expected 0x%02X got 0x%02X", expected_cksum, actual_cksum);
        return -1;
    }

    /* Send ACK */
    uint8_t ack = S21_FRAME_ACK;
    uart_write_bytes(S21_UART_NUM, &ack, 1);

    *cmd0 = buf[1];
    *cmd1 = buf[2];

    size_t plen = (size_t)(total - 5); /* total - STX - cmd0 - cmd1 - cksum - ETX */
    if (plen > max_payload_len) {
        plen = max_payload_len;
    }
    if (plen > 0 && payload) {
        memcpy(payload, &buf[3], plen);
    }

    return (int)plen;
}

/* Encoding helpers */

uint8_t s21_encode_target_temp(float temp)
{
    /* S21: temperature = (encoded - '@') * 0.5 + 18 */
    /* So encoded = (temp - 18) * 2 + '@' */
    int offset = (int)((temp - 18.0f) * 2.0f + 0.5f);
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

/* High-level operations */

bool s21_set_power_and_mode(bool power, uint8_t mode, float temp, uint8_t fan_speed)
{
    uint8_t payload[4];
    payload[0] = power ? '1' : '0';
    payload[1] = '0' + mode;
    payload[2] = s21_encode_target_temp(temp);
    payload[3] = s21_fan_speed_to_char(fan_speed);

    ESP_LOGI(TAG, "D1: power=%d mode=%d temp=%.1f fan=%d", power, mode, temp, fan_speed);

    bool ok = s21_send_command('D', '1', payload, 4);
    if (!ok) {
        /* Retry once */
        vTaskDelay(pdMS_TO_TICKS(50));
        ok = s21_send_command('D', '1', payload, 4);
    }
    return ok;
}

bool s21_query_status(ac_status_t *status)
{
    if (!s21_send_command('F', '1', NULL, 0)) {
        return false;
    }

    uint8_t cmd0, cmd1, payload[8];
    int len = s21_read_response(&cmd0, &cmd1, payload, sizeof(payload));

    if (len < 4 || cmd0 != 'G' || cmd1 != '1') {
        ESP_LOGW(TAG, "Invalid status response: cmd=%c%c len=%d", cmd0, cmd1, len);
        return false;
    }

    ESP_LOGD(TAG, "G1 raw: [0x%02X 0x%02X 0x%02X 0x%02X] '%c%c%c%c' len=%d",
             payload[0], payload[1], payload[2], payload[3],
             payload[0], payload[1], payload[2], payload[3], len);

    /* Store raw for debug API */
    memcpy(last_g1_raw, payload, len < 8 ? len : 8);
    last_g1_len = len;

    status->power = (payload[0] == '1');
    status->mode = payload[1] - '0';
    status->target_temp = s21_decode_target_temp(payload[2]);
    status->fan_speed = s21_char_to_fan_speed(payload[3]);
    status->valid = true;

    ESP_LOGI(TAG, "Status: power=%d mode=%d temp=%.1f fan=%d",
             status->power, status->mode, status->target_temp, status->fan_speed);
    return true;
}

bool s21_query_temperature(float *temperature)
{
    if (!s21_send_command('R', 'H', NULL, 0)) {
        return false;
    }

    uint8_t cmd0, cmd1, payload[8];
    int len = s21_read_response(&cmd0, &cmd1, payload, sizeof(payload));

    if (len < 4 || cmd0 != 'S' || cmd1 != 'H') {
        return false;
    }

    ESP_LOGD(TAG, "SH raw: [0x%02X 0x%02X 0x%02X 0x%02X] '%c%c%c%c'",
             payload[0], payload[1], payload[2], payload[3],
             payload[0], payload[1], payload[2], payload[3]);

    /* Validate digits */
    for (int i = 0; i < 3; i++) {
        if (payload[i] < '0' || payload[i] > '9') {
            return false;
        }
    }
    if (payload[3] != '+' && payload[3] != '-') {
        return false;
    }

    int raw = s21_decode_int_sensor(payload);
    *temperature = (float)raw * 0.1f;

    if (*temperature < -50.0f || *temperature > 100.0f) {
        return false;
    }

    ESP_LOGD(TAG, "Room temp: %.1f C", *temperature);
    return true;
}

bool s21_query_swing(bool *vertical, bool *horizontal)
{
    if (!s21_send_command('F', '5', NULL, 0)) {
        return false;
    }

    uint8_t cmd0, cmd1, payload[8];
    int len = s21_read_response(&cmd0, &cmd1, payload, sizeof(payload));

    if (len < 1 || cmd0 != 'G' || cmd1 != '5') {
        return false;
    }

    ESP_LOGD(TAG, "G5 raw: [0x%02X 0x%02X 0x%02X 0x%02X] len=%d",
             payload[0], len > 1 ? payload[1] : 0,
             len > 2 ? payload[2] : 0, len > 3 ? payload[3] : 0, len);

    /* G5 byte[0]: swing flags as ASCII digit (bit0=V, bit1=H) */
    uint8_t flags = payload[0] - '0';
    *vertical = (flags & 1) != 0;
    *horizontal = (flags & 2) != 0;

    ESP_LOGD(TAG, "Swing: V=%d H=%d", *vertical, *horizontal);
    return true;
}

int s21_get_last_g1_raw(uint8_t *out, size_t max_len)
{
    int n = last_g1_len < (int)max_len ? last_g1_len : (int)max_len;
    memcpy(out, last_g1_raw, n);
    return n;
}

bool s21_set_swing(bool vertical, bool horizontal)
{
    /*
     * D5 payload byte[0]: swing flags as ASCII digit
     *   bit 0 = vertical, bit 1 = horizontal
     *   '0'=off, '1'=V, '2'=H, '3'=V+H
     * Byte[1]: '?' when swing active, '0' when off
     * Bytes[2-3]: reserved ('0')
     */
    uint8_t flags = (vertical ? 1 : 0) | (horizontal ? 2 : 0);
    uint8_t payload[4];
    payload[0] = '0' + flags;
    payload[1] = flags ? '?' : '0';
    payload[2] = '0';
    payload[3] = '0';

    ESP_LOGI(TAG, "D5: flags=%d [0x%02X 0x%02X 0x%02X 0x%02X]",
             flags, payload[0], payload[1], payload[2], payload[3]);
    bool ok = s21_send_command('D', '5', payload, 4);
    ESP_LOGI(TAG, "D5 result: %s", ok ? "ACK" : "FAIL");
    return ok;
}
