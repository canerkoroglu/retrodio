/*
 * Guestbook state machine. One instance, run by the main task with a 10 ms tick:
 * debounced hook events + audio engine events in, UI model + LED effect + audio commands out.
 * See docs/architecture.md for the state diagram.
 */
#include "gb_fsm.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "gb_audio.h"
#include "gb_soak.h"
#include "gb_ui.h"
#include "gb_web.h"
#include "rg_audio.h"
#include "rg_board.h"
#include "rg_catalog.h"
#include "rg_err.h"
#include "rg_eventlog.h"
#include "rg_hook.h"
#include "rg_led.h"
#include "rg_oled.h"
#include "rg_recorder.h"
#include "rg_settings.h"
#include "rg_storage.h"
#include "rg_wifi_cfg.h"

#define LOOP_MS               10
#define WAKE_CONFIRM_LIMIT_MS 1000 /* give up confirming a lift after this */

static const char *TAG = "gb_fsm";

typedef enum {
    ST_INITIALIZING,
    ST_WAKING,
    ST_GREETING,
    ST_INSTRUCTION,
    ST_BEEP,
    ST_RECORDING,
    ST_MAX_DURATION,
    ST_SAVING,
    ST_SUCCESS,
    ST_ERROR,
    ST_HANG_UP,
    ST_PREPARING_SLEEP,
    ST_WIFI, /* listening mode: access point + web player, no guest sessions */
} state_t;

static const char *const STATE_NAMES[] = {
    "INITIALIZING", "WAKING", "GREETING", "INSTRUCTION",     "BEEP", "RECORDING", "MAX_DURATION", "SAVING",
    "SUCCESS",      "ERROR",  "HANG_UP",  "PREPARING_SLEEP", "WIFI",
};

/* Sessions since power-on: burn-in pixel shift and log line prefix. */
RTC_DATA_ATTR static uint32_t s_session;

typedef struct {
    const rg_boot_info_t *boot;
    QueueHandle_t events;
    state_t state;
    int64_t entered_us;
    rg_hook_debounce_t hook;
    rg_settings_t settings;
    uint32_t msg_index;
    uint32_t seconds_left;
    gb_ui_error_t error;
    bool reboot_after_error; /* SD card stopped responding: a reset recovers it */
    bool need_hangup;        /* a session ended with the handset lifted: no new one until it is down */
    bool wdt;                /* main task currently subscribed to the task watchdog */
    bool result_ready;       /* MAX_DURATION: the message is already saved, result held for hang-up */
    gb_audio_event_t result;
    rg_wifi_cfg_t wifi;     /* listening mode credentials */
    int64_t wifi_active_us; /* last handset lift, playback or web request */
    uint32_t wifi_playing;  /* last values shown on the OLED */
    uint32_t wifi_clients;
} fsm_t;

static bool hook_raw(void)
{
    return gb_soak_active() ? gb_soak_lifted() : rg_hook_raw_lifted();
}

static bool lifted(const fsm_t *f)
{
    return f->hook.stable_lifted;
}

static void show(fsm_t *f, gb_ui_screen_t screen, bool sync)
{
    gb_ui_model_t m = {
        .screen = screen,
        .seconds_left = f->seconds_left,
        .error = f->error,
        .standby = f->settings.standby_screen,
        .shift = s_session,
        .wifi_playing = f->wifi_playing,
        .wifi_clients = f->wifi_clients,
    };
    strlcpy(m.wifi_ssid, f->wifi.ssid, sizeof(m.wifi_ssid));
    if (rg_catalog_valid()) {
        rg_catalog_state_t c;
        rg_catalog_get(&c);
        m.total = c.count;
        m.total_known = true;
        m.low_space = c.minutes_left < (uint32_t)f->settings.low_space_minutes;
    }
    if (sync) {
        gb_ui_show_sync(&m);
    } else {
        gb_ui_show(&m);
    }
}

static void wdt(fsm_t *f, bool on)
{
    if (on && !f->wdt) {
        esp_task_wdt_add(NULL);
    } else if (!on && f->wdt) {
        esp_task_wdt_delete(NULL);
    }
    f->wdt = on;
}

