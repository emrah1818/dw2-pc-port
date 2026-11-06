#ifndef DW2_PICTURETABLE_H
#define DW2_PICTURETABLE_H
#include <stdint.h>
/* Entry 0 often points to the TIM after the sprite lists. Stop at the earliest
 * payload pointer while scanning the directory; never treat payload as offsets. */
static inline uint32_t PictureTable_Read32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline int PictureTable_Count(const uint8_t* d, int size)
{
    if (!d || size < 4)
        return 0;
    uint32_t end = PictureTable_Read32(d);
    if (end < 4 || end >= (uint32_t)size || end % 4)
        return 0;
    for (uint32_t i = 0; i * 4 < end; i++) {
        uint32_t p = PictureTable_Read32(d + i * 4);
        if (!p)
            continue;
        if (p % 4 || p < (i + 1) * 4 || p >= (uint32_t)size)
            return 0;
        if (p < end)
            end = p;
    }
    return (int)(end / 4);
}
static inline uint32_t PictureTable_Entry(const uint8_t* d, int size, int i)
{
    int n = PictureTable_Count(d, size);
    return i >= 0 && i < n ? PictureTable_Read32(d + i * 4) : 0;
}
#endif
