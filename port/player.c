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
#include "task.h"

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

#define PLAYER_W      0xFFFF9EEEu   /* the player's entity offset (a word) */
#define LADDER        0x4000          /* collision types: a ladder; with bit 3 its top */
#define WATER         0x7000

void hearts(void);                    /* hud.c: the hearts drawn again ($8AA2) */
static void sword_tip(Ent e, uint32_t list);
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
        sword_tip(e, 0xE9F2 + 2 * rd8(ATTACK_STEP(e)));
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

/* ---- the rest of the player's frame ---- */

#define HIT_STATE(e)  ((e) - 0x2AFD)   /* byte: being hit (bit 7), knocked back (4), stunned (6) */
#define HURT_BOXES(e) ((e) - 0x2900)   /* long: the player's own hit-box list (shield, body) */
#define ANIM_RATE(e)  ((e) - 0x39FE)
#define COLL_TYPE(e)  ((e) - 0x30FE)   /* what it stands on (movement.c) */
#define MODE(e)       ((e) - 0x2F00)   /* bit 7: in the water (for the splash) */
#define PREV_HELD     0xFFFF9F0Cu
#define INPUT_MODE    0xFFFF9F05u   /* 0 controller, 2 walking right on its own, 4 none and still,
                                     * 6 a routine at $9F22, 8 none */
#define INPUT_FN      0xFFFF9F22u
#define PAD           0xFFFF8A7Au   /* the controller, as the frame read it */
#define ATTACK_DELAY  0xFFFF9F08u
#define BLINK_TIME    0xFFFF9F0Au   /* after a hit: frames left blinking */
#define ACCEL_BASE    0xFFFF9EFAu   /* the acceleration, drag and speed this frame starts from */
#define DRAG_BASE     0xFFFF9EFCu
#define SHIELD        0xFFFF95D0u
#define FALL_FLAGS    0xFFFF9F03u   /* GROUND; bit 6: the knocked-back hit-box list */
#define T_SLOPE       0x0100
#define T_ICE         0x0200

/* $5D8: the game's random number generator */
static uint32_t random32(void)
{
    Cpu t = *cpu;
    call_68k(&t, 0x5D8);
    return t.d[0];
}

/* $10910 (effects.c): an effect (or item) of type `kind` at x, y; returns the entity the
 * spawner left in a0 (when no slot is free, $C0) */
void SpawnItem(Cpu *), SpawnItemAt(Cpu *);
static Ent spawn_effect(Ent e, uint16_t kind, int16_t x, int16_t y)
{
    Cpu t = *cpu;
    t.a[4] = e; t.d[0] = kind; t.d[1] = (uint16_t)x; t.d[2] = (uint16_t)y;
    SpawnItemAt(&t);
    return t.a[0];
}

/* the x of a point beside the player, mirrored when it faces left */
static int16_t beside(Ent e, uint8_t dx)
{
    if (rd8(e - 0x3FFE) & 8) dx = (uint8_t)-(uint8_t)(dx + 1);
    return (int16_t)((int8_t)dx + rdw(X(e)));
}

/* $EE70: the sword's tip, the first point of the list (x, y from the player; $80: none). A
 * breakable block there (its collision byte 2, $26F0) is broken ($B2BA). With the power spell
 * on (FALL_FLAGS bit 5), the swing's script ($9F2C, at step $9F2A) leaves sparkles ($216) at
 * the points of its next step. */
static void sword_tip(Ent e, uint32_t list)
{
    uint8_t dx = rd8(list);
    if (dx != 0x80) {
        int16_t x = beside(e, dx), y = (int16_t)((int8_t)rd8(list + 1) + rdw(Y(e)));
        Cpu t = *cpu;
        t.a[4] = e; t.a[3] = 0xFFFF9754u; t.a[0] = list + 2;
        t.d[6] = (t.d[6] & 0xFFFF0000u) | (uint16_t)((uint16_t)x >> 4);
        t.d[7] = (t.d[7] & 0xFFFF0000u) | (uint16_t)(((uint16_t)y & 0xFFF0) << 2);
        call_68k(&t, 0x26F0);                       /* the block's collision byte */
        if ((uint8_t)t.d[1] == 2) call_68k(&t, 0xB2BA);
    }
    if (!(rd8(FALL_FLAGS) & 0x20)) return;
    int16_t step = rdw(ATTACK_STEP_N);
    if (step < 0) return;
    uint32_t script = rd32(ATTACK_SCRIPT);
    int16_t off;
    while (!(off = rdw(script + (uint32_t)(int32_t)(int16_t)(2 * step)))) {
        step = rdw(script + (uint32_t)(int32_t)(int16_t)(2 * step) + 2);   /* a jump in the script */
        if (step < 0) { wr8(ATTACK_STEP_N, 0xFF); return; }                 /* its end */
        wrw(ATTACK_STEP_N, step);
    }
    wrw(ATTACK_STEP_N, rdw(ATTACK_STEP_N) + 1);
    for (uint32_t p = script + (uint32_t)(int32_t)off; (dx = rd8(p)) != 0x80; p += 2) {
        Cpu t = *cpu;
        t.a[4] = e; t.d[0] = 0x216;
        t.d[1] = (uint16_t)beside(e, dx); t.d[2] = (uint16_t)((int8_t)rd8(p + 1) + rdw(Y(e)));
        SpawnItem(&t);
    }
}

static void call_with_d0(Ent e, uint32_t addr, uint16_t d0)
{
    Cpu t = *cpu;
    t.a[4] = e; t.d[0] = d0;
    call_68k(&t, addr);
}

