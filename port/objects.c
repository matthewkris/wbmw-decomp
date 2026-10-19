/* The enemies' objects, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The enemies").
 *
 * Thirteen of the enemy handlers ($1A464-$1AA3B), run by RunEnemies (enemies.c) through the
 * handler table at $13422. What the objects are is read from what they do; group 2's records
 * are not decoded yet (docs/NOTES.md):
 *
 *   handler  kinds            what it does
 *    6 $1A464  $208           thrown in an arc, falls, bursts on the floor or a touch
 *    7 $1A4D2  $20A $20C $22A thrown at its own speed, gone on any touch or collision
 *   27 $1A538  $226           stands still, touching what it touches
 *   18 $1A55A  $214           flies ahead accelerating for 72 frames (counted in $FFA10E)
 *   22 $1A5B0  $21C           walks forward, then falls off the screen when hit or blocked
 *   30 $1A62C  $212 $21E $230 walks forward along the floor until a wall or a touch
 *   32 $1A678  $232           the same, clearing the word at -$3DFE first
 *   29 $1A686  $22C           aimed at the entity in $FFA11C, with a random spread
 *   31 $1A6EE  $22E           leaves at an angle from TIMER ($1A780), bounces back off a touch
 *    4 $1A7B8  $802           hops along the floor, turning at walls (sound $39)
 *    5 $1A84E  $804           floats up and down 16 pixels about where it started
 *    2 $1A8BA  $806           a column that rises and sinks between its home and $30 away
 *    3 $1A99A  $808           crawls along a floor until it ends, then drops
 *
 * (Kinds from the tables at $13418: the kind word's low byte / 2 picks the handler.)
 *
 * Common to most: STEP bit 7 (or ALIVE bit 6) marks the first frame; an object that leaves the
 * screen is removed and counted lost at its spawn point (LINK: -$5DB1), one that ends is removed
 * and counted beaten (-$5DB2).
 *
 * The routines this calls keep running on the caller's registers (`c` below), as the original
 * does: what one enemy's code leaves in a register is what the next one's sees, and some calls
 * read registers set before them (a2, d0, d3, d4, d6, d7). */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

#define COUNT_A10E    0xFFFFA10Eu   /* objects of kind $214 alive */
#define TARGET_W      0xFFFFA11Cu   /* word: the entity they hit and aim at */
#define PLAYER_W      0xFFFF9EEEu
#define TILE_CTX      0xFFFF9754u   /* a3 for TileAttrAt: plane A */
#define HOP_ANGLES    0x1A780u      /* bytes: $E0, $00, $20, $00 (indexed by TIMER: any byte) */

#define ALIVE(e)      ((e) - 0x4000)   /* bit 7 in use; 6 drawn; 0 on screen. Word: with ATTR's neighbour */
#define ATTR(e)       ((e) - 0x3FFE)   /* bit 3 facing left; 4, 7 */
#define WORD_3DFE(e)  ((e) - 0x3DFE)
#define ANIM_ID(e)    ((e) - 0x3CFE)
#define ANIM_RATE(e)  ((e) - 0x3CFD)
#define ANIM_BUSY(e)  ((e) - 0x3A00)
#define X(e)          ((e) - 0x3800)
#define Y(e)          ((e) - 0x3700)
#define Y_LO(e)       ((e) - 0x36FF)   /* bit 0 of y: the spawner's direction flag */
#define VEL_X(e)      ((e) - 0x3600)
#define VEL_Y(e)      ((e) - 0x35FE)
#define HALF_H(e)     ((e) - 0x34FD)
#define ACCEL_X(e)    ((e) - 0x3400)
#define ACCEL_Y(e)    ((e) - 0x33FE)
#define SPEED_X(e)    ((e) - 0x3300)
#define SPEED_Y(e)    ((e) - 0x32FE)
#define NO_COUNT(e)   ((e) - 0x3200)   /* bit 0: not counted lost */
#define COLL(e)       ((e) - 0x3100)
#define MOVE_FLAGS(e) ((e) - 0x2F00)
#define PREV_X(e)     ((e) - 0x2E00)
#define START_COL(e)  ((e) - 0x2CFE)   /* word: the column it started in (x / 16) */
#define STEP(e)       ((e) - 0x2BFF)   /* bit 7 once begun */
#define LINK(e)       ((e) - 0x2B00)   /* word: its spawn point, or negative */
#define TOUCH(e)      ((e) - 0x2AFD)
#define TIMER(e)      ((e) - 0x2800)
#define TIMER2(e)     ((e) - 0x27FF)
#define VAR(e)        ((e) - 0x27FE)   /* byte: falling; or a word */
#define HOME_Y(e)     ((e) - 0x20FE)

