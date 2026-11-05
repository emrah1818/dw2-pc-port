/* PR.2 F1 settings window (release and dev; host/ui.cpp runs the ImGui frame). Main thread.
 * Display settings go to the game thread as requests (Host_RequestSetting: it applies them at its
 * next VBlank wait and updates the settings, as F5 / F6 / F7 do), so the window always shows the
 * live values. Bindings, stick to D-pad and volume belong to the main thread and change here.
 * Every change marks the settings file for saving (host/settings.c). */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"

#include "host/devdata.h"
#include "host/devedit.h"
#include "host/settings.h"
#include "host/update.h"
#include "host/ui.h"

/* Community server (PC port and DW2 Online); also at the top of README.md. */
#define DISCORD_URL "https://discord.gg/nHXAq2fdCJ"

extern "C" void Host_RequestSetting(int id, int value);
extern "C" int Host_ResolveScale(int setting);
extern "C" void Host_AudioSetVolume(int volume);
extern "C" const char *Host_RendererName(void);
extern "C" int Host_WindowMode(void);
extern "C" int Host_WindowHeadless(void);
extern "C" void Host_SetWindowMode(int mode);
extern "C" void Host_WindowModeRefresh(void);
extern "C" int Assets_Export(const char *dir, void (*progress)(int done, int total, const char *what));
extern "C" int Cheat_SetActive(int on);
extern "C" int Cheat_Active(void);
extern "C" const char *Cheat_BackupDir(void);
extern "C" int Cheat_Snapshot(unsigned char *out, int *mode);
extern "C" void Cheat_Undo(void);
extern "C" int Cheat_UndoResult(void);
extern "C" int Cheat_UndoPending(void);
extern "C" const char *Host_CardDir(void);
extern "C" int Host_PakModCount(void);

