/* No disc data: verify font coverage, fitting and atlas/palette isolation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host/fonts.h"
#include "backend/psxgpu.h"

uint16_t   PsxGpu_Vram[512][1024];
static int uploads;
void       PsxGpu_LoadImage(int x, int y, int w, int h, const uint16_t* src)
{
    uploads++;
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            PsxGpu_Vram[(y + yy) & 511][(x + xx) & 1023] = src[yy * w + xx];
}

void* Host_PakReadFileLayered(int id, int* size)
{
    (void)id;
    *size = 0;
    return NULL;
}
void Host_PakSetLayer(int id, const uint8_t* data, int size)
{
    (void)id;
    (void)data;
    (void)size;
}
void Lang_RestyleWords(uint8_t* data, int size)
{
    (void)data;
    (void)size;
}

int main(int argc, char** argv)
{
    assert(Fonts_Init());
    const char* samples[] = { "ÄÖÜäöüß", "0123456789 HP/MP", "KAMPF", "STARTTASTE DRÜCKEN", "Magisches Feuer", "Wählen & Bestätigen" };
    for (int role = 0; role < FONT_COUNT; role++) {
        int size;
        assert(Fonts_Data(role, &size) && size > 20000);
        for (int k = 0; k < 6; k++) {
            uint8_t dst[36 * 256], before[sizeof(dst)];
            memset(dst, 0xA5, sizeof(dst));
            memcpy(before, dst, sizeof(dst));
            int width = Fonts_Mask(role, samples[k], dst + 256 + 3, 256, 80, 24), ink = 0;
            assert(width > 0 && width <= 80);
            for (int y = 0; y < 36; y++)
                for (int x = 0; x < 256; x++) {
                    int inside = y >= 1 && y < 25 && x >= 3 && x < 83;
                    if (!inside)
                        assert(dst[y * 256 + x] == before[y * 256 + x]);
                    else
                        ink += dst[y * 256 + x] != 0;
                }
            assert(ink > 0);
        }
        uint8_t cell[32 * 32];
        for (int c = 0; c < 0xED; c++) {
            int cp = Fonts_GameCodepoint(c);
            if (cp && !Fonts_Cell(role, cp, cell, 8, 13)) {
                fprintf(stderr, "cell failure role=%d cp=%x\n", role, cp);
                abort();
            }
        }
    }
    for (int big = 0; big < 2; big++) {
        uint8_t data[68 + 256 * 64 + 16], before[sizeof(data)];
        memset(data, 0xA5, sizeof(data));
        data[4] = 0x10;
        data[8] = 8;
        memcpy(before, data, sizeof(data));
        int count = Fonts_PatchGameFont(data, sizeof(data) - 16, big);
        assert(count == 92); /* letters, digits, punctuation, umlauts, II and L1..L5 */
        assert(!memcmp(data, before, 68));
        /* Palette rows, file tail, and caller's guard bytes are untouched. */
        assert(!memcmp(data + 68 + 224 * 64, before + 68 + 224 * 64, 32 * 64 + 16));
        assert(memcmp(data + 68, before + 68, 224 * 64));
        assert(!Fonts_PatchGameFont(data, 80, big));
    }
    uint8_t out[32];
    memset(out, 0xA5, sizeof(out));
    assert(Fonts_Mask(0, "\xF0\x9F\x98\x80", out, 8, 8, 4) < 0);
    for (int i = 0; i < 32; i++)
        assert(out[i] == 0xA5);
    for (int bits = 0; bits < 2; bits++) {
        uint8_t cell[16] = { 3, 10, 0, 0, 0, 0, 0, 0, 32, 64, 0, 0, 8, 0, 0, 0 };
        int     page = bits << 7, per = bits ? 2 : 4;
        memset(PsxGpu_Vram, 0, sizeof(PsxGpu_Vram));
        for (int y = 10; y < 74; y++)
            for (int x = 0; x < 20; x++)
                PsxGpu_Vram[y][x] = bits ? 0x0101 : 0x1111;
        PsxGpu_Vram[100][1] = 0x7FFF; /* CLUT, brightest ink is index 1 */
        int old             = uploads;
        Fonts_NumberCell(123 + bits, cell, page, 0, 100 << 6);
        assert(uploads == old); /* unregistered images are never changed */
        Fonts_RegisterNumber(123 + bits);
        Fonts_NumberCell(123 + bits, cell, page, 0, 100 << 6);
        assert(uploads == old + 1);
        assert(PsxGpu_Vram[100][1] == 0x7FFF);
        int first = cell[0] / per;
        if (first > 0)
            assert(PsxGpu_Vram[10][0] == (bits ? 0x0101 : 0x1111)); /* left neighbour */
        assert((PsxGpu_Vram[10][first] & ((1 << ((cell[0] % per) * (bits ? 8 : 4))) - 1)) == 1 * (bits ? 1 : 0x111));
        Fonts_NumberCell(123 + bits, cell, page, 0, 100 << 6);
        assert(uploads == old + 1); /* unchanged texture is cached */
        PsxGpu_Vram[10][first] ^= 0xFFFF;
        Fonts_NumberCell(123 + bits, cell, page, 0, 100 << 6);
        assert(uploads == old + 2); /* an upload/reload invalidates the cache */
    }
    if (argc > 1) {
        /* PGM contact sheet from the exact C rasterizer, no game content. */
        enum { W = 768,
               H = 270 };
        uint8_t*    image   = calloc(W, H);
        const char* line[]  = { "KAMPF  /  LEVELAUFSTIEG", "Wählen  ÄÖÜ äöü ß  Gegenangriff", "HP 123/999   MP 045/300   EXP 01234567" };
        const int   roles[] = { FONT_DISPLAY, FONT_INTERFACE, FONT_TELEMETRY };
        for (int i = 0; i < 3; i++)
            assert(Fonts_Mask(roles[i], line[i], image + (i * 90 + 12) * W + 12, W, W - 24, 56) > 0);
        FILE* f = fopen(argv[1], "wb");
        assert(f);
        fprintf(f, "P5\n%d %d\n255\n", W, H);
        fwrite(image, 1, W * H, f);
        fclose(f);
        free(image);
    }
    puts("fonts_test: coverage, UTF-8, bounded fitting and atlas isolation passed");
    return 0;
}