/* the spawn point's counters, from its LINK */
#define BEATEN(p)     ((p) - 0x5DB2)
#define LOST(p)       ((p) - 0x5DB1)

/* routines still called as they are */
#define QUEUE_SOUND     0x366u
#define RANDOM          0x5D8u
#define SET_ANIM        0x7E8u
#define SET_ANIM_NOW    0x7FCu
#define APPLY_VEL       0xAB4u
#define APPLY_VEL_X     0xAB8u
#define APPLY_VEL_Y     0xAD6u
#define ACCEL_X_        0xAF4u
#define ACCEL_XY        0xB42u
#define ACCEL_Y_        0xB44u
#define OFFSCREEN_X_AT  0xBD0u
#define OFFSCREEN       0xBEEu
#define OFFSCREEN_Y_AT  0xBFEu
#define BODY_VS_HURT    0xD2Cu
#define BODY_VS_ATTACK  0xD76u
#define CHECK_BODY      0xE64u
#define VEL_AT_COARSE   0xFC0u
#define DIRECTION_TO    0x108Cu
#define SET_VEL_AT      0x1208u
#define MOVE_ON_SLOPES  0x2EDEu
#define TYPE_AT_PIXEL   0x30CEu
#define TILE_ATTR_AT    0x30DAu
#define CHECK_GROUNDED  0x4362u
#define WALK_FORWARD    0x4492u
#define FACE_PLAYER     0x44D6u
#define ACCEL_FACING    0x46C6u
#define ENEMY_MOVE      0x46DAu
#define PLAYER_CONTACT  0x47AEu   /* the player (or the shield) against this enemy: TOUCH */
#define DESPAWN         0x4814u
#define OUT_OF_MAP      0x1211Eu  /* nz: past the map's edges */

static Cpu *c;                        /* the running registers */

static void call(uint32_t addr) { Cpu *self = c; call_68k(c, addr); c = self; }   /* c kept: see port/rooms.c */
static void call_d0(uint32_t addr, uint32_t d0) { c->d[0] = d0; call_68k(c, addr); }   /* moveq #d0 */
static int  zero(void) { return (c->ccr & CC_Z) != 0; }
static void set_d0w(int v) { c->d[0] = set_w(c->d[0], (uint32_t)v); }
static void set_d0b(uint8_t v) { c->d[0] = set_b(c->d[0], v); }
static void a2_from(uint32_t w) { c->a[2] = (uint32_t)(int32_t)rdw(w); }   /* movea.w (w).w,a2 */

/* the first frame: 1 the first time (STEP bit 7, or ALIVE bit 6 for some) */
static int begins(Ent e) { uint8_t s = rd8(STEP(e)); wr8(STEP(e), s | 0x80); return !(s & 0x80); }
static int shows(Ent e)  { uint8_t a = rd8(ALIVE(e)); wr8(ALIVE(e), a | 0x40); return !(a & 0x40); }
static void dec8(uint32_t a) { wr8(a, (uint8_t)(rd8(a) - 1)); }
static int  dec8_zero(uint32_t a) { dec8(a); return rd8(a) == 0; }
static void slow_anim(Ent e) { wr8(ANIM_RATE(e), (uint8_t)(rd8(ANIM_RATE(e)) - 2)); }   /* subq.b #2 */
static void neg_word(uint32_t a) { wrw(a, -rdw(a)); }
static int  facing_left(Ent e) { return (rd8(ATTR(e)) & 8) != 0; }

