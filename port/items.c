/* Items and effects, each frame, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * $10A1E runs each of the sixteen item slots ($80-$BC, spawned by effects.c) by its kind:
 *
 *   group 0 (things to pick up), each through steps 0 appear, 2 pop up, 4 settle, 6 vanish:
 *     $00-$0A gold that rolls away    $0C-$16 gold that stays    $18-$1C a heart, an item
 *   group 2 (effects):
 *     $200, $202 a splash or puff     $204, $210 a bubble rising  $206-$20C block debris
 *     $20E a hit spark (on the swing  $212 a broken block's       $214 a pickup flying to
 *          of $9F30's attack)              remains (the map)           the player
 *     $216 a sparkle
 *
 * A pickup the player touches is handed over in $FF9F78 (kind | $80) for the player's frame
 * ($F1B0) and disappears.
 *
 * The routines this calls keep running on the caller's registers (`c` below), as the original
 * does: what one item's code leaves in a register is what the next one's sees, and some calls
 * read registers set by the call before them (a2, a3). */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

#define PICKUP        0xFFFF9F78u   /* kind | $80: touched this frame */
#define PLAYER_W      0xFFFF9EEEu
#define ATTACKER_W    0xFFFF9F30u   /* the entity whose attack makes the hit spark */
#define FRAME         0xFFFF965Au
#define ATTACK_POWER  0xFFFF95F4u

#define ALIVE(e)      ((e) - 0x4000)   /* bit 7 in use; 6 drawn; 0 on screen */
#define ATTR(e)       ((e) - 0x3FFE)
#define BLINK(e)      ((e) - 0x3FFD)   /* bit 6: blinking */
#define FLASH(e)      ((e) - 0x3900)
#define X(e)          ((e) - 0x3800)
#define Y(e)          ((e) - 0x3700)
#define VEL_X(e)      ((e) - 0x3600)
#define VEL_Y(e)      ((e) - 0x35FE)
#define HALF_H(e)     ((e) - 0x34FD)
#define ACCEL_X(e)    ((e) - 0x3400)
#define ACCEL_Y(e)    ((e) - 0x33FE)
#define SPEED_X(e)    ((e) - 0x3300)
#define SPEED_Y(e)    ((e) - 0x32FE)
#define COLL(e)       ((e) - 0x3100)
#define MOVE_FLAGS(e) ((e) - 0x2F00)
#define LAST_COLL(e)  ((e) - 0x2EFF)
#define ANIM_SET(e)   ((e) - 0x3D00)
#define ANIM_RATE(e)  ((e) - 0x3CFD)
#define ANIM_BUSY(e)  ((e) - 0x3A00)
#define ANIM_STEP(e)  ((e) - 0x39FE)
#define STEP(e)       ((e) - 0x2BFF)   /* bit 7 once begun */
#define TYPE(e)       ((e) - 0x2BFE)   /* word: the kind (group, kind) */
#define BOUNCE(e)     ((e) - 0x2CFE)   /* word: the next bounce's speed; $214: the entity it flies to */
#define TOUCH(e)      ((e) - 0x2AFD)
#define TIMER(e)      ((e) - 0x2800)
#define FADE(e)       ((e) - 0x27FF)   /* frames left blinking */
#define START_Y(e)    ((e) - 0x27FE)   /* word: where it popped up from; a bubble's life */
#define DAMAGE(e)     ((e) - 0x2700)
#define RANDOM(e)     ((e) - 0x25FE)
#define DROP(e)       ((e) - 0x25FD)   /* bit 0: dropped moving (SAVED_VEL); $9F30's: the spark's state */
#define HEAVY(e)      ((e) - 0x2200)
#define SAVED_VEL(e)  ((e) - 0x2100)   /* long; or the saved x, y (words) */

static Cpu *c;                        /* the running registers */

static void call(uint32_t addr) { call_68k(c, addr); }
static void call_d0(uint32_t addr, uint32_t d0) { c->d[0] = d0; call_68k(c, addr); }
static Ent player(void) { return (Ent)(int32_t)rdw(PLAYER_W); }

