/* dw2-pc-port asset export and texture import (main thread, before or outside the game).
 *
 * Export (dw2 --export-assets DIR, or the F1 Modding tab): from the verified dw2.pak, i.e. from
 * the player's own disc, never from anything shipped:
 *   DIR/Original/files/<ID>.bin   every disc file (Form 1) unchanged, ID = file id in hex
 *   DIR/Original/index.csv        id, LBA, sectors, size, what the file is (as far as known)
 *   DIR/Textures/<ID>/<ID>_<OFFSET>.png  every TIM image found in a file (RGBA, the first palette;
 *                                 more palettes as _p<N>.png), OFFSET = byte offset of the TIM
 *   DIR/Digimon/<Name>_<id>/      per species: Metadata/info.json (from the game's DIGIMNDT table),
 *                                 Models/<ID>.bin (the species' model file from the table),
 *                                 Textures/ (the TIMs of that model file)
 *   DIR/GameDatabase/digimon.json, items.json, skills.json   the game's tables, decoded
 *   DIR/README.txt                how to change files (Mods/files) and textures
 * Unknown things stay unknown: a file without a TIM and not in a table is listed as "unknown".
 * Form 2 files (XA audio, STR movies) are listed, not exported (raw CD sectors).
 *
 * Texture import (dw2 --import-texture PNG): PNG named <ID>_<OFFSET>.png as exported, same width
 * and height as that TIM. The pixels are mapped to the TIM's own palette (nearest colour;
 * transparent pixels to a palette entry that is colour 0), written into a copy of the file in
 * Mods/files/<ID>.bin next to the exe (the mod layer of host/pak.c). The original stays in the
 * pack; deleting the Mods file restores it. 16-bit TIMs take the colours directly. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "host/devdata.h"
#include "host/host.h"
#include "host/miniz/miniz.h"

#define FILES 0xE5B

typedef struct {
    uint32_t off;      /* TIM start in the file */
    int bpp;           /* 4, 8, 16, 24 */
    int clut_w, clut_h; /* palette entries per row, rows (0 without palette) */
    uint32_t clut_off; /* first palette entry */
    int w, h;          /* pixels */
    uint32_t img_off;  /* first pixel byte */
    uint32_t end;      /* TIM end */
    int dw2pal;        /* DW2 model texture: 128x256 4-bit, 64 palettes of 16 in its bottom 32 rows
                          (the CLUT block header does not match; as the DW2 3D viewer reads it) */
} Tim;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int rd16(const uint8_t *p) {
    return p[0] | p[1] << 8;
}

/* A TIM at `o`: magic 0x10, flags bpp | 8 with CLUT, block lengths that match their sizes. */
static int tim_at(const uint8_t *d, uint32_t size, uint32_t o, Tim *t) {
    uint32_t flags, p, len;
    int bw, bh;

    if (o + 8 > size || rd32(d + o) != 0x10) {
        return 0;
    }
    flags = rd32(d + o + 4);
    if (flags & ~0xBu) {
        return 0;
    }
    t->off = o;
    t->bpp = (int[]){ 4, 8, 16, 24 }[flags & 3];
    if (t->bpp == 24) {
        return 0;
    }
    p = o + 8;
    t->clut_w = t->clut_h = 0;
    t->dw2pal = 0;
    if (flags == 8 && p + 12 <= size && rd32(d + p) == 44 && p + 44 + 12 <= size && rd32(d + p + 44) == 16396 &&
        rd16(d + p + 44 + 8) == 32 && rd16(d + p + 44 + 10) == 256 && p + 44 + 16396 <= size) {
        t->dw2pal = 1;
        t->w = 128;
        t->h = 256;
        t->img_off = p + 44 + 12;
        t->clut_off = t->img_off + 224 * 64;
        t->clut_w = 16;
        t->clut_h = 64;
        t->end = p + 44 + 16396;
        return 1;
    }
    if (flags & 8) {
        if (p + 12 > size) {
            return 0;
        }
        len = rd32(d + p);
        bw = rd16(d + p + 8);
        bh = rd16(d + p + 10);
        if (bw <= 0 || bh <= 0 || bw > 256 || bh > 256 || len != 12u + (uint32_t)bw * bh * 2 || p + len > size) {
            return 0;
        }
        t->clut_w = bw;
        t->clut_h = bh;
        t->clut_off = p + 12;
        p += len;
    } else if (t->bpp < 16) {
        return 0;
    }
    if (p + 12 > size) {
        return 0;
    }
    len = rd32(d + p);
    bw = rd16(d + p + 8);
    bh = rd16(d + p + 10);
    if (bw <= 0 || bh <= 0 || bw > 1024 || bh > 512 || len != 12u + (uint32_t)bw * bh * 2 || p + len > size) {
        return 0;
    }
    t->w = t->bpp == 4 ? bw * 4 : t->bpp == 8 ? bw * 2 : bw;
    t->h = bh;
    t->img_off = p + 12;
    t->end = p + len;
    if (t->bpp == 4 && t->clut_w < 16) {
        return 0;
    }
    if (t->bpp == 8 && t->clut_w < 256 && t->clut_w * t->clut_h < 256) {
        return 0;
    }
    return 1;
}

