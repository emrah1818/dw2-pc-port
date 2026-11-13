#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/psxgpu.h"
#include "backend/pgxp.h"
#include "backend/psxgpu_hd.h"
#include "backend/texrep.h"
#include "host/host.h"
#include "host/host_sdl.h"
#ifndef _WIN32
#include "host/icon_rgba.h"
#endif
#include "host/settings.h"
#include "host/ui.h"
#include "host/update.h"
#include "psyq/gte_core.h"
#if DW2_DEV
#include "host/devsnap.h"
#include "host/devui.h"
#endif

/* SDL3 host: one window (320x240 logical, letterboxed, resizable) showing the emulated GPU's
 * display area (P1.4), stretched to the 4:3 picture: 240-line modes scale by whole pixels
 * (nearest), 480-line modes linear. Black while the display is off. Display vsync is off. HD
 * output (PG.1, --scale N, F5): the display area from the HD surface, w*S x h*S, filtered linear
 * to the window. PG.2 (--pgxp, F6): precise GTE vertices for the HD output. PG.3 (--wide, F7):
 * 16:9 picture (427x240 logical), the 4:3 one pillarboxed in it. PG.10b GPU renderer
 * (--renderer gpu, the default with a window): an SDL_GPU device (Vulkan, SPIR-V) draws the HD
 * surfaces (backend/psxgpu_hw.c) and the SDL renderer runs on the same device, showing GPU
 * textures directly (no readback); --renderer soft keeps the software HD rasterizer.
 *
 * PG.10 b3: with a visible window the game runs on its own thread (host/main.c) and the window
 * stays on the main thread (SDL video is main-thread only), so a title-bar drag or a slow present
 * no longer stops the game or the sound. The game thread publishes the displayed picture at each
 * VBlank wait (Host_Publish: GPU: a copy of the display area into a slot texture; soft: the 1x or
 * HD picture into a slot buffer); the main thread shows the newest one (Host_Present), never the
 * one being written (3 slots: newest, shown, written). Hotkeys and quit go to the game thread as
 * requests (Host_GameEvents at its next VBlank wait). Headless runs keep one thread, no present.
 *
 * PR.2: the ImGui layer (host/ui.cpp: the F1 settings window, in dev builds also the PD.1 dev
 * overlay, F2) draws last in Host_Present; the game thread fills the dev snapshot in
 * Host_GameEvents (host/devsnap.c). With both windows closed nothing of this runs.
 * --settings-ui (or dev --devui) with --no-window: the UI also runs headless (publish and present
 * into the hidden window, shots add the UI picture). Display settings changed in F1 reach the game
 * thread as requests (Host_RequestSetting), like F5 / F6 / F7; both update the settings file.
 *
 * PR.3 window modes (windowed, borderless fullscreen, exclusive fullscreen; F11, F1, settings
 * window_mode / fullscreen_display / fullscreen_mode): main thread only, the game thread never
 * sees a mode change (the logical presentation letterboxes the picture into any window size, the
 * GPU renderer's swapchain follows the window). The windowed size, position and maximized state
 * are saved when the player changes them. Headless runs keep the hidden window windowed.
 *
 * PR.2b sharpening (F8, F1, settings sharpen): the game thread sharpens the HD picture when it
 * publishes it (GPU: PsxHw_Sharpen instead of the copy; soft: PsxHd_ReadDisplay). PR.10: the game
 * thread notes at each VBlank wait whether the title runs, for the host's F1 hint (host/ui.cpp).
 *
 * PR.34 smooth frames (on by default; F9, F1, settings smooth, --smooth; GPU renderer): the game thread hands each
 * frame that can be drawn again over as a package (backend/psxgpu_hd.c); the window thread
 * presents with vsync at the monitor's refresh rate and draws the newest package for the time of
 * each picture, blended between the frame before and it (PsxHd_SmoothPicture). The game thread
 * never draws those pictures, so they cannot hold up its VBlank clock. Frames without a package
 * are shown from the slots as before. */

#define WINDOW_SCALE 3
#define SLOTS 3

enum { REQ_SCALE = 1, REQ_WIDE = 2, REQ_PGXP = 4, REQ_SHOT = 8, REQ_QUIT = 16, REQ_SETTING = 32, REQ_SHARPEN = 64,
       REQ_SPEED = 128, REQ_SMOOTH = 256, REQ_TEXF = 512 };

typedef struct {
    int kind;      /* 0 black, 1 1x picture, 2 software HD picture, 3 GPU texture */
    int w, h;      /* picture size */
    int margin;    /* 16:9 margin of the picture (PsxHd_LastMargin), output pixels */
    int logical_w; /* window picture width: 320, 427 in 16:9 */
    Uint32 *px;    /* kinds 1, 2 */
    size_t cap;
    void *tex;     /* kind 3: GPU texture tex_w x tex_h */
    int tex_w, tex_h;
    unsigned gen;  /* changes when tex is replaced */
    unsigned seq;  /* publish number */
} Frame;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture; /* 640x480 streaming, the display area in its top-left corner */
static SDL_Texture *hd_texture;
static int hd_w, hd_h;
static int headless;
static int threaded;
static int logical_w = 320; /* 427 in 16:9 (game side) */
static int shown_logical_w; /* the window's logical presentation (main thread) */
static SDL_AtomicInt out_pic_h; /* dw2-pc-port: picture height in output pixels (main thread writes) */
static int auto_want, auto_stable; /* game thread: auto scale candidate and how long it held */
static int initialized;
static int renderer_choice; /* 0 automatic (GPU with a window), 1 soft, 2 GPU */
static SDL_GPUDevice *gpudev;

static Frame frames[SLOTS];
static int latest = -1, in_use = -1; /* under frame_lock */
static unsigned pub_seq;
static unsigned pub_serial; /* PsxHd_Serial of the newest HD picture */
static int pub_valid;       /* the newest picture is an HD one with that serial */
static SDL_Mutex *frame_lock;
static SDL_Semaphore *frame_sem;
static SDL_Texture *wraps[SLOTS]; /* main thread: the slot textures as renderer textures */
static unsigned wrap_gen[SLOTS];
static unsigned shown_seq[SLOTS];
static SDL_AtomicInt requests;
static SDL_AtomicInt wanted[SET_COUNT]; /* REQ_SETTING: F1's values (+1) for scale, wide, pgxp, sharpen, smooth */
static SDL_AtomicInt game_done;
static SDL_AtomicInt on_title; /* PR.10: the game thread saw the title scene at its last VBlank wait */
static int last_sharpen = 50;  /* F8 turns sharpening back on at this strength */
static const char *volatile quit_why = "window closed";
static SDL_AtomicInt smooth_on;  /* PR.34: smooth frames active (game side sets it) */
static int shown_vsync;          /* main thread: the renderer's vsync */
static int vsync_ok = 1;         /* main thread: vsync could be turned on */
/* SDL's Vulkan backend presents while holding its shared submit lock. Use mailbox when
 * available and pace the window outside GPU calls, so the game can keep submitting. */
static int mailbox_supported;
static SDL_AtomicInt mailbox_pacing;
static SDL_AtomicInt window_present_us, window_presents;
static SDL_Texture *smooth_wrap; /* main thread: the smooth picture as a renderer texture */
static void *smooth_wrap_tex;
static int smooth_wrap_w, smooth_wrap_h;

/* PR.3, main thread */
static int win_mode;         /* the mode applied to the window */
static int last_fs = SET_WINDOW_BORDERLESS; /* F11 from windowed goes here */
static int geo_x, geo_y, geo_w, geo_h, geo_max; /* the windowed rect last seen */
static int moved_for_display; /* the window was moved to fullscreen_display: */
static int pre_x, pre_y;      /* where it was before */
static int cursor_hidden;
static void saved_geometry(int *w, int *h, int *x, int *y);
static void remember_geometry(void);

/* Headless runs publish and present only for the headless UI checks (--settings-ui, --devui). */
static int no_present(void) {
    return headless && !Ui_Headless();
}

