#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * Append-only diagnostics log on the card (RG_FILE_LOG), rotated to log.old at 256 KB.
 * Lines: "b<boot>.<session> +<ms> <message>". There is no RTC clock: boot/session numbers
 * give the order. Each call opens, writes, fsyncs and closes (~10-50 ms): never call it
 * while recording.
 */
void rg_eventlog_set_boot(uint32_t boot_count, uint32_t session);
esp_err_t rg_eventlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