static uint32_t rgba15(int c) {
    uint32_t r = (uint32_t)(c & 31) * 255 / 31, g = (uint32_t)((c >> 5) & 31) * 255 / 31,
             b = (uint32_t)((c >> 10) & 31) * 255 / 31;
    uint32_t a = c == 0 ? 0 : 255; /* PS1: colour 0 is transparent */

    return r | g << 8 | b << 16 | a << 24; /* bytes R G B A in memory (little endian) */
}

static int tim_index(const uint8_t *d, const Tim *t, int x, int y) {
    const uint8_t *row = d + t->img_off + (size_t)y * (t->bpp == 4 ? t->w / 2 : t->bpp == 8 ? t->w : t->w * 2);

    if (t->bpp == 4) {
        return (row[x / 2] >> ((x & 1) * 4)) & 15;
    }
    if (t->bpp == 8) {
        return row[x];
    }
    return rd16(row + x * 2);
}

/* RGBA pixels of TIM t with palette row `pal` (malloc'd, w*h words) */
static uint32_t *tim_rgba(const uint8_t *d, const Tim *t, int pal) {
    uint32_t *px = malloc((size_t)t->w * t->h * 4);

    for (int y = 0; px != NULL && y < t->h; y++) {
        for (int x = 0; x < t->w; x++) {
            int v = tim_index(d, t, x, y);

            if (t->bpp < 16) {
                int n = t->clut_w * t->clut_h;
                int i = pal * t->clut_w + v;
                v = i < n ? rd16(d + t->clut_off + (size_t)i * 2) : 0;
            }
            px[(size_t)y * t->w + x] = rgba15(v);
        }
    }
    return px;
}

static int write_png_rgba(const char *path, const uint32_t *px, int w, int h) {
    size_t len = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(px, w, h, 4, &len, 6, MZ_FALSE);
    SDL_IOStream *io;
    int ok = 0;

    if (png == NULL) {
        return 0;
    }
    io = SDL_IOFromFile(path, "wb");
    if (io != NULL) {
        ok = SDL_WriteIO(io, png, len) == len;
        SDL_CloseIO(io);
    }
    mz_free(png);
    return ok;
}

static int write_file(const char *path, const void *data, size_t n) {
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    int ok;

    if (io == NULL) {
        printf("[assets] cannot write %s: %s\n", path, SDL_GetError());
        return 0;
    }
    ok = SDL_WriteIO(io, data, n) == n;
    SDL_CloseIO(io);
    return ok;
}

static void mkdirs(const char *path) {
    SDL_CreateDirectory(path); /* SDL3 creates parent folders too */
}

/* JSON string body (UTF-8 passes through; quotes, backslashes and controls escaped) */
static void json_str(SDL_IOStream *io, const char *s) {
    SDL_WriteIO(io, "\"", 1);
    for (; s != NULL && *s; s++) {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\') {
            SDL_IOprintf(io, "\\%c", c);
        } else if (c < 0x20) {
            SDL_IOprintf(io, "\\u%04x", c);
        } else {
            SDL_WriteIO(io, s, 1);
        }
    }
    SDL_WriteIO(io, "\"", 1);
}

