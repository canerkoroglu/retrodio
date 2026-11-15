#include <stdlib.h>
#include "check.h"
#include "rg_mic_conv.h"

/* Mic words carry a 24-bit sample in the upper bits of an int32 slot. */
static int32_t word(int32_t sample24)
{
    return (int32_t)((uint32_t)sample24 << 8);
}

int main(void)
{
    rg_mic_conv_t c;
    int32_t in[4];
    int16_t out[4];

    /* 0 dB, no DC blocker: 24-bit -> 16-bit is a plain >> 8. */
    rg_mic_conv_init(&c, 0, false);
    in[0] = word(1000 * 256);
    in[1] = word(-1000 * 256);
    rg_mic_conv_run(&c, in, 1, out, 2);
    CHECK(out[0] == 1000 && out[1] == -1000);

    /* +12 dB = x3.98 */
    rg_mic_conv_init(&c, 12, false);
    in[0] = word(1000 * 256);
    rg_mic_conv_run(&c, in, 1, out, 1);
    CHECK(out[0] >= 3975 && out[0] <= 3990);

    /* Saturation instead of wrap-around, and it is counted. */
    rg_mic_conv_init(&c, 12, false);
    in[0] = word(0x7FFFFF);
    in[1] = word(-0x800000);
    rg_mic_conv_run(&c, in, 1, out, 2);
    CHECK(out[0] == INT16_MAX && out[1] == INT16_MIN && c.clipped == 2);

    /* Stride 2 picks one slot out of stereo frames. */
    rg_mic_conv_init(&c, 0, false);
    int32_t stereo[4] = {word(256 * 7), word(256 * 99), word(256 * 8), word(256 * 99)};
    rg_mic_conv_run(&c, stereo, 2, out, 2);
    CHECK(out[0] == 7 && out[1] == 8);

    /* DC blocker: a constant offset decays to ~0 within a second at 16 kHz. */
    rg_mic_conv_init(&c, 0, true);
    int32_t dc = word(20000 * 256);
    int16_t y = 0;
    for (int i = 0; i < 16000; i++) {
        rg_mic_conv_run(&c, &dc, 1, &y, 1);
    }
    CHECK(abs(y) < 50);
    return DONE();
}
