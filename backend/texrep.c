/* dw2-pc-port: external replacement textures (see texrep.h). */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "backend/psxgpu.h"
#include "backend/texrep.h"

#define MAX_LEVELS 5 /* 16x, 8x, 4x, 2x, 1x */

struct TexRepImg {
    int w, h;
    uint32_t *px; /* 0xAARRGGBB, or with idx8 one palette index per byte (4 per word, low byte first) */
    int idx8;     /* runtime tiles: palette indices, coloured where drawn through the primitive's CLUT */
    /* dw2-pc-port: halved copies for a power-of-two factor (level 0 = px), averaged with alpha:
       each output pixel reads the level that matches the HD renderer's scale */
    int nlev;
    struct {
        int w, h;
        uint32_t *px;
    } lev[MAX_LEVELS];
};

typedef struct {
    char name[64]; /* file name, lower case, without ".png" */
    char *path;
    TexRepImg *img;      /* set by the loader once the picture is complete */
    SDL_AtomicInt state; /* 0 not asked for, 1 queued for the loader, 2 done (img or failed) */
    int bw, bh;          /* the texture size it must be a multiple of (set when queued) */
} Entry;

typedef struct {
    int x, y, w, h;
    uint64_t hash;
    int src; /* index source (TexRep_AddIndexSource) with these pixels, or -1 */
    /* index sources: parts written over since (the game rewrites a few texels of a page every
       frame): primitives touching them draw the original, the rest keeps its runtime picture */
    int ndirty;
    struct {
        int x, y, w, h;
    } dirty[8];
} Upload;

#define MAX_ENTRIES 65536
#define MAP_SIZE (MAX_ENTRIES * 2)
#define MAX_UPLOADS 1024
#define MAX_PACKS 64
#define MAX_DUMPED 32768
#define MEM_LIMIT ((size_t)1 << 30) /* 1 GiB of replacement pixels */

static Entry *entries;
static int nentries;
static int *map; /* name hash -> entry index + 1 */
static uint64_t *hashes; /* the upload hashes some file names (sorted, for bsearch) */
static int nhashes, hashes_sorted;
static Upload uploads[MAX_UPLOADS];
static int nuploads;
static char *packs[MAX_PACKS];
static int npacks;
static char *dump_dir;
static uint64_t *dumped;
static int ndumped;
static int disabled;
/* the loader thread and (tests, no thread) the game thread both count: atomic */
static SDL_AtomicInt mem_kib; /* replacement pixels held, in KiB */
static SDL_AtomicInt a_loaded, a_failed;
static int stat_uses;
#define mem_bytes() ((size_t)SDL_GetAtomicInt(&mem_kib) << 10)
#define mem_add(n) SDL_AddAtomicInt(&mem_kib, (int)(((size_t)(n) + 1023) >> 10))
#define stat_failed_inc() SDL_AddAtomicInt(&a_failed, 1)
static FILE *logf_;

/* [textures] lines: console and, when set, the log file */
static void tlog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[textures] ");
    vprintf(fmt, ap);
    va_end(ap);
    if (logf_ != NULL) {
        va_start(ap, fmt);
        vfprintf(logf_, fmt, ap);
        va_end(ap);
        fflush(logf_);
    }
}

void TexRep_SetLog(const char *path) {
    if (logf_ == NULL && path != NULL) {
        logf_ = fopen(path, "w");
    }
}

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

static uint32_t name_hash(const char *s) {
    return (uint32_t)fnv(0xCBF29CE484222325ull, s, strlen(s));
}

static int find_entry(const char *name) {
    uint32_t h;

    if (map == NULL) return -1;
    for (h = name_hash(name) % MAP_SIZE; map[h] != 0; h = (h + 1) % MAP_SIZE) {
        if (strcmp(entries[map[h] - 1].name, name) == 0) return map[h] - 1;
    }
    return -1;
}

static void add_entry(const char *name, const char *path) {
    int i = find_entry(name);
    uint32_t h;
    unsigned long long hv;

    if (i >= 0) { /* a later pack wins */
        SDL_free(entries[i].path);
        entries[i].path = SDL_strdup(path);
        return;
    }
    if (nentries >= MAX_ENTRIES) {
        tlog("more than %d files, %s ignored\n", MAX_ENTRIES, path);
        return;
    }
    i = nentries++;
    SDL_strlcpy(entries[i].name, name, sizeof(entries[i].name));
    entries[i].path = SDL_strdup(path);
    for (h = name_hash(name) % MAP_SIZE; map[h] != 0; h = (h + 1) % MAP_SIZE) {
    }
    map[h] = i + 1;
    if (sscanf(name, "%16llx_", &hv) == 1) {
        hashes[nhashes++] = (uint64_t)hv;
        hashes_sorted = 0;
    }
}