/* $E1A6: the start of the player's frame: no attack or hit boxes yet, this frame's speeds,
 * the ground flag from the last move, and the timers counted down */
static void start_frame(Ent e)
{
    wr8(HIT_STATE(e), 0);
    wr32(ATTACK_BOX(e), 0);
    wr32(HURT_BOXES(e), 0);
    wrw(ACCEL, rdw(ACCEL_BASE));
    wrw(SWIM_DRAG(e), rdw(DRAG_BASE));
    wr32(SPEED(e), rd32(SWIM_SPEED));
    wrw(ANIM_RATE(e), 0x100);
    wr8(ANIM, rd8(ANIM) & 0xBA);
    clr8(GROUND, 1);
    clr8(FLAGS(e), F_DUCKING | F_IN_AIR);
    clr8(CTRL, 0x10);
    wr8(POSE, 0);
    if ((rd8(COLL(e)) & 0x0A) == 2) set8(GROUND, 1);
    if (rd8(DUCK_DELAY)) wr8(DUCK_DELAY, (uint8_t)(rd8(DUCK_DELAY) - 1));
    if (rd8(CLIMB_DELAY)) wr8(CLIMB_DELAY, (uint8_t)(rd8(CLIMB_DELAY) - 1));
    if (rd8(ATTACK_DELAY)) wr8(ATTACK_DELAY, (uint8_t)(rd8(ATTACK_DELAY) - 1));
    clr8(FACING(e), 0x40);
    if (rd8(BLINK_TIME)) {
        wr8(BLINK_TIME, (uint8_t)(rd8(BLINK_TIME) - 1));
        set8(FACING(e), 0x40);
    }
}

/* $DE6A: the buttons for this frame, from the controller or a script; an item used with up or
 * down keeps that direction to itself while it is held. PRESSED: newly down this frame. */
static void read_input(Ent e)
{
    wr8(PREV_HELD, held());
    switch (rd8(INPUT_MODE)) {
    case 0: wr8(HELD, rd8(PAD)); break;
    case 2:                                         /* walking right on its own, to x = $1140 */
        wr8(HELD, 8);
        if ((uint16_t)rdw(X(e)) >= 0x1140) {
            wrw(X(e), 0x1140);
            wrw(VEL_X(e), 0);
            wr8(HELD, 0); wr8(PREV_HELD, 0); wr8(PRESSED, 0);
        }
        break;
    case 4: stop(e); wr8(HELD, 0); wr8(PRESSED, 0); break;
    case 6: { Cpu t = *cpu; t.a[4] = e; call_68k(&t, rd32(INPUT_FN)); break; }
    case 8: wr8(HELD, 0); break;
    default:
        fprintf(stderr, "player: input mode %d is outside the original's table ($DED8)\n", rd8(INPUT_MODE));
        exit(3);
    }
    if (rd8(CTRL) & 0x60) {
        uint8_t keep = 0;
        if (rd8(CTRL) & 0x40) { clr8(CTRL, 0x40); if (held() & 2) { set8(CTRL, 0x40); keep |= 2; } }
        if (rd8(CTRL) & 0x20) { clr8(CTRL, 0x20); if (held() & 1) { set8(CTRL, 0x20); keep |= 1; } }
        wr8(HELD, held() & (uint8_t)~keep);
    }
    wr8(PRESSED, (uint8_t)((held() ^ rd8(PREV_HELD)) & held()));
}

/* $DFA0: a bounce tile ($9xxx) under the feet springs the player up */
static void bounce(Ent e)
{
    if ((rd16(TILE_BELOW) & 0xF000) != 0x9000) return;
    launch(e, -0x680);
    spawn_effect(e, 0x200, (int16_t)((rdw(X(e)) & 0xFFF0) + 8),
                 (int16_t)((rd8(HALF_H(e)) + rdw(Y(e))) & 0xFFF0));
    queue_sound(0x33);
}

/* $EF4E: acceleration (unless a handler did its own) and friction. On ice the acceleration is
 * a quarter, the top speed doubles and there is no friction; on an icy slope it also slides. */
static void accelerate(Ent e)
{
    uint8_t ctrl = rd8(CTRL);
    clr8(CTRL, 2);
    if (!(ctrl & 2)) call_with_d0(e, 0xB44, 0);     /* AccelY: gravity */
    clr8(CTRL, 1);
    if (ctrl & 1) return;
    int16_t a = rdw(ACCEL);
    uint16_t ground = rd16(COLL_TYPE(e));
    if (!on_ground()) a >>= 1;
    else if (ground & T_ICE) {
        a >>= 2;
        wrw(SPEED(e), 2 * rdw(SWIM_SPEED));
        if (ground & T_SLOPE) {
            if (!(rd8(ANIM) & 1)) a = 0;
            else if (rd8(ANIM) & 2) a = (int16_t)-a;
            int8_t slide = (int8_t)rd8(0x3E32 + ((ground & 0xF0) >> 4));   /* $3E20: by the slope */
            wrw(ACCEL_X(e), a + slide);
            call_with_d0(e, 0xAF4, 0);              /* AccelX */
            return;
        }
    }
    if (!(rd8(FLAGS(e)) & F_ATTACKING) && (rd8(ANIM) & 1)) {   /* moving */
        wrw(ACCEL_X(e), rd8(ANIM) & 2 ? -a : a);
        call_with_d0(e, 0xAF4, 0);                  /* AccelX */
        return;
    }
    if (!(ground & T_ICE)) call_with_d0(e, 0xB92, (uint16_t)a);   /* FrictionX */
}

