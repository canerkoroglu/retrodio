#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "rg_catalog.h"
#include "rg_wav.h"

static void make_valid(const char *dir, const char *name, size_t pcm_bytes)
{
    char p[256];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    rg_wav_writer_t w;
    static uint8_t zeros[4096];
    CHECK(rg_wav_writer_open(&w, p, 16000) == ESP_OK);
    CHECK(rg_wav_writer_write(&w, zeros, pcm_bytes) == ESP_OK);
    CHECK(rg_wav_writer_close(&w) == ESP_OK);
}

static void make_garbage(const char *dir, const char *name)
{
    char p[256];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0 && write(fd, "not a wav", 9) == 9);
    close(fd);
}

int main(void)
{
    uint32_t idx;
    CHECK(rg_catalog_parse_name("MSG_000001.wav", &idx) && idx == 1);
    CHECK(rg_catalog_parse_name("msg_000123.WAV", &idx) && idx == 123); /* FAT case may vary */
    CHECK(!rg_catalog_parse_name("MSG_000000.wav", &idx));              /* 0 is never used */
    CHECK(!rg_catalog_parse_name("MSG_00001.wav", &idx));
    CHECK(!rg_catalog_parse_name("MSG_0000012.wav", &idx));
    CHECK(!rg_catalog_parse_name("MSG_00A001.wav", &idx));
    CHECK(!rg_catalog_parse_name("TEST_MIC.wav", &idx));
    CHECK(!rg_catalog_parse_name("MSG_000001.wav.tmp", &idx));

    char name[20];
    rg_catalog_format_name(42, name, sizeof(name));
    CHECK(strcmp(name, "MSG_000042.wav") == 0);
    CHECK(rg_catalog_parse_name(name, &idx) && idx == 42);

    /* Scan: counts only valid MSG files; damaged ones are counted separately but still
     * advance the next index, so a damaged file's number is never reused. */
    const char *dir = scratch_dir();
    make_valid(dir, "MSG_000001.wav", 3200);
    make_valid(dir, "MSG_000003.wav", 6400);
    make_garbage(dir, "MSG_000007.wav");
    make_valid(dir, "TEST_MIC.wav", 3200);
    make_garbage(dir, "notes.txt");
    rg_catalog_scan_t scan;
    CHECK(rg_catalog_scan_dir(dir, &scan) == ESP_OK);
    CHECK(scan.valid == 2 && scan.invalid == 1 && scan.max_index == 7 && scan.audio_bytes == 9600);
    CHECK(rg_catalog_scan_dir("/nonexistent-dir", &scan) == ESP_ERR_NOT_FOUND);

    /* Manifest: header once, one row per message, zlib-style lowercase hex CRC. */
    char line[96];
    rg_catalog_manifest_line(line, sizeof(line), "MSG_000001.wav", 32000, 0xDEADBEEF);
    CHECK(strcmp(line, "MSG_000001.wav,32000,1000,deadbeef\n") == 0);
    char mpath[256];
    snprintf(mpath, sizeof(mpath), "%s/manifest.csv", dir);
    CHECK(rg_catalog_manifest_append(mpath, "MSG_000001.wav", 32000, 1) == ESP_OK);
    CHECK(rg_catalog_manifest_append(mpath, "MSG_000002.wav", 64000, 2) == ESP_OK);
    FILE *f = fopen(mpath, "r");
    char content[256] = {0};
    CHECK(f && fread(content, 1, sizeof(content) - 1, f) > 0);
    if (f) {
        fclose(f);
    }
    CHECK(strcmp(content, "file,bytes,duration_ms,crc32\n"
                          "MSG_000001.wav,32000,1000,00000001\n"
                          "MSG_000002.wav,64000,2000,00000002\n") == 0);
    return DONE();
}
