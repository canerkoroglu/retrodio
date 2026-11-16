#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "rg_err.h"
#include "rg_wav.h"

static char path[256];

static const char *tmp(const char *name)
{
    snprintf(path, sizeof(path), "%s/%s", scratch_dir(), name);
    return path;
}

static void append_raw(const char *p, const void *data, size_t n)
{
    int fd = open(p, O_WRONLY | O_APPEND);
    CHECK(fd >= 0 && write(fd, data, n) == (ssize_t)n);
    close(fd);
}

static void write_file(const char *p, const void *data, size_t n)
{
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0 && write(fd, data, n) == (ssize_t)n);
    close(fd);
}

static long file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

int main(void)
{
    int16_t pcm[1000];
    for (int i = 0; i < 1000; i++) {
        pcm[i] = (int16_t)i;
    }
    rg_wav_writer_t w, w2;
    rg_wav_info_t info;
    rg_wav_repair_action_t act;
    uint32_t bytes;

    /* Writer: never overwrites, checkpoint makes data durable, close leaves a consistent file. */
    const char *a = strdup(tmp("a.wav"));
    CHECK(rg_wav_writer_open(&w, a, 16000) == ESP_OK);
    CHECK(rg_wav_writer_open(&w2, a, 16000) == ESP_ERR_INVALID_STATE);
    CHECK(rg_wav_writer_write(&w, pcm, sizeof(pcm)) == ESP_OK);
    CHECK(rg_wav_writer_checkpoint(&w) == ESP_OK);
    CHECK(rg_wav_writer_write(&w, pcm, sizeof(pcm)) == ESP_OK);
    CHECK(rg_wav_probe(a, &info) == ESP_OK);
    CHECK(!info.header_consistent && info.data_bytes == sizeof(pcm)); /* header lags the data: power-cut state */
    CHECK(rg_wav_writer_close(&w) == ESP_OK);
    CHECK(rg_wav_probe(a, &info) == ESP_OK);
    CHECK(info.header_consistent && info.data_bytes == 2 * sizeof(pcm) && info.data_offset == 44);
    CHECK(rg_wav_is_playable(&info));

    /* Repair: consistent file is left alone. */
    CHECK(rg_wav_repair(a, 0, &act, &bytes) == ESP_OK && act == RG_WAV_REPAIR_NONE && bytes == 2 * sizeof(pcm));

    /* Repair: power cut after a checkpoint plus an odd trailing byte. */
    const char *b = strdup(tmp("b.wav"));
    CHECK(rg_wav_writer_open(&w, b, 16000) == ESP_OK);
    CHECK(rg_wav_writer_write(&w, pcm, sizeof(pcm)) == ESP_OK);
    CHECK(rg_wav_writer_checkpoint(&w) == ESP_OK);
    close(w.fd); /* simulated power loss: no final header */
    append_raw(b, pcm, sizeof(pcm));
    append_raw(b, "x", 1);
    CHECK(rg_wav_repair(b, 1000, &act, &bytes) == ESP_OK && act == RG_WAV_REPAIR_FIXED);
    CHECK(bytes == 2 * sizeof(pcm) && file_size(b) == 44 + 2 * (long)sizeof(pcm));
    CHECK(rg_wav_probe(b, &info) == ESP_OK && info.header_consistent && rg_wav_is_playable(&info));

    /* Repair: power cut before the first checkpoint (data size field still 0). */
    const char *c = strdup(tmp("c.wav"));
    CHECK(rg_wav_writer_open(&w, c, 16000) == ESP_OK);
    CHECK(rg_wav_writer_write(&w, pcm, sizeof(pcm)) == ESP_OK);
    close(w.fd);
    CHECK(rg_wav_repair(c, 1000, &act, &bytes) == ESP_OK && act == RG_WAV_REPAIR_FIXED && bytes == sizeof(pcm));

    /* Repair: too little audio is deleted (no phantom messages). */
    const char *d = strdup(tmp("d.wav"));
    CHECK(rg_wav_writer_open(&w, d, 16000) == ESP_OK);
    close(w.fd);
    CHECK(rg_wav_repair(d, 32000, &act, &bytes) == ESP_OK && act == RG_WAV_REPAIR_DELETED);
    CHECK(file_size(d) == -1);

    /* Repair: header-only file is deleted even with min_seconds = 0 (power cut during the greeting). */
    const char *d0 = strdup(tmp("d0.wav"));
    CHECK(rg_wav_writer_open(&w, d0, 16000) == ESP_OK);
    close(w.fd);
    CHECK(rg_wav_repair(d0, 0, &act, &bytes) == ESP_OK && act == RG_WAV_REPAIR_DELETED);
    CHECK(file_size(d0) == -1);

    /* Repair: not a WAV at all is deleted; missing file reports NOT_FOUND. */
    const char *e = strdup(tmp("e.wav"));
    write_file(e, "garbage-garbage-garbage-garbage-garbage-garbage", 47);
    CHECK(rg_wav_repair(e, 0, &act, &bytes) == ESP_OK && act == RG_WAV_REPAIR_DELETED);
    CHECK(rg_wav_repair(tmp("missing.wav"), 0, &act, &bytes) == ESP_ERR_NOT_FOUND);

    /* Probe: LIST chunk before data (Audacity / ffmpeg metadata) is skipped. */
    uint8_t list_wav[44 + 12 + 8];
    rg_wav_build_header(list_wav, 16000, 1, 16, 8);
    uint8_t fmt_part[36];
    memcpy(fmt_part, list_wav, 36);
    uint8_t buf[sizeof(list_wav)];
    size_t o = 0;
    memcpy(buf + o, fmt_part, 36), o += 36;
    memcpy(buf + o, "LIST\x04\x00\x00\x00INFO", 12), o += 12;
    memcpy(buf + o, "data\x08\x00\x00\x00", 8), o += 8;
    memset(buf + o, 0, 8), o += 8;
    buf[4] = (uint8_t)(o - 8); /* RIFF size */
    const char *f = strdup(tmp("list.wav"));
    write_file(f, buf, o);
    CHECK(rg_wav_probe(f, &info) == ESP_OK);
    CHECK(info.data_offset == 56 && info.data_bytes == 8 && info.header_consistent && rg_wav_is_playable(&info));

    /* Format gate: 44.1 kHz stereo is rejected for playback. */
    rg_wav_build_header(buf, 44100, 2, 16, 0);
    const char *g = strdup(tmp("stereo.wav"));
    write_file(g, buf, 44);
    CHECK(rg_wav_probe(g, &info) == ESP_OK && !rg_wav_is_playable(&info));

    /* Probe: a damaged chunk size that would wrap the 32-bit offset terminates instead of looping. */
    memcpy(buf, list_wav, 36);
    memcpy(buf + 36, "JUNK\xf8\xff\xff\xff", 8);
    const char *h = strdup(tmp("wrap.wav"));
    write_file(h, buf, 52);
    CHECK(rg_wav_probe(h, &info) == ESP_ERR_INVALID_RESPONSE);
    return DONE();
}
