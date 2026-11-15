#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "rg_wav.h"

typedef struct {
    uint32_t data_bytes;        /* PCM bytes in the finalized file */
    uint32_t crc32;             /* CRC-32 of the PCM data (zlib-compatible) */
    uint32_t dropped_bytes;     /* bytes rejected because the ring buffer was full */
    uint32_t max_write_us;      /* worst single SD write */
    uint32_t max_checkpoint_us; /* worst header rewrite + fsync */
    uint32_t ring_high_water;   /* peak ring buffer fill in bytes */
    bool file_valid;            /* the file on the card verified (also after a salvaged write error) */
} rg_rec_stats_t;

/**
 * Create the WAV file (never overwrites) and start the SD writer task.
 * ring_bytes decouples capture from SD latency spikes.
 * Returns RG_ERR_CARD_FULL if the card has less than min_free_bytes free.
 * The path is stored in NVS until the file is finalized, so rg_rec_recover() can repair it
 * after a power cut.
 */
esp_err_t rg_rec_start(const char *path, size_t ring_bytes, uint64_t min_free_bytes);

/** Non-blocking push from the audio task. Returns bytes accepted (the rest is counted as dropped). */
size_t rg_rec_push(const void *pcm, size_t bytes);

/** True once a write or checkpoint has failed; capture should stop. */
bool rg_rec_failed(void);

/**
 * Drain the ring, finalize the header, fsync, close, then re-open and validate the file.
 * Returns ESP_OK only if the file on the card is a consistent 16 kHz mono 16-bit WAV holding
 * every accepted byte (RG_ERR_VERIFY otherwise). After a write error (e.g. RG_ERR_CARD_FULL)
 * the file is truncated to the last complete chunk and verified anyway: stats->file_valid then
 * says whether that shorter message is usable. Waits at most timeout_ms; RG_ERR_TIMEOUT means
 * the SD card is unresponsive and the caller should reboot (the file is repaired on boot).
 */
esp_err_t rg_rec_stop(rg_rec_stats_t *stats, uint32_t timeout_ms);

/** Stop and delete the file (hang-up during the greeting, or a too-short message). */
esp_err_t rg_rec_discard(uint32_t timeout_ms);

typedef struct {
    bool found; /* an unfinished recording was pending */
    char path[96];
    rg_wav_repair_action_t action;
    uint32_t data_bytes;
} rg_rec_recovery_t;

/**
 * Call after mounting the card at boot: repairs (or deletes, if shorter than min_data_bytes)
 * the recording that was in progress when power was lost or the device rebooted.
 */
esp_err_t rg_rec_recover(uint32_t min_data_bytes, rg_rec_recovery_t *out);
