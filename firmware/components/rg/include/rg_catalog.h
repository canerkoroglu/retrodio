#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* ---- Pure helpers (no ESP-IDF dependencies; host-tested) ---- */

typedef struct {
    uint32_t valid;       /* MSG_*.wav with a consistent 16 kHz mono 16-bit header */
    uint32_t invalid;     /* MSG_*.wav that fail validation (kept on the card, not counted) */
    uint32_t max_index;   /* highest MSG index seen, valid or not; 0 if none */
    uint64_t audio_bytes; /* PCM bytes in valid files */
} rg_catalog_scan_t;

/** "MSG_000123.wav" (case-insensitive) -> 123. Index range 1..999999. */
bool rg_catalog_parse_name(const char *name, uint32_t *index);
void rg_catalog_format_name(uint32_t index, char *buf, size_t len);
esp_err_t rg_catalog_scan_dir(const char *dir, rg_catalog_scan_t *out);

/** One manifest row: "name,bytes,duration_ms,crc32\n". Returns the length written. */
int rg_catalog_manifest_line(char *buf, size_t len, const char *name, uint32_t data_bytes, uint32_t crc32);
/** Append a row (header line on first use) and fsync. */
esp_err_t rg_catalog_manifest_append(const char *manifest_path, const char *name, uint32_t data_bytes, uint32_t crc32);

/* ---- Device catalog (needs the card mounted) ---- */

typedef struct {
    uint32_t count;        /* valid messages on the card */
    uint32_t invalid;      /* damaged MSG files found by the last scan */
    uint32_t next_index;   /* never reused: max(card, NVS) */
    uint32_t minutes_left; /* recording time left on the card */
    bool scanned;          /* false: served from the deep-sleep cache */
} rg_catalog_state_t;

/**
 * Load the catalog. After deep sleep with the same card (CID) the RTC cache is used (no
 * directory scan, saves wake time and energy); otherwise the folder is scanned.
 */
esp_err_t rg_catalog_open(rg_catalog_state_t *state);
/** Full path of the next unused message file. */
esp_err_t rg_catalog_next_path(char *path, size_t len, uint32_t *index);
/** Record a verified message: count, next index (NVS), manifest row with CRC-32. */
esp_err_t rg_catalog_commit(uint32_t index, uint32_t data_bytes, uint32_t crc32);
void rg_catalog_get(rg_catalog_state_t *state);
/** True once rg_catalog_open() has succeeded since power-on (cache survives deep sleep). */
bool rg_catalog_valid(void);
