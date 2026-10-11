/* The player's states, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * Each frame the player's update ($DD08) runs the handler for the player's state ($E2C6):
 * standing, walking, in the air, in water, ducking, climbing a ladder, holding on to a vine, and
 * so on. A handler looks at the buttons and the ground and either acts or hands over to a check
 * (jump, walk, duck, start climbing...). In the original each check ends the handler itself when
 * it acts, by unlinking the handler's stack frame (unlk a6; rts): here a check returns 1 and the
 * handler returns.
 *
 * Not yet rewritten: the rest of the player's update (attacks, items, damage, death), which
 * still calls this. */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

/* ---- the player's entity fields (a4 = the player) ---- */
#define X(e)          ((e) - 0x3800)
#define Y(e)          ((e) - 0x3700)
#define VEL_X(e)      ((e) - 0x3600)   /* 8.8; written as a long with VEL_Y to stop */
#define VEL_Y(e)      ((e) - 0x35FE)
#define HALF_H(e)     ((e) - 0x34FD)
#define ACCEL_X(e)    ((e) - 0x3400)
#define SWIM_DRAG(e)  ((e) - 0x33FE)   /* the deceleration FrictionY takes in water */
#define SPEED(e)      ((e) - 0x3300)
#define FLAGS(e)      ((e) - 0x3200)   /* bits below */
#define STATE(e)      ((e) - 0x31FF)   /* which handler runs */
#define COLL(e)       ((e) - 0x3100)   /* movement.c's collision result */
#define PREV_COLL(e)  ((e) - 0x2EFF)
#define PLATFORM(e)   ((e) - 0x2D00)
#define FACING(e)     ((e) - 0x3FFD)   /* the sprite attribute's low byte; bit 3 faces left */
#define ATTACK_STEP(e) ((e) - 0x2C00)
#define ATTACK_BOX(e) ((e) - 0x2A00)   /* long: the attack's hit-box list (or 0) */
#define TIMER(e)      ((e) - 0x2800)   /* byte: how long the current state lasts; climbing: offset in the rung */
#define CLIMB(e)      ((e) - 0x27FF)   /* byte: climbing: how far up the cell; vine: -1 hanging on */
#define CLIMB_W(e)    ((e) - 0x27FE)   /* word, cleared on grabbing a vine */
#define VINE_PUSH(e)  ((e) - 0x27FD)   /* byte: frames pushing sideways on a vine (bit 7: left) */
#define BUBBLES(e)    ((e) - 0x25FE)

enum { F_SWIMMING = 0x04, F_DUCKING = 0x08, F_IN_AIR = 0x10, F_ATTACKING = 0x20, F_HURT = 0x40 };
enum { S_HURT, S_STAND, S_WALK, S_AIR, S_WATER, S_DUCK, S_UP_ATTACK, S_LADDER, S_VINE, S_STAND2 };

/* ---- the player's globals ---- */
#define AREA_FLAGS    0xFFFF966Bu   /* bit 0 under water, bit 4 vines on platforms */
#define TASK_FLAGS    0xFFFF9659u
#define FRAME         0xFFFF965Au
#define FORM          0xFFFF9F04u   /* 2: a form that cannot duck */
#define ANIM          0xFFFF9F02u   /* for the animation: bit 0 moving, 1 facing left, 2 climbing,
                                     * 3 going up, 4 changed state, 6 walking */
#define GROUND        0xFFFF9F03u   /* bit 0 on the ground */
#define CTRL          0xFFFF9F06u   /* bit 0 no friction, 1 no acceleration, 4 the up attack,
                                     * 5/6 used an item */
