/* The monsters' action routines, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The
 * enemies").
 *
 * A monster (group 0) runs the state machine in enemies.c: its kind and state (STEP, 0-8) pick
 * one of the 135 actions in the table at $13DB6, and EnemyDispatch jumps there (d0 = the action
 * number * 4, d1 = the state * 2, a0 = the action). An action returns to RunEnemies, or goes on
 * into other code as the original's bra/jmp does (go_on below). Each is named by its address.
 *
 * Batch 7, the first actions ($140F2-$1436B):
 *
 *   appearing (state 0 of most kinds): $140F2 and its variants $140FC (attribute bit 4), $1410E
 *     (remembers the frame), $14120 (in water: half speed, steps 3 or 7), $1416E (makes a
 *     shadow entity first), $141A4 (waits for the player within $60, throws debris as its
 *     animation goes). $14250 is the shared wait: on its first frame it is placed ($14228) and,
 *     if its spawn flags (-$2AFE) say so, shows a puff ($1208 or $120A) until it ends; $14202
 *     then sets it up ($14208) and moves on to the next state (EnemyNextState).
 *   defeated: $1429E (30 frames, then EnemyDefeated), $142D6 and $142E4 (blinking), $142F6 (also
 *     removes its partner, -$2CFE), $14308 (when its animation ends); $1432C fades it out with
 *     the puff when EnemyDefeated left TIMER set.
 *
 * Batch 8, hurt ($1436C-$14517): state 2's actions. The first frame clears the hit records and
 * takes the damage (beaten: state 1), then blinks and is knocked back ($4558); each frame after
 * ($144E8) it can be hit again, and after TIMER frames it goes on. $1436C and $14394 stand,
 * $143BC, $143F2 and $14420 are knocked back and fall, $14456 drifts in water.
 *
 * Batch 9, walking, hovering and shooting ($14518-$14821): $14518 stands a random while,
 * $1454C the same until $168 frames old, then three shots and gone; $145C4 hovers and swoops
 * (state 7); $1463C walks; $14682 stands shooting; $146FC hops, shooting once per hop; $1476C
 * rises $60 and waits for the player below. Most call EnemyCheckHit ($43BC) and
 * DespawnIfOffscreen ($48C4), which leave past the action: hit() and despawned() do their
 * tests in C and the action returns.
 *
 * Batch 10, choosing ($14822-$14C4F): standing until its wait is over ($42AA, which leaves past
 * the action otherwise), then a step from a table by how near the player is ($428C): $14822,
 * $14888 (with the shield), $1492A, $14974, $14AEA/$14AFA (a range of their own), $14B7E (water
 * rows); $149D0 walks home; $14A82 turns to a player it faces; $14C1A waits, then falls.
 *
 * Spawn flags (-$2AFE): bit 7 puff on appearing, bit 1 the other puff, bit 2 turned over
 * (attribute bit 7), bit 0 not placed by its animation.
 *
 * The routines this calls keep running on the caller's registers (`c` below), as the original
 * does. */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

#define FRAME         0xFFFF965Au
#define WORLD_FLAGS   0xFFFF966Bu   /* bit 0: in water */

#define ALIVE(e)      ((e) - 0x4000)   /* bit 7 in use; 6 drawn; 5; 0 on screen. Word: with its neighbour */
#define ATTR(e)       ((e) - 0x3FFE)   /* bit 3 facing left; 4; 7 turned over */
#define BLINK(e)      ((e) - 0x3FFD)   /* bit 6 blinking; bit 3 */
#define ANIM_SET(e)   ((e) - 0x3D00)   /* word: set, group */
#define ANIM_RATE(e)  ((e) - 0x3CFD)
#define ANIM_BUSY(e)  ((e) - 0x3A00)
#define ANIM_STEP(e)  ((e) - 0x39FE)   /* word */
#define X(e)          ((e) - 0x3800)
#define Y(e)          ((e) - 0x3700)
#define HALF_H(e)     ((e) - 0x34FD)
#define SPEED_X(e)    ((e) - 0x3300)
#define SPEED_Y(e)    ((e) - 0x32FE)
#define WORD_31FE(e)  ((e) - 0x31FE)
#define MOVE_FLAGS(e) ((e) - 0x2F00)
#define FRAME_SEEN(e) ((e) - 0x2C00)   /* byte: its animation's frame counter */
#define LINK2(e)      ((e) - 0x2CFE)   /* word: its partner entity; or a frame number */
#define STEP(e)       ((e) - 0x2BFF)   /* the state; bit 7 once begun */
#define TYPE(e)       ((e) - 0x2BFE)   /* word: group, kind */
#define LINK(e)       ((e) - 0x2B00)   /* word: its spawn point */
#define SPAWN(e)      ((e) - 0x2AFE)   /* byte: spawn flags */
#define HIT_A(e)      ((e) - 0x2A00)   /* long: hit records, cleared when it takes damage */
#define HIT_B(e)      ((e) - 0x2900)   /* long */
#define TIMER(e)      ((e) - 0x2800)
#define TIMER2(e)     ((e) - 0x27FF)
#define SAVED(e)      ((e) - 0x27FE)   /* word: the animation set while the puff shows */
#define VAR(e)        ((e) - 0x27FE)   /* byte: a flag of the action's */
#define SHOT(e)       ((e) - 0x27FD)   /* byte: bit 0, shot this hop */
#define WORD_3DFE(e)  ((e) - 0x3DFE)
#define VEL_X(e)      ((e) - 0x3600)
#define VEL(e)        ((e) - 0x3600)   /* long: x, y */
#define VEL_Y(e)      ((e) - 0x35FE)
#define ACCEL_X(e)    ((e) - 0x3400)
#define COLL(e)       ((e) - 0x3100)
#define HOME_Y(e)     ((e) - 0x20FE)
#define HOME_X(e)     ((e) - 0x2100)
#define ANIM_ID(e)    ((e) - 0x3CFE)

