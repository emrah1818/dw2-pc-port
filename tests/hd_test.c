/* dw2-pc-port renderer test (no game data): a synthetic scene of PS1 primitives with generated
 * textures and CLUTs, drawn by the HD software rasterizer and, when a Vulkan device exists, by
 * the GPU renderer (SDL_GPU + the SPIR-V shaders). Checks for each option set (texture filter,
 * 2D smoothing):
 *   - the software and GPU pictures agree (few pixels differ by more than a small amount),
 *   - the texture filter changes the 3D quads and leaves the sprites alone,
 *   - 2D smoothing changes the sprites and leaves the 3D quads alone,
 *   - texture packs (backend/texrep.c): --dump-textures names, a 4x replacement drawn texel-exact
 *     on the sprites, broken / wrong-size files ignored, the 1x picture unchanged.
 * Pictures go to the output folder as .bmp for a look.
 *
 *   hd_test [--out DIR] [--require-gpu] [--scale N]
 * Exit 0 = all checks passed (GPU checks skipped without a device unless --require-gpu). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "backend/psxgpu.h"
#include "backend/psxgpu_hd.h"
#include "backend/texrep.h"

/* backend/pgxp.c needs two GTE symbols; PGXP stays off here */
uint32_t Gte_MatSerial;
uint32_t Gte_Cfc2(int reg) {
    (void)reg;
    return 0;
}

#define W 320
#define H 240

static const char *out_dir = ".";
static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static void write_bmp(const char *name, const uint32_t *px, int w, int h) {
    char path[1024];
    FILE *f;
    uint8_t hdr[54] = { 'B', 'M' };
    int row = w * 3, pad = (4 - row % 4) % 4, y;
    uint32_t size = 54 + (uint32_t)(row + pad) * h;

    snprintf(path, sizeof(path), "%s/%s.bmp", out_dir, name);
    f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54;
    hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (y = h - 1; y >= 0; y--) {
        int x;
        for (x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * w + x];
            uint8_t b[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
            fwrite(b, 1, 3, f);
        }
        fwrite("\0\0\0", 1, (size_t)pad, f);
    }
    fclose(f);
}

static uint16_t rgb15(int r, int g, int b) {
    return (uint16_t)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

/* Textures: a 4-bit "glyph" sheet at VRAM (512, 0) (64x64 texels = 16 x 64 words), CLUT16 at
 * (0, 480); an 8-bit gradient / checker at (640, 0) with CLUT256 at (0, 481); a 15-bit picture
 * at (768, 0). Index / colour 0 is transparent. */
static void load_textures(void) {
    static uint16_t t4[64][16], t8[64][32], t15[64][64], clut16[16], clut256[256];
    int x, y;

    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            /* letters made of diagonal strokes with a shade and a 1-texel shadow */
            int gx = x % 16, gy = y % 16, idx = 0;
            int stroke = (gx == gy || gx == 15 - gy || gx == 3 || (gy == 8 && gx < 12)) && gx < 14 && gy < 14;
            int shadow = !stroke && gx > 0 && gy > 0 && ((gx - 1 == gy - 1) || (gx - 1 == 15 - (gy - 1)) || gx - 1 == 3);

            if (stroke) {
                idx = gy < 7 ? 1 : 2;
            } else if (shadow) {
                idx = 3;
            } else if (((x / 8) + (y / 8)) & 1) {
                idx = 4 + ((x * 3 + y) & 7);
            }
            t4[y][x / 4] |= (uint16_t)(idx << ((x & 3) * 4));
        }
    }
    clut16[0] = 0;
    clut16[1] = rgb15(255, 230, 90);
    clut16[2] = rgb15(240, 200, 60);
    clut16[3] = rgb15(30, 20, 10);
    for (x = 4; x < 16; x++) {
        clut16[x] = rgb15(40 + x * 12, 80 + x * 8, 200 - x * 9) | (x == 9 ? 0x8000 : 0);
    }
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            int idx = ((x ^ y) & 8) ? (x * 4) & 255 : (y * 4 + 1) & 255;
            if ((x - 32) * (x - 32) + (y - 32) * (y - 32) < 64) {
                idx = 0;
            }
            t8[y][x / 2] |= (uint16_t)(idx << ((x & 1) * 8));
        }
    }
    for (x = 0; x < 256; x++) {
        clut256[x] = x == 0 ? 0 : rgb15(x, 255 - x, (x * 7) & 255);
    }
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            t15[y][x] = (uint16_t)(rgb15(x * 4, y * 4, ((x / 4 + y / 4) & 1) * 200) | 0x0001);
        }
    }
    PsxGpu_LoadImage(512, 0, 16, 64, &t4[0][0]);
    PsxGpu_LoadImage(640, 0, 32, 64, &t8[0][0]);
    PsxGpu_LoadImage(768, 0, 64, 64, &t15[0][0]);
    PsxGpu_LoadImage(0, 480, 16, 1, clut16);
    PsxGpu_LoadImage(0, 481, 256, 1, clut256);
}