static void enter(fsm_t *f, state_t st);

/* Mount + boot services. Blocking (a cold-boot folder scan can take seconds), so the main task
 * leaves the watchdog for its duration. */
static bool mount_card(fsm_t *f)
{
    if (rg_storage_mounted()) {
        return true;
    }
    wdt(f, false);
    static const uint32_t tries[] = {CONFIG_RG_SD_FREQ_KHZ, 10000, 4000};
    esp_err_t err = ESP_FAIL;
    uint32_t khz = 0;
    for (size_t i = 0; i < sizeof(tries) / sizeof(tries[0]) && err != ESP_OK; i++) {
        if (i == 0 || tries[i] < CONFIG_RG_SD_FREQ_KHZ) {
            khz = tries[i];
            err = rg_storage_mount(khz);
        }
    }
    bool ok = err == ESP_OK;
    if (ok) {
        rg_storage_ensure_dirs();
        char warn[384];
        int warnings = rg_settings_load(RG_FILE_CONFIG, &f->settings, warn, sizeof(warn));
        if (gb_soak_active()) {
            f->settings.max_seconds = (int)gb_soak_max_seconds();
        }
        rg_led_set_max_brightness((uint8_t)f->settings.led_brightness);
        rg_oled_set_contrast((uint8_t)f->settings.oled_contrast);
        rg_audio_set_volume_db(f->settings.volume_db);

        rg_rec_recovery_t rec;
        if (rg_rec_recover((uint32_t)f->settings.min_seconds * RG_BYTES_PER_SECOND, &rec) == ESP_OK && rec.found) {
            rg_eventlog("power-cut recovery %s: %s, %lu B", rec.path,
                        rec.action == RG_WAV_REPAIR_FIXED     ? "header repaired"
                        : rec.action == RG_WAV_REPAIR_DELETED ? "deleted (too short)"
                                                              : "already consistent",
                        (unsigned long)rec.data_bytes);
            if (rec.action != RG_WAV_REPAIR_DELETED) {
                rg_catalog_invalidate(); /* a kept file is not in the deep-sleep cache count yet */
            }
        }
        rg_catalog_state_t cat;
        ok = rg_catalog_open(&cat) == ESP_OK;
        static bool boot_logged; /* cold boot only, once: deep-sleep wakes stay write-free on the card */
        if (!f->boot->from_deep_sleep && !boot_logged) {
            boot_logged = true;
            rg_eventlog("boot #%lu fw %s, reset %s, crashes %lu, card %lu kHz, %lu messages, %lu damaged, %lu min free",
                        (unsigned long)f->boot->boot_count, f->boot->version, rg_reset_reason_name(f->boot->reason),
                        (unsigned long)f->boot->consecutive_crashes, (unsigned long)khz, (unsigned long)cat.count,
                        (unsigned long)cat.invalid, (unsigned long)cat.minutes_left);
            if (warnings > 0) {
                rg_eventlog("config.txt: %d warning(s)\n%s", warnings, warn);
            }
        }
    } else {
        ESP_LOGE(TAG, "card: %s", rg_err_name(err));
    }
    wdt(f, true);
    return ok;
}

static void fail(fsm_t *f, gb_ui_error_t e, bool reboot)
{
    f->error = e;
    f->reboot_after_error = reboot;
    gb_soak_outcome(GB_SOAK_ERROR);
    enter(f, ST_ERROR);
}

