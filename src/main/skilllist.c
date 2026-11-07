#include "common.h"
#include "main/game.h"
#include "main/task.h"
#include "main/cdpreload.h"
#include "main/portrait.h"
#include "main/faceslot.h"
#include "main/itemeffect.h"
#include "main/nameentry.h"
#include "main/gamemode.h"
#include "main/topmenu.h"
#include "main/submenu.h"
#include "main/status.h"
#include "main/itemuse.h"
#include "main/itemmenu.h"
#ifdef DW2_NATIVE
/* declared with its return type (an implicit int declaration reads the unextended high bits
 * natively) */
u8 Skill_GetMpCost(s32 id);
#endif

/* Small data this file defines (.sdata). Retail reaches it with %gp_rel here. */
Halves Menu_SkillMsgPos = { 0x10, 0xBA };

/* Task callbacks the descriptor below names (defined further down). */
void Task_DefaultDestroy(Actor *arg0);
void Menu_SkillListInit(Actor *arg0, s16 arg1);
void Menu_SkillListTask(Actor *a0);
void Menu_SkillListDraw(Actor *actor);

u16 Menu_SkillPaneMasks[] = { 0xAC, 0xCA, 0xB2, 0x12A };
#ifdef DW2_NATIVE
static s32 skill_tab_back = -1;
/* Modern UI: each listed technique's MP cost at the right of its line ("8 MP": the game's digits
 * and its own "MP" label letters), in the line's colour */
extern int UiText_Modern(void);
extern int UiText_Advance(int big, int c, int cell);
extern int UiText_SpaceAdvance(int big, int cell);
static s32 skill_mp[12] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static u8 skill_mpbuf[12][12];
#define SKILL_MP_RIGHT 110 /* from a name's left edge: the category window's inner right edge */

static void skill_mp_close(void) {
    for (s32 i = 0; i < 12; i++) Text_Close(&skill_mp[i]);
}

static void skill_mp_open(s32 i, s32 id, TextDescHalves *name) {
    TextDescHalves st = *name;
    u8 *b = skill_mpbuf[i], *e;
    s32 w = 0;

    Text_FormatNumber(b, Skill_GetMpCost(id), -3);
    for (e = b; *e != 0xFF; e++);
    e[0] = 0xFD;
    e[1] = 0x16; /* M */
    e[2] = 0x19; /* P */
    e[3] = 0xFF;
    for (e = b; *e != 0xFF; e++) w += *e == 0xFD ? UiText_SpaceAdvance(0, 7) : UiText_Advance(0, *e, 7);
    st.text = (s32)b;
    st.strArg0 = 0;
    st.pos.lo = (s16)(name->pos.lo + SKILL_MP_RIGHT - ((w + 255) >> 8));
    Text_OpenDesc(&skill_mp[i], (TextDesc *)&st);
}
#endif
TaskDesc Menu_SkillListDesc = {
    (TaskInitFn)Menu_SkillListInit, Menu_SkillListTask, Task_DefaultDestroy, Menu_SkillListDraw, 0x128, 4,
};

void Menu_SkillListBuildTabs(a)
Actor194C8 *a;
{
    u8 *tbl;
    s32 i;
    s32 r, c;
    u8 *q;
    void *base;

    tbl = a->selRecord;

    for (i = 3; i >= 0; i--) {
        a->records[i].count = 0;
    }

    for (i = 0; i < 0xC; i++) {
        q = tbl + i;
        if (q[0x22] == 0) continue;
        r = Skill_GetType(q[0x22]);
        c = a->records[r].count;
        a->records[r].arr[c] = q[0x22];
        a->records[r].count = (u16)a->records[r].count + 1;
    }

    for (i = 0; i < 4; i++) {
        base = Cd_GetFileEntry(0x5130022);
        a->block64[i] = *(Blk12 *)((u8 *)base + i * 0xC);
        a->scrollTop[i] = 0;
        a->slot54[i].v = 0;
        *(s16 *)((u8 *)&a->block64[i] + 2) = a->records[i].count;
    }
}

