/* Small enemy handlers, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The enemies").
 *
 * Eight of the enemy handlers, run by RunEnemies (enemies.c) through the handler table at $13422:
 *
 *   handler  kinds   what it does
 *    1 $135AE  $058  the monsters' state machine, but in area 6 room 5 the code at $16908
 *   16 $135BC  $800  the hopper (objects.c), or $160E8 when $FF966B bit 5 is set
 *   17 $135CA  $060  the state machine; with $FF966B bit 5, a guardian of a place ($1100, $1050):
 *                    the entity in $FFA11C standing there for 48 frames opens it ($FFA111), and
 *                    a hit blinks it and its three companions
 *   20 $1369A  $218  gone when its animation ends
 *   21 $18910  $21A  flies on, speeding up by its first velocity every 8 frames, 4 times
 *   23 $194AA  $220  thrown when told (step 1), falls; on a touch or after $200 frames it
 *                    becomes $218 (handler 20)
 *   28 $1766C  $228  carried along, pushing the player; gone in a wall, or in water ($B82A)
 *   34 $19810  $80C  paces between x $1020 and $1080
 *
 * Handlers 1, 16 and 17 end by going on into other code, as the original's bra does
 * (port_continue_at); the rest return. The routines this calls keep running on the caller's
 * registers (`c` below), as the original does. */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

#define PLAYER_W      0xFFFF9EEEu
#define TARGET_W      0xFFFFA11Cu   /* word: the entity the guardian watches */
#define ROOM          0xFFFF9668u   /* word: area, room */
#define WORLD_FLAGS   0xFFFF966Bu
#define G_MODE        0xFFFFA107u   /* the guardian's: nonzero: opens with 1, not 2 */
#define G_FLAGS       0xFFFFA10Fu   /* bit 1: watching; bit 0: every other frame */
#define G_COUNT       0xFFFFA10Eu
#define G_TIME        0xFFFFA110u   /* frames the target has stood there */
#define G_OPEN        0xFFFFA111u
#define G_WORD        0xFFFFA112u
#define FLY_ANIMS     0x18988u      /* handler 21's animations: $0E, $10, $12 */

#define ALIVE(e)      ((e) - 0x4000)   /* bit 7 in use; 6 drawn; 0 on screen */
#define ATTR(e)       ((e) - 0x3FFE)   /* bit 3 facing left; 7 */
#define BLINK(e)      ((e) - 0x3FFD)   /* bit 6 blinking; bit 2 */
#define ANIM_SET(e)   ((e) - 0x3D00)   /* word: set, group */
#define ANIM_RATE(e)  ((e) - 0x3CFD)
#define ANIM_BUSY(e)  ((e) - 0x3A00)
#define ANIM_STEP(e)  ((e) - 0x39FE)   /* word */
#define X(e)          ((e) - 0x3800)
#define Y(e)          ((e) - 0x3700)
#define VEL_X(e)      ((e) - 0x3600)
#define VEL(e)        ((e) - 0x3600)   /* long: x, y */
#define VEL_Y(e)      ((e) - 0x35FE)
#define HALF_H(e)     ((e) - 0x34FD)
#define ACCEL(e)      ((e) - 0x3400)   /* long: x, y */
#define ACCEL_X(e)    ((e) - 0x3400)
#define ACCEL_Y(e)    ((e) - 0x33FE)
#define LIFE(e)       ((e) - 0x31FE)   /* word: frames to live */
#define COLL(e)       ((e) - 0x3100)
#define MOVE_FLAGS(e) ((e) - 0x2F00)
#define STEP(e)       ((e) - 0x2BFF)   /* bit 7 once begun */
#define TYPE(e)       ((e) - 0x2BFE)   /* word: group, kind */
#define TOUCH(e)      ((e) - 0x2AFD)
#define TIMER(e)      ((e) - 0x2800)
#define TIMER2(e)     ((e) - 0x27FF)

/* routines still called as they are */
#define SET_ANIM          0x7E8u
#define SET_ANIM_NOW      0x7FCu
#define APPLY_VEL         0xAB4u
#define APPLY_VEL_X       0xAB8u
#define BODY_OVERLAP      0xE9Au
#define CHECK_BODY        0xE64u
#define PROBE_BELOW       0x3F54u   /* d2: the tile's type below */
#define ENEMY_TAKE_HIT    0x439Eu   /* mi: hit (TOUCH updated from the magic hit) */
#define SET_VEL_X_FACING  0x4496u
#define FACE_VELOCITY     0x44A6u
#define FALL_AND_MOVE     0x46D6u
#define PLAYER_CONTACT    0x47AEu
#define DAMAGE_ENEMY      0x4986u   /* nz: still alive */
#define SPLASH            0xB82Au
#define ENEMY_DISPATCH    0x13582u
#define BLINK_GROUP       0x15E10u  /* the guardian and its three companions start blinking */
#define UNBLINK_GROUP     0x15E2Eu
#define DISPATCH_AREA605  0x16908u
#define DISPATCH_160E8    0x160E8u
#define HOPPER            0x1A7B8u