/* a file-system safe copy of a game name */
static void safe_name(const char *in, char *out, int n) {
    int k = 0;

    for (; in != NULL && *in && k < n - 1; in++) {
        unsigned char c = (unsigned char)*in;
        out[k++] = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c >= 0x80
                       ? (char)c : '_';
    }
    out[k] = 0;
    if (k == 0) {
        SDL_strlcpy(out, "unnamed", (size_t)n);
    }
}

/* The palettes (bit p = palette p of a dw2pal TIM) a DW2 model file's polygons use: the CLUT word
 * of each primitive (as the DW2 3D viewer's NativeModel reads them). 0 when it is not a model. */
static uint64_t model_palettes(const uint8_t *d, uint32_t size) {
    static const int corners[4] = { 4, 4, 3, 3 }, lengths[4] = { 20, 20, 16, 16 };
    uint64_t mask = 0;
    uint32_t n;

    if (size < 36) {
        return 0;
    }
    n = rd32(d + 8);
    if (n < 1 || n > 256 || 12 + 12 * n > size) {
        return 0;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t pos = rd32(d + 12 + 4 * (i + 2 * n));

        for (int k = 0; k < 4; k++) {
            uint32_t count;

            if (pos + 4 > size) {
                return 0;
            }
            count = rd32(d + pos);
            pos += 4;
            if (count > 10000 || pos + count * (uint32_t)lengths[k] > size) {
                return 0;
            }
            for (uint32_t j = 0; j < count; j++) {
                int clut = rd16(d + pos + j * lengths[k] + 4 * corners[k]);
                int x = clut & 63, y = (clut >> 6) & 1023;

                if (x <= 1 && y >= 224 && y < 256) {
                    mask |= 1ull << (((y - 224) * 32 + x * 16) / 16);
                }
            }
            pos += count * (uint32_t)lengths[k];
        }
    }
    return mask;
}

/* TIMs of one file into `dir` as <ID>_<OFFSET>.png (+ _p<N> palettes); returns the count */
static int export_tims(const uint8_t *d, int size, int id, const char *dir, int *made_dir) {
    int count = 0;
    char path[1024];
    uint64_t used = model_palettes(d, (uint32_t)size);

    for (uint32_t o = 0; o + 8 <= (uint32_t)size; o += 4) {
        Tim t;

        if (!tim_at(d, (uint32_t)size, o, &t)) {
            continue;
        }
        if (!*made_dir) {
            mkdirs(dir);
            *made_dir = 1;
        }
        int pals = t.bpp < 16 ? (t.clut_w * t.clut_h) / (t.bpp == 4 ? 16 : 256) : 1;
        int written = 0;
        pals = pals < 1 ? 1 : pals > 64 ? 64 : pals;
        for (int p = 0; p < pals && written < 16; p++) {
            Tim tp = t;
            uint32_t *px;
            int nz = 0, dup = 0, cols = t.bpp == 4 ? 16 : 256;

            if (t.bpp < 16 && !t.dw2pal && t.clut_w > cols && p > 0) {
                break; /* one wide palette row: entries beyond the first block are not separate palettes */
            }
            if (t.dw2pal && used != 0) {
                if (!(used >> p & 1)) {
                    continue; /* a model texture: only the palettes its polygons use */
                }
            } else if (t.bpp < 16 && p > 0) {
                /* skip empty palettes and repeats of an earlier one */
                const uint8_t *pp = d + t.clut_off + (size_t)p * cols * 2;
                for (int k = 0; k < cols * 2; k++) {
                    nz |= pp[k];
                }
                for (int q = 0; q < p && !dup; q++) {
                    dup = memcmp(pp, d + t.clut_off + (size_t)q * cols * 2, (size_t)cols * 2) == 0;
                }
                if (!nz || dup) {
                    continue;
                }
            }
            written++;
            px = tim_rgba(d, &tp, p);
            if (px == NULL) {
                break;
            }
            if (p == 0 && !(t.dw2pal && used != 0)) {
                snprintf(path, sizeof(path), "%s/%04X_%06X.png", dir, id, o);
            } else {
                snprintf(path, sizeof(path), "%s/%04X_%06X_p%d.png", dir, id, o, p);
            }
            write_png_rgba(path, px, t.w, t.h);
            free(px);
        }
        count++;
        o = (t.end & ~3u) - 4; /* past this TIM */
    }
    return count;
}

