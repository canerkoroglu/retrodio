#include "rg_wifi_cfg.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MAX_FILE_BYTES 1024

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

static char *unquote(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        s[n - 1] = 0;
        return s + 1;
    }
    return s;
}

int rg_wifi_cfg_parse(const char *text, rg_wifi_cfg_t *cfg, char *warnings, size_t warnings_len)
{
    int count = 0;
    if (warnings && warnings_len) {
        warnings[0] = 0;
    }
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) { /* UTF-8 BOM written by some Windows editors */
        text += 3;
    }
    char line[160];
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

        char *l = trim(line);
        if (!*l || *l == '#') {
            continue;
        }
        char *eq = strchr(l, '=');
        if (!eq) {
            warn(warnings, warnings_len, &count, "line %d: expected KEY = value\n", lineno);
            continue;
        }
        *eq = 0;
        char *key = trim(l);
        char *val = unquote(trim(eq + 1));
        size_t len = strlen(val);
        if (strcasecmp(key, "SSID") == 0) {
            if (len == 0 || len > RG_WIFI_SSID_MAX) {
                warn(warnings, warnings_len, &count, "line %d: SSID must be 1..%d characters\n", lineno,
                     RG_WIFI_SSID_MAX);
            } else {
                memcpy(cfg->ssid, val, len + 1);
            }
        } else if (strcasecmp(key, "PASSWORD") == 0) {
            if (len < RG_WIFI_PASS_MIN || len > RG_WIFI_PASS_MAX) {
                warn(warnings, warnings_len, &count, "line %d: PASSWORD must be %d..%d characters\n", lineno,
                     RG_WIFI_PASS_MIN, RG_WIFI_PASS_MAX);
            } else {
                memcpy(cfg->password, val, len + 1);
            }
        } else {
            warn(warnings, warnings_len, &count, "line %d: unknown key '%s'\n", lineno, key);
        }
    }
    return count;
}

int rg_wifi_cfg_load(const char *path, rg_wifi_cfg_t *cfg, char *warnings, size_t warnings_len)
{
    if (warnings && warnings_len) {
        warnings[0] = 0;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char buf[MAX_FILE_BYTES + 1];
    size_t n = fread(buf, 1, MAX_FILE_BYTES, f);
    fclose(f);
    buf[n] = 0;
    return rg_wifi_cfg_parse(buf, cfg, warnings, warnings_len);
}