static void fail(const char *what) {
    char msg[512];

    snprintf(msg, sizeof(msg), "%s failed: %s", what, SDL_GetError());
    fprintf(stderr, "[host] %s\n", msg);
    /* Windows release builds have no console (host/winconsole.c): say it in a box too */
    if (!headless) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 2", msg, NULL);
    }
    exit(1);
}

void Host_SetRenderer(int choice) {
    renderer_choice = choice;
}

const char *Host_RendererName(void) {
    return gpudev != NULL ? "GPU (Vulkan)" : "Software";
}

void Host_SetThreaded(int on) {
    threaded = on;
}

int Host_Threaded(void) {
    return threaded;
}

/* GPU device + renderer on it, or 0 (the caller falls back to the default renderer). */
static int init_gpu(void) {
    gpudev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
    if (gpudev == NULL) {
        printf("[host] GPU renderer unavailable (%s), software HD output\n", SDL_GetError());
        return 0;
    }
    if (!PsxHw_Init(gpudev)) {
        SDL_DestroyGPUDevice(gpudev);
        gpudev = NULL;
        return 0;
    }
    renderer = SDL_CreateGPURenderer(gpudev, window);
    if (renderer == NULL && headless && getenv("DW2_GPU_HEADLESS") != NULL) {
        /* measurements without a display (lavapipe has no headless surface): the GPU draws, the
           pictures are not shown (no_present), the window gets a plain renderer */
        printf("[host] GPU renderer: no presentation (%s), the GPU draws headless\n", SDL_GetError());
        renderer = SDL_CreateRenderer(window, NULL);
        if (renderer != NULL) {
            PsxHd_SetGpu(1);
            return 1;
        }
    }
    if (renderer == NULL) {
        printf("[host] GPU renderer: SDL_CreateGPURenderer failed (%s), software HD output\n", SDL_GetError());
        PsxHw_Shutdown();
        SDL_DestroyGPUDevice(gpudev);
        gpudev = NULL;
        return 0;
    }
    PsxHd_SetGpu(1);
    mailbox_supported = SDL_WindowSupportsGPUPresentMode(gpudev, window, SDL_GPU_PRESENTMODE_MAILBOX);
    return 1;
}

/* Main thread: SDL, window, renderer, gamepads, audio device. */
void Host_InitWindow(int no_window) {
    int v = SDL_GetVersion();
    int want_gpu = renderer_choice == 2 || (renderer_choice == 0 && !no_window);
    int win_w = logical_w * WINDOW_SCALE, win_h = 240 * WINDOW_SCALE;
    int win_x = SDL_WINDOWPOS_CENTERED, win_y = SDL_WINDOWPOS_CENTERED;
    SDL_PropertiesID props;

    if (no_window) {
        headless = 1;
        if (!want_gpu) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        }
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fail("SDL_Init");
    }
    initialized = 1;
    printf("[host] SDL %d.%d.%d, video driver %s\n", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
           SDL_VERSIONNUM_MICRO(v), SDL_GetCurrentVideoDriver());
    if (!no_window) {
        saved_geometry(&win_w, &win_h, &win_x, &win_y);
    }
#if DW2_DEV
    DevUi_WindowSize(&win_w, &win_h);
#endif
    props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "DW2-PC");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, win_x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, win_y);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, win_w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, win_h);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                          SDL_WINDOW_RESIZABLE | (no_window ? SDL_WINDOW_HIDDEN : 0) |
                              (!no_window && Settings_Get(SET_WIN_MAX) ? SDL_WINDOW_MAXIMIZED : 0));
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (window == NULL) {
        fail("SDL_CreateWindow");
    }
    remember_geometry();
#ifndef _WIN32
    /* Windows takes the icon from the exe (host/dw2.rc); elsewhere it comes from here. */
    {
        SDL_Surface *icon = SDL_CreateSurfaceFrom(DW2_ICON_W, DW2_ICON_H, SDL_PIXELFORMAT_RGBA32,
                                                  (void *)dw2_icon_rgba, DW2_ICON_W * 4);
        if (icon != NULL) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }
#endif
    if (!want_gpu || !init_gpu()) {
        renderer = SDL_CreateRenderer(window, NULL);
    }
    if (renderer == NULL) {
        fail("SDL_CreateRenderer");
    }
    SDL_SetRenderVSync(renderer, 0);
    SDL_SetRenderLogicalPresentation(renderer, logical_w, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    shown_logical_w = logical_w;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
    if (texture == NULL) {
        fail("SDL_CreateTexture");
    }
    frame_lock = SDL_CreateMutex();
    frame_sem = SDL_CreateSemaphore(0);
    if (frame_lock == NULL || frame_sem == NULL) {
        fail("SDL_CreateMutex");
    }
    Host_InputInit();
    printf("[host] window %dx%d (%dx240 logical), renderer %s, vsync off%s\n", win_w, win_h, logical_w,
           SDL_GetRendererName(renderer), threaded ? ", game thread" : "");
    Ui_Init(window, renderer, no_window);
    Host_AudioOpen();
    win_mode = SET_WINDOW_WINDOWED;
    if (Settings_Get(SET_WINDOW_MODE) != SET_WINDOW_WINDOWED) {
        last_fs = Settings_Get(SET_WINDOW_MODE);
        Host_SetWindowMode(Settings_Get(SET_WINDOW_MODE));
    }
    fflush(stdout);
}

/* ---- PR.3 window modes, main thread ---- */

/* window_size / window_pos from the settings, if the saved rect still lies on a display. */
static void saved_geometry(int *w, int *h, int *x, int *y) {
    int sw, sh, sx, sy, n = 0, i, on = 0;
    SDL_DisplayID *ids;

    if (Settings_WindowSize(&sw, &sh)) {
        *w = sw;
        *h = sh;
    }
    if (!Settings_WindowPos(&sx, &sy)) {
        return;
    }
    /* the title bar must be on a display (a monitor may be gone since) */
    ids = SDL_GetDisplays(&n);
    for (i = 0; i < n && !on; i++) {
        SDL_Rect b, bar = { sx, sy, *w, 32 }, cut;

        on = SDL_GetDisplayBounds(ids[i], &b) && SDL_GetRectIntersection(&b, &bar, &cut) && cut.w >= 64;
    }
    SDL_free(ids);
    if (on) {
        *x = sx;
        *y = sy;
    } else {
        printf("[host] saved window position %d,%d is on no display: centred\n", sx, sy);
    }
}

static void remember_geometry(void) {
    SDL_GetWindowPosition(window, &geo_x, &geo_y);
    SDL_GetWindowSize(window, &geo_w, &geo_h);
    geo_max = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
}

/* A move / resize / maximize while windowed: saved when it differs from what was seen last, so
 * the events of the window's own creation or of a return from fullscreen save nothing. */
static void geometry_event(void) {
    SDL_WindowFlags fl;
    int x, y, w, h, max;

    if (headless || window == NULL) {
        return;
    }
    fl = SDL_GetWindowFlags(window);
    if (fl & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED)) {
        return;
    }
    max = (fl & SDL_WINDOW_MAXIMIZED) != 0;
    if (max != geo_max) {
        geo_max = max;
        Settings_Set(SET_WIN_MAX, max);
    }
    if (max) {
        return; /* the normal rect stays the saved one */
    }
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    if (x != geo_x || y != geo_y) {
        geo_x = x;
        geo_y = y;
        Settings_SetWindowPos(x, y);
    }
    if (w != geo_w || h != geo_h) {
        geo_w = w;
        geo_h = h;
        Settings_SetWindowSize(w, h);
    }
}

/* fullscreen_display: 0 = the display the window is on, N = the Nth display. */
static SDL_DisplayID fullscreen_display(void) {
    int want = Settings_Get(SET_FS_DISPLAY), n = 0;
    SDL_DisplayID *ids, id = 0;

    if (want > 0) {
        ids = SDL_GetDisplays(&n);
        if (ids != NULL && want <= n) {
            id = ids[want - 1];
        } else {
            printf("[host] fullscreen_display %d: only %d display(s), using the window's\n", want, n);
        }
        SDL_free(ids);
    }
    return id != 0 ? id : SDL_GetDisplayForWindow(window);
}

