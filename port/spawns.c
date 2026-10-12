/* The spawn points, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * A room's spawn points (up to sixteen, at $FFA15C + 4n, only four while $FF9659 bit 4 is set)
 * make its enemies. Each frame ($12A70) every active one runs by its kind:
 *
 *    0  one enemy each time it is triggered          8  a guardian and three companions
 *    1  the same, waiting RELOAD frames between      9, 10  one enemy, once
 *    2  the same, once the last one is beaten        11  an enemy from a map block (a door?)
 *    3  near the player: a row of enemies            12  a scripted boss room (six phases)
 *    4  an item, unless its game flag is set         13  a group, from a list of offsets
 *    5  one enemy, once, placed and turned           14  waves, from a list of positions
 *    6  the room's chests                            15  one enemy when the player comes near
 *    7  one enemy, once, with a parameter
 *
 * Many are triggered: by a periodic timer ($FFA14C, four of them, OPTS bits 4-5), not while
 * the player is close, and depending on whether the point is on the screen (OPTS bits 1-2).
 * A point counts the enemies it has made, beaten and lost; at its maximum it waits until
 * they are gone, then stops. The routines this calls run on the caller's registers. */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

#define TASK_FLAGS    0xFFFF9659u   /* bit 0: spawn regardless; bit 4: only four points */
#define TIMERS        0xFFFFA14Cu   /* four periodic timers: word last time, word period (bit 15: due) */
#define FRAME         0xFFFF965Au
#define ENEMY_PLAYER  0xFFFFA11Cu   /* the player, for the enemies */
#define PLAYER_W      0xFFFF9EEEu
#define AREA          0xFFFF9668u

/* a spawn point's fields: p = 0, 4 .. $3C */
#define STATE(p)      ((p) - 0x5EA4)   /* bit 7 active; 6 waiting; 5, 4 phases; 0 triggered */
#define KIND(p)       ((p) - 0x5EA3)   /* bit 7: done (the one-shot kinds) */
#define APPEAR(p)     ((p) - 0x5EA2)   /* bit 7: the enemy appears on the screen */
#define OPTS(p)       ((p) - 0x5EA1)   /* bit 0 keep counts; 1, 2 screen rules; 3 near is fine;
                                        * 6 on a timer (bits 4-5 which) */
#define OFFSET(p)     ((p) - 0x5E54)   /* bytes: an offset, or ranges (kind 15); word: a block */
#define ENEMY(p)      ((p) - 0x5E52)   /* word: the enemy's kind */
#define X(p)          ((p) - 0x5E04)
#define Y(p)          ((p) - 0x5E02)
#define MAX(p)        ((p) - 0x5DB4)   /* bytes: how many in all (negative: no limit), made, */
#define MADE(p)       ((p) - 0x5DB3)   /* beaten, and lost off the screen */
#define BEATEN(p)     ((p) - 0x5DB2)
#define LOST(p)       ((p) - 0x5DB1)
#define RELOAD(p)     ((p) - 0x5D64)
#define TIMER(p)      ((p) - 0x5D63)
#define COUNT(p)      ((p) - 0x5D62)
#define PHASE(p)      ((p) - 0x5D61)   /* a parameter; kind 12: its phase (bit 7 once begun) */
#define DATA(p)       ((p) - 0x5D14)   /* long, or two words: a range, a list, an entity */
#define DATA2(p)      ((p) - 0x5D12)

/* an enemy's fields (e: the new one, in a2) */
#define E_ALIVE(e)    ((e) - 0x4000)
#define E_ATTR(e)     ((e) - 0x3FFE)
#define E_TILES(e)    ((e) - 0x3DFE)
#define E_SET(e)      ((e) - 0x3D00)
#define E_RATE(e)     ((e) - 0x3CFD)
#define E_X(e)        ((e) - 0x3800)
#define E_Y(e)        ((e) - 0x3700)
#define E_STEP(e)     ((e) - 0x2BFF)
#define E_TYPE(e)     ((e) - 0x2BFE)
#define E_FROM(e)     ((e) - 0x2B00)   /* word: the spawn point that made it, or -1 */
#define E_TIMER(e)    ((e) - 0x2800)
#define E_PARAM(e)    ((e) - 0x27FE)
#define E_HOME_X(e)   ((e) - 0x2100)
#define E_HOME_Y(e)   ((e) - 0x20FE)