namespace {

/* dw2-pc-port: German / English PC menus (setting "language"). The game's own texts stay as on
 * the disc. T(en) returns the German text when German is chosen and the table has it. */
struct Tr { const char *en, *de; };
const Tr k_tr[] = {
    { "Settings (F1)", "Einstellungen (F1)" },
    { "Start", "Start" }, { "Display", "Grafik" }, { "Controls", "Steuerung" }, { "Game", "Spiel" },
    { "Sound", "Audio" }, { "Modding", "Modding" }, { "Cheats", "Cheats" },
    { "Window", "Fenster" }, { "Window mode (F11)", "Fenstermodus (F11)" },
    { "Windowed", "Fenster" }, { "Borderless fullscreen", "Randloses Vollbild" },
    { "Exclusive fullscreen", "Exklusives Vollbild" }, { "Fullscreen display", "Bildschirm für Vollbild" },
    { "Current display", "Aktueller Bildschirm" }, { "Fullscreen resolution", "Vollbild-Auflösung" },
    { "Window size", "Fenstergröße" },
    { "Headless run: the window mode is not applied.", "Lauf ohne Fenster: Fenstermodus wird nicht angewendet." },
    { "F11 switches between windowed and fullscreen.", "F11 wechselt zwischen Fenster und Vollbild." },
    { "Profile", "Profil" }, { "Classic", "Classic" }, { "Remastered", "Remastered" }, { "(custom)", "(angepasst)" },
    { "Classic: the PS1 picture. Remastered: sharp 3D at your resolution, widescreen,",
      "Classic: das PS1-Bild. Remastered: scharfes 3D in deiner Auflösung, Breitbild," },
    { "no wobble, smooth frames, filtered textures. Gameplay is the same in both.",
      "kein Wackeln, flüssige Bilder, gefilterte Texturen. Das Gameplay ist in beiden gleich." },
    { "Picture", "Bild" }, { "Resolution (F5)", "Auflösung (F5)" }, { "Auto: fits the window", "Auto: passt sich dem Fenster an" },
    { "Auto draws 3D at the window's height: Full HD, WQHD and 4K stay sharp.",
      "Auto zeichnet 3D in Fensterhöhe: Full HD, WQHD und 4K bleiben scharf." },
    { "Aspect ratio (F7)", "Seitenverhältnis (F7)" }, { "4:3 (PS1)", "4:3 (PS1)" },
    { "16:9 widescreen", "16:9 Breitbild" }, { "21:9 ultrawide", "21:9 Ultrawide" },
    { "Widescreen shows more of the 3D world; menus and text stay 4:3.",
      "Breitbild zeigt mehr von der 3D-Welt, nichts wird gestreckt; Menüs und Text bleiben 4:3." },
    { "No wobble (F6)", "Kein Wackeln (F6)" },
    { "Removes the PS1 vertex jitter and texture warp (PGXP).", "Entfernt das PS1-Zittern der Ecken und Texturverzerrung (PGXP)." },
    { "Texture filtering (F4)", "Texturfilter (F4)" }, { "Off (original pixels)", "Aus (Originalpixel)" },
    { "Bilinear (3D only)", "Bilinear (nur 3D)" },
    { "Smooths 3D textures; text, menus and 2D art keep their pixels.",
      "Glättet 3D-Texturen; Text, Menüs und 2D-Grafik behalten ihre Pixel." },
    { "Smooth text and menus", "Text und Menüs glätten" },
    { "Upscales the game's text, menus and window frames with smooth edges.",
      "Skaliert Spieltext, Menüs und Fensterrahmen mit glatten Kanten hoch." },
    { "Sharpening (F8)", "Schärfen (F8)" }, { "off", "aus" },
    { "Sharpens the scaled picture: needs a resolution scale of 2x or more.",
      "Schärft das skalierte Bild: braucht eine Auflösungsstufe ab 2x." },
    { "Contrast adaptive sharpening of the scaled picture.", "Kontrastadaptives Schärfen des skalierten Bildes." },
    { "Smooth frames (F9)", "Flüssige Bilder (F9)" },
    { "Pictures at the monitor's refresh rate (60 / 120 / 144 Hz and more), blended",
      "Bilder mit der Bildrate des Monitors (60 / 120 / 144 Hz und mehr), überblendet" },
    { "between game frames (GPU renderer). The game logic runs at its original rate.",
      "zwischen Spielbildern (GPU-Renderer). Die Spiellogik läuft im Originaltempo." },
    { "VSync", "VSync" }, { "Waits for the monitor: no tearing, slightly more input delay.",
      "Wartet auf den Monitor: kein Tearing, etwas mehr Eingabeverzögerung." },
    { "FPS limit", "FPS-Begrenzung" }, { "no limit", "keine" },
    { "Caps the pictures per second shown; the game itself always runs at its own speed.",
      "Begrenzt die angezeigten Bilder pro Sekunde; das Spiel selbst läuft immer im eigenen Tempo." },
    { "Interface", "Oberfläche" }, { "Menu size", "Menügröße" }, { "auto", "auto" },
    { "Size of this window and the notices; auto follows the window size.",
      "Größe dieses Fensters und der Hinweise; auto folgt der Fenstergröße." },
    { "Show \"Press F1 for settings\" on the title", "Auf dem Titel \"F1 für Einstellungen\" zeigen" },
    { "Language", "Sprache" },
    { "Language of these PC menus and, after a restart, of the game texts that have a German version: attack names (verified German names) and Digimon types (Serum, Datei, Virus). Dialogues and items stay English.",
      "Sprache dieser PC-Menüs und, nach einem Neustart, der Spieltexte mit deutscher Fassung: Attackennamen (belegte deutsche Namen) und Digimon-Typen (Serum, Datei, Virus). Dialoge und Items bleiben englisch." },
    { "Renderer", "Renderer" }, { "GPU (Vulkan)", "GPU (Vulkan)" }, { "Software", "Software" },
    { "Takes effect at the next start. Running now: %s.", "Gilt ab dem nächsten Start. Läuft jetzt: %s." },
    { "Some values come from the command line for this run and are not saved.",
      "Einige Werte kommen aus der Befehlszeile und werden nicht gespeichert." },
    { "Left stick moves like the D-pad", "Linker Stick wie Steuerkreuz" }, { "Stick dead zone", "Stick-Totzone" },
    { "Hold Select (Backspace or the gamepad's Back / Select) to open this window",
      "Select halten (Rücktaste oder Back / Select am Controller) öffnet dieses Fenster" },
    { "Held for 1 second. A short press is Select for the game, sent when you let go.",
      "1 Sekunde halten. Kurz drücken ist Select fürs Spiel, beim Loslassen." },
    { "Start or Select on the gamepad closes this window.", "Start oder Select am Controller schließt dieses Fenster." },
    { "Click a slot, then press the key or gamepad button. Right click clears it.",
      "Feld anklicken, dann Taste oder Controller-Knopf drücken. Rechtsklick leert es." },
    { "Esc, Tab, F1 to F9, F11 and F12 are reserved.", "Esc, Tab, F1 bis F9, F11 und F12 sind reserviert." },
    { "PS1 button", "PS1-Taste" }, { "Key", "Taste" }, { "Key 2", "Taste 2" }, { "Gamepad", "Controller" },
    { "Reset controls", "Steuerung zurücksetzen" }, { "Connected controllers", "Verbundene Controller" },
    { "none (keyboard only)", "keiner (nur Tastatur)" },
    { "Speed-up", "Beschleunigung" }, { "Speed when unlocked", "Tempo wenn entsperrt" }, { "No limit", "Unbegrenzt" },
    { "F3 unlocks and locks the speed in play: battles, walking in the domains, menus. "
      "Music and voice lines play faster too (voices are silent with no limit). Movies always play at normal speed.",
      "F3 schaltet die Beschleunigung im Spiel ein und aus: Kämpfe, Laufen in den Domains, Menüs. "
      "Musik und Stimmen laufen mit (Stimmen sind bei unbegrenzt stumm). Videos laufen immer normal." },
    { "Master volume", "Gesamtlautstärke" }, { "Music", "Musik" }, { "Sound effects", "Soundeffekte" },
    { "Voices and movies", "Stimmen und Videos" }, { "Mute all sound", "Ton aus" },
    { "Music is the background track; jingles and effects count as sound effects.",
      "Musik ist die Hintergrundmusik; Jingles und Effekte zählen als Soundeffekte." },
    { "Voices: the battle and DigiLab voice lines and the movie sound (CD audio).",
      "Stimmen: die Kampf- und DigiLab-Sprachausgabe und der Videoton (CD-Audio)." },
    { "Close (F1 / Esc)", "Schließen (F1 / Esc)" },
    { "Settings file: %s", "Einstellungsdatei: %s" }, { "Not saved (no settings file in this run).",
      "Nicht gespeichert (keine Einstellungsdatei in diesem Lauf)." },
    { "Play", "Spielen" },
    { "New game and Continue are on the game's title screen: close this window (F1) and press Start.",
      "Neues Spiel und Fortsetzen sind im Titelbild des Spiels: Fenster schließen (F1) und Start drücken." },
    { "Continue playing", "Weiterspielen" }, { "Game data", "Spieldaten" },
    { "Data pack: %s", "Datenpaket: %s" },
    { "Built from your own disc image; every file checked against the SHA-256 list.",
      "Aus deinem eigenen Disc-Image gebaut; jede Datei gegen die SHA-256-Liste geprüft." },
    { "Open the game folder", "Programmordner öffnen" }, { "Open the save folder", "Spielstand-Ordner öffnen" },
    { "Import another image: delete dw2.pak (and dw2.pak.slus) and start again with the image next to dw2.exe.",
      "Anderes Image importieren: dw2.pak (und dw2.pak.slus) löschen und mit dem Image neben dw2.exe neu starten." },
    { "Diagnostics", "Diagnose" }, { "Information", "Informationen" },
    { "Mod files active: %d (Mods/files next to the program)", "Aktive Mod-Dateien: %d (Mods/files neben dem Programm)" },
    { "Export game files", "Spieldateien exportieren" },
    { "Writes GameData next to the program: every disc file, textures as PNG, a folder per Digimon with its "
      "model, textures and data, and the game tables as JSON. From your disc: do not share it.",
      "Schreibt GameData neben das Programm: jede Disc-Datei, Texturen als PNG, einen Ordner je Digimon mit "
      "Modell, Texturen und Daten, und die Spieltabellen als JSON. Aus deiner Disc: nicht weitergeben." },
    { "Export now", "Jetzt exportieren" }, { "Exporting...", "Exportiere..." }, { "Open GameData", "GameData öffnen" },
    { "Open Mods folder", "Mods-Ordner öffnen" },
    { "Replace a texture: edit its PNG (same size) and run  dw2.exe --import-texture <PNG>. "
      "Colours are mapped to the texture's own palette. Changes show after a restart.",
      "Textur ersetzen: PNG bearbeiten (gleiche Größe) und  dw2.exe --import-texture <PNG>  ausführen. "
      "Farben werden auf die Originalpalette abgebildet. Änderungen gelten nach einem Neustart." },
    { "Restore originals: delete the files in Mods/files (or start with --no-mods).",
      "Originale wiederherstellen: Dateien in Mods/files löschen (oder mit --no-mods starten)." },
    { "3D models (glTF) and animations: the DW2 3D Viewer (tools/viewer_bridge).",
      "3D-Modelle (glTF) und Animationen: der DW2 3D Viewer (tools/viewer_bridge)." },
    { "Cheats change your save data. They are off at every start.",
      "Cheats ändern deine Spieldaten. Sie sind bei jedem Start aus." },
    { "Turning them on copies your memory cards to a backup folder first and keeps the current state for undo.",
      "Beim Einschalten werden deine Memory Cards zuerst gesichert und der jetzige Stand für Rückgängig gemerkt." },
    { "Enable cheats", "Cheats einschalten" }, { "Backup: %s", "Sicherung: %s" },
    { "Undo all cheat changes", "Alle Cheat-Änderungen rückgängig" },
    { "Undo waits until the battle or card screen is over.", "Rückgängig wartet, bis Kampf oder Kartenbildschirm vorbei ist." },
    { "Restored.", "Wiederhergestellt." }, { "Tamer", "Tamer" }, { "Bits", "Bits" }, { "Set", "Setzen" },
    { "Party and server", "Team und Server" }, { "Digi-Beetle", "Digi-Beetle" }, { "Add item to the bag", "Item in die Tasche" },
    { "Level", "Level" }, { "EXP", "EXP" }, { "Max HP", "Max. HP" }, { "HP", "HP" }, { "Max MP", "Max. MP" },
    { "MP", "MP" }, { "Attack", "Angriff" }, { "Defense", "Verteidigung" }, { "Speed", "Tempo" },
    { "Heal all", "Alle heilen" }, { "Apply", "Anwenden" }, { "Last results", "Letzte Ergebnisse" },
    { "Waiting for the game...", "Warte auf das Spiel..." },
    { "Values are checked against the game's limits before they are written at the next frame.",
      "Werte werden gegen die Grenzen des Spiels geprüft und beim nächsten Bild geschrieben." },
    { "Add", "Hinzufügen" }, { "Open download page", "Download-Seite öffnen" }, { "Item", "Item" }, { "EP", "EP" }, { "Max EP", "Max. EP" },
};

void language_combo(void);
void start_tab(void);

/* dw2-pc-port: a greyed hint that wraps at the window edge (no cut-off text in any language) */
void ui_hint(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
void modding_tab(void);
void cheats_tab(void);

const char *T(const char *en) {
    if (Settings_Get(SET_LANGUAGE) != SET_LANG_DE) {
        return en;
    }
    for (const Tr &t : k_tr) {
        if (SDL_strcmp(t.en, en) == 0) {
            return t.de;
        }
    }
    return en;
}

/* translated label with a fixed ImGui id ("text###id") */
const char *TL(const char *en, const char *id) {
    static char buf[8][256];
    static int k;
    char *b = buf[k++ & 7];

    snprintf(b, sizeof(buf[0]), "%s###%s", T(en), id);
    return b;
}

bool g_open;
/* remap waiting for input: bit, slot 0 / 1 = keyboard key, 2 = gamepad */
int g_cap_bit = -1, g_cap_slot;
bool g_pad_quiet; /* no gamepad navigation until every pad button is up again */
char g_msg[96];
char g_start_tab[16]; /* --settings-tab: selected in the first frame (headless shots) */

const char *key_label(int sc) {
    const char *n = sc != 0 ? SDL_GetScancodeName((SDL_Scancode)sc) : "";
    return n[0] != 0 ? n : "-";
}

/* Gamepad slots by position, with the Xbox and PlayStation names. */
const char *pad_label(int code) {
    switch (code) {
    case SET_PAD_NONE: return "-";
    case SET_PAD_LTRIGGER: return "LT / L2";
    case SET_PAD_RTRIGGER: return "RT / R2";
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A / Cross";
    case SDL_GAMEPAD_BUTTON_EAST: return "B / Circle";
    case SDL_GAMEPAD_BUTTON_WEST: return "X / Square";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y / Triangle";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB / L1";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB / R1";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L stick / L3";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R stick / R3";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back / Select";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad Right";
    default: return Settings_PadName(code);
    }
}

bool any_pad_button(void) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    bool held = false;

    for (int i = 0; i < n && !held; i++) {
        SDL_Gamepad *g = SDL_GetGamepadFromID(ids[i]);

        for (int b = 0; g != NULL && b < SDL_GAMEPAD_BUTTON_COUNT && !held; b++) {
            held = SDL_GetGamepadButton(g, (SDL_GamepadButton)b);
        }
        if (g != NULL && !held) {
            held = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000 ||
                   SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000;
        }
    }
    SDL_free(ids);
    return held;
}

void end_capture(void) {
    g_cap_bit = -1;
    g_pad_quiet = true;
}

/* PR.3: the Window section. Changes apply at once (main thread) and are saved. */
void window_section(void) {
    int mode = Host_WindowMode();
    int disp = Settings_Get(SET_FS_DISPLAY);
    int n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    const char *modes[] = { T("Windowed"), T("Borderless fullscreen"), T("Exclusive fullscreen") };
    char label[128];

    ImGui::SeparatorText(T("Window"));
    if (ImGui::Combo(TL("Window mode (F11)", "wmode"), &mode, modes, 3)) {
        Settings_Set(SET_WINDOW_MODE, mode);
        Host_SetWindowMode(mode);
    }
    /* display: 0 = the one the window is on */
    if (disp > n) {
        snprintf(label, sizeof(label), "Display %d (not connected)", disp);
    } else if (disp > 0) {
        snprintf(label, sizeof(label), "%d: %s", disp, SDL_GetDisplayName(ids[disp - 1]));
    } else {
        snprintf(label, sizeof(label), "%s", T("Current display"));
    }
    if (ImGui::BeginCombo(TL("Fullscreen display", "fsdisp"), label)) {
        for (int i = 0; i <= n; i++) {
            char item[128];

            if (i == 0) {
                snprintf(item, sizeof(item), "%s", T("Current display"));
            } else {
                snprintf(item, sizeof(item), "%d: %s", i, SDL_GetDisplayName(ids[i - 1]));
            }
            ImGui::PushID(i);
            if (ImGui::Selectable(item, i == disp)) {
                Settings_Set(SET_FS_DISPLAY, i);
                Host_WindowModeRefresh();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    /* exclusive fullscreen resolution: the desktop mode or one of the display's modes */
    SDL_DisplayID id = disp > 0 && disp <= n ? ids[disp - 1] : 0;
    SDL_Window *win = SDL_GetKeyboardFocus();
    if (id == 0) {
        id = win != NULL ? SDL_GetDisplayForWindow(win) : SDL_GetPrimaryDisplay();
    }
    int fw, fh, fhz;
    Settings_FsMode(&fw, &fh, &fhz);
    const SDL_DisplayMode *desk = id != 0 ? SDL_GetDesktopDisplayMode(id) : NULL;
    char desk_label[64];
    if (desk != NULL) {
        snprintf(desk_label, sizeof(desk_label), "Desktop (%dx%d @ %.0f Hz)", desk->w, desk->h, desk->refresh_rate);
    } else {
        snprintf(desk_label, sizeof(desk_label), "Desktop");
    }
    if (fw == 0) {
        SDL_strlcpy(label, desk_label, sizeof(label));
    } else if (fhz != 0) {
        snprintf(label, sizeof(label), "%dx%d @ %.2f Hz", fw, fh, fhz / 100.0);
    } else {
        snprintf(label, sizeof(label), "%dx%d", fw, fh);
    }
    ImGui::BeginDisabled(mode != SET_WINDOW_EXCLUSIVE);
    if (ImGui::BeginCombo(TL("Fullscreen resolution", "fsres"), label)) {
        if (ImGui::Selectable(desk_label, fw == 0)) {
            Settings_SetFsMode(0, 0, 0);
            Host_WindowModeRefresh();
        }
        int count = 0;
        SDL_DisplayMode **list = id != 0 ? SDL_GetFullscreenDisplayModes(id, &count) : NULL;
        for (int i = 0; i < count; i++) {
            const SDL_DisplayMode *m = list[i];
            int hz = (int)(m->refresh_rate * 100 + 0.5f);
            bool dup = m->pixel_density != 1.0f;

            for (int j = 0; j < i && !dup; j++) {
                dup = list[j]->pixel_density == 1.0f && list[j]->w == m->w && list[j]->h == m->h &&
                      (int)(list[j]->refresh_rate * 100 + 0.5f) == hz;
            }
            if (dup) {
                continue;
            }
            char item[64];
            snprintf(item, sizeof(item), "%dx%d @ %.2f Hz", m->w, m->h, m->refresh_rate);
            ImGui::PushID(i);
            if (ImGui::Selectable(item, fw == m->w && fh == m->h && fhz == hz)) {
                Settings_SetFsMode(m->w, m->h, hz);
                Host_WindowModeRefresh();
            }
            ImGui::PopID();
        }
        SDL_free(list);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    {
        /* dw2-pc-port: window size presets (windowed mode); the picture keeps its aspect ratio */
        static const int sizes[][2] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
                                        { 2560, 1080 }, { 3440, 1440 } };
        static const char *const names[] = { "1280 x 720 (HD)", "1600 x 900", "1920 x 1080 (Full HD)",
                                              "2560 x 1440 (WQHD)", "3840 x 2160 (4K)", "2560 x 1080 (21:9)",
                                              "3440 x 1440 (21:9)" };
        int ww = 0, wh = 0;
        char cur[48];

        Settings_WindowSize(&ww, &wh);
        if (ww > 0) {
            snprintf(cur, sizeof(cur), "%d x %d", ww, wh);
        } else {
            snprintf(cur, sizeof(cur), "%s", T("auto"));
        }
        ImGui::BeginDisabled(mode != SET_WINDOW_WINDOWED);
        if (ImGui::BeginCombo(TL("Window size", "wsize"), cur)) {
            for (int i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++) {
                if (ImGui::Selectable(names[i], ww == sizes[i][0] && wh == sizes[i][1])) {
                    SDL_Window *win = SDL_GetKeyboardFocus();

                    Settings_SetWindowSize(sizes[i][0], sizes[i][1]);
                    if (win != NULL) {
                        SDL_SetWindowSize(win, sizes[i][0], sizes[i][1]);
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
    }
    SDL_free(ids);
    if (Host_WindowHeadless()) {
        ui_hint(T("Headless run: the window mode is not applied."));
    } else {
        ui_hint(T("F11 switches between windowed and fullscreen."));
    }
}

ImGuiTabItemFlags tab_flags(const char *name) {
    return SDL_strcasecmp(g_start_tab, name) == 0 ? ImGuiTabItemFlags_SetSelected : 0;
}

void apply_profile(int p) {
    /* dw2-pc-port: Classic / Remastered (host/settings.c Settings_ProfileValue) */
    static const int ids[] = { SET_SCALE, SET_WIDE, SET_PGXP, SET_SHARPEN, SET_SMOOTH, SET_TEX_FILTER, SET_SMOOTH2D };

    for (int id : ids) {
        Host_RequestSetting(id, Settings_ProfileValue(p, id));
    }
}

int profile_now(void) {
    static const int ids[] = { SET_SCALE, SET_WIDE, SET_PGXP, SET_SHARPEN, SET_SMOOTH, SET_TEX_FILTER, SET_SMOOTH2D };

    for (int p = SET_PROFILE_CLASSIC; p <= SET_PROFILE_REMASTERED; p++) {
        bool all = true;

        for (int id : ids) {
            all = all && Settings_Get(id) == Settings_ProfileValue(p, id);
        }
        if (all) {
            return p;
        }
    }
    return -1;
}

void display_tab(void) {
    int scale = Settings_Get(SET_SCALE);
    int wide = Settings_Get(SET_WIDE);
    bool pgxp = Settings_Get(SET_PGXP) != 0;
    int renderer = Settings_Get(SET_RENDERER);
    int sharpen = Settings_Get(SET_SHARPEN);
    bool smooth = Settings_Get(SET_SMOOTH) != 0;
    bool hint = Settings_Get(SET_TITLE_HINT) != 0;
    int filter = Settings_Get(SET_TEX_FILTER);
    bool vsync = Settings_Get(SET_VSYNC) != 0;
    int ui = Settings_Get(SET_UI_SCALE);
    int prof = profile_now();

    ImGui::SeparatorText(T("Profile"));
    if (ImGui::RadioButton("Classic", prof == SET_PROFILE_CLASSIC)) {
        apply_profile(SET_PROFILE_CLASSIC);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Remastered", prof == SET_PROFILE_REMASTERED)) {
        apply_profile(SET_PROFILE_REMASTERED);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", prof < 0 ? T("(custom)") : "");
    ui_hint(T("Classic: the PS1 picture. Remastered: sharp 3D at your resolution, widescreen,"));
    ui_hint(T("no wobble, smooth frames, filtered textures. Gameplay is the same in both."));

    ImGui::SeparatorText(T("Picture"));
    {
        /* dw2-pc-port: Auto, 1x .. 12x with the lines they draw */
        static const int steps[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 12 };
        char label[64];

        if (scale == 0) {
            snprintf(label, sizeof(label), Settings_Get(SET_LANGUAGE) == SET_LANG_DE ? "Auto (jetzt %dx, %d Zeilen)" : "Auto (now %dx, %d lines)", Host_ResolveScale(0), 240 * Host_ResolveScale(0));
        } else {
            snprintf(label, sizeof(label), Settings_Get(SET_LANGUAGE) == SET_LANG_DE ? "%dx (%d Zeilen)" : "%dx (%d lines)", scale, 240 * scale);
        }
        if (ImGui::BeginCombo(TL("Resolution (F5)", "res"), label)) {
            for (int v : steps) {
                char item[64];

                if (v == 0) {
                    snprintf(item, sizeof(item), "%s", T("Auto: fits the window"));
                } else {
                    snprintf(item, sizeof(item), Settings_Get(SET_LANGUAGE) == SET_LANG_DE ? "%dx: %d Zeilen%s" : "%dx: %d lines%s", v, 240 * v,
                             v == 1 ? " (PS1)" : v == 3 ? " (720p)" : v == 5 ? " (1080p+)" : v == 6 ? " (1440p WQHD)"
                             : v == 9 ? " (2160p 4K)" : v == 12 ? " (2880p 5K)" : "");
                }
                if (ImGui::Selectable(item, v == scale)) {
                    Host_RequestSetting(SET_SCALE, v);
                }
            }
            ImGui::EndCombo();
        }
    }
    ui_hint(T("Auto draws 3D at the window's height: Full HD, WQHD and 4K stay sharp."));
    {
        const char *aspects[] = { T("4:3 (PS1)"), T("16:9 widescreen"), T("21:9 ultrawide") };

        if (ImGui::Combo(TL("Aspect ratio (F7)", "aspect"), &wide, aspects, 3)) {
            Host_RequestSetting(SET_WIDE, wide);
        }
    }
    ui_hint(T("Widescreen shows more of the 3D world; menus and text stay 4:3."));
    if (ImGui::Checkbox(TL("No wobble (F6)", "pgxp"), &pgxp)) {
        Host_RequestSetting(SET_PGXP, pgxp);
    }
    ui_hint(T("Removes the PS1 vertex jitter and texture warp (PGXP)."));
    {
        const char *filters[] = { T("Off (original pixels)"), T("Bilinear (3D only)") };

        if (ImGui::Combo(TL("Texture filtering (F4)", "texf"), &filter, filters, 2)) {
            Host_RequestSetting(SET_TEX_FILTER, filter);
        }
    }
    ui_hint(T("Smooths 3D textures; text, menus and 2D art keep their pixels."));
    {
        bool s2d = Settings_Get(SET_SMOOTH2D) != 0;

        if (ImGui::Checkbox(TL("Smooth text and menus", "s2d"), &s2d)) {
            Host_RequestSetting(SET_SMOOTH2D, s2d);
        }
    }
    ui_hint(T("Upscales the game's text, menus and window frames with smooth edges."));
    if (ImGui::SliderInt(TL("Sharpening (F8)", "sharp"), &sharpen, 0, 100, sharpen == 0 ? T("off") : "%d")) {
        Host_RequestSetting(SET_SHARPEN, sharpen);
    }
    if (scale == 1) {
        ui_hint(T("Sharpens the scaled picture: needs a resolution scale of 2x or more."));
    } else {
        ui_hint(T("Contrast adaptive sharpening of the scaled picture."));
    }
    if (ImGui::Checkbox(TL("Smooth frames (F9)", "smooth"), &smooth)) {
        Host_RequestSetting(SET_SMOOTH, smooth);
    }
    ui_hint(T("Pictures at the monitor's refresh rate (60 / 120 / 144 Hz and more), blended"));
    ui_hint(T("between game frames (GPU renderer). The game logic runs at its original rate."));
    if (ImGui::Checkbox(TL("VSync", "vsync"), &vsync)) {
        Settings_Set(SET_VSYNC, vsync);
    }
    ui_hint(T("Waits for the monitor: no tearing, slightly more input delay."));
    {
        /* dw2-pc-port: FPS limit (window thread) */
        static const int lims[] = { 0, 30, 60, 75, 120, 144, 165, 240, 360 };
        int cur = Settings_Get(SET_FPS_LIMIT);
        char label[32];

        if (cur == 0) {
            snprintf(label, sizeof(label), "%s", T("no limit"));
        } else {
            snprintf(label, sizeof(label), "%d FPS", cur);
        }
        if (ImGui::BeginCombo(TL("FPS limit", "fps"), label)) {
            for (int v : lims) {
                char item[32];

                if (v == 0) {
                    snprintf(item, sizeof(item), "%s", T("no limit"));
                } else {
                    snprintf(item, sizeof(item), "%d FPS", v);
                }
                if (ImGui::Selectable(item, v == cur)) {
                    Settings_Set(SET_FPS_LIMIT, v);
                }
            }
            ImGui::EndCombo();
        }
        ui_hint(T("Caps the pictures per second shown; the game itself always runs at its own speed."));
    }

    ImGui::SeparatorText(T("Interface"));
    {
        int pct = ui == 0 ? 0 : ui;

        if (ImGui::SliderInt(TL("Menu size", "uisize"), &pct, 0, 300, pct == 0 ? T("auto") : "%d%%")) {
            pct = pct != 0 && pct < 50 ? 50 : pct;
            Settings_Set(SET_UI_SCALE, pct);
        }
    }
    ui_hint(T("Size of this window and the notices; auto follows the window size."));
    language_combo();
    if (ImGui::Checkbox(TL("Show \"Press F1 for settings\" on the title", "hint"), &hint)) {
        Settings_Set(SET_TITLE_HINT, hint);
    }

    ImGui::SeparatorText(T("Renderer"));
    const char *names[] = { T("GPU (Vulkan)"), T("Software") };
    if (ImGui::Combo(TL("Renderer", "renderer"), &renderer, names, 2)) {
        Settings_Set(SET_RENDERER, renderer);
    }
    ImGui::TextDisabled(T("Takes effect at the next start. Running now: %s."), Host_RendererName());
    window_section();
    for (int id = SET_SCALE; id < SET_COUNT; id++) {
        if (Settings_Overridden(id)) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                               "%s", T("Some values come from the command line for this run and are not saved."));
            break;
        }
    }
}

void bind_button(int bit, int slot, const char *label) {
    char id[64];
    bool waiting = g_cap_bit == bit && g_cap_slot == slot;

    snprintf(id, sizeof(id), "%s##b%d_%d", waiting ? "press..." : label, bit, slot);
    if (ImGui::Button(id, ImVec2(-1, 0))) {
        g_cap_bit = bit;
        g_cap_slot = slot;
        g_msg[0] = 0;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (slot < 2) {
            Settings_SetKey(bit, slot, 0);
        } else {
            Settings_SetPad(bit, SET_PAD_NONE);
        }
    }
}

void controls_tab(void) {
    bool stick = Settings_Get(SET_STICK_DPAD) != 0;
    bool hold = Settings_Get(SET_SELECT_HOLD) != 0;

    if (ImGui::Checkbox(TL("Left stick moves like the D-pad", "stick"), &stick)) {
        Settings_Set(SET_STICK_DPAD, stick);
    }
    {
        int dz = Settings_Get(SET_DEADZONE);

        if (ImGui::SliderInt(TL("Stick dead zone", "dz"), &dz, 10, 90, "%d%%")) {
            Settings_Set(SET_DEADZONE, dz);
        }
    }
    if (ImGui::Checkbox(TL("Hold Select (Backspace or the gamepad's Back / Select) to open this window", "hold"), &hold)) {
        Settings_Set(SET_SELECT_HOLD, hold);
    }
    ui_hint(T("Held for 1 second. A short press is Select for the game, sent when you let go."));
    ui_hint(T("Start or Select on the gamepad closes this window."));
    {
        /* dw2-pc-port: which controllers SDL sees right now */
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);

        ImGui::Text("%s:", T("Connected controllers"));
        if (n == 0) {
            ImGui::SameLine();
            ui_hint(T("none (keyboard only)"));
        }
        for (int i = 0; i < n; i++) {
            ImGui::BulletText("%s", SDL_GetGamepadNameForID(ids[i]) ? SDL_GetGamepadNameForID(ids[i]) : "?");
        }
        SDL_free(ids);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", T("Click a slot, then press the key or gamepad button. Right click clears it."));
    ImGui::PopStyleColor();
    if (g_msg[0] != 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s", g_msg);
    } else if (g_cap_bit >= 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "%s: press a %s (Esc cancels)",
                           Settings_ButtonName(g_cap_bit), g_cap_slot < 2 ? "key" : "gamepad button");
    } else {
        ui_hint(T("Esc, Tab, F1 to F9, F11 and F12 are reserved."));
    }
    if (ImGui::BeginTable("binds", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(T("PS1 button"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(T("Key"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(T("Key 2"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(T("Gamepad"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 16; i++) {
            int bit = Settings_ButtonOrder[i];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Settings_ButtonName(bit));
            for (int slot = 0; slot < 2; slot++) {
                ImGui::TableNextColumn();
                bind_button(bit, slot, key_label(Settings_Key(bit, slot)));
            }
            ImGui::TableNextColumn();
            bind_button(bit, 2, pad_label(Settings_Pad(bit)));
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(TL("Reset controls", "resetc"))) {
        Settings_ResetControls();
        g_cap_bit = -1;
    }
}

void open_releases(void);

/* PR.28: speed-up multiplier; F3 switches the speed-up on and off in play */
void game_tab(void) {
    static const int values[] = { 2, 3, 4, 5, 6, 8, 0 };
    const char *const names[] = { "2x", "3x", "4x", "5x", "6x", "8x", T("No limit") };
    int cur = Settings_Get(SET_SPEED);
    int sel = 1;

    for (int i = 0; i < (int)(sizeof(values) / sizeof(values[0])); i++) {
        if (values[i] == cur) {
            sel = i;
        }
    }
    ImGui::SeparatorText(T("Speed-up"));
    if (ImGui::Combo(TL("Speed when unlocked", "speed"), &sel, names, (int)(sizeof(names) / sizeof(names[0])))) {
        Settings_Set(SET_SPEED, values[sel]);
    }
    ImGui::TextWrapped("%s", T("F3 unlocks and locks the speed in play: battles, walking in the domains, menus. "
                               "Music and voice lines play faster too (voices are silent with no limit). "
                               "Movies always play at normal speed."));

    /* PR.30: the update check is off in dw2-pc-port builds (host/update.c) */
    ImGui::SeparatorText(T("Information"));
    ImGui::TextDisabled("DW2-PC-Port v%s (fork of Wyrelade/DW2-PC, CC0)", Update_LocalVersion());
    if (ImGui::Button(TL("Open download page", "odl"))) {
        open_releases();
    }
}

void open_releases(void) {
    if (!SDL_OpenURL(DW2_RELEASES_URL)) {
        fprintf(stderr, "[ui] could not open %s: %s\n", DW2_RELEASES_URL, SDL_GetError());
    }
}

/* PR.30: a newer release found by the start check, above the tabs */
void update_banner(void) {
    const char *tag = Update_NewerTag();

    if (tag == NULL) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.84f, 0.35f, 1.0f));
    ImGui::Text("DW2-PC %s is available (this is v%s).", tag, Update_LocalVersion());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("Open download page")) {
        open_releases();
    }
    ImGui::Separator();
}

void sound_tab(void) {
    int v = Settings_Get(SET_VOLUME);
    int m = Settings_Get(SET_MUSIC_VOL), fx = Settings_Get(SET_SFX_VOL), vo = Settings_Get(SET_VOICE_VOL);

    if (ImGui::SliderInt(TL("Master volume", "vol"), &v, 0, 100, "%d%%")) {
        Settings_Set(SET_VOLUME, v);
        Host_AudioSetVolume(v);
    }
    {
        bool mute = Settings_Get(SET_MUTE) != 0;

        if (ImGui::Checkbox(TL("Mute all sound", "mute"), &mute)) {
            Settings_Set(SET_MUTE, mute);
            Host_AudioSetVolume(v);
        }
    }
    ImGui::Spacing();
    if (ImGui::SliderInt(TL("Music", "music"), &m, 0, 100, "%d%%")) {
        Settings_Set(SET_MUSIC_VOL, m);
    }
    if (ImGui::SliderInt(TL("Sound effects", "sfx"), &fx, 0, 100, "%d%%")) {
        Settings_Set(SET_SFX_VOL, fx);
    }
    if (ImGui::SliderInt(TL("Voices and movies", "voice"), &vo, 0, 100, "%d%%")) {
        Settings_Set(SET_VOICE_VOL, vo);
    }
    ui_hint(T("Music is the background track; jingles and effects count as sound effects."));
    ui_hint(T("Voices: the battle and DigiLab voice lines and the movie sound (CD audio)."));
}


/* ---- dw2-pc-port: language, start hub, modding, cheats ---- */

void language_combo(void) {
    const char *langs[] = { "English", "Deutsch" };
    int lang = Settings_Get(SET_LANGUAGE);

    if (ImGui::Combo(TL("Language", "lang"), &lang, langs, 2)) {
        Settings_Set(SET_LANGUAGE, lang);
    }
    ui_hint(T("Language of these PC menus and, after a restart, of the game texts that have a German version: attack names (verified German names) and Digimon types (Serum, Datei, Virus). Dialogues and items stay English."));
}

void open_folder(const char *path) {
    char url[1200];

    snprintf(url, sizeof(url), "file:///%s", path);
    if (!SDL_OpenURL(url)) {
        fprintf(stderr, "[ui] could not open %s: %s\n", url, SDL_GetError());
    }
}

void start_tab(void) {
    const char *base = SDL_GetBasePath();

    ImGui::SeparatorText(T("Play"));
    ImGui::TextWrapped("%s", T("New game and Continue are on the game's title screen: close this window (F1) and press Start."));
    if (ImGui::Button(TL("Continue playing", "play"))) {
        SettingsUi_SetOpen(0);
    }
    ImGui::SeparatorText(T("Language"));
    language_combo();
    ImGui::SeparatorText(T("Game data"));
    ImGui::TextWrapped("%s", T("Built from your own disc image; every file checked against the SHA-256 list."));
    ImGui::TextDisabled(T("Mod files active: %d (Mods/files next to the program)"), Host_PakModCount());
    if (ImGui::Button(TL("Open the game folder", "ogame")) && base != NULL) {
        open_folder(base);
    }
    ImGui::SameLine();
    if (ImGui::Button(TL("Open the save folder", "osave"))) {
        open_folder(Host_CardDir());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", T("Import another image: delete dw2.pak (and dw2.pak.slus) and start again with the image next to dw2.exe."));
    ImGui::PopStyleColor();
    ImGui::SeparatorText(T("Diagnostics"));
    ImGui::Text("DW2-PC-Port v%s, SDL %d.%d.%d", Update_LocalVersion(), SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
                SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
    ImGui::Text("%s: %s", T("Renderer"), Host_RendererName());
    {
        SDL_Window *win = SDL_GetKeyboardFocus();
        int w = 0, h = 0;

        if (win != NULL) {
            SDL_GetWindowSizeInPixels(win, &w, &h);
        }
        ImGui::Text("%s: %dx%d, %s %dx (%d)", T("Window"), w, h, T("Resolution (F5)"), Host_ResolveScale(Settings_Get(SET_SCALE)),
                    240 * Host_ResolveScale(Settings_Get(SET_SCALE)));
    }
    ImGui::Text("Audio: %s", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "-");
}

SDL_AtomicInt g_export_state; /* 0 idle, 1 running, 2 done, 3 failed */
SDL_AtomicInt g_export_done, g_export_total;
char g_export_dir[1024];

void export_progress(int done, int total, const char *what) {
    (void)what;
    SDL_SetAtomicInt(&g_export_done, done);
    SDL_SetAtomicInt(&g_export_total, total);
}

int SDLCALL export_thread(void *arg) {
    (void)arg;
    SDL_SetAtomicInt(&g_export_state, Assets_Export(g_export_dir, export_progress) == 0 ? 2 : 3);
    return 0;
}

void modding_tab(void) {
    const char *base = SDL_GetBasePath();
    int st = SDL_GetAtomicInt(&g_export_state);
    char mods[1100];

    ImGui::SeparatorText(T("Export game files"));
    ImGui::TextWrapped("%s", T("Writes GameData next to the program: every disc file, textures as PNG, a folder per Digimon with its "
                               "model, textures and data, and the game tables as JSON. From your disc: do not share it."));
    ImGui::BeginDisabled(st == 1);
    if (ImGui::Button(st == 1 ? T("Exporting...") : TL("Export now", "export"))) {
        snprintf(g_export_dir, sizeof(g_export_dir), "%sGameData", base != NULL ? base : "");
        SDL_SetAtomicInt(&g_export_state, 1);
        SDL_Thread *t = SDL_CreateThread(export_thread, "export", NULL);
        if (t != NULL) {
            SDL_DetachThread(t);
        } else {
            SDL_SetAtomicInt(&g_export_state, 3);
        }
    }
    ImGui::EndDisabled();
    if (st == 1 && SDL_GetAtomicInt(&g_export_total) > 0) {
        ImGui::SameLine();
        ImGui::ProgressBar((float)SDL_GetAtomicInt(&g_export_done) / (float)SDL_GetAtomicInt(&g_export_total), ImVec2(-1, 0));
    } else if (st == 2) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "OK: %s", g_export_dir);
    } else if (st == 3) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "export failed (log)");
    }
    if (ImGui::Button(TL("Open GameData", "ogd")) && base != NULL) {
        snprintf(mods, sizeof(mods), "%sGameData", base);
        SDL_CreateDirectory(mods);
        open_folder(mods);
    }
    ImGui::SameLine();
    if (ImGui::Button(TL("Open Mods folder", "omods")) && base != NULL) {
        snprintf(mods, sizeof(mods), "%sMods/files", base);
        SDL_CreateDirectory(mods);
        open_folder(mods);
    }
    ImGui::Text(T("Mod files active: %d (Mods/files next to the program)"), Host_PakModCount());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", T("Replace a texture: edit its PNG (same size) and run  dw2.exe --import-texture <PNG>. "
                               "Colours are mapped to the texture's own palette. Changes show after a restart."));
    ImGui::TextWrapped("%s", T("Restore originals: delete the files in Mods/files (or start with --no-mods)."));
    ImGui::TextWrapped("%s", T("3D models (glTF) and animations: the DW2 3D Viewer (tools/viewer_bridge)."));
    ImGui::PopStyleColor();
}

int rd16s(const unsigned char *p) {
    return (short)(p[0] | p[1] << 8);
}

int rd32s(const unsigned char *p) {
    return (int)((unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24);
}

void push_edit(int kind, int a, int b, int c) {
    DevEdit e;

    memset(&e, 0, sizeof(e));
    e.kind = kind;
    e.a = a;
    e.b = b;
    e.c = c;
    DevEdit_Push(&e);
}

int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void cheats_tab(void) {
    static unsigned char gs[0x1058];
    static int edit_bits = -1, item_sel = 0;
    static int vals[0x24][9];
    static bool have_vals[0x24];
    bool on = Cheat_Active() != 0;
    int mode = 0;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.35f, 1.0f));
    ImGui::TextWrapped("%s", T("Cheats change your save data. They are off at every start."));
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", T("Turning them on copies your memory cards to a backup folder first and keeps the current state for undo."));
    if (ImGui::Checkbox(TL("Enable cheats", "cheaton"), &on)) {
        Cheat_SetActive(on);
    }
    if (Cheat_BackupDir()[0] != 0) {
        ImGui::TextDisabled(T("Backup: %s"), Cheat_BackupDir());
    }
    if (!Cheat_Active()) {
        return;
    }
    if (!Cheat_Snapshot(gs, &mode)) {
        ui_hint(T("Waiting for the game..."));
        return;
    }
    if (DevData_Ready() == 0) {
        DevData_Load();
    }
    if (ImGui::Button(TL("Undo all cheat changes", "undo"))) {
        Cheat_Undo();
    }
    ImGui::SameLine();
    if (Cheat_UndoPending()) {
        ui_hint(T("Undo waits until the battle or card screen is over."));
    } else if (Cheat_UndoResult() == 1) {
        ui_hint(T("Restored."));
    }
    ui_hint(T("Values are checked against the game's limits before they are written at the next frame."));

    ImGui::SeparatorText(T("Tamer"));
    {
        /* the field follows the game's value whenever that changes (load, shop, an applied edit) */
        static int seen_bits = -1;
        if (rd32s(gs + 0x08) != seen_bits) {
            seen_bits = rd32s(gs + 0x08);
            edit_bits = seen_bits;
        }
    }
    ImGui::Text("%s: %d", T("Bits"), rd32s(gs + 0x08));
    ImGui::SetNextItemWidth(160);
    ImGui::InputInt("##bits", &edit_bits, 100, 1000);
    edit_bits = clampi(edit_bits, 0, 9999999);
    ImGui::SameLine();
    if (ImGui::Button(TL("Set", "setbits"))) {
        push_edit(DEVEDIT_BITS, edit_bits, 0, 0);
    }

    ImGui::SeparatorText(T("Digi-Beetle"));
    ImGui::Text("%s %d / %d   %s %d / %d", T("HP"), rd16s(gs + 0x24), rd16s(gs + 0x26), T("EP"), rd16s(gs + 0x28), rd16s(gs + 0x2A));
    if (ImGui::Button(TL("Heal all", "healbeetle"))) {
        push_edit(DEVEDIT_BEETLE, 0, rd16s(gs + 0x26), 0);
        push_edit(DEVEDIT_BEETLE, 2, rd16s(gs + 0x2A), 0);
    }

    ImGui::SeparatorText(T("Party and server"));
    static const char *const cols[9] = { "Level", "EXP", "HP", "Max HP", "MP", "Max MP", "Attack", "Defense", "Speed" };
    static const int fields[9] = { DEVDIGI_LEVEL, DEVDIGI_EXP, DEVDIGI_HP, DEVDIGI_MAXHP, DEVDIGI_MP, DEVDIGI_MAXMP,
                                   DEVDIGI_ATTACK, DEVDIGI_DEFENSE, DEVDIGI_SPEED };
    static const int offs[9] = { 0x0D, 0x10, 0x16, 0x14, 0x1A, 0x18, 0x1C, 0x1E, 0x20 };
    static const int lo[9] = { 1, 0, 0, 1, 0, 1, 1, 1, 1 }, hi[9] = { 99, 9999999, 9999, 9999, 9999, 9999, 999, 999, 999 };
    if (ImGui::Button(TL("Heal all", "healall"))) {
        for (int i = 0; i < 0x24; i++) {
            const unsigned char *d = gs + 0xE4 + i * 0x5C;
            if (d[0] != 0) {
                push_edit(DEVEDIT_DIGI, i, DEVDIGI_HP, rd16s(d + 0x14));
                push_edit(DEVEDIT_DIGI, i, DEVDIGI_MP, rd16s(d + 0x18));
            }
        }
    }
    if (ImGui::BeginTable("digis", 11, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Digimon");
        for (int k = 0; k < 9; k++) {
            ImGui::TableSetupColumn(T(cols[k]));
        }
        ImGui::TableHeadersRow();
        for (int i = 0; i < 0x24; i++) {
            const unsigned char *d = gs + 0xE4 + i * 0x5C;
            int cur[9];

            if (d[0] == 0) {
                have_vals[i] = false;
                continue;
            }
            for (int k = 0; k < 9; k++) {
                cur[k] = k == 0 ? d[0x0D] : k == 1 ? rd32s(d + 0x10) : rd16s(d + offs[k]);
            }
            static int seen[0x24][9];
            if (!have_vals[i] || memcmp(seen[i], cur, sizeof(cur)) != 0) {
                memcpy(vals[i], cur, sizeof(cur)); /* follow the game's values when they change */
                memcpy(seen[i], cur, sizeof(cur));
                have_vals[i] = true;
            }
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", i);
            ImGui::TableNextColumn();
            ImGui::Text("%s L%d", DevData_DigiName(d[1]), d[0x0D]);
            if (ImGui::SmallButton(T("Apply"))) {
                for (int k = 0; k < 9; k++) {
                    vals[i][k] = clampi(vals[i][k], lo[k], hi[k]);
                    if (vals[i][k] != cur[k]) {
                        push_edit(DEVEDIT_DIGI, i, fields[k], vals[i][k]);
                    }
                }
            }
            for (int k = 0; k < 9; k++) {
                ImGui::TableNextColumn();
                ImGui::PushID(k);
                ImGui::SetNextItemWidth(k == 1 ? 90.0f : 60.0f);
                ImGui::InputInt("##v", &vals[i][k], 0, 0);
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText(T("Add item to the bag"));
    if (DevData_Ready() && DevData_ItemCount() > 0) {
        item_sel = clampi(item_sel, 0, DevData_ItemCount() - 1);
        const DevItem *it = DevData_ItemAt(item_sel);
        char label[80];

        snprintf(label, sizeof(label), "0x%03X %s", it->id, it->name);
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("##item", label)) {
            for (int i = 0; i < DevData_ItemCount(); i++) {
                const DevItem *x = DevData_ItemAt(i);
                char l2[80];

                snprintf(l2, sizeof(l2), "0x%03X %s", x->id, x->name);
                if (ImGui::Selectable(l2, i == item_sel)) {
                    item_sel = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(TL("Add", "additem"))) {
            push_edit(DEVEDIT_BAG_ADD, it->id, 0, 0);
        }
    }
    {
        char res[8][DEVEDIT_RESULT_LEN];
        int n = DevEdit_Results(res, 8);

        if (n > 0) {
            ImGui::SeparatorText(T("Last results"));
            for (int i = 0; i < n; i++) {
                ImGui::TextDisabled("%s", res[i]);
            }
        }
    }
}

} // namespace

extern "C" {

void SettingsUi_SetTab(const char *name) {
    SDL_strlcpy(g_start_tab, name, sizeof(g_start_tab));
}

int SettingsUi_IsOpen(void) {
    return g_open;
}

void SettingsUi_SetOpen(int on) {
    g_open = on != 0;
    g_cap_bit = -1;
    g_msg[0] = 0;
    g_pad_quiet = true;
    printf("[ui] settings %s (F1)\n", g_open ? "open" : "closed");
    fflush(stdout);
}

int SettingsUi_Capturing(void) {
    return g_open && g_cap_bit >= 0;
}

int SettingsUi_PadNavOk(void) {
    if (g_cap_bit >= 0) {
        return 0;
    }
    if (g_pad_quiet && !any_pad_button()) {
        g_pad_quiet = false;
    }
    return !g_pad_quiet;
}

int SettingsUi_Capture(const SDL_Event *e) {
    if (!SettingsUi_Capturing()) {
        return 0;
    }
    if (e->type == SDL_EVENT_KEY_DOWN) {
        SDL_Scancode sc = e->key.scancode;

        if (e->key.repeat) {
            return 1;
        }
        if (sc == SDL_SCANCODE_ESCAPE) {
            end_capture();
        } else if (g_cap_slot == 2) {
            /* waiting for a gamepad button: keys other than Esc are ignored */
        } else if (Settings_KeyReserved(sc)) {
            snprintf(g_msg, sizeof(g_msg), "%s is reserved, pick another key.", SDL_GetScancodeName(sc));
        } else {
            Settings_SetKey(g_cap_bit, g_cap_slot, sc);
            g_msg[0] = 0;
            end_capture();
        }
        return 1;
    }
    if (e->type == SDL_EVENT_KEY_UP || e->type == SDL_EVENT_TEXT_INPUT) {
        return 1;
    }
    if (g_cap_slot == 2 && e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        Settings_SetPad(g_cap_bit, e->gbutton.button);
        end_capture();
        return 1;
    }
    if (g_cap_slot == 2 && e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e->gaxis.value > 16384 &&
        (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        Settings_SetPad(g_cap_bit, e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? SET_PAD_LTRIGGER : SET_PAD_RTRIGGER);
        end_capture();
        return 1;
    }
    return 0;
}

void SettingsUi_Draw(void) {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 ds = io.DisplaySize;
    float k = ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi;
    float w = 600.0f * k, h = 720.0f * k;
    bool open = true;

    w = w < ds.x * 0.95f ? w : ds.x * 0.95f;
    h = h < ds.y * 0.95f ? h : ds.y * 0.95f;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.94f);
    if (ImGui::Begin(TL("Settings (F1)", "settings"), &open, ImGuiWindowFlags_NoCollapse)) {
        update_banner();
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem(TL("Start", "t_start"), NULL, tab_flags("Start"))) {
                start_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(TL("Display", "t_display"), NULL, tab_flags("Display"))) {
                display_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(TL("Controls", "t_controls"), NULL, tab_flags("Controls"))) {
                controls_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(TL("Game", "t_game"), NULL, tab_flags("Game"))) {
                game_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(TL("Sound", "t_sound"), NULL, tab_flags("Sound"))) {
                sound_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(TL("Modding", "t_modding"), NULL, tab_flags("Modding"))) {
                modding_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(TL("Cheats", "t_cheats"), NULL, tab_flags("Cheats"))) {
                cheats_tab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            g_start_tab[0] = 0;
        }
        ImGui::Separator();
        const char *path = Settings_FilePath();
        if (path != NULL) {
            ImGui::TextDisabled(T("Settings file: %s"), path);
        } else {
            ui_hint(T("Not saved (no settings file in this run)."));
        }
        if (ImGui::Button(TL("Close (F1 / Esc)", "close"))) {
            open = false;
        }
        /* Community link, right of Close in Discord's colour. The system opens it: the Discord
           app when it handles discord.gg links, else the browser. */
        const char *discord = "Join Discord";
        float bw = ImGui::CalcTextSize(discord).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - bw);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f, 0.396f, 0.949f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.420f, 0.467f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.278f, 0.322f, 0.769f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button(discord)) {
            if (!SDL_OpenURL(DISCORD_URL)) {
                fprintf(stderr, "[ui] could not open %s: %s\n", DISCORD_URL, SDL_GetError());
            }
        }
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", DISCORD_URL);
        }
    }
    ImGui::End();
    if (!open) {
        SettingsUi_SetOpen(0);
    }
}

} // extern "C"
