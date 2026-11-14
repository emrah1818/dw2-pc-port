#ifndef _WIN32
#define _GNU_SOURCE /* MAP_32BIT */
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <ucontext.h>
#endif

#include "common.h"
#include "main/156C.h"
extern SysState Sys_State;
extern Blk20 Gfx_IdentityMatrix;
extern s32 Mem_Alloc(s32 size, s32 tag);
extern void Mem_Zero(void *ptr, s32 size);
extern ActorModel *Gfx_AttachModel(Actor *, s32);
extern void Anim_SetModelAnimFile(Actor *, s32, s32);
extern void Anim_StepModelAnim(Actor *);
extern void Gfx_CalcModelBoneMatrices(Actor *);
extern GfxTexSlot *Gfx_FindOrLoadTexSlot(s32);
#include <SDL3/SDL.h>

#include "backend/psxgpu_hd.h"
#include "backend/texrep.h"
#include "host/host.h"
#include "host/host_sdl.h"
#include "host/settings.h"
#include "host/ui.h"
#include "host/update.h"
#include "psyq/psyq_log.h"
#if DW2_DEV || DW2_CHEATS
#include "host/devedit.h"
void Cheat_GameTick(void);
#endif
#if DW2_DEV
#include "host/devui.h"
#endif

/* Native entry point: replaces crt0 (Sys_Start). Checks the memory layout, keeps the overlays'
 * initial data, starts the SDL3 host, then runs the game's Sys_Main (it never returns; the host
 * quits on window close or Esc).
 *
 *   dw2 [IMAGE] [--disc IMAGE] [--pak PATH] [--vblanks N] [--no-window] [--shot-dir DIR] [--shot-at N]... [--shot-vb N]...
 *       [--hold-boot N] [--press N:BUTTONS[:LEN]]... [--press2 N:BUTTONS[:LEN]]... [--save-dir DIR]
 *       [--fast] [--pad2-keys] [--scale N] [--sharpen N] [--hd-threads N] [--pgxp] [--wide] [--smooth] [--renderer gpu|soft]
 *       [--settings PATH] [--settings-set KEY=VALUE]... [--settings-ui] [--settings-tab T] [--ui-shots] [--quit-dialog]
 *       [--no-update-check] [--update-check] [--version]
 *       [--movies] [--window-mode M] [--fullscreen] [--windowed]
 *     --pak PATH     the data pack (default dw2.pak next to the exe, in the user data folder,
 *                    then build/native/dw2.pak; doc/PACK_FORMAT.md), checked before anything else
 *                    runs; missing or failing, it is built from the disc image (host/pakbuild.c)
 *     --disc IMAGE   the disc image to build the pack from (.cue, .img, .bin, raw .iso); a bare
 *                    IMAGE argument (drag and drop onto the exe) is the same. Default: the first
 *                    Digimon World 2 USA image next to the exe, then in the user data folder;
 *                    with a window and none found, a file dialog
 *     --vblanks N    quit after N VBlank waits in the Sys_FlipPending spin (0 = run on)
 *     --no-window    SDL dummy video and audio drivers (headless test runs)
 *     --shot-dir DIR screenshots go to DIR (default screenshots/ next to the exe, else in the user
 *                    data folder): the boot image when the display turns on (if any screenshot
 *                    option is given), each --shot-at (display + VRAM PNGs); F12 writes one PNG
 *                    of the picture as the window shows it
 *     --shot-at N    screenshot pair at VBlank wait N (repeatable, up to 16)
 *     --shot-vb N    screenshot pair at the first wait with N VBlanks run (repeatable, up to 16,
 *                    ascending; an emulator probe counts the same frames)
 *     --hold-boot N  keep the boot image on screen N VBlanks (dev option, Host_DisplayOn)
 *     --press N:BUTTONS[:LEN]  hold BUTTONS ("Start", "Down+Cross", names as in the [input]
 *                    log) on port 0 from VBlank wait N for LEN waits (default 4); repeatable,
 *                    for headless input tests
 *     --press2 N:BUTTONS[:LEN]  the same on port 1; any --press2 makes port 1 a connected pad
 *                    (VS mode with one keyboard, headless)
 *     --pad2-keys    port 1 counts as connected; Tab moves the keyboard between port 0 and port 1
 *                    (VS mode with one keyboard in the window)
 *     --save-dir DIR memory card images card1.mcd / card2.mcd in DIR instead of the user data
 *                    folder (%APPDATA%\DW2-Online\saves\ on Windows,
 *                    ~/.local/share/DW2-Online/saves/ on Linux)
 *     --fast         no 59.94 Hz pacing: one VBlank per wait, as fast as the host runs (headless
 *                    test runs; same frame sequence as a paced run without catch-up VBlanks)
 *     --speed-up     start with the speed-up on (PR.28; F3 in the window, multiplier "speed" in
 *                    the settings file / F1 Game tab)
 *     --scale N      HD output at N x the PS1 resolution (1..8, default 1 = classic; PG.1,
 *                    backend/psxgpu_hd.c); F5 in the window steps through 1..8
 *     --sharpen N    sharpening of the HD picture, 0..100 (PR.2b; 0 = off; needs --scale 2 or
 *                    more); F8 toggles it in the window. Overrides the settings file, not saved
 *     --no-update-check  no update check in this run (PR.30; setting "update_check", F1 Game tab)
 *     --update-check ask GitHub for a newer release even with --no-window (PR.30 check; the
 *                    answer is logged as "[update] ...")
 *     --update-as V  the update check compares as if this program were version V (tests)
 *     --update-auto  with a newer release, install it without asking, also with --no-window
 *                    (PR.31 tests; with --update-check)
 *     --version      print the version and quit
 *     --ui-shots     with --no-window: the window picture is rendered headless (F1 closed) and
 *                    each shot adds <tag>_ui.png (title hint check)
 *     --quit-dialog  the Esc quit question open at start (PR.15; headless shot with --ui-shots)
 *     --movies       with --no-window: play the STR movies too (PR.4 movie checks); headless runs
 *                    otherwise end each movie on its first frame (P1.10 stub), windows play them
 *     --hd-threads N drawing threads for the HD output (default 0 = one per core, at most 8)
 *     --pgxp         no-wobble geometry for the HD output (PG.2: precise GTE vertices,
 *                    perspective-correct textures); F6 toggles it in the window
 *     --wide         16:9 picture (PG.3: wider view, 2D pictures pillarboxed); F7 toggles it
 *     --renderer R   HD output drawn by the GPU (gpu, PG.10b: SDL_GPU / Vulkan; the default with
 *                    a window) or the software rasterizer (soft; the default with --no-window)
 *     --settings PATH  the settings file (PR.2, host/settings.c; default settings.ini in the user
 *                    data folder with a window, none with --no-window). --scale, --wide, --pgxp
 *                    and --renderer override it for this run and are not saved
 *     --settings-set KEY=VALUE  set a value as the F1 window does (saved; repeatable; tests)
 *     --settings-ui  the F1 settings window open at start; with --no-window it renders headless
 *                    and each shot adds <tag>_ui.png
 *     --settings-tab T the F1 tab shown first (Display, Controls, Sound; headless checks)
 *     --window-mode M windowed, borderless (fullscreen at the desktop resolution) or exclusive
 *                    (fullscreen in the fullscreen_mode of settings.ini); PR.3, F11 toggles in the
 *                    window; overrides the settings file for this run, not saved. Headless runs
 *                    keep the hidden window windowed
 *     --fullscreen   the same as --window-mode borderless; --windowed as --window-mode windowed
 *
 *   Dev builds only (DW2_DEV, PD; the release client has none of these):
 *     --start-mode N dev: start in game mode N instead of the title (0x402), e.g. the stag0000
 *                    debug menus 0x101..0x106 (0x105: domain select)
 *     --devui        the dev overlay (F2, PD.1) open at start; with --no-window it also runs
 *                    headless into the hidden window and each shot adds <tag>_devui.png
 *     --devui-tab T  the overlay tab shown first (Timing, Game, Tasks, Heap, Input, Save, View);
 *                    Data:SUB a Data sub tab, Save:Edit[+SECTION] the Save tab with its Edit
 *                    controls on, SECTION (Party, Digi-Beetle, Bag, Storage, Flags) scrolled to
 *     --window-size W H  window size in pixels (default 960x720, 1281x720 with --wide)
 *     --devedit N:KIND=ARGS  PD.4 state edit at VBlank wait N (repeatable; kinds and arguments in
 *                    host/devedit.c DevEdit_Script, e.g. 900:bits=12345, 900:warp=0x200,0) */

