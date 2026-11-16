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
    // 128x32 SSD1306 configuration. Every command is idempotent and leaves GDDRAM intact,
    // so a warm init can run while the panel keeps showing its retained image.
    const uint8_t seq[] = {
        0xD5, 0x80,       // clock divide / oscillator
        0xA8, 0x1F,       // multiplex 32
        0xD3, 0x00,       // display offset
        0x40,             // start line 0
        0x8D, 0x14,       // charge pump on
        0x20, 0x00,       // horizontal addressing
        0xA1,             // segment remap
        0xC8,             // COM scan descending
        0xDA, 0x02,       // COM pins for 128x32
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

void rg_oled_text_vertical_xy(int x, int y, const char *s, int scale_x, int scale_y)
{
    int bottom = y + rg_oled_text_width(s, scale_y) - 1;
    for (int i = 0; s[i]; i++) {
        const uint8_t *g = font(s[i]);
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (g[col] & (1U << row)) {
                    for (int sy = 0; sy < scale_y; sy++) {
                        for (int sx = 0; sx < scale_x; sx++) {
                            rg_oled_pixel(x + row * scale_x + sx, bottom - (i * RG_FONT_ADVANCE + col) * scale_y - sy,
                                          true);
                        }
                    }
                }
            }
        }
    }
}

void rg_oled_text_vertical(int x, int y, const char *s, int scale)
{
    rg_oled_text_vertical_xy(x, y, s, scale, scale);
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

void rg_oled_action_screen(const char *side, const char *top, const char *bottom, int reserve_right, int dx)
{
    const int left = RG_OLED_SIDE_WIDTH;
    const int span = RG_OLED_WIDTH - left;
    if (side) {
        /* Up to three characters: 2x along the text (28 px of the 32 px height) and 3x across it,
         * so the digits read 21 px tall. Longer labels (025!, 1000) drop to 1x along, 2x across. */
        bool big = rg_oled_text_width(side, 2) <= RG_OLED_HEIGHT;
        int along = big ? 2 : 1;
        int h = rg_oled_text_width(side, along);
        rg_oled_text_vertical_xy(1 + dx, (RG_OLED_HEIGHT - h) / 2, side, big ? 3 : 2, along);
    }
    /* Capitals use 6 of the 7 font rows: 12 px per line at scale 2, centred in 32 px. The reserved
     * corner (e.g. the countdown) sits bottom-right, so only the bottom line gives it room. */
    if (top) {
        rg_oled_text(left + (span - rg_oled_text_width(top, 2)) / 2 + dx, bottom ? 2 : 10, top, 2);
    }
    if (bottom) {
        int bspan = span - reserve_right;
        rg_oled_text(left + (bspan - rg_oled_text_width(bottom, 2)) / 2 + dx, 18, bottom, 2);
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
