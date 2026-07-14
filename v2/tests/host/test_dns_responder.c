#include "wifi/dns_responder.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const uint8_t valid_query[] = {
    0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm', 0x00,
    0x00, 0x01, 0x00, 0x01,
};

static void test_valid_query(void)
{
    uint8_t response[512] = {0};
    size_t len = dns_build_captive_response(valid_query, sizeof(valid_query),
                                             response, sizeof(response));

    assert(len == sizeof(valid_query) + 16);
    assert(response[0] == 0x12 && response[1] == 0x34);
    assert(response[2] == 0x85 && response[3] == 0x00);
    assert(response[6] == 0x00 && response[7] == 0x01);
    assert(response[len - 4] == 192 && response[len - 3] == 168);
    assert(response[len - 2] == 4 && response[len - 1] == 1);
}

static void test_rejects_malformed_or_unsupported_queries(void)
{
    uint8_t response[512];
    uint8_t malformed[512];
    memset(malformed, 0x3f, sizeof(malformed));
    malformed[2] = 0x01;
    malformed[4] = 0x00;
    malformed[5] = 0x01;

    assert(dns_build_captive_response(malformed, sizeof(malformed),
                                      response, sizeof(response)) == 0);

    uint8_t aaaa_query[sizeof(valid_query)];
    memcpy(aaaa_query, valid_query, sizeof(aaaa_query));
    aaaa_query[sizeof(aaaa_query) - 3] = 0x1c;
    assert(dns_build_captive_response(aaaa_query, sizeof(aaaa_query),
                                      response, sizeof(response)) == 0);

    uint8_t trailing_query[sizeof(valid_query) + 1];
    memcpy(trailing_query, valid_query, sizeof(valid_query));
    trailing_query[sizeof(trailing_query) - 1] = 0;
    assert(dns_build_captive_response(trailing_query, sizeof(trailing_query),
                                      response, sizeof(response)) == 0);

    uint8_t opcode_query[sizeof(valid_query)];
    memcpy(opcode_query, valid_query, sizeof(opcode_query));
    opcode_query[2] |= 0x08;
    assert(dns_build_captive_response(opcode_query, sizeof(opcode_query),
                                      response, sizeof(response)) == 0);
}

static void test_honors_output_capacity(void)
{
    uint8_t response[sizeof(valid_query) + 15];
    assert(dns_build_captive_response(valid_query, sizeof(valid_query),
                                      response, sizeof(response)) == 0);
}

int main(void)
{
    test_valid_query();
    test_rejects_malformed_or_unsupported_queries();
    test_honors_output_capacity();
    return 0;
}
