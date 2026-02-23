/*
 * Trial by Combat - Raylib Edition
 * Compile: gcc trial_by_combat_raylib.c -lraylib -lm -o trial_by_combat
 *
 * Sprites (place PNGs in same folder as executable):
 *   p1_knight.png   p1_magician.png   p1_alchemist.png
 *   p2_knight.png   p2_magician.png   p2_alchemist.png
 * When a fighter's HP hits 0, their sprite vanishes completely.
 *
 * Window: 1280x720, black background
 * Layout:
 *   TOP:    P1 name + HP bar + charge pips (left)
 *           P2 name + HP bar + charge pips (right)
 *   MIDDLE: P1 sprite (left), P2 sprite (right)
 *   BOTTOM: move menu / battle log / result screen
 */

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===================== CONSTANTS ===================== */

#define SW 1280
#define SH 720

#define MAX_CHARGE     10
#define MAX_TURNS      25
#define MAX_DOT_STACKS 3
#define MAX_LOG_LINES  8

#define MOVE_ATK  0
#define MOVE_DEF  1
#define MOVE_DOT  2
#define MOVE_BUFF 3
#define MOVE_ULT  4

static const int CHARGE_GAIN[5] = {3, 2, 1, 1, 0};

#define CLASS_KNIGHT    0
#define CLASS_MAGICIAN  1
#define CLASS_ALCHEMIST 2

/* Sprite placeholder colors per class (used on class select screen only) */
static const Color CLASS_COLOR[3] = {
    {120, 140, 200, 255},  /* Knight   - steel blue */
    {180, 100, 220, 255},  /* Magician - purple     */
    {80,  180, 120, 255},  /* Alchemist - green     */
};

/* Loaded sprite textures: [player 0/1][class 0/1/2] */
static Texture2D gSprites[2][3];

/*
 * Font: place MedievalSharp.ttf in the same folder as the executable.
 * Download free from: https://www.1001fonts.com/medievalsharp-font.html
 * If the file is missing, Raylib falls back to its built-in font.
 * Other good options: Cinzel.ttf, UnifrakturMaguntia.ttf, Almendra.ttf
 */
#define FONT_FILE "MedievalSharp.ttf"
#define FONT_SIZE_LOAD 64   /* load at high res so it looks sharp at all sizes */
static Font gFont;

/* ===================== STRUCTS ===================== */

typedef struct {
    char name[32];
    int  classId;
    int  hp, maxHp;
    int  baseAtk, baseDef, baseSpd;
    int  crt;
    int  charge;
    int  buffActive, buffTurns, buffStat, buffAmt;
    int  dotStacks, dotTurns;
    int  defPenalty;
} Fighter;

typedef struct {
    char name[32];
    int  type;
    int  cost;
} Move;

/* ===================== MOVE TABLES ===================== */

static Move KNIGHT_MOVES[5] = {
    {"Steady Blade",          MOVE_ATK,  0},
    {"Aegis Wall",            MOVE_DEF,  0},
    {"Mortal Wounds",         MOVE_DOT,  3},
    {"Indomitable Spirit",    MOVE_BUFF, 2},
    {"Executioner's Verdict", MOVE_ULT,  10}
};
static Move MAGICIAN_MOVES[5] = {
    {"Elemental Spark",  MOVE_ATK,  0},
    {"Mana Barrier",     MOVE_DEF,  0},
    {"Flesh Embers",     MOVE_DOT,  3},
    {"Runic Overclock",  MOVE_BUFF, 2},
    {"Arcane Overload",  MOVE_ULT,  10}
};
static Move ALCHEMIST_MOVES[5] = {
    {"Primed Flask",        MOVE_ATK,  0},
    {"Pact of Attrition",   MOVE_DEF,  0},
    {"Vial of Corrosion",   MOVE_DOT,  3},
    {"Adrenal Mixture",     MOVE_BUFF, 2},
    {"Grand Transmutation", MOVE_ULT,  10}
};

static const int BASE_ATK_DAMAGE[3] = {15, 13, 14};
static const int BASE_ULT_DAMAGE[3] = {28, 26, 22};
static const int DOT_BASE[3]        = {5,  8,  12};

/* ===================== GAME STATE ===================== */

typedef enum {
    SCREEN_MENU,
    SCREEN_SELECT_CLASS_P1,
    SCREEN_SELECT_CLASS_P2,
    SCREEN_SELECT_OPPONENT,
    SCREEN_BATTLE,
    SCREEN_RESOLVE,
    SCREEN_RESULT,
    SCREEN_GAUNTLET_BATTLE,   /* secret 3v1 mode - choosing move + target */
    SCREEN_GAUNTLET_RESOLVE,  /* secret 3v1 mode - showing results        */
} GameScreen;

typedef struct {
    char lines[MAX_LOG_LINES][128];
    int  count;
} BattleLog;

typedef struct {
    GameScreen screen;
    Fighter    p1, p2;
    int        vsComputer;
    int        turn;
    int        moveP1, moveP2;
    int        p1chosen;          /* in pvp: has p1 chosen yet */
    BattleLog  log;
    int        selectedMove;      /* cursor in move menu */
    char       resultMsg[128];
    int        postChoice;        /* 0=none,1=again,2=menu,3=exit */

    /* === GAUNTLET STATE === */
    int        gauntletMode;      /* 1 if in gauntlet */
    Fighter    enemies[3];        /* the three opponents */
    int        selectedTarget;    /* 0/1/2 which enemy to attack */
    int        gauntletMove;      /* player's chosen move this turn */

    /* secret word buffer for menu unlock */
    char       secretBuf[16];
    int        secretLen;
} GameState;

/* ===================== HELPERS ===================== */

int eAtk(Fighter *f) { return f->baseAtk + (f->buffActive && f->buffStat==2 ? f->buffAmt : 0); }
int eDef(Fighter *f) { int d = f->baseDef + (f->buffActive && f->buffStat==0 ? f->buffAmt:0) - f->defPenalty; return d<0?0:d; }
int eSpd(Fighter *f) { return f->baseSpd  + (f->buffActive && f->buffStat==1 ? f->buffAmt : 0); }

int randPct(void) { return rand() % 100; }

int calcDamage(int base, int atk, int def) {
    int d = base + (atk/2) - (def/3);
    return d < 1 ? 1 : d;
}
int calcDotTick(int base, int atk, int def) {
    int d = base + (atk/4) - (def/4);
    return d < 1 ? 1 : d;
}

Move *getMoves(int classId) {
    if (classId == CLASS_MAGICIAN)  return MAGICIAN_MOVES;
    if (classId == CLASS_ALCHEMIST) return ALCHEMIST_MOVES;
    return KNIGHT_MOVES;
}

void initFighter(Fighter *f, const char *name, int classId) {
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, 31);
    f->classId = classId;
    f->crt = 12;
    switch (classId) {
        case CLASS_KNIGHT:
            f->hp=f->maxHp=115; f->baseAtk=10; f->baseDef=12; f->baseSpd=9;
            f->buffStat=0; f->buffAmt=4; break;
        case CLASS_MAGICIAN:
            f->hp=f->maxHp=105; f->baseAtk=10; f->baseDef=10; f->baseSpd=12;
            f->buffStat=1; f->buffAmt=4; break;
        case CLASS_ALCHEMIST:
            f->hp=f->maxHp=110; f->baseAtk=12; f->baseDef=10; f->baseSpd=10;
            f->buffStat=2; f->buffAmt=4; break;
    }
}

