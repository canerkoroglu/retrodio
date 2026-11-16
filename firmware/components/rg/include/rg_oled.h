#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define RG_OLED_WIDTH   128
#define RG_OLED_HEIGHT  32
#define RG_FONT_ADVANCE 5 /* glyph cells include a blank column; 25 characters per line at scale 1 */
#define RG_FONT_HEIGHT  8
/* Left column of rg_oled_action_screen(): 1 px margin, 21 px label (3x across), 1 px burn-in
 * shift, 2 px gap. Leaves 103 px, i.e. 10 characters per line at scale 2. */
#define RG_OLED_SIDE_WIDTH 25

/**
 * Initialise I2C and the SSD1306.
 * warm=true skips the display-off command, so an image retained through deep sleep
 * stays visible until the next flush (no flicker on wake).
 */
esp_err_t rg_oled_init(bool warm);

/** True if a device ACKs at the configured address. Valid after rg_oled_init(). */
bool rg_oled_present(void);

void rg_oled_clear(void);
void rg_oled_pixel(int x, int y, bool on);
void rg_oled_rect(int x, int y, int w, int h, bool fill);
/** Draw ASCII text; characters outside 0x20-0x7E render as '?'. scale is 1 or 2. */
void rg_oled_text(int x, int y, const char *s, int scale);
void rg_oled_text_center(int y, const char *s, int scale);
int rg_oled_text_width(const char *s, int scale);
/** Text rotated 90 degrees counter-clockwise (reads bottom to top). (x, y) is the top-left of a
 *  box 7*scale wide and rg_oled_text_width(s, scale) tall. */
void rg_oled_text_vertical(int x, int y, const char *s, int scale);
/** Rotated text with separate scales: scale_x across the text (glyph height when read), scale_y
 *  along it. The box is 7*scale_x wide and rg_oled_text_width(s, scale_y) tall. */
void rg_oled_text_vertical_xy(int x, int y, const char *s, int scale_x, int scale_y);
/**
 * Guestbook screen layout: `side` (e.g. the message count) rotated in the left column, vertically
 * centred (21 px tall digits for up to 3 characters, smaller beyond), then one or two lines at
 * scale 2 (up to 10 characters each), centred right of the column. `reserve_right` pixels are kept
 * free at the bottom-right corner (bottom line only). `dx` shifts everything by a pixel against
 * burn-in. NULL skips a part.
 */
void rg_oled_action_screen(const char *side, const char *top, const char *bottom, int reserve_right, int dx);

/** Push the frame buffer to the panel (~13 ms at 400 kHz). */
esp_err_t rg_oled_flush(void);
esp_err_t rg_oled_set_contrast(uint8_t contrast);
esp_err_t rg_oled_display_on(bool on);
