/* Device part of the catalog: deep-sleep cache, NVS-backed next index. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "rg_board.h"
#include "rg_catalog.h"
#include "rg_crc32.h"
#include "rg_err.h"
#include "rg_storage.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "nvs.h"

#define NVS_NS      "rg_cat"
#define CACHE_MAGIC 0x52474331u

static const char *TAG = "rg_catalog";

/* Survives deep sleep only; any reset or power cut forces a rescan (the card is the truth). */
RTC_DATA_ATTR static struct {
    uint32_t magic;
    uint32_t card_id;
    uint32_t count;
    uint32_t invalid;
    uint32_t next_index;
    uint32_t minutes_left;
} s_cache;

static uint32_t card_id(void)
{
    const sdmmc_card_t *c = rg_storage_card();
    return c ? rg_crc32_update(0, &c->cid, sizeof(c->cid)) : 0;
}

static uint32_t nvs_next_for(uint32_t id)
{
    nvs_handle_t h;
    uint32_t stored_id = 0, next = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u32(h, "card", &stored_id) != ESP_OK || stored_id != id ||
            nvs_get_u32(h, "next", &next) != ESP_OK) {
            next = 0;
        }
        nvs_close(h);
    }
    return next;
}

static esp_err_t nvs_store(uint32_t id, uint32_t next)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs open");
    esp_err_t err = nvs_set_u32(h, "card", id);
    if (err == ESP_OK) {
        err = nvs_set_u32(h, "next", next);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void update_minutes_left(void)
{
    uint64_t total = 0, free_b = 0;
    s_cache.minutes_left =
        rg_storage_space(&total, &free_b) == ESP_OK ? (uint32_t)(free_b / RG_BYTES_PER_SECOND / 60) : 0;
}

esp_err_t rg_catalog_open(rg_catalog_state_t *state)
{
    ESP_RETURN_ON_FALSE(rg_storage_mounted(), ESP_ERR_INVALID_STATE, TAG, "not mounted");
    uint32_t id = card_id();
    bool scanned = false;
    if (s_cache.magic != CACHE_MAGIC || s_cache.card_id != id) {
        rg_catalog_scan_t scan;
        esp_err_t err = rg_catalog_scan_dir(RG_DIR_RECORDINGS, &scan);
        if (err == ESP_ERR_NOT_FOUND) {
            memset(&scan, 0, sizeof(scan));
        } else if (err != ESP_OK) {
            return err;
        }
        uint32_t next = scan.max_index + 1;
        uint32_t stored = nvs_next_for(id);
        s_cache.magic = CACHE_MAGIC;
        s_cache.card_id = id;
        s_cache.count = scan.valid;
        s_cache.invalid = scan.invalid;
        s_cache.next_index = next > stored ? next : stored;
        scanned = true;
        ESP_LOGI(TAG, "scan: %lu valid, %lu invalid, next %lu", (unsigned long)scan.valid, (unsigned long)scan.invalid,
                 (unsigned long)s_cache.next_index);
    }
    update_minutes_left();
    rg_catalog_get(state);
    state->scanned = scanned;
    return ESP_OK;
}

bool rg_catalog_valid(void)
{
    return s_cache.magic == CACHE_MAGIC;
}

void rg_catalog_get(rg_catalog_state_t *state)
{
    state->count = s_cache.count;
    state->invalid = s_cache.invalid;
    state->next_index = s_cache.next_index;
    state->minutes_left = s_cache.minutes_left;
    state->scanned = false;
}

esp_err_t rg_catalog_next_path(char *path, size_t len, uint32_t *index)
{
    ESP_RETURN_ON_FALSE(s_cache.magic == CACHE_MAGIC, ESP_ERR_INVALID_STATE, TAG, "catalog not open");
    char name[20];
    struct stat st;
    for (uint32_t idx = s_cache.next_index; idx < 1000000; idx++) {
        rg_catalog_format_name(idx, name, sizeof(name));
        snprintf(path, len, "%s/%s", RG_DIR_RECORDINGS, name);
        if (stat(path, &st) != 0) {
            *index = idx;
            return ESP_OK;
        }
    }
    return RG_ERR_CARD_FULL; /* all 999999 names used */
}

esp_err_t rg_catalog_commit(uint32_t index, uint32_t data_bytes, uint32_t crc32)
{
    ESP_RETURN_ON_FALSE(s_cache.magic == CACHE_MAGIC, ESP_ERR_INVALID_STATE, TAG, "catalog not open");
    s_cache.count++;
    if (index + 1 > s_cache.next_index) {
        s_cache.next_index = index + 1;
    }
    esp_err_t err = nvs_store(s_cache.card_id, s_cache.next_index);
    char name[20];
    rg_catalog_format_name(index, name, sizeof(name));
    esp_err_t m = rg_catalog_manifest_append(RG_FILE_MANIFEST, name, data_bytes, crc32);
    update_minutes_left();
    return err != ESP_OK ? err : m;
}
