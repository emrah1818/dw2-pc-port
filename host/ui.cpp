/* PR.2 ImGui layer (release and dev): one Dear ImGui context (host/imgui/, pinned in README.txt)
 * with the SDL3 platform backend and the SDL_Renderer backend, on the renderer host/sdl.c
 * presents with (the SDL_GPU renderer or the default one). Main thread only. It draws the F1
 * settings window (host/settingsui.cpp) and, in dev builds, the F2 overlay (host/devui.cpp, PD.1);
 * both are windows in this one context and can be open together. ImGui draws in window pixels
 * over the picture: the renderer's logical presentation is switched off around it.
 * PR.3: a short notice top left (Ui_Notice: window mode after F11 / F1, F5 / F6 / F7 values),
 * drawn by the same layer for 2 s, with or without the windows open.
 * PR.10: "Press F1 for settings" at the bottom left of the picture while the title runs.
 * PR.15: Esc opens a "Quit?" box (Quit / Cancel) instead of quitting at once.
 * PR.38 (issue #5): Select held opens the F1 window (host/input.c); Start or Select closes it. */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

#include "host/settings.h"
#include "host/ui.h"
#include "host/update.h"
#include "host/fonts.h"
#if DW2_DEV
#include "host/devui.h"
#endif

extern "C" int Host_WritePng(const char *path, const uint32_t *pixels, int w, int h);
extern "C" int Host_TitleHint(void);
extern "C" void Host_RequestQuit(const char *why);
extern "C" int Host_InputSelectHoldOn(void);
extern "C" int Host_InputSelectKeyHeld(const SDL_Event *e);
extern "C" int Host_Restart(void);
extern "C" int Host_LanguageAtStart;

namespace {

SDL_Renderer *g_renderer;
bool g_ready;
bool g_headless;
bool g_settings_at_start;
bool g_shots; /* --ui-shots */
/* PR.15 quit question: wanted (Esc toggles it), shown (the ImGui popup is open) */
bool g_quit_want, g_quit_shown;
/* dw2-pc-port: the language picker (first start, Select on the title) */
bool g_lang_open;
int g_lang_step;        /* 0 choose, 1 restart question (in the game) */
Uint64 g_select_down;   /* when Select went down on the title (a short press opens the picker) */
/* PR.38: the gamepad button (Start or Select) that went down while the F1 window was open, -1 if
 * none: its release closes the window */
int g_close_button = -1;

/* PR.3 notice: any thread posts, the main thread draws */
SDL_Mutex *g_notice_lock;
char g_notice[128];
Uint64 g_notice_until; /* SDL_GetTicks */
#define NOTICE_MS 2000

bool notice_live(void) {
    Uint64 until;

    SDL_LockMutex(g_notice_lock);
    until = g_notice_until;
    SDL_UnlockMutex(g_notice_lock);
    return until != 0 && SDL_GetTicks() < until;
}

void draw_notice(void) {
    char text[128];
    Uint64 now = SDL_GetTicks(), until;

    SDL_LockMutex(g_notice_lock);
    SDL_strlcpy(text, g_notice, sizeof(text));
    until = g_notice_until;
    SDL_UnlockMutex(g_notice_lock);
    if (until == 0 || now >= until) {
        return;
    }
    float k = ImGui::GetStyle().FontScaleDpi;
    float a = until - now < 400 ? (float)(until - now) / 400.0f : 1.0f; /* fade out */
    ImGui::SetNextWindowPos(ImVec2(12.0f * k, 12.0f * k));
    ImGui::SetNextWindowBgAlpha(0.7f * a);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    ImGui::Begin("##notice", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(text);
    ImGui::End();
    ImGui::PopStyleVar();
}

/* PR.15: Esc asks before quitting (a modal box in the window centre; Cancel has the focus, so
 * Enter or the gamepad's confirm button on a stray press does not quit). The game keeps running
 * behind it without input, as with F1 open. */
void draw_quit(void) {
    static const char id[] = "Quit##quit";
    float k = ImGui::GetStyle().FontScaleDpi;

    if (g_quit_want && !ImGui::IsPopupOpen(id)) {
        ImGui::OpenPopup(id);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(id, NULL,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        g_quit_shown = true;
        ImGui::TextUnformatted("Quit Digimon World 2?");
        ImGui::TextDisabled("Progress since your last save will be lost.");
        ImGui::Spacing();
        if (ImGui::Button("Quit", ImVec2(110.0f * k, 0))) {
            Host_RequestQuit("Esc, confirmed");
            g_quit_want = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f * k, 0))) {
            g_quit_want = false;
        }
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(-1); /* Cancel */
        }
        if (!g_quit_want) {
            ImGui::CloseCurrentPopup();
            g_quit_shown = false;
        }
        ImGui::EndPopup();
    } else {
        g_quit_shown = false;
    }
}