/* routines still called as they are */
#define SET_ANIM          0x7E8u
#define SET_ANIM_NOW      0x7FCu
#define TYPE_AT_PIXEL     0x30CEu   /* d2: the collision type at d6, d7 */
#define MAKE_PARTNER      0x41A2u   /* a2 becomes its partner (-$2CFE both ways), cleared */
#define SPAWN_DEBRIS      0x42E8u
#define START_BLINK       0x451Au
#define FACE_PLAYER       0x44D6u
#define DIST_X_TO_PLAYER  0x455Eu   /* d1: the distance */
#define ENEMY_DEFEATED    0x482Cu
#define FIND_FREE_ENTITY  0x12812u  /* a2; mi: none */
#define ENEMY_NEXT_STATE  0x13542u
#define ENEMY_SET_STATE   0x1357Eu  /* d0: the state; runs its action */
#define RANDOM            0x5D8u
#define ACCEL_Y           0xB44u
#define FRICTION_X        0xB92u
#define MOVE_WITH_PLATFORMS 0x2F8Eu
#define BUBBLE            0x4218u   /* an item ($204 or $210) at its top */
#define SLOW_ON_GROUND    0x4356u
#define CHECK_GROUNDED    0x4362u   /* z: on the ground */
#define ENEMY_TAKE_HIT    0x439Eu   /* mi: hit */
#define START_BLINK_FROZEN 0x4514u
#define KNOCK_BACK_AT     0x453Cu   /* d0: the speed */
#define KNOCK_BACK        0x4558u   /* at $400 ($100 on the ground) */
#define ENEMY_MOVE        0x46DAu
#define PLAYER_TOUCH      0x4778u   /* the player (or the shield) against it */
#define DAMAGE_ENEMY      0x4986u   /* z: beaten */
#define APPLY_VEL         0xAB4u
#define BOB_TOWARD        0x4274u   /* ACCEL_Y toward the height d0, give or take d1, by d2 */
#define CONTACT_AND_MOVE  0x42C4u   /* touching the player, then falling and moving */
#define RANDOM_TIMER      0x43CCu   /* TIMER: $40 + up to $7F */
#define RANDOM_TIMER_32   0x43DEu   /* TIMER: $20 + up to $1F; d0 the random */
#define RANDOM_DELAY      0x43F0u   /* TIMER2: $10 + up to $1F */
#define FACE_VELOCITY     0x44A6u
#define JUMP              0x44F4u   /* VEL_Y up by SPEED_Y + $80 */
#define DIST_X_TO_ENTITY  0x4562u   /* d1: to a2 */
#define DIST_Y_TO_TARGET  0x4576u   /* a2 = $FFA11C's entity; d1 = its y - this one's (flags of sub.w) */
#define PLAYER_CONTACT_ALL 0x4792u  /* touching, and the sword */
#define DESPAWN           0x4814u
#define SHOOT             0x15182u
#define APPLY_VEL_Y       0xAD6u
#define MAYBE_BUBBLE      0x41F0u   /* in water, now and then */
#define SHIELD_AND_MOVE   0x42CCu   /* the shield's touch, then falling and moving */
#define BACK_OFF          0x437Eu   /* touched: VEL_X backwards at twice its speed */
#define WALK_FORWARD      0x4492u
#define FACE_X            0x44DEu   /* towards x d0 */
#define DIST_Y_ABS_TARGET 0x4584u   /* a2 = $FFA11C's entity; d1 = |its y - this one's| */
#define DIST_Y_ABS        0x4588u   /* the same to a2 */
#define KIND_STATS        0x15A3Au  /* a1: its kind's record in $1381C */
#define KIND_MOVES        0x15A76u  /* -$2900: its moves, by d0 */

void EnemySetState(Cpu *);           /* enemies.c */

static Cpu *c;                        /* the running registers */
static int went_on;                   /* go_on was called: c->ret holds where */

