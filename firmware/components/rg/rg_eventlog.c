#include "rg_eventlog.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "rg_board.h"
#include "rg_storage.h"
#include "esp_timer.h"

#define LOG_MAX_BYTES (256 * 1024)
#define LOG_OLD       RG_DIR_AUDIO "/log.old"

static uint32_t s_boot, s_session;

void rg_eventlog_set_boot(uint32_t boot_count, uint32_t session)
{
    s_boot = boot_count;
    s_session = session;
}

esp_err_t rg_eventlog(const char *fmt, ...)
{
    if (!rg_storage_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    struct stat st;
    if (stat(RG_FILE_LOG, &st) == 0 && st.st_size > LOG_MAX_BYTES) {
        unlink(LOG_OLD);
        rename(RG_FILE_LOG, LOG_OLD);
    }
    FILE *f = fopen(RG_FILE_LOG, "a");
    if (!f) {
        return ESP_FAIL;
    }
    fprintf(f, "b%lu.%lu +%lu ", (unsigned long)s_boot, (unsigned long)s_session,
            (unsigned long)(esp_timer_get_time() / 1000));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = fclose(f) == 0 && ok;
    return ok ? ESP_OK : ESP_FAIL;
}
