#include "gb_soak.h"
#include "sdkconfig.h"

#if CONFIG_GB_SOAK_TEST

#include <stdio.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "rg_eventlog.h"

#define SOAK_MAGIC     0x534F414Bu
#define SOAK_MAX_S     10 /* lowest allowed limit: keeps max-length sessions short */
#define WAKE_PERIOD_US 2000000ULL

static const char *TAG = "gb_soak";

typedef enum { PLAN_NORMAL, PLAN_ABORT_INTRO, PLAN_TOO_SHORT, PLAN_LIMIT } plan_t;

RTC_DATA_ATTR static struct {
    uint32_t magic;
    uint32_t session; /* 1-based number of the running session; 0 before the first */
    uint32_t saved, discarded, errors, mismatches;
    bool outcome_seen;
    bool done;
} s;
static bool s_lifted;

static plan_t plan_for(uint32_t k)
{
    if (k % 25 == 12) {
        return PLAN_LIMIT;
    }
    if (k % 10 == 3) {
        return PLAN_ABORT_INTRO;
    }
    if (k % 10 == 7) {
        return PLAN_TOO_SHORT;
    }
    return PLAN_NORMAL;
}

bool gb_soak_active(void)
{
    return s.magic == SOAK_MAGIC && !s.done;
}

void gb_soak_boot(bool from_deep_sleep)
{
    if (s.magic != SOAK_MAGIC) {
        s.magic = SOAK_MAGIC;
        s.session = s.saved = s.discarded = s.errors = s.mismatches = 0;
        s.done = false;
        ESP_LOGW(TAG, "SOAK TEST: %d simulated sessions, hook switch ignored", CONFIG_GB_SOAK_SESSIONS);
    }
    s_lifted = false;
    if (from_deep_sleep && !s.done && s.session < CONFIG_GB_SOAK_SESSIONS) {
        s.session++;
        s.outcome_seen = false;
        s_lifted = true; /* the "guest" lifts the handset */
    }
}

bool gb_soak_lifted(void)
{
    return s_lifted;
}

void gb_soak_tick(gb_soak_phase_t phase, uint32_t ms)
{
    if (!s_lifted) {
        return;
    }
    switch (plan_for(s.session)) {
    case PLAN_ABORT_INTRO:
        s_lifted = !(phase == GB_SOAK_PHASE_INTRO && ms >= 200); /* during greeting or beep */
        break;
    case PLAN_TOO_SHORT:
        s_lifted = !(phase == GB_SOAK_PHASE_RECORDING && ms >= 300);
        break;
    case PLAN_NORMAL:
        s_lifted = !(phase == GB_SOAK_PHASE_RECORDING && ms >= CONFIG_GB_SOAK_RECORD_S * 1000U);
        break;
    case PLAN_LIMIT:
        s_lifted = !(phase == GB_SOAK_PHASE_LIMIT && ms >= 2000);
        break;
    }
    /* Never strand the state machine waiting for a hang-up: 5 s idle, or 2 min in any phase
     * (longer than any greeting plus the 10 s soak recording limit). */
    if ((phase == GB_SOAK_PHASE_IDLE && ms > 5000) || ms > 120000) {
        s_lifted = false;
    }
}

void gb_soak_outcome(gb_soak_outcome_t o)
{
    if (!gb_soak_active() || s.session == 0) {
        return;
    }
    plan_t p = plan_for(s.session);
    gb_soak_outcome_t expected = (p == PLAN_ABORT_INTRO || p == PLAN_TOO_SHORT) ? GB_SOAK_DISCARDED : GB_SOAK_SAVED;
    s.outcome_seen = true;
    s.saved += o == GB_SOAK_SAVED;
    s.discarded += o == GB_SOAK_DISCARDED;
    s.errors += o == GB_SOAK_ERROR;
    if (o != expected) {
        s.mismatches++;
        ESP_LOGE(TAG, "session %lu: outcome %d, expected %d", (unsigned long)s.session, o, expected);
    }
}

uint32_t gb_soak_max_seconds(void)
{
    return SOAK_MAX_S;
}

void gb_soak_before_sleep(void)
{
    if (!gb_soak_active()) {
        return;
    }
    if (s.session > 0 && !s.outcome_seen) {
        s.mismatches++;
        s.outcome_seen = true;
        ESP_LOGE(TAG, "session %lu ended without an outcome", (unsigned long)s.session);
    }
    if (s.session < CONFIG_GB_SOAK_SESSIONS) {
        printf("SOAK %lu/%d saved=%lu discarded=%lu errors=%lu mismatches=%lu\n", (unsigned long)s.session,
               CONFIG_GB_SOAK_SESSIONS, (unsigned long)s.saved, (unsigned long)s.discarded, (unsigned long)s.errors,
               (unsigned long)s.mismatches);
        esp_sleep_enable_timer_wakeup(WAKE_PERIOD_US);
        return;
    }
    s.done = true;
    bool pass = s.errors == 0 && s.mismatches == 0;
    printf("\nRESULT SOAK %s sessions=%lu saved=%lu discarded=%lu errors=%lu mismatches=%lu\n", pass ? "PASS" : "FAIL",
           (unsigned long)s.session, (unsigned long)s.saved, (unsigned long)s.discarded, (unsigned long)s.errors,
           (unsigned long)s.mismatches);
    rg_eventlog("soak %s: sessions=%lu saved=%lu discarded=%lu errors=%lu mismatches=%lu", pass ? "PASS" : "FAIL",
                (unsigned long)s.session, (unsigned long)s.saved, (unsigned long)s.discarded, (unsigned long)s.errors,
                (unsigned long)s.mismatches);
}

#else /* soak test compiled out */

bool gb_soak_active(void)
{
    return false;
}
void gb_soak_boot(bool from_deep_sleep)
{
    (void)from_deep_sleep;
}
bool gb_soak_lifted(void)
{
    return false;
}
void gb_soak_tick(gb_soak_phase_t phase, uint32_t ms)
{
    (void)phase;
    (void)ms;
}
void gb_soak_outcome(gb_soak_outcome_t outcome)
{
    (void)outcome;
}
uint32_t gb_soak_max_seconds(void)
{
    return 0;
}
void gb_soak_before_sleep(void)
{
}

#endif