static Cpu *c;
static uint32_t p;                    /* the spawn point */

static void call(uint32_t addr) { c->a[3] = p; call_68k(c, addr); }
static int negative(void) { return (c->ccr & CC_N) != 0; }
static uint8_t dec8(uint32_t a) { uint8_t v = (uint8_t)(rd8(a) - 1); wr8(a, v); return v; }
static Ent ent(uint32_t addr) { return (Ent)(int32_t)rdw(addr); }
static Ent new_enemy(void) { return c->a[2]; }

/* the original's endless loop when a guaranteed spawn finds no free slot */
static void hang(const char *where)
{
    fprintf(stderr, "spawns: no free enemy slot at %s: the original hangs here\n", where);
    exit(3);
}

/* $13320: the counts. 1 when the point has made all it may: the kind's routine stops there
 * (and the point ends once its enemies are all gone). Leaves d0 = enemies made. */
static int full(void)
{
    uint8_t made = rd8(MADE(p)), max = rd8(MAX(p));
    c->d[0] = set_b(c->d[0], made); c->d[1] = set_b(c->d[1], max);
    if (max & 0x80) return 0;
    if (!(rd8(OPTS(p)) & 1)) {
        uint8_t lost = rd8(LOST(p));
        c->d[2] = set_b(c->d[2], lost);
        if (lost) {
            made = (uint8_t)(made - lost);
            wr8(MADE(p), made);
            wr8(TIMER(p), 0);
            wr8(LOST(p), 0);
        }
    }
    c->d[0] = set_b(c->d[0], made);
    if (made != max) return 0;
    uint8_t left = (uint8_t)(made - rd8(BEATEN(p)) - rd8(LOST(p)));
    c->d[0] = set_b(c->d[0], left);
    if (!left) wr8(STATE(p), 0);
    return 1;
}

/* $13280: 1 if the player is too close to spawn (within $50 by $40), unless spawning anyway */
static int player_close(void)
{
    if ((rd8(TASK_FLAGS) & 1) || (rd8(OPTS(p)) & 8)) { c->d[0] = 0; return 0; }
    Ent pl = ent(ENEMY_PLAYER);
    c->a[2] = pl;
    int16_t dx = (int16_t)(rdw(X(p)) - rdw(E_X(pl)));
    if (dx < 0) dx = (int16_t)-dx;
    c->d[0] = set_w(c->d[0], (uint16_t)dx);
    if ((uint16_t)dx >= 0x50) return 0;
    int16_t dy = (int16_t)(rdw(Y(p)) - rdw(E_Y(pl)));
    if (dy < 0) dy = (int16_t)-dy;
    c->d[0] = set_w(c->d[0], (uint16_t)dy);
    return (uint16_t)dy < 0x40;
}

/* OffscreenXAt / OffscreenYAt with margin d2: 1 if off */
static int off_x(uint16_t margin) { c->d[2] = margin; c->d[0] = set_w(c->d[0], rd16(X(p))); call(0xBD0); return negative(); }
static int off_y(uint16_t margin) { c->d[2] = margin; c->d[0] = set_w(c->d[0], rd16(Y(p))); call(0xBFE); return negative(); }

/* $132BC: whether the point may spawn by where it is: -1 no, 0 yes, 1 yes and appearing on the
 * screen. OPTS bits 1-2: 6 only when off the screen; 4 only when on it */
static int screen_rule(void)
{
    uint8_t o = rd8(OPTS(p));
    int r;
    if (!off_x(0) && !off_y(0)) {                   /* on the screen */
        if ((o & 6) == 6) r = -1;
        else r = (rd8(TASK_FLAGS) & 1) ? 0 : 1;
    } else if ((o & 4) && !(o & 2)) r = -1;
    else r = off_x(64) || off_y(48) ? -1 : 0;      /* not too far off */
    c->d[0] = (uint32_t)r;
    return r;
}