void logAdd(BattleLog *log, const char *msg) {
    if (log->count < MAX_LOG_LINES) {
        strncpy(log->lines[log->count++], msg, 127);
    } else {
        /* scroll up */
        for (int i = 0; i < MAX_LOG_LINES-1; i++)
            strncpy(log->lines[i], log->lines[i+1], 127);
        strncpy(log->lines[MAX_LOG_LINES-1], msg, 127);
    }
}

void logClear(BattleLog *log) { log->count = 0; }

/* ===================== AI ===================== */

int chooseMoveAI(Fighter *ai, Fighter *opp) {
    int hpPct = (ai->hp * 100) / ai->maxHp;

    if (ai->charge == MAX_CHARGE && randPct() < 65) return MOVE_ULT;
    if (hpPct < 25 && randPct() < 60)               return MOVE_DEF;

    if (opp->buffActive) {
        int r = randPct();
        if (r < 45) return MOVE_ATK;
        if (r < 70 && ai->charge >= 3) return MOVE_DOT;
    }
    if (opp->dotStacks < MAX_DOT_STACKS && ai->charge >= 3 && randPct() < 35)
        return MOVE_DOT;
    if (!ai->buffActive && ai->charge >= 2 && hpPct > 40 && randPct() < 40)
        return MOVE_BUFF;
    if (ai->charge >= 7 && ai->charge < MAX_CHARGE && randPct() < 25)
        return MOVE_DEF;
    return MOVE_ATK;
}

/* ===================== RESOLVE TURN ===================== */

void resolveTurn(Fighter *a, Fighter *b, int moveA, int moveB, BattleLog *log) {
    Move *movesA = getMoves(a->classId);
    Move *movesB = getMoves(b->classId);
    int typeA = movesA[moveA].type;
    int typeB = movesB[moveB].type;

    char buf[128];
    snprintf(buf, 128, "%s used %s", a->name, movesA[moveA].name); logAdd(log, buf);
    snprintf(buf, 128, "%s used %s", b->name, movesB[moveB].name); logAdd(log, buf);

    for (int dir = 0; dir < 2; dir++) {
        Fighter *att = (dir==0)?a:b, *def=(dir==0)?b:a;
        int myT  = (dir==0)?typeA:typeB;
        int oppT = (dir==0)?typeB:typeA;
        int aStat = eAtk(att), dStat = eDef(def);
        int dodge = 5 + eSpd(def);

        if (myT == MOVE_ATK) {
            if (randPct() < dodge) {
                snprintf(buf,128,"%s dodged!", def->name); logAdd(log,buf);
            } else {
                double mult = 1.0;
                if (oppT==MOVE_DEF)  mult=0.5;
                if (oppT==MOVE_BUFF) mult=1.3;
                int crit = (randPct() < att->crt);
                int dmg  = calcDamage(BASE_ATK_DAMAGE[att->classId], aStat, dStat);
                if (crit) dmg = dmg*3/2;
                dmg = (int)(dmg*mult); if(dmg<1)dmg=1;
                def->hp -= dmg;
                snprintf(buf,128,"%s%s -> %s: %d dmg%s",
                    crit?"CRIT! ":"", att->name, def->name, dmg,
                    oppT==MOVE_DEF?" (blocked)":oppT==MOVE_BUFF?" (off-guard)":"");
                logAdd(log,buf);
            }
        }

        if (myT == MOVE_DOT) {
            if (oppT == MOVE_ATK) {
                snprintf(buf,128,"%s's DoT interrupted!", att->name); logAdd(log,buf);
            } else if (randPct() < dodge) {
                snprintf(buf,128,"%s evaded DoT!", def->name); logAdd(log,buf);
            } else {
                if (def->dotStacks < MAX_DOT_STACKS) def->dotStacks++;
                def->dotTurns = 3;
                snprintf(buf,128,"%s: DoT stack %d/3%s", def->name, def->dotStacks,
                    oppT==MOVE_BUFF?" EMPOWERED!":""); logAdd(log,buf);
            }
        }

        if (myT == MOVE_BUFF) {
            if (oppT == MOVE_DEF) {
                snprintf(buf,128,"%s's buff suppressed!", att->name); logAdd(log,buf);
            } else {
                att->buffActive=1; att->buffTurns=3;
                static const char *sn[3]={"DEF","SPD","ATK"};
                snprintf(buf,128,"%s buffed! +%d %s (3T)", att->name, att->buffAmt, sn[att->buffStat]);
                logAdd(log,buf);
            }
        }

        if (myT == MOVE_ULT) {
            double mult=1.0;
            if (oppT==MOVE_DEF)  mult=0.25;
            if (oppT==MOVE_BUFF) mult=1.25;
            int effDef = (att->classId==CLASS_MAGICIAN)?dStat/2:dStat;
            int crit   = (randPct()<att->crt);
            int dmg    = calcDamage(BASE_ULT_DAMAGE[att->classId], aStat, effDef);
            if (crit) dmg=dmg*7/5;
            dmg=(int)(dmg*mult); if(dmg<1)dmg=1;
            def->hp -= dmg;
            snprintf(buf,128,"%sULTIMATE! %s -> %s: %d dmg%s",
                crit?"CRIT! ":"", att->name, def->name, dmg,
                oppT==MOVE_DEF?" (deflected)":""); logAdd(log,buf);

            if (att->classId==CLASS_KNIGHT) {
                def->defPenalty+=2;
                snprintf(buf,128,"Armor sundered! %s -2 DEF permanently", def->name);
                logAdd(log,buf);
            }
            if (att->classId==CLASS_ALCHEMIST && def->hp>0) {
                int total=att->hp+def->hp; if(total<0)total=0;
                int na=total*6/10, nd=total-na;
                if(na>att->maxHp)na=att->maxHp;
                att->hp=na; def->hp=nd;
                snprintf(buf,128,"Transmutation! HP split: %s=%d, %s=%d",
                    att->name,att->hp,def->name,def->hp); logAdd(log,buf);
            }
        }
    }

    /* DoT ticks */
    for (int dir=0;dir<2;dir++) {
        Fighter *f=(dir==0)?a:b, *src=(dir==0)?b:a;
        if (f->dotStacks>0 && f->dotTurns>0) {
            int tick=calcDotTick(DOT_BASE[f->dotStacks-1],eAtk(src),eDef(f));
            f->hp-=tick; f->dotTurns--;
            snprintf(buf,128,"DoT: %s burned %d (%dT left)",f->name,tick,f->dotTurns);
            logAdd(log,buf);
            if(f->dotTurns==0){ f->dotStacks=0;
                snprintf(buf,128,"%s's DoT faded",f->name); logAdd(log,buf); }
        }
    }

    /* Charge */
    int ga=CHARGE_GAIN[typeA]-movesA[moveA].cost;
    int gb=CHARGE_GAIN[typeB]-movesB[moveB].cost;
    a->charge+=ga; b->charge+=gb;
    if(a->charge>MAX_CHARGE)a->charge=MAX_CHARGE;
    if(b->charge>MAX_CHARGE)b->charge=MAX_CHARGE;
    if(a->charge<0)a->charge=0;
    if(b->charge<0)b->charge=0;

    /* Buff tick */
    for(int dir=0;dir<2;dir++){
        Fighter *f=(dir==0)?a:b;
        if(f->buffActive && --f->buffTurns<=0){
            f->buffActive=0;
            snprintf(buf,128,"%s's buff expired",f->name); logAdd(log,buf);
        }
    }
}

/* ===================== FONT WRAPPERS ===================== */
/* Use gFont everywhere so swapping the font file changes all text at once */

