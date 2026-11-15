#include <string.h>
#include "check.h"
#include "rg_crc32.h"

int main(void)
{
    const char *v = "123456789";
    CHECK(rg_crc32_update(0, v, 9) == 0xCBF43926u); /* standard CRC-32 check value */
    CHECK(rg_crc32_update(0, "", 0) == 0);

    /* Chunked updates (as the recorder feeds 4 KB blocks) equal a single pass. */
    uint32_t c = rg_crc32_update(0, v, 4);
    c = rg_crc32_update(c, v + 4, 5);
    CHECK(c == 0xCBF43926u);
    return DONE();
}