/* $1A784: removed, counted beaten at its spawn point */
static void finish(Ent e)
{
    wrw(ALIVE(e), 0);
    int16_t p = rdw(LINK(e));
    set_d0w(p);
    if (p < 0) return;
    c->a[0] = (uint32_t)(int32_t)p;
    wr8(BEATEN(c->a[0]), (uint8_t)(rd8(BEATEN(c->a[0])) + 1));
}

/* $1A796: off the screen: removed and counted lost (unless NO_COUNT), and the handler ends */
static int gone(Ent e)
{
    call(OFFSCREEN);
    if (zero()) return 0;
    if (!(rd8(NO_COUNT(e)) & 1)) {
        int16_t p = rdw(LINK(e));
        set_d0w(p);
        if (p >= 0) {
            c->a[0] = (uint32_t)(int32_t)p;
            wr8(LOST(c->a[0]), (uint8_t)(rd8(LOST(c->a[0])) + 1));
        }
    }
    wr8(ALIVE(e), 0);
    return 1;
}

/* $48C4, DespawnIfOffscreen, which returns past its caller when it removes the enemy: here it
 * answers 1 and the handler returns. Kept off by being on the screen, NO_COUNT, or $FF966B bit
 * 5; NO_COUNT's bits 1-2 choose the test (0 or both: 64 pixels sideways, 48 up and down, by the
 * position alone; else the enemy's own size). */
static int despawned_offscreen(Ent e)
{
    if (rd8(ALIVE(e)) & 1) return 0;
    if (rd8(NO_COUNT(e)) & 1) return 0;
    if (rd8(0xFFFF966Bu) & 0x20) return 0;
    uint8_t how = rd8(NO_COUNT(e)) & 6;
    c->d[0] = how;
    if (how == 0 || how == 6) {
        c->d[0] = set_b(c->d[0], 0);                /* subq.b #6 (or the beq): d0 = 0 */
        c->d[2] = 64; set_d0w(rdw(X(e)));
        call(OFFSCREEN_X_AT);
        if (!(c->ccr & CC_N)) {
            c->d[2] = 48; set_d0w(rdw(Y(e)));
            call(OFFSCREEN_Y_AT);
            if (!(c->ccr & CC_N)) return 0;
        }
    } else {
        c->d[0] = set_b(c->d[0], (uint8_t)(how - 6));
        call(OFFSCREEN);
        if (zero()) return 0;
    }
    call(DESPAWN);
    return 1;
}

/* the same for the other files (world.h): 1 if it removed the enemy (the caller returns) */
int world_despawn_offscreen(Cpu *cpu)
{
    Cpu *outer = c;
    c = cpu;
    int gone = despawned_offscreen((Ent)cpu->a[4]);
    c = outer;
    return gone;
}

/* $1A518: launched at (vx, vy) with its first animation; 180 frames to live */
static void launch(Ent e, int vx, int vy)
{
    slow_anim(e);
    wrw(VEL_X(e), vx);
    wrw(VEL_Y(e), vy);
    call_d0(SET_ANIM_NOW, 0);
    set8(ALIVE(e), 0x40);
    wr8(TIMER(e), 0xB4);
}

/* $1A610: knocked off, falling until it leaves the screen or TIMER runs out */
static void falling(Ent e)
{
    if (!(rd8(ALIVE(e)) & 1)) { finish(e); return; }
    call(ACCEL_Y_);
    call(APPLY_VEL);
    if (dec8_zero(TIMER(e))) finish(e);
}

/* knocked away: stops, turned over, falls for 16 frames */
static void knocked(Ent e, uint8_t how)
{
    set8(ATTR(e), 0x80);
    wrw(VEL_X(e), 0);
    c->d[0] = 2;
    wr8(VAR(e), how);
    wr8(TIMER(e), 0x10);
    call(SET_ANIM);
}

/* the collision type at the enemy's position (d6, d7: x, y): 1 if solid */
static int in_wall(Ent e)
{
    c->d[6] = set_w(c->d[6], (uint32_t)rdw(X(e)));
    c->d[7] = set_w(c->d[7], (uint32_t)rdw(Y(e)));
    call(TYPE_AT_PIXEL);
    c->d[2] = set_w(c->d[2], c->d[2] & 0xF);
    return (c->d[2] & 0xF) != 0;
}

/* ---- the handlers ---- */

