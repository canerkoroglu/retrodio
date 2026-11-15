#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

/** Initialise SPI2, mount FAT at RG_MOUNT_POINT. Never formats the card. */
esp_err_t rg_storage_mount(uint32_t freq_khz);
/** Unmount and free the SPI bus; CS returns to its idle-high safe state. */
void rg_storage_unmount(void);
bool rg_storage_mounted(void);
sdmmc_card_t *rg_storage_card(void);

/** Create /audio, /audio/greetings, /audio/recordings if missing. */
esp_err_t rg_storage_ensure_dirs(void);
esp_err_t rg_storage_space(uint64_t *total_bytes, uint64_t *free_bytes);