int Host_WindowMode(void) {
    return win_mode;
}

int Host_WindowHeadless(void) {
    return headless;
}

void Host_SetWindowMode(int mode) {
    SDL_DisplayID id;
    SDL_DisplayMode m;
    const SDL_DisplayMode *dm;
    int fw, fh, fhz;

    if (headless || window == NULL) {
        win_mode = mode;
        printf("[host] window mode %s (headless: not applied)\n", Settings_WindowModeName(mode));
        fflush(stdout);
        return;
    }
    if (mode == SET_WINDOW_WINDOWED) {
        SDL_SetWindowFullscreen(window, false);
        SDL_SyncWindow(window);
        if (moved_for_display) {
            SDL_SetWindowPosition(window, pre_x, pre_y);
            moved_for_display = 0;
        }
        win_mode = mode;
        Ui_Notice("Windowed (F11)");
        printf("[host] window mode windowed\n");
        fflush(stdout);
        return;
    }
    id = fullscreen_display();
    if (id != SDL_GetDisplayForWindow(window)) {
        /* to the chosen display first, out of fullscreen if needed */
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) {
            SDL_SetWindowFullscreen(window, false);
            SDL_SyncWindow(window);
        }
        if (!moved_for_display) {
            SDL_GetWindowPosition(window, &pre_x, &pre_y);
            moved_for_display = 1;
        }
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
        SDL_SyncWindow(window);
    }
    if (mode == SET_WINDOW_EXCLUSIVE) {
        Settings_FsMode(&fw, &fh, &fhz);
        dm = SDL_GetDesktopDisplayMode(id);
        if (fw != 0 && SDL_GetClosestFullscreenDisplayMode(id, fw, fh, fhz / 100.0f, false, &m)) {
            dm = &m;
        } else if (fw != 0) {
            printf("[host] fullscreen_mode %dx%d not offered by the display: desktop mode\n", fw, fh);
        }
        if (dm == NULL || !SDL_SetWindowFullscreenMode(window, dm)) {
            printf("[host] exclusive fullscreen: no display mode (%s), borderless\n", SDL_GetError());
            SDL_SetWindowFullscreenMode(window, NULL);
            mode = SET_WINDOW_BORDERLESS;
        }
    } else {
        SDL_SetWindowFullscreenMode(window, NULL);
    }
    if (!SDL_SetWindowFullscreen(window, true)) {
        printf("[host] fullscreen failed: %s\n", SDL_GetError());
    }
    SDL_SyncWindow(window);
    win_mode = mode;
    last_fs = mode;
    dm = SDL_GetWindowFullscreenMode(window);
    {
        char note[128];

        if (dm != NULL) {
            snprintf(note, sizeof(note), "Exclusive fullscreen %dx%d @ %.0f Hz (F11)", dm->w, dm->h, dm->refresh_rate);
        } else {
            snprintf(note, sizeof(note), "Borderless fullscreen (F11)");
        }
        Ui_Notice(note);
    }
    if (dm != NULL) {
        printf("[host] window mode exclusive, display %u \"%s\", %dx%d @ %.2f Hz\n", (unsigned)id,
               SDL_GetDisplayName(id), dm->w, dm->h, dm->refresh_rate);
    } else {
        int w = 0, h = 0;

        SDL_GetWindowSizeInPixels(window, &w, &h);
        printf("[host] window mode %s, display %u \"%s\", %dx%d\n", Settings_WindowModeName(mode), (unsigned)id,
               SDL_GetDisplayName(id), w, h);
    }
    fflush(stdout);
}

/* F1: the display or the exclusive mode changed: applied again if fullscreen. */
void Host_WindowModeRefresh(void) {
    if (win_mode != SET_WINDOW_WINDOWED) {
        Host_SetWindowMode(win_mode);
    }
}

/* F11: windowed <-> the last fullscreen kind; saved like an F1 change. */
static void toggle_fullscreen(void) {
    int mode = win_mode == SET_WINDOW_WINDOWED ? last_fs : SET_WINDOW_WINDOWED;

    Settings_Set(SET_WINDOW_MODE, mode);
    Host_SetWindowMode(mode);
}

/* Fullscreen: no cursor over the picture while F1 / F2 are closed. */
static void update_cursor(void) {
    int hide = !headless && win_mode != SET_WINDOW_WINDOWED && !Ui_Active();

    if (hide != cursor_hidden) {
        cursor_hidden = hide;
        if (hide) {
            SDL_HideCursor();
        } else {
            SDL_ShowCursor();
        }
    }
}

void Host_SetPgxp(int on) {
    PsxHd_SetPgxp(on);
    Gte_PreciseHook = on || PsxHd_Smooth() ? Pgxp_Record : NULL;
}

/* PR.34, game side (or before the game runs): smooth frames need the PGXP vertices and their
 * sources (no wobble while on) and the GPU renderer. */
void Host_SetSmooth(int on) {
    on = on != 0;
    PsxHd_SetSmooth(on);
    Pgxp_SetSources(on);
    Gte_MatrixHook = on ? Pgxp_Matrix : NULL;
    Gte_PreciseHook = on || PsxHd_Pgxp() ? Pgxp_Record : NULL;
    SDL_SetAtomicInt(&smooth_on, PsxHd_SmoothActive());
}

void Host_SmoothLayer(int id, const void *start, const void *end, int x, int y) {
    PsxHd_SmoothLayer(id, (uintptr_t)start, (uintptr_t)end, x, y);
}

void Host_SmoothLattice(int px, int py, int pz) {
    Pgxp_Lattice(px, py, pz);
}

void Host_SmoothScene(void) {
    PsxHd_SmoothScene();
}

void Host_SmoothCamera(const int *v, int n, int hard) {
    Pgxp_Camera(v, n, hard);
}

/* PR.2b, game side (or before the game runs). */
void Host_ApplyPost(void) {
    PsxHd_SetPost(Settings_Get(SET_FXAA), Settings_Get(SET_BRIGHTNESS), Settings_Get(SET_CONTRAST),
                  Settings_Get(SET_SATURATION), Settings_Get(SET_GAMMA));
}

void Host_SetSharpen(int strength) {
    PsxHd_SetSharpen(strength);
    if (PsxHd_Sharpen() != 0) {
        last_sharpen = PsxHd_Sharpen();
    }
}

/* PR.10, main thread: the title hint may show. */
int Host_TitleHint(void) {
    return SDL_GetAtomicInt(&on_title);
}

/* Game side (or before the window exists); the window follows with the next picture. */
void Host_SetWide(int on) {
    static const int widths[3] = { 320, 427, 560 }; /* 4:3, 16:9, 21:9 */

    PsxHd_SetWide(on);
    logical_w = widths[PsxHd_Wide()];
}

/* dw2-pc-port: the render scale for a scale setting (0 = auto: the smallest scale whose picture
 * is at least as tall as the window's picture, 240 lines per step: 720 -> 3, 1080 -> 5,
 * 1440 -> 6, 2160 -> 9). Before the window shows a frame, auto uses the display height. */
/* dw2-pc-port: auto scale with the software renderer drops a step when the game thread stays late
 * even with the smooth pictures paused (vblank.c): game speed first, then resolution */
static int perf_cap = PSXHD_MAX_SCALE;

int Host_ResolveScale(int setting) {
    int h;

    if (setting > 0) {
        return setting;
    }
    h = SDL_GetAtomicInt(&out_pic_h);
    if (h <= 0) {
        if (headless) {
            return 1;
        }
        if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
            h = 720;
        } else {
            const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());

            h = dm != NULL ? (int)(dm->h * dm->pixel_density) : 720;
            if (Settings_Get(SET_WINDOW_MODE) == SET_WINDOW_WINDOWED) {
                h = h * 2 / 3; /* a window is smaller than the display */
            }
        }
    }
    h = (h + 239) / 240;
    h = h > perf_cap ? perf_cap : h;
    return h < 1 ? 1 : h > PSXHD_MAX_SCALE ? PSXHD_MAX_SCALE : h;
}

