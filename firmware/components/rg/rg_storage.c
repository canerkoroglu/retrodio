#include "rg_storage.h"
#include <errno.h>
#include <sys/stat.h>
#include "rg_board.h"
#include "rg_power.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "esp_check.h"

static const char *TAG = "rg_storage";
static sdmmc_card_t *s_card;
static spi_host_device_t s_host = SPI2_HOST;

/* After the SPI driver frees its pins, leave all four high-Z (no internal pulls: they would
 * back-power the unpowered module). The external 100k pull-up keeps CS deselected and the
 * GPIO2 strapping pin high. Then drop the rail. */
static void release_bus(void)
{
    spi_bus_free(s_host);
    rg_board_pin_float(RG_PIN_SD_CS);
    rg_board_pin_float(RG_PIN_SD_SCK);
    rg_board_pin_float(RG_PIN_SD_MOSI);
    rg_board_pin_float(RG_PIN_SD_MISO);
    rg_power_release();
}

esp_err_t rg_storage_mount(uint32_t freq_khz)
{
    ESP_RETURN_ON_FALSE(!s_card, ESP_ERR_INVALID_STATE, TAG, "already mounted");

    spi_bus_config_t bus = {
        .mosi_io_num = RG_PIN_SD_MOSI,
        .miso_io_num = RG_PIN_SD_MISO,
        .sclk_io_num = RG_PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    rg_power_acquire(); /* module must be powered before its inputs are driven */
    esp_err_t err = spi_bus_initialize(s_host, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        rg_power_release();
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = s_host;
    host.max_freq_khz = (int)freq_khz;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = s_host;
    slot.gpio_cs = RG_PIN_SD_CS;

    esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 32 * 1024,
        .disk_status_check_enable = true,
    };
    err = esp_vfs_fat_sdspi_mount(RG_MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        release_bus();
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

void rg_storage_unmount(void)
{
    if (!s_card) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(RG_MOUNT_POINT, s_card);
    s_card = NULL;
    release_bus();
}

bool rg_storage_mounted(void)
{
    return s_card != NULL;
}

sdmmc_card_t *rg_storage_card(void)
{
    return s_card;
}

static esp_err_t mkdir_p(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir %s: errno %d", path, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t rg_storage_ensure_dirs(void)
{
    ESP_RETURN_ON_FALSE(s_card, ESP_ERR_INVALID_STATE, TAG, "not mounted");
    ESP_RETURN_ON_ERROR(mkdir_p(RG_DIR_AUDIO), TAG, "audio");
    ESP_RETURN_ON_ERROR(mkdir_p(RG_DIR_GREETINGS), TAG, "greetings");
    return mkdir_p(RG_DIR_RECORDINGS);
}

esp_err_t rg_storage_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
    ESP_RETURN_ON_FALSE(s_card, ESP_ERR_INVALID_STATE, TAG, "not mounted");
    return esp_vfs_fat_info(RG_MOUNT_POINT, total_bytes, free_bytes);
}
