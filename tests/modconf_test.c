/* dw2-pc-port: Mods/digivolve_levels.json and Mods/battle_camera.json reader (host/modconf.c).
 * Made-up files only, no game data. */
#include <stdio.h>
#include <string.h>

void Host_ModConfReset(void);
int Host_ModConfParse(const char *data, unsigned long size, int which);
int Host_DigivolveMinLevel(int digi_id);
void Host_BattleCamAdjust(int digi_id, int closeup, int *vpy, int *vpz, int *vry, int *rot_y);

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int parse(const char *s, int which) { return Host_ModConfParse(s, strlen(s), which); }

int main(void) {
    int vpy, vpz, vry, ry;

    /* levels */
    Host_ModConfReset();
    CHECK(parse("{\"format\": 1, \"levels\": {\"84\": 8, \"131\": 15}, \"future\": [1, {\"x\": 2}]}", 0) == 1);
    CHECK(Host_DigivolveMinLevel(84) == 8 && Host_DigivolveMinLevel(131) == 15 && Host_DigivolveMinLevel(3) == 0);
    CHECK(parse("{\"format\":1,\"levels\":{\"0\":5,\"300\":5,\"5\":0,\"6\":100,\"7\":20}}", 0) == 1);
    CHECK(Host_DigivolveMinLevel(5) == 0 && Host_DigivolveMinLevel(6) == 0 && Host_DigivolveMinLevel(7) == 20);
    CHECK(parse("{\"format\":2,\"levels\":{\"9\":3}}", 0) == -1 && Host_DigivolveMinLevel(9) == 0);
    CHECK(parse("{\"format\":1,\"levels\":{\"9\":3", 0) == 0 && Host_DigivolveMinLevel(9) == 0);  /* cut off */
    CHECK(parse("not json", 0) == 0 && Host_DigivolveMinLevel(84) == 8);                           /* kept */
    CHECK(Host_DigivolveMinLevel(0) == 0 && Host_DigivolveMinLevel(-1) == 0 && Host_DigivolveMinLevel(999) == 0);

    /* camera */
    Host_ModConfReset();
    CHECK(parse("{\"format\":1,\"digimon\":{\"84\":{\"distance\":150,\"height\":200,\"target\":-100,\"angle\":64,"
                "\"closeup\":{\"distance\":80}}}}", 1) == 1);
    vpy = -1300, vpz = 12000, vry = -1400, ry = 170;
    Host_BattleCamAdjust(84, 0, &vpy, &vpz, &vry, &ry);
    CHECK(vpz == 18000 && vpy == -1500 && vry == -1300 && ry == 234);
    vpy = -3120, vpz = 7495, vry = -304, ry = 0;
    Host_BattleCamAdjust(84, 1, &vpy, &vpz, &vry, &ry);
    CHECK(vpz == 5996 && vpy == -3120 && vry == -304 && ry == 0);
    vpy = -1300, vpz = 12000, vry = -1400, ry = 170;
    Host_BattleCamAdjust(3, 0, &vpy, &vpz, &vry, &ry);                 /* not listed: unchanged */
    CHECK(vpz == 12000 && vpy == -1300 && vry == -1400 && ry == 170);
    CHECK(parse("{\"format\":1,\"digimon\":{\"5\":{\"distance\":9999}}}", 1) == 1);
    vpy = 0, vpz = 1000, vry = 0, ry = 0;
    Host_BattleCamAdjust(5, 0, &vpy, &vpz, &vry, &ry);
    CHECK(vpz == 4000);                                                 /* clamped to 400 % */
    CHECK(parse("{\"format\":1,\"digimon\":{\"6\":{\"closeup\":{\"height\":10}}}}", 1) == 1);
    vpy = 0, vpz = 1000, vry = 0, ry = 0;
    Host_BattleCamAdjust(6, 0, &vpy, &vpz, &vry, &ry);                 /* attack shots unchanged */
    CHECK(vpz == 1000 && vpy == 0);
    Host_BattleCamAdjust(6, 1, &vpy, &vpz, &vry, &ry);
    CHECK(vpy == -10);

    printf(fails ? "modconf_test: %d failed\n" : "modconf_test: all passed\n", fails);
    return fails != 0;
}
