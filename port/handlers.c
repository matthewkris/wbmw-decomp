/* Small enemy handlers, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The enemies").
 *
 * Twelve of the enemy handlers, run by RunEnemies (enemies.c) through the handler table at $13422
 * (batches 5 and 6):
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
 *   19 $17EE6  $216  fired off backwards, slowing; then aimed at the entity in $FFA11C
 *   24 $19C86  $222  waits, then comes after the player's height; bursts ($19D60) on a touch
 *   25 $1A0B0  $224  flies at its angle, turning an eighth towards the player once
 *   33 $19544  $80A  falls, grows (animations 4-8), stays a second, shrinks away; a touch
 *                    shrinks it at once
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
#define AIMS          0x1A196u      /* handler 25: by angle, 6 bytes: attribute, animation, velocity */
#define DIVER_HITS    0xFFFF99EDu   /* handler 24's touches on the screen, up to $FF */

#define ALIVE(e)      ((e) - 0x4000)   /* bit 7 in use; 6 drawn; 0 on screen. Word: with its neighbour */
#define WORD_3DFE(e)  ((e) - 0x3DFE)
#define HALF_W(e)     ((e) - 0x34FE)   /* (a word with HALF_H) */
#define PHASE(e)      ((e) - 0x31FF)
#define MAKER(e)      ((e) - 0x2CFE)   /* word: the entity that made it; a byte set when it is gone */
#define STRENGTH(e)   ((e) - 0x2700)   /* word */
#define HEALTH(e)     ((e) - 0x2600)   /* word */
#define ANGLE(e)      ((e) - 0x27FE)   /* byte: handler 25's angle */
#define TURNED(e)     ((e) - 0x27FD)
#define SIZE(e)       ((e) - 0x27FE)   /* byte: handler 33's animation */
#define WAIT(e)       ((e) - 0x27FD)
#define DELAY(e)      ((e) - 0x25FE)
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
#define QUEUE_SOUND       0x366u
#define RANDOM            0x5D8u
#define SET_ANIM          0x7E8u
#define FRICTION_X        0xB92u
#define FRICTION_Y        0xBACu
#define ACCEL_Y_          0xB44u
#define OFFSCREEN         0xBEEu
#define BODY_VS_HURT      0xD2Cu
#define BODY_VS_ATTACK    0xD76u
#define DIRECTION_TO      0x108Cu   /* d3: the angle to a2 */
#define SET_VEL_AT_D6     0x123Au   /* the velocity at angle d6, speed d4 */
#define MOVE_FLAT         0x2E9Eu
#define CHECK_GROUNDED    0x4362u
#define ENEMY_MOVE        0x46DAu
#define SHIELD_CONTACT    0x4742u   /* the player's shield against this enemy */
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

/* $19D60 / $19D64: it bursts (kind $218, handler 20: gone when the animation ends), with
 * animation 0 or 2 of the burst */
static void burst(Ent e, uint8_t anim)
{
    c->d[0] = anim;
    set8(ATTR(e), 0x80);
    wrw(ANIM_SET(e), 0x161E);
    wr8(ANIM_RATE(e), 0x0A);
    wrw(TYPE(e), 0x218);
    call(SET_ANIM_NOW);
}

/* its maker (the word at -$2CFE) is told it is gone: that one's own -$2CFE byte set */
static void tell_maker(Ent e)
{
    c->a[0] = (uint32_t)(int32_t)rdw(MAKER(e));
    wr8(MAKER(c->a[0]), 0xFF);
}

/* handler 19, $17EE6: fired off backwards, slowing; then aimed at the entity in $FFA11C give or
 * take an eighth of a turn; burst by a hit */