extern void Sys_Main(void);
extern void Host_OvlSnapshot(void);
extern void Snd_NativeInit(void);
extern u8 Ovl_LoadArea[];
extern s32 Sys_FlipPending;

/* No original game data required: reproduce full-cache pressure with a large heap. */
static int check_mod_cache(void) {
    extern void Mem_InitHeap(MemBlock *, s32);
    extern void Cd_ClearFileCache(void);
    extern void Cd_QueueFile(s32);
    extern CdCacheEntry Cd_FileCache[0x50];
    extern u16 Cd_FileSectors[];
    int i, j, count;
    Mem_InitHeap((MemBlock *)Dw2_ModHeap, DW2_MOD_HEAP_SIZE);
    Cd_ClearFileCache();
    Sys_State.frameCount = 100;
    for (i = 0; i < 80; i++) {
        CdCacheEntry *e = &Cd_FileCache[i];
        e->fileId = 0x100+i; e->data = Mem_Alloc(32,3);
        memset((void *)(uintptr_t)e->data, 0xA5, 32);
        e->state = i == 1 ? 2 : 3; e->locked = i == 0; e->lastUsed = i;
    }
    for (j = 1; j <= 200; j++) {
        CdCacheEntry *added;
        Cd_FileSectors[j] = 1;
        Cd_QueueFile(j);
        added = Cd_FindCachedFile(j);
        if (!added || added->state != 1 || !added->data ||
            !Cd_FindCachedFile(0x100) || !Cd_FindCachedFile(0x101) ||
            (j == 1 && Cd_FindCachedFile(0x102))) return 1;
        added->state = 3; added->lastUsed = ++Sys_State.frameCount;
        count = 0;
        for (i = 0; i < 80; i++) if (Cd_FileCache[i].fileId) count++;
        if (count != 80) return 1;
    }
    for (i = 0; i < 32; i++)
        if (((unsigned char *)(uintptr_t)Cd_FindCachedFile(0x100)->data)[i] != 0xA5) return 1;
    puts("[mod-check] 200 full-cache replacements passed; locked and pending entries preserved");
    return 0;
}

