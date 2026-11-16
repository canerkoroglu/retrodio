#pragma once

#include <stddef.h>

/*
 * Access-point credentials from /audio/wifi.txt (listening mode):
 *
 *     SSID = AgAdiniz
 *     PASSWORD = parolaniz
 *
 * The password is required (WPA2); there is no open-network option. Keys are case-insensitive. A line whose first
 * non-blank character is '#' is a comment; a '#' elsewhere is part of the value, so passwords may contain it. Optional
 * surrounding double quotes are removed. Pure C, no ESP-IDF dependencies (host-tested).
 */

#define RG_WIFI_SSID_MAX 32 /* bytes, 802.11 limit */
#define RG_WIFI_PASS_MIN 8  /* WPA2-PSK limits */
#define RG_WIFI_PASS_MAX 63

typedef struct {
    char ssid[RG_WIFI_SSID_MAX + 1];
    char password[RG_WIFI_PASS_MAX + 1];
} rg_wifi_cfg_t;

/**
 * Apply the keys found in `text` to `cfg` (pre-filled with defaults by the caller). Invalid
 * values are reported in `warnings` (may be NULL) and leave the previous value untouched.
 * Returns the warning count.
 */
int rg_wifi_cfg_parse(const char *text, rg_wifi_cfg_t *cfg, char *warnings, size_t warnings_len);

/** rg_wifi_cfg_parse() on the file at `path`. Returns the warning count, or -1 if there is no file. */
int rg_wifi_cfg_load(const char *path, rg_wifi_cfg_t *cfg, char *warnings, size_t warnings_len);