#define CLIMB_DELAY   0xFFFF9F07u   /* frames before it can grab a ladder or vine again */
#define DUCK_DELAY    0xFFFF9F09u
#define HELD          0xFFFF9F0Bu   /* buttons: 0 up 1 down 2 left 3 right 4 B 5 C 6 A 7 start */
#define PRESSED       0xFFFF9F0Du   /* pressed this frame */
#define ATTACK_TIME    0xFFFF9F0Eu
#define POSE          0xFFFF9F0Fu
#define LADDER_TOP    0xFFFF9F12u   /* byte: where on the player a ladder is caught */
#define ATTACK_SCRIPT  0xFFFF9F2Cu   /* long, and its step at $9F2A */
#define ATTACK_STEP_N    0xFFFF9F2Au
#define JUMP_SPEED    0xFFFF9EF2u
#define CLIMB_SPEED   0xFFFF9EF4u
#define SWIM_SPEED    0xFFFF9EF6u   /* word (and its neighbour as a long) */
#define ACCEL         0xFFFF9EFEu
#define EQUIP         0xFFFF9EF1u   /* equipment bits (TileTypeOf); bit 5 also keeps it out of the water state */
#define TILE_BELOW    0xFFFF9EDEu
#define TILE_FEET     0xFFFF9EE0u
#define TILE_HERE     0xFFFF9EE2u
#define ITEM_UP       0xFFFF9980u   /* has an item for up+A ... */
#define ITEM_DOWN     0xFFFF9981u   /* ... and down+A; their routines: */
#define ITEM_UP_FN    0xFFFF998Au
#define ITEM_DOWN_FN  0xFFFF998Eu
#define SWORD         0xFFFF95CEu   /* which sword (docs/NOTES.md) */
#define DOOR          0xFFFF9F7Au

/* platforms (movement.c), from a2 = 0, 4 .. 20 */
#define P_FLAGS(p)    ((uint32_t)(int32_t)(p) - 0x6072)
#define P_ATTR(p)     ((uint32_t)(int32_t)(p) - 0x6071)
#define P_X(p)        ((uint32_t)(int32_t)(p) - 0x601E)
#define P_Y(p)        ((uint32_t)(int32_t)(p) - 0x601C)
#define P_DY(p)       ((uint32_t)(int32_t)(p) - 0x5FC8)
#define P_HALF_W(p)   ((uint32_t)(int32_t)(p) - 0x5F76)
#define P_HALF_H(p)   ((uint32_t)(int32_t)(p) - 0x5F74)

#define LADDER        0x4000          /* collision types: a ladder; with bit 3 its top */
#define WATER         0x7000

static Cpu *cpu;                      /* the call into C: a real stack for calls into the 68000 */

static uint8_t held(void)    { return rd8(HELD); }
static uint8_t pressed(void) { return rd8(PRESSED); }
static int on_ground(void)   { return rd8(GROUND) & 1; }
static void set_state(Ent e, int s) { wr8(STATE(e), (uint8_t)s); }
static void stop(Ent e) { wr32(VEL_X(e), 0); }

/* left or right held (not with A): face that way and move */
static int face_held_way(void)
{
    if (held() & 0x40) return 0;
    if (held() & 0x04) set8(ANIM, 2);
    else if (held() & 0x08) clr8(ANIM, 2);
    else return 0;
    set8(ANIM, 1);
    return 1;
}

static void queue_sound(int id)
{
    Cpu t = *cpu;
    t.d[0] = (uint32_t)id;
    call_68k(&t, 0x366);
}

/* ---- the checks: each returns 1 if it acted (the handler ends there) ---- */

/* $E9FA: attacking */
static int attacking(Ent e) { return rd8(FLAGS(e)) & F_ATTACKING; }

/* $EA06: into water (unless it breathes there) */
static int enter_water(Ent e)
{
    if (rd16(TILE_HERE) != WATER || (rd8(EQUIP) & 0x20)) return 0;
    set_state(e, S_WATER);
    return 1;
}

static void stand(Ent e) { set_state(e, S_STAND); }       /* $EA22 */

/* $DF2E: up+A or down+A uses the item for it (a routine of its own) */
static int use_item(Ent e)
{
    if ((rd8(FLAGS(e)) & F_HURT) || !on_ground()) return 0;
    uint8_t dir = pressed() & 0xF;
    uint32_t fn;
    if (rd8(ITEM_UP) && dir == 1) { set8(CTRL, 0x20); fn = rd32(ITEM_UP_FN); }
    else if (rd8(ITEM_DOWN) && dir == 2) { set8(CTRL, 0x40); fn = rd32(ITEM_DOWN_FN); }
    else return 0;
    wrw(VEL_X(e), 0);
    Cpu t = *cpu;
    t.a[4] = e;
    call_68k(&t, fn);
    return 1;
}

/* $EDCC: standing on a tile of type $8xxx or $Axxx */
static int on_8_or_a(void)
{
    uint16_t t = rd16(TILE_BELOW) & 0xF000;
    return t == 0x8000 || t == 0xA000;
}