/* PR.10: "Press F1 for settings" at the bottom left of the picture while the title runs (the
 * game thread's flag), unless switched off or the F1 window is open. Drawn with each presented
 * picture only: it does not keep the window loop presenting. PR.30: with a newer release found,
 * the line says so instead (also when the hint is switched off). */
bool title_hint_shown(void) {
    return g_ready && Host_TitleHint() && (Settings_Get(SET_TITLE_HINT) || Update_NewerTag() != NULL) &&
           !SettingsUi_IsOpen() && !g_quit_want && !g_quit_shown;
}

void draw_title_hint(void) {
    const char *tag = Update_NewerTag();
    char text[96];
    SDL_FRect r;

    if (tag != NULL) {
        SDL_snprintf(text, sizeof(text), "Update %s available: press F1", tag);
    } else {
        bool de = Settings_Get(SET_LANGUAGE) == SET_LANG_DE;
        SDL_strlcpy(text, de ? (Host_InputSelectHoldOn() ? "Select: Sprache / Language   F1 oder Select halten: Einstellungen"
                                                         : "Select: Sprache / Language   F1: Einstellungen")
                             : (Host_InputSelectHoldOn() ? "Select: Language / Sprache   F1 or hold Select: settings"
                                                         : "Select: Language / Sprache   F1: settings"),
                    sizeof(text));
    }

    if (!SDL_GetRenderLogicalPresentationRect(g_renderer, &r) || r.h <= 0) {
        return;
    }
    /* about 1/32 of the picture height, never below the UI's own text size */
    float size = r.h / 32.0f;
    float min = 13.0f * ImGui::GetStyle().FontScaleDpi;
    size = size < min ? min : size;
    float pad = r.h / 60.0f, in = size * 0.35f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    ImVec2 pos(r.x + pad + in, r.y + r.h - pad - in - ts.y);
    ImDrawList *dl = ImGui::GetForegroundDrawList();

    /* a dark box behind it: the title background is busy */
    dl->AddRectFilled(ImVec2(pos.x - in, pos.y - in * 0.6f), ImVec2(pos.x + ts.x + in, pos.y + ts.y + in * 0.6f),
                      IM_COL32(0, 0, 0, 150), in * 0.6f);
    dl->AddText(ImGui::GetFont(), size, pos, tag != NULL ? IM_COL32(255, 214, 90, 235) : IM_COL32(255, 255, 255, 220),
                text);
}

/* the style for the display's content scale (again when the window moves to another display) */
/* dw2-pc-port UI scale: ui_scale percent, or auto (0): the display's content scale, at least
 * the window height / 900 (1080p 1.2, 1440p 1.6, 2160p 2.4), at most 4. */
SDL_Window *g_window;
float g_ui_scale = 1.0f;

float ui_factor(SDL_Window *window) {
    float dpi = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
    int set = Settings_Get(SET_UI_SCALE), w = 0, h = 0;
    float k;

    if (!(dpi > 0.5f && dpi < 8.0f)) {
        dpi = 1.0f;
    }
    if (set > 0) {
        return dpi * (float)set / 100.0f;
    }
    SDL_GetWindowSizeInPixels(window, &w, &h);
    k = (float)h / 900.0f;
    k = k > dpi ? k : dpi;
    return k < 1.0f ? 1.0f : k > 4.0f ? 4.0f : k;
}

void apply_scale(SDL_Window *window) {
    float k = ui_factor(window);

    ImGui::GetStyle() = ImGuiStyle();
    ImGui::StyleColorsDark();
    /* dw2-pc-port: rounder, calmer look than the stock dark style */
    ImGui::GetStyle().WindowRounding = 8.0f;
    ImGui::GetStyle().FrameRounding = 5.0f;
    ImGui::GetStyle().GrabRounding = 5.0f;
    ImGui::GetStyle().TabRounding = 5.0f;
    ImGui::GetStyle().WindowPadding = ImVec2(14.0f, 12.0f);
    ImGui::GetStyle().ItemSpacing = ImVec2(9.0f, 7.0f);
    ImGui::GetStyle().FramePadding = ImVec2(8.0f, 5.0f);
    ImGui::GetStyle().ScaleAllSizes(k);
    ImGui::GetStyle().FontScaleDpi = k;
    g_ui_scale = k;
}

