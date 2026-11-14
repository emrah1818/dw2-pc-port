/* dw2-pc-port: per-Digimon settings from the Mod Studio (Mods/digivolve_levels.json,
 * Mods/battle_camera.json). Both files are optional; without them the game behaves exactly as
 * before. Read once at start from the same folders as Mods/files (user folder, then the folder of
 * dw2.exe, which wins). Interface: doc/MOD_API2.md, "Format 1".
 *
 *   digivolve_levels.json  {"format":1,"levels":{"<id>":<level 1-99>,...}}
 *       Stg20_LabInfo: a Digimon with an entry may digivolve from that level on instead of the
 *       Stg20_DigivolveRuleTbl band (Rookie 11, Champion 21, Ultimate 31). Mega stays locked.
 *   battle_camera.json     {"format":1,"digimon":{"<id>":{"distance":<percent 25-400>,
 *       "height":<units>,"target":<units>,"angle":<units, 4096 = 360 deg>,
 *       "closeup":{same four keys}},...}}
 *       Stg30_CameraUpdate: the top-level keys change the attack / target shots (states 10-21)
 *       of that Digimon, "closeup" the command / target-select close-up (states 2-7): distance
 *       scales vpz, height moves the viewpoint up, target the look-at point up, angle turns the
 *       shot around the Digimon. Missing keys keep the game's values. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/host.h"

#define IDS 256

static unsigned char levels[IDS];
typedef struct {
    int set, distance, height, target, angle;
} Cam;
static Cam cams[IDS], closeups[IDS];
static int n_levels, n_cams, loaded;

/* --- a small JSON reader: objects, strings, integers, true/false/null; arrays are skipped ---- */
typedef struct {
    const char *p, *end;
    int bad;
} Js;

static void ws(Js *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int str(Js *j, char *out, int size) {
    int n = 0;
    ws(j);
    if (j->p >= j->end || *j->p != '"') return j->bad = 1, 0;
    j->p++;
    while (j->p < j->end && *j->p != '"') {
        if (*j->p == '\\' && j->p + 1 < j->end) j->p++;
        if (n < size - 1) out[n++] = *j->p;
        j->p++;
    }
    if (j->p >= j->end) return j->bad = 1, 0;
    j->p++;
    out[n] = 0;
    return 1;
}

static void skip(Js *j);

static long num(Js *j) {
    char *e;
    long v;
    ws(j);
    v = strtol(j->p, &e, 10);
    if (e == j->p) {
        j->bad = 1;
        return 0;
    }
    j->p = e;
    if (j->p < j->end && (*j->p == '.' || *j->p == 'e' || *j->p == 'E')) { /* fractions: dropped */
        while (j->p < j->end && strchr("0123456789.eE+-", *j->p)) j->p++;
    }
    return v;
}

static void skip(Js *j) {
    char tmp[8];
    ws(j);
    if (j->p >= j->end) {
        j->bad = 1;
        return;
    }
    if (*j->p == '"') {
        str(j, tmp, sizeof tmp);
    } else if (*j->p == '{' || *j->p == '[') {
        char close = *j->p == '{' ? '}' : ']';
        int obj = close == '}';
        j->p++;
        ws(j);
        if (j->p < j->end && *j->p == close) {
            j->p++;
            return;
        }
        while (!j->bad) {
            if (obj) {
                str(j, tmp, sizeof tmp);
                ws(j);
                if (j->p >= j->end || *j->p != ':') { j->bad = 1; return; }
                j->p++;
            }
            skip(j);
            ws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == close) { j->p++; return; }
            j->bad = 1;
        }
    } else if (!strncmp(j->p, "true", 4) || !strncmp(j->p, "null", 4)) {
        j->p += 4;
    } else if (!strncmp(j->p, "false", 5)) {
        j->p += 5;
    } else {
        num(j);
    }
}

/* calls FN for every member of the object at J; returns 0 on a syntax error */
typedef void (*MemberFn)(Js *j, const char *key, void *ctx);
static int members(Js *j, MemberFn fn, void *ctx) {
    char key[32];
    ws(j);
    if (j->p >= j->end || *j->p != '{') return 0;
    j->p++;
    ws(j);
    if (j->p < j->end && *j->p == '}') { j->p++; return 1; }
    while (!j->bad) {
        const char *before;
        if (!str(j, key, sizeof key)) return 0;
        ws(j);
        if (j->p >= j->end || *j->p != ':') return 0;
        j->p++;
        ws(j);
        before = j->p;
        fn(j, key, ctx);
        if (j->p == before) skip(j); /* the callback did not read the value */
        ws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == '}') { j->p++; return !j->bad; }
        return 0;
    }
    return 0;
}

static int id_of(const char *key) {
    char *e;
    long v = strtol(key, &e, 10);
    return *key && !*e && v >= 1 && v < IDS ? (int)v : 0;
}

/* --- digivolve_levels.json ---------------------------------------------------------------------- */
static void level_member(Js *j, const char *key, void *ctx) {
    int id = id_of(key);
    long v;
    (void)ctx;
    ws(j);
    if (j->p < j->end && (*j->p == '-' || (*j->p >= '0' && *j->p <= '9'))) {
        v = num(j);
        if (id && v >= 1 && v <= 99) {
            n_levels += levels[id] == 0;
            levels[id] = (unsigned char)v;
        } else {
            printf("[modconf] digivolve_levels: \"%s\": %ld ignored (id 1-255, level 1-99)\n", key, v);
        }
    }
}

typedef struct {
    int format;
    int which; /* 0 levels file, 1 camera file */
} Top;