/* $DD7C: entering the water (or a $5xxx tile) splashes at the surface; leaving it too */
static void splash(Ent e)
{
    if (rd8(AREA_FLAGS) & 1) return;                /* an area all under water */
    uint16_t here = rd16(TILE_HERE);
    Cpu *c = cpu;
    if (here == WATER || here == 0x5000) {
        if (rd8(MODE(e)) & 0x80) return;
        set8(MODE(e), 0x80);
        queue_sound(0x47);
        c->d[2] = (c->d[2] & 0xFFFF0000u) | (uint16_t)rdw(Y(e));
    } else {
        uint16_t below = rd16(TILE_BELOW);
        if (below == WATER || below == 0x5000) return;
        if (!(rd8(MODE(e)) & 0x80)) return;
        clr8(MODE(e), 0x80);
        c->d[2] = (uint16_t)(rd8(HALF_H(e)) + rdw(Y(e)));
    }
    /* the spawner runs on the caller's registers, as in the original: items ($10A1E) call
     * this and then use the a2 the spawner leaves (the kinds' table), not the player */
    c->d[2] = (c->d[2] & 0xFFFF0000u) | (uint16_t)((c->d[2] - 8) & 0xFFF0);
    c->d[0] = (c->d[0] & 0xFFFF0000u) | 0x202;
    c->d[1] = (c->d[1] & 0xFFFF0000u) | (uint16_t)rdw(X(e));
    c->a[4] = e;
    SpawnItemAt(c);
}

/* $DDDE: under water, a bubble every 120 frames (sooner while moving fast); hurt, a burst */
static void bubbles(Ent e)
{
    if (!(rd8(AREA_FLAGS) & 1)) return;
    int16_t v = rdw(VEL_X(e));
    int steps = 1;
    if (v) {
        steps++;
        if ((uint16_t)(v < 0 ? -v : v) >= (uint16_t)rdw(SPEED(e))) steps++;
    }
    wr8(BUBBLES(e), (uint8_t)(rd8(BUBBLES(e)) - steps));
    if ((int8_t)rd8(BUBBLES(e)) >= 0) return;
    wr8(BUBBLES(e), 0x78);
    int16_t dx = 0;
    uint16_t kind = 0x204;
    if (rd8(FLAGS(e)) & F_HURT) {                   /* scattered, sometimes the other kind */
        uint32_t r = random32();
        dx = (int16_t)(((r >> 16) & 3) + (r & 7));
        if (!(r & 2)) {
            kind = 0x210;
            if (r & 8) dx = (int16_t)-dx;
        }
    }
    Ent b = spawn_effect(e, kind, (int16_t)(rdw(X(e)) + dx), (int16_t)(rdw(Y(e)) - (rd8(HALF_H(e)) >> 2)));
    int16_t vy = rdw(VEL_Y(e));
    if (vy < 0) wrw(VEL_Y(b), (vy >> 2) - 0x80);    /* rising with the player */
}

/* $EFE2: the player's own hit boxes for this frame: knocked back, or by pose, form, sword and
 * shield */
static void hurt_boxes(Ent e)
{
    if (rd8(FALL_FLAGS) & 0x40) {
        wr32(HURT_BOXES(e), rd8(FORM) == 2 ? 0x1158A : 0x11584);
        return;
    }
    if (rd8(FLAGS(e)) & F_ATTACKING) return;
    uint8_t pose = rd8(POSE);
    if (pose & 0x80) return;
    if (pose == 0) {                                /* standing: the shield */
        if (rd8(FORM) == 2) { wr32(HURT_BOXES(e), 0xF4DE); return; }
        if (0x70 >> (rd8(SWORD) & 31) & 1) return;
        uint8_t shield = rd8(SHIELD);
        if (shield & 0x80) return;
        wr32(HURT_BOXES(e), 0xF4BE + (uint32_t)(int32_t)rdw(0xF4BE + 2 * shield));
    } else if (pose == 2) {                         /* the up attack */
        int8_t i = (int8_t)rd8(0xF04C + rd8(SWORD));
        if (i < 0) return;
        wr32(HURT_BOXES(e), 0xF4E4 + (uint32_t)(int32_t)rdw(0xF4E4 + i));
    } else {
        fprintf(stderr, "player: pose %d is outside the original's table ($F008)\n", pose);
        exit(3);
    }
}

/* ---- batch 2: the animation, the sword, A + left/right ---- */

#define SPRITE(e)     ((e) - 0x3FFE)   /* byte: bit 3 the sprite faces left; bit 0 see swing() */
#define ANIM_ID(e)    ((e) - 0x3CFE)   /* byte: the animation playing */
#define ANIM_BUSY(e)  ((e) - 0x3A00)   /* byte: non-zero while an animation runs on */
#define POWER(e)      ((e) - 0x2700)   /* word: the sword's strength */
#define HIT_POWER(e)  ((e) - 0x2400)   /* word: this swing's */
#define SPECIAL_LEFT  0xFFFF9F10u   /* the special action for A + left, and A + right (likely magic) */
#define SPECIAL_RIGHT 0xFFFF9F11u

static void set_anim(Ent e, int id) { call_with_d0(e, 0x7E8, (uint16_t)id); }   /* SetAnim */