static const char *aspect_name(int wide) {
    return wide == 2 ? "21:9" : wide ? "16:9" : "4:3";
}

int Host_WideMargin(int width) {
    return PsxHd_Margin(width);
}

void Host_PillarboxFrame(void) {
    PsxHd_PillarboxFrame();
}

/* ---- game thread: the displayed picture into a free slot ---- */

static int ensure_px(Frame *f, size_t n) {
    if (f->cap < n) {
        Uint32 *p = realloc(f->px, n * sizeof(Uint32));

        if (p == NULL) {
            return 0;
        }
        f->px = p;
        f->cap = n;
    }
    return 1;
}

static void publish_frame(void);

/* DW2_PERF_LOG=<file>: one CSV line per picture of the game thread (frame time, the time in the
   HD renderer's flushes and in Host_Publish, the runtime font tiles, the GPU pool uploads) */
static FILE *perf_f;
static int perf_init;
static Uint64 perf_last, perf_frame;

static void perf_close(void) {
    if (perf_f != NULL) fclose(perf_f);
    perf_f = NULL;
}

static void perf_line(Uint64 pub_ns) {
    Uint64 now = SDL_GetTicksNS();
    TexRepPerf *p = &texrep_perf;
    int srcs, tiles, kib;

    if (!perf_init) {
        const char *path = getenv("DW2_PERF_LOG");
        perf_init = 1;
        perf_f = path != NULL && path[0] ? fopen(path, "w") : NULL;
        if (perf_f != NULL) atexit(perf_close);
        if (perf_f != NULL)
            fprintf(perf_f, "frame,t_ms,frame_ms,flush_ms,publish_ms,finds,hits,new_tiles,new_kpx,full,drops,raster_ms,"
                            "conv_ms,style_ms,gpu_ups,gpu_kib,gpu_up_ms,gpu_deferred,tiles,tile_kib,sleep_ms,vblanks,used_kib,mag1,mag2,mag3,mag4,"
                            "submit_ms,fence_wait_ms,acquire_ms,build_ms,record_ms,submits,fence_waits,package_ms,packages,"
                            "gpu,scale,smooth,late,vsync_setting,fps_limit,language,ui_style,sharpen,smooth2d,version,"
                            "mailbox_pacing,window_present_ms,window_presents\n");
    }
    if (perf_f != NULL && perf_last != 0) {
        TexRep_IndexStats(&srcs, &tiles, &kib);
        fprintf(perf_f, "%llu,%.1f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%.3f,%.3f,%.3f,%llu,%llu,%.3f,%llu,%d,%d,%.3f,%llu,%llu,%llu,%llu,%llu,%llu",
                (unsigned long long)perf_frame, now / 1e6, (now - perf_last) / 1e6, p->flush_ns / 1e6, pub_ns / 1e6,
                (unsigned long long)p->finds, (unsigned long long)p->idx_hits, (unsigned long long)p->idx_new,
                (unsigned long long)(p->idx_new_px / 1000), (unsigned long long)p->idx_full,
                (unsigned long long)p->drops, p->raster_ns / 1e6, p->conv_ns / 1e6, p->style_ns / 1e6,
                (unsigned long long)p->gpu_ups, (unsigned long long)(p->gpu_up_bytes >> 10), p->gpu_up_ns / 1e6,
                (unsigned long long)p->gpu_refused, tiles, kib, p->sleep_ns / 1e6, (unsigned long long)p->vblanks,
                (unsigned long long)p->used_kib, (unsigned long long)p->mag_hist[1], (unsigned long long)p->mag_hist[2],
                (unsigned long long)p->mag_hist[3], (unsigned long long)p->mag_hist[4]);
        fprintf(perf_f, ",%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%.3f,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%.3f,%d\n",
                p->gpu_submit_ns / 1e6, p->gpu_wait_ns / 1e6, p->gpu_acquire_ns / 1e6,
                p->gpu_build_ns / 1e6, p->gpu_record_ns / 1e6,
                (unsigned long long)p->gpu_submits, (unsigned long long)p->gpu_waits,
                p->package_ns / 1e6, (unsigned long long)p->packages,
                PsxHd_Gpu(), PsxHd_Scale(), PsxHd_Smooth(), Host_GameLate(),
                Settings_Get(SET_VSYNC), Settings_Get(SET_FPS_LIMIT), Settings_Get(SET_LANGUAGE),
                Settings_Get(SET_UI_STYLE), Settings_Get(SET_SHARPEN), Settings_Get(SET_SMOOTH2D), DW2_VERSION,
                SDL_GetAtomicInt(&mailbox_pacing), SDL_SetAtomicInt(&window_present_us, 0) / 1000.0,
                SDL_SetAtomicInt(&window_presents, 0));
        if ((perf_frame & 63) == 0) fflush(perf_f);
    }
    memset(p, 0, sizeof(*p));
    if (perf_f == NULL || perf_last == 0) {
        SDL_SetAtomicInt(&window_present_us, 0);
        SDL_SetAtomicInt(&window_presents, 0);
    }
    TexRep_PerfPicture();
    perf_last = now;
    perf_frame++;
}

/* dw2-pc-port: counters for the picture rate display (host/ui.cpp, setting show_fps): the game's
 * own pictures, the pictures the window showed, and the longest time between two game pictures
 * since the last read (microseconds). Counting only: nothing waits on these. */
static SDL_AtomicInt stat_game, stat_window, stat_worst_us;
static Uint64 stat_last_ns;

void Host_PictureStats(unsigned *game, unsigned *window, unsigned *worst_us) {
    *game = (unsigned)SDL_GetAtomicInt(&stat_game);
    *window = (unsigned)SDL_GetAtomicInt(&stat_window);
    *worst_us = (unsigned)SDL_SetAtomicInt(&stat_worst_us, 0);
}

void Host_Publish(void) {
    Uint64 t0 = SDL_GetTicksNS();

    if (stat_last_ns != 0) {
        Uint64 us = (t0 - stat_last_ns) / 1000;
        int v = us > 0x7FFFFFFF ? 0x7FFFFFFF : (int)us;

        if (v > SDL_GetAtomicInt(&stat_worst_us)) {
            SDL_SetAtomicInt(&stat_worst_us, v);
        }
    }
    stat_last_ns = t0;
    SDL_AddAtomicInt(&stat_game, 1);
    publish_frame();
    PsxHd_TrimRuntimeTiles(); /* the picture is out: runtime font tiles may go */
    perf_line(SDL_GetTicksNS() - t0);
}

static void publish_frame(void) {
    Frame *f;
    void *tex;
    int tw, th, sx, sy, sw, sh, w, h, i;

    if (no_present()) {
        /* no window: only what showing the picture does to the HD side (the queue drawn, the
         * display surface made), as before PG.10 b3; the queue must not grow between transfers */
        if (PsxHd_Gpu()) {
            PsxGpu_DisplayTexture(&tex, &tw, &th, &sx, &sy, &sw, &sh);
        } else {
            PsxGpu_ReadDisplayHd(NULL, &w, &h);
        }
        return;
    }
    if (pub_valid && PsxHd_On() && PsxHd_Serial() == pub_serial && logical_w == frames[latest].logical_w) {
        return; /* the HD picture has not changed */
    }
    SDL_LockMutex(frame_lock);
    for (i = 0; i < SLOTS && (i == latest || i == in_use); i++) {
    }
    SDL_UnlockMutex(frame_lock);
    f = &frames[i];
    f->kind = 0;
    if (PsxHd_Gpu() && PsxGpu_DisplayTexture(&tex, &tw, &th, &sx, &sy, &sw, &sh)) {
        if (f->tex == NULL || f->tex_w != sw || f->tex_h != sh) {
            if (f->tex != NULL) {
                PsxHw_Release(f->tex);
            }
            f->tex = PsxHw_Create(sw, sh);
            f->tex_w = sw;
            f->tex_h = sh;
            f->gen++;
        }
        if (f->tex != NULL && PsxHd_SharpenActive()) {
            PsxHw_Sharpen(tex, sx, sy, sw, sh, f->tex, PsxHd_SharpenActive());
        } else if (f->tex != NULL) {
            PsxHw_Copy(tex, sx, sy, sw, sh, f->tex);
        }
        if (f->tex != NULL) {
            f->kind = 3;
            f->w = sw;
            f->h = sh;
        }
    } else if (!PsxHd_Gpu() && PsxGpu_ReadDisplayHd(NULL, &w, &h)) {
        if (ensure_px(f, (size_t)w * h) && PsxGpu_ReadDisplayHd(f->px, &w, &h)) {
            f->kind = 2;
            f->w = w;
            f->h = h;
        }
    } else if (ensure_px(f, 640 * 480) && PsxGpu_ReadDisplay(f->px, &w, &h)) {
        f->kind = 1;
        f->w = w;
        f->h = h;
    }
    f->margin = PsxHd_LastMargin();
    f->logical_w = logical_w;
    pub_serial = PsxHd_Serial();
    pub_valid = f->kind >= 2;
    SDL_LockMutex(frame_lock);
    f->seq = ++pub_seq;
    latest = i;
    SDL_UnlockMutex(frame_lock);
    SDL_SignalSemaphore(frame_sem);
}

