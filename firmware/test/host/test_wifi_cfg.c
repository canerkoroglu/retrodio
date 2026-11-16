#include <string.h>
#include "check.h"
#include "rg_wifi_cfg.h"

static void defaults(rg_wifi_cfg_t *c)
{
    strcpy(c->ssid, "Default");
    strcpy(c->password, "defaultpw");
}

int main(void)
{
    rg_wifi_cfg_t c;
    char warn[256];

    /* The documented format, with CRLF, spacing, comments and mixed-case keys. */
    defaults(&c);
    int n = rg_wifi_cfg_parse("# listening mode\r\nSSID = Guestbook\r\n\r\n  password=secret123  \r\n", &c, warn,
                              sizeof(warn));
    CHECK(n == 0);
    CHECK(strcmp(c.ssid, "Guestbook") == 0 && strcmp(c.password, "secret123") == 0);

    /* UTF-8 BOM, quotes, spaces inside the SSID, '#' and '=' inside the password. */
    defaults(&c);
    n = rg_wifi_cfg_parse("\xEF\xBB\xBFSSID = \"Naz & Caner\"\nPASSWORD = a#b=c d12\n", &c, warn, sizeof(warn));
    CHECK(n == 0);
    CHECK(strcmp(c.ssid, "Naz & Caner") == 0 && strcmp(c.password, "a#b=c d12") == 0);

    /* An empty password is rejected: no open network by accident. */
    defaults(&c);
    n = rg_wifi_cfg_parse("PASSWORD =\n", &c, warn, sizeof(warn));
    CHECK(n == 1 && strcmp(c.password, "defaultpw") == 0);

    /* Invalid values warn and keep the previous value. */
    defaults(&c);
    n = rg_wifi_cfg_parse("SSID =\nPASSWORD = short\nSSID = 123456789012345678901234567890123\n", &c, warn,
                          sizeof(warn));
    CHECK(n == 3);
    CHECK(strcmp(c.ssid, "Default") == 0 && strcmp(c.password, "defaultpw") == 0);
    CHECK(strstr(warn, "PASSWORD") != NULL);

    /* Unknown keys and lines without '=' warn. */
    defaults(&c);
    n = rg_wifi_cfg_parse("channel = 6\njust text\n", &c, warn, sizeof(warn));
    CHECK(n == 2);

    /* Missing file. */
    defaults(&c);
    CHECK(rg_wifi_cfg_load("/nonexistent/wifi.txt", &c, warn, sizeof(warn)) == -1);
    CHECK(strcmp(c.ssid, "Default") == 0);

    /* The shipped example (placeholders only) parses without warnings. */
    c.ssid[0] = 0;
    c.password[0] = 0;
    CHECK(rg_wifi_cfg_load(RG_TEMPLATE_WIFI, &c, warn, sizeof(warn)) == 0);
    CHECK(c.ssid[0] != 0 && strlen(c.password) >= RG_WIFI_PASS_MIN);

    return DONE();
}
