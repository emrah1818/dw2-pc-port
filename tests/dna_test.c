/* Real game DNA resolver and rank getter, synthetic DIGIMNDT only. */
#include "common.h"
#define printf game_printf
#include "main/game.h"
#undef printf
#include "host/host.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

u8 Stg20_GetDnaResult(s32 a, s32 b);
extern u8 Stg20_DnaResultTbl[][3][8][8];
static DigiBaseData base[5];
s32 Cd_GetFileOrNull(s32 id) { return id == 0xC6C ? (s32)(intptr_t)base : 0; }
void Stg20_BuildDnaOffspring(DigiRosterEntry *, const DigiRosterEntry *, const DigiRosterEntry *, s32);
void Mem_Zero(void *data, s32 size) { memset(data, 0, size); }
s32 Digi_GetExpToNextLevel(s32 level, s32 max, s32 current) { return level * 100; }
s32 Skill_GetRank(s32 id) { return 0; }
s16 Skill_GetPower(s32 id) { return 1; }
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int parse(const char *s) { return Host_ModConfParse(s, strlen(s), 2); }
#define RULE "{\"format\":\"dw2-dna-overrides\",\"version\":1,\"rules\":[{\"a\":1,\"b\":2,\"result\":3,\"rank\":3}]}"
int main(void) {
    for (int i=0; i<4; i++) { base[i].id=i+1; base[i].u4.field_4h=0x10; }
    Host_ModConfReset();
    CHECK(Host_DnaRank(3) == -1);
    CHECK(Digi_GetRank(3) == 1);
    CHECK(Stg20_GetDnaResult(1,2) == Stg20_DnaResultTbl[0][0][0][0]);
    CHECK(parse(RULE) == 1);
    CHECK(Stg20_GetDnaResult(1,2) == 3 && Stg20_GetDnaResult(2,1) == 3);
    CHECK(Digi_GetRank(3) == 3);
    {
        DigiRosterEntry p = {0}, q = {0}, child;
        p.digiId = 1; q.digiId = 2;
        p.level = q.level = 11;
        p.maxHp = 100; q.maxHp = 200;
        p.maxMp = 50; q.maxMp = 100;
        p.attack = p.defense = p.speed = 100;
        q.attack = q.defense = q.speed = 50;
        p.dp = 2; q.dp = 4;
        Stg20_BuildDnaOffspring(&child, &p, &q, Stg20_GetDnaResult(1, 2));
        CHECK(child.digiId == 3 && child.level == 31 && child.maxLevel == 31);
        CHECK(child.hp == 135 && child.mp == 67);
        CHECK(child.attack == 70 && child.defense == 70 && child.speed == 75);
        CHECK(child.dp == 5 && child.parent0 == 1 && child.parent1 == 2);
    }
    CHECK(Stg20_GetDnaResult(1,4) == Stg20_DnaResultTbl[0][0][0][0]);
    CHECK(parse("{\"format\":\"dw2-dna-overrides\",\"version\":2,\"rules\":[]}") == 0);
    CHECK(parse("{\"format\":\"dw2-dna-overrides\",\"version\":1,\"rules\":[{\"a\":1,\"b\":2,\"result\":3,\"rank\":4}]}") == 0);
    CHECK(parse("{\"format\":\"dw2-dna-overrides\",\"version\":1,\"rules\":[{\"a\":1,\"b\":2,\"result\":3,\"rank\":true}]}") == 0);
    CHECK(parse("{\"format\":\"dw2-dna-overrides\",\"version\":1,\"rules\":[{\"a\":1,\"b\":2,\"result\":3,\"rank\":1.5}]}") == 0);
    CHECK(parse("{\"format\":\"dw2-dna-overrides\",\"version\":1,\"rules\":[{\"a\":1,\"b\":2,\"result\":3,\"rank\":3},{\"a\":2,\"b\":1,\"result\":4,\"rank\":2}]}") == 0);
    CHECK(parse("{\"format\":\"dw2-dna-overrides\",\"version\":1,\"rules\":[{\"a\":1,\"b\":2,\"result\":3,\"rank\":3},{\"a\":1,\"b\":4,\"result\":3,\"rank\":2}]}") == 0);
    CHECK(parse(RULE " trailing") == 0);
    CHECK(Host_DnaResult(1,2) == 3 && Digi_GetRank(3) == 3);
    CHECK(parse("{\"format\":\"dw2-dna-overrides\",\"version\":1,\"rules\":[]}") == 1);
    CHECK(Host_DnaResult(1,2) == 0 && Digi_GetRank(3) == 1);
    base[0].u4.field_4h = 0; /* No vanilla table row exists for Rookie DNA. */
    CHECK(Stg20_GetDnaResult(1,2) == 0);
    CHECK(parse(RULE) == 1);
    CHECK(Stg20_GetDnaResult(1,2) == 3);
    base[0].u4.field_4h = 0x10;
    Host_ModConfReset();
    CHECK(Host_DnaResult(1,2) == 0 && Digi_GetRank(3) == 1);
    CHECK(Host_DnaResult(-1,999) == 0 && Host_DnaRank(256) == -1);
    printf("dna_test: %s (%d failures)\n", fails ? "FAILED" : "passed", fails);
    return fails != 0;
}