void FDrawText(const char *text, int x, int y, int size, Color color) {
    DrawTextEx(gFont, text, (Vector2){(float)x,(float)y}, (float)size, 1.0f, color);
}

int FMeasureText(const char *text, int size) {
    Vector2 v = MeasureTextEx(gFont, text, (float)size, 1.0f);
    return (int)v.x;
}

/* ===================== DRAWING ===================== */

/* HP bar: x,y = top-left, w=width, h=height, fills left to right */
void drawHPBar(int x, int y, int w, int h, int hp, int maxHp, const char *label) {
    float ratio = (maxHp>0) ? (float)hp/(float)maxHp : 0;
    if(ratio<0)ratio=0;
    if(ratio>1)ratio=1;

    Color fill = GREEN;
    if (ratio < 0.5f) fill = YELLOW;
    if (ratio < 0.25f) fill = RED;

    DrawRectangle(x, y, w, h, (Color){40,40,40,255});        /* bg */
    DrawRectangle(x, y, (int)(w*ratio), h, fill);             /* fill */
    DrawRectangleLines(x, y, w, h, (Color){180,180,180,255}); /* border */

    char txt[64];
    snprintf(txt, 64, "%s  %d/%d", label, hp>0?hp:0, maxHp);
    FDrawText(txt, x+5, y+h+4, 19, WHITE);
}

/* HP bar filling right to left (for player 2) */
void drawHPBarRTL(int x, int y, int w, int h, int hp, int maxHp, const char *label) {
    float ratio = (maxHp>0) ? (float)hp/(float)maxHp : 0;
    if(ratio<0)ratio=0;
    if(ratio>1)ratio=1;

    Color fill = GREEN;
    if (ratio < 0.5f) fill = YELLOW;
    if (ratio < 0.25f) fill = RED;

    int fillW = (int)(w*ratio);
    DrawRectangle(x, y, w, h, (Color){40,40,40,255});
    DrawRectangle(x + w - fillW, y, fillW, h, fill);
    DrawRectangleLines(x, y, w, h, (Color){180,180,180,255});

    char txt[64];
    snprintf(txt, 64, "%d/%d  %s", hp>0?hp:0, maxHp, label);
    int tw = FMeasureText(txt, 19);
    FDrawText(txt, x+w-tw-5, y+h+4, 19, WHITE);
}

/* Charge pips: 10 small squares */
void drawChargePips(int x, int y, int charge, int align) {
    int pipW=22, pipH=16, gap=3;
    int totalW = MAX_CHARGE*(pipW+gap)-gap;
    int startX = (align==1) ? x-totalW : x;

    for (int i=0;i<MAX_CHARGE;i++) {
        int px = startX + i*(pipW+gap);
        Color c = (i < charge) ? (Color){255,220,50,255} : (Color){50,50,50,255};
        DrawRectangle(px, y, pipW, pipH, c);
        DrawRectangleLines(px, y, pipW, pipH, (Color){120,120,120,255});
    }

    if (charge == MAX_CHARGE) {
        const char *ultTxt = "ULTIMATE READY!";
        int tw = FMeasureText(ultTxt, 16);
        int tx = (align==1) ? x-totalW : x;
        FDrawText(ultTxt, tx + totalW/2 - tw/2, y+pipH+3, 16, (Color){255,80,80,255});
    }
}

/* Draw a fighter's sprite scaled up. If dead, draw nothing. */
#define SPRITE_SCALE 3.0f
void drawSprite(int playerIdx, int classId, int x, int y, int dead) {
    if (dead) return;
    Texture2D tex = gSprites[playerIdx][classId];
    int drawW = (int)(tex.width * SPRITE_SCALE);
    DrawTextureEx(tex, (Vector2){x - drawW/2, (float)y}, 0.0f, SPRITE_SCALE, WHITE);
}

/* Status tags (buff/dot) drawn under sprite */
void drawStatusTags(int x, int y, Fighter *f) {
    int ox=x, fs=13;
    if (f->buffActive) {
        char b[32]; snprintf(b,32,"BUFF %dT",f->buffTurns);
        DrawRectangle(ox,y,60,18,(Color){30,80,180,200});
        FDrawText(b,ox+3,y+2,fs,(Color){180,220,255,255});
        ox+=65;
    }
    if (f->dotStacks>0) {
        char b[32]; snprintf(b,32,"DoT%d %dT",f->dotStacks,f->dotTurns);
        DrawRectangle(ox,y,65,18,(Color){180,50,20,200});
        FDrawText(b,ox+3,y+2,fs,(Color){255,180,100,255});
    }
}

/* Battle log panel */
void drawBattleLog(BattleLog *log, int x, int y, int w, int h) {
    DrawRectangle(x,y,w,h,(Color){15,15,15,230});
    DrawRectangleLines(x,y,w,h,(Color){80,80,80,255});
    int ly=y+8, fs=16;
    for (int i=0;i<log->count;i++) {
        FDrawText(log->lines[i], x+8, ly, fs, (Color){200,200,200,255});
        ly+=fs+5;
    }
}

/* Move menu */
void drawMoveMenu(Fighter *f, int selected, int x, int y, int w) {
    Move *moves = getMoves(f->classId);
    static const char *tn[5]={"ATK","DEF","DoT","Buff","Ult"};
    static const Color typeColor[5]={
        {220,80,80,255},   /* ATK red    */
        {80,120,220,255},  /* DEF blue   */
        {200,120,40,255},  /* DoT orange */
        {80,200,120,255},  /* Buff green */
        {220,180,40,255},  /* Ult gold   */
    };

    int rowH=40, fs=18, pad=10;
    DrawRectangle(x,y,w,rowH*5+pad*2,(Color){20,20,20,240});
    DrawRectangleLines(x,y,w,rowH*5+pad*2,(Color){80,80,80,255});

    for (int i=0;i<5;i++) {
        int ry=y+pad+i*rowH;
        int locked=(f->charge<moves[i].cost);

        if (i==selected)
            DrawRectangle(x+2,ry,w-4,rowH-2,(Color){60,60,80,255});

        Color textC = locked?(Color){80,80,80,255}:WHITE;

        /* type badge */
        DrawRectangle(x+pad,ry+6,38,24, locked?(Color){40,40,40,255}:typeColor[moves[i].type]);
        int bw=FMeasureText(tn[moves[i].type],14);
        FDrawText(tn[moves[i].type],x+pad+19-bw/2,ry+9,14,locked?(Color){60,60,60,255}:BLACK);

        /* move name */
        FDrawText(moves[i].name, x+pad+50, ry+10, fs, textC);

        /* cost & gain */
        char info[32];
        snprintf(info,32,"Cost:%d +%d",moves[i].cost,CHARGE_GAIN[moves[i].type]);
        int iw=FMeasureText(info,15);
        FDrawText(info, x+w-iw-pad, ry+11, 15, locked?(Color){60,60,60,255}:(Color){180,180,180,255});

        if (locked) {
            int lw=FMeasureText("[LOCKED]",14);
            FDrawText("[LOCKED]", x+w-lw-pad-90, ry+12, 14, (Color){150,50,50,255});
        }

        if (i==selected && !locked)
            FDrawText(">", x+w-16, ry+10, fs, (Color){255,220,50,255});
    }

    FDrawText("W/S or UP/DOWN   ENTER to confirm",
             x+pad, y+pad+5*rowH+5, 14, (Color){120,120,120,255});
}

/* ===================== SCREEN RENDERERS ===================== */