/* ---- main thread: the newest picture to the window ---- */

static void draw_hd(SDL_Texture *t, const Frame *f) {
    /* the 4:3 part of the picture is 320 logical pixels wide, the margins beside it */
    float k = 320.0f / (float)(f->w - 2 * f->margin);
    SDL_FRect src = { 0, 0, (float)f->w, (float)f->h };
    SDL_FRect dst = { ((float)f->logical_w - f->w * k) / 2, 0, f->w * k, 240 };

    SDL_RenderTexture(renderer, t, &src, &dst);
}

static void wrap_slot(int slot, const Frame *f) {
    SDL_PropertiesID props;

    if (wraps[slot] != NULL && wrap_gen[slot] == f->gen) {
        return;
    }
    if (wraps[slot] != NULL) {
        SDL_DestroyTexture(wraps[slot]); /* does not release the GPU texture */
    }
    props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, f->tex);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ABGR8888);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, f->tex_w);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, f->tex_h);
    wraps[slot] = SDL_CreateTextureWithProperties(renderer, props);
    SDL_DestroyProperties(props);
    if (wraps[slot] == NULL) {
        fail("GPU picture texture");
    }
    SDL_SetTextureBlendMode(wraps[slot], SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(wraps[slot], SDL_SCALEMODE_LINEAR);
    wrap_gen[slot] = f->gen;
}

/* host/shot.c: pixels w * h words 0x00RRGGBB as a compressed PNG; 0 on failure. */
int Host_WritePngPacked(const char *path, const uint32_t *pixels, int w, int h);

/* PR.22 F12: requested from the game thread, read back by the next Host_Present */
static SDL_AtomicInt shot_wanted;
static char shot_path[1200];
/* PR.23: the PNG is converted, compressed and written by a thread per shot, so F12 costs the
 * render thread only the read back; the result notice comes back through shot_note */
static SDL_AtomicInt shots_busy;
static SDL_SpinLock shot_lock;
static char shot_note[1300];
static int shot_note_set;

typedef struct {
    SDL_Surface *s;
    char path[1200];
} ShotJob;

void Host_WindowShot(const char *path) {
    if (SDL_GetAtomicInt(&shot_wanted)) {
        return;
    }
    SDL_strlcpy(shot_path, path, sizeof(shot_path));
    SDL_SetAtomicInt(&shot_wanted, 1);
}

static int SDLCALL shot_thread(void *data) {
    ShotJob *job = (ShotJob *)data;
    SDL_Surface *c = SDL_ConvertSurface(job->s, SDL_PIXELFORMAT_XRGB8888);
    SDL_PathInfo info;
    Uint64 t0 = SDL_GetTicksNS();
    int ok = 0, w = 0, h = 0, y;

    if (c != NULL) {
        uint32_t *px = (uint32_t *)SDL_malloc((size_t)c->w * c->h * 4);

        w = c->w;
        h = c->h;
        if (px != NULL) {
            for (y = 0; y < c->h; y++) {
                SDL_memcpy(px + (size_t)y * c->w, (const Uint8 *)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 4);
            }
            ok = Host_WritePngPacked(job->path, px, c->w, c->h);
            SDL_free(px);
        }
        SDL_DestroySurface(c);
    }
    if (ok && SDL_GetPathInfo(job->path, &info)) {
        printf("[shot] F12 %s (%dx%d, %llu bytes, %.0f ms)\n", job->path, w, h,
               (unsigned long long)info.size, (double)(SDL_GetTicksNS() - t0) / 1e6);
    } else {
        printf("[shot] cannot write %s (%dx%d)\n", job->path, w, h);
    }
    fflush(stdout);
    SDL_LockSpinlock(&shot_lock);
    SDL_snprintf(shot_note, sizeof(shot_note), ok ? "Screenshot saved: %s" : "Screenshot failed: %s", job->path);
    shot_note_set = 1;
    SDL_UnlockSpinlock(&shot_lock);
    SDL_DestroySurface(job->s);
    SDL_free(job);
    SDL_AddAtomicInt(&shots_busy, -1);
    return 0;
}

/* the game picture in output pixels (the logical presentation's letterbox rect), before the UI
 * layer draws; read with the logical presentation off so the rect is in window pixels */
static void window_shot(void) {
    SDL_FRect fr;
    SDL_Rect r;
    SDL_Surface *s;
    SDL_Thread *t;
    ShotJob *job;
    int lw, lh;
    SDL_RendererLogicalPresentation mode;
    char text[1300];

    if (!SDL_GetRenderLogicalPresentationRect(renderer, &fr)) {
        return;
    }
    r.x = (int)(fr.x + 0.5f);
    r.y = (int)(fr.y + 0.5f);
    r.w = (int)(fr.w + 0.5f);
    r.h = (int)(fr.h + 0.5f);
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    s = SDL_RenderReadPixels(renderer, r.w > 0 && r.h > 0 ? &r : NULL);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
    job = s != NULL ? (ShotJob *)SDL_malloc(sizeof(ShotJob)) : NULL;
    if (job == NULL) {
        printf("[shot] F12 read failed: %s\n", SDL_GetError());
        SDL_DestroySurface(s);
        SDL_snprintf(text, sizeof(text), "Screenshot failed: %s", shot_path);
        Ui_Notice(text);
        return;
    }
    job->s = s;
    SDL_strlcpy(job->path, shot_path, sizeof(job->path));
    SDL_AddAtomicInt(&shots_busy, 1);
    t = SDL_CreateThread(shot_thread, "dw2 shot", job);
    if (t != NULL) {
        SDL_DetachThread(t);
    } else {
        shot_thread(job); /* no thread: write it here */
    }
}

/* PR.23: the shot thread's notice, shown from the render thread */
static void shot_notice(void) {
    char text[1300];
    int set;

    SDL_LockSpinlock(&shot_lock);
    set = shot_note_set;
    if (set) {
        SDL_strlcpy(text, shot_note, sizeof(text));
        shot_note_set = 0;
    }
    SDL_UnlockSpinlock(&shot_lock);
    if (set) {
        Ui_Notice(text);
    }
}

/* PR.34: the smooth picture texture as a renderer texture (main thread) */
static SDL_Texture *wrap_smooth(void *tex, int w, int h) {
    SDL_PropertiesID props;

    if (smooth_wrap != NULL && smooth_wrap_tex == tex && smooth_wrap_w == w && smooth_wrap_h == h) {
        return smooth_wrap;
    }
    if (smooth_wrap != NULL) {
        SDL_DestroyTexture(smooth_wrap); /* does not release the GPU texture */
    }
    props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, tex);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ABGR8888);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    smooth_wrap = SDL_CreateTextureWithProperties(renderer, props);
    SDL_DestroyProperties(props);
    if (smooth_wrap == NULL) {
        fail("GPU smooth picture texture");
    }
    SDL_SetTextureBlendMode(smooth_wrap, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(smooth_wrap, SDL_SCALEMODE_LINEAR);
    smooth_wrap_tex = tex;
    smooth_wrap_w = w;
    smooth_wrap_h = h;
    return smooth_wrap;
}