static void cam_field(Js *j, const char *key, void *ctx) {
    Cam *c = ctx;
    long v;
    ws(j);
    if (!(j->p < j->end && (*j->p == '-' || (*j->p >= '0' && *j->p <= '9')))) return;
    v = num(j);
    if (!strcmp(key, "distance")) c->distance = (int)(v < 25 ? 25 : v > 400 ? 400 : v);
    else if (!strcmp(key, "height")) c->height = (int)(v < -20000 ? -20000 : v > 20000 ? 20000 : v);
    else if (!strcmp(key, "target")) c->target = (int)(v < -20000 ? -20000 : v > 20000 ? 20000 : v);
    else if (!strcmp(key, "angle")) c->angle = (int)(v % 4096);
}

typedef struct {
    Cam shot, close;
} CamPair;

static void cam_field2(Js *j, const char *key, void *ctx) {
    CamPair *c = ctx;
    if (!strcmp(key, "closeup")) {
        c->close.set = 1;
        if (!members(j, cam_field, &c->close)) j->bad = 1;
    } else {
        cam_field(j, key, &c->shot);
    }
}

static void cam_member(Js *j, const char *key, void *ctx) {
    int id = id_of(key);
    CamPair c = {{1, 100, 0, 0, 0}, {0, 100, 0, 0, 0}};
    (void)ctx;
    if (!members(j, cam_field2, &c)) {
        j->bad = 1;
        return;
    }
    if (!id) {
        printf("[modconf] battle_camera: \"%s\" is not a Digimon id 1-255, ignored\n", key);
        return;
    }
    n_cams += !cams[id].set;
    cams[id] = c.shot;
    closeups[id] = c.close;
}

static void top_member(Js *j, const char *key, void *ctx) {
    Top *t = ctx;
    if (!strcmp(key, "format")) {
        t->format = (int)num(j);
    } else if (t->which == 0 && !strcmp(key, "levels")) {
        if (!members(j, level_member, NULL)) j->bad = 1;
    } else if (t->which == 1 && !strcmp(key, "digimon")) {
        if (!members(j, cam_member, NULL)) j->bad = 1;
    }
}

/* parses one file's text (WHICH 0 levels, 1 camera); a damaged or newer file changes nothing.
 * Returns 1 if read, 0 if ignored. */
int Host_ModConfParse(const char *data, unsigned long size, int which) {
    unsigned char save_levels[IDS];
    Cam save_cams[IDS], save_close[IDS];
    int save_nl = n_levels, save_nc = n_cams;
    Js j;
    Top t = {0, which};

    memcpy(save_levels, levels, sizeof levels);
    memcpy(save_cams, cams, sizeof cams);
    memcpy(save_close, closeups, sizeof closeups);
    j.p = data;
    j.end = data + size;
    j.bad = 0;
    if (!members(&j, top_member, &t) || j.bad || t.format != 1) {
        memcpy(levels, save_levels, sizeof levels);
        memcpy(cams, save_cams, sizeof cams);
        memcpy(closeups, save_close, sizeof closeups);
        n_levels = save_nl;
        n_cams = save_nc;
        return t.format > 1 ? -1 : 0;
    }
    return 1;
}

static void read_file(const char *root, const char *name, int which) {
    char *path = NULL;
    size_t size = 0;
    char *data;
    int r;

    SDL_asprintf(&path, "%sMods/%s", root, name);
    data = path ? SDL_LoadFile(path, &size) : NULL;
    if (data == NULL) {
        SDL_free(path);
        return;
    }
    r = Host_ModConfParse(data, size, which);
    if (r == 1) {
        printf("[modconf] %s: read\n", path);
    } else {
        /* the game keeps its own rules */
        printf("[modconf] %s: %s, ignored\n", path, r < 0 ? "newer format than this port reads" : "not a valid format-1 file");
    }
    SDL_free(data);
    SDL_free(path);
}

void Host_ModConfReset(void) {
    memset(levels, 0, sizeof levels);
    memset(cams, 0, sizeof cams);
    memset(closeups, 0, sizeof closeups);
    n_levels = n_cams = 0;
}

void Host_ModConfLoad(int no_mods) {
    const char *base;
    char *pref;

    if (loaded) return;
    loaded = 1;
    if (no_mods) return;
    base = SDL_GetBasePath();
    pref = SDL_GetPrefPath("", "DW2-Online");
    for (int which = 0; which < 2; which++) {
        const char *name = which ? "battle_camera.json" : "digivolve_levels.json";
        if (pref) read_file(pref, name, which);
        if (base) read_file(base, name, which); /* next to the exe wins */
    }
    SDL_free(pref);
    printf("[modconf] digivolve levels for %d Digimon, battle cameras for %d Digimon\n", n_levels, n_cams);
}

int Host_DigivolveMinLevel(int digi_id) {
    return digi_id > 0 && digi_id < IDS ? levels[digi_id] : 0;
}

void Host_BattleCamAdjust(int digi_id, int closeup, int *vpy, int *vpz, int *vry, int *rot_y) {
    const Cam *c;
    if (digi_id <= 0 || digi_id >= IDS) return;
    c = closeup ? &closeups[digi_id] : &cams[digi_id];
    if (!c->set) return;
    *vpz = (int)((long long)*vpz * c->distance / 100);
    *vpy -= c->height; /* PS1 y points down: up is negative */
    *vry -= c->target;
    *rot_y += c->angle;
}

/* tests: set a level / camera without a file */
void Host_ModConfSetLevel(int digi_id, int level) {
    if (digi_id > 0 && digi_id < IDS) levels[digi_id] = (unsigned char)(level < 0 ? 0 : level > 99 ? 99 : level);
}