static void begin_session(fsm_t *f)
{
    if (!mount_card(f)) {
        fail(f, GB_UI_ERR_CARD, false);
        return;
    }
    uint64_t total = 0, free_b = 0;
    rg_storage_space(&total, &free_b);
    gb_session_t ss = {
        .max_seconds = (uint32_t)f->settings.max_seconds,
        .min_seconds = (uint32_t)f->settings.min_seconds,
        .mic_gain_db = f->settings.mic_gain_db,
        .ring_bytes = CONFIG_GB_RING_KB * 1024,
        .min_free_bytes = CONFIG_GB_MIN_FREE_KB * 1024ULL + 10ULL * RG_BYTES_PER_SECOND,
        .save_timeout_ms = CONFIG_GB_SAVE_TIMEOUT_MS,
    };
    if (free_b < ss.min_free_bytes || rg_catalog_next_path(ss.path, sizeof(ss.path), &f->msg_index) != ESP_OK) {
        rg_eventlog("card full: %llu KB free", (unsigned long long)(free_b / 1024));
        fail(f, GB_UI_ERR_FULL, false);
        return;
    }
    s_session++;
    rg_eventlog_set_boot(f->boot->boot_count, s_session);
    f->need_hangup = true;
    f->seconds_left = ss.max_seconds;
    f->result_ready = false;
    if (gb_audio_start_session(&ss) != ESP_OK) {
        fail(f, GB_UI_ERR_SAVE, false);
        return;
    }
    enter(f, ST_GREETING);
}

static void on_failed(fsm_t *f, const gb_audio_event_t *ev)
{
    rg_eventlog("session failed: %s", rg_err_name(ev->err));
    fail(f, ev->err == RG_ERR_CARD_FULL ? GB_UI_ERR_FULL : GB_UI_ERR_SAVE, ev->err == RG_ERR_TIMEOUT);
}

static void on_discarded(fsm_t *f)
{
    gb_soak_outcome(GB_SOAK_DISCARDED);
    enter(f, ST_PREPARING_SLEEP);
}

/* Only a verified file counts. A card that filled up mid-message still yields a valid, shorter
 * message (the recorder truncates to the last complete chunk and verifies it). */
static void on_saved(fsm_t *f, const gb_audio_event_t *ev)
{
    bool salvaged = ev->err == RG_ERR_CARD_FULL && ev->stats.file_valid;
    if (ev->err != ESP_OK && !salvaged) {
        rg_eventlog("save failed MSG_%06lu: %s", (unsigned long)f->msg_index, rg_err_name(ev->err));
        fail(f, GB_UI_ERR_SAVE, ev->err == RG_ERR_TIMEOUT);
        return;
    }
    esp_err_t c = rg_catalog_commit(f->msg_index, ev->stats.data_bytes, ev->stats.crc32);
    rg_eventlog("saved MSG_%06lu %lu B %lu ms crc %08lx%s%s", (unsigned long)f->msg_index,
                (unsigned long)ev->stats.data_bytes,
                (unsigned long)(ev->stats.data_bytes * 1000ULL / RG_BYTES_PER_SECOND), (unsigned long)ev->stats.crc32,
                salvaged ? " (card full)" : "", ev->stats.dropped_bytes ? " (audio dropped: enlarge ring)" : "");
    if (c != ESP_OK) {
        ESP_LOGW(TAG, "catalog commit: %s", rg_err_name(c));
    }
    gb_soak_outcome(GB_SOAK_SAVED);
    enter(f, ST_SUCCESS);
}

static void prepare_sleep(fsm_t *f)
{
    gb_soak_before_sleep(); /* card still mounted: the soak summary can be logged */
    int64_t deadline = esp_timer_get_time() + (CONFIG_GB_SAVE_TIMEOUT_MS + 1000) * 1000LL;
    while (gb_audio_busy() && esp_timer_get_time() < deadline) {
        gb_audio_hang_up();
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
        esp_task_wdt_reset();
    }
    rg_storage_unmount(); /* last rail user except the LED */
    rg_led_off_sync();    /* -> rail off: SD, amp, LED and mic unpowered */
    show(f, GB_UI_STANDBY, true);
}

/* Listening mode starts only on a deliberate power-on or RESET with the handset lifted, never
 * after a crash or watchdog reset in the middle of a guest's session. */
static bool wifi_trigger(esp_reset_reason_t reason)
{
    return reason == ESP_RST_POWERON || reason == ESP_RST_EXT || reason == ESP_RST_USB || reason == ESP_RST_JTAG;
}

