/* Host-testable part of the catalog: names, directory scan, manifest rows. POSIX only. */
#include "rg_catalog.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "rg_wav.h"

#define MANIFEST_HEADER "file,bytes,duration_ms,crc32\n"

bool rg_catalog_parse_name(const char *name, uint32_t *index)
{
    /* MSG_ + 6 digits + .wav */
    if (strlen(name) != 14 || strncasecmp(name, "MSG_", 4) != 0 || strcasecmp(name + 10, ".wav") != 0) {
        return false;
    }
    uint32_t v = 0;
    for (int i = 4; i < 10; i++) {
        if (!isdigit((unsigned char)name[i])) {
            return false;
        }
        v = v * 10 + (uint32_t)(name[i] - '0');
    }
    if (v == 0) {
        return false;
    }
    *index = v;
    return true;
}

void rg_catalog_format_name(uint32_t index, char *buf, size_t len)
{
    snprintf(buf, len, "MSG_%06lu.wav", (unsigned long)(index % 1000000));
}

esp_err_t rg_catalog_scan_dir(const char *dir, rg_catalog_scan_t *out)
{
    memset(out, 0, sizeof(*out));
    DIR *d = opendir(dir);
    if (!d) {
        return ESP_ERR_NOT_FOUND;
    }
    char path[320];
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        uint32_t idx;
        if (!rg_catalog_parse_name(e->d_name, &idx)) {
            continue;
        }
        if (idx > out->max_index) {
            out->max_index = idx;
        }
        int n = snprintf(path, sizeof(path), "%s/%.*s", dir, (int)(sizeof(path) / 2), e->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            continue; /* name cannot be a valid MSG file anyway (length is checked above) */
        }
        rg_wav_info_t info;
        if (rg_wav_probe(path, &info) == ESP_OK && rg_wav_is_playable(&info) && info.header_consistent) {
            out->valid++;
            out->audio_bytes += info.data_bytes;
        } else {
            out->invalid++;
        }
    }
    closedir(d);
    return ESP_OK;
}

int rg_catalog_manifest_line(char *buf, size_t len, const char *name, uint32_t data_bytes, uint32_t crc32)
{
    uint32_t ms = (uint32_t)((uint64_t)data_bytes * 1000 / (16000 * 2));
    return snprintf(buf, len, "%s,%lu,%lu,%08lx\n", name, (unsigned long)data_bytes, (unsigned long)ms,
                    (unsigned long)crc32);
}

esp_err_t rg_catalog_manifest_append(const char *manifest_path, const char *name, uint32_t data_bytes, uint32_t crc32)
{
    struct stat st;
    bool fresh = stat(manifest_path, &st) != 0 || st.st_size == 0;
    int fd = open(manifest_path, O_WRONLY | O_CREAT | O_APPEND, 0664);
    if (fd < 0) {
        return ESP_FAIL;
    }
    char line[96];
    bool ok = true;
    if (fresh) {
        ok = write(fd, MANIFEST_HEADER, sizeof(MANIFEST_HEADER) - 1) == (ssize_t)(sizeof(MANIFEST_HEADER) - 1);
    }
    int n = rg_catalog_manifest_line(line, sizeof(line), name, data_bytes, crc32);
    ok = ok && n > 0 && write(fd, line, (size_t)n) == n;
    ok = fsync(fd) == 0 && ok;
    ok = close(fd) == 0 && ok;
    return ok ? ESP_OK : ESP_FAIL;
}