void Menu_SkillListOpenNames(Actor194C8 *w, s32 arg1)
{
  int new_var;
  TextDescHalves st;
  s32 i;
  s32 new_var2;
  s32 ch;
  s32 n;
  s32 j;
  s32 k;
  s32 d0;
  s32 d;
  s32 f;
  s32 m;
  for (i = 6; i < 18; i++)
  {
    Text_Close(&w->textBoxes[i]);
  }
#ifdef DW2_NATIVE
  skill_mp_close();
#endif

  st.strArg0 = 0;
  st.packedStyle = arg1;
  for (ch = 0; ch < 4; ch++)
  {
    st.pos = ((Halves *) Cd_GetFileEntry(0x5130025))[ch];
    d0 = w->scrollTop[ch];
    d = w->records[ch].count - d0;
    n = 3;
    if (d < 4)
    {
      n = d;
    }
    for (j = 0; j < n; j++)
    {
      i = d0;
      f = 0;
      if (w->curTab != ch)
      {
        f = 1;
      }
      else
      {
        new_var2 = d0;
        if (w->slot54[ch].row != (j + new_var2))
        {
          f = 1;
        }
      }
      new_var = 11;
      st.color = f;
      st.text = Skill_GetNameText(w->records[ch].arr[j + i]);
      m = j + 6;
      Text_OpenDesc(&w->textBoxes[(ch * 3) + m], (TextDesc *) (&st));
#ifdef DW2_NATIVE
      if (UiText_Modern()) skill_mp_open(ch * 3 + j, w->records[ch].arr[j + i], &st);
#endif
      st.pos.hi += new_var;
    }

    Text_SetColor(w->textBoxes[ch + 2], w->curTab != ch);
  }

}

void Menu_SkillListInit(Actor *arg0, s16 arg1) {
    arg0->work->field_A4 = arg1;
}

void Menu_SkillListTask(Actor *a0) {
    Actor194C8 *w = (Actor194C8 *)a0->work;
    Halves h;
    TextDescHalves st;
    s32 i;
    s32 k;
    s32 id;
    s32 t;
    s32 j;
    s32 n;

    switch (a0->stateLevel0) {
    case 0:
    default:
        w->selRecord = Menu_Ctx->selRecord;
        Menu_SkillListBuildTabs(w);
        w->curTab = 0;
        Mem_FillWordsNeg1(&w->textBoxes, 0x15);
        Task_NextState0(a0);
        break;
    case 1:
        switch (a0->stateLevel1) {
        case 0:
        default:
            if (Math_RampToOne((s32)a0, &w->fadeRamp) != 0) {
                break;
            }
#ifdef DW2_NATIVE
            /* Modern UI: Status / Techniques tabs (src/main/topmenu.c); every text slot of this screen
             * is in use, the back hint gets its own (closed with the others) */
            if (!Menu_OpenModernTabs(w->textBoxes, &skill_tab_back, (TextIdListEntry *)Cd_GetFileEntrySubPtr(0x5130024, 0), 1, 1))
#endif
            Text_PrintIdList(&w->textBoxes, (TextIdListEntry *)Cd_GetFileEntrySubPtr(0x5130024, 0), 1);
            Menu_SkillListOpenNames(w, 1);
            h.lo = 0x13;
            h.hi = 0x32;
            Text_OpenPacked(&w->nameText, (s32)&w->selRecord[0x4C], 1, h);
            Task_NextState1(a0);
            break;
        case 1:
            i = w->curTab;
            k = Menu_GridIndexColMajor(&w->slot54[i].v, (s16 *)w->block64[i].data);
            Text_Close(&w->descText);
            Text_Close(&w->numberText);
            if (k < w->records[i].count) {
                id = w->records[i].arr[w->slot54[i].row];
                st.pos = Menu_SkillMsgPos;
                st.packedStyle = 0x80;
                st.color = 0;
                st.text = Skill_GetDescText(id);
                Text_OpenDesc(&w->descText, (TextDesc *)&st);
                st.pos.hi = 0xCA;
                st.text = (s32)Cd_GetFileEntry(0x1FD0150);
                Text_FormatNumber(w->numBuf, Skill_GetMpCost(id), -4);
                st.strArg0 = (s32)w->numBuf;
                Text_OpenDesc(&w->numberText, (TextDesc *)&st);
            }
            Task_NextState1(a0);
            break;
        case 2:
            t = Pad_State[0].left;
            if (t > 0 || Pad_State[0].right > 0) {
                n = w->curTab;
                if (t > 0) {
                    n--;
                } else {
                    n++;
                }
                w->curTab = n & 3;
                Snd_PlayById(0xD, 0);
                Menu_SkillListOpenNames(w, 0);
                Task_SetState1(a0, 1);
            } else {
                j = w->curTab;
                if (Menu_MoveGridCursorP1((s32)&w->slot54[j], (s32)&w->block64[j]) != 0) {
                    Menu_ScrollToShow(&w->scrollTop[j], w->slot54[j].row, 3);
                    Menu_SkillListOpenNames(w, 0);
                    Snd_PlayById(0xD, 0);
                    Task_SetState1(a0, 1);
                } else if (Pad_State[0].triangle > 0 || Pad_State[0].circle > 0) {
                    Task_SetState0(a0, 2);
                    if (Pad_State[0].circle > 0) {
                        Menu_Ctx->confirmed = -1;
                        Snd_PlayById(0xE, 0);
                    } else {
                        Menu_Ctx->confirmed = 0;
                        Snd_PlayById(0xB, 0);
                    }
                }
            }
            break;
        }
        break;
    case 2:
        switch (a0->stateLevel1) {
        case 0:
        default:
            Text_CloseArray(&w->textBoxes, 0x15);
#ifdef DW2_NATIVE
            Text_CloseArray(&skill_tab_back, 1);
            skill_mp_close();
#endif
            Task_NextState1(a0);
            break;
        case 1:
            if (Math_RampToZero((s32)a0, &w->fadeRamp) == 0) {
                Task_SetState0(a0, 3);
            }
            break;
        }
        break;
    }
}

