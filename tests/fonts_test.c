/* No disc data: verify font coverage, fitting and atlas/palette isolation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host/fonts.h"
#include "host/buttons.h"
#include "backend/psxgpu.h"
#include "host/picturetable.h"

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
/* the runtime atlas source the fonts register (backend/texrep.c in the game) */
#include "backend/texrep.h"
static TexRepIndexFn src_fn;
static void*         src_ctx;
static int           src_w, src_h, src_tw, src_th, src_count;
void TexRep_AddIndexSource(const uint16_t* words, int w, int h, int mode, int tw, int th, TexRepIndexFn fn, void* ctx)
{
    assert(words && mode == 0);
    src_fn  = fn;
    src_ctx = ctx;
    src_w   = w;
    src_h   = h;
    src_tw  = tw;
    src_th  = th;
    src_count++;
}
void TexRep_ClearIndexSources(void) {}
/* numeric sprites: the 1x path (scale 1 here) */
int  TexRep_SourceGeneration(void) { return 1; }
int  TexRep_RuntimeScale(void) { return 1; }
void TexRep_NoteUploadAt(const uint16_t* words, int w, int h, int vx, int vy) { (void)words; (void)w; (void)h; (void)vx; (void)vy; }
void TexRep_AddIndexOverlay(const uint16_t* words, int w, int h, int mode, TexRepIndexFn fn, void* ctx)
{
    (void)words; (void)w; (void)h; (void)mode; (void)fn; (void)ctx;
}
void Fonts_RegisterHd(int big, const uint8_t* data, int size);