/* dw2-pc-port: follow window size and ui_scale changes (checked each drawn frame) */
void check_scale(void) {
    if (g_window != NULL) {
        float k = ui_factor(g_window);

        if (k < g_ui_scale - 0.02f || k > g_ui_scale + 0.02f) {
            apply_scale(g_window);
        }
    }
}

/* headless shots: requested on the game side, written at the next render */
SDL_AtomicInt g_shot_pending;
char g_shot_path[1024];

bool dev_open(void) {
#if DW2_DEV
    return DevUi_IsOpen() != 0;
#else
    return false;
#endif
}

void write_shot(void) {
    SDL_Surface *s = SDL_RenderReadPixels(g_renderer, NULL);
    SDL_Surface *c;

    if (s == NULL) {
        printf("[ui] shot: read failed (%s)\n", SDL_GetError());
        return;
    }
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(s);
    if (c == NULL) {
        return;
    }
    /* rows into one block (pitch may be padded) */
    uint32_t *px = (uint32_t *)malloc((size_t)c->w * c->h * 4);
    if (px != NULL) {
        for (int y = 0; y < c->h; y++) {
            memcpy(px + (size_t)y * c->w, (const uint8_t *)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 4);
        }
        if (Host_WritePng(g_shot_path, px, c->w, c->h)) {
            printf("[shot] %s (%dx%d)\n", g_shot_path, c->w, c->h);
        } else {
            printf("[shot] cannot write %s\n", g_shot_path);
        }
        free(px);
    }
    SDL_DestroySurface(c);
    fflush(stdout);
}

bool is_hotkey(SDL_Scancode sc) {
    return sc == SDL_SCANCODE_F3 || sc == SDL_SCANCODE_F4 || sc == SDL_SCANCODE_F5 || sc == SDL_SCANCODE_F6 || sc == SDL_SCANCODE_F7 || sc == SDL_SCANCODE_F8 ||
           sc == SDL_SCANCODE_F9 || sc == SDL_SCANCODE_F11 || sc == SDL_SCANCODE_F12;
}

} // namespace

extern "C" {

void Ui_SetSettingsAtStart(int on) {
    g_settings_at_start = on != 0;
}

int Ui_Headless(void) {
#if DW2_DEV
    if (DevUi_Headless()) {
        return 1;
    }
#endif
    return g_headless && (g_settings_at_start || g_shots);
}

void Ui_SetShots(int on) {
    g_shots = on != 0;
}

void Ui_Notice(const char *text) {
    if (g_notice_lock == NULL) {
        return;
    }
    SDL_LockMutex(g_notice_lock);
    SDL_strlcpy(g_notice, text, sizeof(g_notice));
    g_notice_until = SDL_GetTicks() + NOTICE_MS;
    SDL_UnlockMutex(g_notice_lock);
}

int Ui_Drawing(void) {
    return Ui_Active() || (g_ready && notice_live());
}


/* dw2-pc-port: "Select Language / Sprache wählen". The choice is the same setting as F1 → Language
 * and is saved at once. The game texts change at a start: on the title (and at the first start)
 * the program restarts by itself, in the game it asks first (unsaved progress would be lost). */
void draw_language(void) {
    ImGuiIO &io = ImGui::GetIO();
    bool de = Settings_Get(SET_LANGUAGE) == SET_LANG_DE;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.y * 0.62f, 0), ImGuiCond_Always);
    ImGui::Begin("Language###lang", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.6f);
    if (g_lang_step == 0) {
        ImGui::TextUnformatted("Select Language");
        ImGui::TextUnformatted("Sprache w\xc3\xa4hlen");
        ImGui::Separator();
        int pick = -1;
        float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        if (ImGui::Button(de ? "English###en" : "> English <###en", ImVec2(w, 0))) pick = SET_LANG_EN;
        if (ImGui::Button(de ? "> Deutsch <###de" : "Deutsch###de", ImVec2(w, 0))) pick = SET_LANG_DE;
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("%s", de ? "Pfeiltasten / Steuerkreuz, Enter / Kreuz" : "Arrow keys / D-pad, Enter / Cross");
        if (pick >= 0) {
            int old = Settings_Get(SET_LANGUAGE);
            Settings_Set(SET_LANGUAGE, pick);
            if (pick == old) {
                g_lang_open = false;
            } else if (Host_TitleHint() || Host_LanguageAtStart) {
                Host_LanguageAtStart = 0;
                g_lang_open = false;
                Host_Restart(); /* nothing to lose on the title */
            } else {
                g_lang_step = 1;
            }
        }
    } else {
        bool nde = Settings_Get(SET_LANGUAGE) == SET_LANG_DE;
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::TextWrapped("%s", nde ? "Die Spieltexte wechseln nach einem Neustart. Ungespeicherter Fortschritt geht dabei verloren."
                                     : "The game texts change after a restart. Unsaved progress is lost then.");
        if (ImGui::Button(nde ? "Sp\xc3\xa4ter (beim n\xc3\xa4" "chsten Start)###later" : "Later (at the next start)###later", ImVec2(w, 0))) {
            g_lang_open = false;
            g_lang_step = 0;
        }
        if (ImGui::Button(nde ? "Jetzt neu starten###now" : "Restart now###now", ImVec2(w, 0))) {
            g_lang_open = false;
            g_lang_step = 0;
            Host_Restart();
        }
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
}