static void call(uint32_t addr) { call_68k(c, addr); }
static void call_d0(uint32_t addr, uint32_t d0) { c->d[0] = d0; call_68k(c, addr); }   /* moveq #d0 */
static void set_d0w(int v) { c->d[0] = set_w(c->d[0], (uint32_t)v); }
static int  begins(Ent e) { uint8_t s = rd8(STEP(e)); wr8(STEP(e), s | 0x80); return !(s & 0x80); }

/* ---- appearing ---- */

/* $14228: placed: turned over by its spawn flags, and standing on its spawn point */
static void place(Ent e)
{
    if (rd8(SPAWN(e)) & 4) set8(ATTR(e), 0x80);
    if (rd8(SPAWN(e)) & 1) return;
    call_d0(SET_ANIM_NOW, 0);
    c->d[0] = rd8(HALF_H(e));
    wrw(Y(e), rdw(Y(e)) - (int)c->d[0]);
}

/* the puff's animation set by the spawn flags (bit 1) */
static void puff_set(Ent e)
{
    set_d0w(rd8(SPAWN(e)) & 2 ? 0x120A : 0x1208);
    wrw(ANIM_SET(e), (int)(c->d[0] & 0xFFFF));
}

/* $14250: 1 once it has appeared (at once, or when its puff ends) */
static int appeared(Ent e)
{
    if (begins(e)) {
        place(e);
        if (!(rd8(SPAWN(e)) & 0x80)) return 1;
        wrw(SAVED(e), rdw(ANIM_SET(e)));
        puff_set(e);
        set8(BLINK(e), 8);
        call_d0(SET_ANIM_NOW, 0);
        set8(ALIVE(e), 0x40);
    }
    if (rd8(ANIM_BUSY(e))) return 0;
    wrw(ANIM_SET(e), rdw(SAVED(e)));
    c->d[0] = 0;
    return 1;
}

/* $14208: shown, facing the player, moving */
static void set_up(Ent e)
{
    clr8(BLINK(e), 8);
    c->d[0] = 0;
    wrw(WORD_31FE(e), 0);
    call(SET_ANIM_NOW);
    set8(ALIVE(e), 0x40);
    set8(MOVE_FLAGS(e), 3);
    call(FACE_PLAYER);
}

/* $14202: set up, and on to the next state */
static void begin_life(Ent e)
{
    set_up(e);
    call(ENEMY_NEXT_STATE);
}

static void a140F2(Ent e) { if (appeared(e)) begin_life(e); }

static void a140FC(Ent e)
{
    if (!appeared(e)) return;
    set8(ATTR(e), 0x10);
    begin_life(e);
}

static void a1410E(Ent e)
{
    if (!appeared(e)) return;
    wrw(LINK2(e), rdw(FRAME));
    begin_life(e);
}

/* in water: half its speed, up as well as along, and step 3 or 7 by what it stands in */
static void a14120(Ent e)
{
    if (!appeared(e)) return;
    if (!(rd8(WORLD_FLAGS) & 1)) { begin_life(e); return; }
    wrw(SPEED_X(e), rdw(SPEED_X(e)) >> 1);
    set_d0w(rdw(SPEED_X(e)));
    wrw(SPEED_Y(e), rdw(SPEED_X(e)));
    set_up(e);
    set8(MOVE_FLAGS(e), 0x40);
    c->d[6] = set_w(c->d[6], (uint16_t)rdw(X(e)));
    c->d[7] = rd8(HALF_H(e));
    c->d[7] = set_w(c->d[7], (c->d[7] + (uint16_t)rdw(Y(e))) & 0xFFFF);
    call(TYPE_AT_PIXEL);
    c->d[0] = c->d[2] & 8 ? 3 : 7;
    wr8(STEP(e), (uint8_t)c->d[0]);
    set_d0w((int)(c->d[0] * 2));
    call(SET_ANIM);
}

/* with a shadow: a partner entity drawn turned over, a little slower, of no kind */
static void a1416E(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        call(FIND_FREE_ENTITY);
        if (c->ccr & CC_N) return;                  /* none free: tried again next frame */
        call(MAKE_PARTNER);
        Ent s = c->a[2];
        wrw(ANIM_SET(s), 0x162C);
        set8(ATTR(s), 0x80);
        c->d[0] = set_b(c->d[0], (uint8_t)(rd8(ANIM_RATE(e)) + 2));
        wr8(ANIM_RATE(s), (uint8_t)c->d[0]);
        wr8(LINK(s), 0xFF);
        wr8(TYPE(s), 0xFF);
    }
    if (appeared(e)) begin_life(e);
}