#define XY(x, y) ((uint32_t)(uint16_t)(x) | (uint32_t)(uint16_t)(y) << 16)
#define UV(u, v, hi) ((uint32_t)(u) | (uint32_t)(v) << 8 | (uint32_t)(hi) << 16)
#define CLUT(x, y) ((uint32_t)((y) << 6 | ((x) >> 4)))
/* texpage: x / 64, y / 256, abr, colour mode */
#define TPAGE(px, py, abr, mode) ((uint32_t)((px) / 64 | ((py) / 256) << 4 | (abr) << 5 | (mode) << 7))

static void quad_ft4(uint32_t col, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, int u0, int v0, int u1,
                     int v1, int u2, int v2, int u3, int v3, uint32_t clut, uint32_t tpage, int semi) {
    uint32_t w[9] = { (semi ? 0x2E000000u : 0x2C000000u) | col, XY(x0, y0), UV(u0, v0, clut), XY(x1, y1), UV(u1, v1, tpage),
                      XY(x2, y2), UV(u2, v2, 0), XY(x3, y3), UV(u3, v3, 0) };
    PsxGpu_Gp0(w, 9);
}

static void sprite(int x, int y, int w, int h, int u, int v, uint32_t clut) {
    uint32_t c[4] = { 0x64808080u, XY(x, y), UV(u, v, clut), XY(w, h) };
    PsxGpu_Gp0(c, 4);
}

static void scene(void) {
    uint32_t setup[] = {
        0xE3000000u,                                /* draw area top left 0, 0 */
        0xE4000000u | (239u << 10) | 319u,          /* bottom right 319, 239 */
        0xE5000000u,                                /* offset 0, 0 */
        0xE6000000u,
        0x02101828u, XY(0, 0), XY(W, H),            /* fill */
    };
    uint32_t tp4 = TPAGE(512, 0, 0, 0), tp8 = TPAGE(640, 0, 0, 1), tp15 = TPAGE(768, 0, 1, 2);
    uint32_t e1;
    int i;

    PsxHd_Register(0, 0, W, H);
    PsxGpu_Gp0(setup, (int)(sizeof(setup) / sizeof(setup[0])));
    /* 3D-like quads (rotated, foreshortened): texture filtering applies */
    quad_ft4(0x808080, 20, 30, 140, 10, 10, 120, 150, 110, 0, 0, 63, 0, 0, 63, 63, 63, CLUT(0, 480), tp4, 0);
    quad_ft4(0x8080A0, 170, 20, 300, 40, 160, 130, 290, 120, 0, 0, 63, 0, 0, 63, 63, 63, CLUT(0, 481), tp8, 0);
    quad_ft4(0x707070, 40, 130, 150, 140, 30, 230, 160, 220, 0, 0, 63, 0, 0, 63, 63, 63, 0, tp15, 1);
    /* 2D: sprite-like quads at 2x (the game's text) and SPRT rects: 2D smoothing applies */
    e1 = 0xE1000000u | tp4;
    PsxGpu_Gp0(&e1, 1);
    for (i = 0; i < 3; i++) {
        int u = i * 16;
        quad_ft4(0x808080, 180 + i * 34, 150, 212 + i * 34, 150, 180 + i * 34, 182, 212 + i * 34, 182, u, 0, u + 16, 0, u,
                 16, u + 16, 16, CLUT(0, 480), tp4, 0);
        sprite(180 + i * 34, 195, 16, 16, u, 16, CLUT(0, 480));
    }
}