/* $EA2A, $EA6E: up+B on the ground, with swords 4-6, starts the up attack */
static int start_up_attack(Ent e)
{
    if ((held() & 0x40) || on_8_or_a() || (rd8(EQUIP) & 0x10)) return 0;
    if (!(0x70 >> (rd8(SWORD) & 31) & 1)) return 0;
    if (!on_ground() || (held() & 0xF) != 1 || !(pressed() & 0x10)) return 0;
    set8(CTRL, 0x10);
    wr32(ATTACK_SCRIPT, 0xF5C8);
    wrw(ATTACK_STEP_N, 0);
    wr8(ATTACK_TIME, 0x1E);
    set_state(e, S_UP_ATTACK);
    queue_sound(0x31);
    return 1;
}

/* $EA92: down on the ground ducks */
static int duck(Ent e)
{
    if (rd8(FORM) == 2 || rd8(DUCK_DELAY) || !on_ground() || (held() & 0xF) != 2) return 0;
    wr8(DUCK_DELAY, 4);
    set_state(e, S_DUCK);
    return 1;
}

/* $EACA: down on the top of a ladder (its middle third) climbs down it */
static int climb_down(Ent e)
{
    if (!on_ground() || !(held() & 2)) return 0;
    set8(ANIM, 4);
    uint16_t t = world_type_at_pixel(e, rdw(X(e)), (int16_t)(rdw(Y(e)) + rd8(HALF_H(e))));
    if ((t & 0xF00F) != (LADDER | 8)) return 0;
    int16_t in_cell = rdw(X(e)) & 15;
    if (in_cell < 4 || in_cell > 12) return 0;
    wrw(X(e), rdw(X(e)) + 8 - in_cell);             /* to the ladder's middle */
    wrw(TIMER(e), 0);
    wr8(COLL(e), 0);
    stop(e);
    wrw(ACCEL_X(e), 0);
    clr8(ANIM, 8);
    set_state(e, S_LADDER);
    set8(CTRL, 3);
    return 1;
}

/* $EB4C: left or right walks */
static int walk(Ent e)
{
    if (held() & 0x40) return 0;
    uint8_t lr = held() & 0xC;
    if (!lr) return 0;
    if (lr & 4) set8(ANIM, 2); else clr8(ANIM, 2);
    set8(ANIM, 1);
    set_state(e, S_WALK);
    if (!on_ground()) return 0;
    set8(ANIM, 0x40);
    return 1;
}

/* $EC08, $EBFC, $EBF2: leave the ground upwards */
static void launch(Ent e, int16_t vy)
{
    wr8(COLL(e), rd8(COLL(e)) & 0x75);
    wrw(VEL_Y(e), vy);
}

static void take_off(Ent e, int16_t vy)
{
    set8(FLAGS(e), F_IN_AIR);
    set_state(e, S_AIR);
    launch(e, vy);
}

static void jump(Ent e)
{
    queue_sound(0x1E);
    take_off(e, rdw(JUMP_SPEED));
}

/* $EB9E: C jumps (on land, not from $8xxx tiles); under water C swims up, or from the ground
 * jumps */
static int jump_or_swim(Ent e)
{
    if (rd8(AREA_FLAGS) & 1) {
        if (!(pressed() & 0x20)) return 0;
        if (on_ground()) take_off(e, rdw(JUMP_SPEED));
        else { set8(FLAGS(e), F_SWIMMING); set_state(e, S_WATER); queue_sound(0x4A); }   /* $EC14 */
        return 1;
    }
    if (!(pressed() & 0x20) || !on_ground()) return 0;
    if ((rd16(TILE_BELOW) & 0xF000) == 0x8000) return 0;
    jump(e);
    return 1;
}

/* $ED18: grab the vine, centred on x */
static void grab_vine(Ent e, int16_t x)
{
    set_state(e, S_VINE);
    wrw(X(e), x);
    wr8(CLIMB(e), 0);
    stop(e);
    wrw(ACCEL_X(e), 0);
    wrw(CLIMB_W(e), 0);
    set8(ANIM, 0x10);
    set8(CTRL, 3);
    if (on_ground()) wrw(Y(e), rdw(Y(e)) - 1);
    wr8(COLL(e), rd8(COLL(e)) & 0x20);
    set8(ANIM, 4);
}

