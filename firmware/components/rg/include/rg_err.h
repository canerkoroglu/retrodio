#pragma once

#include "esp_err.h"

/* Project error codes (0xA000 range is unused by ESP-IDF). */
#define RG_ERR_BASE       0xA000
#define RG_ERR_ABORTED    (RG_ERR_BASE + 1) /* stopped on purpose, e.g. handset hung up during playback */
#define RG_ERR_CARD_FULL  (RG_ERR_BASE + 2) /* not enough free space on the microSD card */
#define RG_ERR_BAD_FORMAT (RG_ERR_BASE + 3) /* WAV is not 16 kHz mono 16-bit PCM */
#define RG_ERR_VERIFY     (RG_ERR_BASE + 4) /* file on the card does not match what was written */
#define RG_ERR_TIMEOUT    (RG_ERR_BASE + 5) /* SD writer did not finish in time; reboot to recover */

/** Name for RG_ERR_* codes, falling back to esp_err_to_name(). */
const char *rg_err_name(esp_err_t err);