static void darter(Ent e)
{
    if (begins(e)) {
        call_d0(QUEUE_SOUND, 78);
        wr8(PHASE(e), 0);
        wr32(VEL(e), 0xFD000030u);
        call_d0(SET_ANIM, 14);
        set8(ALIVE(e), 0x40);
        set8(ATTR(e), 8);
        wr8(ANIM_RATE(e), 8);
        return;
    }
    if (rd8(STEP(e)) & 1) {                         /* hit: gone when the animation ends */
        c->d[0] = set_b(c->d[0], rd8(ANIM_BUSY(e)));
        if (!(c->d[0] & 0xFF)) wr8(ALIVE(e), 0);
        return;
    }
    c->a[2] = (Ent)(int32_t)rdw(PLAYER_W);
    int hit = 0;
    if (rd8(0xFFFF9F03u) & 0x40) call(SHIELD_CONTACT);
    else {
        call(CHECK_BODY);
        call(BODY_VS_HURT);
        c->d[0] = rd8(TOUCH(e)) & 6;
        hit = c->d[0] != 0;
    }
    if (!hit) {
        call(BODY_VS_ATTACK);
        call(ENEMY_TAKE_HIT);
        hit = (c->ccr & CC_N) != 0;
    }
    if (hit) {
        wr8(STEP(e), (uint8_t)(rd8(STEP(e)) + 1));
        call_d0(QUEUE_SOUND, 31);
        wrw(ANIM_SET(e), 0x1208);
        c->d[0] = 0;
        wrw(WORD_3DFE(e), 0);
        call(SET_ANIM);
        return;
    }
    uint8_t phase = rd8(PHASE(e));
    c->d[0] = set_b(c->d[0], phase);
    if (phase == 0) {                               /* slowing, until its animation ends */
        call(APPLY_VEL);
        call_d0(FRICTION_X, 16);
        call_d0(FRICTION_Y, 1);
        if (rd8(ANIM_BUSY(e))) return;
        wr8(PHASE(e), 1);
        wr8(TIMER(e), 5);
        call_d0(SET_ANIM, 16);
        return;
    }
    c->d[0] = set_b(c->d[0], (uint8_t)(phase - 1));
    if (phase == 1) {                               /* 5 frames, then aimed */
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        wr8(PHASE(e), 2);
        c->a[2] = (Ent)(int32_t)rdw(TARGET_W);
        call(DIRECTION_TO);
        c->d[6] = set_w(c->d[6], c->d[3]);
        call(RANDOM);
        c->d[1] = set_w(c->d[1], 0x1000);
        set_d0w((int)(c->d[0] & 0x1800));
        if (!(c->d[0] & 0xFFFF)) set_d0w(0x1000);
        set_d0w((int)((c->d[0] - 0x1000) & 0xFFFF));
        c->d[6] = set_w(c->d[6], (c->d[6] + c->d[0]) & 0xFFFF);
        c->d[4] = set_w(c->d[4], 0x100);
        call(SET_VEL_AT_D6);
        call_d0(SET_ANIM, 18);
        return;
    }
    call(APPLY_VEL);                                /* flying: gone off the screen */
    c->d[0] = rd8(ALIVE(e)) & 1;
    if (!c->d[0]) wr8(ALIVE(e), 0);
}

/* handler 24, $19C86: waits, then comes after the player's height, accelerating; bursts on a
 * wall, the floor or a touch. Its strength (-$2700) by the player's health */
static void diver(Ent e)
{
    uint8_t step = rd8(STEP(e));
    c->d[0] = set_b(c->d[0], step);
    if (step == 0) {
        wr8(STEP(e), 1);
        Ent p = (Ent)(int32_t)rdw(PLAYER_W);
        c->a[0] = p;
        set_d0w(rdw(HEALTH(p)));
        c->d[1] = set_w(c->d[1], (uint16_t)rdw(HEALTH(p)) < 0x300 ? 0x800 : 0x2000);
        wrw(STRENGTH(e), (int)(c->d[1] & 0xFFFF));
        wrw(HALF_W(e), 0x0804);
        wrw(VEL_X(e), -0x800);
        call_d0(SET_ANIM, 10);
        wr8(ANIM_RATE(e), 0x0C);
        wr8(TIMER(e), 8);
        set8(ALIVE(e), 0x40);
        return;
    }
    c->d[0] = set_b(c->d[0], (uint8_t)(step - 1));
    if (step == 1) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        wr8(STEP(e), 2);
        wr8(TIMER(e), 8);
        call_d0(SET_ANIM, 12);
        return;
    }
    c->d[0] = set_b(c->d[0], (uint8_t)(step - 2));
    if (step == 2) {                                /* waits for the player to come down to y $1068 */
        if (rd8(TIMER(e))) {
            wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
            if (rd8(TIMER(e))) return;
        }
        Ent p = (Ent)(int32_t)rdw(PLAYER_W);
        c->a[0] = p;
        if ((uint16_t)rdw(Y(p)) < 0x1068) return;
        wr8(STEP(e), 3);
        call_d0(QUEUE_SOUND, 80);
        call_d0(SET_ANIM_NOW, 14);
    }
    Ent p = (Ent)(int32_t)rdw(PLAYER_W);
    c->a[2] = p;
    c->d[0] = 0xFFFFFF80u;
    c->d[1] = set_w(c->d[1], (uint16_t)rdw(Y(p)));
    if ((uint16_t)rdw(Y(p)) >= (uint16_t)rdw(Y(e))) set_d0w(0x80);
    wrw(VEL_Y(e), rdw(VEL_Y(e)) + (int16_t)(c->d[0] & 0xFFFF));
    call(ENEMY_MOVE);
    c->d[0] = rd8(COLL(e)) & 3;
    if (!c->d[0]) {
        c->a[2] = (Ent)(int32_t)rdw(PLAYER_W);
        call(CHECK_BODY);
        if (!rd8(TOUCH(e))) return;
        if (rd8(ALIVE(e)) & 1) {                    /* hits on the screen, counted to $FF */
            wr8(DIVER_HITS, (uint8_t)(rd8(DIVER_HITS) + 1));
            if (!rd8(DIVER_HITS)) wr8(DIVER_HITS, 0xFF);
        }
    }
    tell_maker(e);
    burst(e, 0);
}

