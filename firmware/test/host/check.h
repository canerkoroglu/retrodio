/* Minimal test harness: CHECK records failures, DONE() sets the exit code. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* mkdtemp on macOS */

static int g_failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                   \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)
#define DONE() (printf("%s\n", g_failures ? "FAILED" : "OK"), g_failures ? EXIT_FAILURE : EXIT_SUCCESS)

/* Fresh scratch directory per test binary. */
static inline const char *scratch_dir(void)
{
    static char dir[] = "/tmp/rg_host_XXXXXX";
    static int made;
    if (!made) {
        if (!mkdtemp(dir)) {
            perror("mkdtemp");
            exit(EXIT_FAILURE);
        }
        made = 1;
    }
    return dir;
}
