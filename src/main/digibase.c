#include "common.h"
#ifdef DW2_NATIVE
#include "host/host.h"
#endif
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

#include "digispecies.inc"

s32 Digi_GetStatGrowth(s32 id, s32 k) {
    switch (k) {
    default:
    case 0:
        return Digi_FindBaseData(id)->u4.field_4h >> 12;
    case 1:
        return Digi_FindBaseData(id)->u6.growthLo & 0xF;
    case 2:
        return (Digi_FindBaseData(id)->u6.field_6h >> 4) & 0xF;
    case 3:
        return (Digi_FindBaseData(id)->u6.field_6h >> 8) & 0xF;
    case 4:
        return Digi_FindBaseData(id)->u6.field_6h >> 12;
    }
}