/* the usual trigger: not too close, allowed where it is; APPEAR set by the screen rule */
static int may_spawn(void)
{
    wr8(APPEAR(p), 0);
    if (player_close()) return 0;
    int r = screen_rule();
    if (r < 0) return 0;
    if (r > 0) set8(APPEAR(p), 0x80);
    return 1;
}

static int find_free(void) { call(0x12812); return !negative(); }   /* FindFreeEntity: a2 */

/* $12650, $127DC: an enemy of the point's kind at x, y (the point's own d0, d1) */
static void make_enemy_at(uint16_t x, uint16_t y)
{
    c->d[0] = set_w(c->d[0], (uint16_t)p);
    c->d[1] = set_w(c->d[1], rd16(ENEMY(p)));
    c->d[6] = set_w(c->d[6], x);
    c->d[7] = set_w(c->d[7], y);
    call(0x12650);
    call(0x127DC);
}

/* $1335C: one enemy at the point. 0 if there was no free slot. */
static int spawn_one(void)
{
    if (!find_free()) return 0;
    make_enemy_at(rd16(X(p)), rd16(Y(p)));
    wr8(MADE(p), (uint8_t)(rd8(MADE(p)) + 1));
    c->d[0] = 0;
    c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    return 1;
}

/* $127A0 on the new enemy a2 (it initialises a4) */
static void init_new(void)
{
    uint32_t a4 = c->a[4];
    c->a[4] = c->a[2]; c->a[2] = a4;
    call(0x127A0);
    a4 = c->a[4]; c->a[4] = c->a[2]; c->a[2] = a4;
}

/* $13380: a thing of the point's kind (not an enemy's spawn: placed directly). 0 if no slot. */
static int place_one(void)
{
    if (!find_free()) return 0;
    Ent e = new_enemy();
    wrw(E_FROM(e), (uint16_t)p);
    wrw(E_TYPE(e), rdw(ENEMY(p)));
    wrw(E_X(e), rdw(X(p))); wrw(E_HOME_X(e), rdw(X(p)));
    wrw(E_Y(e), rdw(Y(p))); wrw(E_HOME_Y(e), rdw(Y(p)));
    init_new();
    return !negative();
}

/* ---- the kinds ---- */

static int wait_timer(void)                         /* 1 while the reload timer runs */
{
    if (rd8(TIMER(p)) && dec8(TIMER(p))) return 1;
    return 0;
}

static void kind_triggered(void)                    /* 0 */
{
    if (full()) return;
    uint8_t s = rd8(STATE(p)); wr8(STATE(p), s & ~1);
    if (!(s & 1)) return;
    if (may_spawn()) spawn_one();
}

static void kind_reload(int after_beaten)           /* 1, 2 */
{
    if (full()) return;
    if (after_beaten && (uint8_t)c->d[0] != rd8(BEATEN(p))) return;
    if (!(rd8(STATE(p)) & 1)) return;
    if (!(after_beaten && (rd8(OPTS(p)) & 0x40)) && wait_timer()) return;
    clr8(STATE(p), 1);
    if (may_spawn() && spawn_one()) wr8(TIMER(p), rd8(RELOAD(p)));
}

static void kind_row(void)                          /* 3: near the player, a row of enemies */
{
    if (full()) return;
    if (!(rd8(STATE(p)) & 1)) return;
    Ent pl = ent(ENEMY_PLAYER);
    c->a[2] = pl;
    int16_t d = (int16_t)(rdw(E_X(pl)) - rdw(X(p)));
    if (d < 0) d = (int16_t)-d;
    c->d[0] = set_w(c->d[0], (uint16_t)d);
    if ((uint16_t)d >= rd16(DATA(p))) return;
    if (wait_timer()) return;
    clr8(STATE(p), 1);
    if (!may_spawn() || !find_free()) return;
    wr8(TIMER(p), rd8(RELOAD(p)));
    make_enemy_at((uint16_t)(rdw(X(p)) + rdw(DATA2(p))), rd16(Y(p)));
    wr8(MADE(p), (uint8_t)(rd8(MADE(p)) + 1));
    int16_t step = (int8_t)rd8(PHASE(p));
    c->d[0] = (uint32_t)(uint16_t)step;
    wrw(DATA2(p), rdw(DATA2(p)) - step);              /* the next one further along */
}

