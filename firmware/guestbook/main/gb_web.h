#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rg_wifi_cfg.h"

/*
 * Listening mode: a Wi-Fi access point with a small web page (http://192.168.4.1) that lists the
 * messages on the card, plays one through the handset speaker, and offers each file for download.
 *
 * Started only from the state machine's WIFI state (cold boot with the handset lifted). The guest
 * session engine stays idle for the whole mode, so this module owns the I2S TX channel. The mode
 * ends with a reboot, which also releases every Wi-Fi resource.
 */

typedef struct {
    uint32_t playing;        /* message index being played, 0 = idle */
    uint32_t clients;        /* phones joined to the access point */
    int64_t last_request_us; /* esp_timer time of the last HTTP request, 0 = none yet */
    bool exit_requested;     /* "Kapat" pressed on the page */
} gb_web_status_t;

/** Start the access point, the web server and the player task. Call once. */
esp_err_t gb_web_start(const rg_wifi_cfg_t *cfg);
void gb_web_status(gb_web_status_t *status);
/** Stop playback and wait up to timeout_ms until the speaker channel is closed. */
void gb_web_stop_playback(uint32_t timeout_ms);