/* ---- texture packs (backend/texrep.c) ---- */

static uint32_t *render(int gpu, int filter, int s2d, int scale, int *w, int *h);
static int diff_count(const uint32_t *a, const uint32_t *b, int w, int scale, int x0, int y0, int x1, int y1, int tol);

static void pattern(int hx, int hy, int *r, int *g, int *b) {
    *r = (hx * 37) & 0xF8;
    *g = (hy * 53) & 0xF8;
    *b = ((hx ^ hy) & 3) * 80;
}

static int write_png(const char *path, int w, int h, int bad) {
    SDL_Surface *s;
    int ok;

    if (bad) { /* not a PNG */
        FILE *f = fopen(path, "wb");
        if (f == NULL) return 0;
        fputs("this is no png", f);
        fclose(f);
        return 1;
    }
    s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (s == NULL) return 0;
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch);
        for (int x = 0; x < w; x++) {
            int r, g, b;
            pattern(x, y, &r, &g, &b);
            row[x] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
    }
    ok = SDL_SavePNG(s, path);
    SDL_DestroySurface(s);
    return ok;
}

/* the file in dir whose name ends with `tail` (dumped names start with the upload hash) */
static int find_dump(const char *dir, const char *pattern_glob, char *name, size_t n) {
    int count = 0;
    char **f = SDL_GlobDirectory(dir, pattern_glob, 0, &count);
    int ok = f != NULL && count == 1;
    if (ok) SDL_strlcpy(name, f[0], n);
    SDL_free(f);
    return ok;
}