void Menu_SkillListDraw(Actor *actor) {
    Wk19BF4 *w = (Wk19BF4 *)actor->work;
    s32 *p;
    s32 *list;
    void *obj;
    s32 i;
    s32 j;
    s32 k;
    s32 m;
    u16 v;
    Pair54 tmp;

    if (w->ramp == 0) {
        return;
    }
    p = (s32 *)Cd_GetFileEntry(0x5130026);
    for (i = 0; p[i] != 0; i++) {
        obj = Cd_GetFileEntry(p[i]);
        switch (i) {
        case 0:
            k = w->activePane;
            v = Menu_SkillPaneMasks[k];
            if (w->records[k].count != 0) {
                tmp = w->cursors[k];
                tmp.field_2 = w->cursors[k].field_2 - w->scroll[k];
                Menu_SetPartsGridPos(obj, 0x4000, (s32 *)&tmp, &w->grids[k].cols);
                Gfx_SetPartsPalette(obj, 0x4000, (actor->elapsed >> 2) & 3);
            } else {
                v |= 0x4000;
            }
            Gfx_HidePartsByMask(obj, v);
            break;
        case 1:
            m = 0xFFFFF;
            for (j = 0; j < 4; j++) {
                if (w->scroll[j] != 0) {
                    if (w->activePane == j) {
                        m -= 1 << (j * 4 + 1);
                    } else {
                        m -= 1 << (j * 4 + 2);
                    }
                }
                if (w->grids[j].rows - w->scroll[j] >= 4) {
                    if (w->activePane == j) {
                        m -= 1 << (j * 4 + 3);
                    } else {
                        m -= 1 << (j * 4 + 4);
                    }
                }
            }
            Gfx_HidePartsByMask(obj, m);
            break;
        }
        Gfx_SetPartsScale(obj, 0x1000, w->ramp);
        Gfx_DrawParts((s32)obj);
    }
}