/* the first frame of a step: 1 the first time (bit 7 of STEP, or of ALIVE's bit 6 for some) */
static int begins(Ent e) { uint8_t s = rd8(STEP(e)); wr8(STEP(e), s | 0x80); return !(s & 0x80); }
static int shows(Ent e)  { uint8_t a = rd8(ALIVE(e)); wr8(ALIVE(e), a | 0x40); return !(a & 0x40); }
static void next_step(Ent e) { wr8(STEP(e), (uint8_t)((rd8(STEP(e)) & 0x7F) + 2)); }

static void set_anim(Ent e, uint8_t id)       /* $7E8 */
{
    c->d[0] = id;
    wr8(e - 0x3CFE, id);
    clr8(e - 0x3FFD, 0x81);
}

/* BodyOverlap against the player (a2 as the calls before left it): 1 if touching (bls) */
static int touches_player(void)
{
    call(0xE9A);
    return (c->ccr & (CC_C | CC_Z)) != 0;
}

/* $10D42: picked up, if the player has not picked up something else this frame */
static void picked_up(Ent e)
{
    if (rd8(PICKUP) & 0x80) return;
    c->d[0] = (c->d[0] & ~0xFFu) | (uint8_t)(rd8(TYPE(e) + 1) | 0x80);
    wr8(PICKUP, (uint8_t)c->d[0]);
    wr8(ALIVE(e), 0);
}

