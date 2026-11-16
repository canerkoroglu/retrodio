#include "rg_oled.h"
#include <string.h>
#include "rg_board.h"
#include "driver/i2c_master.h"
#include "esp_check.h"

#define I2C_HZ      400000
#define I2C_TIMEOUT 50
#define CTRL_CMD    0x00
#define CTRL_DATA   0x40

extern const uint8_t rg_font5x7[95][5];

static const char *TAG = "rg_oled";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_present;
static uint8_t s_fb[RG_OLED_HEIGHT / 8][RG_OLED_WIDTH];
static uint8_t s_contrast = CONFIG_RG_OLED_CONTRAST;

static esp_err_t flush_once(void);
static esp_err_t recover(void);

static esp_err_t cmds(const uint8_t *c, size_t n)
{
    uint8_t buf[1 + 32];
    if (n > 32) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = CTRL_CMD;
    memcpy(buf + 1, c, n);
    return i2c_master_transmit(s_dev, buf, n + 1, I2C_TIMEOUT);
}

static esp_err_t configure(bool warm)
{
    // 128x64 SSD1306 configuration. Every command is idempotent and leaves GDDRAM intact,
    // so a warm init can run while the panel keeps showing its retained image.
    const uint8_t seq[] = {
        0xD5, 0x80,       // clock divide / oscillator
        0xA8, 0x3F,       // multiplex 64
        0xD3, 0x00,       // display offset
        0x40,             // start line 0
        0x8D, 0x14,       // charge pump on
        0x20, 0x00,       // horizontal addressing
        0xA1,             // segment remap
        0xC8,             // COM scan descending
        0xDA, 0x12,       // COM pins: alternative, no remap (128x64)
        0x81, s_contrast, // contrast (config.txt oled_contrast)
        0xD9, 0xF1,       // pre-charge
        0xDB, 0x40,       // VCOMH
        0xA4,             // display follows RAM
        0xA6,             // normal (not inverted)
    };
    if (!warm) {
        ESP_RETURN_ON_ERROR(cmds((const uint8_t[]){0xAE}, 1), TAG, "off");
    }
    ESP_RETURN_ON_ERROR(cmds(seq, sizeof(seq)), TAG, "init");
    if (!warm) {
        rg_oled_clear();
        ESP_RETURN_ON_ERROR(flush_once(), TAG, "clear");
    }
    return cmds((const uint8_t[]){0xAF}, 1);
}

esp_err_t rg_oled_init(bool warm)
{
    if (!s_bus) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = -1,
            .sda_io_num = RG_PIN_I2C_SDA,
            .scl_io_num = RG_PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus");
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = CONFIG_RG_OLED_I2C_ADDR,
            .scl_speed_hz = I2C_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "dev");
    }
    s_present = i2c_master_probe(s_bus, CONFIG_RG_OLED_I2C_ADDR, I2C_TIMEOUT) == ESP_OK;
    ESP_RETURN_ON_FALSE(s_present, ESP_ERR_NOT_FOUND, TAG, "no ACK at 0x%02X", CONFIG_RG_OLED_I2C_ADDR);
    return configure(warm);
}

/* A glitch or a panel stuck mid-transfer (SDA held low) is cleared by clocking the bus free
 * and re-sending the configuration; GDDRAM content is unaffected. */
static esp_err_t recover(void)
{
    ESP_LOGW(TAG, "I2C error, resetting bus");
    esp_err_t err = i2c_master_bus_reset(s_bus);
    return err == ESP_OK ? configure(true) : err;
}

bool rg_oled_present(void)
{
    return s_present;
}

void rg_oled_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void rg_oled_pixel(int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= RG_OLED_WIDTH || y >= RG_OLED_HEIGHT) {
        return;
    }
    uint8_t bit = 1U << (y & 7);
    if (on) {
        s_fb[y >> 3][x] |= bit;
    } else {
        s_fb[y >> 3][x] &= (uint8_t)~bit;
    }
}

void rg_oled_rect(int x, int y, int w, int h, bool fill)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (fill || j == 0 || j == h - 1 || i == 0 || i == w - 1) {
                rg_oled_pixel(x + i, y + j, true);
            }
        }
    }
}

static const uint8_t *font(char c)
{
    return rg_font5x7[(c < 0x20 || c > 0x7E ? '?' : c) - 0x20];
}

static void glyph(int x, int y, char c, int scale)
{
    const uint8_t *g = font(c);
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (g[col] & (1U << row)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        rg_oled_pixel(x + col * scale + sx, y + row * scale + sy, true);
                    }
                }
            }
        }
    }
}

void rg_oled_text(int x, int y, const char *s, int scale)
{
    for (; *s; s++) {
        glyph(x, y, *s, scale);
        x += RG_FONT_ADVANCE * scale;
    }
}

int rg_oled_text_width(const char *s, int scale)
{
    int n = (int)strlen(s);
    return n ? (n * RG_FONT_ADVANCE - 1) * scale : 0;
}

void rg_oled_text_center(int y, const char *s, int scale)
{
    rg_oled_text((RG_OLED_WIDTH - rg_oled_text_width(s, scale)) / 2, y, s, scale);
}

/* ---- Bitmap fonts (rg_fonts.c, Rubik Bold) ---- */

/* Decode one UTF-8 character and advance *p; malformed input yields '?'. */
static uint32_t utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t c = s[0];
    int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : -1;
    if (extra <= 0) {
        (*p)++;
        return extra < 0 ? '?' : c;
    }
    c &= extra == 1 ? 0x1F : 0x0F;
    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *p += i;
            return '?';
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    *p += extra + 1;
    return c;
}