void Host_Present(void) {
    const Frame *f = NULL;
    int slot;
    void *stex = NULL;
    Frame sf;

    if (no_present()) {
        return;
    }
    SDL_AddAtomicInt(&stat_window, 1);
    SDL_LockMutex(frame_lock);
    if (latest >= 0) {
        in_use = latest;
    }
    slot = in_use;
    SDL_UnlockMutex(frame_lock);
    if (slot >= 0) {
        f = &frames[slot];
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (f != NULL && f->logical_w != shown_logical_w) {
        shown_logical_w = f->logical_w;
        SDL_SetRenderLogicalPresentation(renderer, shown_logical_w, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
    {
        /* dw2-pc-port auto scale: the picture's height in output pixels (letterboxed) */
        int ow = 0, oh = 0;

        if (SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh) && ow > 0 && oh > 0) {
            int lw = shown_logical_w > 0 ? shown_logical_w : 320;
            int ph = ow * 240 / lw < oh ? ow * 240 / lw : oh;

            SDL_SetAtomicInt(&out_pic_h, ph);
        }
    }
    if (f != NULL && SDL_GetAtomicInt(&smooth_on) && !Host_GameLate() &&
        PsxHd_SmoothPicture(SDL_GetTicksNS(), &stex, &sf.w, &sf.h, &sf.margin)) {
        /* PR.34: the newest package drawn for now */
        sf.logical_w = f->logical_w;
        draw_hd(wrap_smooth(stex, sf.w, sf.h), &sf);
    } else if (f != NULL && f->kind == 3) {
        wrap_slot(slot, f);
        draw_hd(wraps[slot], f);
    } else if (f != NULL && f->kind == 2) {
        if (f->w != hd_w || f->h != hd_h || hd_texture == NULL) {
            if (hd_texture != NULL) {
                SDL_DestroyTexture(hd_texture);
            }
            hd_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, f->w, f->h);
            if (hd_texture == NULL) {
                fail("HD texture");
            }
            SDL_SetTextureScaleMode(hd_texture, SDL_SCALEMODE_LINEAR);
            hd_w = f->w;
            hd_h = f->h;
            SDL_memset(shown_seq, 0, sizeof(shown_seq));
        }
        if (shown_seq[slot] != f->seq) {
            SDL_memset(shown_seq, 0, sizeof(shown_seq));
            shown_seq[slot] = f->seq;
            SDL_UpdateTexture(hd_texture, NULL, f->px, f->w * 4);
        }
        draw_hd(hd_texture, f);
    } else if (f != NULL && f->kind == 1) {
        SDL_Rect r = { 0, 0, f->w, f->h };
        SDL_FRect src = { 0, 0, (float)f->w, (float)f->h };
        SDL_FRect dst = { (float)(f->logical_w - 320) / 2, 0, 320, 240 };

        SDL_UpdateTexture(texture, &r, f->px, f->w * 4);
        SDL_SetTextureScaleMode(texture, f->h > 256 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(renderer, texture, &src, &dst);
    }
    if (SDL_GetAtomicInt(&shot_wanted)) {
        window_shot();
        SDL_SetAtomicInt(&shot_wanted, 0);
    }
    shot_notice();
    Ui_Render();
    {
        Uint64 start = SDL_GetTicksNS();
        SDL_RenderPresent(renderer);
        /* Approximate window-thread totals, atomically drained by the game log. */
        SDL_AddAtomicInt(&window_present_us, (int)((SDL_GetTicksNS() - start) / 1000));
        SDL_AddAtomicInt(&window_presents, 1);
    }
}

static void request(int bits) {
    int old;

    do {
        old = SDL_GetAtomicInt(&requests);
    } while (!SDL_CompareAndSwapAtomicInt(&requests, old, old | bits));
}

/* Main thread (PR.15 quit box): quit at the game thread's next VBlank wait. */
void Host_RequestQuit(const char *why) {
    quit_why = why;
    request(REQ_QUIT);
}

/* Main thread (F1 window): a display setting for the game thread (scale, wide, pgxp). */
void Host_RequestSetting(int id, int value) {
    SDL_SetAtomicInt(&wanted[id], value + 1); /* 0 = nothing wanted */
    request(REQ_SETTING);
}

/* Main thread (the one thread when headless): window and device events, then the input state. */
void Host_PumpEvents(void) {
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        if (Ui_Event(&e)) {
            continue; /* F1 / F2, a remap key, or a key for an ImGui field */
        }
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit_why = "window closed";
            request(REQ_QUIT);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!e.key.repeat) {
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    /* only without the ImGui layer: it asks first (PR.15) */
                    quit_why = "Esc";
                    request(REQ_QUIT);
                }
                if (e.key.scancode == SDL_SCANCODE_F12) {
                    request(REQ_SHOT);
                }
                if (e.key.scancode == SDL_SCANCODE_F5) {
                    request(REQ_SCALE);
                }
                if (e.key.scancode == SDL_SCANCODE_F7) {
                    request(REQ_WIDE);
                }
                if (e.key.scancode == SDL_SCANCODE_F6) {
                    request(REQ_PGXP);
                }
                if (e.key.scancode == SDL_SCANCODE_F8) {
                    request(REQ_SHARPEN);
                }
                if (e.key.scancode == SDL_SCANCODE_F9) {
                    request(REQ_SMOOTH);
                }
                if (e.key.scancode == SDL_SCANCODE_F4) {
                    request(REQ_TEXF); /* dw2-pc-port */
                }
                if (e.key.scancode == SDL_SCANCODE_F3) {
                    request(REQ_SPEED);
                }
                if (e.key.scancode == SDL_SCANCODE_F11) {
                    toggle_fullscreen();
                }
            }
            Host_InputPress(&e);
            break;
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            geometry_event();
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            Host_InputPress(&e);
            break;
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            Host_InputRelease(&e);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            Host_InputDevice(&e);
            break;
        default:
            break;
        }
    }
    Host_InputPoll();
    update_cursor();
    Settings_SaveIfDirty();
}

/* A hotkey's new value as a notice (F5 / F6 / F7: game thread; the window shows it). */
static void notice(const char *fmt, ...) {
    char text[128];
    va_list ap;

    if (headless) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    Ui_Notice(text);
}

/* Game thread, once per VBlank wait: the window's requests, then the pad state. A key hit twice
 * between two VBlank waits counts once. */
