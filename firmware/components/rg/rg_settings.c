#include "rg_settings.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"

#define MAX_FILE_BYTES 2048

typedef struct {
    const char *key;
    size_t offset;
    int min, max;
} int_key_t;

static const int_key_t INT_KEYS[] = {
    {"volume_db", offsetof(rg_settings_t, volume_db), -40, 0},
    {"mic_gain_db", offsetof(rg_settings_t, mic_gain_db), 0, 36},
    {"max_seconds", offsetof(rg_settings_t, max_seconds), 10, 300},
    {"led_brightness", offsetof(rg_settings_t, led_brightness), 1, 255},
    {"oled_contrast", offsetof(rg_settings_t, oled_contrast), 1, 255},
    {"low_space_minutes", offsetof(rg_settings_t, low_space_minutes), 5, 600},
    {"min_seconds", offsetof(rg_settings_t, min_seconds), 0, 10},
};

void rg_settings_defaults(rg_settings_t *s)
{
    s->volume_db = CONFIG_RG_PLAYBACK_VOLUME_DB;
    s->mic_gain_db = CONFIG_RG_MIC_GAIN_DB;
    s->max_seconds = CONFIG_RG_MAX_RECORD_SECONDS;
    s->led_brightness = CONFIG_RG_LED_MAX_BRIGHTNESS;
    s->oled_contrast = CONFIG_RG_OLED_CONTRAST;
    s->standby_screen = RG_STANDBY_FULL;
    s->low_space_minutes = 30;
    s->min_seconds = 1;
}

static void warn(char *buf, size_t len, int *count, const char *fmt, ...)
{
    (*count)++;
    if (!buf || !len) {
        return;
    }
    size_t used = strlen(buf);
    if (used + 1 >= len) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + used, len - used, fmt, ap);
    va_end(ap);
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = 0;
    }
    return s;
}

static void apply(rg_settings_t *s, const char *key, const char *val, int line, char *w, size_t wl, int *count)
{
    for (size_t i = 0; i < sizeof(INT_KEYS) / sizeof(INT_KEYS[0]); i++) {
        const int_key_t *k = &INT_KEYS[i];
        if (strcmp(key, k->key) != 0) {
            continue;
        }
        char *end;
        long v = strtol(val, &end, 10);
        if (end == val || *end != 0) {
            warn(w, wl, count, "line %d: %s: not a number\n", line, key);
            return;
        }
        if (v < k->min || v > k->max) {
            long c = v < k->min ? k->min : k->max;
            warn(w, wl, count, "line %d: %s=%ld out of range %d..%d, using %ld\n", line, key, v, k->min, k->max, c);
            v = c;
        }
        *(int *)((char *)s + k->offset) = (int)v;
        return;
    }
    if (strcmp(key, "standby_screen") == 0) {
        if (strcmp(val, "full") == 0) {
            s->standby_screen = RG_STANDBY_FULL;
        } else if (strcmp(val, "minimal") == 0) {
            s->standby_screen = RG_STANDBY_MINIMAL;
        } else if (strcmp(val, "off") == 0) {
            s->standby_screen = RG_STANDBY_OFF;
        } else {
            warn(w, wl, count, "line %d: standby_screen must be full, minimal or off\n", line);
        }
        return;
    }
    warn(w, wl, count, "line %d: unknown key '%s'\n", line, key);
}

int rg_settings_parse(const char *text, rg_settings_t *s, char *warnings, size_t warnings_len)
{
    int count = 0;
    if (warnings && warnings_len) {
        warnings[0] = 0;
    }
    char line[128];
    int lineno = 0;
    while (*text) {
        size_t n = strcspn(text, "\r\n");
        lineno++;
        size_t copy = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        memcpy(line, text, copy);
        line[copy] = 0;
        text += n;
        if (*text == '\r') {
            text++;
        }
        if (*text == '\n') {
            text++;
        }

        char *hash = strchr(line, '#');
        if (hash) {
            *hash = 0;
        }
        char *l = trim(line);
        if (!*l) {
            continue;
        }
        char *eq = strchr(l, '=');
        if (!eq) {
            warn(warnings, warnings_len, &count, "line %d: expected key = value\n", lineno);
            continue;
        }
        *eq = 0;
        apply(s, trim(l), trim(eq + 1), lineno, warnings, warnings_len, &count);
    }
    return count;
}

int rg_settings_load(const char *path, rg_settings_t *s, char *warnings, size_t warnings_len)
{
    rg_settings_defaults(s);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (warnings && warnings_len) {
            warnings[0] = 0;
        }
        return -1;
    }
    char *buf = malloc(MAX_FILE_BYTES + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t n = fread(buf, 1, MAX_FILE_BYTES, f);
    fclose(f);
    buf[n] = 0;
    int count = rg_settings_parse(buf, s, warnings, warnings_len);
    free(buf);
    return count;
}