/* $F12C: on a vine the climbing animation steps as it climbs: every 8 pixels, two frames */
static int vine_frame(Ent e)
{
    uint8_t frame = rd8(CLIMB_W(e));
    if (!(rd8(ANIM) & 0x10)) {
        int8_t climb = (int8_t)rd8(CLIMB(e));
        if (rd8(ANIM) & 8) {                        /* going up */
            if (climb > 0) return -1;
            wr8(CLIMB(e), 8);
            frame = (uint8_t)(frame - 1);
            if ((int8_t)frame < 0) frame = 1;
        } else {
            if ((uint8_t)climb < 8) return -1;
            wr8(CLIMB(e), 0);
            frame = (uint8_t)(frame + 1);
            if (frame > 1) frame = 0;
        }
        wr8(CLIMB_W(e), frame);
    }
    return rd8(0xF186 + frame);                     /* $10 or $18 */
}

/* $F064: the animation for the state (unless a handler chose one), and the sprite's facing */
static void animate(Ent e)
{
    uint8_t ctrl = rd8(CTRL);
    clr8(CTRL, 8);
    if (ctrl & 8) return;
    int atk = rd8(FLAGS(e)) & F_ATTACKING, id, facing = 1;
    switch (rd8(STATE(e))) {
    case S_HURT:   id = 8; break;
    case S_STAND:  id = atk ? 6 : 0; break;
    case S_WALK:
        if (atk) { id = 6; break; }
        id = 0;
        if (!on_ground()) break;
        if (rd16(COLL_TYPE(e)) & T_ICE) {           /* on ice: the legs run faster when slipping */
            int16_t v = rdw(VEL_X(e));
            int left = rd8(ANIM) & 2;
            if (v == 0 || (v > 0) == !!left || (uint16_t)(v < 0 ? -v : v) < (uint16_t)rdw(SPEED(e)))
                wrw(ANIM_RATE(e), 0x200);
        }
        id = 2;
        break;
    case S_AIR:    id = atk ? 6 : 4; break;
    case S_WATER:  id = atk ? 6 : 0xE; break;
    case S_DUCK:   id = atk ? 0xA : 0x12; break;
    case S_UP_ATTACK: id = 0x1A; break;
    case S_LADDER: id = 0x14; facing = 0; break;
    case S_VINE:   id = vine_frame(e); if (id < 0) return; facing = 0; break;
    default:
        fprintf(stderr, "player: state %d is outside the original's animation table ($F080)\n", rd8(STATE(e)));
        exit(3);
    }
    if (facing) {
        clr8(SPRITE(e), 8);
        if (rd8(ANIM) & 2) set8(SPRITE(e), 8);
    }
    uint8_t anim = rd8(ANIM);
    clr8(ANIM, 0x10);
    if ((anim & 0x10) || id != rd8(ANIM_ID(e))) set_anim(e, id);   /* changed state: restart it */
}

/* $EE14: during a swing, the sword's hit points for this step of it ($EE70), and the swing's
 * strength. Which points: by form, by sword, ducking or not. */
static void swing_points(Ent e)
{
    set8(POSE, 0x80);
    uint32_t list; unsigned steps;
    if (rd8(FORM) == 2) { list = 0xF48C; steps = 5; }
    else {
        uint8_t sword = rd8(SWORD);
        if (rd8(FLAGS(e)) & F_DUCKING) { list = 0xF406; steps = 2; }
        else { steps = rd8(0xF2F4 + sword); list = 0xF2FC; }
        list += (uint32_t)(int32_t)rdw(list + 2 * sword);
    }
    uint8_t step = rd8(ATTACK_STEP(e));
    if (step >= steps) return;
    list += (uint32_t)(int32_t)rdw(list + 2 * step);
    wr32(ATTACK_BOX(e), list + 2);
    wrw(HIT_POWER(e), rdw(POWER(e)));
    sword_tip(e, list);
}

/* $EDE0: the trail the swing leaves (a script of effects), by form, ducking and sword */
static void swing_trail(Ent e)
{
    uint32_t script;
    if (rd8(FORM) == 2) script = 0xF594;
    else if (rd8(FLAGS(e)) & F_DUCKING) script = 0xF530;
    else script = rd8(SWORD) < 4 ? 0xF4F6 : 0xF55E;
    wr32(ATTACK_SCRIPT, script);
    wrw(ATTACK_STEP_N, 0);
}

/* $ED5C: B swings the sword (not with A, hurt, on $8xxx/$Axxx tiles, or too soon after the last
 * swing); while the swing's animation runs, its hit points; then a short delay */
static void sword(Ent e)
{
    if (rd8(FLAGS(e)) & F_ATTACKING) {
        clr8(FLAGS(e), F_ATTACKING);
        if (!rd8(ANIM_BUSY(e))) { wr8(ATTACK_DELAY, 4); return; }   /* the swing is over */
        set8(FLAGS(e), F_ATTACKING);
        if (!(rd8(SPRITE(e)) & 1)) swing_points(e);
        return;
    }
    if (!(pressed() & 0x10) || (held() & 0x40) || (rd8(FLAGS(e)) & F_HURT) || on_8_or_a()
        || rd8(ATTACK_DELAY) || (rd8(CTRL) & 0x10)) return;
    swing_trail(e);
    queue_sound(0x20);
    set8(FLAGS(e), F_ATTACKING);
    set8(ANIM, 0x10);
    clr8(SPRITE(e), 1);
}

/* $EF12: A + left or right starts the special action set for that side (magic.c, $11170).
 * Returns 1 if it handed over to the original's code (a potion, off a task pass). */
