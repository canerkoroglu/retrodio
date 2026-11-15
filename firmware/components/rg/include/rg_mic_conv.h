#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Mic word (24 valid MSBs in an int32 slot) -> int16 with digital gain,
 * optional ~13 Hz DC blocker and saturation. Integer-only: no FPU on the ESP32-C3. */
typedef struct {
    int32_t gain_q12;
    bool dc_block;
    int32_t x1;       /* previous input, 24-bit domain */
    int64_t y1;       /* previous output, Q16 */
    uint32_t clipped; /* saturated samples since init */
} rg_mic_conv_t;

void rg_mic_conv_init(rg_mic_conv_t *c, int gain_db, bool dc_block);
/** in_stride: 1 for mono RX buffers, 2 to pick one slot from stereo frames. */
void rg_mic_conv_run(rg_mic_conv_t *c, const int32_t *in, size_t in_stride, int16_t *out, size_t n);