static int check_mod_model, check_mod_anim, checking_mod;
static unsigned int max_vblanks;
static unsigned int vblanks;
static const char *shot_dir;
static unsigned int shot_at[16];
static int shot_count;
static unsigned long long shot_vb[16];
static int shot_vb_count, shot_vb_done;
static int hold_boot;
static unsigned long long win_shot_vb[8]; /* dw2-pc-port --window-shot-vb */
static int win_shot_count, win_shot_done;
static int start_mode;
static const char *export_dir; /* dw2-pc-port --export-assets DIR */
static const char *import_png; /* dw2-pc-port --import-texture PNG */
static int cheats_at_start;
static const char *export_texts;
static const char *dump_textures; /* dw2-pc-port: --dump-textures DIR */
static int no_texture_packs;      /* dw2-pc-port: --no-texture-packs */
int Lang_ExportTexts(const char *dir);
int Cheat_SetActive(int on);
int Assets_ImportTexture(const char *png);
int Assets_Export(const char *dir, void (*progress)(int done, int total, const char *what));

int Host_StartMode(void) {
    return start_mode;
}

static const char *shots(void) {
    static int made;
    static char dir[1024];

    if (shot_dir == NULL) {
        /* PR.22: screenshots/ next to the exe; the user data folder when that is not writable */
        const char *base = SDL_GetBasePath();
        char *pref;

        snprintf(dir, sizeof(dir), "%sscreenshots", base != NULL ? base : "");
        if (!SDL_CreateDirectory(dir) && (pref = SDL_GetPrefPath("", "DW2-Online")) != NULL) {
            snprintf(dir, sizeof(dir), "%sscreenshots", pref);
            SDL_free(pref);
        }
        shot_dir = dir;
    }
    if (!made) {
        made = 1;
        SDL_CreateDirectory(shot_dir);
    }
    return shot_dir;
}

void Host_ShotKey(void) {
    static int n;
    char tag[64], path[1200];
    SDL_Time now;
    SDL_DateTime t;

    /* date and time in the name: a new run does not overwrite older shots */
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &t, true)) {
        snprintf(tag, sizeof(tag), "dw2_%04d%02d%02d_%02d%02d%02d_%d", t.year, t.month, t.day, t.hour,
                 t.minute, t.second, n++);
    } else {
        snprintf(tag, sizeof(tag), "f12_%d_wait%u", n++, vblanks);
    }
    snprintf(path, sizeof(path), "%s/%s.png", shots(), tag);
    Host_WindowShot(path);
}

void Host_DisplayOn(void) {
    static int done;
    int i;

    if (done) {
        return;
    }
    done = 1;
    if (shot_dir != NULL || shot_count != 0) {
        Host_SaveShot(shots(), "boot");
    }
    if (hold_boot > 0) {
        printf("[host] boot image held for %d VBlanks (--hold-boot)\n", hold_boot);
        for (i = 0; i < hold_boot; i++) {
            Host_VBlank();
        }
    }
}

/* The image (globals, Ps1_Ram with the heap) must sit in one 16 MB window for the 24-bit OT links
 * (psyq/ps1mem.h): linked at a 16 MB-aligned base (CMakeLists.txt) and smaller than 16 MB.
 * The game C also keeps pointers in 32-bit ints (P1.12): every address it can see, the window
 * and the stack it runs on, must be below 2 GB so that the int gives the pointer back (zero or
 * sign extended alike). */
static void check_window(void) {
    uintptr_t base = (uintptr_t)Ps1_Ram & ~(uintptr_t)0xFFFFFF;
    const void *probes[] = { Dw2_ModHeap, Dw2_ModHeap + DW2_MOD_HEAP_SIZE - 1, Ps1_Ram, Ps1_Ram + PS1_RAM_SIZE - 1, Ps1_Scratchpad, Ovl_LoadArea, &Sys_FlipPending,
                             (const void *)Sys_Main };
    volatile int stack_probe = 0;
    size_t i;

    for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        if (((uintptr_t)probes[i] & ~(uintptr_t)0xFFFFFF) != base) {
            fprintf(stderr, "[host] %p is outside the 16 MB window at 0x%08lX\n", probes[i], (unsigned long)base);
            exit(1);
        }
    }
    if (base >= 0x80000000u || (uintptr_t)&stack_probe >= 0x80000000u) {
        fprintf(stderr, "[host] window 0x%08lX or stack %p is not below 2 GB\n", (unsigned long)base,
                (void *)&stack_probe);
        exit(1);
    }
    printf("[host] 16 MB window 0x%08lX: Ps1_Ram %p, heap %p..%p\n", (unsigned long)base, (void *)Ps1_Ram,
           (void *)Dw2_ModHeap, (void *)(Dw2_ModHeap+DW2_MOD_HEAP_SIZE));
}

