#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/host.h"
#include "host/host_sdl.h"
#include "host/sha256.h"

/* dw2.pak: the game's disc files, built from the player's own disc image on the first start
 * (host/pakbuild.c) or by tools/dw2pack.py (format: doc/PACK_FORMAT.md). Opened and checked once
 * at start against the manifest compiled in from configs/USA/pack_manifest.txt; a pack that is
 * missing or fails the check is (re)built when a disc image is found, otherwise the program stops
 * before Sys_Main. libcd reads sectors through Host_PakSector. */

#define FORMAT_DATA 0 /* Form 1 only: 2048 bytes per sector */
#define FORMAT_RAW 1  /* has Form 2 sectors: 2336-byte sector body per sector */

/* The game's file table (main/cd.c, INCLUDE_BIN of the exe's table). */
extern int Cd_FileLba[];
extern unsigned short Cd_FileSectors[];

/* configs/USA/pack_manifest.txt, NUL-terminated. */
#define PAK_ASM_STR_(x) #x
#define PAK_ASM_XSTR_(x) PAK_ASM_STR_(x)
__asm__(".data\n"
        "    .globl " PAK_ASM_XSTR_(__USER_LABEL_PREFIX__) "Pak_Manifest\n"
        PAK_ASM_XSTR_(__USER_LABEL_PREFIX__) "Pak_Manifest:\n"
        "    .incbin \"configs/USA/pack_manifest.txt\"\n"
        "    .byte 0\n"
        ".text");
extern const char Pak_Manifest[];

typedef struct {
    int lba;
    int sectors;
    int format;
    Uint64 offset;
    Uint64 size;
    Uint8 sha[32];
} PakFile;

static SDL_IOStream *pak;
static PakFile files[PAK_FILES];
static unsigned short by_lba[PAK_FILES]; /* file ids sorted by LBA */
static int no_window;
static char why[512]; /* open_pak's reason for a refusal */
static char pak_path[1024]; /* Host_PakReadFile opens its own stream on it */

/* dw2-pc-port: disc tables filled at run time (host/discassets.c). */
int Host_DiscAssetsFillExe(const unsigned char *exe, Uint32 size);
int Host_DiscAssetsFillOverlays(unsigned char *(*read)(int id, Uint32 *size));
static unsigned char *read_data_file(int id, Uint32 *size);
static void load_mods(void);

static Uint32 rd32(const Uint8 *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (Uint32)p[3] << 24;
}

static Uint64 rd64(const Uint8 *p) {
    return rd32(p) | (Uint64)rd32(p + 4) << 32;
}

static int hexbyte(const char *s) {
    int v = 0;
    int i;

    for (i = 0; i < 2; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v |= c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v |= c - 'A' + 10;
        } else {
            return -1;
        }
    }
    return v;
}

static int hash32(const char *s, Uint8 *out) {
    int i;

    for (i = 0; i < 32; i++) {
        int v = hexbyte(s + i * 2);
        if (v < 0) {
            return -1;
        }
        out[i] = (Uint8)v;
    }
    return 0;
}

/* The "es" manifest lines: the files Alemito's Spanish translation changes, as translated. */
static Uint8 es_sha[PAK_FILES][32];
static char es_file[PAK_FILES];
int Pak_EsFiles;

/* The manifest: the exe line (SLUS_011.93, which holds the file table; host/pakbuild.c checks
 * it), the 0xE5B file hashes and the "es" lines. */