static int name_ok(const char *n) {
    /* <16 hex>_<w>x<h>[_m<d>[_c<8 hex>]] */
    unsigned long long hv;
    int w, h, used = 0;
    if (strlen(n) < 20 || sscanf(n, "%16llx_%dx%d%n", &hv, &w, &h, &used) != 3 || w <= 0 || h <= 0) return 0;
    n += used;
    if (*n == 0) return 1;
    if (n[0] != '_' || n[1] != 'm' || n[2] < '0' || n[2] > '1') return 0;
    n += 3;
    if (*n == 0) return 1;
    return strlen(n) == 10 && n[0] == '_' && n[1] == 'c' && strspn(n + 2, "0123456789abcdef") == 8;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void alloc_tables(void) {
    if (entries == NULL) {
        entries = calloc(MAX_ENTRIES, sizeof(*entries));
        map = calloc(MAP_SIZE, sizeof(*map));
        hashes = calloc(MAX_ENTRIES, sizeof(*hashes));
    }
}

void TexRep_AddRoot(const char *root) {
    char **dirs;
    int ndirs = 0;

    if (disabled || root == NULL) return;
    dirs = SDL_GlobDirectory(root, "*", 0, &ndirs);
    if (dirs == NULL) return;
    alloc_tables();
    if (entries == NULL || map == NULL || hashes == NULL) {
        SDL_free(dirs);
        return;
    }
    qsort(dirs, (size_t)ndirs, sizeof(*dirs), cmp_str); /* packs in name order: the later wins */
    for (int d = 0; d < ndirs; d++) {
        char *dir = NULL;
        char **files;
        int nfiles = 0, used = 0, bad = 0;
        SDL_PathInfo info;

        if (dirs[d][0] == '_' || dirs[d][0] == '.' || strchr(dirs[d], '/') != NULL) continue;
        SDL_asprintf(&dir, "%s/%s", root, dirs[d]);
        if (dir == NULL || !SDL_GetPathInfo(dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
            SDL_free(dir);
            continue;
        }
        files = SDL_GlobDirectory(dir, "*.png", SDL_GLOB_CASEINSENSITIVE, &nfiles);
        for (int f = 0; files != NULL && f < nfiles; f++) {
            char name[64], *path = NULL;
            size_t len = strlen(files[f]);

            if (len < 5 || len - 4 >= sizeof(name) || strchr(files[f], '/') != NULL) continue;
            memcpy(name, files[f], len - 4);
            name[len - 4] = 0;
            for (char *c = name; *c; c++) *c = (char)SDL_tolower((unsigned char)*c);
            if (!name_ok(name)) {
                tlog("%s/%s: not a texture id name (hash_WxH[_mN[_cCLUT]].png), skipped\n", dir, files[f]);
                bad++;
                continue;
            }
            SDL_asprintf(&path, "%s/%s", dir, files[f]);
            if (path != NULL) {
                add_entry(name, path);
                used++;
                SDL_free(path);
            }
        }
        SDL_free(files);
        if (npacks < MAX_PACKS) packs[npacks++] = SDL_strdup(dirs[d]);
        tlog("pack %s: %d textures%s\n", dir, used, bad ? " (some names skipped)" : "");
        SDL_free(dir);
    }
    SDL_free(dirs);
}

void TexRep_SetDump(const char *dir) {
    if (disabled) return;
    dump_dir = SDL_strdup(dir);
    SDL_CreateDirectory(dir);
    dumped = calloc(MAX_DUMPED, sizeof(*dumped));
}

void TexRep_Disable(void) {
    disabled = 1;
}

void TexRep_Enable(void) { /* tests */
    disabled = 0;
}

int TexRep_Count(void) {
    return nentries;
}

int TexRep_PackCount(void) {
    return npacks;
}

const char *TexRep_PackName(int i) {
    return i >= 0 && i < npacks ? packs[i] : "";
}

static int nsrcs;
static int out_scale = 1; /* the HD renderer's scale (TexRep_SetScale) */
static int prim_mag = 1;  /* TexRep_SetMagnify */
void TexRep_SetMagnify(int m) {
    prim_mag = m < 1 ? 1 : m > 4 ? 4 : m;
}

int TexRep_Active(void) {
    return !disabled && (nentries > 0 || dump_dir != NULL || nsrcs > 0);
}

void TexRep_Stats(int *loaded, int *failed, int *uses) {
    *loaded = SDL_GetAtomicInt(&a_loaded);
    *failed = SDL_GetAtomicInt(&a_failed);
    *uses = stat_uses;
}

static int has_hash(uint64_t h) {
    if (nhashes == 0) return 0;
    if (!hashes_sorted) {
        qsort(hashes, (size_t)nhashes, sizeof(*hashes), cmp_u64);
        hashes_sorted = 1;
    }
    return bsearch(&h, hashes, (size_t)nhashes, sizeof(*hashes), cmp_u64) != NULL;
}

/* ---- index sources (see texrep.h) ---- */
#define MAX_SRCS 256
#define IDX_TABLE 32768
#define IDX_MEM_LIMIT ((size_t)256 << 20)
typedef struct {
    uint64_t hash;
    int w, h, mode, tw, th;
    TexRepIndexFn fn;
    void *ctx;
    const uint8_t *cls;   /* Modern UI panels (TexRep_SetSourceStyle) or NULL */
    const uint16_t *cid;  /* per texel: its panel */
    const uint16_t *comp; /* per panel: top, bottom texel row */
    int ncomp;
    float *dist; /* per texel: distance (texels) from its centre to the panel outline, inside > 0 */
    const uint16_t *words; /* the pixels as registered (the upload's original content) */
    TexRepIndexFn ofn; /* TexRep_AddIndexOverlay */
    void *octx;
} IdxSrc;
typedef struct {
    uint64_t src; /* the source's pixel hash: tiles stay valid when the sources are registered again */
    int ox, oy, rw, rh, k;
    uint32_t clut;
    uint32_t used; /* the picture it was last drawn in (perf) */
    TexRepImg *img;
} IdxTile;
static uint32_t perf_pic = 1;
void TexRep_PerfPicture(void) {
    perf_pic++;
}
TexRepPerf texrep_perf;
static IdxSrc srcs[MAX_SRCS];
static int src_of(uint64_t hv, int w, int h);
static IdxTile *idx_tab;
static int idx_tiles;
static size_t idx_bytes;
static int idx_full_logged;
static int idx_reset_wanted;

void TexRep_AddIndexSource(const uint16_t *words, int w, int h, int mode, int tw, int th, TexRepIndexFn fn, void *ctx) {
    int dims[2] = { w, h };
    uint64_t hv;

    if (words == NULL || w <= 0 || h <= 0 || fn == NULL || mode < 0 || mode > 1 || nsrcs == MAX_SRCS) return;
    hv = fnv(0xCBF29CE484222325ull, dims, sizeof(dims));
    hv = fnv(hv, words, (size_t)w * (size_t)h * 2);
    for (int i = 0; i < nsrcs; i++) {
        if (srcs[i].hash == hv && srcs[i].w == w && srcs[i].h == h) {
            srcs[i].fn = fn; /* the same pixels again (language switch): the newer drawing */
            srcs[i].ctx = ctx;
            srcs[i].words = words;
            return;
        }
    }
    srcs[nsrcs].hash = hv;
    srcs[nsrcs].w = w;
    srcs[nsrcs].h = h;
    srcs[nsrcs].mode = mode;
    srcs[nsrcs].tw = tw > 0 ? tw : w * (mode == 0 ? 4 : 2);
    srcs[nsrcs].th = th > 0 ? th : h;
    srcs[nsrcs].fn = fn;
    srcs[nsrcs].ctx = ctx;
    srcs[nsrcs].cls = NULL;
    srcs[nsrcs].cid = NULL;
    srcs[nsrcs].comp = NULL;
    srcs[nsrcs].ncomp = 0;
    srcs[nsrcs].dist = NULL;
    srcs[nsrcs].ofn = NULL;
    srcs[nsrcs].octx = NULL;
    srcs[nsrcs].words = words;
    nsrcs++;
}

typedef struct {
    const uint16_t *words;
    int w, h, mode;
} PlainSrc;
static PlainSrc plain_srcs[64];
static int nplain;

static void plain_fn(void *ctx, int k, int x0, int y0, int w, int h, uint8_t *idx) {
    const PlainSrc *p = ctx;
    const uint8_t *b = (const uint8_t *)p->words;
    for (int y = 0; y < h * k; y++)
        for (int x = 0; x < w * k; x++) {
            int X = x0 + x / k, Y = y0 + y / k, v;
            v = p->mode == 0 ? (b[Y * p->w * 2 + X / 2] >> ((X & 1) * 4)) & 15 : b[Y * p->w * 2 + X];
            idx[y * w * k + x] = (uint8_t)v;
        }
}

static uint64_t src_hash(const uint16_t *words, int w, int h) {
    int dims[2] = { w, h };
    uint64_t hv = fnv(0xCBF29CE484222325ull, dims, sizeof(dims));
    return fnv(hv, words, (size_t)w * (size_t)h * 2);
}

static int src_gen = 1;
int TexRep_SourceGeneration(void) {
    return src_gen;
}

int TexRep_RuntimeScale(void) {
    return out_scale;
}

void TexRep_RefreshSource(const uint16_t *words, int w, int h) {
    uint64_t hv = src_hash(words, w, h);
    if (idx_tab == NULL) return;
    for (int i = 0; i < IDX_TABLE; i++) {
        /* a tombstone: the slot stays taken (probe chains), its picture stays allocated until the
           next TexRep_DropTiles (the GPU pool may still hold its pixels) */
        if (idx_tab[i].img != NULL && idx_tab[i].src == hv) idx_tab[i].src = 0;
    }
}

void TexRep_AddIndexOverlay(const uint16_t *words, int w, int h, int mode, TexRepIndexFn fn, void *ctx) {
    uint64_t hv;
    int si;

    if (words == NULL || w <= 0 || h <= 0 || mode < 0 || mode > 1) return;
    hv = src_hash(words, w, h);
    si = src_of(hv, w, h);
    if (si < 0 && nplain < 64) {
        PlainSrc *p = &plain_srcs[nplain++];
        p->words = words;
        p->w = w;
        p->h = h;
        p->mode = mode;
        TexRep_AddIndexSource(words, w, h, mode, 16, 16, plain_fn, p);
        si = src_of(hv, w, h);
    }
    if (si < 0) return;
    srcs[si].ofn = fn;
    srcs[si].octx = ctx;
    TexRep_RefreshSource(words, w, h);
}

/* the sheet is already in VRAM at (vx, vy) (uploaded before its overlay existed: OnUpload only
   records uploads that have a replacement): recorded when VRAM there still holds its pixels */
void TexRep_NoteUploadAt(const uint16_t *words, int w, int h, int vx, int vy) {
    uint64_t hv = src_hash(words, w, h);
    int si = src_of(hv, w, h);

    if (si < 0 || vx < 0 || vy < 0 || vx + w > PSXGPU_VRAM_W || vy + h > PSXGPU_VRAM_H) return;
    for (int i = 0; i < nuploads; i++)
        if (uploads[i].x == vx && uploads[i].y == vy && uploads[i].w == w && uploads[i].h == h) {
            uploads[i].src = si;
            return;
        }
    for (int y = 0; y < h; y++)
        if (memcmp(&PsxGpu_Vram[vy + y][vx], words + (size_t)y * w, (size_t)w * 2) != 0) return;
    if (nuploads == MAX_UPLOADS) {
        memmove(uploads, uploads + 1, sizeof(uploads[0]) * (MAX_UPLOADS - 1));
        nuploads--;
    }
    uploads[nuploads].x = vx;
    uploads[nuploads].y = vy;
    uploads[nuploads].w = w;
    uploads[nuploads].h = h;
    uploads[nuploads].hash = hv;
    uploads[nuploads].src = si;
    uploads[nuploads].ndirty = 0;
    nuploads++;
}

/* the 1x texels of a source as plain indices, k times larger (sources without a drawing of
   their own: a sheet that only gets the panel style) */
void TexRep_SetSourceStyle(const uint16_t *words, int w, int h, const uint8_t *cls, const uint16_t *cid,
                           const uint16_t *comp, int ncomp) {
    int dims[2] = { w, h };
    uint64_t hv;

    if (words == NULL) return;
    hv = fnv(0xCBF29CE484222325ull, dims, sizeof(dims));
    hv = fnv(hv, words, (size_t)w * (size_t)h * 2);
    for (int i = 0; i < nsrcs; i++) {
        if (srcs[i].hash == hv && srcs[i].w == w && srcs[i].h == h) {
            srcs[i].cls = cls;
            srcs[i].cid = cid;
            srcs[i].comp = comp;
            srcs[i].ncomp = ncomp;
            free(srcs[i].dist);
            srcs[i].dist = NULL;
            if (cls != NULL) {
                /* the distance field: to the nearest texel outside every panel, at most 4 texels */
                int tpw = srcs[i].mode == 0 ? 4 : 2, W = w * tpw, H = h;
                float *d = malloc(sizeof(float) * (size_t)W * H);
                if (d != NULL) {
                    for (int y = 0; y < H; y++)
                        for (int x = 0; x < W; x++) {
                            float best = 4.0f;
                            if (cls[y * W + x] == 0) {
                                d[y * W + x] = -0.5f;
                                continue;
                            }
                            for (int dy = -4; dy <= 4; dy++)
                                for (int dx = -4; dx <= 4; dx++) {
                                    int X = x + dx, Y = y + dy;
                                    if (X < 0 || Y < 0 || X >= W || Y >= H) continue; /* the sheet's edge: an open end */
                                    if (cls[Y * W + X] != 0) continue;
                                    float e = sqrtf((float)(dx * dx + dy * dy)) - 0.5f;
                                    best = e < best ? e : best;
                                }
                            d[y * W + x] = best;
                        }
                }
                srcs[i].dist = d;
            }
            return;
        }
    }
}

/* ---- Modern UI panel style (design: the port's own; blue / cyan accents, after the reference
   "partner status" screen): computed per output pixel from the 1x panel shape ---- */
static float panel_dist(const IdxSrc *s, float x, float y) {
    /* the distance field bilinearly between texel centres: 0 is the smoothed outline */
    int tpw = s->mode == 0 ? 4 : 2, W = s->w * tpw, H = s->h;
    float fx = x - 0.5f, fy = y - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0, v[4];
    for (int j = 0; j < 4; j++) {
        int X = x0 + (j & 1), Y = y0 + (j >> 1);
        X = X < 0 ? 0 : X >= W ? W - 1 : X; /* outside the sheet: as the nearest texel (open end) */
        Y = Y < 0 ? 0 : Y >= H ? H - 1 : Y;
        v[j] = s->dist[Y * W + X];
    }
    return (v[0] * (1 - ax) + v[1] * ax) * (1 - ay) + (v[2] * (1 - ax) + v[3] * ax) * ay;
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, float t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    uint32_t r = (uint32_t)(((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t);
    uint32_t g = (uint32_t)(((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t);
    uint32_t bl = (uint32_t)((a & 255) * (1 - t) + (b & 255) * t);
    return 0xFF000000u | r << 16 | g << 8 | bl;
}

static void style_panels(const IdxSrc *s, int k, int ox, int oy, int rw, int rh, uint32_t *px) {
    int tpw = s->mode == 0 ? 4 : 2, W = s->w * tpw;
    /* edge cyan, a darker blue inside it, the fill a dark blue gradient; old frame lines and
       labels (grey / white on the panel) become light cyan lines */
    const uint32_t edge = 0xFF46D2FFu, inner = 0xFF1A6FA8u, fill_top = 0xFF10305Eu, fill_bot = 0xFF081A38u,
                   line = 0xFF5CC8F0u;
    float px1 = 1.0f / k; /* one output pixel in texels */
    if (s->dist == NULL) return;
    for (int Y = 0; Y < rh * k; Y++) {
        for (int X = 0; X < rw * k; X++) {
            int tx = ox + X / k, ty = oy + Y / k;
            uint8_t c = s->cls[ty * W + tx];
            if (c == 0) continue; /* not a panel: as drawn */
            float x = ox + (X + 0.5f) / k, y = oy + (Y + 0.5f) / k;
            float d = panel_dist(s, x, y);
            uint32_t *p = &px[(size_t)Y * rw * k + X];
            if (d < 0.0f) { /* outside the smoothed outline */
                *p = 0;
                continue;
            }
            int n = s->cid[ty * W + tx];
            float t = 0.5f;
            if (n < s->ncomp && s->comp[2 * n + 1] > s->comp[2 * n])
                t = (y - s->comp[2 * n]) / (float)(s->comp[2 * n + 1] + 1 - s->comp[2 * n]);
            uint32_t base = mix_rgb(fill_top, fill_bot, t);
            if (c == 2) {
                uint32_t o = *p;
                if ((o >> 24) == 0) continue;
                int r = (o >> 16) & 255, g = (o >> 8) & 255, b = o & 255;
                int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
                int lum = (r * 3 + g * 6 + b) / 10;
                if (mx - mn < 48 && lum > 110) {
                    *p = line; /* old frame lines, labels */
                    continue;
                }
                if (!(b > r + 30 && b >= g && lum < 150)) continue; /* bars, slots, icons: as drawn */
                /* the old blue shading: fill */
            }
            /* the edge: 0.35 texel and at least 1.5 output pixels, then 0.35 texel of inner blue */
            float e = 0.35f > 1.5f * px1 ? 0.35f : 1.5f * px1;
            if (d < e) {
                *p = mix_rgb(edge, inner, d / e);
            } else if (d < e + 0.35f) {
                *p = mix_rgb(inner, base, (d - e) / 0.35f);
            } else {
                *p = base;
            }
        }
    }
}

void TexRep_AddIndexSourceIfNew(const uint16_t *words, int w, int h, int mode, int tw, int th, TexRepIndexFn fn, void *ctx) {
    int dims[2] = { w, h };
    uint64_t hv;

    if (words == NULL || w <= 0 || h <= 0) return;
    hv = fnv(0xCBF29CE484222325ull, dims, sizeof(dims));
    hv = fnv(hv, words, (size_t)w * (size_t)h * 2);
    if (src_of(hv, w, h) >= 0) return;
    TexRep_AddIndexSource(words, w, h, mode, tw, th, fn, ctx);
}

/* Sources only: the tiles already made stay (a primitive of the current frame may use one). */
void TexRep_ClearIndexSources(void) {
    idx_reset_wanted = idx_tiles > 0; /* tiles of the old drawing or style go after the next picture */
    nsrcs = 0;
    nplain = 0;
    src_gen++;
    for (int i = 0; i < nuploads; i++) uploads[i].src = -1;
}

int TexRep_TileResetWanted(void) {
    return idx_reset_wanted;
}

/* every runtime tile freed: only when no queued primitive uses one (psxgpu_hd.c, after a flush) */
void TexRep_DropTiles(void) {
    if (idx_tiles > 0) texrep_perf.drops++;
    if (idx_tab != NULL) {
        for (int i = 0; i < IDX_TABLE; i++) {
            if (idx_tab[i].img != NULL) {
                free(idx_tab[i].img->px);
                free(idx_tab[i].img);
            }
        }
        memset(idx_tab, 0, IDX_TABLE * sizeof(*idx_tab));
    }
    idx_tiles = 0;
    idx_bytes = 0;
    idx_reset_wanted = 0;
}

void TexRep_IndexStats(int *sources, int *tiles, int *kib) {
    *sources = nsrcs;
    *tiles = idx_tiles;
    *kib = (int)(idx_bytes >> 10);
}

static int src_of(uint64_t hv, int w, int h) {
    for (int i = 0; i < nsrcs; i++) {
        if (srcs[i].hash == hv && srcs[i].w == w && srcs[i].h == h) return i;
    }
    return -1;
}

static uint32_t argb_of(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return 0xFF000000u | (r << 19 | (r >> 2) << 16) | (g << 11 | (g >> 2) << 8) | (b << 3 | b >> 2);
}

/* The tile of source si covering texels [ox, ox+rw) x [oy, oy+rh) at k. A tile holds palette
 * indices (1 byte per pixel): the primitive's CLUT colours it where it is drawn (TexRep_Sample, the
 * GPU's rep_sample), so palette changes and cycling (battle banners, the title's pulsing text, fades)
 * use the same tile and the GPU keeps it: before, every palette state was a full colour tile of its
 * own (made, stored and uploaded again for each state). A source with the Modern UI panel style is
 * coloured here (the style's colours are not the palette's): those tiles are per palette (pal, clut). */
static TexRepImg *idx_tile(int si, int ox, int oy, int rw, int rh, int k, const uint16_t *pal, uint32_t clut) {
    uint64_t sh = srcs[si].hash;
    int styled = srcs[si].cls != NULL;
    uint32_t h;
    int key[5] = { ox, oy, rw, rh, k };
    size_t n = (size_t)rw * k * (size_t)rh * k, bytes = styled ? n * 4 : (n + 3) & ~(size_t)3;
    uint8_t *idx;
    TexRepImg *img;

    if (!styled) clut = 0;
    h = (uint32_t)fnv(fnv(0xCBF29CE484222325ull, &sh, sizeof(sh)), &clut, sizeof(clut));
    h = (uint32_t)fnv(h, key, sizeof(key)) % IDX_TABLE;
    if (idx_tab == NULL) {
        idx_tab = calloc(IDX_TABLE, sizeof(*idx_tab));
        if (idx_tab == NULL) return NULL;
    }
    for (int probe = 0; probe < IDX_TABLE; probe++, h = (h + 1) % IDX_TABLE) {
        IdxTile *t = &idx_tab[h];
        if (t->img == NULL) break;
        if (t->src == sh && t->ox == ox && t->oy == oy && t->rw == rw && t->rh == rh && t->k == k && t->clut == clut) {
            texrep_perf.idx_hits++;
            if (t->used != perf_pic) {
                t->used = perf_pic;
                texrep_perf.used_kib += (uint64_t)t->img->w * t->img->h * (t->img->idx8 ? 1 : 4) >> 10;
            }
            texrep_perf.mag_hist[prim_mag]++;
            return t->img;
        }
    }
    if (idx_tab[h].img != NULL || idx_tiles >= IDX_TABLE / 2 || idx_bytes + bytes + (styled ? n : 0) > IDX_MEM_LIMIT) {
        /* full: this primitive draws the game's texture; the tiles are dropped after the picture
           (PsxHd_TrimRuntimeTiles at the frame's end) and made again when drawn */
        if (!idx_reset_wanted && idx_full_logged < 3) {
            idx_full_logged++;
            tlog("runtime fonts: tile memory full (%d tiles, %zu MiB), starting over after this picture\n", idx_tiles,
                 idx_bytes >> 20);
        }
        idx_reset_wanted = 1;
        texrep_perf.idx_full++;
        return NULL;
    }
    img = calloc(1, sizeof(*img));
    uint32_t *px = malloc(bytes);
    idx = styled ? malloc(n) : (uint8_t *)px;
    if (idx == NULL || img == NULL || px == NULL) {
        if (styled) free(idx);
        free(img);
        free(px);
        return NULL;
    }
    memset(idx, 0, styled ? n : bytes);
    Uint64 t0 = SDL_GetTicksNS();
    srcs[si].fn(srcs[si].ctx, k, ox, oy, rw, rh, idx);
    if (srcs[si].ofn != NULL) srcs[si].ofn(srcs[si].octx, k, ox, oy, rw, rh, idx);
    Uint64 t1 = SDL_GetTicksNS();
    if (styled) {
        for (size_t i = 0; i < n; i++) {
            uint16_t c = pal[idx[i] & (srcs[si].mode == 0 ? 15 : 255)];
            px[i] = idx[i] == 0 || c == 0 ? 0u : argb_of(c); /* index 0 and colour 0: transparent, as on the PS1 */
        }
        free(idx);
    } else if (srcs[si].mode == 0) {
        for (size_t i = 0; i < n; i++) idx[i] &= 15;
    }
    Uint64 t2 = SDL_GetTicksNS();
    if (styled) style_panels(&srcs[si], k, ox, oy, rw, rh, px);
    Uint64 t3 = SDL_GetTicksNS();
    texrep_perf.raster_ns += t1 - t0;
    texrep_perf.conv_ns += t2 - t1;
    texrep_perf.style_ns += t3 - t2;
    texrep_perf.idx_new++;
    texrep_perf.idx_new_px += n;
    img->w = rw * k;
    img->h = rh * k;
    img->px = px;
    img->idx8 = !styled;
    img->nlev = 1;
    img->lev[0].w = img->w;
    img->lev[0].h = img->h;
    img->lev[0].px = px;
    idx_tab[h].src = sh;
    idx_tab[h].ox = ox;
    idx_tab[h].oy = oy;
    idx_tab[h].rw = rw;
    idx_tab[h].rh = rh;
    idx_tab[h].k = k;
    idx_tab[h].clut = clut;
    idx_tab[h].img = img;
    idx_tab[h].used = perf_pic;
    texrep_perf.used_kib += bytes >> 10;
    texrep_perf.mag_hist[prim_mag]++;
    if (getenv("DW2_PERF_TILES") != NULL)
        printf("[perf] tile src %d (%dx%d tiles %dx%d) at %d,%d %dx%d k %d mag %d: %zu kpx\n", si, srcs[si].w, srcs[si].h,
               srcs[si].tw, srcs[si].th, ox, oy, rw, rh, k, prim_mag, n / 1000);
    idx_tiles++;
    idx_bytes += bytes;
    return img;
}

static int overlaps(const Upload *u, int x, int y, int w, int h) {
    return x < u->x + u->w && u->x < x + w && y < u->y + u->h && u->y < y + h;
}

void TexRep_OnWrite(int x, int y, int w, int h) {
    for (int i = 0; i < nuploads;) {
        Upload *u = &uploads[i];
        if (overlaps(u, x, y, w, h)) {
            int whole = x <= u->x && y <= u->y && x + w >= u->x + u->w && y + h >= u->y + u->h, known = 0;
            if (u->src >= 0 && !whole) {
                for (int d = 0; d < u->ndirty && !known; d++)
                    known = u->dirty[d].x <= x && u->dirty[d].y <= y && u->dirty[d].x + u->dirty[d].w >= x + w &&
                            u->dirty[d].y + u->dirty[d].h >= y + h;
                if (known) {
                    i++;
                    continue;
                }
                if (u->ndirty < 8) {
                    u->dirty[u->ndirty].x = x;
                    u->dirty[u->ndirty].y = y;
                    u->dirty[u->ndirty].w = w;
                    u->dirty[u->ndirty].h = h;
                    u->ndirty++;
                    i++;
                    continue;
                }
            }
            uploads[i] = uploads[--nuploads];
        } else {
            i++;
        }
    }
}

void TexRep_OnUpload(int x, int y, int w, int h, const uint16_t *src) {
    uint64_t hv;
    int dims[2] = { w, h };

    if (!TexRep_Active() || w <= 0 || h <= 0) return;
    TexRep_OnWrite(x, y, w, h);
    if (x + w > PSXGPU_VRAM_W || y + h > PSXGPU_VRAM_H) return; /* wrapping uploads: not tracked */
    hv = fnv(0xCBF29CE484222325ull, dims, sizeof(dims));
    hv = fnv(hv, src, (size_t)w * (size_t)h * 2);
    int si = src_of(hv, w, h);
    if (dump_dir == NULL && si < 0 && !has_hash(hv)) return;
    if (nuploads == MAX_UPLOADS) {
        memmove(uploads, uploads + 1, sizeof(uploads[0]) * (MAX_UPLOADS - 1)); /* drop the oldest */
        nuploads--;
    }
    uploads[nuploads].x = x;
    uploads[nuploads].y = y;
    uploads[nuploads].w = w;
    uploads[nuploads].h = h;
    uploads[nuploads].hash = hv;
    uploads[nuploads].src = si;
    uploads[nuploads].ndirty = 0;
    nuploads++;
}

/* dw2-pc-port: PNGs are decoded on a loader thread. A large pack (16x textures of 8 MB and more)
 * took seconds on the game thread at a texture's first use; now the game draws the original
 * texture until the replacement is ready (a few frames) and never waits. Tests load at once. */
#define LOAD_QUEUE 4096
static SDL_Thread *loader;
static SDL_Mutex *load_lock;
static SDL_Condition *load_cond;
static int load_queue[LOAD_QUEUE], load_head, load_count;
static int load_sync;
static int load_quit; /* under load_lock */
static SDL_AtomicInt load_pending;

void TexRep_SetSyncLoad(int on) {
    load_sync = on != 0;
}

int TexRep_Pending(void) {
    return SDL_GetAtomicInt(&load_pending);
}

static TexRepImg *decode_png(Entry *e, int bw, int bh);

static int SDLCALL loader_main(void *unused) {
    (void)unused;
    for (;;) {
        int i;
        SDL_LockMutex(load_lock);
        while (load_count == 0 && !load_quit) {
            SDL_WaitCondition(load_cond, load_lock);
        }
        if (load_quit) {
            SDL_UnlockMutex(load_lock);
            break;
        }
        i = load_queue[load_head];
        load_head = (load_head + 1) % LOAD_QUEUE;
        load_count--;
        SDL_UnlockMutex(load_lock);
        {
            TexRepImg *img = decode_png(&entries[i], entries[i].bw, entries[i].bh);
            SDL_SetAtomicPointer((void **)&entries[i].img, img);
            SDL_SetAtomicInt(&entries[i].state, 2);
            SDL_AddAtomicInt(&load_pending, -1);
        }
    }
    return 0;
}

/* Ends the loader thread (the program is closing): no new PNG is started, the one being
 * decoded is finished, the thread is joined. Pictures already decoded stay until the process ends;
 * the game thread may still sample them. Afterwards load_png only answers from what is ready. */
void TexRep_Shutdown(void) {
    if (loader == NULL) return;
    SDL_LockMutex(load_lock);
    load_quit = 1;
    SDL_SignalCondition(load_cond);
    SDL_UnlockMutex(load_lock);
    SDL_WaitThread(loader, NULL);
    loader = NULL;
}

static TexRepImg *load_png(Entry *e, int bw, int bh, int *k) {
    int st = SDL_GetAtomicInt(&e->state);
    TexRepImg *img;

    if (st == 2) {
        img = (TexRepImg *)SDL_GetAtomicPointer((void **)&e->img);
        if (img != NULL) *k = img->w / bw;
        return img;
    }
    if (st == 1) {
        return NULL; /* still loading: the original this frame */
    }
    if (load_quit) return NULL; /* closing */
    e->bw = bw;
    e->bh = bh;
    if (load_sync || (loader == NULL && (load_lock = SDL_CreateMutex()) != NULL &&
                      (load_cond = SDL_CreateCondition()) != NULL &&
                      (loader = SDL_CreateThread(loader_main, "texrep", NULL)) == NULL)) {
        img = decode_png(e, bw, bh); /* tests, or no thread: at once */
        e->img = img;
        SDL_SetAtomicInt(&e->state, 2);
        if (img != NULL) *k = img->w / bw;
        return img;
    }
    SDL_LockMutex(load_lock);
    if (load_count < LOAD_QUEUE) {
        load_queue[(load_head + load_count) % LOAD_QUEUE] = (int)(e - entries);
        load_count++;
        SDL_SetAtomicInt(&e->state, 1);
        SDL_AddAtomicInt(&load_pending, 1);
        SDL_SignalCondition(load_cond);
    }
    SDL_UnlockMutex(load_lock);
    return NULL;
}

/* Test of the loader thread's life: queue one PNG, wait for the result, end the thread, ask once
 * more. Returns 0 when everything held (1 not decoded in time, 2 pending count wrong, 3 a request
 * after the shutdown was not refused, 4 no picture). */
int TexRep_SelfTestLoader(const char *png, int bw, int bh) {
    int k = 0, id, was_sync = load_sync;
    Uint64 t0;
    TexRepImg *img = NULL;

    alloc_tables();
    load_sync = 0;
    add_entry("selftest_loader", png);
    id = find_entry("selftest_loader");
    if (id < 0) {
        load_sync = was_sync;
        return 4;
    }
    (void)load_png(&entries[id], bw, bh, &k); /* queues */
    t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < 10000 && (img = load_png(&entries[id], bw, bh, &k)) == NULL &&
           SDL_GetAtomicInt(&entries[id].state) != 2) {
        SDL_Delay(2);
    }
    if (img == NULL) {
        load_sync = was_sync;
        return SDL_GetAtomicInt(&entries[id].state) == 2 ? 4 : 1;
    }
    if (TexRep_Pending() != 0) {
        load_sync = was_sync;
        return 2;
    }
    TexRep_Shutdown();
    SDL_SetAtomicInt(&entries[id].state, 0);
    entries[id].img = NULL;
    if (load_png(&entries[id], bw, bh, &k) != NULL || SDL_GetAtomicInt(&entries[id].state) != 0) {
        load_sync = was_sync;
        return 3;
    }
    load_sync = was_sync;
    return 0;
}

static TexRepImg *decode_png(Entry *e, int bw, int bh) {
    SDL_Surface *s, *c;
    TexRepImg *img;

    s = SDL_LoadPNG(e->path);
    if (s == NULL) {
        tlog("%s: cannot read (%s), original kept\n", e->path, SDL_GetError());
        stat_failed_inc();
        return NULL;
    }
    if (s->w % bw || s->h % bh || s->w / bw != s->h / bh || s->w / bw < 1 || s->w / bw > 16) {
        tlog("%s: %dx%d is not 1..16 times the texture (%dx%d), original kept\n", e->path, s->w, s->h,
               bw, bh);
        SDL_DestroySurface(s);
        stat_failed_inc();
        return NULL;
    }
    if (mem_bytes() + (size_t)s->w * (size_t)s->h * 4 * 4 / 3 > MEM_LIMIT) /* with the mip levels */ {
        tlog("%s: over the memory limit (%u MiB), original kept\n", e->path, (unsigned)(MEM_LIMIT >> 20));
        SDL_DestroySurface(s);
        stat_failed_inc();
        return NULL;
    }
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(s);
    img = c != NULL ? calloc(1, sizeof(*img)) : NULL;
    if (img != NULL) {
        img->w = c->w;
        img->h = c->h;
        img->px = malloc((size_t)c->w * (size_t)c->h * 4);
        if (img->px != NULL) {
            for (int y = 0; y < c->h; y++) {
                memcpy(img->px + (size_t)y * (size_t)c->w, (uint8_t *)c->pixels + (size_t)y * (size_t)c->pitch,
                       (size_t)c->w * 4);
            }
            mem_add((size_t)c->w * (size_t)c->h * 4);
        } else {
            free(img);
            img = NULL;
        }
    }
    SDL_DestroySurface(c);
    if (img == NULL) {
        tlog("%s: out of memory, original kept\n", e->path);
        stat_failed_inc();
        return NULL;
    }
    img->nlev = 1;
    img->lev[0].w = img->w;
    img->lev[0].h = img->h;
    img->lev[0].px = img->px;
    {
        int k = img->w / bw;
        while ((k & (k - 1)) == 0 && k >> img->nlev >= 1 && img->nlev < MAX_LEVELS) {
            int L = img->nlev, pw = img->lev[L - 1].w, ph = img->lev[L - 1].h, w = pw / 2, h = ph / 2;
            const uint32_t *src = img->lev[L - 1].px;
            uint32_t *dst = malloc((size_t)w * (size_t)h * 4);
            if (dst == NULL || mem_bytes() + (size_t)w * h * 4 > MEM_LIMIT) {
                free(dst);
                break;
            }
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int q = 0; q < 4; q++) {
                        uint32_t p = src[(size_t)(2 * y + (q >> 1)) * pw + (size_t)(2 * x + (q & 1))];
                        a += p >> 24;
                        if ((p >> 24) >= 128) {
                            r += (p >> 16) & 255;
                            g += (p >> 8) & 255;
                            b += p & 255;
                            n++;
                        }
                    }
                    /* transparent where most of the four are */
                    dst[(size_t)y * w + x] = n == 0 || n < 2 ? (uint32_t)(a / 4) << 24 & 0x7F000000u
                                                             : 0xFF000000u | (r / n) << 16 | (g / n) << 8 | (b / n);
                }
            }
            mem_add((size_t)w * h * 4);
            img->lev[L].w = w;
            img->lev[L].h = h;
            img->lev[L].px = dst;
            img->nlev++;
        }
    }
    SDL_AddAtomicInt(&a_loaded, 1);
    tlog("%s: %dx%d (%dx, %d levels)\n", e->path, img->w, img->h, img->w / bw, img->nlev);
    return img;
}

static uint16_t vram(int x, int y) {
    return PsxGpu_Vram[y & (PSXGPU_VRAM_H - 1)][x & (PSXGPU_VRAM_W - 1)];
}

/* the whole upload as the game would draw it with this mode and CLUT, as a PNG */
static void dump(const Upload *u, int mode, int cx, int cy, const char *name) {
    int tpw = mode == 0 ? 4 : mode == 1 ? 2 : 1, bw = u->w * tpw;
    SDL_Surface *s = SDL_CreateSurface(bw, u->h, SDL_PIXELFORMAT_ARGB8888);
    char *path = NULL;

    if (s == NULL) return;
    for (int y = 0; y < u->h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch);
        for (int x = 0; x < bw; x++) {
            uint16_t w = vram(u->x + x / tpw, u->y + y), c;
            if (mode == 0) c = vram(cx + ((w >> ((x & 3) * 4)) & 0xF), cy);
            else if (mode == 1) c = vram(cx + ((w >> ((x & 1) * 8)) & 0xFF), cy);
            else c = w;
            row[x] = c == 0 ? 0 : 0xFF000000u | (uint32_t)((c & 31) << 3 | (c & 31) >> 2) << 16 |
                                     (uint32_t)(((c >> 5) & 31) << 3 | ((c >> 5) & 31) >> 2) << 8 |
                                     (uint32_t)(((c >> 10) & 31) << 3 | ((c >> 10) & 31) >> 2);
        }
    }
    SDL_asprintf(&path, "%s/%s.png", dump_dir, name);
    if (path != NULL && SDL_SavePNG(s, path)) {
        tlog("dumped %s\n", path);
    }
    SDL_free(path);
    SDL_DestroySurface(s);
}

static int was_dumped(const char *name) {
    uint64_t h = fnv(0xCBF29CE484222325ull, name, strlen(name));
    for (int i = 0; i < ndumped; i++) {
        if (dumped[i] == h) return 1;
    }
    if (ndumped < MAX_DUMPED) dumped[ndumped++] = h;
    return 0;
}

int TexRep_Find(int mode, int tx, int ty, int cx, int cy, int tw_mask_x, int tw_mask_y, int umin, int umax,
                int vmin, int vmax, TexRepUse *use) {
    int tpw = mode == 0 ? 4 : mode == 1 ? 2 : 1;

    use->img = NULL;
    texrep_perf.finds++;
    if (nuploads == 0 || tw_mask_x || tw_mask_y || mode > 2 || umin < 0 || vmin < 0 || umax > 255 || vmax > 255) {
        return 0;
    }
    for (int i = nuploads - 1; i >= 0; i--) {
        const Upload *u = &uploads[i];
        int du = (tx - u->x) * tpw, dv = ty - u->y, bw = u->w * tpw, bh = u->h;
        char base[48], name[64];
        uint32_t clut = 0;
        int e = -1, k = 1;
        TexRepImg *img;

        /* a quad's far UV edge is exclusive (u1 = u0 + w): a box reaching exactly one texel past
           the upload is still inside it (the result windows' boxes end at u 128 of a 128 wide page) */
        int um = umax + du == bw ? umax - 1 : umax, vm = vmax + dv == bh ? vmax - 1 : vmax;
        if (umin + du < 0 || um + du >= bw || vmin + dv < 0 || vm + dv >= bh) continue;
        umax = um;
        vmax = vm;
        if (u->src >= 0 && u->src < nsrcs && srcs[u->src].mode == mode && out_scale >= 2) {
            const IdxSrc *s = &srcs[u->src];
            uint16_t pal[256];
            int n = mode == 0 ? 16 : 256, k = out_scale * prim_mag > 16 ? 16 : out_scale * prim_mag;
            int tx0 = (umin + du) / s->tw, tx1 = (umax + du) / s->tw, ty0 = (vmin + dv) / s->th, ty1 = (vmax + dv) / s->th;
            int ox = tx0 * s->tw, oy = ty0 * s->th, rw = (tx1 - tx0 + 1) * s->tw, rh = (ty1 - ty0 + 1) * s->th;
            TexRepImg *ti;

            if (ox + rw > bw) rw = bw - ox;
            if (oy + rh > bh) rh = bh - oy;
            if (rw > 256 || rh > 256) return 0;
            /* a tile of at most 1M pixels (a primitive over a whole sheet): fewer replacement pixels
               per texel there, never fewer than the scale's */
            while (k > out_scale && (int64_t)rw * k * rh * k > ((int64_t)1 << 20)) k--;
            for (int d = 0; d < u->ndirty; d++) { /* the primitive's texels in VRAM words / lines */
                int vx0 = u->x + (umin + du) / tpw, vx1 = u->x + (umax + du) / tpw, vy0 = u->y + vmin + dv, vy1 = u->y + vmax + dv;
                if (vx0 < u->dirty[d].x + u->dirty[d].w && u->dirty[d].x <= vx1 && vy0 < u->dirty[d].y + u->dirty[d].h &&
                    u->dirty[d].y <= vy1) {
                    /* a partial write over the page (palette animation in the page, a copied piece):
                       the replacement stays when this primitive's texels are still the original
                       ones (strips of the attack names were drawn as 1x texels otherwise) */
                    int same = s->words != NULL;
                    for (int y = vy0; y <= vy1 && same; y++)
                        for (int x = vx0; x <= vx1 && same; x++)
                            same = vram(x, y) == s->words[(size_t)(y - u->y) * s->w + (x - u->x)];
                    if (!same) return 0;
                    break;
                }
            }
            uint32_t ch = 0;
            if (s->cls != NULL) { /* a styled tile is coloured when made: per palette */
                for (int j = 0; j < n; j++) pal[j] = vram(cx + j, cy);
                ch = (uint32_t)fnv(0xCBF29CE484222325ull, pal, (size_t)n * 2);
            }
            ti = idx_tile(u->src, ox, oy, rw, rh, k, pal, ch);
            if (ti == NULL) return 0;
            use->cx = cx;
            use->cy = cy;
            use->mode = mode;
            use->img = ti;
            use->k = k;
            use->du = du - ox;
            use->dv = dv - oy;
            use->umin = umin;
            use->umax = umax;
            use->vmin = vmin;
            use->vmax = vmax;
            use->mag = prim_mag;
            stat_uses++;
            return 1;
        }
        snprintf(base, sizeof(base), "%016llx_%dx%d", (unsigned long long)u->hash, u->w, u->h);
        if (mode < 2) {
            uint16_t pal[256];
            int n = mode == 0 ? 16 : 256;
            for (int j = 0; j < n; j++) pal[j] = vram(cx + j, cy);
            clut = (uint32_t)fnv(0xCBF29CE484222325ull, pal, (size_t)n * 2);
            snprintf(name, sizeof(name), "%s_m%d_c%08x", base, mode, clut);
            if (dump_dir != NULL && !was_dumped(name)) dump(u, mode, cx, cy, name);
            e = find_entry(name);
            if (e < 0) {
                snprintf(name, sizeof(name), "%s_m%d", base, mode);
                e = find_entry(name);
            }
        } else {
            if (dump_dir != NULL && !was_dumped(base)) dump(u, mode, cx, cy, base);
            e = find_entry(base);
        }
        if (e < 0 || (img = load_png(&entries[e], bw, bh, &k)) == NULL) {
            continue; /* the array is not in upload order (OnWrite fills gaps): another upload may hold
                         the box and a replacement (a runtime index source) */
        }
        use->img = img;
        use->k = k;
        use->du = du;
        use->dv = dv;
        use->umin = umin;
        use->umax = umax;
        use->vmin = vmin;
        use->vmax = vmax;
        use->mag = prim_mag;
        stat_uses++;
        return 1;
    }
    return 0;
}

/* dw2-pc-port: the HD renderer's scale. A replacement k times the texture drawn at scale S < k
 * gives each output pixel about k / S replacement pixels: they are averaged (at most 4 x 4), so a
 * large PNG does not shimmer at a lower internal resolution. */

void TexRep_SetScale(int s) {
    out_scale = s < 1 ? 1 : s;
}

/* The GPU renderer's view of a replacement (backend/psxgpu_hw.c, shaders/hd.frag rep_sample): the
 * level TexRep_Sample would read at the current scale. Returns its pixels (0xAARRGGBB, w x h) and
 * kl = replacement pixels per original texel at that level, n = the block edge of the averaging
 * (0 or 1: one pixel, 2..4: n x n pixels). */
/* Output pixels per texel of the primitive: the scale times its magnification. A magnified
   primitive (zooming battle words) covers fewer replacement pixels per output pixel: averaging
   k / scale of them there blurred its edges and cost up to 16 reads per pixel for one that
   matches (runtime tiles are made at k = scale x magnification: one read). */
static int out_px(const TexRepUse *use) {
    int m = use->mag < 1 ? 1 : use->mag;
    return out_scale * m;
}

const uint32_t *TexRep_GpuLevel(const TexRepUse *use, int *kl, int *n, int *w, int *h, int *idx8) {
    const TexRepImg *img0 = use->img;
    *idx8 = img0->idx8;
    int k = use->k, L = 0, op = out_px(use);

    while (L + 1 < img0->nlev && (k >> (L + 1)) >= op) {
        L++;
    }
    k >>= L;
    *kl = k;
    *n = k / op;
    *n = *n < 1 ? 1 : *n;
    *n = *n > 4 ? 4 : *n;
    *w = img0->lev[L].w;
    *h = img0->lev[L].h;
    return img0->lev[L].px;
}

int TexRep_Sample(const TexRepUse *use, int fu, int fv) {
    const TexRepImg *img0 = use->img;
    int k = use->k, L = 0, op = out_px(use);
    while (L + 1 < img0->nlev && (k >> (L + 1)) >= op) {
        L++; /* the level with about one replacement pixel per output pixel */
    }
    k >>= L;
    struct {
        int w, h;
        const uint32_t *px;
    } lv = { img0->lev[L].w, img0->lev[L].h, img0->lev[L].px }, *img = &lv;
    int hx = (int)(((int64_t)fu * k) >> 8), hy = (int)(((int64_t)fv * k) >> 8);
    int x0 = use->umin * k, x1 = (use->umax + 1) * k - 1, y0 = use->vmin * k, y1 = (use->vmax + 1) * k - 1;
    int n = k / op;
    uint32_t p;

    hx = hx < x0 ? x0 : hx > x1 ? x1 : hx; /* inside the primitive's texels: no atlas bleeding */
    hy = hy < y0 ? y0 : hy > y1 ? y1 : hy;
    if (img0->idx8) {
        /* a runtime tile (made at this primitive's output size: one pixel each) through the CLUT */
        hx += use->du * k;
        hy += use->dv * k;
        if (hx < 0 || hy < 0 || hx >= img->w || hy >= img->h) return -1;
        int i = ((const uint8_t *)img->px)[(size_t)hy * (size_t)img->w + (size_t)hx];
        uint16_t c = i != 0 ? vram(use->cx + i, use->cy) : 0;
        if (c == 0) return -1; /* index 0 and colour 0: transparent, as on the PS1 */
        /* with its own semi-transparency bit (the CLUT colour's, as the PS1 does per texel): the 1x
           texel's bit followed the old 1x letter shape (blended attack names showed it) */
        return c | 0x10000;
    }
    if (n >= 2) {
        /* the block of the output pixel, kept inside the primitive's texels */
        unsigned r = 0, g = 0, b = 0, cnt = 0, all = 0;
        n = n > 4 ? 4 : n;
        hx -= hx - x0 >= n / 2 ? n / 2 : hx - x0;
        hy -= hy - y0 >= n / 2 ? n / 2 : hy - y0;
        for (int dy = 0; dy < n; dy++) {
            for (int dx = 0; dx < n; dx++) {
                int sx = hx + dx > x1 ? x1 : hx + dx, sy = hy + dy > y1 ? y1 : hy + dy;
                sx += use->du * k;
                sy += use->dv * k;
                if (sx < 0 || sy < 0 || sx >= img->w || sy >= img->h) continue;
                p = img->px[(size_t)sy * (size_t)img->w + (size_t)sx];
                all++;
                if ((p >> 24) < 128) continue;
                r += (p >> 16) & 255;
                g += (p >> 8) & 255;
                b += p & 255;
                cnt++;
            }
        }
        if (cnt == 0 || cnt * 2 < all) return -1; /* mostly transparent there */
        r /= cnt;
        g /= cnt;
        b /= cnt;
        return (int)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
    }
    hx += use->du * k;
    hy += use->dv * k;
    if (hx < 0 || hy < 0 || hx >= img->w || hy >= img->h) return -1;
    p = img->px[(size_t)hy * (size_t)img->w + (size_t)hx];
    if ((p >> 24) < 128) return -1;
    return (int)(((p >> 19) & 31) | ((p >> 11) & 31) << 5 | ((p >> 3) & 31) << 10);
}

/* tests/hd_test.c: a 16x replacement of one texel with a one-pixel checkerboard (black / white).
 * Drawn at scale 4 (four replacement pixels per output pixel), every sample is the averaged grey;
 * at scale 16 (one per output pixel), the samples are black or white as in the PNG.
 * Returns the number of wrong samples. */
int TexRep_SelfTestAverage(void) {
    static uint32_t px[16 * 16];
    TexRepImg img;
    TexRepUse use;
    int bad = 0, keep = out_scale;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            px[y * 16 + x] = (x + y) & 1 ? 0xFFFFFFFFu : 0xFF000000u;
        }
    }
    memset(&img, 0, sizeof(img));
    img.w = 16;
    img.h = 16;
    img.px = px;
    img.nlev = 1; /* no halved copies: the block averaging alone */
    img.lev[0].w = 16;
    img.lev[0].h = 16;
    img.lev[0].px = px;
    memset(&use, 0, sizeof(use));
    use.img = &img;
    use.k = 16;
    TexRep_SetScale(4);
    for (int i = 0; i < 16; i++) {
        int c = TexRep_Sample(&use, i * 16, (15 - i) * 16), r = c & 31;
        bad += c < 0 || r < 14 || r > 17; /* 15.5 of 31: grey */
    }
    TexRep_SetScale(16);
    for (int i = 0; i < 16; i++) {
        int c = TexRep_Sample(&use, i * 16 + 8, 8), r = c & 31;
        bad += c < 0 || r != (i & 1 ? 31 : 0);
    }
    TexRep_SetScale(keep);
    return bad;
}
