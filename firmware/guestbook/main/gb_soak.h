#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Phase 5 soak test (CONFIG_GB_SOAK_TEST): replaces the hook switch with a script and wakes the
 * board by timer, so N complete guest sessions run unattended through the real code path,
 * including deep sleep and the cold-start of every peripheral. The script mixes normal
 * messages, hang-ups during the greeting, too-short messages and maximum-length messages,
 * and checks every outcome against what the script expected.
 */

typedef enum {
    GB_SOAK_PHASE_IDLE,      /* waking / result / preparing sleep */
    GB_SOAK_PHASE_INTRO,     /* greeting, instruction, beep */
    GB_SOAK_PHASE_RECORDING, /* recording countdown */
    GB_SOAK_PHASE_LIMIT,     /* max duration reached, waiting for hang-up */
} gb_soak_phase_t;

typedef enum {
    GB_SOAK_SAVED,
    GB_SOAK_DISCARDED,
    GB_SOAK_ERROR,
} gb_soak_outcome_t;

bool gb_soak_active(void);
/** Call once at boot, before the state machine starts. */
void gb_soak_boot(bool from_deep_sleep);
/** Simulated hook state. */
bool gb_soak_lifted(void);
/** Drive the script: called every loop iteration with the current phase and time in it. */
void gb_soak_tick(gb_soak_phase_t phase, uint32_t ms_in_phase);
void gb_soak_outcome(gb_soak_outcome_t outcome);
/** Recording limit the script expects (overrides config.txt max_seconds while soaking). */
uint32_t gb_soak_max_seconds(void);
/** Before deep sleep: arms the timer wake while sessions remain; prints the summary at the end. */
void gb_soak_before_sleep(void);