/* handler 6, $1A464: thrown in an arc; bursts (animation 2) on the floor, a wall or a touch */
static void lobbed(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 3);
        wr8(TIMER2(e), 0);
        int vx = 0x200;
        set_d0w(vx);
        if (facing_left(e)) { vx = -vx; set_d0w(vx); }
        c->d[1] = set_w(c->d[1], 0xFE00);
        launch(e, vx, -0x200);
        return;
    }
    if (dec8_zero(TIMER(e))) { finish(e); return; }
    if (gone(e)) return;
    if (rd8(TIMER2(e))) {                           /* bursting: until the animation ends */
        if (!rd8(ANIM_BUSY(e))) finish(e);
        return;
    }
    call(PLAYER_CONTACT);
    if (!rd8(TOUCH(e))) {
        call(ACCEL_Y_);
        call(MOVE_ON_SLOPES);
        uint8_t hit = (uint8_t)((rd8(COLL(e)) & 0x41) - 1);   /* only the floor */
        c->d[0] = hit;
        if (hit) return;
    }
    c->d[0] = 2;
    wr8(TIMER2(e), 2);
    wr8(TIMER(e), 2);
    call(SET_ANIM);
}

/* handler 7, $1A4D2: thrown at its own speed (up), gone on any touch or collision */
static void thrown(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 3);
        int vx = rdw(SPEED_X(e));
        set_d0w(vx);
        if (facing_left(e)) { vx = -vx; set_d0w(vx); }
        int vy = -rdw(SPEED_Y(e));
        c->d[1] = set_w(c->d[1], (uint32_t)vy);
        launch(e, vx, vy);
        return;
    }
    if (gone(e)) return;
    call(PLAYER_CONTACT);
    if (rd8(TOUCH(e))) { finish(e); return; }
    call(ACCEL_Y_);
    call(MOVE_ON_SLOPES);
    if (rd8(COLL(e))) finish(e);
}

/* handler 27, $1A538: stands still, touching what it touches */
static void standing(Ent e)
{
    if (begins(e)) {
        slow_anim(e);
        call_d0(SET_ANIM, 0);
        set8(ALIVE(e), 0x40);
    }
    a2_from(TARGET_W);
    call(CHECK_BODY);
}

/* handler 18, $1A55A: flies ahead, accelerating, for 72 frames or until a touch */
static void flying(Ent e)
{
    if (begins(e)) {
        wr8(COUNT_A10E, (uint8_t)(rd8(COUNT_A10E) + 1));
        if (facing_left(e)) neg_word(ACCEL_X(e));
        wr8(ANIM_RATE(e), 8);
        wr8(MOVE_FLAGS(e), 3);
        set8(ALIVE(e), 0x40);
        wr8(TIMER(e), 0x48);
        call_d0(SET_ANIM, 0);
        return;
    }
    call(ACCEL_X_);
    call(PLAYER_CONTACT);
    if (!rd8(TOUCH(e)) && !dec8_zero(TIMER(e))) { call(APPLY_VEL); return; }
    wr8(COUNT_A10E, (uint8_t)(rd8(COUNT_A10E) - 1));
    finish(e);
}

/* handler 22, $1A5B0: walks forward; a touch or a wall knocks it off */
static void walker(Ent e)
{
    if (begins(e)) {
        slow_anim(e);
        wr8(MOVE_FLAGS(e), 3);
        c->d[0] = 0;
        wr8(VAR(e), 0);
        call(SET_ANIM);
        set8(ALIVE(e), 0x40);
        call(WALK_FORWARD);
        return;
    }
    if (gone(e)) return;
    if (rd8(VAR(e))) { falling(e); return; }
    call(PLAYER_CONTACT);
    uint8_t why = rd8(TOUCH(e));
    set_d0b(why);
    if (!why) {
        call(MOVE_ON_SLOPES);
        why = rd8(COLL(e));
        set_d0b(why);
        if (!why) return;
    }
    knocked(e, why);
}

/* handlers 30 and 32, $1A62C and $1A678: walks along the floor until a touch or a wall */
static void crawler_start(Ent e)
{
    slow_anim(e);
    wr8(MOVE_FLAGS(e), 3);
    call_d0(SET_ANIM, 0);
    set8(ALIVE(e), 0x40);
    call(WALK_FORWARD);
}

