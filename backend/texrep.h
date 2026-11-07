/* dw2-pc-port: external replacement textures (texture packs) for the HD software renderer.
 *
 * The game uploads its textures to VRAM (PsxGpu_LoadImage). Every upload gets a stable id: an
 * FNV-1a 64 hash of its size and pixels, so the same picture has the same id wherever and
 * whenever it is uploaded. A pack is a folder of PNG files named after these ids:
 *
 *   <hash>_<w>x<h>.png                  16-bit (direct colour) texture
 *   <hash>_<w>x<h>_m<mode>.png          4-bit (m0) / 8-bit (m1) texture, every palette
 *   <hash>_<w>x<h>_m<mode>_c<clut>.png  the same for one palette (wins over the line above)
 *
 * w/h are the upload size in VRAM words; the PNG must be k times the texture's size in texels
 * (w*4 x h for 4-bit, w*2 x h for 8-bit, w x h for 16-bit), k = 1..16. `--dump-textures DIR`
 * writes the textures the game draws under exactly these names (from the player's disc: keep
 * them private). Packs: Mods/Textures/<pack>/ next to the exe or in the data folder; every pack
 * is used, a later one (by name) wins; a folder whose name starts with '_' is skipped.
 *
 * The HD paths (scale >= 2) of both renderers draw replacements (the GPU renderer through its
 * replacement pool, backend/psxgpu_hw.c); the 1x picture (classic) stays original. A replacement is used only for polygons
 * whose texture area lies inside one upload, without texture window; a later VRAM write over an
 * upload (palette change, fill, copy) drops its replacement (back to the original). */
#ifndef BACKEND_TEXREP_H
#define BACKEND_TEXREP_H

#include <stdint.h>

typedef struct TexRepImg TexRepImg;

/* What a primitive samples instead of VRAM (filled by TexRep_Find; img NULL = original). */
typedef struct {
    const TexRepImg *img;
    int k;          /* replacement texels per original texel */
    int du, dv;     /* texture page texel -> upload texel */
    int umin, umax, vmin, vmax; /* the primitive's texel box (texture page coordinates) */
} TexRepUse;

/* Setup (main thread, before the game runs). A pack root is a "Mods/Textures" folder. */
void TexRep_AddRoot(const char *mods_textures_dir);
void TexRep_SetDump(const char *dir);
void TexRep_Disable(void);
void TexRep_Enable(void); /* tests: undo TexRep_Disable */
const uint32_t *TexRep_GpuLevel(const TexRepUse *use, int *kl, int *n, int *w, int *h);
void TexRep_Shutdown(void); /* joins the loader thread (program exit) */
/* every [textures] line also into this file (Mods/Textures/textures_log.txt) */
void TexRep_SetLog(const char *path);
int TexRep_Count(void);       /* PNG files known from all packs */
int TexRep_PackCount(void);
const char *TexRep_PackName(int i);
int TexRep_Active(void);      /* any pack file or dumping */
/* Statistics for the F1 menu: replacements loaded / failed (bad file) so far. */
void TexRep_Stats(int *loaded, int *failed, int *uses);

/* VRAM writes (game thread, backend/psxgpu.c). */
void TexRep_OnUpload(int x, int y, int w, int h, const uint16_t *src);
void TexRep_OnWrite(int x, int y, int w, int h);

/* A textured primitive (game thread, when it is queued): mode 0/1/2, texture page (tx, ty) in
 * VRAM words / lines, CLUT (cx, cy), texture window masks, texel box. Returns 1 and fills `use`
 * when a replacement applies. */
int TexRep_Find(int mode, int tx, int ty, int cx, int cy, int tw_mask_x, int tw_mask_y, int umin, int umax,
                int vmin, int vmax, TexRepUse *use);

/* The replacement colour at (fu, fv) (texture page texels, 8.8 fixed point): PS1 15-bit BGR
 * without mask bit, or -1 when the replacement pixel is transparent. Thread safe (read only). */
void TexRep_SetScale(int s); /* the HD renderer scale (block averaging of large PNGs) */
int TexRep_SelfTestAverage(void);
int TexRep_SelfTestLoader(const char *png, int bw, int bh); /* 0 = loader thread start, result, shutdown ok */
void TexRep_SetSyncLoad(int on); /* tests: decode PNGs on the calling thread (default: loader thread) */
int TexRep_Pending(void);         /* replacements still being decoded */ /* tests: wrong samples of the averaging (0 = right) */
int TexRep_Sample(const TexRepUse *use, int fu, int fv);

/* dw2-pc-port: runtime index sources (modern fonts, sharp at any scale). The host registers the
 * pixels of a 4-bit (mode 0) or 8-bit (mode 1) upload it knows (a game font atlas, a painted word
 * picture) and a function that draws any texel rectangle of it k times larger as palette indices
 * (k = the HD renderer's scale, 2..16), e.g. letters rasterized from the TrueType outlines at the
 * output pixel size. The renderers then sample those indices through the game's palette of each
 * primitive: the game's text colours, palette cycling, order, clipping and fades stay. Tiles of
 * tw x th texels (a glyph cell) are made on first use, one per palette, and kept. */
typedef void (*TexRepIndexFn)(void *ctx, int k, int x0, int y0, int w, int h, uint8_t *idx);
void TexRep_AddIndexSource(const uint16_t *words, int w, int h, int mode, int tw, int th, TexRepIndexFn fn, void *ctx);
void TexRep_ClearIndexSources(void);
/* as TexRep_AddIndexSource, but an existing source with these pixels keeps its drawing */
void TexRep_AddIndexSourceIfNew(const uint16_t *words, int w, int h, int mode, int tw, int th, TexRepIndexFn fn, void *ctx);
/* Modern UI panels on a source: cls holds one class per texel of the upload (0 not a panel,
 * 1 panel fill, 2 other texel on a panel: text, bars, icons) and cid its panel's number, whose
 * rows are comp[2 n] .. comp[2 n + 1]. Panel fills are drawn in the port's own style at the output
 * resolution (an outline smoothed from the 1x shape, cyan edge, dark blue gradient); the other
 * texels as the source draws them. The caller keeps the arrays. */
void TexRep_SetSourceStyle(const uint16_t *words, int w, int h, const uint8_t *cls, const uint16_t *cid,
                           const uint16_t *comp, int ncomp);
/* the screen pixels per texel of the next TexRep_Find's primitive (1..4, rounded up): an index source
 * is drawn at k = scale x this (at most 16), so magnified text (zooming battle words) stays sharp */
void TexRep_SetMagnify(int m);
void TexRep_IndexStats(int *sources, int *tiles, int *kib);
int TexRep_TileResetWanted(void); /* the tile memory filled up: drop the tiles after the picture */
void TexRep_DropTiles(void);      /* frees all tiles (no queued primitive may use one) */

#endif
