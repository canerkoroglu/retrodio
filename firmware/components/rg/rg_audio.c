#include "rg_audio.h"
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include "rg_board.h"
#include "rg_err.h"
#include "rg_power.h"
#include "rg_wav.h"
#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "esp_check.h"

#define IO_TIMEOUT_MS 1000
#define CHUNK_SAMPLES RG_AUDIO_DMA_FRAMES

static const char *TAG = "rg_audio";
static i2s_chan_handle_t s_tx, s_rx;
static int32_t s_vol_q15 = 32767;
static volatile uint32_t s_rx_ovf;
static bool s_powered;
static int32_t s_scratch[RG_AUDIO_DMA_FRAMES * 2];

static bool IRAM_ATTR on_rx_ovf(i2s_chan_handle_t h, i2s_event_data_t *e, void *ctx)
{
    (void)h;
    (void)e;
    (void)ctx;
    s_rx_ovf++;
    return false;
}

bool rg_audio_is_open(void)
{
    return s_tx || s_rx;
}

void rg_audio_set_volume_db(int db)
{
    if (db > 0) {
        db = 0;
    }
    s_vol_q15 = (int32_t)lroundf(32767.0f * powf(10.0f, (float)db / 20.0f));
}

esp_err_t rg_audio_open(rg_audio_dir_t dir, bool rx_stereo)
{
    ESP_RETURN_ON_FALSE(!rg_audio_is_open(), ESP_ERR_INVALID_STATE, TAG, "already open");
    bool tx = dir & RG_AUDIO_TX, rx = dir & RG_AUDIO_RX;

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = RG_AUDIO_DMA_DESC;
    chan.dma_frame_num = RG_AUDIO_DMA_FRAMES;
    chan.auto_clear_after_cb = true; /* TX sends zeros on underrun */
    rg_power_acquire();              /* amp + mic must be powered before their pins are driven */
    s_powered = true;
    if (!tx) {
        /* RX-only: hold amp DIN low so the powered amp never sees a floating input while BCLK/WS run. */
        gpio_set_level(RG_PIN_I2S_DOUT, 0);
        gpio_set_direction(RG_PIN_I2S_DOUT, GPIO_MODE_OUTPUT);
    }
    esp_err_t err = i2s_new_channel(&chan, tx ? &s_tx : NULL, rx ? &s_rx : NULL);
    if (err != ESP_OK) {
        rg_audio_close();
        return err;
    }

    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(RG_SAMPLE_RATE_HZ);
    /* 64 BCLK per frame: MCLK = 512 fs keeps BCLK divider at 8 (TX needs >= 6). */
    clk.mclk_multiple = I2S_MCLK_MULTIPLE_512;
    i2s_std_gpio_config_t gpio = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = RG_PIN_I2S_BCLK,
        .ws = RG_PIN_I2S_WS,
        .dout = tx ? RG_PIN_I2S_DOUT : I2S_GPIO_UNUSED,
        .din = rx ? RG_PIN_I2S_DIN : I2S_GPIO_UNUSED,
    };

    if (tx) {
        i2s_std_config_t cfg = {
            .clk_cfg = clk,
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = gpio,
        };
        cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
        cfg.slot_cfg.ws_width = 32;
        cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
        err = i2s_channel_init_std_mode(s_tx, &cfg);
    }
    if (err == ESP_OK && rx) {
        i2s_std_config_t cfg = {
            .clk_cfg = clk,
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                            rx_stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO),
            .gpio_cfg = gpio,
        };
        cfg.slot_cfg.slot_mask = rx_stereo ? I2S_STD_SLOT_BOTH : I2S_STD_SLOT_LEFT;
        i2s_event_callbacks_t cbs = {.on_recv_q_ovf = on_rx_ovf};
        err = i2s_channel_init_std_mode(s_rx, &cfg);
        if (err == ESP_OK) {
            err = i2s_channel_register_event_callback(s_rx, &cbs, NULL);
        }
    }
    s_rx_ovf = 0;
    if (err == ESP_OK && tx) {
        err = i2s_channel_enable(s_tx);
    }
    if (err == ESP_OK && rx) {
        err = i2s_channel_enable(s_rx);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        rg_audio_close();
    }
    return err;
}