/* waits for the player to come within $60, then rises out of the ground throwing debris */
static void a141A4(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        call(DIST_X_TO_PLAYER);
        if ((uint16_t)c->d[1] >= 0x60) return;
        place(e);
        clr8(BLINK(e), 8);
        set8(ALIVE(e), 0x40);
        set8(MOVE_FLAGS(e), 3);
        set8(STEP(e), 0x80);
        wr8(TIMER2(e), 0xFF);
        call(FACE_PLAYER);
        return;
    }
    if (!rd8(ANIM_BUSY(e))) { call(ENEMY_NEXT_STATE); return; }
    c->d[0] = set_b(c->d[0], rd8(FRAME_SEEN(e)));
    if ((uint8_t)c->d[0] == rd8(TIMER2(e))) return;
    wr8(TIMER2(e), (uint8_t)c->d[0]);               /* a new frame: debris */
    c->d[4] = 16; c->d[5] = 1;
    c->d[6] = set_w(c->d[6], 0x1208);
    wrw(Y(e), rdw(Y(e)) + 8);
    call(SPAWN_DEBRIS);
    wrw(Y(e), rdw(Y(e)) - 8);
}

/* ---- defeated ---- */

/* $1432C: faded out with the puff, gone when it ends */
static void fade_out(Ent e)
{
    uint8_t t = rd8(TIMER2(e));
    wr8(TIMER2(e), t | 0x80);
    if (!(t & 0x80)) {
        set8(BLINK(e), 8);
        clr8(ALIVE(e), 0x20);
        clr8(BLINK(e), 0x40);
        puff_set(e);
        call_d0(SET_ANIM, 0);
        return;
    }
    if (!rd8(ANIM_BUSY(e))) wrw(ALIVE(e), 0);
}

/* $142AC: still, not blinking, the defeated animation */
static void lie_still(Ent e)
{
    wrw(ANIM_STEP(e), 0x100);
    clr8(ALIVE(e), 0x20);
    clr8(BLINK(e), 0x40);
    call_d0(SET_ANIM, 2);
}

/* $142C4: TIMER frames, then EnemyDefeated; faded out if it was already 0 */
static void count_down(Ent e)
{
    if (!rd8(TIMER(e))) { fade_out(e); return; }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) call(ENEMY_DEFEATED);
}

static void a1429E(Ent e)
{
    if (!begins(e)) { count_down(e); return; }
    wr8(TIMER(e), 0x1E);
    lie_still(e);
}

static void a142D6(Ent e)
{
    if (begins(e)) call(START_BLINK);
    count_down(e);
}

static void blink_down(Ent e) { call(START_BLINK); call_d0(SET_ANIM, 2); }   /* $142EC */

static void a142E4(Ent e)
{
    if (begins(e)) blink_down(e); else count_down(e);
}

static void a142F6(Ent e)
{
    if (!begins(e)) { count_down(e); return; }
    c->a[2] = (uint32_t)(int32_t)rdw(LINK2(e));     /* its partner goes too */
    wrw(ALIVE(c->a[2]), 0);
    blink_down(e);
}

static void a14308(Ent e)
{
    if (begins(e)) { wr8(TIMER(e), 0); lie_still(e); return; }
    if (rd8(TIMER(e))) { fade_out(e); return; }
    if (rd8(ANIM_BUSY(e))) return;
    wr8(TIMER(e), 0xFF);
    call(ENEMY_DEFEATED);
}

/* ---- hurt (batch 8) ---- */

/* a new state (d0 as the caller left it), as `bra EnemySetState` does: the action of that state
 * runs, and returns to this action's caller. Called from a subroutine (`nested`, reached by bsr
 * in the original), the new action returns into the caller's code instead, which carries on */
static void new_state(int nested)
{
    if (nested) { call(ENEMY_SET_STATE); return; }
    EnemySetState(c);
    went_on = 1;
}

/* the hit records cleared, the damage taken: 0 if that beat it (then state 1, defeated) */
static int take_damage(Ent e)
{
    wr32(HIT_A(e), 0);
    wr32(HIT_B(e), 0);
    call(DAMAGE_ENEMY);
    if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return 0; }
    return 1;
}

/* $144E8: each frame while hurt: touching the player, and hit again (state 1 if that beats it);
 * after TIMER frames it stops blinking and goes on to the next state */
static void hurt_tick(Ent e, int nested)
{
    call(PLAYER_TOUCH);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = set_b(c->d[0], 1); new_state(nested); return; }
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    clr8(BLINK(e), 0x40);
    clr8(ALIVE(e), 0x20);
    call(ENEMY_NEXT_STATE);
}

/* $144E4: moved, then the frame's hurt */
static void hurt_move(Ent e) { call(ENEMY_MOVE); hurt_tick(e, 0); }

/* $144D4: knocked back: slowed on the ground, falling */
static void hurt_knocked(Ent e)
{
    call(CHECK_GROUNDED);
    if (c->ccr & CC_Z) call_d0(FRICTION_X, 64);
    call(ACCEL_Y);
    hurt_move(e);
}