static void kind_item(void)                         /* 4: an item, unless its flag is set */
{
    int have;
    if (rd8(AREA) == 0xF) {                         /* a numbered game flag */
        c->d[0] = rd8(TIMER(p));
        call(0x274C);                               /* TestGameFlag */
        have = !(c->ccr & CC_Z);
    } else {                                        /* a bit of this area's flags */
        c->d[0] = set_b(c->d[0], rd8(TIMER(p)));
        call(0x2758);                               /* GameFlagBit: a0, d1, bit d0 */
        have = (rd8(c->a[0] + (uint32_t)(int32_t)(int16_t)c->d[1]) >> (c->d[0] & 7)) & 1;
        c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (have ? 0 : CC_Z));
    }
    if (!have) {
        if (!place_one()) return;
        Ent e = new_enemy();
        wr8(c->a[4] - 0x3CFD, 0x0C);                /* the original sets a4's, not the item's */
        wr8(E_TIMER(e), rd8(TIMER(p)));
        wrw(E_PARAM(e), rdw(DATA(p)));
        wrw(E_FROM(e), 0xFFFF);
    }
    wr8(STATE(p), 0);
}

static void kind_placed(void)                       /* 5: once, offset, facing, a state */
{
    if (rd8(KIND(p)) & 0x80) return;
    if (!spawn_one()) return;
    set8(KIND(p), 0x80);
    Ent e = new_enemy();
    wrw(E_X(e), rdw(E_X(e)) + (int8_t)rd8(OFFSET(p)));
    uint8_t f = rd8(OFFSET(p) + 1);
    if (f & 0x80) {                                 /* given: attributes, state, platform */
        set8(E_ATTR(e), f & 0x7F);
        wr8(E_STEP(e), rd8(PHASE(p)) & 0x7F);
        wrw(e - 0x2D00, rdw(MAX(p)));
    } else if (rd8(E_X(e) + 1) & 1) set8(E_ATTR(e), 8);   /* odd x: faces left */
    wr8(e - 0x3200, 0);
    wr8(E_TIMER(e), rd8(TIMER(p)));
    uint32_t d = rd32(DATA(p));
    wr32(e - 0x2700, d);
    if (d) wr8(e - 0x3200, 1);
}

static void kind_chests(void)                       /* 6: the room's chests, from a list */
{
    if (rd8(KIND(p)) & 0x80) return;
    uint32_t a = rd32(DATA(p));
    do {
        c->d[0] = (uint32_t)(rd8(a) >> 1);
        c->a[0] = a;
        call(0x23E4);                               /* had its item already? */
        if (!(c->ccr & CC_Z)) { a += 6; continue; }
        if (!find_free()) hang("$12D48");
        wrw(0xFFFF9994u, rdw(0xFFFF9994u) | 0x80);
        init_new();
        Ent e = new_enemy();
        uint16_t tile = (uint16_t)(((uint8_t)(rd8(TIMER(p)) - 1)) * 4 + 0x7F0);
        wrw(E_TILES(e), tile);
        wrw(E_TYPE(e), 0x600);
        uint16_t pic = rd8(a);
        wr8(E_TIMER(e), rd8(a + 1));
        wrw(E_X(e), rdw(a + 2));
        wrw(E_Y(e), rdw(a + 4));
        a += 6;
        wrw(E_FROM(e), (uint16_t)p);
        if (pic >= 0xC0) pic = (uint16_t)(pic - 0x80);
        c->d[0] = (uint32_t)(uint16_t)(tile << 5);
        c->d[1] = rd16(0x13AD0 + pic);
        c->d[2] = 32;
        c->a[0] = a;
        call(0x22A8);                               /* CopyRawTiles: its picture */
        c->a[0] = a;
    } while (dec8(TIMER(p)));
    set8(KIND(p), 0x80);
}

