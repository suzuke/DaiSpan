#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Build a minimal authoritative A response for a single-question DNS query.
 * Returns the response length, or 0 when the query is malformed/unsupported
 * or the output buffer is too small.
 */
size_t dns_build_captive_response(const uint8_t *query, size_t query_len,
                                  uint8_t *response, size_t response_capacity);