static bool start_wifi(fsm_t *f)
{
    strlcpy(f->wifi.ssid, CONFIG_GB_WIFI_SSID, sizeof(f->wifi.ssid));
    strlcpy(f->wifi.password, CONFIG_GB_WIFI_PASSWORD, sizeof(f->wifi.password));
    char warn[256];
    int warnings = rg_wifi_cfg_load(RG_FILE_WIFI, &f->wifi, warn, sizeof(warn));
    if (warnings < 0) {
        ESP_LOGW(TAG, "no %s: using the menuconfig fallback, if any", RG_FILE_WIFI);
    } else if (warnings > 0) {
        rg_eventlog("wifi.txt: %d warning(s)\n%s", warnings, warn);
    }
    if (!f->wifi.ssid[0] || strlen(f->wifi.password) < RG_WIFI_PASS_MIN) {
        rg_eventlog("listening mode: no usable SSID/PASSWORD in %s", RG_FILE_WIFI);
        return false;
    }
    esp_err_t err = gb_web_start(&f->wifi);
    if (err != ESP_OK) {
        rg_eventlog("listening mode failed to start: %s", rg_err_name(err));
        return false;
    }
    rg_eventlog("listening mode on, SSID %s", f->wifi.ssid);
    f->wifi_active_us = esp_timer_get_time();
    f->wifi_playing = 0;
    f->wifi_clients = 0;
    return true;
}

/* Every Wi-Fi resource is released by a reboot; the handset decides what comes next
 * (down: standby, lifted: "please hang up" - a software reset never re-enters this mode). */
static void stop_wifi(fsm_t *f, const char *why)
{
    (void)f;
    rg_eventlog("listening mode off (%s)", why);
    gb_web_stop_playback(2000);
    rg_storage_unmount();
    rg_led_off_sync();
    esp_restart();
}

static void go_to_sleep(fsm_t *f)
{
    if (!f->boot->safe_mode) {
        rg_system_mark_healthy(); /* reached standby cleanly: not a crash loop */
    }
    ESP_LOGI(TAG, "deep sleep");
    rg_hook_arm_wakeup();
    esp_deep_sleep_start();
}

static void enter(fsm_t *f, state_t st)
{
    f->state = st;
    f->entered_us = esp_timer_get_time();
    ESP_LOGI(TAG, "-> %s", STATE_NAMES[st]);
    switch (st) {
    case ST_INITIALIZING:
        break;
    case ST_WAKING:
        rg_led_set(RG_LED_BLINK_SHORT, RG_RGB_BLUE);
        show(f, GB_UI_WAKING, false);
        break;
    case ST_GREETING:
        rg_led_set(RG_LED_BREATHE_SLOW, RG_RGB_YELLOW);
        show(f, GB_UI_GREETING, false);
        break;
    case ST_INSTRUCTION:
    case ST_BEEP:
        rg_led_set(RG_LED_BREATHE_SLOW, RG_RGB_YELLOW);
        show(f, GB_UI_INSTRUCTION, false);
        break;
    case ST_RECORDING:
        rg_led_set(RG_LED_BREATHE, RG_RGB_GREEN);
        show(f, GB_UI_RECORDING, false);
        break;
    case ST_MAX_DURATION:
        rg_led_set(RG_LED_SOLID, RG_RGB_YELLOW);
        show(f, GB_UI_MAX_DURATION, false);
        break;
    case ST_SAVING:
        rg_led_set(RG_LED_BLINK_FAST, RG_RGB_CYAN);
        show(f, GB_UI_SAVING, false);
        break;
    case ST_SUCCESS:
        rg_led_set(RG_LED_SOLID, RG_RGB_GREEN);
        show(f, GB_UI_SUCCESS, false);
        break;
    case ST_ERROR:
        rg_led_set(RG_LED_BLINK_FAST, RG_RGB_RED);
        show(f, GB_UI_ERROR, false);
        break;
    case ST_HANG_UP:
        rg_led_off_sync();
        show(f, GB_UI_HANG_UP, false);
        break;
    case ST_PREPARING_SLEEP:
        prepare_sleep(f);
        break;
    case ST_WIFI:
        if (!start_wifi(f)) {
            f->error = GB_UI_ERR_WIFI; /* WIFI.TXT YOK: shown until the handset is down */
            f->reboot_after_error = false;
            enter(f, ST_ERROR);
            break;
        }
        rg_led_set(RG_LED_BREATHE_SLOW, RG_RGB_BLUE);
        show(f, GB_UI_WIFI, false);
        break;
    }
}