/* after the settle time it blinks for 60 frames, then goes to the next step ($10CEA, $10B66) */
static int blink_or_wait(Ent e, int last_step)
{
    if (rd8(FADE(e))) {
        wr8(FADE(e), (uint8_t)(rd8(FADE(e)) - 1));
        if (rd8(FADE(e))) return 0;
        if (last_step) wr8(STEP(e), 6); else next_step(e);
        return 1;
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return 0;
    wrw(FLASH(e), 0x2201);
    set8(BLINK(e), 0x40);
    wr8(FADE(e), 0x3C);
    return 1;
}

/* ---- the pickups' steps ---- */

/* $10BD4, step 0: appear (a dropped one keeps the speed it was given) */
static void appear(Ent e)
{
    static const uint8_t sound[16] = {0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0, 0, 0, 0x48, 0x48, 0x48, 0, 0, 0, 0};
    if (rd8(DROP(e)) & 1) {
        wr32(VEL_X(e), rd32(SAVED_VEL(e)));
        wrw(BOUNCE(e), rdw(VEL_Y(e)));
    }
    wr8(TOUCH(e), 0);
    set8(MOVE_FLAGS(e), 2);
    wr8(STEP(e), 2);
    call_d0(0x7FC, 0);                              /* SetAnimNow */
    set8(ALIVE(e), 0x40);
    int16_t y = (int16_t)(rdw(Y(e)) - rd8(HALF_H(e)));
    wrw(Y(e), y);
    wrw(START_Y(e), y);
    uint8_t s = sound[(rd16(TYPE(e)) & 0x7F) >> 1];
    c->d[0] = s;
    if (s) call(0x366);
}

/* the probes and water an item does each frame: the tile below, the tile at it, a splash */
static void probes(Ent e)
{
    (void)e;
    call(0x3F54);                                   /* below, on plane A */
    call(0x3FB0);                                   /* at it */
    call(0xDD7C);                                   /* a splash on entering water */
}

/* $10C3C, step 2: pop up (a random speed unless dropped), fall back to where it started,
 * then land */
static void pop_up(Ent e)
{
    if (begins(e)) {
        wrw(SAVED_VEL(e) + 2, rdw(Y(e)));
        if (!(rd8(DROP(e)) & 1)) {
            call(0x5D8);                            /* Random */
            wr8(RANDOM(e), (uint8_t)c->d[0]);
            c->d[0] = (c->d[0] & 0xFFFF0000u) | (uint16_t)((c->d[0] & 0xC0) + 0xFA00);
            wrw(VEL_Y(e), (uint16_t)c->d[0]);
        }
    }
    probes(e);
    call(0xB44);                                    /* AccelY */
    if (rdw(VEL_Y(e)) >= 0) {
        wr8(ANIM_RATE(e), 10);
        c->a[2] = player();
        if (touches_player()) { picked_up(e); return; }
        uint16_t bottom = (uint16_t)(rd8(HALF_H(e)) + rd16(Y(e)));
        c->d[0] = (uint16_t)(bottom - rd16(START_Y(e)));
        if (bottom >= rd16(START_Y(e))) {           /* back down: land */
            call(0x2EDE);                           /* MoveOnSlopes */
            if (rd8(COLL(e)) & 2) {
                wr8(LAST_COLL(e), rd8(COLL(e)));
                next_step(e);
            }
            return;
        }
    }
    call(0xAB4);                                    /* ApplyVelocity */
}

/* $10AE4, step 4 of rolling gold: rolls off (away from the player, or by its random byte),
 * bouncing lower each time, until it blinks out */
static void roll(Ent e)
{
    Ent p = player();
    c->a[2] = p;
    if (begins(e)) {
        wrw(ACCEL_Y(e), 0x30);
        set8(MOVE_FLAGS(e), 0x10);
        wrw(TIMER(e), 0xF000);                      /* 240 frames; no blinking yet */
        if (!(rd8(DROP(e)) & 1)) {
            uint8_t r = rd8(RANDOM(e));
            wrw(BOUNCE(e), (r & 0xC0) + 0xFA00);
            int16_t v = (int16_t)((r & 0x70) + 0x110);
            if (r & 3) { if (rd16(X(p)) < rd16(X(e))) v = (int16_t)-v; }
            else if (r & 0x80) v = (int16_t)-v;
            wrw(VEL_X(e), v);
        }
    }
    probes(e);
    if (touches_player()) { picked_up(e); return; }
    call_d0(0xB92, 2);                              /* FrictionX */
    call(0xB44);                                    /* AccelY */
    call(0x2F34);                                   /* MoveOnSlopesWithCeiling */
    if (blink_or_wait(e, 1)) return;
    c->d[0] = (uint8_t)((rd8(COLL(e)) & 0xA) - 2);  /* CheckGrounded */
    if ((rd8(COLL(e)) & 0xA) != 2) return;
    int16_t b = rdw(BOUNCE(e));
    b = (int16_t)(b - (b >> 2));
    c->d[0] = (c->d[0] & 0xFFFF0000u) | (uint16_t)b;
    c->d[1] = (c->d[1] & 0xFFFF0000u) | (uint16_t)(rdw(BOUNCE(e)) >> 2);
    if ((uint16_t)b >= 0xFF80) return;              /* too small a bounce: stays */
    wrw(BOUNCE(e), b);
    wrw(VEL_Y(e), b);
    wr8(COLL(e), rd8(COLL(e)) & 0x75);
}

/* $10CD6: lying there until it blinks out */
static void lie(Ent e)
{
    call(0x3F54);
    call(0x2EDE);                                   /* MoveOnSlopes */
    c->a[2] = player();
    if (touches_player()) { picked_up(e); return; }
    blink_or_wait(e, 0);
}

/* $10D1C, step 6: vanish (a puff), then free the slot */
static void vanish(Ent e)
{
    if (begins(e)) {
        clr8(BLINK(e), 0x40);
        wrw(ANIM_SET(e), 0x1206);
        call_d0(0x7FC, 0);                          /* SetAnimNow */
    }
    c->d[0] = (c->d[0] & ~0xFFu) | rd8(ANIM_BUSY(e));
    if (!rd8(ANIM_BUSY(e))) wr8(ALIVE(e), 0);
}

static void pickup(Ent e, int kind)                 /* 0 rolling gold, 1 gold, 2 heart or item */
{
    uint8_t s = rd8(STEP(e)) & 0x7F;
    c->d[0] = (c->d[0] & 0xFFFF0000u) | s;
    switch (s) {
    case 0: appear(e); break;
    case 2: pop_up(e); break;
    case 4:
        if (kind == 0) roll(e);
        else if (kind == 1) {                       /* $10AAE */
            if (begins(e)) { wrw(VEL_X(e), 0); wrw(TIMER(e), 0xF000); set_anim(e, 2); }
            lie(e);
        } else {                                    /* $10CC0 */
            if (begins(e)) { wr8(TIMER(e), 0xF0); wr8(FADE(e), 0); wr32(VEL_X(e), 0); }
            lie(e);
        }
        break;
    case 6: vanish(e); break;
    default:
        fprintf(stderr, "items: slot $%02X (kind $%04X): step $%02X is outside the original's table ($10ADC)\n",
                (unsigned)e, rd16(TYPE(e)), s);
        exit(3);
    }
}

/* ---- the effects ---- */

/* $10D5A, $10D7A: play the animation once */
static void puff(Ent e)
{
    if (begins(e)) { call_d0(0x7FC, 0); set8(ALIVE(e), 0x40); }
    c->d[0] = (c->d[0] & ~0xFFu) | rd8(ANIM_BUSY(e));
    if (!rd8(ANIM_BUSY(e))) wr8(ALIVE(e), 0);
}

/* $10DAE: a bubble rises, wobbling, for 240 frames or until it leaves the water or the screen */
static void bubble_rise(Ent e)
{
    if (begins(e)) {
        wr8(ANIM_RATE(e), 10);
        call_d0(0x7FC, 0);                          /* SetAnimNow */
        wrw(ACCEL_Y(e), 8);
        wrw(VEL_Y(e), rdw(VEL_Y(e)) - 0x80);
        wrw(SPEED_Y(e), rdw(VEL_Y(e)));
        set8(ALIVE(e), 0x41);
        wr8(TIMER(e), 8);
        wrw(START_Y(e), 0xF0);
    }
    wrw(START_Y(e), rdw(START_Y(e)) - 1);
    if (!rdw(START_Y(e))) { wr8(STEP(e), 2); return; }
    if (!(rd8(ALIVE(e)) & 1)) { wr8(ALIVE(e), 0); return; }   /* off screen */
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { wr8(TIMER(e), 0x10); wrw(ACCEL_X(e), -rdw(ACCEL_X(e))); }
    wr8(MOVE_FLAGS(e), 3);
    call(0xB42);                                    /* Accel */
    call(0xAB4);                                    /* ApplyVelocity */
    uint16_t phase = (uint16_t)(((uint16_t)e >> 2) - rd16(FRAME));
    c->d[0] = (c->d[0] & 0xFFFF0000u) | (phase & 3);
    if (phase & 3) return;                          /* the water checked every 4th frame */
    call(0x3FAC);                                   /* ProbeTileAt: the tile here, in d2 */
    uint16_t tile = (uint16_t)c->d[2];
    c->d[0] = (c->d[0] & 0xFFFF0000u) | tile;
    c->d[2] = (c->d[2] & 0xFFFF0000u) | (tile & 0xF);
    if (tile & 0xF) { wr8(STEP(e), 2); return; }    /* out of the water: pops */
    c->d[1] = (c->d[1] & 0xFFFF0000u) | (tile & 0xB000);
    if ((tile & 0xB000) == 0xB000) {                /* a current, but not $40: no friction */
        c->d[0] = (c->d[0] & 0xFFFF0000u) | (tile & 0x70);
        if ((tile & 0x70) != 0x40) wr8(MOVE_FLAGS(e), 0);
    }
    call(0x3E4C);                                   /* Currents */
}

static void bubble(Ent e)
{
    uint8_t s = rd8(STEP(e)) & 0x7F;
    c->d[0] = s;
    if (s == 0) bubble_rise(e);
    else if (s == 2) vanish(e);
    else {
        fprintf(stderr, "items: bubble step %d is outside the original's table ($10DAA)\n", s);
        exit(3);
    }
}

/* $10E58: debris thrown up and out (lower if light), falling for 28 frames */
static void debris(Ent e)
{
    if (begins(e)) {
        set8(ATTR(e), 0x80);
        wr8(ANIM_RATE(e), 8);
        set_anim(e, 0);
        int16_t v = rdw(SPEED_X(e));
        if (rdw(ACCEL_X(e)) < 0) v = (int16_t)-v;
        wrw(VEL_X(e), v);
        c->d[0] = (c->d[0] & 0xFFFF0000u) | (uint16_t)(rd8(HEAVY(e)) ? 0xFA80 : 0xFC00);
        wrw(VEL_Y(e), (uint16_t)c->d[0]);
        wr8(MOVE_FLAGS(e), 2);
        wr8(TIMER(e), 0x1C);
        set8(ALIVE(e), 0x40);
        return;
    }
    call_d0(0xB92, 12);                             /* FrictionX */
    call(0xB44);                                    /* AccelY */
    call(0xAB4);                                    /* ApplyVelocity */
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) wr8(ALIVE(e), 0);
}