void drawMenuScreen(void) {
    int cx=SW/2;
    FDrawText("TRIAL BY COMBAT", cx-FMeasureText("TRIAL BY COMBAT",48)/2, 180, 48, WHITE);
    FDrawText("1  VS COMPUTER", cx-FMeasureText("1  VS COMPUTER",28)/2, 320, 28, (Color){200,200,200,255});
    FDrawText("2  VS PLAYER",   cx-FMeasureText("2  VS PLAYER",28)/2,   370, 28, (Color){200,200,200,255});
    FDrawText("3  EXIT",        cx-FMeasureText("3  EXIT",28)/2,        420, 28, (Color){200,200,200,255});
    FDrawText("Press 1, 2, or 3", cx-FMeasureText("Press 1, 2, or 3",18)/2, 500, 18, (Color){100,100,100,255});
}

void drawClassSelectScreen(const char *label, int hoveredClass) {
    int cx=SW/2;
    FDrawText(label, cx-FMeasureText(label,32)/2, 80, 32, WHITE);

    static const char *names[3]={"Knight","Magician","Alchemist"};
    static const char *descs[3]={
        "115 HP | ATK 10 | DEF 12 | SPD  9 | Buff: +4 DEF",
        "105 HP | ATK 10 | DEF 10 | SPD 12 | Buff: +4 SPD",
        "110 HP | ATK 12 | DEF 10 | SPD 10 | Buff: +4 ATK",
    };
    static const char *ultsDesc[3]={
        "Ult: Sunder armor (-2 DEF permanent)",
        "Ult: Ignore 50% enemy DEF",
        "Ult: Redistribute HP 60/40",
    };

    for (int i=0;i<3;i++) {
        int bx=cx-280, by=180+i*140, bw=560, bh=120;
        int hovered=(hoveredClass==i);
        DrawRectangle(bx,by,bw,bh, hovered?(Color){40,40,70,255}:(Color){20,20,30,255});
        DrawRectangleLines(bx,by,bw,bh, hovered?(Color){200,200,255,255}:(Color){80,80,80,255});

        /* class color swatch */
        DrawRectangle(bx+10,by+10,60,100, CLASS_COLOR[i]);
        FDrawText(names[i], bx+80, by+15, 26, WHITE);
        FDrawText(descs[i], bx+80, by+52, 16, (Color){180,180,180,255});
        FDrawText(ultsDesc[i], bx+80, by+80, 16, (Color){220,180,80,255});

        char key[4]; snprintf(key,4,"%d",i+1);
        FDrawText(key, bx+bw-30, by+bh/2-12, 24, hovered?YELLOW:(Color){120,120,120,255});
    }
    FDrawText("Press 1, 2, or 3", cx-FMeasureText("Press 1, 2, or 3",18)/2, 620, 18, (Color){100,100,100,255});
}

void drawOpponentSelectScreen(int hovered) {
    int cx=SW/2;
    FDrawText("Choose Opponent", cx-FMeasureText("Choose Opponent",32)/2, 80, 32, WHITE);

    static const char *names[4]={"Knight","Magician","Alchemist","Random"};
    for (int i=0;i<4;i++) {
        int bx=cx-220+i*120, by=300, bw=100, bh=80;
        int h=(hovered==i);
        Color bc = (i<3)?CLASS_COLOR[i]:(Color){100,100,100,255};
        DrawRectangle(bx,by,bw,bh, h?(Color){bc.r,bc.g,bc.b,255}:(Color){bc.r/3,bc.g/3,bc.b/3,255});
        DrawRectangleLines(bx,by,bw,bh, h?WHITE:(Color){80,80,80,255});
        int nw=FMeasureText(names[i],16);
        FDrawText(names[i], bx+bw/2-nw/2, by+bh/2-8, 16, WHITE);
        char key[4]; snprintf(key,4,"%d",i+1);
        FDrawText(key, bx+bw/2-6, by+bh-20, 16, h?YELLOW:(Color){120,120,120,255});
    }
    FDrawText("Press 1-4", cx-FMeasureText("Press 1-4",18)/2, 430, 18, (Color){100,100,100,255});
}

void drawBattleScreen(GameState *gs) {
    Fighter *p1=&gs->p1, *p2=&gs->p2;

    /* --- TOP UI: HP bars --- */
    /* P1: top-left */
    drawHPBar(30, 20, 380, 22, p1->hp, p1->maxHp, p1->name);
    /* P2: top-right, RTL */
    drawHPBarRTL(SW-410, 20, 380, 22, p2->hp, p2->maxHp, p2->name);

    /* Charge pips */
    drawChargePips(30, 62, p1->charge, 0);
    drawChargePips(SW-30, 62, p2->charge, 1);

    /* Turn counter */
    char turnTxt[32];
    snprintf(turnTxt,32,"Turn %d/%d", gs->turn, MAX_TURNS);
    int tw=FMeasureText(turnTxt,20);
    FDrawText(turnTxt, SW/2-tw/2, 20, 20, (Color){160,160,160,255});

    /* --- SPRITES --- */
    /* P1: left side */
    int sp1x=250, spy=110;
    drawSprite(0, p1->classId, sp1x, spy, p1->hp<=0);
    drawStatusTags(sp1x-48, spy + (int)(gSprites[0][p1->classId].height * SPRITE_SCALE) + 6, p1);

    /* P2: right side */
    int sp2x=SW-250, sp2y=110;
    drawSprite(1, p2->classId, sp2x, sp2y, p2->hp<=0);
    drawStatusTags(sp2x-48, sp2y + (int)(gSprites[1][p2->classId].height * SPRITE_SCALE) + 6, p2);

    /* --- BATTLE LOG hidden during move selection --- */
    /* (log is shown on the resolve screen after moves are submitted) */

    /* --- MOVE MENU: under the currently active player's side ---
     * P1 choosing -> left side (under P1 sprite)
     * P2 choosing -> right side (under P2 sprite)
     * VS Computer -> always left (P1) */
    if (!gs->vsComputer && gs->p1chosen) {
        /* P2's turn: menu on the RIGHT */
        char hdr[64]; snprintf(hdr,64,"%s - Choose your move:", p2->name);
        int menuX = SW-580;
        FDrawText(hdr, menuX, 330, 18, WHITE);
        drawMoveMenu(p2, gs->selectedMove, menuX, 355, 560);
    } else {
        /* P1's turn (or VS computer): menu on the LEFT */
        Fighter *cf = gs->vsComputer ? p1 : p1;
        char hdr[64]; snprintf(hdr,64,"%s - Choose your move:", cf->name);
        FDrawText(hdr, 20, 330, 18, WHITE);
        drawMoveMenu(cf, gs->selectedMove, 20, 355, 560);
    }
}

void drawResolveScreen(GameState *gs) {
    Fighter *p1=&gs->p1, *p2=&gs->p2;

    /* HP bars */
    drawHPBar(30, 20, 380, 22, p1->hp, p1->maxHp, p1->name);
    drawHPBarRTL(SW-410, 20, 380, 22, p2->hp, p2->maxHp, p2->name);
    drawChargePips(30, 62, p1->charge, 0);
    drawChargePips(SW-30, 62, p2->charge, 1);

    /* Sprites */
    drawSprite(0, p1->classId, 250,    110, p1->hp<=0);
    drawSprite(1, p2->classId, SW-250, 110, p2->hp<=0);
    drawStatusTags(202, 110 + (int)(gSprites[0][p1->classId].height * SPRITE_SCALE) + 6, p1);
    drawStatusTags(SW-298, 110 + (int)(gSprites[1][p2->classId].height * SPRITE_SCALE) + 6, p2);

    /* Battle log: bottom-center */
    int logW=560, logH=MAX_LOG_LINES*21+16;
    drawBattleLog(&gs->log, SW/2-logW/2, 355, logW, logH);

    FDrawText("Press ENTER to continue...", SW/2-FMeasureText("Press ENTER to continue...",18)/2, 660, 18, (Color){120,120,120,255});
}

