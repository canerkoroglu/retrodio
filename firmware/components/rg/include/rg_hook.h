#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/** Configure the hook GPIO as input. External 10k pull-up is the primary bias. */
esp_err_t rg_hook_init(void);

/** Instantaneous (not debounced) state. */
bool rg_hook_raw_lifted(void);

typedef struct {
    bool stable_lifted; /**< debounced state */
    bool candidate;     /**< last raw state seen */
    int64_t candidate_since_us;
    uint32_t debounce_us;
} rg_hook_debounce_t;

/** Seed the debouncer with the current raw state. */
void rg_hook_debounce_init(rg_hook_debounce_t *d, uint32_t debounce_ms);
/** Seed with a given state (e.g. a simulated hook in the soak test). */
void rg_hook_debounce_reset(rg_hook_debounce_t *d, bool raw_lifted, uint32_t debounce_ms);

/** Read the hook and feed it; returns true when the debounced state changed. */
bool rg_hook_debounce_update(rg_hook_debounce_t *d, int64_t now_us);
/** Feed an externally supplied sample; returns true when the debounced state changed. */
bool rg_hook_debounce_feed(rg_hook_debounce_t *d, bool raw_lifted, int64_t now_us);

/**
 * Block until the raw state has been constant for stable_ms.
 * Returns false on timeout; *lifted receives the stable state on success.
 */
bool rg_hook_wait_stable(bool *lifted, uint32_t stable_ms, uint32_t timeout_ms);

/** Arm deep-sleep wake on "handset lifted". Caller must ensure the handset is down. */
esp_err_t rg_hook_arm_wakeup(void);
