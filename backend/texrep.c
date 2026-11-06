/* dw2-pc-port: external replacement textures (see texrep.h). */
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
    uint32_t *px; /* 0xAARRGGBB */
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
static size_t mem_used;
static int stat_loaded, stat_failed, stat_uses;
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

int TexRep_Count(void) {
    return nentries;
}

int TexRep_PackCount(void) {
    return npacks;
}

const char *TexRep_PackName(int i) {
    return i >= 0 && i < npacks ? packs[i] : "";
}

int TexRep_Active(void) {
    return !disabled && (nentries > 0 || dump_dir != NULL);
}

void TexRep_Stats(int *loaded, int *failed, int *uses) {
    *loaded = stat_loaded;
    *failed = stat_failed;
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

static int overlaps(const Upload *u, int x, int y, int w, int h) {
    return x < u->x + u->w && u->x < x + w && y < u->y + u->h && u->y < y + h;
}

void TexRep_OnWrite(int x, int y, int w, int h) {
    for (int i = 0; i < nuploads;) {
        if (overlaps(&uploads[i], x, y, w, h)) {
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
    if (dump_dir == NULL && !has_hash(hv)) return;
    if (nuploads == MAX_UPLOADS) {
        memmove(uploads, uploads + 1, sizeof(uploads[0]) * (MAX_UPLOADS - 1)); /* drop the oldest */
        nuploads--;
    }
    uploads[nuploads].x = x;
    uploads[nuploads].y = y;
    uploads[nuploads].w = w;
    uploads[nuploads].h = h;
    uploads[nuploads].hash = hv;
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
        while (load_count == 0) {
            SDL_WaitCondition(load_cond, load_lock);
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

static TexRepImg *decode_png(Entry *e, int bw, int bh) {
    SDL_Surface *s, *c;
    TexRepImg *img;

    s = SDL_LoadPNG(e->path);
    if (s == NULL) {
        tlog("%s: cannot read (%s), original kept\n", e->path, SDL_GetError());
        stat_failed++;
        return NULL;
    }
    if (s->w % bw || s->h % bh || s->w / bw != s->h / bh || s->w / bw < 1 || s->w / bw > 16) {
        tlog("%s: %dx%d is not 1..16 times the texture (%dx%d), original kept\n", e->path, s->w, s->h,
               bw, bh);
        SDL_DestroySurface(s);
        stat_failed++;
        return NULL;
    }
    if (mem_used + (size_t)s->w * (size_t)s->h * 4 > MEM_LIMIT) {
        tlog("%s: over the memory limit (%u MiB), original kept\n", e->path, (unsigned)(MEM_LIMIT >> 20));
        SDL_DestroySurface(s);
        stat_failed++;
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
            mem_used += (size_t)c->w * (size_t)c->h * 4;
        } else {
            free(img);
            img = NULL;
        }
    }
    SDL_DestroySurface(c);
    if (img == NULL) {
        tlog("%s: out of memory, original kept\n", e->path);
        stat_failed++;
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
            if (dst == NULL || mem_used + (size_t)w * h * 4 > MEM_LIMIT) {
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
            mem_used += (size_t)w * h * 4;
            img->lev[L].w = w;
            img->lev[L].h = h;
            img->lev[L].px = dst;
            img->nlev++;
        }
    }
    stat_loaded++;
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

        if (umin + du < 0 || umax + du >= bw || vmin + dv < 0 || vmax + dv >= bh) continue;
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
            return 0;
        }
        use->img = img;
        use->k = k;
        use->du = du;
        use->dv = dv;
        use->umin = umin;
        use->umax = umax;
        use->vmin = vmin;
        use->vmax = vmax;
        stat_uses++;
        return 1;
    }
    return 0;
}

/* dw2-pc-port: the HD renderer's scale. A replacement k times the texture drawn at scale S < k
 * gives each output pixel about k / S replacement pixels: they are averaged (at most 4 x 4), so a
 * large PNG does not shimmer at a lower internal resolution. */
static int out_scale = 1;

void TexRep_SetScale(int s) {
    out_scale = s < 1 ? 1 : s;
}

int TexRep_Sample(const TexRepUse *use, int fu, int fv) {
    const TexRepImg *img0 = use->img;
    int k = use->k, L = 0;
    while (L + 1 < img0->nlev && (k >> (L + 1)) >= out_scale) {
        L++; /* the level with about one replacement pixel per output pixel */
    }
    k >>= L;
    struct {
        int w, h;
        const uint32_t *px;
    } lv = { img0->lev[L].w, img0->lev[L].h, img0->lev[L].px }, *img = &lv;
    int hx = (int)(((int64_t)fu * k) >> 8), hy = (int)(((int64_t)fv * k) >> 8);
    int x0 = use->umin * k, x1 = (use->umax + 1) * k - 1, y0 = use->vmin * k, y1 = (use->vmax + 1) * k - 1;
    int n = k / out_scale;
    uint32_t p;

    hx = hx < x0 ? x0 : hx > x1 ? x1 : hx; /* inside the primitive's texels: no atlas bleeding */
    hy = hy < y0 ? y0 : hy > y1 ? y1 : hy;
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
