#include "rg_system.h"
#include <string.h>
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "nvs.h"
#include "nvs_flash.h"

#define GUARD_MAGIC 0x52474744u

static const char *TAG = "rg_system";

/* Not initialised by the bootloader: survives panics and watchdog resets, garbage after power-on. */
RTC_NOINIT_ATTR static struct {
    uint32_t magic;
    uint32_t crashes;
} s_guard;

static bool is_crash(esp_reset_reason_t r)
{
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

static esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS reformatted (%s)", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err;
}

static uint32_t bump_boot_count(void)
{
    nvs_handle_t h;
    uint32_t n = 0;
    if (nvs_open("rg_sys", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &n);
        n++;
        nvs_set_u32(h, "boots", n);
        nvs_commit(h);
        nvs_close(h);
    }
    return n;
}

static uint32_t read_boot_count(void)
{
    nvs_handle_t h;
    uint32_t n = 0;
    if (nvs_open("rg_sys", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &n);
        nvs_close(h);
    }
    return n;
}

esp_err_t rg_system_init(rg_boot_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->reason = esp_reset_reason();
    info->from_deep_sleep = info->reason == ESP_RST_DEEPSLEEP;
    info->version = esp_app_get_description()->version;

    if (s_guard.magic != GUARD_MAGIC || info->reason == ESP_RST_POWERON) {
        s_guard.magic = GUARD_MAGIC;
        s_guard.crashes = 0;
    }
    if (is_crash(info->reason)) {
        s_guard.crashes++;
    }
    info->consecutive_crashes = s_guard.crashes;
    info->safe_mode = s_guard.crashes >= RG_SAFE_MODE_CRASHES;

    esp_err_t err = nvs_init();
    if (err == ESP_OK) {
        /* One flash write per cold boot only; deep-sleep wakes stay write-free. */
        info->boot_count = info->from_deep_sleep ? read_boot_count() : bump_boot_count();
    }
    size_t addr = 0, size = 0;
    info->coredump_present = esp_core_dump_image_get(&addr, &size) == ESP_OK;
    if (is_crash(info->reason) || info->reason == ESP_RST_BROWNOUT) {
        ESP_LOGW(TAG, "reset: %s, consecutive crashes %lu%s", rg_reset_reason_name(info->reason),
                 (unsigned long)s_guard.crashes, info->safe_mode ? " -> SAFE MODE" : "");
    }
    return err;
}

void rg_system_mark_healthy(void)
{
    s_guard.magic = GUARD_MAGIC;
    s_guard.crashes = 0;
}

const char *rg_reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep-sleep wake";
    case ESP_RST_BROWNOUT:
        return "brownout (supply too weak)";
    case ESP_RST_USB:
        return "usb";
    case ESP_RST_EXT:
        return "external";
    default:
        return "other";
    }
}