void drawResultScreen(GameState *gs) {
    int cx=SW/2;
    FDrawText(gs->resultMsg, cx-FMeasureText(gs->resultMsg,36)/2, 200, 36, WHITE);

    char hp1[64],hp2[64];
    snprintf(hp1,64,"%s: %d HP remaining", gs->p1.name, gs->p1.hp>0?gs->p1.hp:0);
    snprintf(hp2,64,"%s: %d HP remaining", gs->p2.name, gs->p2.hp>0?gs->p2.hp:0);
    FDrawText(hp1, cx-FMeasureText(hp1,20)/2, 260, 20, (Color){180,180,180,255});
    FDrawText(hp2, cx-FMeasureText(hp2,20)/2, 290, 20, (Color){180,180,180,255});

    FDrawText("1  Play Again", cx-FMeasureText("1  Play Again",26)/2, 380, 26, (Color){200,200,200,255});
    FDrawText("2  Main Menu",  cx-FMeasureText("2  Main Menu",26)/2,  420, 26, (Color){200,200,200,255});
    FDrawText("3  Exit",       cx-FMeasureText("3  Exit",26)/2,       460, 26, (Color){200,200,200,255});
}

/* ===================== GAUNTLET HELPERS ===================== */

/*
 * Player HP in gauntlet = sum of all three enemy maxHp * 1.5
 * Attacks stay the same. Enemies AI each get a full turn targeting player.
 * Kill reward: +20 HP (capped at maxHp).
 */
#define GAUNTLET_HEAL_REWARD 20

void initGauntlet(GameState *gs) {
    /* Init three enemies: Knight, Magician, Alchemist */
    static const char *en[3] = {"Knight","Magician","Alchemist"};
    for (int i=0;i<3;i++) initFighter(&gs->enemies[i], en[i], i);

    /* Scale player HP: 1.5 * total enemy HP */
    int totalEnemyHp = gs->enemies[0].maxHp + gs->enemies[1].maxHp + gs->enemies[2].maxHp;
    int scaledHp = (int)(totalEnemyHp * 1.5f);
    gs->p1.hp = gs->p1.maxHp = scaledHp;

    gs->turn          = 1;
    gs->selectedMove  = 0;
    gs->selectedTarget = 0;
    gs->gauntletMode  = 1;
    logClear(&gs->log);
}

/* Find first living enemy for default target */
int firstAliveEnemy(GameState *gs) {
    for (int i=0;i<3;i++) if (gs->enemies[i].hp>0) return i;
    return -1;
}

int allEnemiesDead(GameState *gs) {
    return gs->enemies[0].hp<=0 && gs->enemies[1].hp<=0 && gs->enemies[2].hp<=0;
}

/* Resolve one gauntlet turn */
void resolveGauntletTurn(GameState *gs) {
    Fighter *player = &gs->p1;
    int      move   = gs->gauntletMove;
    int      tgt    = gs->selectedTarget;
    char     buf[128];

    Move *pmoves = getMoves(player->classId);
    logAdd(&gs->log, "--- YOUR TURN ---");
    snprintf(buf,128,"You used %s", pmoves[move].name); logAdd(&gs->log, buf);

    /* Player acts on selected target (if alive) */
    if (tgt >= 0 && tgt < 3 && gs->enemies[tgt].hp > 0) {
        Fighter *target = &gs->enemies[tgt];
        int myT  = pmoves[move].type;
        int aStat = eAtk(player), dStat = eDef(target);
        int dodge = 5 + eSpd(target);

        if (myT == MOVE_ATK) {
            if (randPct() < dodge) {
                snprintf(buf,128,"%s dodged!", target->name); logAdd(&gs->log,buf);
            } else {
                int crit=(randPct()<player->crt);
                int dmg=calcDamage(BASE_ATK_DAMAGE[player->classId],aStat,dStat);
                if(crit) dmg=dmg*3/2;
                if(dmg<1)dmg=1;
                target->hp-=dmg;
                snprintf(buf,128,"%s%s -> %s: %d dmg",crit?"CRIT! ":"",player->name,target->name,dmg);
                logAdd(&gs->log,buf);
                if(target->hp<=0){
                    snprintf(buf,128,"%s defeated! +%d HP",target->name,GAUNTLET_HEAL_REWARD);
                    logAdd(&gs->log,buf);
                    player->hp+=GAUNTLET_HEAL_REWARD;
                    if(player->hp>player->maxHp) player->hp=player->maxHp;
                }
            }
        } else if (myT == MOVE_DOT) {
            if (randPct() < dodge) {
                snprintf(buf,128,"%s evaded DoT!", target->name); logAdd(&gs->log,buf);
            } else {
                if(target->dotStacks<MAX_DOT_STACKS) target->dotStacks++;
                target->dotTurns=3;
                snprintf(buf,128,"DoT on %s (stack %d/3)",target->name,target->dotStacks);
                logAdd(&gs->log,buf);
            }
        } else if (myT == MOVE_BUFF) {
            player->buffActive=1; player->buffTurns=3;
            static const char *sn[3]={"DEF","SPD","ATK"};
            snprintf(buf,128,"You buffed! +%d %s",player->buffAmt,sn[player->buffStat]);
            logAdd(&gs->log,buf);
        } else if (myT == MOVE_DEF) {
            snprintf(buf,128,"You brace for impact!"); logAdd(&gs->log,buf);
        } else if (myT == MOVE_ULT) {
            int effDef=(player->classId==CLASS_MAGICIAN)?dStat/2:dStat;
            int crit=(randPct()<player->crt);
            int dmg=calcDamage(BASE_ULT_DAMAGE[player->classId],aStat,effDef);
            if(crit) dmg=dmg*7/5;
            if(dmg<1)dmg=1;
            target->hp-=dmg;
            snprintf(buf,128,"%sULTIMATE -> %s: %d dmg!",crit?"CRIT! ":"",target->name,dmg);
            logAdd(&gs->log,buf);
            if(player->classId==CLASS_KNIGHT){ target->defPenalty+=2;
                snprintf(buf,128,"%s armor sundered! -2 DEF",target->name); logAdd(&gs->log,buf);}
            if(player->classId==CLASS_ALCHEMIST && target->hp>0){
                int total=player->hp+target->hp; if(total<0)total=0;
                int np=total*6/10, nt=total-np;
                if(np>player->maxHp)np=player->maxHp;
                player->hp=np; target->hp=nt;
                snprintf(buf,128,"Transmutation: you=%d, %s=%d",player->hp,target->name,target->hp);
                logAdd(&gs->log,buf);}
            if(target->hp<=0){
                snprintf(buf,128,"%s defeated! +%d HP",target->name,GAUNTLET_HEAL_REWARD);
                logAdd(&gs->log,buf);
                player->hp+=GAUNTLET_HEAL_REWARD;
                if(player->hp>player->maxHp) player->hp=player->maxHp;
            }
        }
    }

    /* Charge update for player */
    int gain = CHARGE_GAIN[pmoves[move].type] - pmoves[move].cost;
    player->charge += gain;
    if(player->charge>MAX_CHARGE) player->charge=MAX_CHARGE;
    if(player->charge<0) player->charge=0;

    /* Buff tick for player */
    if(player->buffActive && --player->buffTurns<=0){
        player->buffActive=0; logAdd(&gs->log,"Your buff expired.");}

    /* DoT tick on player */
    /* (enemies don't apply DoT to player in this version - they only ATK/DEF/ULT) */

    /* ---- ENEMIES ACT ---- */
    logAdd(&gs->log, "--- ENEMIES TURN ---");
    int playerDefending = (pmoves[move].type == MOVE_DEF);

    for (int i=0;i<3;i++) {
        Fighter *e = &gs->enemies[i];
        if (e->hp <= 0) continue;

        int emove = chooseMoveAI(e, player);
        Move *em  = getMoves(e->classId);
        snprintf(buf,128,"%s: %s", e->name, em[emove].name); logAdd(&gs->log,buf);

        int et = em[emove].type;
        int eDodge = 5 + eSpd(player);
        int ea = eAtk(e), ed = eDef(player);

        /* If player is defending, reduce incoming by 50% */
        double defMult = playerDefending ? 0.5 : 1.0;

        if (et == MOVE_ATK) {
            if (randPct() < eDodge) {
                logAdd(&gs->log," You dodged!");
            } else {
                int crit=(randPct()<e->crt);
                int dmg=calcDamage(BASE_ATK_DAMAGE[e->classId],ea,ed);
                if(crit) dmg=dmg*3/2;
                dmg=(int)(dmg*defMult); if(dmg<1)dmg=1;
                player->hp-=dmg;
                snprintf(buf,128,"%s%s deals %d to you%s",crit?"CRIT! ":"",e->name,dmg,playerDefending?" (blocked)":"");
                logAdd(&gs->log,buf);
            }
        } else if (et == MOVE_ULT) {
            int effDef=(e->classId==CLASS_MAGICIAN)?ed/2:ed;
            int crit=(randPct()<e->crt);
            int dmg=calcDamage(BASE_ULT_DAMAGE[e->classId],ea,effDef);
            if(crit) dmg=dmg*7/5;
            dmg=(int)(dmg*defMult); if(dmg<1)dmg=1;
            player->hp-=dmg;
            snprintf(buf,128,"%s%s ULTIMATE: %d dmg!",crit?"CRIT! ":"",e->name,dmg);
            logAdd(&gs->log,buf);
            if(e->classId==CLASS_KNIGHT){ player->defPenalty+=2;
                snprintf(buf,128,"Your armor sundered! -2 DEF"); logAdd(&gs->log,buf);}
        } else if (et == MOVE_BUFF) {
            e->buffActive=1; e->buffTurns=3;
        } else if (et == MOVE_DEF) {
            /* enemy defends - just gains charge */
        }
        /* Charge for enemy */
        int eg = CHARGE_GAIN[et] - em[emove].cost;
        e->charge += eg;
        if(e->charge>MAX_CHARGE)e->charge=MAX_CHARGE;
        if(e->charge<0)e->charge=0;
        /* Buff tick */
        if(e->buffActive && --e->buffTurns<=0) e->buffActive=0;
    }

    /* DoT ticks on enemies */
    for(int i=0;i<3;i++){
        Fighter *e=&gs->enemies[i];
        if(e->hp>0 && e->dotStacks>0 && e->dotTurns>0){
            int tick=calcDotTick(DOT_BASE[e->dotStacks-1],eAtk(player),eDef(e));
            e->hp-=tick; e->dotTurns--;
            snprintf(buf,128,"DoT: %s takes %d",e->name,tick); logAdd(&gs->log,buf);
            if(e->dotTurns==0){ e->dotStacks=0;
                snprintf(buf,128,"%s DoT faded",e->name); logAdd(&gs->log,buf);}
            if(e->hp<=0 && e->dotStacks>=0){
                snprintf(buf,128,"%s defeated by DoT! +%d HP",e->name,GAUNTLET_HEAL_REWARD);
                logAdd(&gs->log,buf);
                player->hp+=GAUNTLET_HEAL_REWARD;
                if(player->hp>player->maxHp) player->hp=player->maxHp;
                e->dotStacks=0;
            }
        }
    }
}