static int nibble(const uint8_t *d,int x,int y) { return (d[68+y*64+x/2]>>((x&1)*4))&15; }
static void test_buttons(void) {
    uint8_t a[68+256*64+16],before[sizeof(a)],again[sizeof(a)],icon[48*24+16];
    assert(Buttons_FontBit(0x3E)==13 && Buttons_FontBit(0x3F)==14);
    assert(Buttons_FontBit(0x40)==12 && Buttons_FontBit(0x41)==15);
    for (int c=0x42;c<=0x4E;c++) assert(Buttons_FontBit(c)==-1); /* punctuation, II, LEVELS */
    for (int bit=0;bit<16;bit++) {
        memset(icon,0xA5,sizeof(icon));
        assert(Buttons_Draw(bit,48,24,icon));
        int ink=0;
        for (int i=0;i<48*24;i++) { assert(icon[i]<16);ink+=icon[i]!=0; }
        assert(ink>50);
        for (int i=48*24;i<(int)sizeof(icon);i++) assert(icon[i]==0xA5);
    }
    for (int big=0;big<2;big++) {
        const int cw=big ? 9:7,ch=big ? 14:10,cols=big ? 13:17;
        memset(a,0,sizeof(a));a[4]=0x10;a[8]=8;a[64]=32;a[67]=1;
        memset(a+sizeof(a)-16,0xA5,16);
        for (int y=248;y<254;y++) { a[68+y*64+2]=0xFF;a[68+y*64+3]=0x7F; }
        /* A letter and an original circle cell must survive the new hidden glyphs. */
        a[68+10/cols*ch*64+(10%cols*cw)/2]=0x12;
        a[68+0x3E/cols*ch*64+(0x3E%cols*cw)/2]=0x21;
        memcpy(before,a,sizeof(a));
        Buttons_SetEnabled(0);
        assert(!Buttons_PatchAtlas(a,sizeof(a)-16,big) && !memcmp(a,before,sizeof(a)));
        Buttons_SetEnabled(1);
        assert(Buttons_PatchAtlas(a,sizeof(a)-16,big)==4);
        for (int y=0;y<256;y++) for (int x=0;x<128;x++) {
            int claimed=y>=248 && x>=64;
            for (int c=0;c<4;c++) {
                int cc=(big ? 0x80:0xF0)+c,x0=cc%cols*cw,y0=cc/cols*ch;
                claimed|=x>=x0 && x<x0+cw && y>=y0 && y<y0+ch;
            }
            if (!claimed) assert(nibble(a,x,y)==nibble(before,x,y));
        }
        assert(!memcmp(a,before,68));
        assert(!memcmp(a+sizeof(a)-16,before+sizeof(a)-16,16));
        memcpy(again,a,sizeof(a));
        assert(Buttons_PatchAtlas(a,sizeof(a)-16,big)==4 && !memcmp(a,again,sizeof(a)));
        for (int c=0x3E;c<=0x41;c++) {
            int u,v;
            assert(Buttons_FontUV(big,c,&u,&v));
            assert(Buttons_FontClut(big,c,0x1234)==0x1235);
            Fonts_RegisterHd(big,a,sizeof(a)-16);
            uint8_t *idx=malloc((size_t)cw*ch*16);
            assert(idx);src_fn(src_ctx,4,u,v,cw,ch,idx);
            int colored=0;
            for (int i=0;i<cw*ch*16;i++) { assert(idx[i]<16);colored+=idx[i]>=4; }
            assert(colored>5);free(idx);
        }
        int u,v;
        assert(!Buttons_FontUV(big,0x4A,&u,&v));
        Fonts_SetEnabled(0);
        Fonts_RegisterHd(big,a,sizeof(a)-16);
        {
            uint8_t idx[9*14*16];
            int x0=10%cols*cw,y0=10/cols*ch;
            src_fn(src_ctx,4,x0,y0,cw,ch,idx);
            for (int y=0;y<ch*4;y++) for (int x=0;x<cw*4;x++)
                assert(idx[y*cw*4+x]==nibble(a,x0+x/4,y0+y/4));
        }
        Fonts_SetEnabled(1);
        Buttons_SetEnabled(0);
        assert(!Buttons_FontUV(big,0x3E,&u,&v));
        assert(Buttons_FontClut(big,0x3E,0x1234)==0x1234);
        /* A mod already using a reserved palette byte is refused without a partial patch. */
        memcpy(a,before,sizeof(a));a[68+248*64+32]=1;memcpy(again,a,sizeof(a));
        Buttons_SetEnabled(1);
        assert(!Buttons_PatchAtlas(a,sizeof(a)-16,big) && !memcmp(a,again,sizeof(a)));
    }
    Buttons_SetEnabled(0);
    puts("buttons_test: 16 inputs, original glyph/CLUT isolation, HD colours, Classic fallback and mod conflict passed");
}

static void button_preview(const char *path) {
    enum { W=960,H=380 };
    uint8_t *rgb=malloc(W*H*3);
    assert(rgb);
    for (int i=0;i<W*H;i++) { rgb[i*3]=13;rgb[i*3+1]=22;rgb[i*3+2]=37; }
    const int bits[16]={14,13,15,12,10,8,11,9,0,3,4,5,6,7,1,2};
    for (int b=0;b<16;b++) {
        int w=b<4 ? 128:112,h=b<4 ? 112:48;
        int x0=b<4 ? 72+b*224 : 16+((b-4)%6)*158,y0=b<4 ? 20:180+((b-4)/6)*100;
        uint8_t *buf=malloc((size_t)w*h);assert(buf);
        assert(Buttons_Draw(bits[b],w,h,buf));
        for (int y=0;y<h;y++) for (int x=0;x<w;x++) if (buf[y*w+x]) {
            uint32_t c=Buttons_Color(buf[y*w+x]);int i=((y0+y)*W+x0+x)*3;
            rgb[i]=(c>>16)&255;rgb[i+1]=(c>>8)&255;rgb[i+2]=c&255;
        }
        free(buf);
    }
    FILE *f=fopen(path,"wb");assert(f);
    fprintf(f,"P6\n%d %d\n255\n",W,H);fwrite(rgb,1,W*H*3,f);fclose(f);free(rgb);
}