static void crawler_frame(Ent e)
{
    if (gone(e)) return;
    call(PLAYER_CONTACT);
    if (rd8(TOUCH(e))) { finish(e); return; }
    call(APPLY_VEL_X);
    if (in_wall(e)) finish(e);
}

static void crawler(Ent e)
{
    if (begins(e)) crawler_start(e); else crawler_frame(e);
}

static void crawler_still(Ent e)
{
    if (!begins(e)) { crawler_frame(e); return; }
    wrw(WORD_3DFE(e), 0);
    crawler_start(e);
}

/* handler 29, $1A686: aimed at the target entity, give or take a random angle */
static void aimed(Ent e)
{
    if (begins(e)) {
        slow_anim(e);
        set8(ALIVE(e), 0x40);
        a2_from(TARGET_W);
        call(DIRECTION_TO);                         /* d3: the angle */
        call(RANDOM);
        c->d[0] = set_w(c->d[0], ((c->d[0] & 0xFFF) - 0x800) & 0xFFFF);
        c->d[3] = set_w(c->d[3], (c->d[3] + c->d[0]) & 0xFFFF);
        c->d[4] = set_w(c->d[4], (uint32_t)rdw(SPEED_X(e)));
        call(SET_VEL_AT);
        call_d0(SET_ANIM, 0);
        return;
    }
    wr8(TOUCH(e), 0);
    if (gone(e)) return;
    a2_from(TARGET_W);
    call(BODY_VS_HURT);
    if (!zero()) { finish(e); return; }
    call(BODY_VS_ATTACK);
    call(CHECK_BODY);
    call(APPLY_VEL);
    uint8_t t = rd8(TOUCH(e));
    set_d0b(t);
    if (t || !(rd8(ALIVE(e)) & 1)) finish(e);        /* btst d0 (0 here): bit 0, on screen */
}

/* handler 31, $1A6EE: leaves at an angle from TIMER's entry in $1A780, bounces back off a touch */
static void bouncer(Ent e)
{
    if (shows(e)) {
        wr8(VAR(e), 0);
        uint8_t a = rd8(HOP_ANGLES + rd8(TIMER(e)));
        uint8_t att = rd8(ATTR(e));
        wr8(ATTR(e), att & (uint8_t)~8);
        if (att & 8) a = (uint8_t)(a + 0x80);
        c->d[3] = a;
        c->d[0] = set_b(c->d[0], a);
        set_d0w((c->d[0] & 0xFFFF) >> 4);
        call(SET_ANIM);
        c->d[3] = set_w(c->d[3], (c->d[3] << 8) & 0xFFFF);
        c->d[4] = set_w(c->d[4], (uint32_t)rdw(SPEED_X(e)));
        call(VEL_AT_COARSE);
        wrw(VEL_X(e), (int)(c->d[0] & 0xFFFF));
        wrw(VEL_Y(e), (int)(c->d[5] & 0xFFFF));
        return;
    }
    if (rd8(VAR(e))) { falling(e); return; }
    if (gone(e)) return;
    call(PLAYER_CONTACT);
    if (rd8(TOUCH(e))) {
        wrw(VEL_X(e), -(rdw(VEL_X(e)) >> 1));       /* asr.w, neg.w: half, back */
        wrw(ACCEL_Y(e), 0x40);
        wrw(VEL_Y(e), -0x100);
        wr8(TIMER(e), 0x20);
        c->d[0] = 16;
        wr8(VAR(e), 16);
        call(SET_ANIM);
        return;
    }
    call(APPLY_VEL);
    if (in_wall(e)) finish(e);
}