static void kind_param(void)                        /* 7: once, with a parameter */
{
    if (rd8(KIND(p)) & 0x80) return;
    if (!spawn_one()) return;
    set8(KIND(p), 0x80);
    wr8(E_PARAM(new_enemy()), rd8(TIMER(p)));
}

static void kind_once(void)                         /* 9, 10 */
{
    if (rd8(KIND(p)) & 0x80) return;
    if (spawn_one()) set8(KIND(p), 0x80);
}

static void kind_near(void)                         /* 15: one enemy when the player comes near */
{
    if (full()) return;
    Ent pl = ent(PLAYER_W);
    c->a[2] = pl;
    int16_t d = (int16_t)(rdw(E_X(pl)) - rdw(DATA(p)));
    if (d < 0) d = (int16_t)-d;
    if ((uint16_t)d > rd8(OFFSET(p))) return;
    d = (int16_t)(rdw(E_Y(pl)) - rdw(DATA2(p)));
    if (d < 0) d = (int16_t)-d;
    if ((uint16_t)d > rd8(OFFSET(p) + 1)) return;
    spawn_one();
}

static void kind_guardian(void)                     /* 8: a guardian and three companions */
{
    static const int16_t at[3][2] = {{0x11C0, 0x1018}, {0x11D8, 0x1020}, {0x11F0, 0x1030}};
    if (rd8(KIND(p)) & 0x80) return;
    c->d[0] = 0;
    call(0xF670);
    if (!spawn_one()) return;
    wr8(E_PARAM(new_enemy()), rd8(TIMER(p)));
    wr8(COUNT(p), 3);
    for (int i = 0; ; i++) {
        if (!find_free()) hang("$12DFC");
        init_new();
        Ent e = new_enemy();
        wrw(E_FROM(e), (uint16_t)p);
        wrw(E_SET(e), 0x200);
        wrw(E_TYPE(e), 0x438);
        wr8(E_RATE(e), 0x10);
        wr8(E_ATTR(e), 0x80);
        wrw(E_X(e), at[i][0]); wrw(E_HOME_X(e), at[i][0]);
        wrw(E_Y(e), at[i][1]); wrw(E_HOME_Y(e), at[i][1]);
        c->a[0] = 0x12E46 + 4u * (uint32_t)(i + 1);
        if (!dec8(COUNT(p))) break;
    }
    set8(KIND(p), 0x80);
}

static void kind_block(void)                        /* 11: from a map block, when it is opened */
{
    if (full()) return;
    if (rd8(STATE(p)) & 0x40) {                     /* waiting for its block */
        uint32_t b = (uint32_t)(int32_t)rdw(OFFSET(p));
        c->a[0] = b;
        uint8_t v = rd8(b - 0x63CC); wr8(b - 0x63CC, v & ~1);
        if (!(v & 1) || !spawn_one()) return;
        wr8(TIMER(p), rd8(RELOAD(p)));
        clr8(STATE(p), 0x40);
        return;
    }
    if (wait_timer()) return;
    uint8_t s = rd8(STATE(p)); wr8(STATE(p), s & ~1);
    if (!(s & 1) || !may_spawn()) return;
    c->d[6] = set_w(c->d[6], (uint16_t)(rd16(X(p)) >> 4));
    c->d[7] = set_w(c->d[7], (uint16_t)((rd16(Y(p)) & 0xFFF0) << 2));
    c->a[0] = rd32(DATA(p));
    call(0x2974);                                   /* the block there */
    if (negative()) return;
    wrw(OFFSET(p), (uint16_t)c->a[1]);
    set8(STATE(p), 0x40);
}