static gb_soak_phase_t soak_phase(state_t st)
{
    switch (st) {
    case ST_GREETING:
    case ST_INSTRUCTION:
    case ST_BEEP:
        return GB_SOAK_PHASE_INTRO;
    case ST_RECORDING:
        return GB_SOAK_PHASE_RECORDING;
    case ST_MAX_DURATION:
        return GB_SOAK_PHASE_LIMIT;
    default:
        return GB_SOAK_PHASE_IDLE;
    }
}

static void step(fsm_t *f, const gb_audio_event_t *ev, bool hook_changed, int64_t now)
{
    uint32_t ms = (uint32_t)((now - f->entered_us) / 1000);
    bool hung_up = hook_changed && !lifted(f);
    if (hung_up) {
        f->need_hangup = false;
    }

    switch (f->state) {
    case ST_INITIALIZING: /* cold boot / reset: never starts a session on its own */
        if (!mount_card(f)) {
            fail(f, GB_UI_ERR_CARD, false);
        } else if (!lifted(f)) {
            enter(f, ST_PREPARING_SLEEP);
        } else {
            enter(f, wifi_trigger(f->boot->reason) && !gb_soak_active() ? ST_WIFI : ST_HANG_UP);
        }
        break;

    case ST_WAKING:
        if (lifted(f) && ms >= CONFIG_GB_WAKE_CONFIRM_MS) {
            begin_session(f);
        } else if (hung_up || (!lifted(f) && ms >= WAKE_CONFIRM_LIMIT_MS)) {
            ESP_LOGI(TAG, "false wake");
            enter(f, ST_PREPARING_SLEEP);
        }
        break;

    case ST_GREETING:
    case ST_INSTRUCTION:
    case ST_BEEP:
        if (hung_up) {
            gb_audio_hang_up(); /* intro aborts; the audio engine deletes the unused file */
        }
        if (!ev) {
            break;
        }
        switch (ev->type) {
        case GB_EV_INSTRUCTION:
            enter(f, ST_INSTRUCTION);
            break;
        case GB_EV_BEEP:
            enter(f, ST_BEEP);
            break;
        case GB_EV_REC_STARTED:
            f->seconds_left = ev->value;
            enter(f, ST_RECORDING);
            break;
        case GB_EV_DISCARDED:
            on_discarded(f);
            break;
        case GB_EV_FAILED:
            on_failed(f, ev);
            break;
        default:
            break;
        }
        break;

    case ST_RECORDING:
        if (hung_up) {
            gb_audio_hang_up();
        }
        if (!ev) {
            if (hung_up) {
                enter(f, ST_SAVING);
            }
            break;
        }
        /* The capture may have ended on its own (recorder failure) in the same tick as the
         * hang-up: its final event is handled here, ST_SAVING would wait for it forever. */
        switch (ev->type) {
        case GB_EV_REC_TICK:
            if (hung_up) {
                enter(f, ST_SAVING);
                break;
            }
            f->seconds_left = ev->value;
            show(f, GB_UI_RECORDING, false);
            break;
        case GB_EV_REC_LIMIT:
            f->seconds_left = 0;
            /* the engine finalizes right away; result shown on hang-up */
            enter(f, hung_up ? ST_SAVING : ST_MAX_DURATION);
            break;
        case GB_EV_SAVED:
            on_saved(f, ev); /* recorder failure ended the capture */
            break;
        case GB_EV_DISCARDED:
            on_discarded(f);
            break;
        case GB_EV_FAILED:
            on_failed(f, ev);
            break;
        default:
            if (hung_up) {
                enter(f, ST_SAVING);
            }
            break;
        }
        break;

    case ST_MAX_DURATION:
        if (ev && (ev->type == GB_EV_SAVED || ev->type == GB_EV_FAILED)) {
            f->result = *ev;
            f->result_ready = true;
        }
        if (!lifted(f)) {
            if (!f->result_ready) {
                enter(f, ST_SAVING);
            } else if (f->result.type == GB_EV_SAVED) {
                on_saved(f, &f->result);
            } else {
                on_failed(f, &f->result);
            }
        }
        break;

    case ST_SAVING:
        if (!ev) {
            break;
        }
        if (ev->type == GB_EV_SAVED) {
            on_saved(f, ev);
        } else if (ev->type == GB_EV_DISCARDED) {
            on_discarded(f);
        } else if (ev->type == GB_EV_FAILED) {
            on_failed(f, ev);
        }
        break;

    case ST_SUCCESS:
        /* The next guest lifting the handset ends the screen at once: no wait for the greeting. */
        if (ms >= CONFIG_GB_SUCCESS_SCREEN_MS || (lifted(f) && !f->need_hangup)) {
            enter(f, ST_PREPARING_SLEEP);
        }
        break;

    case ST_ERROR:
        if (ms >= CONFIG_GB_ERROR_SCREEN_MS && !lifted(f)) {
            if (f->reboot_after_error) {
                ESP_LOGW(TAG, "rebooting to recover the SD card");
                esp_restart();
            }
            enter(f, ST_PREPARING_SLEEP);
        }
        break;

    case ST_HANG_UP:
        if (!lifted(f)) {
            f->need_hangup = false;
            enter(f, ST_PREPARING_SLEEP);
        }
        break;

    case ST_PREPARING_SLEEP:
        if (lifted(f)) {
            enter(f, f->need_hangup ? ST_HANG_UP : ST_WAKING);
        } else if (!f->hook.candidate && now - f->hook.candidate_since_us >= CONFIG_GB_SLEEP_STABLE_MS * 1000LL) {
            go_to_sleep(f);
        }
        break;

    case ST_WIFI: {
        gb_web_status_t ws;
        gb_web_status(&ws);
        if (lifted(f) || ws.playing) {
            f->wifi_active_us = now;
        }
        if (ws.last_request_us > f->wifi_active_us) {
            f->wifi_active_us = ws.last_request_us;
        }
        if (ws.playing != f->wifi_playing || ws.clients != f->wifi_clients) {
            f->wifi_playing = ws.playing;
            f->wifi_clients = ws.clients;
            show(f, GB_UI_WIFI, false);
        }
        if (ws.exit_requested) {
            stop_wifi(f, "Kapat on the page");
        } else if (now - f->wifi_active_us >= CONFIG_GB_WIFI_IDLE_MIN * 60000000LL) {
            stop_wifi(f, "idle");
        }
        break;
    }
    }
}