/* $EC26: up grabs a ladder (from its bottom: onto it) or a vine, of the map or on a platform */
static int grab(Ent e)
{
    if (rd8(CLIMB_DELAY) || !(held() & 1)) return 0;
    int16_t x = rdw(X(e));
    int16_t y = (int16_t)(rdw(Y(e)) + (int8_t)(rd8(HALF_H(e)) - rd8(LADDER_TOP)));
    uint16_t t = world_type_at_pixel(e, x, y);
    if ((t & 0xF000) == LADDER) {
        int16_t in_cell = x & 15;
        if (in_cell >= 4 && in_cell <= 12) {
            if ((t & 0xF008) == (LADDER | 8) && (y & 15) < 5) {   /* the bottom of a ladder */
                wrw(X(e), x + 8 - in_cell);
                wr8(TIMER(e), (uint8_t)(y & 15));
                wr8(CLIMB(e), rd8(LADDER_TOP));
                stop(e);
                set8(ANIM, 0x18);
                set_state(e, S_LADDER);
                return 1;
            }
            grab_vine(e, (int16_t)(x + 8 - in_cell));
            return 1;
        }
    }
    if (!(rd8(AREA_FLAGS) & 0x10)) return 0;
    for (int p = 0; p < 24; p += 4) {               /* a vine hanging from a platform */
        if (!(rd8(P_FLAGS(p)) & 0x80)) continue;
        if (world_tile_type(rd8(P_ATTR(p)), e) != LADDER) continue;
        int16_t dy = (int16_t)(rdw(P_Y(p)) - rdw(Y(e)));
        if ((uint16_t)(dy < 0 ? -dy : dy) > (uint16_t)rdw(P_HALF_H(p))) continue;
        int16_t dx = (int16_t)(rdw(P_X(p)) - rdw(X(e)));
        if ((uint16_t)(dx < 0 ? -dx : dx) > (uint16_t)rdw(P_HALF_W(p))) continue;
        wrw(PLATFORM(e), p);
        set8(P_FLAGS(p), 1);
        set8(COLL(e), 0x20);
        grab_vine(e, rdw(P_X(p)));
        return 1;
    }
    return 0;
}

/* ---- the handlers ---- */

/* 0, $E346: hurt; blinks, then stands */
static void state_hurt(Ent e)
{
    wr8(POSE, 0x80);
    if (!(rd16(FRAME) & 7)) wr8(BUBBLES(e), 0);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    clr8(FACING(e), 0x40);
    clr8(FLAGS(e), F_HURT);
    set_state(e, S_STAND);
}

/* 1 and 9, $E374: standing */
static void state_stand(Ent e)
{
    if (attacking(e) || enter_water(e) || use_item(e) || start_up_attack(e) || grab(e)
        || climb_down(e) || jump_or_swim(e) || walk(e) || duck(e)) return;
    if (!on_ground()) return;
    set8(ANIM, 0x40);
    if (rd8(DOOR) & 0x80) return;
    if ((rd8(AREA_FLAGS) & 1) || !(rd8(e - 0x2F00) & 0x80)) clr8(TASK_FLAGS, 4);
}

/* 2, $E3C4: walking */
static void state_walk(Ent e)
{
    if (!(rd8(FLAGS(e)) & F_ATTACKING)
        && (enter_water(e) || grab(e) || climb_down(e) || jump_or_swim(e) || walk(e) || duck(e))) return;
    stand(e);
}

/* 3, $E3EC: in the air */
static void state_air(Ent e)
{
    if (enter_water(e)) return;
    if (rd8(FLAGS(e)) & F_ATTACKING) { stand(e); return; }
    if (on_ground()) {                              /* landed */
        if (jump_or_swim(e) || walk(e)) return;
        stand(e);
        return;
    }
    set8(FLAGS(e), F_IN_AIR);
    if (grab(e)) return;
    face_held_way();
    jump_or_swim(e);
}