void EnemyDispatch(Cpu *);           /* enemies.c */

static Cpu *c;                        /* the running registers */

static void call(uint32_t addr) { call_68k(c, addr); }
static void call_d0(uint32_t addr, uint32_t d0) { c->d[0] = d0; call_68k(c, addr); }   /* moveq #d0 */
static int  zero(void) { return (c->ccr & CC_Z) != 0; }
static void set_d0w(int v) { c->d[0] = set_w(c->d[0], (uint32_t)v); }
static int  begins(Ent e) { uint8_t s = rd8(STEP(e)); wr8(STEP(e), s | 0x80); return !(s & 0x80); }
static void neg_word(uint32_t a) { wrw(a, -rdw(a)); }

/* ---- the dispatchers: they go on into other code ---- */

/* handler 1, $135AE */
void HndAreaMonster(Cpu *cpu)
{
    if (rd16(ROOM) == 0x605) { port_continue_at(cpu, DISPATCH_AREA605, 0); return; }
    EnemyDispatch(cpu);
}

/* handler 16, $135BC */
void HndHopperOr(Cpu *cpu)          /* one call per target, so that m68k2c.py sees both */
{
    if (rd8(WORLD_FLAGS) & 0x20) port_continue_at(cpu, DISPATCH_160E8, 0);
    else port_continue_at(cpu, HOPPER, 0);
}

/* handler 17, $135CA: after the state machine, the guardian's watch and its hits */
static void guardian(Ent e)
{
    if (!(rd8(G_FLAGS) & 2) || !(rd8(ALIVE(e)) & 0x40)) return;
    c->d[0] = (uint8_t)((rd8(STEP(e)) & 0x7F) - 1);
    if (!c->d[0]) return;                           /* step 1: beaten */
    int watch = 1;
    if (!rd8(G_MODE)) {                             /* every other frame */
        uint8_t f = rd8(G_FLAGS);
        wr8(G_FLAGS, f & (uint8_t)~1);
        if (f & 1) watch = 0; else wr8(G_FLAGS, rd8(G_FLAGS) | 1);
    }
    if (watch && !rd8(G_OPEN) && rd8(G_COUNT) <= 6) {
        Ent t = (Ent)(int32_t)rdw(TARGET_W);
        c->a[2] = t;
        int there = 0;
        uint16_t dx = (uint16_t)(rdw(X(t)) - 0x1100);
        if (dx & 0x8000) dx = (uint16_t)-dx;
        set_d0w(dx);
        if (dx <= 0x20) {
            uint16_t dy = (uint16_t)(rd8(HALF_H(t)) + rdw(Y(t)) - 0x1050);
            if (dy & 0x8000) dy = (uint16_t)-dy;
            c->d[0] = dy;
            there = dy <= 0x20;
        }
        if (there) {
            wr8(G_TIME, (uint8_t)(rd8(G_TIME) + 1));
            if (rd8(G_TIME) >= 0x30) {
                c->d[0] = rd8(G_MODE) ? 1 : 2;
                wr8(G_OPEN, (uint8_t)c->d[0]);
                wrw(G_WORD, 0);
                wr8(G_TIME, 0);
            }
        } else wr8(G_TIME, 0);
    }
    call(ENEMY_TAKE_HIT);
    if ((c->ccr & CC_N) && !rd8(TIMER(e))) {
        call(DAMAGE_ENEMY);
        if (zero()) { wr8(STEP(e), 1); return; }
        call(BLINK_GROUP);
    }
    if (rd8(TIMER(e))) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (!rd8(TIMER(e))) call(UNBLINK_GROUP);
    }
}

void HndGuardian(Cpu *cpu)
{
    c = cpu;
    if (!(rd8(WORLD_FLAGS) & 0x20)) { EnemyDispatch(c); return; }
    call(ENEMY_DISPATCH);
    guardian((Ent)c->a[4]);
    c->ret = 0;
}

/* ---- the rest return ---- */

/* handler 20, $1369A */
static void vanishing(Ent e)
{
    c->d[0] = set_b(c->d[0], rd8(ANIM_BUSY(e)));
    if (!(c->d[0] & 0xFF)) wr8(ALIVE(e), 0);
}

/* handler 21, $18910 */
static void flyer(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 12);
        set8(ALIVE(e), 0x40);
        wr8(ANIM_RATE(e), 0x0A);
        wr32(ACCEL(e), rd32(VEL(e)));               /* speeds up by its first velocity */
        set8(ATTR(e), 0x80);
        wrw(TIMER(e), 8);                           /* TIMER 0, TIMER2 8 */
        return;
    }
    call(APPLY_VEL);
    call(PLAYER_CONTACT);
    if (rd8(TOUCH(e))) { wr8(ALIVE(e), 0); return; }
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    wr8(TIMER2(e), 8);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) + 1));
    c->d[0] = rd8(TIMER(e));
    if (c->d[0] >= 4) { wr8(ALIVE(e), 0); return; }
    c->d[1] = set_w(c->d[1], (uint16_t)rdw(ACCEL_X(e)));
    wrw(VEL_X(e), rdw(VEL_X(e)) + rdw(ACCEL_X(e)));
    c->d[1] = set_w(c->d[1], (uint16_t)rdw(ACCEL_Y(e)));
    wrw(VEL_Y(e), rdw(VEL_Y(e)) + rdw(ACCEL_Y(e)));
    c->d[0] = set_b(c->d[0], rd8(FLY_ANIMS + (c->d[0] & 0xFF) - 1));
    call(SET_ANIM);
}

