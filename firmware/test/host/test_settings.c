#include <string.h>
#include "check.h"
#include "rg_settings.h"

int main(void)
{
    rg_settings_t s;
    char warn[512];

    rg_settings_defaults(&s);
    CHECK(s.volume_db == -6 && s.mic_gain_db == 12 && s.max_seconds == 180 && s.standby_screen == RG_STANDBY_FULL);

    /* Valid file with comments, blank lines, CRLF and spacing. */
    rg_settings_defaults(&s);
    int n = rg_settings_parse("# guestbook settings\r\n"
                              "volume_db = -12\r\n"
                              "\r\n"
                              "  mic_gain_db=18   # louder room\n"
                              "standby_screen = minimal\n"
                              "max_seconds = 120",
                              &s, warn, sizeof(warn));
    CHECK(n == 0);
    CHECK(s.volume_db == -12 && s.mic_gain_db == 18 && s.max_seconds == 120);
    CHECK(s.standby_screen == RG_STANDBY_MINIMAL);

    /* Out-of-range values are clamped, never applied raw. */
    rg_settings_defaults(&s);
    n = rg_settings_parse("volume_db = 6\nled_brightness = 9999\nmax_seconds = 1\n", &s, warn, sizeof(warn));
    CHECK(n == 3);
    CHECK(s.volume_db == 0 && s.led_brightness == 255 && s.max_seconds == 10);
    CHECK(strstr(warn, "volume_db") != NULL);

    /* Unknown keys, bad numbers and bad enums warn and leave defaults untouched. */
    rg_settings_defaults(&s);
    n = rg_settings_parse("colour = red\nvolume_db = loud\nstandby_screen = dim\njust text\n", &s, warn, sizeof(warn));
    CHECK(n == 4);
    CHECK(s.volume_db == -6 && s.standby_screen == RG_STANDBY_FULL);

    /* Warnings never overflow the caller's buffer. */
    char tiny[8];
    n = rg_settings_parse("a=1\nb=2\nc=3\n", &s, tiny, sizeof(tiny));
    CHECK(n == 3 && strlen(tiny) < sizeof(tiny));

    /* Missing file: defaults, -1. */
    s.volume_db = 0;
    CHECK(rg_settings_load("/nonexistent/config.txt", &s, warn, sizeof(warn)) == -1);
    CHECK(s.volume_db == -6);

    /* The shipped sdcard/audio/config.txt template loads with zero warnings. */
    n = rg_settings_load(RG_TEMPLATE_CONFIG, &s, warn, sizeof(warn));
    CHECK(n == 0);
    if (n) {
        fprintf(stderr, "%s", warn);
    }
    return DONE();
}