/* 4, $E44C: in water: swims slowly; C jumps out; drifts up in deep water */
static void state_water(Ent e)
{
    if ((rd8(EQUIP) & 0x20) || rd16(TILE_FEET) != WATER) {   /* out of the water */
        set8(FLAGS(e), F_IN_AIR);
        set_state(e, S_AIR);
        return;
    }
    if (face_held_way()) {
        wrw(ACCEL, rdw(ACCEL) >> 2);                /* a quarter of the acceleration */
    }
    if (pressed() & 0x20) { jump(e); return; }
    wrw(SWIM_DRAG(e), 3);
    if (rd16(TILE_HERE) == WATER) launch(e, -0x48);
}

/* 4 in form 1, $E4CA: swimming (a form that can) */
static void state_swim(Ent e)
{
    if (on_ground()) {
        if (rd8(FLAGS(e)) & F_ATTACKING) {
            set8(CTRL, 0x40);
            clr8(FLAGS(e), F_SWIMMING);
        } else {
            if (!(held() & 2)) goto swimming;
            clr8(FLAGS(e), F_SWIMMING);
            if (jump_or_swim(e)) return;
            set8(CTRL, 0x40);
            if (walk(e)) return;
        }
        stand(e);
        return;
    }
swimming:
    set8(FLAGS(e), F_SWIMMING);
    if (grab(e)) return;
    if (pressed() & 0x20) {                         /* a stroke */
        queue_sound(0x4A);
        wrw(VEL_Y(e), rdw(JUMP_SPEED) >> 1);
        if (held() & 1) wrw(VEL_Y(e), rdw(VEL_Y(e)) + rdw(JUMP_SPEED));
        else if (held() & 2) {
            int16_t drag = rdw(SWIM_DRAG(e));
            wrw(SWIM_DRAG(e), 2 * drag + (drag >> 1));
        }
        if (face_held_way()) {
            wrw(ACCEL, rdw(ACCEL) << 3);
            wrw(SPEED(e), 2 * rdw(SWIM_SPEED));
        }
        return;
    }
    if ((held() & 0xF) == 1) {                      /* up alone: slow the sinking */
        set8(CTRL, 2);
        Cpu t = *cpu;
        t.a[4] = e; t.d[0] = (uint16_t)rdw(SWIM_DRAG(e));
        call_68k(&t, 0xBAC);                        /* FrictionY */
    } else if (held() & 2) {
        wrw(SWIM_DRAG(e), rdw(SWIM_DRAG(e)) + (rdw(SWIM_DRAG(e)) >> 1));
    }
    face_held_way();
}

/* 5, $E5FC: ducking */
static void state_duck(Ent e)
{
    set8(POSE, 0x80);
    if (on_ground()) {
        if (held() & 2) { set8(FLAGS(e), F_DUCKING); return; }
        if (attacking(e) || jump_or_swim(e)) return;
    }
    if (walk(e)) return;
    stand(e);
}

/* 6, $E98A: the up attack: its hit points ($EE70) each frame until the time runs out; up+B again restarts it */
static void state_up_attack(Ent e)
{
    if (!(held() & 0x40)) {
        wr8(POSE, 2);
        if ((held() & 0xF) == 1 && (pressed() & 0x10)) {   /* up+B again: start over */
            queue_sound(0x31);
            wr8(ATTACK_TIME, 0x1E);
        }
        set8(CTRL, 0x10);
        wr32(ATTACK_BOX(e), 0xF400);
        Cpu t = *cpu;
        t.a[4] = e; t.a[0] = 0xE9F2 + 2 * rd8(ATTACK_STEP(e));
        call_68k(&t, 0xEE70);
    }
    wr8(ATTACK_TIME, (uint8_t)(rd8(ATTACK_TIME) - 1));
    if (rd8(ATTACK_TIME)) return;
    wr32(ATTACK_BOX(e), 0);
    wr8(POSE, 0);
    if (walk(e)) return;
    stand(e);
}

/* 7, $E62E: on a ladder: up and down by twice the climbing speed; TIMER and CLIMB count the
 * pixels to the next rung or the end */