void Pak_ParseManifest(Uint8 *exe_sha, Uint8 (*sha)[32]) {
    const char *p = Pak_Manifest;
    int seen = 0;
    int exe_seen = 0;
    int es;

    while (*p != 0) {
        const char *line = p;
        const char *end = strchr(p, '\n');
        char *after;
        unsigned long id;

        p = end != NULL ? end + 1 : line + strlen(line);
        if (*line == '#' || *line == '\n' || *line == '\r') {
            continue;
        }
        if (strncmp(line, "exe ", 4) == 0) {
            if (hash32(line + 4, exe_sha) < 0) {
                fprintf(stderr, "[pak] bad manifest hash for exe\n");
                exit(1);
            }
            exe_seen = 1;
            continue;
        }
        es = strncmp(line, "es ", 3) == 0;
        id = strtoul(line + es * 3, &after, 16);
        if (after == line + es * 3 || *after != ' ' || id >= PAK_FILES) {
            fprintf(stderr, "[pak] bad manifest line: %.40s\n", line);
            exit(1);
        }
        if (hash32(after + 1, es ? es_sha[id] : sha[id]) < 0) {
            fprintf(stderr, "[pak] bad manifest hash for 0x%03lX\n", id);
            exit(1);
        }
        if (es) {
            Pak_EsFiles += !es_file[id];
            es_file[id] = 1;
        } else {
            seen++;
        }
    }
    if (seen != PAK_FILES || !exe_seen) {
        fprintf(stderr, "[pak] manifest has %d files%s, want %d and the exe\n", seen, exe_seen ? "" : ", no exe",
                PAK_FILES);
        exit(1);
    }
}

/* File `id` with SHA-256 `sha`: 1 the English disc's file, 2 the Spanish translation's (an "es"
 * file), 0 neither. A disc or pack is English, or Spanish with all Pak_EsFiles translated. */
int Pak_FileMatch(int id, const Uint8 *sha, Uint8 (*manifest)[32]) {
    if (memcmp(sha, manifest[id], 32) == 0) {
        return 1;
    }
    return es_file[id] && memcmp(sha, es_sha[id], 32) == 0 ? 2 : 0;
}

/* Stops the program: `path`, the reason, and how to get a pack. */
static void refuse(const char *path, const char *reason) {
    char msg[4096];

    snprintf(msg, sizeof(msg),
             "%s: %s\n\n"
             "dw2.exe builds its data pack (dw2.pak) from your own Digimon World 2 disc image\n"
             "(USA, SLUS-01193; a raw dump: .bin + .cue, .img or a raw .iso).\n"
             "Put the image next to dw2.exe and start it again, or pass it with --disc <path>.\n",
             path, reason);
    fprintf(stderr, "[pak] %s", msg);
    if (!no_window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 2: data pack", msg, NULL);
    }
    exit(1);
}

static int bad(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(why, sizeof(why), fmt, ap);
    va_end(ap);
    if (pak != NULL) {
        SDL_CloseIO(pak);
        pak = NULL;
    }
    return -1;
}

static int cmp_lba(const void *a, const void *b) {
    return files[*(const unsigned short *)a].lba - files[*(const unsigned short *)b].lba;
}

/* Opens and checks the pack; 0, or -1 with the reason in `why` (nothing left open). hash_data 0
 * skips hashing the file data: a pack host/pakbuild.c has just written and read back. */
/* dw2-pc-port: SLUS_011.93 from <pack>.slus (written by the pack build), checked against the
 * manifest's exe SHA-256, then its tables go into the game (host/discassets.c). */
static int load_exe(const char *path, const Uint8 *exe_sha) {
    char name[1100];
    size_t size = 0;
    Uint8 *exe;
    Uint8 digest[32];
    Sha256 s;
    int r;

    snprintf(name, sizeof(name), "%s.slus", path);
    exe = SDL_LoadFile(name, &size);
    if (exe == NULL) {
        return bad("%s missing (the pack is rebuilt from the disc once)", name);
    }
    Sha256_Init(&s);
    Sha256_Update(&s, exe, size);
    Sha256_Final(&s, digest);
    if (memcmp(digest, exe_sha, 32) != 0) {
        SDL_free(exe);
        return bad("%s is not the retail SLUS_011.93", name);
    }
    r = Host_DiscAssetsFillExe(exe, (Uint32)size);
    SDL_free(exe);
    return r == 0 ? 0 : bad("SLUS_011.93 tables do not fit");
}

