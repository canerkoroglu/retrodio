#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_system.h"

#define RG_SAFE_MODE_CRASHES 3

typedef struct {
    esp_reset_reason_t reason;
    bool from_deep_sleep;
    uint32_t boot_count;          /* cold boots since first flash (NVS; deep-sleep wakes excluded) */
    uint32_t consecutive_crashes; /* panics / watchdog resets since the last healthy session */
    bool safe_mode;               /* >= RG_SAFE_MODE_CRASHES: skip the card, show a service message */
    bool coredump_present;        /* a crash dump is stored in flash (read with idf.py coredump-info) */
    const char *version;          /* firmware version (PROJECT_VER) */
} rg_boot_info_t;

/** Call first in app_main: initialises NVS and evaluates the crash-loop guard. */
esp_err_t rg_system_init(rg_boot_info_t *info);

/** A full session completed normally: clears the crash counter. */
void rg_system_mark_healthy(void);

const char *rg_reset_reason_name(esp_reset_reason_t reason);
