/* Shared by the live lab and the synthetic runtime regression test. */
void Stg20_BuildDnaOffspring(DigiRosterEntry *nd, const DigiRosterEntry *p,
                            const DigiRosterEntry *q, s32 result) {
    s32 t, i, v, hi, lo, best, bestv, n, s, j;
    u8 ml;
    t = Digi_GetRank(result);
    Mem_Zero(nd, sizeof *nd);
    nd->state = 1;
    nd->level = t * 10 + 1;
    nd->digiId = result;
    nd->dp = (p->dp > q->dp ? p->dp : q->dp) + 1;
    if (p->level > q->level) {
        ml = p->level + q->level / 5;
    } else {
        ml = q->level + p->level / 5;
    }
    nd->maxLevel = ml;
    if (ml >= 99) {
        nd->maxLevel = 99;
    }
#ifdef DW2_NATIVE
    /* Custom high-rank DNA may start above the inherited cap. */
    if (nd->maxLevel < nd->level) nd->maxLevel = nd->level;
#endif
    if (nd->level == 1) {
        nd->exp = 0;
    } else {
        nd->exp = Digi_GetExpToNextLevel(nd->level - 1, 100, 0);
    }
    s = p->maxHp + q->maxHp;
    switch (t) {
    case 0:
    default:
        v = s * 10;
        break;
    case 1:
        v = s * 34;
        break;
#ifdef DW2_NATIVE
    case 3: /* Custom Mega DNA uses Ultimate stat inheritance. */
#endif
    case 2:
        v = s * 45;
        break;
    }
    nd->hp = nd->maxHp = v / 100;
    s = p->maxMp + q->maxMp;
    switch (t) {
    case 0:
    default:
        v = s * 10;
        break;
    case 1:
        v = s * 34;
        break;
#ifdef DW2_NATIVE
    case 3: /* Custom Mega DNA uses Ultimate stat inheritance. */
#endif
    case 2:
        v = s * 45;
        break;
    }
    nd->mp = nd->maxMp = v / 100;
    if (p->attack > q->attack) {
        hi = p->attack;
        lo = q->attack;
    } else {
        lo = p->attack;
        hi = q->attack;
    }
    switch (t) {
    case 0:
    default:
        nd->attack = (hi * 40 + lo * 30) / 100;
        break;
    case 1:
        nd->attack = (hi * 50 + lo * 30) / 100;
        break;
#ifdef DW2_NATIVE
    case 3: /* Custom Mega DNA uses Ultimate stat inheritance. */
#endif
    case 2:
        nd->attack = (hi * 50 + lo * 40) / 100;
        break;
    }
    if (p->defense > q->defense) {
        hi = p->defense;
        lo = q->defense;
    } else {
        lo = p->defense;
        hi = q->defense;
    }
    switch (t) {
    case 0:
    default:
        nd->defense = (hi * 40 + lo * 30) / 100;
        break;
    case 1:
        nd->defense = (hi * 50 + lo * 30) / 100;
        break;
#ifdef DW2_NATIVE
    case 3: /* Custom Mega DNA uses Ultimate stat inheritance. */
#endif
    case 2:
        nd->defense = (hi * 50 + lo * 40) / 100;
        break;
    }
    switch (t) {
    case 0:
    default:
        nd->speed = (p->speed + q->speed) * 30 / 100;
        break;
    case 1:
        nd->speed = (p->speed + q->speed) * 45 / 100;
        break;
#ifdef DW2_NATIVE
    case 3: /* Custom Mega DNA uses Ultimate stat inheritance. */
#endif
    case 2:
        nd->speed = (p->speed + q->speed) / 2;
        break;
    }
    best = 0;
    bestv = 0;
    for (i = 0; i < 12; i++) {
        if (p->skills[i] != 0 && t >= Skill_GetRank(p->skills[i])) {
            v = Skill_GetPower(p->skills[i]);
            if (bestv < v) {
                bestv = v;
                best = p->skills[i];
            }
        }
        if (q->skills[i] != 0 && t >= Skill_GetRank(q->skills[i])) {
            v = Skill_GetPower(q->skills[i]);
            if (bestv < v) {
                bestv = v;
                best = q->skills[i];
            }
        }
    }
    nd->skills[0] = Digi_GetLearnedSkill(nd->digiId);
    if (best != 0 && nd->skills[0] != best) {
        nd->skills[1] = best;
    }
    for (j = 0, n = 0; j < 12; j++) {
        if (p->skills[j] != nd->skills[0] && p->skills[j] != nd->skills[1]) {
            nd->learnableSkills[n++] = p->skills[j];
        }
        if (q->skills[j] != nd->skills[0] && q->skills[j] != nd->skills[1]) {
            nd->learnableSkills[n++] = q->skills[j];
        }
    }
    nd->parent0 = p->digiId;
    nd->parent1 = q->digiId;
}