static void a1436C(Ent e)
{
    if (!begins(e)) { hurt_tick(e, 0); return; }
    if (!take_damage(e)) return;
    call(START_BLINK);
    call_d0(SET_ANIM, 4);
}

static void a14394(Ent e)
{
    if (!begins(e)) { hurt_tick(e, 0); return; }
    clr8(BLINK(e), 0x40);
    if (!take_damage(e)) return;
    call(START_BLINK_FROZEN);
}

static void a143BC(Ent e)
{
    if (!begins(e)) { hurt_knocked(e); return; }
    clr8(BLINK(e), 0x40);
    if (!take_damage(e)) return;
    wr8(TIMER(e), 0x1E);
    call(KNOCK_BACK);
    call_d0(SET_ANIM, 4);
}

static void a143F2(Ent e)
{
    if (!begins(e)) { hurt_knocked(e); return; }
    wrw(ANIM_STEP(e), 0x100);
    if (!take_damage(e)) return;
    call(START_BLINK_FROZEN);
    call(KNOCK_BACK);
}

static void a14420(Ent e)
{
    if (!begins(e)) {
        call(SLOW_ON_GROUND);
        call(ACCEL_Y);
        hurt_move(e);
        return;
    }
    if (!take_damage(e)) return;
    call_d0(SET_ANIM, 6);
    call(START_BLINK_FROZEN);
    call(KNOCK_BACK);
}

/* in water: drifts, sometimes dropping a bubble, and comes to rest at step 3 or 7 */
static void a14456(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        if (!take_damage(e)) return;
        call(START_BLINK_FROZEN);
        if (!(rd8(WORLD_FLAGS) & 1)) { call(KNOCK_BACK); return; }
        set_d0w(0x100);
        call(KNOCK_BACK_AT);
        return;
    }
    if (!(rd8(WORLD_FLAGS) & 1)) { hurt_knocked(e); return; }
    c->d[0] = rd8(TIMER(e)) & 15;
    if (!c->d[0]) {
        call(RANDOM);
        c->d[0] = set_w(c->d[0], c->d[0] & 3);
        if (!(c->d[0] & 3)) call(BUBBLE);
    }
    call_d0(FRICTION_X, 32);
    call(ACCEL_Y);
    call(MOVE_WITH_PLATFORMS);
    hurt_tick(e, 1);
    if (rd8(TIMER(e))) return;
    c->d[1] = 3;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) c->d[1] = 7;
    wr8(STEP(e), (uint8_t)c->d[1]);
}

/* ---- walking, hovering, shooting (batch 9) ---- */

/* $43BC, EnemyCheckHit: hit, so state 2 (hurt). The original drops this action's return and
 * jumps to EnemySetState; here it answers 1 and the action returns */
static int hit(void)
{
    call(ENEMY_TAKE_HIT);
    if (!(c->ccr & CC_N)) return 0;
    c->d[0] = 2;
    new_state(0);
    return 1;
}

/* $48C4, DespawnIfOffscreen (objects.c), which also leaves past the action */
static int despawned(void) { return world_despawn_offscreen(c); }

/* $15182: a shot (a2), turned as it is; mi if there was no room for one */
static void shoot(void) { call(SHOOT); }

/* $14518: stands for a random while, then on */
static void stand_frame(Ent e)
{
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (!rd8(TIMER2(e))) call(ENEMY_NEXT_STATE);
}

static void a14518(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
    }
    stand_frame(e);
}

/* $1454C: stands, as $14518, until $168 frames after it appeared (LINK2); then a puff, three
 * shots at once, and gone when the puff ends */
static void a1454C(Ent e)
{
    if (begins(e)) {
        wr8(VAR(e), 0);
        wrw(VEL_X(e), 0);
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
    }
    if (rd8(VAR(e)) & 0x80) {
        if (!rd8(ANIM_BUSY(e))) call(DESPAWN);
        return;
    }
    uint16_t age = (uint16_t)(rdw(FRAME) - rdw(LINK2(e)));
    if (age & 0x8000) age = (uint16_t)-age;
    set_d0w(age);
    if (age < 0x168) { stand_frame(e); return; }
    call(FACE_PLAYER);
    wr8(VAR(e), 0xFF);
    wrw(ANIM_SET(e), 0x1208);
    set8(BLINK(e), 8);
    call_d0(SET_ANIM, 0);
    wr8(TIMER(e), 2);
    do {
        shoot();
        if (c->ccr & CC_N) return;
        Ent s = c->a[2];
        wrw(WORD_3DFE(s), 0);
        wr8(TIMER(s), rd8(TIMER(e)));
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    } while (!(rd8(TIMER(e)) & 0x80));
}

/* $145C4: hovers about its home height, facing the player; when the player is below within $60
 * and $28 to the side, it swoops (state 7) */