int magic_start(Cpu *c, uint8_t kind);
static int special(Ent e)
{
    if ((rd8(AREA_FLAGS) & 0x80) || !(held() & 0x40) || (rd8(FLAGS(e)) & 0xE0)) return 0;
    uint8_t which;
    if (pressed() & 4) which = rd8(SPECIAL_LEFT);
    else if (pressed() & 8) which = rd8(SPECIAL_RIGHT);
    else return 0;
    cpu->a[4] = e;
    return magic_start(cpu, which);
}

/* ---- batch 3: pickups and damage ---- */

#define HEALTH(e)     ((e) - 0x2600)   /* word; $40 a quarter heart? (see the hearts at $8AA2) */
#define MAX_HEALTH    0xFFFF9F00u
#define REFILL        0xFFFF9F14u   /* health still to add, a little each frame ($F288) */
#define GOLD          0xFFFF962Cu   /* long, up to 999,999 */
#define DAMAGE_BITS   0xFFFF9854u   /* bit 0: touched something harmful; bit 1: harm of $9856 */
#define DAMAGE_AMOUNT 0xFFFF9856u
#define ARMOR         0xFFFF95D1u
#define TILE_ABOVE    0xFFFF9EDCu
#define HIT_FLASH(e)  ((e) - 0x3900)   /* word: $2101 while hit */
#define HIT_DAMAGE(e) ((e) - 0x23FE)   /* word: the damage of the hit taken */
#define KNOCKBACK(e)  ((e) - 0x610A)   /* word: knockback speed */
#define REVIVAL       0x2A          /* the item that saves the player at 0 health */

/* $F1EC: gold: a random amount by the pickup's kind (masks and bases at $F226), the total kept
 * to 999,999, then the message and the gold display, sound $28 */
static void pick_gold(Cpu *c)
{
    uint16_t kind = (uint16_t)c->d[0];              /* 0, 2 .. 22 */
    Cpu t = *c;
    call_68k(&t, 0x5D8);                            /* Random, in t: its d1 goes on to $1FCA */
    uint32_t pair = 0xF226 + 2 * kind;
    int32_t amount = (int16_t)((t.d[0] & rd16(pair)) + rd16(pair + 2));
    uint32_t gold = rd32(GOLD) + (uint32_t)amount, over = gold - 999999;
    if (gold > 999999) { amount -= (int32_t)over; gold -= over; }
    wr32(GOLD, gold);
    t.d[0] = (uint32_t)amount; t.d[2] = gold; t.d[3] = over;
    call_68k(&t, 0x1FCA);                           /* "Found ... GOLD." */
    call_68k(&t, 0x85A8);                           /* the gold display */
    queue_sound(0x28);
}

/* $F256: an item ($26) */
static void pick_item(Cpu *c)
{
    queue_sound(0x28);
    Cpu t = *c; t.d[0] = 0x26;
    call_68k(&t, 0x245A);
}

/* $F262: a heart: $80 more health, up to the maximum; redraw the hearts */
static void pick_heart(Cpu *c)
{
    queue_sound(0x29);
    Ent e = c->a[4];
    uint16_t h = (uint16_t)(rdw(HEALTH(e)) + 0x80);
    if (h > rd16(MAX_HEALTH)) h = rd16(MAX_HEALTH);
    wrw(HEALTH(e), h);
    hearts();
}

/* $E188: thrown up a little, unless still blinking from the last hit */
static void bump_up(Ent e)
{
    if (rd8(BLINK_TIME)) return;
    wr8(COLL(e), rd8(COLL(e)) & 0x75);
    int16_t j = rdw(JUMP_SPEED) >> 1;
    wrw(VEL_Y(e), j + (j >> 1));
}

/* $E16C: knocked back, away from the hit (unless climbing), and up */
static void knock_back(Ent e)
{
    if (rd8(ANIM) & 4) return;
    int16_t v = rdw(KNOCKBACK(e));
    wrw(VEL_X(e), rd8(HIT_STATE(e)) & 1 ? v : -v);
    bump_up(e);
}

/* $E12E: the hurt state: blinking, the hurt pose, sound $36 */
static void hurt(Ent e)
{
    wr8(ANIM, (uint8_t)((rd8(ANIM) & 0xFA) | 0x10));
    wr8(FLAGS(e), (uint8_t)((rd8(FLAGS(e)) & 0xC7) | F_HURT));
    wrw(HIT_FLASH(e), 0x2101);
    set8(FACING(e), 0x40);
    wr8(TIMER(e), 0x1E);
    set_state(e, S_HURT);
    wr8(BLINK_TIME, 0x3C);
    queue_sound(0x36);
}

/* ---- death and the heart refill: they wait for frames (task.h) ---- */

static int on_pass;                     /* 1: running in the player's update on the task's pass */

#define DD08_SP       0xFFFF9F26u       /* the stack pointer at the player's update ($DD08) */
#define AFTER_UPDATE  0xFFFF9F1Eu       /* where the frame carries on after it */
#define GAME_FLAGS    0xFFFF9659u
#define GAME_OVER     0x80              /* in FALL_FLAGS ($9F03) */

static void with_player(Cpu *c, Ent e, uint32_t addr) { Cpu t = *c; t.a[4] = e; call_68k(&t, addr); }