static void state_ladder(Ent e)
{
    wrw(VEL_Y(e), 0);
    set8(POSE, 0x80);
    set8(ANIM, 4);
    set8(CTRL, 3);
    uint16_t step = (uint16_t)(2 * rdw(CLIMB_SPEED));
    if (rd8(ANIM) & 8) {                            /* climbing up */
        int16_t y = (int16_t)(rdw(Y(e)) - step);
        if (rd8(TIMER(e))) {
            uint16_t left = (uint16_t)(rd8(TIMER(e)) - step);
            if ((uint16_t)rd8(TIMER(e)) <= step) {  /* past it: the rest from CLIMB */
                y = (int16_t)(y - (int16_t)left);
                left = 0;
                wr8(CLIMB(e), rd8(LADDER_TOP));
            }
            wrw(Y(e), y);
            wr8(TIMER(e), (uint8_t)left);
            return;
        }
        uint16_t left = (uint16_t)(rd8(CLIMB(e)) - step);
        if ((uint16_t)rd8(CLIMB(e)) <= step) {      /* off the top */
            y = (int16_t)(y - (int16_t)left);
            clr8(FLAGS(e), F_ATTACKING);
            set8(COLL(e), 2);
            clr8(CTRL, 3);
            clr8(ANIM, 4);
            set8(ANIM, 0x10);
            set_state(e, S_STAND);
        }
        wr8(CLIMB(e), (uint8_t)left);
        wrw(Y(e), y);
        return;
    }
    int8_t climb = (int8_t)rd8(CLIMB(e));            /* climbing down */
    if (climb < 0) {                                /* stepping off the bottom */
        wrw(Y(e), rdw(Y(e)) + step);
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) + step));
        if (rd8(TIMER(e)) < 4) return;
        wr8(CLIMB(e), 8);
        wrw(CLIMB_W(e), 0);
        set8(ANIM, 0x10);
        set_state(e, S_VINE);
        return;
    }
    int16_t y = (int16_t)(rdw(Y(e)) + step);
    uint8_t c = (uint8_t)(climb + step);
    if (c >= rd8(LADDER_TOP)) {                     /* at the bottom */
        y = (int16_t)(y - (int8_t)(c - rd8(LADDER_TOP)));
        c = 0xFF;
    }
    wr8(CLIMB(e), c);
    wrw(Y(e), y);
    wrw(e - 0x2DFE, y);                             /* PREV_Y */
    wr8(COLL(e), rd8(COLL(e)) & 0x75);
}

/* $E89A: let go of the vine */
static void let_go(Ent e)
{
    clr8(CTRL, 3);
    clr8(FLAGS(e), F_ATTACKING);
    clr8(ANIM, 4);
    set8(ANIM, 0x10);
    set8(CTRL, 0x40);
    set_state(e, S_STAND);
}

/* $E8C2: on a vine hanging from a platform: carried with it */
static void vine_on_platform(Ent e)
{
    wrw(VEL_Y(e), 0);
    int p = rdw(PLATFORM(e));
    set8(P_FLAGS(p), 1);
    if (!(rd8(COLL(e)) & 1) && !on_ground()) {
        set8(COLL(e), 0x20);
        if (rd8(P_FLAGS(p)) & 0x80) {
            int16_t y = (int16_t)(rdw(Y(e)) + rdw(P_DY(p)));
            int16_t d = (int16_t)(y - rdw(P_Y(p)));
            if ((uint16_t)(d < 0 ? -d : d) <= (uint16_t)rdw(P_HALF_H(p))) {
                int16_t v = rdw(CLIMB_SPEED);
                if (held() & 2) clr8(ANIM, 8);
                else if (!(held() & 1)) return;
                else {
                    set8(ANIM, 8);
                    v = (int16_t)-v;
                    int16_t top = (int16_t)(rdw(P_Y(p)) - rdw(P_HALF_H(p)));
                    if ((int16_t)(top - y) >= v) {  /* at the platform: stay just under it */
                        wr8(CLIMB(e), (uint8_t)(rd8(CLIMB(e)) + v));
                        wrw(Y(e), top - rdw(P_DY(p)));
                        return;
                    }
                }
                wr8(CLIMB(e), (uint8_t)(rd8(CLIMB(e)) + v));
                wr8(VEL_Y(e), (uint8_t)v);           /* whole pixels: the high byte */
                return;
            }
        }
    }
    clr8(P_FLAGS(p), 1);
    wr8(CLIMB_DELAY, 8);
    let_go(e);
    clr8(COLL(e), 0x20);
}