static void a145C4(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 14);
        call(RANDOM_DELAY);
        c->d[0] = 0;
        wrw(VEL_X(e), 0);
        wrw(ACCEL_X(e), 0);
        wrw(SPEED_Y(e), 0x100);
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(FACE_PLAYER);
    call_d0(FRICTION_X, 16);
    call(ACCEL_Y);
    call(APPLY_VEL);
    set_d0w(rdw(HOME_Y(e)));
    c->d[1] = 4;
    c->d[2] = set_w(c->d[2], 0x80);
    call(BOB_TOWARD);
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(DIST_Y_TO_TARGET);
    if (c->ccr & CC_C) return;
    if ((uint16_t)c->d[1] >= 0x60) return;
    call(DIST_X_TO_ENTITY);
    if ((uint16_t)c->d[1] > 0x28) return;
    wr8(STEP(e), 7);
}

/* $1463C: walks, facing the player, choosing every TIMER2 frames whether to go on (1 in 4) */
static void a1463C(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
        wr8(TIMER(e), rd8(TIMER2(e)));
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(FACE_PLAYER);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    wr8(TIMER(e), rd8(TIMER2(e)));
    call(RANDOM);
    c->d[0] = set_w(c->d[0], c->d[0] & 3);
    if (!(c->d[0] & 3)) call(ENEMY_NEXT_STATE);
}

/* $14682: stands still shooting, faster up at a player above it, and every other volley
 * faster along */
static void a14682(Ent e)
{
    if (begins(e)) {
        call(RANDOM_TIMER);
        wr8(TIMER2(e), 0);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(FACE_PLAYER);
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    wr8(COLL(e), 2);
    if (rd8(TIMER(e))) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
    }
    if (!(rd8(ALIVE(e)) & 1)) return;
    shoot();
    c->a[0] = (uint32_t)(int32_t)(int16_t)(c->a[2] & 0xFFFF);
    call(DIST_Y_TO_TARGET);
    if ((c->ccr & CC_N) && (int16_t)c->d[1] <= -0x40) {     /* well above it: faster up */
        set_d0w((uint16_t)rdw(SPEED_Y(c->a[0])) >> 1);
        wrw(SPEED_Y(c->a[0]), rdw(SPEED_Y(c->a[0])) + (int16_t)(c->d[0] & 0xFFFF));
    }
    c->a[2] = (uint32_t)(int32_t)(int16_t)(c->a[0] & 0xFFFF);
    c->d[0] = rd8(TIMER2(e)) & 3;
    if (c->d[0]) {
        set_d0w((uint16_t)rdw(SPEED_X(c->a[2])) >> 1);
        wrw(SPEED_X(c->a[2]), rdw(SPEED_X(c->a[2])) + (int16_t)(c->d[0] & 0xFFFF));
    }
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) + 1));
    call(RANDOM_TIMER);
}

/* $146FC: hops (on the ground: a random wait, then a jump), shooting once per hop: on the
 * ground, or (half of them) on the way down */
static void a146FC(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 6);
        wrw(VAR(e), 0);
        call(RANDOM_TIMER_32);
        if (c->d[0] & 4) wr8(VAR(e), 0xFF);
    }
    if (despawned()) return;
    call(FACE_PLAYER);
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (c->ccr & CC_Z) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (!rd8(TIMER(e))) {
            call(RANDOM_TIMER);
            call(JUMP);
            wr8(SHOT(e), 0);
        }
        if (rd8(VAR(e))) return;
    } else {
        if (!rd8(VAR(e))) return;
        if (rdw(VEL_Y(e)) < 0) return;
    }
    if (!(rd8(ALIVE(e)) & 1)) return;
    uint8_t s = rd8(SHOT(e));
    wr8(SHOT(e), s | 1);
    if (!(s & 1)) shoot();
}

/* $1476C: rises $60 above its home, then hangs facing the player until it is below (between
 * $10 and $80 down, $10 to $C0 aside), checked every 8 frames */
static void a1476C(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 0x50);
        c->d[0] = 1;
        if (!rd32(VEL(e))) {
            wr32(VEL(e), 0xFE80u);
            call(FACE_PLAYER);
            c->d[0] = 8;
        }
        wr8(TIMER(e), (uint8_t)c->d[0]);
        set_d0w(rdw(HOME_Y(e)) - 0x60);
        wrw(LINK2(e), (int)(c->d[0] & 0xFFFF));
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 14);
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { wr8(TIMER(e), 1); call(MOVE_WITH_PLATFORMS); }
    if (rdw(VEL_Y(e))) {
        if (rdw(VEL_X(e))) call(FACE_VELOCITY);
        set_d0w(rdw(LINK2(e)));
        if ((uint16_t)rdw(LINK2(e)) < (uint16_t)rdw(Y(e))) return;    /* not up there yet */
    }
    wr32(VEL(e), 0);
    call(FACE_PLAYER);
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    c->d[0] = (uint32_t)rdw(FRAME) & 7;
    if (c->d[0]) return;
    call(DIST_Y_TO_TARGET);
    if (c->ccr & CC_N) return;
    uint16_t dy = (uint16_t)c->d[1];
    if (dy >= 0x80 || dy < 0x10) return;
    call(DIST_X_TO_ENTITY);
    uint16_t dx = (uint16_t)c->d[1];
    if (dx >= 0xC0) return;
    if (dx >= 0x10) call(ENEMY_NEXT_STATE);
}