static void texture_pack_tests(int scale, const uint32_t *orig, int w) {
    char dump_dir[1024], root[1024], pack[1024], path[1200], n4[256], n8[256], n15[256];
    uint32_t *a1, *b1, *hd;
    int w1, h1, wh, hh, bad = 0, opaque = 0;

    snprintf(dump_dir, sizeof(dump_dir), "%s/texdump", out_dir);
    snprintf(root, sizeof(root), "%s/Textures", out_dir);
    snprintf(pack, sizeof(pack), "%s/Textures/testpack", out_dir);
    SDL_CreateDirectory(root);
    SDL_CreateDirectory(pack);
    /* the 1x picture before any pack (classic path) */
    PsxHd_SetScale(1);
    PsxGpu_Reset(0);
    PsxGpu_SetDisplay(0, 0, W, H, 0);
    PsxGpu_SetDisplayEnable(1);
    load_textures();
    scene();
    a1 = calloc((size_t)640 * 480, 4);
    PsxGpu_ReadDisplay(a1, &w1, &h1);

    TexRep_SetDump(dump_dir);
    free(render(0, 0, 0, scale, &wh, &hh));
    check(find_dump(dump_dir, "*_16x64_m0_c*.png", n4, sizeof(n4)), "textures: 4-bit sheet dumped (hash_16x64_m0_cCLUT)");
    check(find_dump(dump_dir, "*_32x64_m1_c*.png", n8, sizeof(n8)), "textures: 8-bit texture dumped (hash_32x64_m1_cCLUT)");
    check(find_dump(dump_dir, "*_64x64.png", n15, sizeof(n15)), "textures: 16-bit texture dumped (hash_64x64)");
    {
        SDL_Surface *d;
        snprintf(path, sizeof(path), "%s/%s", dump_dir, n4);
        d = SDL_LoadPNG(path);
        check(d != NULL && d->w == 64 && d->h == 64, "textures: dumped 4-bit sheet is 64x64 texels");
        if (d) SDL_DestroySurface(d);
    }
    /* the pack: the 4-bit sheet at 4x for every palette (name without _c), a broken file for the
     * 16-bit texture, a wrong size for the 8-bit one */
    {
        char base[256];
        SDL_strlcpy(base, n4, sizeof(base));
        *strstr(base, "_c") = 0;
        snprintf(path, sizeof(path), "%s/%s.png", pack, base);
        check(write_png(path, 64 * scale, 64 * scale, 0), "textures: test pack written");
        snprintf(path, sizeof(path), "%s/%s", pack, n15);
        write_png(path, 0, 0, 1);
        snprintf(path, sizeof(path), "%s/%s", pack, n8);
        write_png(path, 100, 100, 0);
        snprintf(path, sizeof(path), "%s/not-a-texture.png", pack);
        write_png(path, 8, 8, 0);
    }
    TexRep_AddRoot(root);
    check(TexRep_Count() == 3, "textures: 3 files with texture names found, the other name skipped");
    hd = render(0, 0, 0, scale, &wh, &hh);
    check(hd != NULL && wh == w, "textures: HD picture with the pack");
    if (hd != NULL && wh == w) {
        char what[200];
        /* sprites (SPRT 16x16 at x 180 + i*34, y 195, u = i*16, v = 16): every HD pixel is the
         * replacement texel inside its original texel */
        for (int i = 0; i < 3; i++) {
            for (int Y = 195 * scale; Y < (195 + 16) * scale; Y++) {
                for (int X = (180 + i * 34) * scale; X < (180 + i * 34 + 16) * scale; X++) {
                    int hx = (i * 16) * scale + (X - (180 + i * 34) * scale), hy = 16 * scale + (Y - 195 * scale);
                    int r, g, b;
                    uint32_t p = hd[(size_t)Y * w + X];
                    pattern(hx, hy, &r, &g, &b);
                    if (abs((int)((p >> 16) & 255) - r) > 8 || abs((int)((p >> 8) & 255) - g) > 8 ||
                        abs((int)(p & 255) - (b & 0xF8)) > 8) {
                        bad++;
                    }
                    opaque++;
                }
            }
        }
        snprintf(what, sizeof(what), "textures: sprites drawn from the 4x replacement, texel-exact (%d of %d pixels off)",
                 bad, opaque);
        check(bad == 0 && opaque > 0, what);
        check(diff_count(orig, hd, w, scale, 10, 10, 160, 135, 4) > 1000, "textures: the 4-bit 3D quad uses the replacement");
        check(diff_count(orig, hd, w, scale, 170, 20, 300, 130, 0) == 0, "textures: wrong-size 8-bit file ignored (original)");
        check(diff_count(orig, hd, w, scale, 40, 145, 150, 215, 0) == 0, "textures: broken 16-bit file ignored (original)");
        write_bmp("soft_texture_pack", hd, w, hh);
        {
            int loaded, failed, uses;
            TexRep_Stats(&loaded, &failed, &uses);
            check(loaded == 1 && failed == 2, "textures: 1 replacement loaded, 2 bad files reported");
        }
    }
    free(hd);
    /* 1x (classic) with the pack: unchanged */
    PsxHd_SetScale(1);
    PsxGpu_Reset(0);
    PsxGpu_SetDisplay(0, 0, W, H, 0);
    PsxGpu_SetDisplayEnable(1);
    load_textures();
    scene();
    b1 = calloc((size_t)640 * 480, 4);
    PsxGpu_ReadDisplay(b1, &w1, &h1);
    check(memcmp(a1, b1, (size_t)w1 * h1 * 4) == 0, "textures: 1x picture unchanged by the pack");
    free(a1);
    free(b1);
}