/* 8, $E71C: on a vine: up and down; pushing left or right for 8 frames jumps off */
static void state_vine(Ent e)
{
    set8(POSE, 0x80);
    set8(ANIM, 4);
    if (on_ground()) { let_go(e); return; }
    uint8_t push = rd8(VINE_PUSH(e));
    int sideways = 0;
    int16_t vx = 0;
    if (!(held() & 0x40) && (held() & 0xC)) {
        uint8_t dir = held() & 0xF;
        if (dir == 8) { vx = 0x100; sideways = (int8_t)push >= 0 ? 1 : -1; }
        else if (dir == 4) {
            vx = -0x100;
            if ((int8_t)push < 0) sideways = 1;
            else { wr8(VINE_PUSH(e), 0x81); sideways = 2; }
        } else sideways = 2;                        /* both, or with up/down: keep the count */
    } else sideways = -1;
    if (sideways == 1) {
        push++;
        wr8(VINE_PUSH(e), push);
        if ((int8_t)((push & 0x7F) - 8) >= 0) {     /* jump off */
            wrw(VEL_X(e), vx);
            set8(FLAGS(e), F_IN_AIR);
            wrw(VEL_Y(e), rdw(JUMP_SPEED) >> 2);
            wr8(CLIMB_DELAY, 8);
            set_state(e, S_AIR);
            wr8(COLL(e), rd8(COLL(e)) & 0x75);
            clr8(ANIM, 4);
            return;
        }
    } else if (sideways == -1) wr8(VINE_PUSH(e), 0);
    set8(CTRL, 3);
    if ((rd8(AREA_FLAGS) & 0x10) && (rd8(PREV_COLL(e)) & 0x20)) { vine_on_platform(e); return; }
    wrw(VEL_Y(e), 0);
    int16_t speed = rdw(CLIMB_SPEED);
    if (held() & 1) {                               /* up */
        set8(ANIM, 8);
        int8_t v = (int8_t)-speed;
        wr8(VEL_Y(e), (uint8_t)v);
        wr8(CLIMB(e), (uint8_t)(rd8(CLIMB(e)) + v));
        int16_t y = (int16_t)((int8_t)(rd8(HALF_H(e)) - rd8(LADDER_TOP)) + rdw(Y(e)) + (int16_t)-speed);
        uint16_t t = world_type_at_pixel(e, rdw(X(e)), y);
        if ((t & 0xF000) != LADDER) {               /* the top of the vine: stay below it */
            wrw(Y(e), rdw(Y(e)) + (-y & 15));
            return;
        }
        wr8(TIMER(e), (uint8_t)(y & 15));
        if ((t & 0xF00F) == (LADDER | 8) && rd8(TIMER(e)) < 5) {   /* onto a ladder's bottom */
            set8(ANIM, 0x10);
            set_state(e, S_LADDER);
        }
        return;
    }
    if (!(held() & 2)) return;
    clr8(ANIM, 8);                                  /* down */
    wr8(VEL_Y(e), (uint8_t)speed);
    wr8(CLIMB(e), (uint8_t)(rd8(CLIMB(e)) + speed));
    int16_t y = (int16_t)(rdw(Y(e)) + (int8_t)(rd8(HALF_H(e)) - rd8(LADDER_TOP)));
    if ((world_type_at_pixel(e, rdw(X(e)), y) & 0xF000) == LADDER) return;
    let_go(e);                                      /* the bottom of the vine */
}

/* $E2C6: the handler for the player's state */
static void player_state(Ent e)
{
    if (rd8(FORM) > 2) {                            /* the table has three forms */
        fprintf(stderr, "player: form %d is outside the original's table ($E2FC)\n", rd8(FORM));
        exit(3);
    }
    switch (rd8(STATE(e))) {
    case S_HURT:   state_hurt(e); break;
    case S_STAND: case S_STAND2: state_stand(e); break;
    case S_WALK:   state_walk(e); break;
    case S_AIR:    state_air(e); break;
    case S_WATER:  if (rd8(FORM) == 1) state_swim(e); else state_water(e); break;
    case S_DUCK:   state_duck(e); break;
    case S_UP_ATTACK:  state_up_attack(e); break;
    case S_LADDER: state_ladder(e); break;
    case S_VINE:   state_vine(e); break;
    default:
        fprintf(stderr, "player: state %d jumps outside the original's table ($E2C6)\n", rd8(STATE(e)));
        exit(3);
    }
}

void PlayerState(Cpu *c)
{
    cpu = c;
    player_state(c->a[4]);
}