/* ===================== GAUNTLET DRAW ===================== */

void drawGauntletBattle(GameState *gs) {
    Fighter *p = &gs->p1;

    /* Player HP bar - full width at top */
    int barW=600;
    drawHPBar(SW/2-barW/2, 12, barW, 26, p->hp, p->maxHp, p->name);
    drawChargePips(SW/2-115, 52, p->charge, 0);

    char turnTxt[32];
    snprintf(turnTxt,32,"GAUNTLET - Turn %d/%d", gs->turn, MAX_TURNS);
    int tw=FMeasureText(turnTxt,18);
    FDrawText(turnTxt, SW/2-tw/2, 76, 18, (Color){200,160,60,255});

    /* Three enemies across the top third, each with mini HP bar */
    int eX[3] = {160, SW/2, SW-160};
    int eY = 100;
    for (int i=0;i<3;i++) {
        Fighter *e = &gs->enemies[i];
        int dead = (e->hp<=0);

        /* Target highlight ring */
        if (!dead && gs->selectedTarget==i) {
            int sprW=(int)(gSprites[1][e->classId].width*SPRITE_SCALE);
            int sprH=(int)(gSprites[1][e->classId].height*SPRITE_SCALE);
            DrawRectangleLines(eX[i]-sprW/2-4, eY-4, sprW+8, sprH+8, (Color){255,220,50,255});
        }

        drawSprite(1, e->classId, eX[i], eY, dead);

        /* Mini HP bar under each enemy */
        int mbW=140;
        if (!dead) {
            float r=(float)e->hp/(float)e->maxHp;
            Color fill=GREEN; if(r<0.5f)fill=YELLOW; if(r<0.25f)fill=RED;
            DrawRectangle(eX[i]-mbW/2, eY+220, mbW, 12, (Color){40,40,40,255});
            DrawRectangle(eX[i]-mbW/2, eY+220, (int)(mbW*r), 12, fill);
            DrawRectangleLines(eX[i]-mbW/2, eY+220, mbW, 12, (Color){150,150,150,255});
            char hpTxt[32]; snprintf(hpTxt,32,"%s %d/%d",e->name,e->hp,e->maxHp);
            int ht=FMeasureText(hpTxt,13);
            FDrawText(hpTxt, eX[i]-ht/2, eY+235, 13, WHITE);
            /* charge pips mini */
            for(int p2=0;p2<MAX_CHARGE;p2++){
                int px=eX[i]-mbW/2+p2*14;
                DrawRectangle(px,eY+252,11,7,p2<e->charge?(Color){255,200,30,255}:(Color){40,40,40,255});
            }
            drawStatusTags(eX[i]-70, eY+264, e);
        } else {
            int dw=FMeasureText("DEFEATED",16);
            FDrawText("DEFEATED", eX[i]-dw/2, eY+220, 16, (Color){150,50,50,255});
        }
    }

    /* Target selection hint */
    FDrawText("< > to select target", SW/2-FMeasureText("< > to select target",16)/2, 300, 16, (Color){140,140,140,255});

    /* Move menu centered at bottom */
    drawMoveMenu(p, gs->selectedMove, SW/2-280, 330, 560);
}

