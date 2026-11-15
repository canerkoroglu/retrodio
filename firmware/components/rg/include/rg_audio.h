#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "rg_mic_conv.h"

/*
 * I2S0 on the ESP32-C3, 16 kHz, Philips, 2 x 32-bit slots (BCLK = 1.024 MHz).
 * TX: 16-bit mono samples duplicated into both slots (amp board default (L+R)/2 = the sample).
 * RX: 32-bit words from the MEMS mic (24 valid MSBs), left slot unless rx_stereo.
 * DUPLEX pairs TX and RX on one controller with shared BCLK/WS; TX is the clock master
 * and keeps clocking zeros (auto-clear) after playback, so the clocks never stop mid-session.
 */
typedef enum {
    RG_AUDIO_TX = 1,
    RG_AUDIO_RX = 2,
    RG_AUDIO_DUPLEX = 3,
} rg_audio_dir_t;

#define RG_AUDIO_DMA_FRAMES 256 /* frames per DMA buffer (16 ms) */
#define RG_AUDIO_DMA_DESC   8   /* 128 ms of DMA buffering per direction */

typedef bool (*rg_abort_fn)(void *ctx);

/** Powers the switched rail (amp + mic) via rg_power, then starts the I2S channel(s). */
esp_err_t rg_audio_open(rg_audio_dir_t dir, bool rx_stereo);
/** Stop and delete channels, release pins to high-Z, then release the rail. Idempotent. */
void rg_audio_close(void);
bool rg_audio_is_open(void);

/** Playback attenuation applied to every TX sample (<= 0 dB). */
void rg_audio_set_volume_db(int db);

/** Queue mono samples; blocks while DMA is full. In DUPLEX, stale RX buffers are discarded. */
esp_err_t rg_audio_write(const int16_t *pcm, size_t samples);
esp_err_t rg_audio_write_silence(size_t samples);
/** Block until every queued sample has left the DMA (i.e. has been played). */
esp_err_t rg_audio_drain(void);

/** Play a 16 kHz mono 16-bit PCM WAV from the SD card. abort may be NULL.
 *  Returns RG_ERR_ABORTED if aborted, RG_ERR_BAD_FORMAT for other formats. */
esp_err_t rg_audio_play_wav(const char *path, rg_abort_fn abort, void *ctx);

/** Read raw RX words (int32). words must be a multiple of the slot count. */
esp_err_t rg_audio_read(int32_t *buf, size_t words, size_t *got, uint32_t timeout_ms);
/** Discard all RX data currently queued (start recording "now"). */
void rg_audio_rx_flush(void);
uint32_t rg_audio_rx_overflows(void);