/* $E29C: one frame of falling off the screen, turning about every 30 frames; 1 once gone */
static int fall_step(Cpu *c, Ent e)
{
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { wr8(TIMER(e), 0x1E); wrw(ACCEL_X(e), -rdw(ACCEL_X(e))); }
    Cpu t = *c; t.a[4] = e;
    call_68k(&t, 0xBF4);                            /* off the screen? */
    if (t.d[0]) return 1;
    with_player(c, e, 0xAF4);                       /* AccelX */
    with_player(c, e, 0xAB4);                       /* ApplyVelocity */
    return 0;
}

/* $E22E: dead. The first time: the music stops, the world freezes, the player jumps up and falls
 * off the screen, a frame at a time; then the frame carries on after the player's update with
 * GAME_OVER set. Afterwards (the update runs it each frame), one more step of the fall. Returns
 * 1: the player's update is over. */
static int dying(Cpu *c, Ent e)
{
    if (rd8(STATE(e)) & 0x80) {                     /* already falling */
        if (fall_step(c, e)) {
            set8(FALL_FLAGS, GAME_OVER);
            port_continue_at(c, rd32(AFTER_UPDATE), 0);   /* jmp ($9F1E): the stack as it is */
        }
        return 1;
    }
    set8(STATE(e), 0x80);
    wr8(0xFFFF9661u, 0xFF);
    queue_sound(-1);                                /* stop */
    queue_sound(0x53);
    wr32(AFTER_UPDATE, rd32(rd32(DD08_SP)));        /* the update's return address */
    with_player(c, e, 0xA66);                       /* freeze everything else */
    set8(GAME_FLAGS, 0x60);
    set8(SPRITE(e), 0x80);
    wrw(ACCEL_X(e), 0x20);
    wrw(SPEED(e), 0x100);
    wr8(TIMER(e), 0x1E);
    wrw(VEL_Y(e), -0x100);
    clr8(FACING(e), 0x40);
    if (rd8(ANIM_ID(e)) == 8) {                     /* already in the dying pose: no wait */
        if (fall_step(c, e)) { set8(FALL_FLAGS, GAME_OVER); return 1; }
        task_end_pass(c, 0);                        /* the pass ends; the next starts at the frame */
    }
    set_anim(e, 8);
    hearts();
    for (;;) {
        task_wait_frame(c, 0xE298);
        cpu = c;
        if (fall_step(c, e)) { set8(FALL_FLAGS, GAME_OVER); return 1; }
    }
}

/* $F288: the hearts refill by $10 a frame from REFILL while the world is frozen; then the
 * gameplay loop starts again at $8FFC. Returns only if the health is full already. */
static void refill_frames(Cpu *c, Ent e, int wait);
static void refill(Cpu *c, Ent e)
{
    if (rdw(HEALTH(e)) == rdw(MAX_HEALTH)) return;
    queue_sound(0x3D);
    with_player(c, e, 0xA66);                       /* freeze */
    refill_frames(c, e, 1);
}

/* $F2A6: the refill's frames (`wait` 0: resumed at $F2AA, this frame's step first) */
static void refill_frames(Cpu *c, Ent e, int wait)
{
    for (;; wait = 1) {
        if (wait) task_wait_frame(c, 0xF2AA);
        cpu = c;
        uint16_t add = 0x10, left = rd16(REFILL);
        if (!left) break;
        if (left > 0x10) left -= 0x10; else { add = left; left = 0; }
        wrw(REFILL, left);
        uint16_t h = (uint16_t)(rd16(HEALTH(e)) + add);
        if (h >= rd16(MAX_HEALTH)) { h = rd16(MAX_HEALTH); wrw(REFILL, 0); }
        wrw(HEALTH(e), h);
        hearts();
    }
    with_player(c, e, 0xA8E);                       /* unfreeze */
    queue_sound(0x3E);
    task_end_pass(c, 0x8FFC);
}

/* the refill for a potion (magic.c), on the player's task pass */
void player_refill(Cpu *c) { cpu = c; refill(c, c->a[4]); }

void magic_shield_hit(Cpu *c);

/* $E0A4: lose health. Below $40: saved by the revival item (the hearts refill to half, $F288),
 * or dead ($E22E); both reset the stack and yield. In the player's update they run here (on the
 * task's pass); called on their own (fuzzing) they carry on in the original's code. Returns 1
 * if the update is over. `nested`: reached from inside $E04E, whose return address ($DFE0) the
 * original has on the stack there. */
static int lose_health(Cpu *c, int16_t amount, int nested)
{
    Ent e = c->a[4];
    wrw(HEALTH(e), rdw(HEALTH(e)) - amount);
    if (rdw(HEALTH(e)) >= 0x40) {
        hearts();
        return 0;
    }
    wrw(HEALTH(e), 0);
    Cpu t = *c; t.d[0] = REVIVAL;
    call_68k(&t, 0x23E4);                           /* has the item? (Z clear if so) */
    if (!(t.ccr & CC_Z)) {
        t = *c; t.d[0] = REVIVAL; call_68k(&t, 0x24DC);   /* used up */
        hearts();
        wrw(REFILL, (uint16_t)(((uint16_t)(rd16(MAX_HEALTH) + 0x100) >> 1) & 0xFF00));   /* half */
        wr8(HIT_STATE(e), 0);
        if (on_pass) { refill(c, e); return 0; }
        port_continue_at(c, 0xF288, nested ? 0xDFE0 : 0);
        return 1;
    }
    wr8(e - 0x2BFF, 0);
    set_state(e, S_HURT);
    set8(FLAGS(e), 0x80);                           /* dead */
    if (on_pass) return dying(c, e);
    port_continue_at(c, 0xE22E, nested ? 0xDFE0 : 0);
    return 1;
}