/* $10EC2: the hit spark of $9F30's attack: shown when it hits ($9F30's DROP bit 0), and while
 * shown it hurts the enemies it touches (a quarter of the attack's power); gone with $9F30 */
static void hit_spark(Ent e)
{
    if (begins(e)) {
        wrw(SAVED_VEL(e), rdw(X(e)));
        wrw(SAVED_VEL(e) + 2, rdw(Y(e)));
    }
    Ent a = (Ent)(int32_t)rdw(ATTACKER_W);
    c->a[0] = a;
    if (rd8(DROP(a)) & 0x80) return;
    if (!(rd8(ALIVE(e)) & 0x40)) {
        uint8_t d = rd8(DROP(a));
        wr8(DROP(a), d & ~1);
        if (!(d & 1)) return;
        wrw(X(e), rdw(SAVED_VEL(e)));
        wrw(Y(e), rdw(SAVED_VEL(e) + 2));
        set8(ALIVE(e), 0x40);
        set_anim(e, 8);
        call_d0(0x366, 0x2B);                       /* QueueSound */
        return;
    }
    /* the enemies $40-$7C; counted in d5 and a2 as the original does, since it calls out */
    c->d[5] = (c->d[5] & 0xFFFF0000u) | 15;
    for (c->a[2] = 0x40;; c->a[2] += 4) {
        Ent b = c->a[2];
        uint8_t f = rd8(ALIVE(b)) & 0xC1;
        c->d[0] = (c->d[0] & ~0xFFu) | f;
        if (f == 0xC1 && !rd8(TYPE(b)) && rdw(b - 0x2600)) {
            uint8_t s = (uint8_t)((rd8(STEP(b)) & 0x7F) - 2);
            c->d[0] = (c->d[0] & ~0xFFu) | s;
            if (s) {                                /* not one being knocked back */
                c->d[0] = (c->d[0] & 0xFFFF0000u) | (uint16_t)(rd16(ATTACK_POWER) >> 2);
                wrw(DAMAGE(e), (uint16_t)c->d[0]);
                wr8(TOUCH(e), 0);
                call(0xE64);                        /* CheckBodyContact */
            }
        }
        uint16_t n = (uint16_t)(c->d[5] - 1);       /* dbra */
        c->d[5] = (c->d[5] & 0xFFFF0000u) | n;
        if (n == 0xFFFF) break;
    }
    c->a[2] += 4;
    if (rd8(ANIM_BUSY(e))) return;
    a = (Ent)(int32_t)rdw(ATTACKER_W);
    c->a[0] = a;
    wr8(DROP(a), 0xFF);
    clr8(ALIVE(e), 0x40);
    if (!rd8(TIMER(a))) wrw(ALIVE(e), 0);
}

