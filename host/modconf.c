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

/* DNA rules are unordered species pairs. The result rank is a species property,
 * used by every game screen and saved roster entry, not only the DNA animation. */
static unsigned char dna_results[IDS][IDS];
static signed char dna_ranks[IDS]; /* rank + 1; zero means original rank */

typedef struct {
    unsigned char results[IDS][IDS];
    signed char ranks[IDS];
    int version, format, rules;
} DnaDoc;
typedef struct { int a, b, result, rank, seen; } DnaRule;

static int dna_int(Js *j) {
    long value;
    const char *start;
    ws(j);
    start = j->p;
    if (start >= j->end || *start < '0' || *start > '9') { j->bad = 1; return 0; }
    value = num(j);
    for (const char *p = start; p < j->p; p++)
        if (*p < '0' || *p > '9') j->bad = 1;
    if (value > 255 || (j->p - start > 1 && *start == '0')) j->bad = 1;
    return (int)value;
}
static void dna_field(Js *j, const char *key, void *ctx) {
    DnaRule *r = ctx;
    int bit, *field;
    if (!strcmp(key, "a")) { bit = 1; field = &r->a; }
    else if (!strcmp(key, "b")) { bit = 2; field = &r->b; }
    else if (!strcmp(key, "result")) { bit = 4; field = &r->result; }
    else if (!strcmp(key, "rank")) { bit = 8; field = &r->rank; }
    else { j->bad = 1; return; }
    if (r->seen & bit) { j->bad = 1; return; }
    r->seen |= bit;
    *field = dna_int(j);
}
static void dna_top(Js *j, const char *key, void *ctx) {
    DnaDoc *d = ctx;
    if (!strcmp(key, "format")) {
        char name[64];
        if (d->format || !str(j, name, sizeof name) || strcmp(name, "dw2-dna-overrides")) j->bad = 1;
        d->format = 1;
    } else if (!strcmp(key, "version")) {
        if (d->version) j->bad = 1;
        d->version = dna_int(j);
    } else if (!strcmp(key, "rules")) {
        if (d->rules++ || j->p >= j->end || *j->p++ != '[') { j->bad = 1; return; }
        ws(j);
        if (j->p < j->end && *j->p == ']') { j->p++; return; }
        while (!j->bad) {
            DnaRule r = {0};
            if (!members(j, dna_field, &r) || r.seen != 15 || r.a < 1 || r.b < 1 ||
                r.result < 1 || r.a == r.b || r.rank > 3 ||
                d->results[r.a][r.b] || (d->ranks[r.result] && d->ranks[r.result] != r.rank + 1)) {
                j->bad = 1; return;
            }
            d->results[r.a][r.b] = d->results[r.b][r.a] = r.result;
            d->ranks[r.result] = r.rank + 1;
            ws(j);
            if (j->p < j->end && *j->p == ']') { j->p++; return; }
            if (j->p >= j->end || *j->p++ != ',') { j->bad = 1; return; }
        }
    }
}
static int dna_parse(const char *data, unsigned long size) {
    DnaDoc *d;
    Js j = {data, data + size, 0};
    int ok;
    if (size > 4 * 1024 * 1024) return 0;
    d = calloc(1, sizeof *d);
    if (!d) return 0;
    ok = members(&j, dna_top, d);
    ws(&j);
    ok = ok && !j.bad && j.p == j.end && d->format == 1 && d->version == 1 && d->rules == 1;
    if (ok) {
        memcpy(dna_results, d->results, sizeof dna_results);
        memcpy(dna_ranks, d->ranks, sizeof dna_ranks);
    }
    free(d);
    return ok;
}
int Host_DnaResult(int a, int b) {
    return a > 0 && a < IDS && b > 0 && b < IDS ? dna_results[a][b] : 0;
}
int Host_DnaHasPartner(int id) {
    if (id <= 0 || id >= IDS) return 0;
    for (int b = 1; b < IDS; b++) if (dna_results[id][b]) return 1;
    return 0;
}
int Host_DnaRank(int id) {
    return id > 0 && id < IDS ? dna_ranks[id] - 1 : -1;
}

/* parses one file's text (WHICH 0 levels, 1 camera); a damaged or newer file changes nothing.
 * Returns 1 if read, 0 if ignored. */
int Host_ModConfParse(const char *data, unsigned long size, int which) {
    if (which == 2) return dna_parse(data, size);
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
    memset(dna_results, 0, sizeof dna_results);
    memset(dna_ranks, 0, sizeof dna_ranks);
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
    for (int which = 0; which < 3; which++) {
        const char *names[] = {"digivolve_levels.json", "battle_camera.json", "dna_overrides.json"};
        const char *name = names[which];
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