/* A Form 1 file's bytes from the open pack (overlay tables at startup). */
static unsigned char *read_data_file(int id, Uint32 *size) {
    const PakFile *f;
    unsigned char *buf;

    *size = 0;
    if (pak == NULL || id < 0 || id >= PAK_FILES || files[id].format != FORMAT_DATA) {
        return NULL;
    }
    f = &files[id];
    buf = malloc((size_t)f->size);
    if (buf == NULL || SDL_SeekIO(pak, (Sint64)f->offset, SDL_IO_SEEK_SET) < 0 ||
        SDL_ReadIO(pak, buf, (size_t)f->size) != (size_t)f->size) {
        free(buf);
        return NULL;
    }
    *size = (Uint32)f->size;
    return buf;
}

static int open_pak(const char *path, int hash_data) {
    static Uint8 manifest[PAK_FILES][32];
    static Uint8 index[PAK_FILES * PAK_ENTRY_SIZE];
    Uint8 exe_sha[32];
    Uint8 header[PAK_HEADER_SIZE];
    Uint8 digest[32];
    Uint8 *buf;
    Sha256 all;
    Uint64 size;
    Uint64 total = 0;
    Uint64 t0 = SDL_GetTicksNS();
    int es = 0;
    int i;

    Pak_ParseManifest(exe_sha, manifest);
    if (load_exe(path, exe_sha) != 0) {
        return -1;
    }
    pak = SDL_IOFromFile(path, "rb");
    if (pak == NULL) {
        return bad("cannot open (%s)", SDL_GetError());
    }
    size = (Uint64)SDL_GetIOSize(pak);
    if (SDL_ReadIO(pak, header, sizeof(header)) != sizeof(header) || memcmp(header, PAK_MAGIC, 8) != 0) {
        return bad("not a dw2.pak file");
    }
    if (rd32(header + 0x08) != PAK_VERSION || rd32(header + 0x0C) != PAK_HEADER_SIZE ||
        rd32(header + 0x10) != PAK_FILES || rd32(header + 0x18) != PAK_ENTRY_SIZE || rd32(header + 0x1C) != 0) {
        return bad("pack version %u with %u files, this build reads version %d with %d files (rebuild the pack)",
                   rd32(header + 0x08), rd32(header + 0x10), PAK_VERSION, PAK_FILES);
    }
    if (rd64(header + 0x28) != size) {
        return bad("size %llu, the header says %llu (truncated?)", (unsigned long long)size,
                   (unsigned long long)rd64(header + 0x28));
    }
    if (SDL_SeekIO(pak, rd32(header + 0x14), SDL_IO_SEEK_SET) < 0 ||
        SDL_ReadIO(pak, index, sizeof(index)) != sizeof(index)) {
        return bad("cannot read the index");
    }
    Sha256_Init(&all);
    for (i = 0; i < PAK_FILES; i++) {
        const Uint8 *e = index + i * PAK_ENTRY_SIZE;
        PakFile *f = &files[i];

        f->lba = (int)rd32(e + 0x04);
        f->sectors = (int)rd32(e + 0x08);
        f->format = (int)rd32(e + 0x0C);
        f->offset = rd64(e + 0x10);
        f->size = rd64(e + 0x18);
        memcpy(f->sha, e + 0x20, 32);
        if (rd32(e) != (Uint32)i || f->lba != Cd_FileLba[i] || f->sectors != Cd_FileSectors[i] ||
            (f->format != FORMAT_DATA && f->format != FORMAT_RAW) ||
            f->size != (Uint64)f->sectors * (f->format == FORMAT_RAW ? 2336 : 2048) || f->offset > size ||
            f->size > size - f->offset) {
            return bad("index entry 0x%03X does not fit the game's file table", i);
        }
        switch (Pak_FileMatch(i, f->sha, manifest)) {
        case 0:
            return bad("file 0x%03X does not match the manifest (modified or other disc)", i);
        case 2:
            es++;
            break;
        }
        Sha256_Update(&all, f->sha, 32);
        by_lba[i] = (unsigned short)i;
        total += f->size;
    }
    Sha256_Final(&all, digest);
    if (memcmp(digest, header + 0x30, 32) != 0) {
        return bad("content hash does not match the index");
    }
    if (es != 0 && es != Pak_EsFiles) {
        return bad("%d of the %d Spanish files translated (mixed or other translation)", es, Pak_EsFiles);
    }

    /* Hash every file's data: a pack whose bytes were altered after it was written. */
    buf = hash_data ? malloc(1 << 20) : NULL;
    for (i = 0; hash_data && i < PAK_FILES; i++) {
        PakFile *f = &files[i];
        Uint64 left = f->size;
        Sha256 s;

        Sha256_Init(&s);
        if (buf == NULL || SDL_SeekIO(pak, (Sint64)f->offset, SDL_IO_SEEK_SET) < 0) {
            free(buf);
            return bad("cannot seek to file 0x%03X", i);
        }
        while (left != 0) {
            size_t n = left < (1 << 20) ? (size_t)left : (1 << 20);
            if (SDL_ReadIO(pak, buf, n) != n) {
                free(buf);
                return bad("cannot read file 0x%03X", i);
            }
            Sha256_Update(&s, buf, n);
            left -= n;
        }
        Sha256_Final(&s, digest);
        if (memcmp(digest, f->sha, 32) != 0) {
            free(buf);
            return bad("file 0x%03X is damaged or altered (SHA-256 differs)", i);
        }
    }
    free(buf);
    qsort(by_lba, PAK_FILES, sizeof(by_lba[0]), cmp_lba);
    SDL_strlcpy(pak_path, path, sizeof(pak_path));
    if (Host_DiscAssetsFillOverlays(read_data_file) != 0) {
        return bad("cannot load the stage overlay tables");
    }
    printf("[pak] %s: %d files, %.1f MB, all match the manifest (%s in %.2f s)%s\n", path, PAK_FILES, total / 1e6,
           hash_data ? "checked" : "index checked, data read back by the build", (SDL_GetTicksNS() - t0) / 1e9,
           es != 0 ? ", Spanish translation by Alemito" : "");
    return 0;
}

