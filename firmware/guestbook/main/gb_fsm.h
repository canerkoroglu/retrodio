#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rg_system.h"

/** Run the guestbook state machine forever (each cycle ends in deep sleep). */
void gb_fsm_run(const rg_boot_info_t *boot, QueueHandle_t audio_events);