void gb_fsm_run(const rg_boot_info_t *boot, QueueHandle_t audio_events)
{
    static fsm_t f;
    f.boot = boot;
    f.events = audio_events;
    rg_settings_defaults(&f.settings);
    rg_hook_debounce_reset(&f.hook, hook_raw(), CONFIG_RG_HOOK_DEBOUNCE_MS);
    rg_eventlog_set_boot(boot->boot_count, s_session);
    wdt(&f, true);

    if (boot->safe_mode) {
        /* Repeated crashes: do not touch the card again until someone power-cycles the phone. */
        f.error = GB_UI_ERR_SERVICE;
        enter(&f, ST_ERROR);
    } else if (boot->from_deep_sleep) {
        enter(&f, ST_WAKING);
    } else {
        enter(&f, ST_INITIALIZING);
    }

    for (;;) {
        esp_task_wdt_reset();
        gb_audio_event_t ev;
        bool have = xQueueReceive(f.events, &ev, pdMS_TO_TICKS(LOOP_MS)) == pdTRUE;
        int64_t now = esp_timer_get_time();
        bool changed = rg_hook_debounce_feed(&f.hook, hook_raw(), now);
        gb_soak_tick(soak_phase(f.state), (uint32_t)((now - f.entered_us) / 1000));
        step(&f, have ? &ev : NULL, changed, now);
    }
}
