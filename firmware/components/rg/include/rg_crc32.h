#pragma once

#include <stddef.h>
#include <stdint.h>

/* Standard CRC-32 (IEEE 802.3, reflected, as zlib / Python zlib.crc32).
 * Start with crc = 0 and feed chunks: crc = rg_crc32_update(crc, buf, len). */
uint32_t rg_crc32_update(uint32_t crc, const void *data, size_t len);