/* $10F76: a pickup flying to the player for 120 frames (or until it touches its target), then
 * straight at the player until it arrives */
static void fly_to(Ent e, Ent to)
{
    c->a[2] = to;
    c->d[4] = (c->d[4] & 0xFFFF0000u) | (uint16_t)rd16(SPEED_X(e));
    call(0x1234);                                   /* aim: the velocity towards a2 */
}
static void homing(Ent e)
{
    if (shows(e)) {
        wr8(ANIM_RATE(e), 6);
        set8(ATTR(e), 0x80);
        wr8(TIMER(e), 0x78);
        fly_to(e, (Ent)(int32_t)rdw(BOUNCE(e)));
        set_anim(e, 0);
        return;
    }
    call(0x4254);                                   /* slow down, accelerate, move */
    c->d[0] = rd16(FRAME) & 3;
    if (!(rd16(FRAME) & 3)) fly_to(e, (Ent)(int32_t)rdw(BOUNCE(e)));
    if ((int8_t)rd8(TIMER(e)) < 0) {                /* on its way to the player */
        c->a[2] = (Ent)(int32_t)rdw(ATTACKER_W);
        if (touches_player()) {
            wrw(ALIVE(e), 0);
            wr8(DROP(c->a[2]), 0xFF);
        }
        return;
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if ((int8_t)rd8(TIMER(e)) >= 0) {
        c->d[0] = (c->d[0] & 0xFFFF0000u) | (uint16_t)(rd16(ATTACK_POWER) >> 2);
        wrw(DAMAGE(e), (uint16_t)c->d[0]);
        c->a[2] = (Ent)(int32_t)rdw(BOUNCE(e));
        wr8(TOUCH(e), 0);
        call(0xE64);                                /* CheckBodyContact */
        if (!rd8(TOUCH(e))) return;
    }
    Ent p = (Ent)(int32_t)rdw(ATTACKER_W);
    wrw(BOUNCE(e), (uint16_t)p);
    wr8(TIMER(e), 0xFF);
    fly_to(e, p);
}

/* $1100A: a sparkle, once */
static void sparkle(Ent e)
{
    if (shows(e)) {
        wrw(ANIM_STEP(e), 0x80);
        wr8(ANIM_RATE(e), 6);
        set_anim(e, 0);
        return;
    }
    c->d[0] = (c->d[0] & ~0xFFu) | rd8(ANIM_BUSY(e));
    if (!rd8(ANIM_BUSY(e))) wr8(ALIVE(e), 0);
}

/* $11030: a broken block's remains: the map block becomes $296 (the second frame), then it
 * plays out */
static void remains(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        if (shows(e)) {
            set8(ATTR(e), 0x80);
            wr8(ANIM_RATE(e), 4);
            wrw(SAVED_VEL(e), rdw(X(e)));
            wrw(SAVED_VEL(e) + 2, rdw(Y(e)));
            wrw(X(e), rdw(X(e)) + 8);
            wrw(Y(e), rdw(Y(e)) + 8);
            wrw(ANIM_SET(e), 0x1208);
            set_anim(e, 0);
            return;
        }
        c->d[0] = (c->d[0] & 0xFFFF0000u) | 0x296;
        c->d[6] = (c->d[6] & 0xFFFF0000u) | (uint16_t)((uint16_t)rdw(SAVED_VEL(e)) >> 4);
        c->d[7] = (c->d[7] & 0xFFFF0000u) | (uint16_t)(((uint16_t)rdw(SAVED_VEL(e) + 2) & 0xFFF0) << 2);
        call(0x266C);                               /* the map block, drawn */
        set8(STEP(e), 0x80);
    }
    if (!rd8(ANIM_BUSY(e))) wrw(ALIVE(e), 0);
}

