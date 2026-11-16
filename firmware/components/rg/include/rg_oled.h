#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rg_fonts.h"

/* 0.96" SSD1306, 128x64, I2C. */
#define RG_OLED_WIDTH   128
#define RG_OLED_HEIGHT  64
#define RG_FONT_ADVANCE 5 /* 5x7 font: glyph cells include a blank column; 25 characters per line at scale 1 */
#define RG_FONT_HEIGHT  8

/* Baselines of rg_oled_action_screen(). Top: the count (18 px digits) with a caption or the
 * countdown beside it. Below: one or two action lines in rg_font_big (13 px capitals); its 22 px
 * glyph box also holds the dot of İ above and the cedilla of Ç/Ş below. */
#define RG_LAYOUT_COUNT_BASE    19
#define RG_LAYOUT_CAPTION_BASE  15 /* one caption line (rg_font_head), centred on the count */
#define RG_LAYOUT_CAPTION2_BASE 9  /* two caption lines (rg_font_small), 11 px apart, centred */
#define RG_LAYOUT_LINE1_BASE    40
#define RG_LAYOUT_LINE2_BASE    59
#define RG_LAYOUT_SINGLE_BASE   50

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
/** 5x7 ASCII font (diagnostics); characters outside 0x20-0x7E render as '?'. scale is 1 or 2. */
void rg_oled_text(int x, int y, const char *s, int scale);
void rg_oled_text_center(int y, const char *s, int scale);
int rg_oled_text_width(const char *s, int scale);

/** UTF-8 text in a bitmap font (rg_fonts.h). x is the left ink edge; characters the font lacks
 *  draw as '?'. */
void rg_oled_font_text(const rg_font_t *f, int x, int baseline, const char *s);
void rg_oled_font_text_center(const rg_font_t *f, int baseline, const char *s, int dx);
int rg_oled_font_width(const rg_font_t *f, const char *s);

/**
 * Guestbook screen layout. Top-left: `count` (the message count) in 18 px digits. Beside it,
 * either `timer` (countdown, top-right, large) or `caption`, vertically centred on the count:
 * one line in rg_font_head, or two lines split at '\n' in rg_font_small; a line that does not
 * fit is left out. Bottom half: `top` and `bottom` centred in the
 * large font, or `top` alone on one centred line. Strings are UTF-8 (Turkish letters included).
 * `dx` shifts everything by a pixel against burn-in. NULL skips a part.
 */
void rg_oled_action_screen(const char *count, const char *caption, const char *timer, const char *top,
                           const char *bottom, int dx);

/** Push the frame buffer to the panel (~26 ms for 1 KB at 400 kHz). */
esp_err_t rg_oled_flush(void);
esp_err_t rg_oled_set_contrast(uint8_t contrast);
esp_err_t rg_oled_display_on(bool on);
