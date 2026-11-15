#include "rg_wav.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_check.h"
#include "esp_log.h"
#include "rg_err.h"

static const char *TAG = "rg_wav";

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

void rg_wav_build_header(uint8_t h[RG_WAV_HEADER_SIZE], uint32_t rate, uint16_t channels, uint16_t bits,
                         uint32_t data_bytes)
{
    uint16_t block_align = (uint16_t)(channels * bits / 8);
    memcpy(h + 0, "RIFF", 4);
    put_le32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    put_le32(h + 16, 16);
    put_le16(h + 20, 1);
    put_le16(h + 22, channels);
    put_le32(h + 24, rate);
    put_le32(h + 28, rate * block_align);
    put_le16(h + 32, block_align);
    put_le16(h + 34, bits);
    memcpy(h + 36, "data", 4);
    put_le32(h + 40, data_bytes);
}

esp_err_t rg_wav_probe(const char *path, rg_wav_info_t *info)
{
    memset(info, 0, sizeof(*info));
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    struct stat st;
    uint8_t buf[24];
    bool have_fmt = false;
    if (fstat(fd, &st) != 0 || read(fd, buf, 12) != 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
        goto out;
    }
    info->file_bytes = (uint32_t)st.st_size;
    uint32_t riff_size = le32(buf + 4);
    uint32_t pos = 12;
    while (pos + 8 <= info->file_bytes) {
        if (lseek(fd, pos, SEEK_SET) < 0 || read(fd, buf, 8) != 8) {
            goto out;
        }
        uint32_t size = le32(buf + 4);
        if (!memcmp(buf, "fmt ", 4)) {
            if (size < 16 || read(fd, buf, 16) != 16) {
                goto out;
            }
            info->format = le16(buf);
            info->channels = le16(buf + 2);
            info->sample_rate = le32(buf + 4);
            info->bits = le16(buf + 14);
            have_fmt = true;
        } else if (!memcmp(buf, "data", 4)) {
            if (!have_fmt) {
                goto out;
            }
            info->data_offset = pos + 8;
            uint32_t avail = info->file_bytes - info->data_offset;
            info->data_bytes = size > avail ? avail : size;
            info->header_consistent = size == avail && riff_size == info->file_bytes - 8;
            err = ESP_OK;
            goto out;
        }
        pos += 8 + size + (size & 1);
    }
out:
    close(fd);
    return err;
}

bool rg_wav_is_playable(const rg_wav_info_t *i)
{
    return i->format == 1 && i->channels == 1 && i->bits == 16 && i->sample_rate == 16000;
}

esp_err_t rg_wav_writer_open(rg_wav_writer_t *w, const char *path, uint32_t rate)
{
    w->fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (w->fd < 0) {
        ESP_LOGE(TAG, "open %s: errno %d", path, errno);
        return errno == EEXIST ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }
    w->rate = rate;
    w->data_bytes = 0;
    uint8_t h[RG_WAV_HEADER_SIZE];
    rg_wav_build_header(h, rate, 1, 16, 0);
    if (write(w->fd, h, sizeof(h)) != sizeof(h)) {
        close(w->fd);
        w->fd = -1;
        unlink(path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t rg_wav_writer_write(rg_wav_writer_t *w, const void *data, size_t bytes)
{
    ESP_RETURN_ON_FALSE(w->fd >= 0, ESP_ERR_INVALID_STATE, TAG, "closed");
    ssize_t n = write(w->fd, data, bytes);
    if (n > 0) {
        w->data_bytes += (uint32_t)n;
    }
    if (n != (ssize_t)bytes) {
        ESP_LOGE(TAG, "write %u -> %d, errno %d", (unsigned)bytes, (int)n, errno);
        return errno == ENOSPC ? RG_ERR_CARD_FULL : ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t rg_wav_writer_checkpoint(rg_wav_writer_t *w)
{
    ESP_RETURN_ON_FALSE(w->fd >= 0, ESP_ERR_INVALID_STATE, TAG, "closed");
    uint8_t h[RG_WAV_HEADER_SIZE];
    rg_wav_build_header(h, w->rate, 1, 16, w->data_bytes);
    if (lseek(w->fd, 0, SEEK_SET) != 0 || write(w->fd, h, sizeof(h)) != sizeof(h) || lseek(w->fd, 0, SEEK_END) < 0 ||
        fsync(w->fd) != 0) {
        ESP_LOGE(TAG, "checkpoint failed, errno %d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t rg_wav_writer_close(rg_wav_writer_t *w)
{
    if (w->fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = rg_wav_writer_checkpoint(w);
    if (close(w->fd) != 0 && err == ESP_OK) {
        err = ESP_FAIL;
    }
    w->fd = -1;
    return err;
}

esp_err_t rg_wav_repair(const char *path, uint32_t min_data_bytes, rg_wav_repair_action_t *action, uint32_t *data_bytes)
{
    *action = RG_WAV_REPAIR_NONE;
    *data_bytes = 0;
    struct stat st;
    if (stat(path, &st) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    rg_wav_info_t info;
    uint32_t offset;
    bool is_wav = true;
    if (rg_wav_probe(path, &info) == ESP_OK) {
        offset = info.data_offset;
    } else {
        /* Our writer always emits the canonical 44-byte header first; accept it if RIFF/WAVE is present. */
        uint8_t magic[12];
        int fd = open(path, O_RDONLY);
        is_wav = fd >= 0 && read(fd, magic, 12) == 12 && !memcmp(magic, "RIFF", 4) && !memcmp(magic + 8, "WAVE", 4);
        if (fd >= 0) {
            close(fd);
        }
        offset = RG_WAV_HEADER_SIZE;
    }
    uint32_t size = (uint32_t)st.st_size;
    uint32_t data = size > offset ? (size - offset) & ~1U : 0;
    if (!is_wav || data < min_data_bytes) {
        *action = RG_WAV_REPAIR_DELETED;
        return unlink(path) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (info.header_consistent && offset + data == size) {
        *data_bytes = data; /* nothing to fix */
        return ESP_OK;
    }
    if (offset + data != size && truncate(path, (off_t)(offset + data)) != 0) {
        return ESP_FAIL;
    }
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        return ESP_FAIL;
    }
    uint8_t le[4];
    put_le32(le, offset + data - 8);
    bool ok = pwrite(fd, le, 4, 4) == 4;
    put_le32(le, data);
    ok = ok && pwrite(fd, le, 4, offset - 4) == 4;
    ok = fsync(fd) == 0 && ok;
    ok = close(fd) == 0 && ok;
    if (!ok) {
        return ESP_FAIL;
    }
    *action = RG_WAV_REPAIR_FIXED;
    *data_bytes = data;
    return ESP_OK;
}