/* handler 4, $1A7B8: hops along the floor, turning at walls, with a sound on each landing */
static void hopper(Ent e)
{
    if (begins(e)) {
        wrw(VAR(e), rdw(ACCEL_X(e)));               /* its walking acceleration, for later */
        c->d[0] = 0;
        wrw(ACCEL_X(e), 0);
        call(SET_ANIM_NOW);
        set8(ALIVE(e), 0x40);
        set_d0b(rd8(HALF_H(e)));                    /* sub.w d0: with what SetAnimNow left above */
        wrw(Y(e), rdw(Y(e)) - (int)(c->d[0] & 0xFFFF));
        call(FACE_PLAYER);
        wr8(TIMER(e), 1);
        wr8(MOVE_FLAGS(e), 0x13);
    }
    call(OUT_OF_MAP);
    if (!zero()) { call(DESPAWN); return; }
    a2_from(PLAYER_W);
    call(CHECK_BODY);
    call(ACCEL_XY);
    call(ENEMY_MOVE);
    call(CHECK_GROUNDED);
    if (!zero()) wr8(TIMER(e), 0);                  /* CheckGrounded nz: the next landing sounds */
    else {
        if (rdw(ACCEL_X(e)) == 0) {                 /* walk again, as at the start */
            set_d0w(rdw(VAR(e)));
            call(ACCEL_FACING);
        }
        uint8_t t = rd8(TIMER(e));
        wr8(TIMER(e), t | 1);
        if (!(t & 1)) call_d0(QUEUE_SOUND, 0x39);
    }
    uint8_t wall = (uint8_t)((rd8(COLL(e)) & 0x29) - 1);
    c->d[0] = wall;
    if (wall) return;
    wr8(ATTR(e), rd8(ATTR(e)) ^ 8);                 /* a wall: turn round */
    neg_word(ACCEL_X(e));
    call_d0(QUEUE_SOUND, 0x39);
}

/* handler 5, $1A84E: floats up and down 16 pixels about where it started */
static void floater(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        c->d[2] = 64;
        call(RANDOM);
        c->d[0] = set_b(c->d[0], c->d[0] & 2);
        if (c->d[0] & 2) c->d[2] = set_w(c->d[2], (uint32_t)-64);
        wrw(VEL_Y(e), (int)(c->d[2] & 0xFFFF));
        call_d0(SET_ANIM, 0);
        wrw(VAR(e), rdw(Y(e)));
        set8(ALIVE(e), 0x40);
    }
    if (despawned_offscreen(e)) return;
    a2_from(PLAYER_W);
    call(CHECK_BODY);
    wrw(PREV_X(e), rdw(X(e)));
    call(APPLY_VEL_Y);
    uint16_t y = (uint16_t)rdw(Y(e)), home = (uint16_t)rdw(VAR(e));
    int turn;
    if (rdw(VEL_Y(e)) >= 0) { y = (uint16_t)(y - 0x10); turn = y > home; }
    else                    { y = (uint16_t)(y + 0x10); turn = y < home; }
    set_d0w(y);
    if (turn) neg_word(VEL_Y(e));
}

/* handler 2, $1A8BA: a column that rises and sinks between HOME_Y and $30 from it, pausing at
 * each end (TIMER frames); its animation follows its height */