/* Glyphs are sorted by code point. Missing characters draw as '?' (or nothing if the font has no '?'). */
static const rg_glyph_t *find_glyph(const rg_font_t *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) {
            return &f->glyphs[mid];
        }
        if (f->glyphs[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return cp == '?' ? NULL : find_glyph(f, '?');
}

/* Ink extent of s, relative to a pen starting at 0. */
static void extent(const rg_font_t *f, const char *s, int *left, int *right)
{
    int pen = 0;
    bool any = false;
    *left = *right = 0;
    while (*s) {
        const rg_glyph_t *g = find_glyph(f, utf8_next(&s));
        if (!g) {
            continue;
        }
        if (g->w) {
            int l = pen + g->x0, r = l + g->w;
            *left = any && *left < l ? *left : l;
            *right = any && *right > r ? *right : r;
            any = true;
        }
        pen += g->adv;
    }
}

int rg_oled_font_width(const rg_font_t *f, const char *s)
{
    int l, r;
    extent(f, s, &l, &r);
    return r - l;
}

void rg_oled_font_text(const rg_font_t *f, int x, int baseline, const char *s)
{
    int l, r;
    extent(f, s, &l, &r);
    int pen = x - l;
    const int top = baseline - f->ascent;
    while (*s) {
        const rg_glyph_t *g = find_glyph(f, utf8_next(&s));
        if (!g) {
            continue;
        }
        for (int c = 0; c < g->w; c++) {
            uint32_t m = f->cols[g->off + c];
            for (int y = 0; m; y++, m >>= 1) {
                if (m & 1) {
                    rg_oled_pixel(pen + g->x0 + c, top + y, true);
                }
            }
        }
        pen += g->adv;
    }
}

void rg_oled_font_text_center(const rg_font_t *f, int baseline, const char *s, int dx)
{
    rg_oled_font_text(f, (RG_OLED_WIDTH - rg_oled_font_width(f, s)) / 2 + dx, baseline, s);
}

/* ---- Guestbook screen layout (128x64) ---- */

void rg_oled_action_screen(const char *count, const char *caption, const char *timer, const char *top,
                           const char *bottom, int dx)
{
    int count_end = 1 + dx;
    if (count) {
        rg_oled_font_text(&rg_font_count, 1 + dx, RG_LAYOUT_COUNT_BASE, count);
        count_end += rg_oled_font_width(&rg_font_count, count);
    }
    if (timer) {
        /* Large countdown at the top-right; the small font if a 4-digit count leaves no room. */
        const rg_font_t *tf = &rg_font_mid;
        int base = RG_LAYOUT_COUNT_BASE;
        if (RG_OLED_WIDTH - 1 - rg_oled_font_width(tf, timer) < count_end + 4) {
            tf = &rg_font_small;
            base = RG_LAYOUT_CAPTION_BASE;
        }
        int x = RG_OLED_WIDTH - 1 - rg_oled_font_width(tf, timer);
        if (x >= count_end + 4) {
            rg_oled_font_text(tf, x, base, timer);
        }
    } else if (caption) {
        /* Beside the count, centred on it: one line in the title font, or two ('\n') in the small
         * font. A line that does not fit is left out. */
        const bool two = strchr(caption, '\n') != NULL;
        const rg_font_t *cf = two ? &rg_font_small : &rg_font_head;
        for (int k = 0; k < 2 && *caption; k++) {
            char line[32];
            size_t n = strcspn(caption, "\n");
            n = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
            memcpy(line, caption, n);
            line[n] = 0;
            caption += strcspn(caption, "\n");
            caption += *caption == '\n';
            int x = count_end + 6;
            if (x + rg_oled_font_width(cf, line) <= RG_OLED_WIDTH) {
                rg_oled_font_text(cf, x, two ? RG_LAYOUT_CAPTION2_BASE + 11 * k : RG_LAYOUT_CAPTION_BASE, line);
            }
        }
    }
    if (top && bottom) {
        rg_oled_font_text_center(&rg_font_big, RG_LAYOUT_LINE1_BASE, top, dx);
        rg_oled_font_text_center(&rg_font_big, RG_LAYOUT_LINE2_BASE, bottom, dx);
    } else if (top) {
        rg_oled_font_text_center(&rg_font_big, RG_LAYOUT_SINGLE_BASE, top, dx);
    }
}

static esp_err_t flush_once(void)
{
    ESP_RETURN_ON_ERROR(cmds((const uint8_t[]){0x21, 0, RG_OLED_WIDTH - 1, 0x22, 0, RG_OLED_HEIGHT / 8 - 1}, 6), TAG,
                        "window");
    static const uint8_t ctrl = CTRL_DATA;
    i2c_master_transmit_multi_buffer_info_t parts[] = {
        {.write_buffer = &ctrl, .buffer_size = 1},
        {.write_buffer = &s_fb[0][0], .buffer_size = sizeof(s_fb)},
    };
    return i2c_master_multi_buffer_transmit(s_dev, parts, 2, I2C_TIMEOUT);
}

esp_err_t rg_oled_flush(void)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = flush_once();
    if (err != ESP_OK && recover() == ESP_OK) {
        err = flush_once();
    }
    return err;
}

esp_err_t rg_oled_set_contrast(uint8_t contrast)
{
    s_contrast = contrast;
    return s_present ? cmds((const uint8_t[]){0x81, contrast}, 2) : ESP_OK;
}

esp_err_t rg_oled_display_on(bool on)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Off = panel sleep with the charge pump disabled: a few uA instead of mA. */
    return on ? cmds((const uint8_t[]){0x8D, 0x14, 0xAF}, 3) : cmds((const uint8_t[]){0xAE, 0x8D, 0x10}, 3);
}
