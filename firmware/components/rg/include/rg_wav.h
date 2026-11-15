#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define RG_WAV_HEADER_SIZE 44

typedef struct {
    uint16_t format; /* 1 = PCM */
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits;
    uint32_t data_offset; /* byte offset of first sample */
    uint32_t data_bytes;  /* from the data chunk header, clamped to the file size */
    uint32_t file_bytes;
    bool header_consistent; /* RIFF and data sizes agree with the file size */
} rg_wav_info_t;

/** Canonical 44-byte PCM header. */
void rg_wav_build_header(uint8_t hdr[RG_WAV_HEADER_SIZE], uint32_t rate, uint16_t channels, uint16_t bits,
                         uint32_t data_bytes);

/** Parse RIFF/WAVE, skipping unknown chunks (LIST, fact, ...). */
esp_err_t rg_wav_probe(const char *path, rg_wav_info_t *info);

/** True if the file is 16 kHz, mono, 16-bit PCM (the only playback format supported). */
bool rg_wav_is_playable(const rg_wav_info_t *info);

typedef struct {
    int fd;
    uint32_t rate;
    uint32_t data_bytes;
} rg_wav_writer_t;

/** Create a new file; fails with ESP_ERR_INVALID_STATE if it already exists (never overwrites). */
esp_err_t rg_wav_writer_open(rg_wav_writer_t *w, const char *path, uint32_t rate);
/** Returns RG_ERR_CARD_FULL when the card has no space left. */
esp_err_t rg_wav_writer_write(rg_wav_writer_t *w, const void *data, size_t bytes);
/** Rewrite the header with the current size and fsync: data so far survives power loss. */
esp_err_t rg_wav_writer_checkpoint(rg_wav_writer_t *w);
/** Checkpoint and close. The writer is unusable afterwards even on error. */
esp_err_t rg_wav_writer_close(rg_wav_writer_t *w);

typedef enum {
    RG_WAV_REPAIR_NONE,    /* header already consistent */
    RG_WAV_REPAIR_FIXED,   /* sizes rewritten to match the data on the card */
    RG_WAV_REPAIR_DELETED, /* less than min_data_bytes of audio, or not a WAV: removed */
} rg_wav_repair_action_t;

/**
 * Make a file left behind by a power cut playable: rewrite the RIFF/data sizes from the
 * actual file length (dropping a trailing odd byte), or delete it if it holds too little audio.
 */
esp_err_t rg_wav_repair(const char *path, uint32_t min_data_bytes, rg_wav_repair_action_t *action,
                        uint32_t *data_bytes);