/* $E04E: being hit this frame (HIT_STATE, set by whatever hit): bit 7 a hit that hurts (knocked
 * back, the hurt state, the hit's damage after armour, $4964), bit 4 or 6 pushed back only */
static int being_hit(Cpu *c)
{
    Ent e = c->a[4];
    uint8_t hit = rd8(HIT_STATE(e));
    if (hit & 0x80) {
        if (rd8(BLINK_TIME)) { knock_back(e); return 0; }
        knock_back(e);
        hurt(e);
        Cpu t = *c; t.d[0] = (uint16_t)rdw(HIT_DAMAGE(e));
        call_68k(&t, 0x4964);
        return lose_health(c, (int16_t)t.d[0], 1);
    }
    if (hit & 0x10) {
        if (rd8(FALL_FLAGS) & 0x40) { Cpu t = *c; magic_shield_hit(&t); return 0; }   /* the shield */
        queue_sound(0x30);
    } else if (hit & 0x40) queue_sound(0x46);
    else return 0;
    wrw(VEL_X(e), rd8(SPRITE(e)) & 8 ? 0x180 : -0x180);   /* pushed the way it faces? */
    return 0;
}

/* $DFDE: damage this frame: a hit, then the surroundings: a hot area without the armour for it,
 * a $Dxxx tile, harm signalled in $9854, $3xxx tiles at the feet, body or head */
static int damage(Cpu *c)
{
    Ent e = c->a[4];
    if (being_hit(c)) return 1;
    int16_t amount;
    uint16_t below = rd16(TILE_BELOW) & 0xF000;
    if ((rd8(AREA_FLAGS) & 0x40) && ((rd8(EQUIP) & 0x10) || rd8(ARMOR) != 2)) amount = 1;
    else if (below == 0xD000) amount = 1;
    else {
        uint8_t bits = rd8(DAMAGE_BITS);
        clr8(DAMAGE_BITS, 2);
        if (bits & 2) amount = rdw(DAMAGE_AMOUNT);
        else {
            if (rd8(BLINK_TIME)) return 0;
            if (below != 0x3000 && (rd16(TILE_FEET) & 0xF000) != 0x3000 && (rd16(TILE_ABOVE) & 0xF000) != 0x3000) {
                clr8(DAMAGE_BITS, 1);
                if (!(bits & 1)) return 0;
            }
            bump_up(e);                             /* $E11A: thrown up and hurt */
            hurt(e);
            return lose_health(c, 0x80, 0);
        }
    }
    wrw(HIT_FLASH(e), 0x2101);                      /* $E104: a steady burn: blink, small damage */
    set8(FACING(e), 0x40);
    wr8(BLINK_TIME, 0x3C);
    return lose_health(c, amount, 0);
}

/* $F1B0: a pickup the player touched ($FF9F78, bit 7 set by it): gold of 12 kinds, a heart, the
 * full heal, an item */
static void pickups(Cpu *c)
{
    uint8_t p = rd8(0xFFFF9F78u);
    clr8(0xFFFF9F78u, 0x80);
    if (!(p & 0x80)) return;
    uint8_t kind = rd8(0xFFFF9F78u);
    Ent e = (Ent)(int32_t)(int16_t)rd16(PLAYER_W);
    c->a[4] = e;
    if (kind <= 22 && !(kind & 1)) { c->d[0] = kind; pick_gold(c); }
    else if (kind == 24) pick_heart(c);
    else if (kind == 26) { wrw(REFILL, rdw(MAX_HEALTH)); refill(c, e); }   /* $F282 */
    else if (kind == 28) pick_item(c);
    else {
        fprintf(stderr, "player: pickup %d is outside the original's table ($F1CE)\n", kind);
        exit(3);
    }
}

static void player_state(Ent e);
void ProbeTileBelow(Cpu *), ProbeTileAbove(Cpu *), ProbeTileFeet(Cpu *), ProbeTileAtA3(Cpu *);
void UpdateAnim(Cpu *), Currents(Cpu *), MovePlayer(Cpu *);