/* handler 25, $1A0B0's turn: its angle (-$27FE, in eighths) picks the attribute, the animation
 * and the velocity from the table at $1A196 ($1A13A) */
static void aim(Ent e)
{
    c->d[0] = rd8(ANGLE(e));
    set_d0w((c->d[0] & 0xFFFF) >> 3);
    c->d[1] = set_w(c->d[1], c->d[0]);
    set_d0w((c->d[0] & 0xFFFF) >> 1);
    c->d[1] = set_w(c->d[1], (c->d[1] + c->d[0]) & 0xFFFF);
    uint32_t row = AIMS + (uint32_t)(int32_t)(int16_t)(c->d[1] & 0xFFFF);
    c->d[0] = 0xFFFFFFE7u;
    c->d[0] = set_b(c->d[0], (rd8(ATTR(e)) & 0xE7) | rd8(row));
    wr8(ATTR(e), (uint8_t)c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(row + 1));
    wr32(VEL(e), rd32(row + 2));
    call(SET_ANIM);
}

/* handler 25, $1A164: turns an eighth towards the player when it is $1C-$23 off */
static void turn_to_player(Ent e)
{
    c->a[2] = (Ent)(int32_t)rdw(PLAYER_W);
    call(DIRECTION_TO);
    c->d[3] = set_w(c->d[3], (c->d[3] & 0xFFFF) >> 8);
    uint8_t d = (uint8_t)((uint8_t)c->d[3] - rd8(ANGLE(e)));
    if (d & 0x80) d = (uint8_t)-d;
    c->d[0] = set_b(c->d[0], (uint8_t)(d - 0x1C));
    if (d < 0x1C) return;
    c->d[0] = set_b(c->d[0], (uint8_t)(d - 0x24));
    if (d >= 0x24) return;
    c->d[3] = set_b(c->d[3], (uint8_t)((c->d[3] + 0x10) & 0xE0));
    wr8(ANGLE(e), (uint8_t)c->d[3]);
    wr8(TURNED(e), 0xFF);
    aim(e);
}

/* handler 25, $1A0B0: appears (animation 16), then flies at its angle, turning once towards the
 * player after 4 frames; gone off the screen, bursts on a touch */
static void seeker(Ent e)
{
    uint8_t ph = rd8(PHASE(e));
    c->d[0] = set_b(c->d[0], ph);
    if (ph == 0) {
        wr8(PHASE(e), 1);
        wrw(HALF_W(e), 0x0404);
        wr8(ANIM_RATE(e), 0x0C);
        call_d0(SET_ANIM, 16);
        set8(ALIVE(e), 0x40);
        return;
    }
    c->d[0] = set_b(c->d[0], (uint8_t)(ph - 1));
    if (ph == 1) {
        if (rd8(ANIM_BUSY(e))) return;
        wr8(TURNED(e), 0);
        wr8(DELAY(e), 4);
        wr8(PHASE(e), 2);
        call_d0(QUEUE_SOUND, 59);
        tell_maker(e);
        aim(e);
        call(APPLY_VEL);
        return;
    }
    if (rd8(DELAY(e))) wr8(DELAY(e), (uint8_t)(rd8(DELAY(e)) - 1));
    else if (!rd8(TURNED(e))) turn_to_player(e);
    call(APPLY_VEL);
    call(OFFSCREEN);
    if (!zero()) { wrw(ALIVE(e), 0); return; }
    call(PLAYER_CONTACT);
    uint8_t t = rd8(TOUCH(e));
    c->d[0] = set_b(c->d[0], t);
    if (t || !(rd8(ALIVE(e)) & 1)) burst(e, 2);     /* btst d0 (0 here): bit 0, on screen */
}