int Assets_Export(const char *root, void (*progress)(int done, int total, const char *what)) {
    char path[1024], dir[1024];
    int lba, secs, form2;
    unsigned long long fsize;
    int total_tims = 0, files_out = 0, digis_out = 0;
    char kind[FILES][48];
    SDL_IOStream *idx;

    if (!Host_PakFileInfo(0, &lba, &secs, &form2, &fsize)) {
        printf("[assets] no data pack open\n");
        return 1;
    }
    if (!DevData_Ready() && !DevData_Load()) {
        printf("[assets] the game tables could not be read: names stay empty\n");
    }
    memset(kind, 0, sizeof(kind));
    snprintf(dir, sizeof(dir), "%s/Original/files", root);
    mkdirs(dir);
    /* what is known about a file: the tables, the species' model files, the stage overlays */
    SDL_strlcpy(kind[0xC6C], "table DIGIMNDT (Digimon)", sizeof(kind[0]));
    SDL_strlcpy(kind[0x45E], "table ITEMDATA (items)", sizeof(kind[0]));
    SDL_strlcpy(kind[0x25B], "table WAZADATA (skills)", sizeof(kind[0]));
    SDL_strlcpy(kind[0x1FD], "system text", sizeof(kind[0]));
    {
        static const int ovl[] = { 0x190, 0x191, 0xD1E, 0x19A };
        for (unsigned k = 0; k < sizeof(ovl) / sizeof(ovl[0]); k++) {
            SDL_strlcpy(kind[ovl[k]], "stage overlay (program code)", sizeof(kind[0]));
        }
    }
    for (int i = 0; DevData_Ready() && i < DevData_DigiCount(); i++) {
        const DevDigi *g = DevData_DigiAt(i);

        if (g->modelFile > 0 && g->modelFile < FILES && kind[g->modelFile][0] == 0) {
            snprintf(kind[g->modelFile], sizeof(kind[0]), "Digimon model: %s", g->name);
        }
    }

    snprintf(path, sizeof(path), "%s/Original/index.csv", root);
    idx = SDL_IOFromFile(path, "wb");
    if (idx == NULL) {
        printf("[assets] cannot write %s: %s\n", path, SDL_GetError());
        return 1;
    }
    SDL_IOprintf(idx, "id,lba,sectors,bytes,exported,tims,what\n");
    for (int id = 0; id < FILES; id++) {
        int size = 0, tims = 0, made = 0;
        uint8_t *d;

        if (!Host_PakFileInfo(id, &lba, &secs, &form2, &fsize)) {
            continue;
        }
        if (progress != NULL && id % 64 == 0) {
            progress(id, FILES, "files");
        }
        if (form2) {
            SDL_IOprintf(idx, "%04X,%d,%d,%llu,no,0,audio / movie stream (Form 2 sectors, not exported)\n", id, lba, secs,
                         fsize);
            continue;
        }
        d = Host_PakReadFile(id, &size);
        if (d == NULL) {
            SDL_IOprintf(idx, "%04X,%d,%d,%llu,no,0,read error\n", id, lba, secs, fsize);
            continue;
        }
        snprintf(path, sizeof(path), "%s/Original/files/%04X.bin", root, id);
        files_out += write_file(path, d, (size_t)size);
        snprintf(dir, sizeof(dir), "%s/Textures/%04X", root, id);
        tims = export_tims(d, size, id, dir, &made);
        total_tims += tims;
        SDL_IOprintf(idx, "%04X,%d,%d,%d,yes,%d,%s\n", id, lba, secs, size, tims,
                     kind[id][0] ? kind[id] : tims ? "contains textures" : "unknown");
        free(d);
    }
    SDL_CloseIO(idx);

    /* per species and the tables */
    if (DevData_Ready()) {
        SDL_IOStream *js;

        snprintf(dir, sizeof(dir), "%s/GameDatabase", root);
        mkdirs(dir);
        snprintf(path, sizeof(path), "%s/digimon.json", dir);
        js = SDL_IOFromFile(path, "wb");
        if (js != NULL) {
            SDL_IOprintf(js, "[\n");
            for (int i = 0; i < DevData_DigiCount(); i++) {
                const DevDigi *g = DevData_DigiAt(i);

                SDL_IOprintf(js, "%s {\"id\": %d, \"name\": ", i ? ",\n" : "", g->id);
                json_str(js, g->name);
                if (g->hasBase) {
                    SDL_IOprintf(js, ", \"type\": ");
                    json_str(js, DevData_TypeName(g->type));
                    SDL_IOprintf(js, ", \"rank\": ");
                    json_str(js, DevData_RankName(g->rank));
                    SDL_IOprintf(js, ", \"specialty\": ");
                    json_str(js, DevData_SpecialtyName(g->specialty));
                    SDL_IOprintf(js, ", \"learned_skill\": ");
                    json_str(js, DevData_SkillName(g->learnedSkill));
                    SDL_IOprintf(js, ", \"growth_hp_mp_atk_def_spd\": [%d, %d, %d, %d, %d]", g->growth[0], g->growth[1],
                                 g->growth[2], g->growth[3], g->growth[4]);
                    SDL_IOprintf(js, ", \"digivolve_targets\": [");
                    for (int k = 0; k < 4; k++) {
                        SDL_IOprintf(js, "%s", k ? ", " : "");
                        json_str(js, g->dpTargets[k] ? DevData_DigiName(g->dpTargets[k]) : "");
                    }
                    SDL_IOprintf(js, "]");
                } else {
                    SDL_IOprintf(js, ", \"type\": null, \"note\": \"no DIGIMNDT record (unknown)\"");
                }
                if (g->modelFile > 0) {
                    SDL_IOprintf(js, ", \"model_file\": \"%04X\"", g->modelFile);
                } else {
                    SDL_IOprintf(js, ", \"model_file\": null");
                }
                SDL_IOprintf(js, "}");
            }
            SDL_IOprintf(js, "\n]\n");
            SDL_CloseIO(js);
        }
        snprintf(path, sizeof(path), "%s/items.json", dir);
        js = SDL_IOFromFile(path, "wb");
        if (js != NULL) {
            SDL_IOprintf(js, "[\n");
            for (int i = 0; i < DevData_ItemCount(); i++) {
                const DevItem *it = DevData_ItemAt(i);

                SDL_IOprintf(js, "%s {\"id\": %d, \"name\": ", i ? ",\n" : "", it->id);
                json_str(js, it->name);
                SDL_IOprintf(js, ", \"category\": ");
                json_str(js, DevData_CategoryName(it->category));
                SDL_IOprintf(js, ", \"price\": %d, \"description\": ", it->price);
                json_str(js, it->desc);
                SDL_IOprintf(js, "}");
            }
            SDL_IOprintf(js, "\n]\n");
            SDL_CloseIO(js);
        }
        snprintf(path, sizeof(path), "%s/skills.json", dir);
        js = SDL_IOFromFile(path, "wb");
        if (js != NULL) {
            SDL_IOprintf(js, "[\n");
            for (int i = 0; i < DevData_SkillCount(); i++) {
                const DevSkill *k = DevData_SkillAt(i);

                SDL_IOprintf(js, "%s {\"id\": %d, \"name\": ", i ? ",\n" : "", k->id);
                json_str(js, k->name);
                SDL_IOprintf(js, ", \"mp\": %d, \"power\": %d, \"specialty\": ", k->mp, k->power);
                json_str(js, DevData_SpecialtyName(k->specialty));
                SDL_IOprintf(js, ", \"description\": ");
                json_str(js, k->desc);
                SDL_IOprintf(js, "}");
            }
            SDL_IOprintf(js, "\n]\n");
            SDL_CloseIO(js);
        }
        for (int i = 0; i < DevData_DigiCount(); i++) {
            const DevDigi *g = DevData_DigiAt(i);
            char nm[64], sp[1024];
            int size = 0, made = 0;
            uint8_t *d;

            if (!g->hasBase || g->name[0] == 0) {
                continue;
            }
            if (progress != NULL && i % 16 == 0) {
                progress(i, DevData_DigiCount(), "digimon");
            }
            safe_name(g->name, nm, sizeof(nm));
            snprintf(sp, sizeof(sp), "%s/Digimon/%s_%03d", root, nm, g->id);
            snprintf(dir, sizeof(dir), "%s/Metadata", sp);
            mkdirs(dir);
            snprintf(path, sizeof(path), "%s/info.json", dir);
            js = SDL_IOFromFile(path, "wb");
            if (js != NULL) {
                SDL_IOprintf(js, "{\n \"id\": %d,\n \"name\": ", g->id);
                json_str(js, g->name);
                SDL_IOprintf(js, ",\n \"type\": ");
                json_str(js, DevData_TypeName(g->type));
                SDL_IOprintf(js, ",\n \"rank\": ");
                json_str(js, DevData_RankName(g->rank));
                SDL_IOprintf(js, ",\n \"specialty\": ");
                json_str(js, DevData_SpecialtyName(g->specialty));
                SDL_IOprintf(js, ",\n \"learned_skill\": ");
                json_str(js, DevData_SkillName(g->learnedSkill));
                SDL_IOprintf(js, ",\n \"growth_hp_mp_atk_def_spd\": [%d, %d, %d, %d, %d]", g->growth[0], g->growth[1],
                             g->growth[2], g->growth[3], g->growth[4]);
                SDL_IOprintf(js, ",\n \"model_file\": ");
                if (g->modelFile > 0) {
                    SDL_IOprintf(js, "\"%04X\"", g->modelFile);
                } else {
                    SDL_IOprintf(js, "null");
                }
                SDL_IOprintf(js, ",\n \"animations\": \"inside the model file (not split; see the 3D viewer)\"\n}\n");
                SDL_CloseIO(js);
            }
            if (g->modelFile <= 0 || g->modelFile >= FILES) {
                continue;
            }
            d = Host_PakReadFile(g->modelFile, &size);
            if (d == NULL) {
                continue;
            }
            snprintf(dir, sizeof(dir), "%s/Models", sp);
            mkdirs(dir);
            snprintf(path, sizeof(path), "%s/%04X.bin", dir, g->modelFile);
            write_file(path, d, (size_t)size);
            snprintf(dir, sizeof(dir), "%s/Textures", sp);
            export_tims(d, size, g->modelFile, dir, &made);
            free(d);
            digis_out++;
        }
    }
    snprintf(path, sizeof(path), "%s/README.txt", root);
    {
        static const char readme[] =
            "Digimon World 2 - exported game data (from your own disc; do not share)\n"
            "\n"
            "Original/files/<ID>.bin  every disc file unchanged (ID = file id, hex). Original/index.csv: what each is.\n"
            "Textures/<ID>/<ID>_<OFFSET>.png  the TIM images inside file ID at byte OFFSET (RGBA).\n"
            "Digimon/<Name>_<id>/  Metadata/info.json (game table), Models/<ID>.bin, Textures/.\n"
            "GameDatabase/  digimon.json, items.json, skills.json (decoded game tables).\n"
            "\n"
            "Change a file: put it as Mods/files/<ID>.bin next to dw2.exe (not larger than the original).\n"
            "Change a texture: edit the PNG (same size), then: dw2.exe --import-texture <the PNG>\n"
            "  (writes Mods/files/<ID>.bin; colours are mapped to the texture's own palette).\n"
            "Restore: delete the file in Mods/files. Start without mods: dw2.exe --no-mods\n"
            "3D models as glTF: tools/viewer_bridge/export_assets.py (DW2 3D Viewer).\n";
        write_file(path, readme, sizeof(readme) - 1);
    }
    printf("[assets] exported to %s: %d files, %d textures (TIM), %d Digimon folders\n", root, files_out, total_tims,
           digis_out);
    fflush(stdout);
    return files_out > 0 ? 0 : 1;
}