/* 12: a scripted boss room, in phases (PHASE, bit 7 once begun) */
static int phase_begins(void) { uint8_t v = rd8(PHASE(p)); wr8(PHASE(p), v | 0x80); return !(v & 0x80); }

static void set_anim0(Ent g) { c->d[0] = 0; wr8(g - 0x3CFE, 0); clr8(g - 0x3FFD, 0x81); }

static void boss_set_up(void)                       /* phase 0 */
{
    static const int8_t dx[5] = {0x20, 0, -0x20, 0x10, -0x10};
    static const uint8_t attr[5] = {0, 0, 0, 8, 8};
    if (rd8(0xFFFF9F32u) & 0x80) {
        c->d[0] = 2; call(0xF670);
        c->d[0] = 2; call(0xF9C8);
        Ent a = ent(0xFFFF9F30u);
        c->a[0] = a;
        wr8(E_STEP(a), 0x0E);
        wrw(E_FROM(a), (uint16_t)p);
    }
    if (!find_free()) hang("$12F28");
    uint32_t t = c->a[4]; c->a[4] = c->a[2]; c->a[2] = t;
    call(0x127A0);
    c->d[1] = set_w(c->d[1], 0x43A);
    call(0x4910);                                   /* its graphics' tile base, at a0 */
    Ent g = c->a[4];
    wrw(E_TILES(g), rdw(c->a[0]));
    wrw(E_SET(g), 0x1608);
    wrw(E_X(g), 0x1074);
    wrw(E_Y(g), 0x1028);
    wr8(E_RATE(g), 0x1E);
    wrw(E_TYPE(g), 0xFFFF);
    set_anim0(g);
    set8(E_ALIVE(g), 0x40);
    wrw(DATA2(p), (uint16_t)g);
    t = c->a[4]; c->a[4] = c->a[2]; c->a[2] = t;
    set8(0xFFFF9F06u, 4);
    wr8(RELOAD(p), 0x1F);
    wr8(TIMER(p), 5);
    for (uint16_t n = 0; ; n++) {
        c->d[2] = set_w(c->d[2], n);
        if (!spawn_one()) hang("$12F88");
        Ent e = new_enemy();
        c->d[2] = set_w(c->d[2], n);
        wrw(E_X(e), rdw(E_X(e)) + dx[n]);
        set8(E_ATTR(e), attr[n]);
        wr8(E_PARAM(e), (uint8_t)n);
        c->d[2] = set_w(c->d[2], (uint16_t)(n + 1));
        if (!dec8(TIMER(p))) break;
    }
    wr8(PHASE(p), 2);
    wr8(MADE(p), 5);
}

static void boss_wait_player(void)                  /* phase 2: until the player is below $1040 */
{
    Ent pl = ent(PLAYER_W);
    c->a[2] = pl;
    uint16_t feet = (uint16_t)(rd8(pl - 0x34FD) + rd16(E_Y(pl)));
    c->d[0] = feet;
    if (feet <= 0x1040) return;
    c->a[2] = 0x13001;
    call(0x95FE);
    wr8(PHASE(p), 4);
    wr8(0xFFFF9660u, 13);
    wr8(0xFFFF9661u, 13);
    c->d[0] = 13;
    call(0x366);                                    /* the boss music */
}

static void boss_count_down(void)                   /* phase 4 */
{
    if (phase_begins()) {
        wr8(TIMER(p), 0);
        wr8(COUNT(p), 4);
        clr8(STATE(p), 0x40);
    }
    if (rd8(TIMER(p))) { wr8(TIMER(p), (uint8_t)(rd8(TIMER(p)) - 1)); return; }
    uint8_t s = rd8(STATE(p)); wr8(STATE(p), s | 0x40);
    if (s & 0x40) return;
    wr8(TIMER(p), 8);
    c->d[0] = (uint8_t)(4 - rd8(COUNT(p)));
    wrw(DATA(p), (uint16_t)c->d[0]);
    if ((int8_t)dec8(COUNT(p)) < 0) wr8(PHASE(p), 6);
}