static int exists(const char *path) {
    SDL_PathInfo info;

    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

void Host_PakOpen(const char *path, const char *disc, int no_win) {
    char *cand[3] = { NULL, NULL, NULL };
    char *pref = SDL_GetPrefPath("", "DW2-Online");
    const char *base = SDL_GetBasePath();
    char failed_path[1024] = "";
    char failed_why[512] = "";
    char built[1024];
    char err[2048];
    char *image;
    int n = 0;
    int i;

    no_window = no_win;
    /* --pak PATH, else dw2.pak next to the exe, in the user data folder, then build/native/dw2.pak
     * from the current folder (the repo root, where tools/build_native.py --run starts it). */
    if (path != NULL) {
        cand[n++] = SDL_strdup(path);
    } else {
        if (base != NULL) {
            SDL_asprintf(&cand[n++], "%sdw2.pak", base);
        }
        if (pref != NULL) {
            SDL_asprintf(&cand[n++], "%sdw2.pak", pref);
        }
        cand[n++] = SDL_strdup("build/native/dw2.pak");
    }
    for (i = 0; i < n; i++) {
        if (cand[i] == NULL || (path == NULL && !exists(cand[i]))) {
            continue;
        }
        if (open_pak(cand[i], 1) == 0) {
            goto done;
        }
        printf("[pak] %s: %s\n", cand[i], why);
        if (failed_path[0] == 0) {
            SDL_strlcpy(failed_path, cand[i], sizeof(failed_path));
            SDL_strlcpy(failed_why, why, sizeof(failed_why));
        }
    }

    /* No usable pack: build one from the disc image. A pack that failed is rebuilt in place,
     * else it goes next to the exe (the user data folder if that folder is not writable). */
    image = Host_DiscFind(disc, no_window, err, sizeof(err));
    if (image == NULL) {
        if (failed_path[0] != 0) {
            char msg[3072];

            snprintf(msg, sizeof(msg), "%s\n\nNo disc image to rebuild it from: %s", failed_why, err);
            refuse(failed_path, msg);
        }
        refuse(cand[0] != NULL ? cand[0] : "dw2.pak", err);
    }
    /* With no --pak and both folders known, cand[0] is next to the exe and cand[1] in the user
     * data folder. */
    if (Host_PakBuild(image, failed_path[0] != 0 ? failed_path : cand[0],
                      failed_path[0] == 0 && path == NULL && base != NULL && pref != NULL ? cand[1] : NULL,
                      no_window, built, sizeof(built), err, sizeof(err)) != 0) {
        refuse(image, err);
    }
    SDL_free(image);
    if (open_pak(built, 0) != 0) {
        refuse(built, why);
    }
done:
    for (i = 0; i < n; i++) {
        SDL_free(cand[i]);
    }
    SDL_free(pref);
    load_mods();
}

/* dw2-pc-port mod layer: Mods/files/<id>.bin (file id in hex, e.g. 0C6C.bin) next to the exe or
 * in the user data folder replaces that disc file while the game reads it. The pack itself stays
 * untouched and verified; an override may not be larger than the original file (the game's file
 * table gives each file its sector count) and Form 2 files (audio, movies) cannot be replaced.
 * --no-mods (Host_PakNoMods) turns the layer off. Deleting the file restores the original. */
/* Viewer overlay API 2: original pack/index stay immutable. Larger Form-1
 * replacements get virtual CD extents and adjusted game allocation counts. */
static Uint8 *mod_data[PAK_FILES];
static Uint64 mod_capacity[PAK_FILES];
static int mod_lba[PAK_FILES];
static int mod_sectors[PAK_FILES];
static int virtual_next, virtual_first;
static Uint64 mod_bytes;
#define MOD_FILE_LIMIT (2u << 20)
#define MOD_MEMORY_LIMIT (64u << 20)

static int mod_at(int lba) {
    if (virtual_first == 0 || lba < virtual_first) return -1;
    for (int id = 0; id < PAK_FILES; id++)
        if (mod_lba[id] && lba >= mod_lba[id] && lba < mod_lba[id] + mod_sectors[id]) return id;
    return -1;
}
static int no_mods;
static int mod_count;

void Host_PakNoMods(void) {
    no_mods = 1;
}

int Host_PakHasMod(int id) {
    return id >= 0 && id < PAK_FILES && mod_data[id] != NULL;
}

static void load_mod_dir(const char *root) {
    char *dir = NULL;
    char **names;
    int count = 0;

    SDL_asprintf(&dir, "%sMods/files", root);
    names = dir != NULL ? SDL_GlobDirectory(dir, "*.bin", SDL_GLOB_CASEINSENSITIVE, &count) : NULL;
    for (int k = 0; k < count; k++) {
        char *end = NULL;
        long id = strtol(names[k], &end, 16);
        char *path = NULL;
        size_t size = 0;
        void *data;

        if (end == NULL || SDL_strcasecmp(end, ".bin") != 0 || id < 0 || id >= PAK_FILES) {
            printf("[mods] %s/%s: not a file id name (like 0C6C.bin), skipped\n", dir, names[k]);
            continue;
        }
        if (files[id].format != FORMAT_DATA) {
            printf("[mods] %s/%s: file 0x%03lX is audio / movie data, cannot be replaced\n", dir, names[k], id);
            continue;
        }
        SDL_asprintf(&path, "%s/%s", dir, names[k]);
        data = path != NULL ? SDL_LoadFile(path, &size) : NULL;
        if (data == NULL || size == 0 || size > MOD_FILE_LIMIT) {
            printf("[mods] %s: %s (original %llu bytes), skipped\n", path ? path : names[k],
                   data == NULL ? "cannot read" : size == 0 ? "empty" : "larger than 2 MiB",
                   (unsigned long long)files[id].size);
            SDL_free(data);
            SDL_free(path);
            continue;
        }
        Uint64 capacity = size > files[id].size ? ((Uint64)size + 2047) & ~(Uint64)2047 : files[id].size;
        int extended = capacity > files[id].size;
        int sectors = (int)(capacity / 2048);
        if (mod_bytes - mod_capacity[id] + capacity > MOD_MEMORY_LIMIT ||
            (extended && virtual_next + sectors >= 450000)) {
            printf("[mods] %s: memory / virtual CD limit exceeded, skipped\n", path);
            SDL_free(data); SDL_free(path); continue;
        }
        Uint8 *replacement = calloc(1, (size_t)capacity);
        if (replacement != NULL) {
            memcpy(replacement, data, size);
            mod_bytes = mod_bytes - mod_capacity[id] + capacity;
            free(mod_data[id]); mod_data[id] = replacement; mod_capacity[id] = capacity;
            mod_lba[id] = extended ? virtual_next : 0;
            mod_sectors[id] = sectors;
            if (extended) virtual_next += sectors;
            Cd_FileLba[id] = extended ? mod_lba[id] : files[id].lba;
            Cd_FileSectors[id] = (unsigned short)sectors;
            printf("[mods] API 2 file 0x%03lX replaced by %s (%zu bytes, allocation %llu)\n", id, path, size, (unsigned long long)capacity);
        }
        SDL_free(data);
        SDL_free(path);
    }
    SDL_free(names);
    SDL_free(dir);
}

static void load_mods(void) {
    const char *base = SDL_GetBasePath();
    char *pref = SDL_GetPrefPath("", "DW2-Online");

    virtual_next = 1;
    for (int k = 0; k < PAK_FILES; k++)
        if (files[k].lba + files[k].sectors > virtual_next) virtual_next = files[k].lba + files[k].sectors;
    virtual_first = virtual_next;
    if (no_mods) {
        printf("[mods] off (--no-mods)\n");
    } else {
        if (pref != NULL) {
            load_mod_dir(pref);
        }
        if (base != NULL) {
            load_mod_dir(base); /* next to the exe wins */
        }
        mod_count = 0;
        for (int k = 0; k < PAK_FILES; k++) {
            mod_count += mod_data[k] != NULL;
        }
        printf("[mods] %d file override%s active\n", mod_count, mod_count == 1 ? "" : "s");
    }
    SDL_free(pref);
    Host_ModConfLoad(no_mods); /* Mods/digivolve_levels.json, Mods/battle_camera.json */
}

/* dw2-pc-port language layer (host/lang.c): a patched copy of file `id` served like a mod file
 * (not counted as a user mod) */
static int lang_count;

void Host_PakSetLayer(int id, const Uint8 *data, int size) {
    if (id < 0 || id >= PAK_FILES || files[id].format != FORMAT_DATA || size <= 0 || (Uint64)size > files[id].size) {
        return;
    }
    if (mod_data[id] == NULL) {
        mod_data[id] = calloc(1, (size_t)files[id].size);
        mod_capacity[id] = files[id].size;
        mod_bytes += files[id].size;
        lang_count++;
    }
    if (mod_data[id] != NULL) {
        memcpy(mod_data[id], data, (size_t)size);
    }
}

int Host_PakModCount(void) {
    return mod_count;
}

/* file `id` as the game reads it (a mod / language copy when there is one), malloc'd */
void *Host_PakReadFileLayered(int id, int *size) {
    void *buf;

    if (id >= 0 && id < PAK_FILES && mod_data[id] != NULL) {
        buf = malloc((size_t)mod_capacity[id] + 1);
        if (buf != NULL) {
            memcpy(buf, mod_data[id], (size_t)mod_capacity[id]);
            ((Uint8 *)buf)[mod_capacity[id]] = 0xFF;
            *size = (int)mod_capacity[id];
        }
        return buf;
    }
    return Host_PakReadFile(id, size);
}

int Host_PakFileInfo(int id, int *lba, int *sectors, int *form2, unsigned long long *size) {
    if (pak_path[0] == 0 || id < 0 || id >= PAK_FILES) {
        return 0;
    }
    *lba = files[id].lba;
    *sectors = files[id].sectors;
    *form2 = files[id].format != FORMAT_DATA;
    *size = files[id].size;
    return 1;
}

/* The file holding sector `lba` (binary search over the LBA-sorted index), or -1. */
static int file_at(int lba) {
    int lo = 0;
    int hi = PAK_FILES - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const PakFile *f = &files[by_lba[mid]];
        if (lba < f->lba) {
            hi = mid - 1;
        } else if (lba >= f->lba + f->sectors) {
            lo = mid + 1;
        } else {
            return by_lba[mid];
        }
    }
    return -1;
}