void Host_GameEvents(void) {
    int r = SDL_SetAtomicInt(&requests, 0);

    if (r & REQ_SHOT) {
        Host_ShotKey();
    }
    if (r & REQ_SETTING) {
        int v;

        if ((v = SDL_SetAtomicInt(&wanted[SET_SCALE], 0)) != 0) {
            Settings_Set(SET_SCALE, v - 1);
            PsxHd_SetScale(Host_ResolveScale(v - 1));
            printf("[gpu] scale %s%dx (F1)\n", v - 1 == 0 ? "auto " : "", PsxHd_Scale());
        }
        if ((v = SDL_SetAtomicInt(&wanted[SET_WIDE], 0)) != 0 && v - 1 != PsxHd_Wide()) {
            Host_SetWide(v - 1);
            printf("[gpu] aspect %s (F1)\n", aspect_name(PsxHd_Wide()));
            Settings_Set(SET_WIDE, PsxHd_Wide());
        }
        if ((v = SDL_SetAtomicInt(&wanted[SET_PGXP], 0)) != 0 && v - 1 != PsxHd_Pgxp()) {
            Host_SetPgxp(v - 1);
            printf("[gpu] no wobble (PGXP) %s (F1)\n", PsxHd_Pgxp() ? "on" : "off");
            Settings_Set(SET_PGXP, PsxHd_Pgxp());
        }
        {
            /* dw2-pc-port: picture filters (F1) */
            static const int ids[] = { SET_FXAA, SET_BRIGHTNESS, SET_CONTRAST, SET_SATURATION, SET_GAMMA };
            size_t k;
            int any = 0;

            for (k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
                if ((v = SDL_SetAtomicInt(&wanted[ids[k]], 0)) != 0) {
                    Settings_Set(ids[k], v - 1);
                    any = 1;
                }
            }
            if (any) {
                Host_ApplyPost();
            }
        }
        if ((v = SDL_SetAtomicInt(&wanted[SET_SHARPEN], 0)) != 0 && v - 1 != PsxHd_Sharpen()) {
            Host_SetSharpen(v - 1);
            printf("[gpu] sharpen %d (F1)\n", PsxHd_Sharpen());
            Settings_Set(SET_SHARPEN, PsxHd_Sharpen());
        }
        if ((v = SDL_SetAtomicInt(&wanted[SET_SMOOTH], 0)) != 0 && v - 1 != PsxHd_Smooth()) {
            Host_SetSmooth(v - 1);
            printf("[gpu] smooth frames %s (F1)%s\n", PsxHd_Smooth() ? "on" : "off",
                   PsxHd_Smooth() && !PsxHd_Gpu() ? ": needs the GPU renderer" : "");
            Settings_Set(SET_SMOOTH, PsxHd_Smooth());
        }
        if ((v = SDL_SetAtomicInt(&wanted[SET_SMOOTH2D], 0)) != 0) {
            Settings_Set(SET_SMOOTH2D, v - 1);
            PsxHd_SetSmooth2d(v - 1);
            printf("[gpu] 2D smoothing %s (F1)\n", PsxHd_Smooth2d() ? "on" : "off");
        }
        if ((v = SDL_SetAtomicInt(&wanted[SET_TEX_FILTER], 0)) != 0) {
            Settings_Set(SET_TEX_FILTER, v - 1);
            PsxHd_SetTexFilter(v - 1 == SET_TEXF_BILINEAR);
            printf("[gpu] texture filter %s (F1)\n", PsxHd_TexFilter() ? "bilinear" : "nearest");
        }
        fflush(stdout);
    }
    if (r & REQ_TEXF) {
        Settings_Set(SET_TEX_FILTER, PsxHd_TexFilter() ? SET_TEXF_NEAREST : SET_TEXF_BILINEAR);
        PsxHd_SetTexFilter(Settings_Get(SET_TEX_FILTER) == SET_TEXF_BILINEAR);
        printf("[gpu] texture filter %s (F4)\n", PsxHd_TexFilter() ? "bilinear" : "nearest");
        notice("Texture filter %s (F4)%s", PsxHd_TexFilter() ? "bilinear" : "off",
               PsxHd_TexFilter() && PsxHd_Scale() == 1 && !PsxHd_Wide() ? ", needs scale 2x or more" : "");
        fflush(stdout);
    }
    if (r & REQ_SHARPEN) {
        Host_SetSharpen(PsxHd_Sharpen() != 0 ? 0 : last_sharpen);
        printf("[gpu] sharpen %d (F8)\n", PsxHd_Sharpen());
        if (PsxHd_Sharpen() == 0) {
            notice("Sharpening off (F8)");
        } else {
            notice("Sharpening %d (F8)%s", PsxHd_Sharpen(), PsxHd_Scale() > 1 ? "" : ", needs scale 2x or more");
        }
        Settings_Set(SET_SHARPEN, PsxHd_Sharpen());
        fflush(stdout);
    }
    if (r & REQ_SMOOTH) {
        Host_SetSmooth(!PsxHd_Smooth());
        printf("[gpu] smooth frames %s (F9)%s\n", PsxHd_Smooth() ? "on" : "off",
               PsxHd_Smooth() && !PsxHd_Gpu() ? ": needs the GPU renderer" : "");
        if (PsxHd_Smooth() && !PsxHd_Gpu()) {
            notice("Smooth frames on (F9): needs the GPU renderer");
        } else {
            notice("Smooth frames %s (F9)", PsxHd_Smooth() ? "on" : "off");
        }
        Settings_Set(SET_SMOOTH, PsxHd_Smooth());
        fflush(stdout);
    }
    if (r & REQ_SPEED) {
        Host_SpeedSet(!Host_SpeedOn());
        if (!Host_SpeedOn()) {
            printf("[host] speed-up off (F3)\n");
            notice("Speed locked (F3)");
        } else if (Settings_Get(SET_SPEED) == 0) {
            printf("[host] speed-up on, no limit (F3)\n");
            notice("Speed unlocked: no limit (F3)");
        } else {
            printf("[host] speed-up on, %dx (F3)\n", Settings_Get(SET_SPEED));
            notice("Speed unlocked: %dx (F3)", Settings_Get(SET_SPEED));
        }
        fflush(stdout);
    }
    if (r & REQ_SCALE) {
        /* dw2-pc-port: Auto, 1x .. 12x, Auto */
        int set = (Settings_Get(SET_SCALE) + 1) % (PSXHD_MAX_SCALE + 1);

        Settings_Set(SET_SCALE, set);
        PsxHd_SetScale(Host_ResolveScale(set));
        printf("[gpu] scale %s%dx (F5)\n", set == 0 ? "auto " : "", PsxHd_Scale());
        if (set == 0) {
            notice("Resolution scale auto: %dx, %d lines (F5)", PsxHd_Scale(), 240 * PsxHd_Scale());
        } else {
            notice("Resolution scale %dx, %d lines (F5)", PsxHd_Scale(), 240 * PsxHd_Scale());
        }
        fflush(stdout);
    }
    if (r & REQ_WIDE) {
        /* dw2-pc-port: 4:3 -> 16:9 -> 21:9 -> 4:3 */
        Host_SetWide((PsxHd_Wide() + 1) % 3);
        printf("[gpu] aspect %s (F7)\n", aspect_name(PsxHd_Wide()));
        notice("Aspect ratio %s (F7)", aspect_name(PsxHd_Wide()));
        Settings_Set(SET_WIDE, PsxHd_Wide());
        fflush(stdout);
    }
    if (Settings_Get(SET_SCALE) == 0 && !PsxHd_Gpu()) {
        extern int Host_GameLateEvents(void);
        static int seen_ev;
        static Uint64 last_ev_ns, last_up_ns, first_ns;
        static Uint64 recover_ns = 60000000000ull; /* quiet time before a step up; doubles after a failed one */
        int ev = Host_GameLateEvents();
        Uint64 now = SDL_GetTicksNS();

        if (first_ns == 0) {
            first_ns = now;
        }
        if (ev != seen_ev) {
            /* late again within 20 s of the last time (the pause alone did not help), or late in
               the first 15 s of the run (the start scale is too high for this computer: step down
               at once, so the sound does not run dry while it settles) */
            if (((last_ev_ns != 0 && now - last_ev_ns < 20000000000ull) || now - first_ns < 15000000000ull) &&
                PsxHd_Scale() > 2) {
                if (last_up_ns != 0 && now - last_up_ns < 20000000000ull && recover_ns < 960000000000ull) {
                    recover_ns *= 2; /* the step up did not hold: wait longer next time */
                }
                perf_cap = PsxHd_Scale() - 1;
                PsxHd_SetScale(perf_cap);
                printf("[gpu] auto scale %dx for game speed (the game thread was late)\n", perf_cap);
                fflush(stdout);
            }
            last_ev_ns = now;
            seen_ev = ev;
        } else if (perf_cap < PSXHD_MAX_SCALE && now - (last_ev_ns > last_up_ns ? last_ev_ns : last_up_ns) > recover_ns &&
                   now - first_ns > recover_ns) {
            /* recovery: nothing was late for a long time, so try one step more (the cap lifts one
               step; the window's own size still limits it) */
            perf_cap++;
            last_up_ns = now;
            if (Host_ResolveScale(0) > PsxHd_Scale()) {
                PsxHd_SetScale(Host_ResolveScale(0));
                printf("[gpu] auto scale %dx again (no late frames for %.0f s)\n", PsxHd_Scale(), recover_ns / 1e9);
                fflush(stdout);
            }
        }
    }
    if (Settings_Get(SET_SCALE) == 0) {
        /* dw2-pc-port auto scale: follow the window once a new size held for half a second */
        int want = Host_ResolveScale(0);

        if (want == PsxHd_Scale()) {
            auto_stable = 0;
        } else if (want != auto_want) {
            auto_want = want;
            auto_stable = 0;
        } else if (++auto_stable >= 30) {
            PsxHd_SetScale(want);
            printf("[gpu] auto scale %dx (picture %d lines)\n", PsxHd_Scale(), SDL_GetAtomicInt(&out_pic_h));
            fflush(stdout);
            auto_stable = 0;
        }
    }
    if (r & REQ_PGXP) {
        Host_SetPgxp(!PsxHd_Pgxp());
        printf("[gpu] no wobble (PGXP) %s (F6)\n", PsxHd_Pgxp() ? "on" : "off");
        notice("No wobble %s (F6)", PsxHd_Pgxp() ? "on" : "off");
        Settings_Set(SET_PGXP, PsxHd_Pgxp());
        fflush(stdout);
    }
    if (r & REQ_QUIT) {
        Host_Quit(quit_why);
    }
    SDL_SetAtomicInt(&on_title, Host_OnTitle());
    SDL_SetAtomicInt(&smooth_on, PsxHd_SmoothActive()); /* PR.34: also after the GPU came up */
    Host_InputLatch();
#if DW2_DEV
    DevSnap_Capture();
#endif
    if (!threaded && Ui_Headless()) {
        Host_Present(); /* --settings-ui / --devui headless: the UI into the hidden window */
    }
}

