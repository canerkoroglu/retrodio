#include "rg_mic_conv.h"
#include <math.h>

void rg_mic_conv_init(rg_mic_conv_t *c, int gain_db, bool dc_block)
{
    c->gain_q12 = (int32_t)lroundf(4096.0f * powf(10.0f, (float)gain_db / 20.0f));
    c->dc_block = dc_block;
    c->x1 = 0;
    c->y1 = 0;
    c->clipped = 0;
}

void rg_mic_conv_run(rg_mic_conv_t *c, const int32_t *in, size_t in_stride, int16_t *out, size_t n)
{
    const int64_t a_q16 = 65208; /* 0.995 -> ~13 Hz high-pass at 16 kHz */
    for (size_t i = 0; i < n; i++) {
        int32_t x = in[i * in_stride] >> 8; /* 24-bit signed */
        int64_t y = x;
        if (c->dc_block) {
            int64_t yq = ((int64_t)(x - c->x1) << 16) + ((c->y1 * a_q16) >> 16);
            c->x1 = x;
            c->y1 = yq;
            y = yq >> 16;
        }
        int64_t v = (y * c->gain_q12) >> 20; /* 24 -> 16 bit (>>8) and Q12 gain (>>12) */
        if (v > INT16_MAX) {
            v = INT16_MAX;
            c->clipped++;
        } else if (v < INT16_MIN) {
            v = INT16_MIN;
            c->clipped++;
        }
        out[i] = (int16_t)v;
    }
}