static void column(Ent e)
{
    if (shows(e)) {
        wr8(TIMER2(e), 0);
        set8(ATTR(e), 0x10);
        int v = rdw(SPEED_Y(e));
        uint8_t lo = rd8(Y_LO(e));
        wr8(Y_LO(e), lo & (uint8_t)~1);
        if (!(lo & 1)) { clr8(ATTR(e), 0x10); v = -v; }
        set_d0w(v);
        wrw(VEL_Y(e), v);
        wrw(HOME_Y(e), rdw(Y(e)));
        call_d0(SET_ANIM, 0);
        return;
    }
    wr8(TOUCH(e), 0);
    a2_from(TARGET_W);
    call(CHECK_BODY);
    if (rd8(TIMER2(e))) { dec8(TIMER2(e)); return; }
    call(APPLY_VEL_Y);
    uint16_t lim = (uint16_t)rdw(HOME_Y(e)), y = (uint16_t)rdw(Y(e));
    int down = rdw(VEL_Y(e)) >= 0;
    enum { PAST, AT_END, CHECK } r;
    if (!(rd8(ATTR(e)) & 0x10)) {                   /* starting downwards: the home is the top */
        if (down) r = lim >= y ? CHECK : PAST;
        else { lim = (uint16_t)(lim - 0x30); r = lim < y ? CHECK : AT_END; }
    } else {
        if (down) { lim = (uint16_t)(lim + 0x30); r = lim >= y ? CHECK : AT_END; }
        else r = lim < y ? CHECK : PAST;
    }
    set_d0w(lim);
    if (r == PAST) r = rd8(ANIM_ID(e)) ? CHECK : PAST;
    if (r == PAST) { finish(e); return; }
    if (r == AT_END) {
        wrw(Y(e), (int16_t)lim);
        wr8(TIMER2(e), rd8(TIMER(e)));
        neg_word(VEL_Y(e));
        return;
    }
    /* its animation, by how far it is from home: 0, 2, 4 or 6, keeping its foot where it is */
    uint16_t d = (uint16_t)(rdw(Y(e)) - rdw(HOME_Y(e)));
    if (d & 0x8000) d = (uint16_t)-d;
    d = (uint16_t)((d & 0xFF00) | (uint8_t)(d + rd8(HALF_H(e))));
    d >>= 3;
    set_d0w(d);
    if (d > 3) return;
    d = (uint16_t)(d * 2);
    set_d0w(d);
    if ((uint8_t)d == rd8(ANIM_ID(e))) return;
    c->d[3] = set_b(c->d[3], rd8(HALF_H(e)));
    call(SET_ANIM_NOW);
    c->d[3] = set_b(c->d[3], (uint8_t)(c->d[3] - rd8(HALF_H(e))));
    c->d[3] = ext_w(c->d[3]);
    wrw(Y(e), rdw(Y(e)) + (int16_t)(c->d[3] & 0xFFFF));
}

/* handler 3, $1A99A: crawls along a floor until the floor ends (or a touch), then drops */
static void edge_crawler(Ent e)
{
    if (shows(e)) {
        set_d0w((uint16_t)rdw(X(e)) >> 4);
        wrw(START_COL(e), (int)(c->d[0] & 0xFFFF));
        int v = rdw(SPEED_X(e));
        uint8_t lo = rd8(Y_LO(e));
        wr8(Y_LO(e), lo & (uint8_t)~1);
        if (lo & 1) { v = -v; set8(ATTR(e), 8); }
        set_d0w(v);
        wrw(VEL_X(e), v);
        wr8(TIMER(e), 0x3C);
        c->d[0] = 0;
        wr8(VAR(e), 0);
        call(SET_ANIM);
        return;
    }
    if (rd8(VAR(e))) { falling(e); return; }
    if (dec8_zero(TIMER(e))) { finish(e); return; }
    wr8(TOUCH(e), 0);
    call(PLAYER_CONTACT);
    if (rd8(TOUCH(e))) { knocked(e, 2); return; }
    call(APPLY_VEL_X);
    uint16_t col = (uint16_t)rdw(X(e)) >> 4;
    c->d[6] = set_w(c->d[6], col);
    c->d[7] = set_w(c->d[7], (uint16_t)(((uint16_t)rdw(Y(e)) & 0xFFF0) << 2));
    if (col == (uint16_t)rdw(START_COL(e))) return;
    c->a[3] = TILE_CTX;
    call(TILE_ATTR_AT);
    c->d[2] = set_w(c->d[2], c->d[2] & 8);
    if (!(c->d[2] & 8)) return;
    wrw(X(e), rdw(PREV_X(e)));                      /* the floor ends: back, and drop */
    knocked(e, 2);
}

/* the handlers, registered by their addresses (registry.c); call_68k leaves its mark in ret */
#define HANDLER(name, fn) void name(Cpu *cpu) { c = cpu; fn((Ent)c->a[4]); c->ret = 0; }
HANDLER(ObjLobbed, lobbed)
HANDLER(ObjThrown, thrown)
HANDLER(ObjStanding, standing)
HANDLER(ObjFlying, flying)
HANDLER(ObjWalker, walker)
HANDLER(ObjCrawler, crawler)
HANDLER(ObjCrawlerStill, crawler_still)
HANDLER(ObjAimed, aimed)
HANDLER(ObjBouncer, bouncer)
HANDLER(ObjHopper, hopper)
HANDLER(ObjFloater, floater)
HANDLER(ObjColumn, column)
HANDLER(ObjEdgeCrawler, edge_crawler)
