#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * Switched 5 V peripheral rail (P-MOSFET high-side switch on GPIO PWR_EN):
 * SD module, MAX98357A, WS2812B + level shifter, mic LDO.
 * Reference counted: the rail is on while at least one user holds it and switches off
 * immediately when the last user releases it. In deep sleep PWR_EN is high-Z and its
 * 100k pull-down keeps the rail off, so standby current is ESP32 + OLED only.
 *
 * Users must not drive their pins high while the rail is off (back-powering through
 * ESD diodes): acquire before initialising a peripheral, release after de-initialising it.
 */

/** Turn the rail on if needed and wait CONFIG_RG_POWER_SETTLE_MS on the off->on edge. */
void rg_power_acquire(void);
void rg_power_release(void);
bool rg_power_is_on(void);

/** Increments on every off->on transition (lets the LED task re-send its frame after power-up). */
uint32_t rg_power_generation(void);