void Host_WaitVBlank(void) {
    int i;

    if (vblanks == 0) {
        printf("[host] first VBlank wait (Sys_FlipPending spin) reached after %u Psy-Q calls\n", Psyq_CallCount);
        fflush(stdout);
    }
    if (check_mod_model && !checking_mod && vblanks == 0) {
        checking_mod = 1;
        Actor *actor = (Actor *)Mem_Alloc(sizeof(Actor), 2);
        Mem_Zero(actor, sizeof(Actor));
        actor->u38.ptr38 = (ActorTransformView *)Mem_Alloc(sizeof(ActorTransformView), 2);
        Mem_Zero(actor->u38.ptr38, sizeof(ActorTransformView));
        actor->u38.ptr38->matrix = Gfx_IdentityMatrix;
        ActorModel *model = Gfx_AttachModel(actor, check_mod_model);
        if (model == NULL || model->boneCount <= 0 || model->boneCount > 64) {
            fprintf(stderr, "[mod-check] invalid model\n"); exit(1);
        }
        Anim_SetModelAnimFile(actor, 0, check_mod_anim);
        int changes = 0;
        for (int tick = 0; tick < 120; tick++) {
            int old = model->animPos;
            Sys_State.frameDelta = 1;
            Anim_StepModelAnim(actor);
            Gfx_CalcModelBoneMatrices(actor);
            if (old != model->animPos) changes++;
        }
        printf("[mod-check] model %04X: %d bones; animation %04X: 120 game steps, %d row advances\n", check_mod_model, model->boneCount, check_mod_anim, changes);
        fflush(stdout);
        if (!changes) exit(1);
        GfxTexSlot *slot = Gfx_FindOrLoadTexSlot(check_mod_model << 16);
        if (TexRep_Count() > 0) {
            TexRepUse use;
            TexRep_SetScale(4);
            int found = 0;
            for (int attempt = 0; attempt < 1000 && !found; attempt++) {
                found = TexRep_Find(0, slot->vramX, slot->vramY, slot->vramX, slot->vramY+224, 0, 0, 0, 127, 0, 223, &use);
                if (!found) SDL_Delay(10);
            }
            if (!found || use.k < 2) { fprintf(stderr, "[mod-check] HD replacement not resolved\n"); exit(1); }
            printf("[mod-check] HD replacement resolved at %dx (sample %d)\n", use.k, TexRep_Sample(&use,32<<8,32<<8));
        }
        Host_Quit("native model/animation check passed");
    }
    vblanks++;
    Host_TraceTick(vblanks);
    Host_SceneTick();
    Host_BotTick(vblanks);
    Host_InputScriptTick(vblanks);
#if DW2_DEV || DW2_CHEATS
    DevEdit_ScriptTick(vblanks); /* release: only the bot's devedit (tests), pushed only with cheats on */
    DevEdit_Apply(); /* PD.4: between frames, before Sys_Main reads nextGameMode */
    Cheat_GameTick(); /* dw2-pc-port: cheat menu snapshot / undo (host/cheat.c) */
#endif
    for (i = 0; i < shot_count; i++) {
        if (shot_at[i] == vblanks) {
            char tag[32];

            snprintf(tag, sizeof(tag), "wait%u", vblanks);
            Host_SaveShot(shots(), tag);
        }
    }
    /* --shot-vb: by VBlank count, the frame count an emulator probe sees (the shots come in order) */
    while (shot_vb_done < shot_vb_count && Host_VBlankCount() >= shot_vb[shot_vb_done]) {
        char tag[32];

        snprintf(tag, sizeof(tag), "vb%llu", shot_vb[shot_vb_done++]);
        Host_SaveShot(shots(), tag);
    }
    /* dw2-pc-port --window-shot-vb N: the window picture as F12 saves it (GPU renderer, any scale) */
    while (win_shot_done < win_shot_count && Host_VBlankCount() >= win_shot_vb[win_shot_done]) {
        win_shot_done++;
        Host_ShotKey();
    }
    if (max_vblanks != 0 && vblanks > max_vblanks) {
        printf("[host] stop after %u VBlank waits (%u Psy-Q calls)\n", max_vblanks, Psyq_CallCount);
        Host_Quit("--vblanks");
    }
    Host_VBlank();
}

/* --press / --press2 N:BUTTONS[:LEN] */
static int parse_press(int port, const char *arg) {
    char names[64];
    unsigned int at, len = 4;
    const char *c1 = strchr(arg, ':');
    const char *c2;
    size_t n;
    unsigned short bits;

    if (c1 == NULL) {
        return 0;
    }
    at = (unsigned int)strtoul(arg, NULL, 0);
    c2 = strchr(c1 + 1, ':');
    n = c2 != NULL ? (size_t)(c2 - (c1 + 1)) : strlen(c1 + 1);
    if (n == 0 || n >= sizeof(names)) {
        return 0;
    }
    memcpy(names, c1 + 1, n);
    names[n] = '\0';
    if (c2 != NULL) {
        len = (unsigned int)strtoul(c2 + 1, NULL, 0);
    }
    bits = Host_PadParseButtons(names);
    return bits != 0 && len != 0 && Host_InputScriptAdd(port, at, bits, len);
}

static const char *pak = NULL;
static const char *disc = NULL;
static int no_window = 0;
static int movies = 0; /* --movies */

int Host_MoviesOn(void) {
    return !no_window || movies;
}

/* PG.10 b3: with a game thread, the main thread does the SDL / window init for it. */
static SDL_Semaphore *init_req, *init_done;

void Host_Init(int no_win) {
    if (init_req != NULL) {
        SDL_SignalSemaphore(init_req);
        SDL_WaitSemaphore(init_done);
    } else {
        Host_InitWindow(no_win);
    }
    Host_ClockStart();
}

/* Everything from the layout check on: the game's own call stack. */
static void game_main(void) {
    check_window();
    Host_OvlSnapshot();
    Snd_NativeInit();
    Host_Init(no_window);
    printf("[host] Sys_Main\n");
    fflush(stdout);
    Sys_Main();
    Host_Quit("Sys_Main returned");
}

#ifndef _WIN32
/* Linux: the main thread's stack lies far above 4 GB, but the game keeps stack addresses in
 * 32-bit ints (Task_Create(id, slot, (s32)&local), P1.12). The game runs on a stack mapped below
 * 2 GB (MAP_32BIT), entered with a context switch on the same thread. */
#define GAME_STACK_SIZE (8u << 20)

static ucontext_t main_ctx, game_ctx;