int main(int argc, char** argv)
{
    assert(Fonts_Init());
    if (argc==3 && !strcmp(argv[1],"--buttons-preview")) { button_preview(argv[2]); return 0; }
    uint8_t directory[64] = { 40, 0, 0, 0, 16, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0 };
    directory[20]         = 4; /* payload bytes that the old parser mistook for an offset */
    assert(PictureTable_Count(directory, sizeof(directory)) == 4);
    assert(PictureTable_Entry(directory, sizeof(directory), 2) == 32);
    assert(PictureTable_Entry(directory, sizeof(directory), 4) == 0);
    directory[4] = 4;
    assert(PictureTable_Count(directory, sizeof(directory)) == 0);
    assert(!PictureTable_Count(NULL, 0));
    uint8_t developer[514];
    memset(developer, 0xA5, sizeof(developer));
    assert(!Fonts_DevBitmap(developer, 511));
    for (int i = 0; i < 514; i++)
        assert(developer[i] == 0xA5);
    assert(Fonts_DevBitmap(developer, 512));
    assert(developer[512] == 0xA5 && developer[513] == 0xA5);
    for (int i = 0; i < 8; i++)
        assert(developer[i] == 0); /* ASCII space */
    for (int c = '0'; c <= 'Z'; c++) {
        int ink = 0;
        for (int y = 0; y < 8; y++)
            ink |= developer[(c - 32) * 8 + y];
        assert(ink);
    }
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
        /* runtime glyphs: the atlas registered as an index source (32 x 256 words, glyph cells) */
        data[64] = 32;
        data[65] = 0;
        data[66] = 0;
        data[67] = 1;
        Fonts_PatchGameFont(data, sizeof(data) - 16, big);
        int before_count = src_count;
        Fonts_RegisterHd(big, data, sizeof(data) - 16);
        assert(src_count == before_count + 1 && src_w == 32 && src_h == 256);
        assert(src_tw == (big ? 9 : 7) && src_th == (big ? 14 : 10));
        for (int k = 2; k <= 12; k += 5) {
            /* the cell of 'A' (code 10; big atlas: its cell 10 holds code 0x92) */
            const int cw = big ? 9 : 7, ch = big ? 14 : 10, cols = big ? 13 : 17, gw = big ? 8 : 7, gh = big ? 13 : 9;
            int       x0 = (10 % cols) * cw, y0 = (10 / cols) * ch, ink = 0, outside = 0, shadow = 0;
            uint8_t*  idx = calloc((size_t)cw * k * ch * k, 1);
            assert(idx);
            src_fn(src_ctx, k, x0, y0, cw, ch, idx);
            for (int y = 0; y < ch * k; y++)
                for (int x = 0; x < cw * k; x++) {
                    /* body 1, the original shadow 2 (big font: 2 right, 3 below) */
                    assert(idx[y * cw * k + x] <= (big ? 3 : 2));
                    ink += idx[y * cw * k + x] == 1;
                    shadow += idx[y * cw * k + x] >= 2;
                    if (x >= gw * k || y >= gh * k)
                        outside += idx[y * cw * k + x] == 1;
                }
            /* ink in proportion to the 1x cell (at least a third of k*k per 1x ink texel), none
               outside the glyph box */
            int ink1 = 0;
            for (int y = 0; y < gh; y++)
                for (int x = 0; x < gw; x++) {
                    int gx = x0 + x;
                    ink1 += (data[68 + (y0 + y) * 64 + gx / 2] >> ((gx & 1) * 4) & 15) == 1;
                }
            assert(outside == 0 && ink1 > 0 && shadow > 0 && ink * 3 > ink1 * k * k && ink < ink1 * k * k * 3);
            free(idx);
        }
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
    test_buttons();
    puts("fonts_test: coverage, UTF-8, bounded fitting and atlas isolation passed");
    return 0;
}