/* ---- choosing what to do next (batch 10) ---- */

/* $42AA: standing on the ground, facing the player, its wait (TIMER2) over: 0. Otherwise the
 * original drops this action's return (it is done for this frame): 1, and the action returns.
 * The ground test is $4356's, whose flags come from FrictionX when it is on the ground */
static int still_waiting(Ent e)
{
    call(SLOW_ON_GROUND);
    if (!(c->ccr & CC_Z)) return 1;
    call(FACE_PLAYER);
    if (!rd8(TIMER2(e))) return 0;
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    return rd8(TIMER2(e)) != 0;
}

/* $428C: the next step from a table, at d2 (the row: how near the player is) plus a random 0-7:
 * 0 stays, negative goes on to the next state, else that step */
static void pick_step(Ent e, uint32_t table)
{
    c->a[0] = table;
    call(RANDOM);
    c->d[0] = set_w(c->d[0], ((c->d[0] & 7) + c->d[2]) & 0xFFFF);
    uint8_t s = rd8(table + (uint32_t)(int32_t)(int16_t)(c->d[0] & 0xFFFF));
    c->d[0] = set_b(c->d[0], s);
    if (!s) return;
    if (s & 0x80) { call(ENEMY_NEXT_STATE); return; }
    wr8(STEP(e), s);
}

/* the player within $30 up or down and less than `dx` aside: the rows of the tables below */
static int near_target(uint16_t dx)
{
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] >= 0x30) return 0;
    call(DIST_X_TO_ENTITY);
    return (uint16_t)c->d[1] < dx;
}

static void a14822(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    if (!near_target(0x80)) { wr8(STEP(e), 4); return; }
    c->d[2] = (uint16_t)c->d[1] < 0x30 ? 8 : 0;
    pick_step(e, 0x14878);
}

/* with the shield's touch and its own moves (from its kind's stats, $15A3A, $15A76) */
static void a14888(Ent e)
{
    if (begins(e)) {
        wr32(HIT_A(e), 0);
        call(KIND_STATS);
        c->d[0] = set_b(c->d[0], rd8(c->a[1] + 7));
        wr8(TIMER(e), (uint8_t)c->d[0]);
        call(KIND_MOVES);
        call(RANDOM_DELAY);
        call(FACE_PLAYER);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(SHIELD_AND_MOVE);
    if (hit()) return;
    call(BACK_OFF);
    call(SLOW_ON_GROUND);
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    call(FACE_PLAYER);
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] >= 0x30) { wr8(STEP(e), 4); return; }
    call(DIST_X_TO_PLAYER);
    if ((uint16_t)c->d[1] >= 0x80) { wr8(STEP(e), 4); return; }
    c->d[2] = 0;
    if ((uint16_t)c->d[1] < 0x60) c->d[2] = (uint16_t)c->d[1] < 0x28 ? 16 : 8;
    pick_step(e, 0x14912);
}

static void a1492A(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 6);
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    call(DIST_X_TO_PLAYER);
    c->d[2] = (uint16_t)c->d[1] < 0x60 ? 8 : 0;
    pick_step(e, 0x14964);
}

static void a14974(Ent e)
{
    if (begins(e)) {
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    c->d[2] = 0;
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] < 0x30) {
        c->d[2] = 8;
        call(DIST_X_TO_ENTITY);
        if ((uint16_t)c->d[1] >= 0x60) c->d[2] = 16;
    }
    pick_step(e, 0x149B8);
}

/* walks back to its home x, then stands facing the player; near the player (within $20 up or
 * down, $40 aside) it goes to step 4 */
static void a149D0(Ent e)
{
    if (begins(e)) {
        wr8(TIMER(e), 0xFF);
        wrw(ANIM_STEP(e), 0x200);
        call(RANDOM_DELAY);
        set_d0w(rdw(HOME_X(e)));
        if (rdw(HOME_X(e)) == rdw(X(e))) { wrw(VEL_X(e), 0); c->d[0] = 0; }
        else { call(FACE_X); call(WALK_FORWARD); c->d[0] = 8; }
        call(SET_ANIM);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    set_d0w(rdw(HOME_X(e)));
    int16_t v = rdw(VEL_X(e));
    if (v) {
        uint16_t home = (uint16_t)rdw(HOME_X(e)), x = (uint16_t)rdw(X(e));
        set_d0w((uint16_t)(home - x));
        int there = v < 0 ? home >= x : home < x;   /* reached or passed it */
        if (there) {
            wrw(VEL_X(e), 0);
            if (rd8(ANIM_ID(e))) {
                call(FACE_PLAYER);
                call_d0(SET_ANIM, 6);
                wr8(TIMER(e), 0x1E);
            }
        }
    }
    if (!(rd8(TIMER(e)) & 0x80)) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e)) & 0x80) call_d0(SET_ANIM, 0);
    }
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(DIST_Y_ABS_TARGET);
    if ((uint8_t)c->d[1] >= 0x20) return;           /* cmpi.b: the low byte */
    call(DIST_X_TO_ENTITY);
    if ((uint16_t)c->d[1] >= 0x40) return;
    wr8(STEP(e), 4);
}