static void boss_release(void)                      /* phase 6 */
{
    if (phase_begins()) {
        clr8(0xFFFF9F06u, 4);
        wrw(DATA(p), 0xFFFF);
        wr8(TIMER(p), 0);
        wr8(COUNT(p), 0);
    }
    if (rd8(BEATEN(p)) >= 3) { wr8(PHASE(p), 8); return; }
    if (rd8(TIMER(p))) { wr8(TIMER(p), (uint8_t)(rd8(TIMER(p)) - 1)); return; }
    if (rd8(COUNT(p)) >= 3 || (rd8(STATE(p)) & 0x40)) return;
    uint16_t n = (uint16_t)(rd16(DATA(p)) + 1);
    if (n > 4) n = 0;
    c->d[0] = n;
    if ((rd8(RELOAD(p)) >> (n & 7)) & 1) {
        set8(STATE(p), 0x40);
        wr8(TIMER(p), 8);
        wr8(COUNT(p), (uint8_t)(rd8(COUNT(p)) + 1));
    }
    wrw(DATA(p), n);
}

static void boss_all_beaten(void)                   /* phase 8 */
{
    if (phase_begins()) set8(STATE(p), 0x20);
    uint8_t beaten = rd8(BEATEN(p));
    c->d[0] = set_b(c->d[0], beaten);
    if (beaten < rd8(MADE(p))) return;
    wr8(PHASE(p), 10);
    wrw(0xFFFF9F8Eu, 0);
    c->d[0] = 56;
    call(0x2752);                                   /* SetGameFlag */
}

static void boss_end(void)                          /* phase 10 */
{
    if (phase_begins()) {
        Ent g = ent(DATA2(p));
        set8(g - 0x3FFD, 8);
        wrw(E_SET(g), 0x1206);
        wrw(E_X(g), 0x1074);
        wrw(E_Y(g), 0x1020);
        wrw(E_TYPE(g), 0xFFFF);
        set_anim0(g);
        set8(E_ALIVE(g), 0x40);
        c->a[2] = g;                                /* exg a2,a4; a4 = g; .. exg a4,a2 */
        return;
    }
    if (!(rd8(STATE(p)) & 0x10)) {
        Ent g = ent(DATA2(p));
        c->a[2] = g;
        if (rd8(g - 0x3A00)) return;
        wrw(E_ALIVE(g), 0);
        set8(STATE(p), 0x10);
        return;
    }
    Ent a = ent(0xFFFF9F30u);
    c->a[2] = a;
    if (rd8(E_ALIVE(a)) & 1) return;
    wrw(E_ALIVE(a), 0);
    wrw(STATE(p), 0);
    wr8(0xFFFF9F32u, 0xFF);
}

static void kind_boss(void)
{
    uint8_t ph = rd8(PHASE(p)) & 0x7F;
    c->d[0] = ph;
    switch (ph) {
    case 0: boss_set_up(); break;
    case 2: boss_wait_player(); break;
    case 4: boss_count_down(); break;
    case 6: boss_release(); break;
    case 8: boss_all_beaten(); break;
    case 10: boss_end(); break;
    default:
        fprintf(stderr, "spawns: boss phase %d is outside the original's table ($12EF4)\n", ph);
        exit(3);
    }
}

static void kind_group(void)                        /* 13: a group, from a list of offsets */
{
    if (full()) return;
    uint8_t s = rd8(STATE(p)); wr8(STATE(p), s & ~1);
    if (!(s & 1) || !may_spawn()) return;
    do {
        if (!find_free()) return;
        uint32_t a = rd32(DATA(p));
        c->a[0] = a + 2;
        uint16_t x = (uint16_t)(((int16_t)(int8_t)rd8(a) << 2) + rdw(X(p)));
        uint16_t y = (uint16_t)(((int16_t)(int8_t)rd8(a + 1) << 2) + rdw(Y(p)));
        wr32(DATA(p), a + 2);
        make_enemy_at(x, y);
        uint8_t made = (uint8_t)(rd8(MADE(p)) + 1);
        wr8(MADE(p), made);
        c->d[0] = set_b(c->d[0], made);
    } while (rd8(MADE(p)) < rd8(MAX(p)));
}

