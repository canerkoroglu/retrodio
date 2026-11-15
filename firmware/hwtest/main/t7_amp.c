/*
 * T7: MAX98357A playback through the earpiece.
 *  a) 1 kHz tone ladder to pick a safe/comfortable volume
 *  b) pop check: full power-up / power-down cycles of the switched rail with I2S, i.e. exactly
 *     what happens at every session start and end
 *  c) greeting / instruction / beep files from the card at the suggested volume
 */
#include <stdio.h>
#include "hwtest.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rg_audio.h"
#include "rg_board.h"
#include "rg_err.h"
#include "rg_storage.h"

static int16_t s_tone[RG_AUDIO_DMA_FRAMES];

static void play_tone(int dbfs, uint32_t ms)
{
    for (uint32_t done = 0; done < RG_SAMPLE_RATE_HZ * ms / 1000; done += RG_AUDIO_DMA_FRAMES) {
        hw_tone(s_tone, RG_AUDIO_DMA_FRAMES, dbfs);
        rg_audio_write(s_tone, RG_AUDIO_DMA_FRAMES);
    }
}

tr_t t7_amp(void)
{
    rg_audio_set_volume_db(0); /* tone levels below are absolute */
    esp_err_t err = rg_audio_open(RG_AUDIO_TX, false);
    if (err != ESP_OK) {
        hw_result(7, TR_FAIL, "i2s_open=%s", rg_err_name(err));
        return TR_FAIL;
    }
    rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 20); /* > 7.5 ms amp turn-on after clocks start */

    printf("Hold the handset to your ear. 1 kHz tone, rising level:\n");
    hw_oled_status("AMP TEST", "TONE LADDER");
    const int levels[] = {-36, -30, -24, -18, -12, -6};
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        printf("  step %u: %d dBFS\n", (unsigned)(i + 1), levels[i]);
        play_tone(levels[i], 1200);
        rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 3);
    }
    rg_audio_drain();
    rg_audio_close();
    printf("Which step was the most comfortable listening level (1-6)? ");
    int k = hw_key(UINT32_MAX);
    int step = (k >= '1' && k <= '6') ? k - '0' : 0;
    printf("%c\n", k);
    bool distorted = hw_ask_yes("Did any step sound distorted or buzzy?");

    printf("\nPop check: 3 session start/end cycles (rail on, I2S on, silence, I2S off, rail off)...\n");
    hw_oled_status("AMP TEST", "POP CHECK");
    for (int i = 0; i < 3 && err == ESP_OK; i++) {
        err = rg_audio_open(RG_AUDIO_TX, false);
        if (err == ESP_OK) {
            rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 2);
            rg_audio_drain();
            rg_audio_close();
        }
        vTaskDelay(pdMS_TO_TICKS(700));
    }
    bool pops = hw_ask_yes("Did you hear clicks or pops?");

    /* Prepared speech files are normalised to -18 dBFS RMS; a sine at L dBFS peak has RMS L-3.
     * Equal loudness: -18 + V = L - 3  ->  V = L + 15 (clamped to <= 0 dB). */
    int suggested_db = step ? levels[step - 1] + 15 : g_settings.volume_db;
    suggested_db = suggested_db > 0 ? 0 : suggested_db;
    int files_ok = 0, files_found = 0;
    if (hw_sd_mount() && rg_audio_open(RG_AUDIO_TX, false) == ESP_OK) {
        rg_audio_set_volume_db(suggested_db);
        rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 20);
        const char *files[] = {RG_FILE_GREETING, RG_FILE_INSTRUCTION, RG_FILE_BEEP};
        hw_oled_status("AMP TEST", "SD FILES");
        for (size_t i = 0; i < 3; i++) {
            err = rg_audio_play_wav(files[i], NULL, NULL);
            printf("  %s: %s\n", files[i], rg_err_name(err));
            files_found += err != ESP_ERR_NOT_FOUND;
            files_ok += err == ESP_OK;
            rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 4);
        }
        rg_audio_drain();
        rg_audio_close();
        rg_audio_set_volume_db(g_settings.volume_db);
    } else {
        printf("SD not mounted: skipping greeting files.\n");
    }

    bool clean = files_found == 0 || hw_ask_yes("Did the greeting files play cleanly and intelligibly?");
    tr_t r = step && !distorted && !pops && files_ok == files_found && clean ? TR_PASS : TR_FAIL;
    hw_result(7, r,
              "comfortable_step=%d suggested_volume_db=%d (config.txt volume_db) distortion=%s pops=%s files_ok=%d/%d "
              "clean=%s",
              step, suggested_db, distorted ? "yes" : "no", pops ? "yes" : "no", files_ok, files_found,
              clean ? "yes" : "no");
    return r;
}