/* handler 33, $19600: touched by the player: straight to shrinking (nz) */
static int grower_touched(Ent e)
{
    c->a[2] = (Ent)(int32_t)rdw(PLAYER_W);
    call(CHECK_BODY);
    if (!(rd8(TOUCH(e)) & 4)) return 0;
    wr8(STEP(e), 8);
    wr8(WAIT(e), 1);
    return 1;
}

/* handler 33, $19544: falls to the floor, grows through animations 4-8 (8 frames each), stays a
 * second, then shrinks away; a touch makes it shrink at once */
static void grower(Ent e)
{
    uint8_t step = rd8(STEP(e));
    c->d[0] = step;
    if (step > 8 || (step & 1)) {
        fprintf(stderr, "handlers.c: step %d of $19544 is past its table (entity $%04X)\n", step, (unsigned)(e & 0xFFFF));
        exit(3);
    }
    c->a[0] = 0x19554u + (uint32_t)(int32_t)rdw(0x19554u + step);
    switch (step) {
    case 0:
        wr8(ALIVE(e), 0xC0);
        wr32(VEL(e), 0);
        wrw(ACCEL_Y(e), 0x40);
        wr8(STEP(e), 2);
        c->d[0] = 4;
        wr8(SIZE(e), 4);
        call(SET_ANIM);
        return;
    case 2:                                         /* falling */
        if (grower_touched(e)) return;
        call(ACCEL_Y_);
        call(MOVE_FLAT);
        call(CHECK_GROUNDED);
        if (!zero()) return;
        wr8(STEP(e), 4);
        wr8(WAIT(e), 1);
        return;
    case 4: {                                       /* growing */
        if (grower_touched(e)) return;
        wr8(WAIT(e), (uint8_t)(rd8(WAIT(e)) - 1));
        if (rd8(WAIT(e))) return;
        wr8(SIZE(e), (uint8_t)(rd8(SIZE(e)) + 2));
        c->d[0] = rd8(SIZE(e));
        c->d[1] = 8;
        if (c->d[0] >= 8) { c->d[1] = 60; wr8(STEP(e), 6); }
        wr8(WAIT(e), (uint8_t)c->d[1]);
        call(SET_ANIM);
        return;
    }
    case 6:                                         /* full size */
        grower_touched(e);
        wr8(WAIT(e), (uint8_t)(rd8(WAIT(e)) - 1));
        if (!rd8(WAIT(e))) { wr8(STEP(e), 8); wr8(WAIT(e), 1); }
        return;
    case 8:                                         /* shrinking */
        wr8(WAIT(e), (uint8_t)(rd8(WAIT(e)) - 1));
        if (rd8(WAIT(e))) return;
        wr8(SIZE(e), (uint8_t)(rd8(SIZE(e)) - 2));
        if (rd8(SIZE(e)) & 0x80) { wr8(ALIVE(e), 0); return; }
        wr8(WAIT(e), 8);
        c->d[0] = rd8(SIZE(e));
        call(SET_ANIM);
        return;
    }
}

/* the handlers that return, registered by their addresses (registry.c); call_68k leaves its mark
 * in ret */
#define HANDLER(name, fn) void name(Cpu *cpu) { c = cpu; fn((Ent)c->a[4]); c->ret = 0; }
HANDLER(HndDarter, darter)
HANDLER(HndDiver, diver)
HANDLER(HndSeeker, seeker)
HANDLER(HndGrower, grower)
HANDLER(HndVanishing, vanishing)
HANDLER(HndFlyer, flyer)
HANDLER(HndThrownLater, thrown_later)
HANDLER(HndCarried, carried)
HANDLER(HndPacer, pacer)