static void kind_waves(void)                        /* 14: waves, once each is beaten */
{
    uint8_t left = (uint8_t)(rd8(MADE(p)) - rd8(BEATEN(p)));
    c->d[0] = set_b(c->d[0], left);
    if (left) return;
    wr8(MADE(p), 0);
    wr8(BEATEN(p), 0);
    if (dec8(RELOAD(p))) return;
    uint32_t a = rd32(DATA(p));
    uint8_t n;
    while (!(n = rd8(a++)))                         /* 0: a jump in the list */
        a += (uint32_t)(int32_t)(int16_t)(rd8(a) << 8 | rd8(a + 1));
    wr8(RELOAD(p), n);                              /* frames to the next wave */
    for (;;) {
        uint8_t hi = rd8(a++);
        if (hi & 0x80) break;
        uint16_t x = (uint16_t)(hi << 8 | rd8(a++));
        uint16_t y = (uint16_t)(rd8(a) << 8 | rd8(a + 1)); a += 2;
        c->a[0] = a;
        if (!find_free()) {                         /* no room: the rest of this wave dropped */
            a += 2;
            while (!(rd8(a++) & 0x80)) a += 3;
            break;
        }
        make_enemy_at(x, y);
        Ent e = new_enemy();
        wrw(e - 0x32FE, rd8(a) << 4);
        wr8(E_TIMER(e), rd8(a + 1));
        a += 2;
        wr8(MADE(p), (uint8_t)(rd8(MADE(p)) + 1));
    }
    c->a[0] = a;
    wr32(DATA(p), a);
}

/* $12A70: every active spawn point, then the periodic timers that came due are restarted */
void RunSpawnPoints(Cpu *cpu)
{
    c = cpu;
    uint32_t first = 0, n = 16;
    if (rd8(TASK_FLAGS) & 0x10) { first = 0x40; n = 4; }
    for (p = first; n--; p += 4) {
        c->a[3] = p;
        if (!(rd8(STATE(p)) & 0x80)) continue;
        uint8_t s = rd8(STATE(p));
        wr8(STATE(p), s | 1);
        if (!(s & 1) && (rd8(OPTS(p)) & 0x40)) {   /* on a timer: triggered when it is due */
            uint32_t t = TIMERS + ((rd8(OPTS(p)) & 0x30) >> 2);
            int16_t since = (int16_t)(rdw(t) - rdw(FRAME));
            if (since < 0) since = (int16_t)-since;
            clr8(STATE(p), 1);
            if ((uint16_t)since >= (rd16(t + 2) & 0x7FFF)) {
                set8(STATE(p), 1);
                wrw(t + 2, rdw(t + 2) | 0x8000);
            }
        }
        switch (rd8(KIND(p)) & 0x7F) {
        case 0: kind_triggered(); break;
        case 1: kind_reload(0); break;
        case 2: kind_reload(1); break;
        case 3: kind_row(); break;
        case 4: kind_item(); break;
        case 5: kind_placed(); break;
        case 6: kind_chests(); break;
        case 7: kind_param(); break;
        case 8: kind_guardian(); break;
        case 9: case 10: kind_once(); break;
        case 11: kind_block(); break;
        case 12: kind_boss(); break;
        case 13: kind_group(); break;
        case 14: kind_waves(); break;
        case 15: kind_near(); break;
        default:
            fprintf(stderr, "spawns: kind %d is outside the original's table ($12B12)\n", rd8(KIND(p)) & 0x7F);
            exit(3);
        }
    }
    for (uint32_t t = TIMERS; t < TIMERS + 16; t += 4) {
        uint8_t f = rd8(t + 2);
        wr8(t + 2, f & 0x7F);
        if (f & 0x80) wrw(t, rdw(FRAME));
    }
    c->ret = 0;                                     /* call_68k on these registers left its mark */
}