/* turns to the player (on to the next state) when facing it, close by */
static void a14A82(Ent e)
{
    if (begins(e)) {
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] >= 0x30) { wr8(STEP(e), 4); return; }
    call(DIST_X_TO_ENTITY);                         /* d0: 8 if the player is to the left */
    if ((uint16_t)c->d[1] < 0x30) {
        c->d[1] = rd8(ATTR(e)) & 8;
        if ((uint8_t)c->d[0] == (uint8_t)c->d[1]) { call(ENEMY_NEXT_STATE); return; }
    }
    call(RANDOM);
    c->d[0] = set_w(c->d[0], c->d[0] & 7);
    c->d[0] = set_b(c->d[0], rd8(0x14AE2u + (c->d[0] & 7)));
    if ((uint8_t)c->d[0]) wr8(STEP(e), (uint8_t)c->d[0]);
}

/* $14AEA and $14AFA: as $14974, the near row by a distance of their own (VAR: $28 or $40) */
static void near_by_range(Ent e)
{
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    c->d[2] = 0;
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] < 0x30) {
        c->d[2] = 8;
        call(DIST_X_TO_ENTITY);
        if ((uint16_t)c->d[1] < (uint16_t)rdw(VAR(e))) c->d[2] = 16;
    }
    pick_step(e, 0x14B44);
}

static void ranged_start(Ent e, int range)
{
    wrw(VAR(e), range);
    call_d0(SET_ANIM, 6);
    call(RANDOM_DELAY);
}

static void a14AEA(Ent e) { if (begins(e)) ranged_start(e, 0x28); near_by_range(e); }
static void a14AFA(Ent e) { if (begins(e)) ranged_start(e, 0x40); near_by_range(e); }

/* in water or on land; off the ground it goes to step 7. Its table's rows: near, middle, far,
 * and the same again in water */
static void a14B7E(Ent e)
{
    if (!begins(e)) {                               /* the original's test is this way round */
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
    }
    call(MAYBE_BUBBLE);
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    call(MOVE_WITH_PLATFORMS);
    if (hit()) return;
    call(SLOW_ON_GROUND);
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) { wr8(STEP(e), 7); return; }
    call(DIST_X_TO_PLAYER);
    c->d[2] = 0;
    int check_y = 1;
    if ((uint16_t)c->d[1] >= 0x30) {
        c->d[2] = 8;
        if ((uint16_t)c->d[1] < 0x60) check_y = 0;
    }
    if (check_y) {
        call(DIST_Y_ABS);
        if ((uint16_t)c->d[1] >= 0x28) c->d[2] = 16;
    }
    if (rd8(WORLD_FLAGS) & 1) c->d[2] = set_w(c->d[2], (c->d[2] + 0x18) & 0xFFFF);
    pick_step(e, 0x14BEA);
}

/* waits for the player within $40, then falls for 30 frames */
static void a14C1A(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        call(DIST_X_TO_PLAYER);
        if ((uint16_t)c->d[1] >= 0x40) return;
        set8(STEP(e), 0x80);
        wr8(TIMER(e), 0x1E);
    }
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(ACCEL_Y);
    call(APPLY_VEL_Y);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) call(ENEMY_NEXT_STATE);
}

/* the actions, registered by their addresses (registry.c). call_68k leaves its mark in ret: it
 * is cleared unless the action went on elsewhere */
#define ACTION(addr) void Act##addr(Cpu *cpu) { c = cpu; went_on = 0; a##addr((Ent)c->a[4]); if (!went_on) c->ret = 0; }
ACTION(140F2) ACTION(140FC) ACTION(1410E) ACTION(14120) ACTION(1416E) ACTION(141A4)
ACTION(1429E) ACTION(142D6) ACTION(142E4) ACTION(142F6) ACTION(14308)
ACTION(1436C) ACTION(14394) ACTION(143BC) ACTION(143F2) ACTION(14420) ACTION(14456)
ACTION(14518) ACTION(1454C) ACTION(145C4) ACTION(1463C) ACTION(14682) ACTION(146FC) ACTION(1476C)
ACTION(14822) ACTION(14888) ACTION(1492A) ACTION(14974) ACTION(149D0) ACTION(14A82) ACTION(14AEA)
ACTION(14AFA) ACTION(14B7E) ACTION(14C1A)