void Ui_OpenLanguage(void) {
    g_lang_open = true;
    g_lang_step = 0;
}

void Ui_Init(SDL_Window *window, SDL_Renderer *renderer, int headless) {
    float dpi;

    g_renderer = renderer;
    g_notice_lock = SDL_CreateMutex();
    g_headless = headless != 0;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL; /* no imgui.ini in the working folder */
    io.LogFilename = NULL;
    g_window = window;
    /* dw2-pc-port: the embedded scalable font, sharp at any UI scale (the bitmap default is
     * made for 13 px at 1x) */
    int font_size = 0;
    const unsigned char *font_data = Fonts_Data(FONT_INTERFACE, &font_size);
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void *)font_data, font_size, 16.0f, &font_config);
    apply_scale(window);
    dpi = ImGui::GetStyle().FontScaleDpi;
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) || !ImGui_ImplSDLRenderer3_Init(renderer)) {
        printf("[ui] ImGui backend init failed: no settings window\n");
        return;
    }
    g_ready = true;
#if DW2_DEV
    DevUi_Init(window, renderer, headless);
#endif
    if (g_settings_at_start) {
        SettingsUi_SetOpen(1);
    }
    if (Host_LanguageAtStart && (!headless || g_shots)) {
        Ui_OpenLanguage(); /* the first start: Select Language / Sprache wählen */
    }
    printf("[ui] Dear ImGui %s ready (F1 settings%s), display scale %.2f%s\n", IMGUI_VERSION,
           DW2_DEV ? ", F2 dev overlay" : "", dpi, g_settings_at_start ? ", settings open" : "");
    fflush(stdout);
}

int Ui_Active(void) {
    return g_ready && (SettingsUi_IsOpen() || dev_open() || g_quit_want || g_quit_shown || g_lang_open);
}

void Ui_SetQuitAtStart(int on) {
    g_quit_want = on != 0;
}

int Ui_BlocksGameInput(void) {
    if (g_ready && (SettingsUi_IsOpen() || g_quit_want || g_quit_shown || g_lang_open)) {
        return 1;
    }
#if DW2_DEV
    if (DevUi_WantsKeyboard()) {
        return 1; /* PD.1: the keyboard types into an ImGui field */
    }
#endif
    return 0;
}

