/* Disc assets at run time (dw2-pc-port): the INCLUDE_BIN blobs filled from the player's own disc.
 *
 * The native build links these blobs from assets/ (tools/build_dw2.py extracts them from the disc,
 * configs/USA/include_bin.txt and include_bin_native.txt). With tools/build_native.py --no-disc
 * they are zero-filled placeholders, so a build machine needs no game data and the exe ships none.
 * Before any game code runs, this file copies the real bytes in: from SLUS_011.93 (the verified
 * dw2.pak.slus next to the pack) and from the stage overlays inside dw2.pak.
 *
 * Offsets and sizes are exactly the include_bin lines. A build made WITH the disc assets gets the
 * same bytes again; the log line then reports every table as identical (the self-check). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/host.h"

extern unsigned char Rand_Table[], Cd_FileLba[], Cd_FileSectors[], Psyq_RatanTable[];
extern unsigned char Snd_RetailRodata[], Snd_RetailData[], Ovl_LoadArea[];
extern unsigned char Stg11_CardIconTim1[], Stg11_CardIconTim2[], Stg11_CardIconTim3[];
extern unsigned char Stg40_AutomapClut[], Stg00_FontGlyphs[], Stg10_VlcTablePacked[];

#define FROM_EXE (-1)

typedef struct {
    const char *name;
    unsigned char *dst;
    int file;      /* FROM_EXE or the dw2.pak file id of the stage overlay */
    uint32_t offset;
    uint32_t size;
} DiscAsset;

/* Stage overlay file ids: tools/dw2pack.py OVERLAYS (Ovl_FileIds). */
#define STAG0000 0x190
#define STAG1000 0x191
#define STAG1100 0xD1E
#define STAG4000 0x19A

static const DiscAsset exe_assets[] = {
    { "Rand_Table", Rand_Table, FROM_EXE, 0x31F04, 0x2000 },
    { "Cd_FileLba", Cd_FileLba, FROM_EXE, 0x33F94, 0x396C },
    { "Cd_FileSectors", Cd_FileSectors, FROM_EXE, 0x37900, 0x1CB6 },
    { "Psyq_RatanTable", Psyq_RatanTable, FROM_EXE, 0x3E5C0, 0x802 },
    { "Snd_RetailRodata", Snd_RetailRodata, FROM_EXE, 0x800, 0x1000 },
    { "Snd_RetailData", Snd_RetailData, FROM_EXE, 0x28800, 0x2B360 },
    { "Ovl_LoadArea", Ovl_LoadArea, FROM_EXE, 0x53B60, 0x4B4A0 },
};

static const DiscAsset ovl_assets[] = {
    { "Stg11_CardIconTim1", Stg11_CardIconTim1, STAG1100, 0x4ED0, 0xC0 },
    { "Stg11_CardIconTim2", Stg11_CardIconTim2, STAG1100, 0x4F90, 0xC0 },
    { "Stg11_CardIconTim3", Stg11_CardIconTim3, STAG1100, 0x5050, 0xC0 },
    { "Stg40_AutomapClut", Stg40_AutomapClut, STAG4000, 0xF5E8, 0x40 },
    { "Stg00_FontGlyphs", Stg00_FontGlyphs, STAG0000, 0x5770, 0x200 },
    { "Stg10_VlcTablePacked", Stg10_VlcTablePacked, STAG1000, 0x2078, 0xE10 },
};

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

static int copy_in(const DiscAsset *a, const unsigned char *src, uint32_t src_size, int *same) {
    if ((uint64_t)a->offset + a->size > src_size) {
        printf("[disc] %s: source too short (0x%X + 0x%X > 0x%X)\n", a->name, a->offset, a->size, src_size);
        return -1;
    }
    if (memcmp(a->dst, src + a->offset, a->size) == 0) {
        (*same)++;
    } else {
        memcpy(a->dst, src + a->offset, a->size);
    }
    return 0;
}

/* SLUS_011.93 tables. Called by host/pak.c with the verified exe, before it checks the pack index
 * against Cd_FileLba / Cd_FileSectors. */
int Host_DiscAssetsFillExe(const unsigned char *exe, uint32_t size) {
    int same = 0;
    int i;

    for (i = 0; i < COUNT(exe_assets); i++) {
        if (copy_in(&exe_assets[i], exe, size, &same) != 0) {
            return -1;
        }
    }
    printf("[disc] %d exe tables loaded (%d already identical)\n", COUNT(exe_assets), same);
    return 0;
}

/* Stage overlay tables, read from the verified pack through `read` (file id -> data, size). */
int Host_DiscAssetsFillOverlays(unsigned char *(*read)(int id, uint32_t *size)) {
    int same = 0;
    int i;

    for (i = 0; i < COUNT(ovl_assets); i++) {
        uint32_t size = 0;
        unsigned char *data = read(ovl_assets[i].file, &size);
        int r;

        if (data == NULL) {
            printf("[disc] %s: cannot read file 0x%03X from the pack\n", ovl_assets[i].name, ovl_assets[i].file);
            return -1;
        }
        r = copy_in(&ovl_assets[i], data, size, &same);
        free(data);
        if (r != 0) {
            return -1;
        }
    }
    printf("[disc] %d overlay tables loaded (%d already identical)\n", COUNT(ovl_assets), same);
    return 0;
}