/* ---- GPU readback ---- */

static SDL_GPUDevice *dev;

static int read_gpu(uint32_t *out, int *pw, int *ph) {
    void *tex;
    int tw, th, sx, sy, sw, sh, x, y;
    SDL_GPUTransferBufferCreateInfo ti = { 0 };
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTextureRegion reg = { 0 };
    SDL_GPUTextureTransferInfo dst = { 0 };
    SDL_GPUFence *fence;
    const uint8_t *p;

    if (!PsxGpu_DisplayTexture(&tex, &tw, &th, &sx, &sy, &sw, &sh)) {
        return 0;
    }
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = (Uint32)(sw * sh * 4);
    tb = SDL_CreateGPUTransferBuffer(dev, &ti);
    cmd = SDL_AcquireGPUCommandBuffer(dev);
    cp = SDL_BeginGPUCopyPass(cmd);
    reg.texture = (SDL_GPUTexture *)tex;
    reg.x = (Uint32)sx;
    reg.y = (Uint32)sy;
    reg.w = (Uint32)sw;
    reg.h = (Uint32)sh;
    reg.d = 1;
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &reg, &dst);
    SDL_EndGPUCopyPass(cp);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);
    p = (const uint8_t *)SDL_MapGPUTransferBuffer(dev, tb, false);
    for (y = 0; y < sh; y++) {
        for (x = 0; x < sw; x++) {
            const uint8_t *q = p + ((size_t)y * sw + x) * 4; /* R8G8B8A8 */
            out[(size_t)y * sw + x] = (uint32_t)q[0] << 16 | (uint32_t)q[1] << 8 | q[2];
        }
    }
    SDL_UnmapGPUTransferBuffer(dev, tb);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    *pw = sw;
    *ph = sh;
    return 1;
}

/* ---- one picture ---- */

static uint32_t *render(int gpu, int filter, int s2d, int scale, int *w, int *h) {
    uint32_t *px = calloc((size_t)W * H * scale * scale * 4, 1);
    int ok;

    PsxHd_SetGpu(gpu);
    PsxHd_SetScale(scale);
    PsxHd_SetTexFilter(filter);
    PsxHd_SetSmooth2d(s2d);
    PsxGpu_Reset(0);
    PsxGpu_SetDisplay(0, 0, W, H, 0);
    PsxGpu_SetDisplayEnable(1);
    load_textures();
    scene();
    ok = gpu ? read_gpu(px, w, h) : PsxGpu_ReadDisplayHd(px, w, h);
    if (!ok) {
        free(px);
        return NULL;
    }
    return px;
}

/* pixels differing by more than tol in any 8-bit channel, inside the rect (1x coordinates) */
static int diff_count(const uint32_t *a, const uint32_t *b, int w, int scale, int x0, int y0, int x1, int y1, int tol) {
    int n = 0, x, y;

    for (y = y0 * scale; y < y1 * scale; y++) {
        for (x = x0 * scale; x < x1 * scale; x++) {
            uint32_t p = a[(size_t)y * w + x], q = b[(size_t)y * w + x];
            int k;
            for (k = 0; k < 24; k += 8) {
                int d = (int)((p >> k) & 255) - (int)((q >> k) & 255);
                if (d > tol || d < -tol) {
                    n++;
                    break;
                }
            }
        }
    }
    return n;
}