int Host_PakFileAt(int lba, int *sectors) {
    int id = mod_at(lba);
    if (id >= 0) { *sectors = mod_sectors[id]; return id; }
    id = file_at(lba);
    *sectors = id < 0 ? 0 : files[id].sectors;
    return id;
}

int Host_PakSector(int lba, unsigned char *body) {
    static const unsigned char form1_subheader[8] = { 0, 0, 0x08, 0, 0, 0, 0x08, 0 };
    int id = file_at(lba);
    const PakFile *f;
    Uint64 at;

    memset(body, 0, 2336);
    int extended = mod_at(lba);
    if (extended >= 0) {
        memcpy(body, form1_subheader, 8);
        memcpy(body + 8, mod_data[extended] + (size_t)(lba-mod_lba[extended])*2048, 2048);
        return extended;
    }
    if (id < 0) {
        return -1;
    }
    f = &files[id];
    if (f->format == FORMAT_RAW) {
        at = f->offset + (Uint64)(lba - f->lba) * 2336;
        SDL_SeekIO(pak, (Sint64)at, SDL_IO_SEEK_SET);
        SDL_ReadIO(pak, body, 2336);
    } else {
        at = f->offset + (Uint64)(lba - f->lba) * 2048;
        memcpy(body, form1_subheader, 8);
        if (mod_data[id] != NULL) {
            memcpy(body + 8, mod_data[id] + (size_t)(lba - f->lba) * 2048, 2048); /* dw2-pc-port mod layer */
        } else {
            SDL_SeekIO(pak, (Sint64)at, SDL_IO_SEEK_SET);
            SDL_ReadIO(pak, body + 8, 2048);
        }
    }
    return id;
}