static void run(Ent e)
{
    c->a[4] = e;
    uint16_t kind = rd16(TYPE(e));
    uint8_t group = (uint8_t)(kind >> 8), k = (uint8_t)kind;    if (group == 0 && k <= 0x1C && !(k & 1)) pickup(e, k <= 0x0A ? 0 : k <= 0x16 ? 1 : 2);
    else if (group == 2 && k <= 0x16 && !(k & 1)) {
        switch (k) {
        case 0x00: case 0x02: puff(e); break;
        case 0x04: case 0x10: bubble(e); break;
        case 0x06: case 0x08: case 0x0A: case 0x0C: debris(e); break;
        case 0x0E: hit_spark(e); break;
        case 0x12: remains(e); break;
        case 0x14: homing(e); break;
        case 0x16: sparkle(e); break;
        }
    } else {
        fprintf(stderr, "items: kind $%04X is outside the original's table ($10A5A)\n", kind);
        exit(3);
    }
}

/* $10A1E: every item and effect, once */
void RunItems(Cpu *cpu)
{
    c = cpu;
    for (Ent e = 0x80; e < 0xC0; e += 4)
        if (rd8(ALIVE(e)) & 0x80) run(e);
    c->a[4] = 0xC0;
    c->ret = 0;                                     /* call_68k on these registers left its mark */
}