/* PR.34, main thread: vsync on while smooth frames are on (one picture per refresh). */
static int smooth_vsync(void) {
    int sm = SDL_GetAtomicInt(&smooth_on) && !(SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
    /* dw2-pc-port: the vsync setting turns it on without smooth frames too */
    int want = sm || (Settings_Get(SET_VSYNC) && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED));

    if (want != shown_vsync) {
        int mailbox = want && mailbox_supported;
        shown_vsync = want;
        /* SDL 3.4.8 GPU_SetVSync(0) selects MAILBOX before IMMEDIATE. Only use this
         * for requested synchronization when mailbox is actually supported: no tearing
         * fallback, no preference rewrite. FIFO remains the fallback on other devices. */
        vsync_ok = SDL_SetRenderVSync(renderer, want && !mailbox ? 1 : 0);
        SDL_SetAtomicInt(&mailbox_pacing, mailbox && vsync_ok);
        if (mailbox && !vsync_ok) {
            vsync_ok = SDL_SetRenderVSync(renderer, 1);
        }
        printf("[host] vsync %s%s%s\n", want ? (sm ? "on (smooth frames)" : "on (setting)") : "off",
               SDL_GetAtomicInt(&mailbox_pacing) ? " [mailbox, window-thread pacing]" : "",
               vsync_ok ? "" : ": not available");
        fflush(stdout);
    }
    return sm;
}

/* Sleep outside SDL's device/submit locks. Mailbox is tear-free but does not itself
 * wait for each refresh; cap it to the display rate (and a lower user FPS cap). */
static void pace_window(int smooth) {
    static Uint64 next;
    int mailbox = SDL_GetAtomicInt(&mailbox_pacing);
    int lim = Settings_Get(SET_FPS_LIMIT);
    double hz = lim;
    Uint64 now = SDL_GetTicksNS();

    if (mailbox || (smooth && lim <= 0 && !vsync_ok)) {
        const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        double refresh = dm != NULL && dm->refresh_rate > 0.0f ? dm->refresh_rate : 60.0;
        if (hz <= 0 || hz > refresh) hz = refresh;
    }
    if (!smooth && !mailbox) {
        next = 0;
        return;
    }
    if (hz > 0) {
        Uint64 step = (Uint64)(1000000000.0 / hz + 0.5);
        if (next == 0 || now > next + step) next = now;
        next += step;
        if (next > now) SDL_DelayPrecise(next - now);
    } else {
        next = 0;
    }
}

/* Main thread while the game thread runs: events and present until the game has quit. */
void Host_WindowLoop(void) {
    Uint64 rate_t0 = 0, worst = 0;
    unsigned pics = 0;

    while (!SDL_GetAtomicInt(&game_done)) {
        Host_PumpEvents();
        if (smooth_vsync()) {
            /* One picture per refresh: mailbox uses pace_window, FIFO waits in present. */
            Uint64 t = SDL_GetTicksNS();

            if (Host_GameLate()) {
                /* dw2-pc-port: the game thread misses its VBlanks: only its own frames for now,
                   so the smooth pictures cannot slow the game down (vblank.c) */
                if (SDL_WaitSemaphoreTimeout(frame_sem, 20) || Ui_Drawing()) {
                    Host_Present();
                    pace_window(1);
                }
                continue;
            }
            Host_Present();
            pace_window(1);
            /* the picture rate, about every 10 s */
            if (rate_t0 == 0 || t - rate_t0 > 30000000000ull) {
                rate_t0 = t;
                pics = 0;
                worst = 0;
            } else if (++pics % 64 == 0 && SDL_GetTicksNS() - rate_t0 >= 10000000000ull) {
                printf("[host] smooth frames: %u pictures in %.2f s (%.1f Hz), longest picture %.1f ms\n", pics,
                       (double)(SDL_GetTicksNS() - rate_t0) / 1e9,
                       pics * 1e9 / (double)(SDL_GetTicksNS() - rate_t0), (double)worst / 1e6);
                fflush(stdout);
                rate_t0 = SDL_GetTicksNS();
                pics = 0;
                worst = 0;
            }
            t = SDL_GetTicksNS() - t;
            worst = t > worst ? t : worst;
        } else if (SDL_WaitSemaphoreTimeout(frame_sem, 4)) {
            Host_Present();
            pace_window(0);
        }
        else if (Ui_Drawing()) {
            Host_Present(); /* ImGui needs frames while the game picture stays the same */
            pace_window(0);
        }
    }
    Host_Shutdown();
    exit(0);
}

void Host_Shutdown(void) {
    int i;

    if (!initialized) {
        return;
    }
    initialized = 0;
    while (SDL_GetAtomicInt(&shots_busy) > 0) {
        SDL_Delay(5); /* PR.23: let F12 shots finish writing */
    }
    Settings_Flush();
    Ui_Shutdown();
    TexRep_Shutdown();
    Host_AudioClose();
    Host_InputClose();
    for (i = 0; i < SLOTS; i++) {
        if (wraps[i] != NULL) {
            SDL_DestroyTexture(wraps[i]);
            wraps[i] = NULL;
        }
        if (frames[i].tex != NULL) {
            PsxHw_Release(frames[i].tex);
            frames[i].tex = NULL;
        }
        free(frames[i].px);
        frames[i].px = NULL;
    }
    if (hd_texture != NULL) {
        SDL_DestroyTexture(hd_texture);
        hd_texture = NULL;
    }
    if (texture != NULL) {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }
    if (gpudev != NULL) {
        PsxHd_SetGpu(0);
        PsxHw_Shutdown();
    }
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (gpudev != NULL) {
        SDL_DestroyGPUDevice(gpudev);
        gpudev = NULL;
    }
    if (window != NULL) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
    printf("[host] shutdown\n");
    fflush(stdout);
}

void Host_Quit(const char *why) {
    printf("[host] quit (%s)\n", why);
    Host_LogRate("run");
    if (threaded) {
        /* the game thread stops here; the main thread shuts SDL down and exits */
        SDL_SetAtomicInt(&game_done, 1);
        SDL_SignalSemaphore(frame_sem);
        for (;;) {
            SDL_Delay(1000);
        }
    }
    Host_Shutdown();
    exit(0);
}