/* ---- texture import ---- */

static uint8_t *png_rgba(const char *path, int *w, int *h) {
    size_t n = 0, zn = 0, out_n = 0;
    uint8_t *f = SDL_LoadFile(path, &n), *z = NULL, *raw, *px = NULL;
    int ct = -1, bd = 0, ch, il = 0;
    size_t p = 8;

    if (f == NULL || n < 8 || memcmp(f, "\x89PNG\r\n\x1a\n", 8) != 0) {
        SDL_free(f);
        return NULL;
    }
    while (p + 12 <= n) {
        uint32_t len = (uint32_t)f[p] << 24 | (uint32_t)f[p + 1] << 16 | (uint32_t)f[p + 2] << 8 | f[p + 3];
        const uint8_t *type = f + p + 4, *data = f + p + 8;

        if (p + 12 + len > n) {
            break;
        }
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            *w = (int)((uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3]);
            *h = (int)((uint32_t)data[4] << 24 | (uint32_t)data[5] << 16 | (uint32_t)data[6] << 8 | data[7]);
            bd = data[8];
            ct = data[9];
            il = data[12];
        } else if (memcmp(type, "IDAT", 4) == 0) {
            uint8_t *nz = realloc(z, zn + len);
            if (nz == NULL) {
                break;
            }
            z = nz;
            memcpy(z + zn, data, len);
            zn += len;
        }
        p += 12 + len;
    }
    SDL_free(f);
    ch = ct == 6 ? 4 : ct == 2 ? 3 : 0;
    if (z == NULL || bd != 8 || ch == 0 || il != 0 || *w <= 0 || *h <= 0 || *w > 4096 || *h > 4096) {
        printf("[assets] %s: only 8-bit RGB / RGBA PNGs without interlace\n", path);
        free(z);
        return NULL;
    }
    raw = tinfl_decompress_mem_to_heap(z, zn, &out_n, TINFL_FLAG_PARSE_ZLIB_HEADER);
    free(z);
    if (raw == NULL || out_n < (size_t)*h * (1 + (size_t)*w * ch)) {
        mz_free(raw);
        return NULL;
    }
    px = malloc((size_t)*w * *h * 4);
    if (px != NULL) {
        size_t stride = (size_t)*w * ch;
        uint8_t *prev = calloc(1, stride), *cur = malloc(stride);

        for (int y = 0; y < *h && prev && cur; y++) {
            const uint8_t *src = raw + (size_t)y * (stride + 1);
            int ft = src[0];

            for (size_t x = 0; x < stride; x++) {
                int a = x >= (size_t)ch ? cur[x - ch] : 0, b = prev[x], c = x >= (size_t)ch ? prev[x - ch] : 0;
                int v = src[1 + x], pr;

                switch (ft) {
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: {
                    int pp = a + b - c, pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
                    pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                    v += pr;
                    break;
                }
                default: break;
                }
                cur[x] = (uint8_t)v;
            }
            for (int x = 0; x < *w; x++) {
                uint8_t *o = px + ((size_t)y * *w + x) * 4;
                o[0] = cur[x * ch];
                o[1] = cur[x * ch + 1];
                o[2] = cur[x * ch + 2];
                o[3] = ch == 4 ? cur[x * ch + 3] : 255;
            }
            memcpy(prev, cur, stride);
        }
        free(prev);
        free(cur);
    }
    mz_free(raw);
    return px;
}