void drawGauntletResolve(GameState *gs) {
    Fighter *p = &gs->p1;

    /* Player HP bar */
    int barW=600;
    drawHPBar(SW/2-barW/2, 12, barW, 26, p->hp, p->maxHp, p->name);
    drawChargePips(SW/2-115, 52, p->charge, 0);

    char turnTxt[32];
    snprintf(turnTxt,32,"GAUNTLET - Turn %d/%d", gs->turn, MAX_TURNS);
    int tw=FMeasureText(turnTxt,18);
    FDrawText(turnTxt, SW/2-tw/2, 76, 18, (Color){200,160,60,255});

    /* Enemies */
    int eX[3]={160,SW/2,SW-160}, eY=100;
    for(int i=0;i<3;i++){
        Fighter *e=&gs->enemies[i];
        int dead=(e->hp<=0);
        drawSprite(1,e->classId,eX[i],eY,dead);
        int mbW=140;
        if(!dead){
            float r=(float)e->hp/(float)e->maxHp;
            Color fill=GREEN; if(r<0.5f)fill=YELLOW; if(r<0.25f)fill=RED;
            DrawRectangle(eX[i]-mbW/2,eY+220,mbW,12,(Color){40,40,40,255});
            DrawRectangle(eX[i]-mbW/2,eY+220,(int)(mbW*r),12,fill);
            DrawRectangleLines(eX[i]-mbW/2,eY+220,mbW,12,(Color){150,150,150,255});
            char hpTxt[32]; snprintf(hpTxt,32,"%s %d/%d",e->name,e->hp,e->maxHp);
            int ht=FMeasureText(hpTxt,13);
            FDrawText(hpTxt,eX[i]-ht/2,eY+235,13,WHITE);
        } else {
            int dw=FMeasureText("DEFEATED",16);
            FDrawText("DEFEATED",eX[i]-dw/2,eY+220,16,(Color){150,50,50,255});
        }
    }

    /* Battle log centered */
    int logW=600, logH=MAX_LOG_LINES*21+16;
    drawBattleLog(&gs->log, SW/2-logW/2, 330, logW, logH);

    FDrawText("Press ENTER to continue...", SW/2-FMeasureText("Press ENTER to continue...",18)/2, 680, 18, (Color){120,120,120,255});
}

/* ===================== MAIN ===================== */

