/* dw2-pc-port cheat menu backend (F1 Cheats tab, host/settingsui.cpp).
 *
 * Off by default and never saved as on. Every change goes through the PD.4 state edit queue
 * (host/devedit.c: checked ranges, applied by the game thread at its VBlank wait), so the main
 * thread never writes game memory. Turning cheats on for the first time in a run:
 *   - copies card1.mcd / card2.mcd to cheat_backup_<date>/ in the save folder,
 *   - keeps the save block (Save_GameState) as it is just before the first change, for "Undo all
 * cheat changes".
 * The game thread copies the save block for the tab to show (Cheat_GameTick). Undo writes the kept
 * block back at a VBlank wait outside battles and memory card scenes. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "common.h"
#include "host/devedit.h"
#include "main/156C.h"

#define GS_SIZE 0x1058

extern GameState Save_GameState;
extern SysState Sys_State;
const char *Host_CardDir(void);

static SDL_Mutex *lock;
static SDL_AtomicInt active, want_keep, want_undo, undo_result;
static unsigned char snap[GS_SIZE], kept[GS_SIZE];
static int snap_ok, kept_ok, snap_mode;
static char backup[1024];

static int copy_file(const char *from, const char *to) {
    size_t n = 0;
    void *d = SDL_LoadFile(from, &n);
    SDL_IOStream *io;
    int ok = 0;

    if (d == NULL) {
        return 0;
    }
    io = SDL_IOFromFile(to, "wb");
    if (io != NULL) {
        ok = SDL_WriteIO(io, d, n) == n;
        SDL_CloseIO(io);
    }
    SDL_free(d);
    return ok;
}

/* Game thread, at each VBlank wait (host/main.c) */
void Cheat_GameTick(void) {
    int mode = Sys_State.gameMode;

    if (lock == NULL || !SDL_GetAtomicInt(&active)) {
        return;
    }
    SDL_LockMutex(lock);
    memcpy(snap, &Save_GameState, GS_SIZE);
    snap_ok = 1;
    snap_mode = mode;
    if (SDL_GetAtomicInt(&want_undo)) {
        int table = mode >> 8;

        if (!kept_ok) {
            SDL_SetAtomicInt(&undo_result, -1);
            SDL_SetAtomicInt(&want_undo, 0);
        } else if (table != 5 && table != 6) {
            memcpy(&Save_GameState, kept, GS_SIZE);
            printf("[cheat] save block restored to the state before cheats\n");
            SDL_SetAtomicInt(&undo_result, 1);
            SDL_SetAtomicInt(&want_undo, 0);
        } /* in a battle or the card scene: waits */
    }
    SDL_UnlockMutex(lock);
}

/* Game thread, DevEdit_Apply, just before a batch of edits: the first one after cheats were turned
 * on keeps the save block as it was, for undo. */
void Cheat_BeforeEdit(void) {
    if (lock != NULL && SDL_GetAtomicInt(&want_keep)) {
        SDL_LockMutex(lock);
        memcpy(kept, &Save_GameState, GS_SIZE);
        kept_ok = 1;
        SDL_SetAtomicInt(&want_keep, 0);
        SDL_UnlockMutex(lock);
    }
}

/* Main thread. Returns 1 when on. */
int Cheat_SetActive(int on) {
    if (lock == NULL) {
        lock = SDL_CreateMutex();
    }
    if (on && !SDL_GetAtomicInt(&active)) {
        if (backup[0] == 0) {
            const char *dir = Host_CardDir();
            SDL_Time now = 0;
            SDL_DateTime dt;
            char from[1024], to[1024];

            SDL_GetCurrentTime(&now);
            SDL_TimeToDateTime(now, &dt, true);
            snprintf(backup, sizeof(backup), "%scheat_backup_%04d%02d%02d_%02d%02d%02d", dir, dt.year, dt.month, dt.day,
                     dt.hour, dt.minute, dt.second);
            SDL_CreateDirectory(backup);
            for (int p = 1; p <= 2; p++) {
                snprintf(from, sizeof(from), "%scard%d.mcd", dir, p);
                snprintf(to, sizeof(to), "%s/card%d.mcd", backup, p);
                if (copy_file(from, to)) {
                    printf("[cheat] backup %s\n", to);
                }
            }
            SDL_SetAtomicInt(&want_keep, 1);
        }
        DevEdit_SetAllowed(1);
        SDL_SetAtomicInt(&active, 1);
        printf("[cheat] on\n");
    } else if (!on && SDL_GetAtomicInt(&active)) {
        DevEdit_SetAllowed(0);
        SDL_SetAtomicInt(&active, 0);
        printf("[cheat] off\n");
    }
    fflush(stdout);
    return SDL_GetAtomicInt(&active);
}

int Cheat_Active(void) {
    return SDL_GetAtomicInt(&active);
}

const char *Cheat_BackupDir(void) {
    return backup;
}

/* Main thread: the save block as the game thread copied it last; 0 when none yet. */
int Cheat_Snapshot(unsigned char *out, int *mode) {
    int ok;

    if (lock == NULL) {
        return 0;
    }
    SDL_LockMutex(lock);
    ok = snap_ok;
    if (ok) {
        memcpy(out, snap, GS_SIZE);
        *mode = snap_mode;
    }
    SDL_UnlockMutex(lock);
    return ok;
}

void Cheat_Undo(void) {
    SDL_SetAtomicInt(&undo_result, 0);
    SDL_SetAtomicInt(&want_undo, 1);
}

/* 1 restored, -1 nothing kept, 0 pending / not asked */
int Cheat_UndoResult(void) {
    return SDL_GetAtomicInt(&undo_result);
}

int Cheat_UndoPending(void) {
    return SDL_GetAtomicInt(&want_undo);
}