/* handler 23, $194AA: steps 0 and 2 wait (another's code moves it on), 1 throws it, 3 flies */
static void thrown_later(Ent e)
{
    uint8_t step = rd8(STEP(e));
    c->d[0] = (uint8_t)(step * 2);
    if (step > 3) {
        fprintf(stderr, "handlers.c: step %d of $194AA is past its table (entity $%04X)\n", step, (unsigned)(e & 0xFFFF));
        exit(3);
    }
    c->a[0] = 0x194BCu + (uint32_t)(int32_t)rdw(0x194BCu + c->d[0]);
    if (step == 1) {
        wr8(STEP(e), 2);
        wr8(MOVE_FLAGS(e), 0x33);
        wrw(LIFE(e), 0x200);
        wrw(VEL_Y(e), -0x300);
        int v = 0x200;
        if (rd8(ATTR(e)) & 8) v = -v;
        set_d0w(v);
        wrw(VEL_X(e), v);
        wr8(ANIM_RATE(e), 0x0A);
        call(SET_ANIM);                             /* with d0 as it is: its low byte, 0 */
        c->d[0] = 0;
        set8(ALIVE(e), 0x40);
        return;
    }
    if (step != 3) return;
    call(FALL_AND_MOVE);
    if ((rd8(COLL(e)) & 2) && rdw(VEL_Y(e)) < 0) wrw(VEL_Y(e), rdw(VEL_Y(e)) + 0x20);   /* a ceiling */
    call(FACE_VELOCITY);
    call(PLAYER_CONTACT);
    if (!rd8(TOUCH(e))) {
        wrw(LIFE(e), rdw(LIFE(e)) - 1);
        if (rdw(LIFE(e))) return;
    }
    wrw(TYPE(e), 0x218);                            /* handler 20's: gone when its animation ends */
    clr8(BLINK(e), 0x40);
    wrw(ANIM_SET(e), 0x1206);
    call_d0(SET_ANIM, 0);
}

/* handler 28, $1766C */
static void carried(Ent e)
{
    if (begins(e)) {
        set_d0w(0x100);
        wrw(VEL_Y(e), 0x100);
        set_d0w(0x200);
        call(SET_VEL_X_FACING);
        wr8(ANIM_RATE(e), 0x0A);
        call_d0(SET_ANIM_NOW, 0);
        set8(ALIVE(e), 0x40);
        return;
    }
    if (!(rd8(BLINK(e)) & 4) && !(rd8(ALIVE(e)) & 1)) { wr8(ALIVE(e), 0); return; }
    call(APPLY_VEL);
    call(PROBE_BELOW);
    if (c->d[2] & 4) {                              /* water */
        c->d[0] = set_b(c->d[0], rd8(ATTR(e)));
        call(SPLASH);
        wr8(ALIVE(e), 0);
        return;
    }
    c->d[2] = set_b(c->d[2], c->d[2] & 3);
    if (c->d[2] & 3) { wr8(ALIVE(e), 0); return; }  /* a wall */
    Ent p = (Ent)(int32_t)rdw(PLAYER_W);
    c->a[2] = p;
    call(BODY_OVERLAP);
    if (!(c->ccr & (CC_C | CC_Z))) return;          /* bhi: apart */
    int16_t push = (int16_t)(rdw(VEL_X(e)) >> 3);
    set_d0w(push);
    wrw(VEL_X(p), rdw(VEL_X(p)) + push);
}

/* handler 34, $19810 */
static void pacer(Ent e)
{
    if (!rd8(STEP(e))) {
        wr8(STEP(e), 2);
        wr8(ALIVE(e), 0xC0);
        wrw(VEL_X(e), 0x100);
        wrw(ANIM_STEP(e), rdw(ANIM_STEP(e)) << 1);
        call_d0(SET_ANIM, 0);
        return;
    }
    call(APPLY_VEL_X);
    c->a[2] = (Ent)(int32_t)rdw(PLAYER_W);
    call(CHECK_BODY);
    int turn = rdw(VEL_X(e)) < 0 ? rdw(X(e)) <= 0x1020 : (uint16_t)rdw(X(e)) > 0x1080;
    if (turn) neg_word(VEL_X(e));
}

/* the handlers that return, registered by their addresses (registry.c); call_68k leaves its mark
 * in ret */
#define HANDLER(name, fn) void name(Cpu *cpu) { c = cpu; fn((Ent)c->a[4]); c->ret = 0; }
HANDLER(HndVanishing, vanishing)
HANDLER(HndFlyer, flyer)
HANDLER(HndThrownLater, thrown_later)
HANDLER(HndCarried, carried)
HANDLER(HndPacer, pacer)