static void run_on_low_stack(void) {
    void *stack = mmap(NULL, GAME_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
                       -1, 0);

    if (stack == MAP_FAILED) {
        perror("[host] mmap game stack");
        exit(1);
    }
    getcontext(&game_ctx);
    game_ctx.uc_stack.ss_sp = stack;
    game_ctx.uc_stack.ss_size = GAME_STACK_SIZE;
    game_ctx.uc_link = &main_ctx;
    makecontext(&game_ctx, game_main, 0);
    swapcontext(&main_ctx, &game_ctx);
}
#endif

/* PG.10 b3: the game thread; check_window checks its stack too (Windows: the low range, as for
 * the main thread; Linux: the same switch to the MAP_32BIT stack). */
static HOST_ENTRY int game_thread(void *arg) {
    (void)arg;
#ifdef _WIN32
    game_main();
#else
    run_on_low_stack();
#endif
    return 0;
}

/* With a visible window the game gets its own thread; the main thread keeps the window. */
static void run_threaded(void) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Thread *t;

    Host_SetThreaded(1);
    init_req = SDL_CreateSemaphore(0);
    init_done = SDL_CreateSemaphore(0);
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)game_thread);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "game");
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 16 << 20);
    t = init_req != NULL && init_done != NULL ? SDL_CreateThreadWithProperties(props) : NULL;
    SDL_DestroyProperties(props);
    if (t == NULL) {
        fprintf(stderr, "[host] game thread: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_DetachThread(t);
    SDL_WaitSemaphore(init_req);
    Host_InitWindow(0);
    SDL_SignalSemaphore(init_done);
    Host_WindowLoop();
}

/* dw2-pc-port: the arguments of this run, for a restart after a language change (Host_Restart) */
static int g_argc;
static char **g_argv;
void Host_RequestQuit(const char *why); /* host/sdl.c */
int Host_LanguageAtStart; /* 1: the first start, ask for the language (host/ui.cpp) */

int Host_Restart(void) {
    const char **args = (const char **)SDL_calloc((size_t)g_argc + 1, sizeof(char *));
    SDL_Process *p;
    int n = 0;

    if (args == NULL || g_argc < 1) {
        return 0;
    }
    for (int i = 0; i < g_argc; i++) {
        if ((strcmp(g_argv[i], "--lang") == 0) && i + 1 < g_argc) {
            i++; /* the new language comes from the settings file */
            continue;
        }
        args[n++] = g_argv[i];
    }
    args[n] = NULL;
    Settings_SaveIfDirty();
    p = SDL_CreateProcess(args, false);
    SDL_free(args);
    if (p == NULL) {
        printf("[host] restart failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_DestroyProcess(p);
    printf("[host] restarted for the new language\n");
    Host_RequestQuit("language change");
    return 1;
}

HOST_ENTRY int main(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
    int i;
    int cl_scale_value = 1, cl_profile = -1;
    int cl_scale = 0, cl_wide = 0, cl_pgxp = 0, cl_renderer = 0, cl_window = -1, cl_sharpen = -1, cl_smooth = 0;
    int update_force = 0;

#ifdef _WIN32
    Host_AttachParentConsole();
#endif
    SDL_SetAppMetadata("DW2-PC", DW2_VERSION, NULL);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mod-api") == 0) {
            uintptr_t base = (uintptr_t)Ps1_Ram & ~(uintptr_t)0xFFFFFF;
            if (((uintptr_t)Dw2_ModHeap & ~(uintptr_t)0xFFFFFF) != base ||
                (((uintptr_t)Dw2_ModHeap+DW2_MOD_HEAP_SIZE-1) & ~(uintptr_t)0xFFFFFF) != base ||
                ((uintptr_t)main & ~(uintptr_t)0xFFFFFF) != base) {
                fprintf(stderr, "[mods] native image / heap exceeds the 16 MiB GPU link window\n");
                return 1;
            }
            puts("{\"mod_api\":2,\"max_file_bytes\":2097152,\"virtual_cd_limit\":450000,"
                 "\"digivolve_levels\":1,\"battle_camera\":1}");
            return 0;
        } else if (strcmp(argv[i], "--check-cache") == 0) {
            return check_mod_cache();
        } else if (strcmp(argv[i], "--check-model") == 0 && i+2 < argc) {
            check_mod_model = (int)strtol(argv[++i], NULL, 0);
            check_mod_anim = (int)strtol(argv[++i], NULL, 0);
            if (check_mod_model <= 0 || check_mod_model >= 3675 || check_mod_anim <= 0 || check_mod_anim >= 3675) return 2;
        } else if (strcmp(argv[i], "--vblanks") == 0 && i + 1 < argc) {
            max_vblanks = (unsigned int)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--pak") == 0 && i + 1 < argc) {
            pak = argv[++i];
        } else if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            disc = argv[++i];
        } else if (strcmp(argv[i], "--no-window") == 0) {
            no_window = 1;
        } else if (strcmp(argv[i], "--no-update-check") == 0) {
            Update_Disable();
        } else if (strcmp(argv[i], "--update-check") == 0) {
            update_force = 1;
        } else if (strcmp(argv[i], "--update-as") == 0 && i + 1 < argc) {
            Update_SetLocalVersion(argv[++i]);
        } else if (strcmp(argv[i], "--update-auto") == 0) {
            Update_SetAuto();
        } else if (strcmp(argv[i], "--bot") == 0 && i + 2 < argc) {
            if (Host_BotOpen(argv[i + 1], argv[i + 2]) != 0) {
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("DW2-PC v%s\n", DW2_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--shot-dir") == 0 && i + 1 < argc) {
            shot_dir = argv[++i];
        } else if (strcmp(argv[i], "--shot-at") == 0 && i + 1 < argc && shot_count < 16) {
            shot_at[shot_count++] = (unsigned int)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--shot-vb") == 0 && i + 1 < argc && shot_vb_count < 16) {
            shot_vb[shot_vb_count++] = strtoull(argv[++i], NULL, 0);
#if DW2_DEV
        } else if (strcmp(argv[i], "--start-mode") == 0 && i + 1 < argc) {
            start_mode = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--devui") == 0) {
            DevUi_SetOpenAtStart(1);
        } else if (strcmp(argv[i], "--devui-tab") == 0 && i + 1 < argc) {
            DevUi_SetTab(argv[++i]);
        } else if (strcmp(argv[i], "--window-size") == 0 && i + 2 < argc) {
            int w = (int)strtol(argv[i + 1], NULL, 0), h = (int)strtol(argv[i + 2], NULL, 0);

            i += 2;
            DevUi_SetWindowSize(w, h);
        } else if (strcmp(argv[i], "--devedit") == 0 && i + 1 < argc && DevEdit_Script(argv[++i])) {
#endif
        } else if (strcmp(argv[i], "--hold-boot") == 0 && i + 1 < argc) {
            hold_boot = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--press") == 0 && i + 1 < argc && parse_press(0, argv[++i])) {
        } else if (strcmp(argv[i], "--press2") == 0 && i + 1 < argc && parse_press(1, argv[++i])) {
        } else if (strcmp(argv[i], "--pad2-keys") == 0) {
            Host_InputPad2Keys();
        } else if (strcmp(argv[i], "--fast") == 0) {
            Host_ClockFast();
        } else if (strcmp(argv[i], "--speed-up") == 0) {
            Host_SpeedSet(1);
        } else if (strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc) {
            Host_CardSetDir(argv[++i]);
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            /* dw2-pc-port: 1..12 or auto */
            i++;
            cl_scale_value = strcmp(argv[i], "auto") == 0 ? 0 : (int)strtol(argv[i], NULL, 0);
            cl_scale_value = cl_scale_value < 0 ? 1 : cl_scale_value > PSXHD_MAX_SCALE ? PSXHD_MAX_SCALE : cl_scale_value;
            cl_scale = 1;
        } else if (strcmp(argv[i], "--aspect") == 0 && i + 1 < argc) {
            /* dw2-pc-port: 4:3, 16:9 or 21:9 */
            i++;
            cl_wide = strcmp(argv[i], "21:9") == 0 ? 3 : strcmp(argv[i], "16:9") == 0 ? 2 : 1;
        } else if (strcmp(argv[i], "--texture-filter") == 0 && i + 1 < argc) {
            /* dw2-pc-port: nearest or bilinear, this run only */
            Settings_Override(SET_TEX_FILTER, strcmp(argv[++i], "bilinear") == 0 ? SET_TEXF_BILINEAR : SET_TEXF_NEAREST);
        } else if (strcmp(argv[i], "--cheats") == 0) {
            cheats_at_start = 1; /* dw2-pc-port: cheats on at start (tests; with backup as in F1) */
        } else if (strcmp(argv[i], "--lang-dump") == 0 && i + 1 < argc) {
            extern void Lang_SetDump(const char *dir);
            Lang_SetDump(argv[++i]); /* dw2-pc-port tests: write the patched font files */
        } else if (strcmp(argv[i], "--export-texts") == 0 && i + 1 < argc) {
            export_texts = argv[++i]; /* dw2-pc-port: text inventory (host/lang.c) and quit */
        } else if (strcmp(argv[i], "--no-mods") == 0) {
            Host_PakNoMods(); /* dw2-pc-port: Mods/files overrides off */
        } else if (strcmp(argv[i], "--no-texture-packs") == 0) {
            no_texture_packs = 1; /* dw2-pc-port: Mods/Textures off (backend/texrep.c) */
        } else if (strcmp(argv[i], "--dump-textures") == 0 && i + 1 < argc) {
            dump_textures = argv[++i]; /* dw2-pc-port: write the textures the game draws as PNG */
        } else if (strcmp(argv[i], "--import-texture") == 0 && i + 1 < argc) {
            import_png = argv[++i]; /* dw2-pc-port: PNG into Mods/files (host/assets.c) and quit */
        } else if (strcmp(argv[i], "--export-assets") == 0 && i + 1 < argc) {
            export_dir = argv[++i]; /* dw2-pc-port: write GameData (host/assets.c) and quit */
        } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            /* dw2-pc-port: menu language for this run (de / en) */
            Settings_Override(SET_LANGUAGE, strcmp(argv[++i], "de") == 0 ? SET_LANG_DE : SET_LANG_EN);
        } else if (strcmp(argv[i], "--window-shot-vb") == 0 && i + 1 < argc && win_shot_count < 8) {
            win_shot_vb[win_shot_count++] = strtoull(argv[++i], NULL, 0); /* dw2-pc-port */
        } else if (strcmp(argv[i], "--smooth-2d") == 0) {
            Settings_Override(SET_SMOOTH2D, 1); /* dw2-pc-port, this run only */
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            /* dw2-pc-port: original (classic), balanced, hd (remastered) or ultra, this run only */
            i++;
            cl_profile = Settings_ParseProfile(argv[i]);
            if (cl_profile < 0) {
                fprintf(stderr, "--profile %s: unknown profile (original, balanced, hd, ultra)\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--sharpen") == 0 && i + 1 < argc) {
            cl_sharpen = (int)strtol(argv[++i], NULL, 0);
            cl_sharpen = cl_sharpen < 0 ? 0 : cl_sharpen > 100 ? 100 : cl_sharpen;
        } else if (strcmp(argv[i], "--language-picker") == 0) {
            Host_LanguageAtStart = 1; /* dw2-pc-port: open the language picker at start (tests) */
        } else if (strcmp(argv[i], "--ui-shots") == 0) {
            Ui_SetShots(1);
        } else if (strcmp(argv[i], "--quit-dialog") == 0) {
            Ui_SetQuitAtStart(1);
        } else if (strcmp(argv[i], "--movies") == 0) {
            movies = 1;
        } else if (strcmp(argv[i], "--hd-threads") == 0 && i + 1 < argc) {
            PsxHd_SetThreads((int)strtol(argv[++i], NULL, 0));
        } else if (strcmp(argv[i], "--pgxp") == 0) {
            Host_SetPgxp(1);
            cl_pgxp = 1;
        } else if (strcmp(argv[i], "--smooth") == 0) {
            cl_smooth = 1;
        } else if (strcmp(argv[i], "--wide") == 0) {
            cl_wide = 2;
        } else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc &&
                   (strcmp(argv[i + 1], "gpu") == 0 || strcmp(argv[i + 1], "soft") == 0)) {
            Host_SetRenderer(strcmp(argv[++i], "gpu") == 0 ? 2 : 1);
            cl_renderer = strcmp(argv[i], "gpu") == 0 ? 1 : 2;
        } else if (strcmp(argv[i], "--settings") == 0 && i + 1 < argc) {
            Settings_SetPath(argv[++i]);
        } else if (strcmp(argv[i], "--settings-set") == 0 && i + 1 < argc && Settings_AddSet(argv[++i])) {
        } else if (strcmp(argv[i], "--settings-ui") == 0) {
            Ui_SetSettingsAtStart(1);
        } else if (strcmp(argv[i], "--settings-tab") == 0 && i + 1 < argc) {
            SettingsUi_SetTab(argv[++i]);
        } else if (strcmp(argv[i], "--window-mode") == 0 && i + 1 < argc && Settings_ParseWindowMode(argv[i + 1]) >= 0) {
            cl_window = Settings_ParseWindowMode(argv[++i]);
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            cl_window = SET_WINDOW_BORDERLESS;
        } else if (strcmp(argv[i], "--windowed") == 0) {
            cl_window = SET_WINDOW_WINDOWED;
        } else if (argv[i][0] != '-' && disc == NULL) {
            disc = argv[i];
        } else {
            fprintf(stderr,
                    "usage: %s [IMAGE] [--disc IMAGE] [--pak PATH] [--vblanks N] [--no-window] [--shot-dir DIR] [--shot-at N]... "
                    "[--shot-vb N]... [--hold-boot N] [--press N:BUTTONS[:LEN]]... [--press2 N:BUTTONS[:LEN]]... "
                    "[--save-dir DIR] [--fast] [--speed-up] [--pad2-keys] [--scale N] [--sharpen N] [--hd-threads N] [--pgxp] [--wide] [--smooth] "
                    "[--renderer gpu|soft] [--settings PATH] [--settings-set KEY=VALUE]... [--settings-ui] [--settings-tab T] [--ui-shots] [--quit-dialog] [--no-update-check] [--update-check] [--update-as V] [--update-auto] [--version] "
                    "[--movies] [--window-mode windowed|borderless|exclusive] [--fullscreen] [--windowed]%s\n",
                    argv[0], DW2_DEV ? " [--start-mode N] [--devui] [--devui-tab T] [--window-size W H] [--devedit N:KIND=ARGS]..." : "");
            return 2;
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 1 << 16);
    /* PR.2: the settings file; command line options win for this run and are not saved */
    if (cl_profile >= 0) {
        /* dw2-pc-port: --profile; the single options below win over it */
        static const int ids[] = { SET_SCALE, SET_WIDE, SET_PGXP, SET_SHARPEN, SET_SMOOTH, SET_TEX_FILTER, SET_SMOOTH2D, SET_UI_STYLE };
        size_t k;

        for (k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
            if (!Settings_Overridden(ids[k])) {
                Settings_Override(ids[k], Settings_ProfileValue(cl_profile, ids[k]));
            }
        }
        printf("[settings] profile %s (this run)\n", Settings_ProfileName(cl_profile));
    }
    if (cl_scale) {
        Settings_Override(SET_SCALE, cl_scale_value);
    }
    if (cl_wide) {
        Settings_Override(SET_WIDE, cl_wide - 1);
    }
    if (cl_pgxp) {
        Settings_Override(SET_PGXP, 1);
    }
    if (cl_renderer) {
        Settings_Override(SET_RENDERER, cl_renderer == 2 ? SET_RENDERER_SOFT : SET_RENDERER_GPU);
    }
    if (cl_window >= 0) {
        Settings_Override(SET_WINDOW_MODE, cl_window);
    }
    if (cl_sharpen >= 0) {
        Settings_Override(SET_SHARPEN, cl_sharpen);
    }
    if (cl_smooth) {
        Settings_Override(SET_SMOOTH, 1);
    }
    Settings_Load(no_window);
    if (Settings_FirstRun() && !no_window) {
        /* dw2-pc-port: the first start with a window gets the Remastered profile (saved; F1 or
         * --profile classic change it) */
        static const int ids[] = { SET_SCALE, SET_WIDE, SET_PGXP, SET_SHARPEN, SET_SMOOTH, SET_TEX_FILTER, SET_SMOOTH2D, SET_UI_STYLE };
        size_t k;

        for (k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
            if (!Settings_Overridden(ids[k])) {
                Settings_Set(ids[k], Settings_ProfileValue(SET_PROFILE_REMASTERED, ids[k]));
            }
        }
        printf("[settings] first start: HD profile\n");
        Host_LanguageAtStart = 1; /* the language picker (host/ui.cpp) */
        /* dw2-pc-port: the PC menus in German when the system prefers German */
        if (!Settings_Overridden(SET_LANGUAGE)) {
            int n = 0;
            SDL_Locale **loc = SDL_GetPreferredLocales(&n);

            if (loc != NULL && n > 0 && loc[0]->language != NULL && SDL_strcasecmp(loc[0]->language, "de") == 0) {
                Settings_Set(SET_LANGUAGE, SET_LANG_DE);
            }
            SDL_free(loc);
            printf("[settings] first start: menu language %s\n", Settings_Get(SET_LANGUAGE) == SET_LANG_DE ? "de" : "en");
        }
    }
    PsxHd_SetScale(Host_ResolveScale(Settings_Get(SET_SCALE)));
    Host_SetWide(Settings_Get(SET_WIDE));
    if (Settings_Get(SET_PGXP)) {
        Host_SetPgxp(1);
    }
    Host_SetSharpen(Settings_Get(SET_SHARPEN)); /* the override's value with --sharpen */
    Host_ApplyPost(); /* dw2-pc-port: fxaa, brightness, contrast, saturation, gamma */
    PsxHd_SetTexFilter(Settings_Get(SET_TEX_FILTER) == SET_TEXF_BILINEAR); /* dw2-pc-port */
    PsxHd_SetSmooth2d(Settings_Get(SET_SMOOTH2D));
    if (Settings_Get(SET_SMOOTH)) {
        Host_SetSmooth(1); /* PR.34, also with --smooth */
    }
    if (!cl_renderer && Settings_Get(SET_RENDERER) == SET_RENDERER_SOFT) {
        Host_SetRenderer(1);
    }
    /* PR.30: the update check first, so a newer release is offered before the pack is built */
    Update_Start(no_window, update_force);
    if (Update_AskAtStart(no_window, argc, argv)) {
        SDL_Quit();
        return 0;
    }
    /* Main thread, before the game thread and window: the pack build may show its own progress
     * window and a file dialog. */
    Host_PakOpen(pak, disc, no_window);
    {
        /* dw2-pc-port texture packs: Mods/Textures/<pack>/ in the data folder, then next to the
         * exe (the later wins for the same texture); only the software HD renderer draws them */
        extern void TexRep_AddRoot(const char *dir);
        extern void TexRep_SetDump(const char *dir);
        extern void TexRep_Disable(void);
        extern int TexRep_Count(void);
        extern int TexRep_PackCount(void);
        extern void TexRep_SetLog(const char *path);

        if (no_texture_packs || export_dir != NULL || import_png != NULL || export_texts != NULL) {
            TexRep_Disable();
        } else {
            char *pref = SDL_GetPrefPath("", "DW2-Online"), *dir = NULL;
            const char *base = SDL_GetBasePath();
            if (pref != NULL) {
                SDL_asprintf(&dir, "%sMods/Textures", pref);
                TexRep_AddRoot(dir);
                SDL_free(dir);
                SDL_free(pref);
            }
            if (base != NULL) {
                SDL_PathInfo info;
                dir = NULL;
                SDL_asprintf(&dir, "%sMods/Textures", base);
                if (dir != NULL && SDL_GetPathInfo(dir, &info)) { /* a log only where packs live */
                    char *lp = NULL;
                    SDL_asprintf(&lp, "%s/textures_log.txt", dir);
                    TexRep_SetLog(lp);
                    SDL_free(lp);
                }
                TexRep_AddRoot(dir);
                SDL_free(dir);
            }
            if (dump_textures != NULL) {
                TexRep_SetDump(dump_textures);
            }
            printf("[textures] %d pack(s), %d replacement files%s\n", TexRep_PackCount(), TexRep_Count(),
                   TexRep_Count() > 0 ? " (drawn by the software renderer at scale 2 and above)" : "");
        }
    }
    {
        /* dw2-pc-port: the game's text dictionary for decoding, then the German text layer */
        extern void DevData_InitConst(void);
        extern void Lang_Apply(void);

        DevData_InitConst();
        if (export_dir == NULL && import_png == NULL) {
            extern void UiText_Init(void);
            Lang_Apply();
            UiText_Init(); /* dw2-pc-port: glyph metrics for the PC text layout (host/uilayout.c) */
        }
    }
    if (export_texts != NULL) {
        int r = Lang_ExportTexts(export_texts);

        SDL_Quit();
        return r;
    }
    if (import_png != NULL) {
        int r = Assets_ImportTexture(import_png);

        SDL_Quit();
        return r;
    }
    if (export_dir != NULL) {
        int r = Assets_Export(export_dir, NULL);

        SDL_Quit();
        return r == 0 ? 0 : 1;
    }
#if DW2_DEV || DW2_CHEATS
    DevEdit_Init();
    if (cheats_at_start) {
        Cheat_SetActive(1);
    }
#endif
    if (!no_window) {
        run_threaded();
    }
#ifdef _WIN32
    game_main();
#else
    run_on_low_stack();
#endif
    return 0;
}
