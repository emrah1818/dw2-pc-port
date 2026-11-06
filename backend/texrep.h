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
 * Only the software HD path (scale >= 2 or wide, renderer "soft") draws replacements; the GPU
 * renderer and the 1x picture (classic) stay original. A replacement is used only for polygons
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

#endif