static int c15(const uint8_t *p) {
    if (p[3] < 128) {
        return 0;
    }
    int c = (p[0] * 31 + 127) / 255 | ((p[1] * 31 + 127) / 255) << 5 | ((p[2] * 31 + 127) / 255) << 10;
    return c == 0 ? 0x8000 : c; /* opaque black: the STP bit keeps it from being transparent */
}

int Assets_ImportTexture(const char *png_path) {
    const char *base = SDL_GetBasePath();
    const char *name = strrchr(png_path, '/');
    const char *bs = strrchr(png_path, '\\');
    char mods[1024], path[1024];
    unsigned id = 0, off = 0;
    int w = 0, h = 0, size = 0, changed = 0;
    uint8_t *px, *d = NULL;
    Tim t;

    name = bs != NULL && (name == NULL || bs > name) ? bs + 1 : name != NULL ? name + 1 : png_path;
    int pal = 0;
    {
        const char *pp = strstr(name, "_p");
        if (pp != NULL) {
            pal = atoi(pp + 2);
        }
    }
    if (sscanf(name, "%4x_%6x", &id, &off) != 2 || id >= FILES) {
        printf("[assets] %s: the name must be <ID>_<OFFSET>.png as exported (e.g. 0C6C_000340.png)\n", name);
        return 1;
    }
    px = png_rgba(png_path, &w, &h);
    if (px == NULL) {
        printf("[assets] cannot read %s\n", png_path);
        return 1;
    }
    snprintf(mods, sizeof(mods), "%sMods/files", base != NULL ? base : "");
    snprintf(path, sizeof(path), "%s/%04X.bin", mods, id);
    {
        size_t n = 0;
        void *m = SDL_LoadFile(path, &n); /* edit the current mod file when there is one */

        if (m != NULL) {
            d = malloc(n);
            memcpy(d, m, n);
            size = (int)n;
            SDL_free(m);
        } else {
            d = Host_PakReadFile((int)id, &size);
        }
    }
    if (d == NULL || !tim_at(d, (uint32_t)size, off, &t)) {
        printf("[assets] file %04X has no TIM at offset 0x%X\n", id, off);
        free(px);
        free(d);
        return 1;
    }
    if (t.w != w || t.h != h) {
        printf("[assets] %s is %dx%d, the texture is %dx%d: same size needed (no HD replacement in this version)\n",
               name, w, h, t.w, t.h);
        free(px);
        free(d);
        return 1;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *p = px + ((size_t)y * w + x) * 4;
            int want = c15(p), v;

            if (t.bpp == 16) {
                v = want;
                uint8_t *q = d + t.img_off + (size_t)y * w * 2 + x * 2;
                if (rd16(q) != v) {
                    q[0] = (uint8_t)v;
                    q[1] = (uint8_t)(v >> 8);
                    changed++;
                }
                continue;
            }
            /* nearest entry of the palette the PNG was exported with (_p<N>, else the first),
               transparent to an entry that is 0 */
            int n = t.bpp == 4 ? 16 : 256, best = -1;
            long bestd = 1L << 30;
            for (int i = 0; i < n && pal * n + i < t.clut_w * t.clut_h; i++) {
                int c = rd16(d + t.clut_off + ((size_t)pal * n + i) * 2);
                long dd;

                if ((want == 0) != (c == 0)) {
                    continue;
                }
                dd = (long)((c & 31) - (want & 31)) * ((c & 31) - (want & 31)) +
                     (long)(((c >> 5) & 31) - ((want >> 5) & 31)) * (((c >> 5) & 31) - ((want >> 5) & 31)) +
                     (long)(((c >> 10) & 31) - ((want >> 10) & 31)) * (((c >> 10) & 31) - ((want >> 10) & 31));
                if (dd < bestd) {
                    bestd = dd;
                    best = i;
                }
            }
            if (best < 0) {
                best = 0;
            }
            v = tim_index(d, &t, x, y);
            if (v != best) {
                changed++;
                if (t.bpp == 4) {
                    uint8_t *q = d + t.img_off + (size_t)y * (w / 2) + x / 2;
                    *q = (uint8_t)((*q & ~(15 << ((x & 1) * 4))) | best << ((x & 1) * 4));
                } else {
                    d[t.img_off + (size_t)y * w + x] = (uint8_t)best;
                }
            }
        }
    }
    free(px);
    mkdirs(mods);
    if (!write_file(path, d, (size_t)size)) {
        free(d);
        return 1;
    }
    free(d);
    printf("[assets] %s -> %s: %d pixels changed (texture %dx%d, %d bpp at 0x%X)\n", name, path, changed, w, h, t.bpp,
           off);
    return 0;
}