void rg_audio_close(void)
{
    if (s_rx) {
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
    rg_board_pin_float(RG_PIN_I2S_BCLK);
    rg_board_pin_float(RG_PIN_I2S_WS);
    rg_board_pin_float(RG_PIN_I2S_DOUT);
    rg_board_pin_float(RG_PIN_I2S_DIN);
    if (s_powered) {
        s_powered = false;
        rg_power_release(); /* clocks are stopped and pins released before the amp and mic lose power */
    }
}

void rg_audio_rx_flush(void)
{
    if (!s_rx) {
        return;
    }
    size_t n;
    while (i2s_channel_read(s_rx, s_scratch, sizeof(s_scratch), &n, 0) == ESP_OK && n > 0) {
    }
}

uint32_t rg_audio_rx_overflows(void)
{
    return s_rx_ovf;
}

static esp_err_t tx_chunk(const int16_t *pcm, size_t n)
{
    int16_t buf[CHUNK_SAMPLES];
    for (size_t i = 0; i < n; i++) {
        buf[i] = pcm ? (int16_t)((pcm[i] * s_vol_q15) >> 15) : 0;
    }
    size_t written;
    esp_err_t err = i2s_channel_write(s_tx, buf, n * sizeof(int16_t), &written, IO_TIMEOUT_MS);
    rg_audio_rx_flush(); /* DUPLEX: RX runs in lockstep and its data is not wanted during playback */
    return err;
}

static esp_err_t tx(const int16_t *pcm, size_t samples)
{
    ESP_RETURN_ON_FALSE(s_tx, ESP_ERR_INVALID_STATE, TAG, "TX not open");
    while (samples) {
        size_t n = samples < CHUNK_SAMPLES ? samples : CHUNK_SAMPLES;
        ESP_RETURN_ON_ERROR(tx_chunk(pcm, n), TAG, "i2s write");
        if (pcm) {
            pcm += n;
        }
        samples -= n;
    }
    return ESP_OK;
}

esp_err_t rg_audio_write(const int16_t *pcm, size_t samples)
{
    return tx(pcm, samples);
}

esp_err_t rg_audio_write_silence(size_t samples)
{
    return tx(NULL, samples);
}

esp_err_t rg_audio_drain(void)
{
    /* Once DMA-depth (+1 buffer) of silence has been accepted, every earlier sample has been sent. */
    return tx(NULL, (RG_AUDIO_DMA_DESC + 1) * RG_AUDIO_DMA_FRAMES);
}

esp_err_t rg_audio_play_wav(const char *path, rg_abort_fn abort, void *ctx)
{
    rg_wav_info_t info;
    ESP_RETURN_ON_ERROR(rg_wav_probe(path, &info), TAG, "probe %s", path);
    if (!rg_wav_is_playable(&info)) {
        ESP_LOGE(TAG, "%s: %u Hz %u ch %u bit fmt %u; need 16000 Hz mono 16-bit PCM", path, (unsigned)info.sample_rate,
                 info.channels, info.bits, info.format);
        return RG_ERR_BAD_FORMAT;
    }
    int fd = open(path, O_RDONLY);
    ESP_RETURN_ON_FALSE(fd >= 0, ESP_ERR_NOT_FOUND, TAG, "open %s", path);
    esp_err_t err = lseek(fd, info.data_offset, SEEK_SET) < 0 ? ESP_FAIL : ESP_OK;
    int16_t pcm[CHUNK_SAMPLES * 4];
    uint32_t left = info.data_bytes & ~1U;
    while (err == ESP_OK && left) {
        if (abort && abort(ctx)) {
            err = RG_ERR_ABORTED;
            break;
        }
        size_t want = left < sizeof(pcm) ? left : sizeof(pcm);
        ssize_t got = read(fd, pcm, want);
        if (got <= 0) {
            err = ESP_FAIL;
            break;
        }
        left -= (uint32_t)got;
        err = tx(pcm, (size_t)got / 2);
    }
    close(fd);
    return err;
}

esp_err_t rg_audio_read(int32_t *buf, size_t words, size_t *got, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_rx, ESP_ERR_INVALID_STATE, TAG, "RX not open");
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(s_rx, buf, words * sizeof(int32_t), &bytes, timeout_ms);
    *got = bytes / sizeof(int32_t);
    return err;
}