int main(int argc, char **argv) {
    int require_gpu = 0, scale = 4, i, w = 0, h = 0, w2 = 0, h2 = 0, have_gpu = 0;
    uint32_t *soft[4], *gpu[4] = { 0 };
    static const char *names[4] = { "plain", "filter", "smooth2d", "both" };
    char what[160];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--require-gpu") == 0) {
            require_gpu = 1;
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
        }
    }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("note: SDL video init failed (%s), GPU part needs it\n", SDL_GetError());
    }
    PsxHd_SetThreads(2);
    {
        /* warm-up: the very first picture after the surface is made differs (the surface starts
         * from the VRAM before the scene); every later one is the same */
        uint32_t *a, *b;

        free(render(0, 0, 0, scale, &w, &h));
        a = render(0, 0, 0, scale, &w, &h);
        b = render(0, 0, 0, scale, &w, &h);
        check(a != NULL && b != NULL && diff_count(a, b, w, scale, 0, 0, W, H, 0) == 0, "soft: repeatable picture");
        free(a);
        free(b);
    }
    for (i = 0; i < 4; i++) {
        soft[i] = render(0, i & 1, i >> 1, scale, &w, &h);
        if (soft[i] == NULL) {
            printf("FAIL soft render %s\n", names[i]);
            return 1;
        }
        snprintf(what, sizeof(what), "soft_%s", names[i]);
        write_bmp(what, soft[i], w, h);
    }
    /* soft: what each option touches (3D quads: 1x x 0..160 / 160..300, y 0..135; sprites:
     * x 178..285, y 148..213) */
    check(diff_count(soft[0], soft[1], w, scale, 10, 10, 300, 135, 4) > 1000, "soft: filter changes the 3D quads");
    check(diff_count(soft[0], soft[1], w, scale, 178, 148, 285, 213, 0) == 0, "soft: filter leaves the sprites");
    check(diff_count(soft[0], soft[2], w, scale, 178, 148, 285, 213, 4) > 100, "soft: 2D smoothing changes the sprites");
    check(diff_count(soft[0], soft[2], w, scale, 10, 10, 300, 135, 0) == 0, "soft: 2D smoothing leaves the 3D quads");

    texture_pack_tests(scale, soft[0], w);

    dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
    if (dev != NULL && PsxHw_Init(dev)) {
        have_gpu = 1;
        printf("GPU device: %s\n", SDL_GetGPUDeviceDriver(dev));
    } else {
        printf("%s GPU: no Vulkan device (%s)\n", require_gpu ? "FAIL" : "SKIP", SDL_GetError());
        if (require_gpu) {
            failures++;
        }
    }
    if (have_gpu) {
        free(render(1, 0, 0, scale, &w2, &h2)); /* warm-up as above */
        for (i = 0; i < 4; i++) {
            int n, total;

            gpu[i] = render(1, i & 1, i >> 1, scale, &w2, &h2);
            snprintf(what, sizeof(what), "gpu: %s picture read back", names[i]);
            check(gpu[i] != NULL && w2 == w && h2 == h, what);
            if (gpu[i] == NULL || w2 != w || h2 != h) {
                continue;
            }
            snprintf(what, sizeof(what), "gpu_%s", names[i]);
            write_bmp(what, gpu[i], w, h);
            n = diff_count(soft[i], gpu[i], w, scale, 0, 0, W, H, 24);
            total = w * h;
            snprintf(what, sizeof(what), "gpu = soft (%s): %d of %d pixels differ by more than 24 (%.2f%%)", names[i], n,
                     total, 100.0 * n / total);
            check(n * 100 < total * 2, what);
        }
        if (gpu[0] && gpu[1] && gpu[2]) {
            check(diff_count(gpu[0], gpu[1], w, scale, 10, 10, 300, 135, 4) > 1000, "gpu: filter changes the 3D quads");
            check(diff_count(gpu[0], gpu[1], w, scale, 178, 148, 285, 213, 0) == 0, "gpu: filter leaves the sprites");
            check(diff_count(gpu[0], gpu[2], w, scale, 178, 148, 285, 213, 4) > 100, "gpu: 2D smoothing changes the sprites");
            check(diff_count(gpu[0], gpu[2], w, scale, 10, 10, 300, 135, 0) == 0, "gpu: 2D smoothing leaves the 3D quads");
        }
        PsxHd_SetGpu(0);
        PsxHw_Shutdown();
        SDL_DestroyGPUDevice(dev);
    }
    printf("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}
