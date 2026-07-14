#include "wifi/dns_responder.h"

#include <string.h>

#define DNS_HEADER_LEN 12U
#define DNS_ANSWER_LEN 16U

static size_t find_question_end(const uint8_t *query, size_t query_len)
{
    size_t pos = DNS_HEADER_LEN;
    size_t name_len = 0;

    while (pos < query_len) {
        uint8_t label_len = query[pos++];
        name_len++;
        if (label_len == 0) {
            break;
        }
        /* Compression pointers are not valid in a normal query name. */
        if (label_len > 63 || label_len > query_len - pos ||
            name_len > 255U - label_len) {
            return 0;
        }
        pos += label_len;
        name_len += label_len;
    }

    if (pos > query_len || pos + 4 > query_len) {
        return 0;
    }

    /* Only answer A/IN queries. */
    if (query[pos] != 0x00 || query[pos + 1] != 0x01 ||
        query[pos + 2] != 0x00 || query[pos + 3] != 0x01) {
        return 0;
    }
    return pos + 4;
}

size_t dns_build_captive_response(const uint8_t *query, size_t query_len,
                                  uint8_t *response, size_t response_capacity)
{
    if (!query || !response || query_len < DNS_HEADER_LEN) {
        return 0;
    }

    /* Standard query only, exactly one question and no other records. */
    if ((query[2] & 0xF8U) != 0 || query[3] != 0x00 ||
        query[4] != 0x00 || query[5] != 0x01 ||
        query[6] != 0x00 || query[7] != 0x00 ||
        query[8] != 0x00 || query[9] != 0x00 ||
        query[10] != 0x00 || query[11] != 0x00) {
        return 0;
    }

    size_t question_end = find_question_end(query, query_len);
    if (question_end == 0 || question_end != query_len ||
        question_end > SIZE_MAX - DNS_ANSWER_LEN) {
        return 0;
    }

    size_t response_len = question_end + DNS_ANSWER_LEN;
    if (response_len > response_capacity) {
        return 0;
    }

    memcpy(response, query, question_end);

    /* QR=1, AA=1; preserve RD from the query. */
    response[2] = (uint8_t)(0x84U | (query[2] & 0x01U));
    response[3] = 0x00;
    response[4] = 0x00; response[5] = 0x01;  /* QDCOUNT */
    response[6] = 0x00; response[7] = 0x01;  /* ANCOUNT */
    response[8] = 0x00; response[9] = 0x00;  /* NSCOUNT */
    response[10] = 0x00; response[11] = 0x00; /* ARCOUNT */

    size_t pos = question_end;
    response[pos++] = 0xC0; response[pos++] = 0x0C; /* query name */
    response[pos++] = 0x00; response[pos++] = 0x01; /* A */
    response[pos++] = 0x00; response[pos++] = 0x01; /* IN */
    response[pos++] = 0x00; response[pos++] = 0x00;
    response[pos++] = 0x00; response[pos++] = 0x0A; /* TTL 10 seconds */
    response[pos++] = 0x00; response[pos++] = 0x04;
    response[pos++] = 192; response[pos++] = 168;
    response[pos++] = 4; response[pos++] = 1;

    return pos;
}