/* $DD08: the player's frame */
static void update(Cpu *c)
{
    wr32(DD08_SP, c->a[7]);
    set8(GAME_FLAGS, 4);
    Ent e = (Ent)(int32_t)(int16_t)rd16(PLAYER_W);
    c->a[4] = e;
    if (rd8(FLAGS(e)) & 0x80) { dying(c, e); return; }
    c->a[3] = 0xFFFF9754u;                          /* plane A: what is under, over, at the player */
    ProbeTileBelow(c); ProbeTileAbove(c); ProbeTileFeet(c); ProbeTileAtA3(c);
    if (damage(c)) return;
    start_frame(e);
    read_input(e);
    pickups(c);
    bounce(e);
    player_state(e);
    if (special(e)) return;
    sword(e);
    hurt_boxes(e);
    accelerate(e);
    animate(e);
    c->a[4] = e; UpdateAnim(c);
    splash(e);
    bubbles(e);
    c->a[4] = e; Currents(c);
    MovePlayer(c);                                  /* leaves the cell under the feet in d6/d7 */
    wrw(0xFFFF9F18u, (uint16_t)c->d[6]);
    wrw(0xFFFF9F1Au, (uint16_t)c->d[7]);
    wr8(0xFFFF9F1Cu, rd8(e - 0x30FF));
    port_continue_at(c, 0x1141A, 0);                /* the special actions' frame */
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

void PlayerState(Cpu *c)      { cpu = c; player_state(c->a[4]); }
void PlayerStartFrame(Cpu *c) { cpu = c; start_frame(c->a[4]); }
void PlayerInput(Cpu *c)      { cpu = c; read_input(c->a[4]); }
void PlayerBounce(Cpu *c)     { cpu = c; bounce(c->a[4]); }
void PlayerAccel(Cpu *c)      { cpu = c; accelerate(c->a[4]); }
void PlayerSplash(Cpu *c)     { cpu = c; splash(c->a[4]); }
void PlayerBubbles(Cpu *c)    { cpu = c; bubbles(c->a[4]); }
void PlayerHurtBoxes(Cpu *c)  { cpu = c; hurt_boxes(c->a[4]); }
void PlayerAnimate(Cpu *c)    { cpu = c; animate(c->a[4]); }
void PlayerSword(Cpu *c)      { cpu = c; sword(c->a[4]); }
void PlayerSwordTip(Cpu *c)   { cpu = c; sword_tip(c->a[4], c->a[0]); }
void PlayerSpecial(Cpu *c)    { cpu = c; special(c->a[4]); }   /* may carry on at $F288 */
void PlayerDamage(Cpu *c)     { cpu = c; on_pass = 0; damage(c); }
void PlayerUpdate(Cpu *c)     { cpu = c; on_pass = 1; update(c); on_pass = 0; }
void PlayerGold(Cpu *c)       { cpu = c; pick_gold(c); }
void PlayerItem(Cpu *c)       { cpu = c; pick_item(c); }
void PlayerHeart(Cpu *c)      { cpu = c; pick_heart(c); }
void PlayerPickups(Cpu *c)    { cpu = c; pickups(c); }        /* only from fuzzing: the update ($DD3A) runs it itself */

/* The death and the refill as entries (batch T65): where the original jumps to them or a task's
 * pass resumes in them. They wait for frames, so they run only on a task pass (CT_WAITS); the
 * player's update runs the same code itself (dying, refill). */
#define ON_PASS_ONLY(self) do { if (!task_in_pass()) { port_leave_to_68k(c, (self)); return; } } while (0)
static Ent player_in_a4(Cpu *c) { c->a[4] = (uint32_t)(int32_t)(int16_t)rd16(PLAYER_W); return c->a[4]; }
/* $E22E: dead (a4 the player): the fall started, or its next step; when it is over the frame
 * carries on after the player's update ($9F1E) */
void PlayerDead(Cpu *c)
{
    ON_PASS_ONLY(0xE22E);
    cpu = c; c->ret = 0;
    int over = dying(c, (Ent)(int32_t)(int16_t)c->a[4]);
    if (over && !c->ret && (rd8(FALL_FLAGS) & GAME_OVER)) port_continue_at(c, rd32(AFTER_UPDATE), 0);
}
/* $E298: a pass resumed in the fall: one step */
void PlayerDeadFall(Cpu *c)
{
    ON_PASS_ONLY(0xE298);
    cpu = c; c->ret = 0;
    Ent e = player_in_a4(c);
    if (fall_step(c, e)) { set8(FALL_FLAGS, GAME_OVER); port_continue_at(c, rd32(AFTER_UPDATE), 0); }
}
/* $F282: the full heal (all of the maximum to add), then $F288 */
void PlayerFullHeal(Cpu *c) { ON_PASS_ONLY(0xF282); cpu = c; c->ret = 0; wrw(REFILL, rdw(MAX_HEALTH)); refill(c, player_in_a4(c)); }
/* $F288: the refill from REFILL; returns at once if the health is full */
void PlayerRefill(Cpu *c) { ON_PASS_ONLY(0xF288); cpu = c; c->ret = 0; refill(c, player_in_a4(c)); }
/* $F2AA: a pass resumed in the refill: this frame's step, then on */
void PlayerRefillStep(Cpu *c) { ON_PASS_ONLY(0xF2AA); cpu = c; c->ret = 0; refill_frames(c, player_in_a4(c), 0); }
/* $DF2E: up+A or down+A at a room's trigger ($9980/$9981, set by it) runs its routine ($998A,
 * $998E), then returns past its caller's frame (unlk a6), as use_item's caller does (batch T67;
 * verified by the owner's session `doors`) */
void PlayerUseItem(Cpu *c)
{
    cpu = c; c->ret = 0;
    Ent e = (Ent)(int32_t)(int16_t)c->a[4];
    if ((rd8(e - 0x3200) & 0x40) || !(rd8(FALL_FLAGS) & 1)) return;
    c->d[0] = set_b(c->d[0], rd8(0xFFFF9F0Du) & 0xF);
    c->d[1] = set_b(c->d[1], c->d[0]);
    if (rd8(ITEM_UP)) {
        c->d[0] = set_b(c->d[0], c->d[0] ^ 1);
        if (!(c->d[0] & 0xFF)) { set8(CTRL, 0x20); c->a[0] = rd32(ITEM_UP_FN); goto use; }
    }
    if (!rd8(ITEM_DOWN)) return;
    c->d[1] = set_b(c->d[1], c->d[1] ^ 2);
    if (c->d[1] & 0xFF) return;
    set8(CTRL, 0x40);
    c->a[0] = rd32(ITEM_DOWN_FN);
use:
    wrw(VEL_X(e), 0);
    call_68k(c, c->a[0]);
    c->ret = 0;
    c->a[7] = c->a[6];                              /* unlk a6 */
    c->a[6] = rd32(c->a[7]); c->a[7] += 4;
}