int Ui_Event(const SDL_Event *e) {
    bool key = e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP || e->type == SDL_EVENT_TEXT_INPUT;

    if (!g_ready) {
        return 0;
    }
    if (e->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED || e->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
        SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);

        if (w != NULL) {
            apply_scale(w);
        }
    }
    if (SettingsUi_Capture(e)) {
        return 1;
    }
    if (Host_InputSelectKeyHeld(e)) {
        return 1; /* PR.38: a Select key held opened F1; its repeats must not press a button in it */
    }
    /* dw2-pc-port: a short Select press on the title opens the language picker (a held Select
       still opens F1, PR.38) */
    if (!SettingsUi_IsOpen() && !g_lang_open && Host_TitleHint()) {
        bool sel_down = (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->gbutton.button == Settings_Pad(0)) ||
                        (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat &&
                         (e->key.scancode == Settings_Key(0, 0) || e->key.scancode == Settings_Key(0, 1)) && e->key.scancode != 0);
        bool sel_up = (e->type == SDL_EVENT_GAMEPAD_BUTTON_UP && e->gbutton.button == Settings_Pad(0)) ||
                      (e->type == SDL_EVENT_KEY_UP && (e->key.scancode == Settings_Key(0, 0) || e->key.scancode == Settings_Key(0, 1)) &&
                       e->key.scancode != 0);
        if (sel_down) {
            g_select_down = SDL_GetTicks();
        } else if (sel_up && g_select_down && SDL_GetTicks() - g_select_down < 700) {
            g_select_down = 0;
            Ui_OpenLanguage();
            return 1;
        }
    }
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F1) {
        if (!e->key.repeat) {
            SettingsUi_SetOpen(!SettingsUi_IsOpen());
        }
        return 1;
    }
    /* PR.38: Start or Select on a gamepad closes the F1 window, at the release: the game never sees it */
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && SettingsUi_IsOpen() && g_close_button < 0 &&
            (e->gbutton.button == Settings_Pad(3 /* Start */) || e->gbutton.button == Settings_Pad(0 /* Select */))) {
            g_close_button = e->gbutton.button;
            return 1;
        }
        if (e->type == SDL_EVENT_GAMEPAD_BUTTON_UP && e->gbutton.button == g_close_button) {
            g_close_button = -1;
            if (SettingsUi_IsOpen()) {
                SettingsUi_SetOpen(0);
            }
            return 1;
        }
    }
#if DW2_DEV
    if (DevUi_Event(e)) {
        return 1; /* F2 */
    }
#endif
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE) {
        if (!e->key.repeat) {
            if (SettingsUi_IsOpen()) {
                SettingsUi_SetOpen(0); /* Esc closes the settings window instead of quitting */
            } else {
                g_quit_want = !g_quit_want; /* PR.15: Esc asks first, Esc again cancels */
            }
        }
        return 1;
    }
    if (!Ui_Active()) {
        return 0;
    }
    ImGui_ImplSDL3_ProcessEvent(e);
    if (key && e->type != SDL_EVENT_TEXT_INPUT && is_hotkey(e->key.scancode)) {
        return 0; /* F5 / F6 / F7 / F12 work with the windows open */
    }
    if (key && ImGui::GetIO().WantCaptureKeyboard) {
        return 1;
    }
    return 0;
}

void Ui_Render(void) {
    int lw, lh;
    SDL_RendererLogicalPresentation mode;

    bool hint = title_hint_shown();

    if (!Ui_Drawing() && !hint && !SDL_GetAtomicInt(&g_shot_pending)) {
        return;
    }
    ImGuiIO &io = ImGui::GetIO();
    /* the F1 window is driven by keyboard and gamepad too; the dev overlay alone is not */
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    if (SettingsUi_IsOpen() || g_quit_want || g_quit_shown || g_lang_open) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (SettingsUi_PadNavOk()) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        }
    }
    check_scale();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
#if DW2_DEV
    if (DevUi_IsOpen()) {
        DevUi_Draw();
    }
#endif
    if (SettingsUi_IsOpen()) {
        SettingsUi_Draw();
    }
    if (hint) {
        draw_title_hint();
    }
    if (g_quit_want || g_quit_shown) {
        draw_quit();
    }
    if (g_lang_open && !SettingsUi_IsOpen()) {
        draw_language();
    }
    draw_notice();
    ImGui::Render();
    /* window pixels for ImGui: the logical presentation off around it */
    SDL_GetRenderLogicalPresentation(g_renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(g_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
    if (SDL_GetAtomicInt(&g_shot_pending)) {
        write_shot();
        SDL_SetAtomicInt(&g_shot_pending, 0);
    }
    SDL_SetRenderLogicalPresentation(g_renderer, lw, lh, mode);
}

void Ui_Shot(const char *dir, const char *tag) {
    if (!Ui_Headless() || SDL_GetAtomicInt(&g_shot_pending)) {
        return;
    }
#if DW2_DEV
    snprintf(g_shot_path, sizeof(g_shot_path), "%s/%s_%s.png", dir, tag, DevUi_Headless() ? "devui" : "ui");
#else
    snprintf(g_shot_path, sizeof(g_shot_path), "%s/%s_ui.png", dir, tag);
#endif
    SDL_SetAtomicInt(&g_shot_pending, 1);
}

void Ui_Shutdown(void) {
    if (!g_ready) {
        return;
    }
#if DW2_DEV
    DevUi_Shutdown();
#endif
    g_ready = false;
    SDL_DestroyMutex(g_notice_lock);
    g_notice_lock = NULL;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

} // extern "C"