int main(void) {
    srand((unsigned)time(NULL));

    InitWindow(SW, SH, "Trial by Combat");
    SetTargetFPS(60);

    /* Load custom font. Place font.ttf in the same folder as the executable.
     * Rename FONT_FILE at the top of this file to match your font filename.
     * If the file is missing, Raylib uses its default font automatically. */
    gFont = LoadFontEx(FONT_FILE, FONT_SIZE_LOAD, NULL, 0);
    if (gFont.baseSize == 0) gFont = GetFontDefault();

    /* Load sprites: [player][class] */
    static const char *spriteFiles[2][3] = {
        {"p1_knight.png",  "p1_magician.png",  "p1_alchemist.png"},
        {"p2_knight.png",  "p2_magician.png",  "p2_alchemist.png"},
    };
    for (int p=0;p<2;p++)
        for (int c=0;c<3;c++)
            gSprites[p][c] = LoadTexture(spriteFiles[p][c]);

    GameState gs;
    memset(&gs, 0, sizeof(gs));
    gs.screen = SCREEN_MENU;

    int hoverClass = 0;  /* for class/opponent select hover */

    while (!WindowShouldClose()) {

        /* F11 toggles fullscreen on any screen */
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        /* ===== UPDATE ===== */
        switch (gs.screen) {

            case SCREEN_MENU:
                if (IsKeyPressed(KEY_ONE))   { gs.vsComputer=1; gs.screen=SCREEN_SELECT_CLASS_P1; hoverClass=0; }
                if (IsKeyPressed(KEY_TWO))   { gs.vsComputer=0; gs.screen=SCREEN_SELECT_CLASS_P1; hoverClass=0; }
                if (IsKeyPressed(KEY_THREE)) CloseWindow();

                /* Secret: type GAUNTLET to unlock 3v1 mode */
                {
                    static const char *secret = "GAUNTLET";
                    int key = GetKeyPressed();
                    if (key >= 'A' && key <= 'Z') {
                        if (gs.secretLen < 8) {
                            gs.secretBuf[gs.secretLen++] = (char)key;
                            gs.secretBuf[gs.secretLen]   = '\0';
                        } else {
                            /* shift buffer left */
                            memmove(gs.secretBuf, gs.secretBuf+1, 7);
                            gs.secretBuf[7] = (char)key;
                            gs.secretBuf[8] = '\0';
                            gs.secretLen = 8;
                        }
                        if (strcmp(gs.secretBuf, secret) == 0) {
                            /* Unlock! Go to class select for gauntlet */
                            gs.vsComputer = 2; /* 2 = gauntlet flag */
                            gs.screen = SCREEN_SELECT_CLASS_P1;
                            gs.secretLen = 0;
                            gs.secretBuf[0] = '\0';
                            hoverClass = 0;
                        }
                    }
                }
                break;

            case SCREEN_SELECT_CLASS_P1: {
                int c=-1;
                if (IsKeyPressed(KEY_ONE))   c=0;
                if (IsKeyPressed(KEY_TWO))   c=1;
                if (IsKeyPressed(KEY_THREE)) c=2;
                if (c>=0) {
                    if (gs.vsComputer==2) {
                        /* Gauntlet mode */
                        initFighter(&gs.p1, "Champion", c);
                        initGauntlet(&gs);
                        gs.screen=SCREEN_GAUNTLET_BATTLE;
                    } else {
                        initFighter(&gs.p1, gs.vsComputer?"Player":"Player 1", c);
                        gs.screen = gs.vsComputer ? SCREEN_SELECT_OPPONENT : SCREEN_SELECT_CLASS_P2;
                    }
                    hoverClass=0;
                }
                if (IsKeyPressed(KEY_UP))   hoverClass=(hoverClass+2)%3;
                if (IsKeyPressed(KEY_DOWN)) hoverClass=(hoverClass+1)%3;
                break;
            }

            case SCREEN_SELECT_CLASS_P2: {
                int c=-1;
                if (IsKeyPressed(KEY_ONE))   c=0;
                if (IsKeyPressed(KEY_TWO))   c=1;
                if (IsKeyPressed(KEY_THREE)) c=2;
                if (c>=0) {
                    initFighter(&gs.p2, "Player 2", c);
                    gs.screen=SCREEN_BATTLE;
                    gs.turn=1; gs.selectedMove=0; gs.p1chosen=0;
                    logClear(&gs.log);
                }
                if (IsKeyPressed(KEY_UP))   hoverClass=(hoverClass+2)%3;
                if (IsKeyPressed(KEY_DOWN)) hoverClass=(hoverClass+1)%3;
                break;
            }

            case SCREEN_SELECT_OPPONENT: {
                int chosen=-1;
                if (IsKeyPressed(KEY_ONE))   chosen=0;
                if (IsKeyPressed(KEY_TWO))   chosen=1;
                if (IsKeyPressed(KEY_THREE)) chosen=2;
                if (IsKeyPressed(KEY_FOUR))  chosen=rand()%3;
                if (chosen>=0) {
                    static const char *cn[3]={"Knight","Magician","Alchemist"};
                    initFighter(&gs.p2, cn[chosen], chosen);
                    gs.screen=SCREEN_BATTLE;
                    gs.turn=1; gs.selectedMove=0; gs.p1chosen=0;
                    logClear(&gs.log);
                }
                if (IsKeyPressed(KEY_UP))   hoverClass=(hoverClass+3)%4;
                if (IsKeyPressed(KEY_DOWN)) hoverClass=(hoverClass+1)%4;
                break;
            }

            case SCREEN_BATTLE: {
                /* move selection with keyboard */
                Fighter *cf = (!gs.vsComputer && gs.p1chosen) ? &gs.p2 : &gs.p1;
                Move *moves = getMoves(cf->classId);

                if (IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_W))
                    gs.selectedMove=(gs.selectedMove+4)%5;
                if (IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S))
                    gs.selectedMove=(gs.selectedMove+1)%5;

                if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) {
                    int idx=gs.selectedMove;
                    if (cf->charge < moves[idx].cost) break; /* locked, ignore */

                    if (gs.vsComputer) {
                        gs.moveP1=idx;
                        gs.moveP2=chooseMoveAI(&gs.p2,&gs.p1);
                        logClear(&gs.log);
                        resolveTurn(&gs.p1,&gs.p2,gs.moveP1,gs.moveP2,&gs.log);
                        gs.screen=SCREEN_RESOLVE;
                    } else {
                        if (!gs.p1chosen) {
                            gs.moveP1=idx;
                            gs.p1chosen=1;
                            gs.selectedMove=0;
                        } else {
                            gs.moveP2=idx;
                            gs.p1chosen=0;
                            logClear(&gs.log);
                            resolveTurn(&gs.p1,&gs.p2,gs.moveP1,gs.moveP2,&gs.log);
                            gs.screen=SCREEN_RESOLVE;
                        }
                    }
                }
                break;
            }

            case SCREEN_RESOLVE:
                if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) {
                    int d1=(gs.p1.hp<=0), d2=(gs.p2.hp<=0);
                    if (d1||d2) {
                        if (d1&&d2) strncpy(gs.resultMsg,"DRAW! Both fell!",127);
                        else if(d1) snprintf(gs.resultMsg,128,"%s WINS!",gs.p2.name);
                        else        snprintf(gs.resultMsg,128,"%s WINS!",gs.p1.name);
                        gs.screen=SCREEN_RESULT;
                    } else if (gs.turn >= MAX_TURNS) {
                        if      (gs.p1.hp>gs.p2.hp) snprintf(gs.resultMsg,128,"%s WINS by HP!",gs.p1.name);
                        else if (gs.p2.hp>gs.p1.hp) snprintf(gs.resultMsg,128,"%s WINS by HP!",gs.p2.name);
                        else    strncpy(gs.resultMsg,"DRAW! Equal HP!",127);
                        gs.screen=SCREEN_RESULT;
                    } else {
                        gs.turn++;
                        gs.selectedMove=0;
                        gs.p1chosen=0;
                        logClear(&gs.log);   /* clear log so battle screen is clean */
                        gs.screen=SCREEN_BATTLE;
                    }
                }
                break;

            case SCREEN_GAUNTLET_BATTLE: {
                Fighter *p = &gs.p1;
                Move *moves = getMoves(p->classId);

                if (IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_W))
                    gs.selectedMove=(gs.selectedMove+4)%5;
                if (IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S))
                    gs.selectedMove=(gs.selectedMove+1)%5;

                /* LEFT/RIGHT to cycle living targets */
                if (IsKeyPressed(KEY_LEFT)||IsKeyPressed(KEY_A)) {
                    int t=gs.selectedTarget;
                    do { t=(t+2)%3; } while(gs.enemies[t].hp<=0 && t!=gs.selectedTarget);
                    gs.selectedTarget=t;
                }
                if (IsKeyPressed(KEY_RIGHT)||IsKeyPressed(KEY_D)) {
                    int t=gs.selectedTarget;
                    do { t=(t+1)%3; } while(gs.enemies[t].hp<=0 && t!=gs.selectedTarget);
                    gs.selectedTarget=t;
                }

                if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) {
                    int idx=gs.selectedMove;
                    if (p->charge < moves[idx].cost) break;
                    gs.gauntletMove=idx;
                    logClear(&gs.log);
                    resolveGauntletTurn(&gs);
                    gs.screen=SCREEN_GAUNTLET_RESOLVE;
                }
                break;
            }

            case SCREEN_GAUNTLET_RESOLVE:
                if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) {
                    int playerDead=(gs.p1.hp<=0);
                    int allDead=allEnemiesDead(&gs);

                    if (playerDead) {
                        snprintf(gs.resultMsg,128,"You fell... the Gauntlet wins.");
                        gs.screen=SCREEN_RESULT;
                    } else if (allDead) {
                        snprintf(gs.resultMsg,128,"GAUNTLET CLEARED! Champion stands alone!");
                        gs.screen=SCREEN_RESULT;
                    } else if (gs.turn >= MAX_TURNS) {
                        snprintf(gs.resultMsg,128,"Time expired. The Gauntlet is unfinished.");
                        gs.screen=SCREEN_RESULT;
                    } else {
                        gs.turn++;
                        gs.selectedMove=0;
                        int f=firstAliveEnemy(&gs);
                        if(f>=0 && gs.enemies[gs.selectedTarget].hp<=0) gs.selectedTarget=f;
                        logClear(&gs.log);
                        gs.screen=SCREEN_GAUNTLET_BATTLE;
                    }
                }
                break;

            case SCREEN_RESULT:
                if (IsKeyPressed(KEY_ONE)) {
                    char name1[32]; int c1=gs.p1.classId;
                    strncpy(name1, gs.p1.name, 31); name1[31]='\0';
                    int wasGauntlet = gs.gauntletMode;
                    if (wasGauntlet) {
                        initFighter(&gs.p1, name1, c1);
                        initGauntlet(&gs);
                        gs.screen=SCREEN_GAUNTLET_BATTLE;
                    } else {
                        char name2[32]; int c2=gs.p2.classId;
                        strncpy(name2, gs.p2.name, 31); name2[31]='\0';
                        initFighter(&gs.p1, name1, c1);
                        initFighter(&gs.p2, name2, c2);
                        gs.turn=1; gs.selectedMove=0; gs.p1chosen=0;
                        logClear(&gs.log);
                        gs.screen=SCREEN_BATTLE;
                    }
                }
                if (IsKeyPressed(KEY_TWO)) { memset(&gs,0,sizeof(gs)); gs.screen=SCREEN_MENU; }
                if (IsKeyPressed(KEY_THREE)) CloseWindow();
                break;
        }

        /* ===== DRAW ===== */
        BeginDrawing();
        ClearBackground(BLACK);

        switch (gs.screen) {
            case SCREEN_MENU:            drawMenuScreen();                      break;
            case SCREEN_SELECT_CLASS_P1: drawClassSelectScreen("Choose Class", hoverClass); break;
            case SCREEN_SELECT_CLASS_P2: drawClassSelectScreen("Player 2 - Choose Class", hoverClass); break;
            case SCREEN_SELECT_OPPONENT: drawOpponentSelectScreen(hoverClass);  break;
            case SCREEN_BATTLE:          drawBattleScreen(&gs);                 break;
            case SCREEN_RESOLVE:         drawResolveScreen(&gs);                break;
            case SCREEN_RESULT:          drawResultScreen(&gs);                 break;
            case SCREEN_GAUNTLET_BATTLE:  drawGauntletBattle(&gs);             break;
            case SCREEN_GAUNTLET_RESOLVE: drawGauntletResolve(&gs);            break;
        }

        EndDrawing();
    }

    for (int p=0;p<2;p++)
        for (int c=0;c<3;c++)
            UnloadTexture(gSprites[p][c]);
    UnloadFont(gFont);

    CloseWindow();
    return 0;
}