#if 1 /* dw2-pc-port: also release (cheat menu names, asset export) */
/* Dev tools (PD.7), main thread: the whole Form 1 file `id` in a malloc'd buffer (*size bytes),
 * read through a stream of its own, so the game thread's sector reads are not disturbed. NULL
 * before Host_PakOpen, for Form 2 files and on read errors. The pack was checked at open. */
void *Host_PakReadFile(int id, int *size) {
    SDL_IOStream *io;
    const PakFile *f;
    void *buf;

    *size = 0;
    if (pak_path[0] == 0 || id < 0 || id >= PAK_FILES || files[id].format != FORMAT_DATA) {
        return NULL;
    }
    f = &files[id];
    io = SDL_IOFromFile(pak_path, "rb");
    if (io == NULL) {
        return NULL;
    }
    buf = malloc((size_t)f->size + 1);
    if (buf != NULL && (SDL_SeekIO(io, (Sint64)f->offset, SDL_IO_SEEK_SET) < 0 ||
                        SDL_ReadIO(io, buf, (size_t)f->size) != (size_t)f->size)) {
        free(buf);
        buf = NULL;
    }
    SDL_CloseIO(io);
    if (buf != NULL) {
        ((Uint8 *)buf)[f->size] = 0xFF; /* a text walk off the end stops here */
        *size = (int)f->size;
    }
    return buf;
}
#endif
