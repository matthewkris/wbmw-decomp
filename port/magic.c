/* Magic and the other special actions, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * A + left or right ($EF12) starts the special action set for that side ($11170): thirteen
 * kinds. Most are spells with a number of uses left (`$FF95B8` + kind, shown in the HUD); a
 * spell in progress is ACTIVE ($9F7A bit 7) and runs a little each frame after the player's
 * update ($1141A):
 *
 *    0  fire spirits: seven homing flames, one per free magic slot (needs an enemy in reach)
 *    1  quake: the screen shakes, then a rock drops on each enemy standing on the ground
 *    2  thunder: a bolt crosses the screen, the screen flashes three times, every enemy is hit
 *    3  power: a timed boost (FALL_FLAGS bit 5) while the magic meter runs down
 *    4, 6, 7  shield: while the meter runs down, a hit on the player's hurt boxes
 *       throws back a spark (each costs meter, by the attacker's kind)
 *    5  sets $9962 to $B (no frame of its own)
 *    8-11  potions (items 43-46): refill the hearts by $100, $300, $500 or fully ($F288)
 *    12  sets $9962 to $A and gives the player animation set $140A, in some areas only
 *
 * The magic's own entities are the eight slots at $C0-$DC: fire spirits, rocks, sparks and the
 * bolt, run each frame by run_entities ($11832).
 *
 * The quake and the thunder's flashes take over the gameplay task: the original resets the stack
 * to the task's base, freezes the world and yields, so that each later pass of the task runs only
 * the next frame of the spell, until it restarts the gameplay loop at $8FFC. Here they are loops
 * that wait for the next frame (task_wait_frame, port/task.h); each resume address is also a
 * registered entry, so a pass that starts there without a waiting loop (a state loaded part-way
 * through) runs the same C from that point. Off a task pass (fuzzing) they hand over to the
 * original's code instead. */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"
#include "task.h"

/* ---- the globals ---- */
#define ACTIVE        0xFFFF9F7Au   /* bit 7: a spell runs; bit 0: thunder struck / quake shaking down */
#define KIND          0xFFFF9F7Bu   /* which; bit 7 once its frame has started */
#define COUNT_A       0xFFFF9F7Cu   /* bytes for the spells' counters (a word for the spirits) */
#define COUNT_B       0xFFFF9F7Du
#define ROCK_DELAY    0xFFFF9F7Eu
#define ROCKS         0xFFFF9F7Fu   /* rocks dropped ... */
#define ROCKS_DOWN    0xFFFF9F80u   /* ... and landed */
#define SCROLL_HOME   0xFFFF9F82u   /* the vertical scroll the quake shakes about */
#define NEXT_ENEMY    0xFFFF9F84u   /* the quake's search through the enemies */
#define SHIELD_HITS   0xFFFF9F13u   /* bits: which hurt boxes were hit (shield) */
#define METER         0xFFFF9F16u   /* the magic meter: $2000 full, -8 a frame */
#define REFILL        0xFFFF9F14u   /* health still to add ($F288) */
#define MAX_HEALTH    0xFFFF9F00u
#define FALL_FLAGS    0xFFFF9F03u   /* bit 5 power, bit 6 shield */
#define CTRL          0xFFFF9F06u   /* bit 2: only kinds 5 and up */
#define FORM          0xFFFF9F04u
#define EQUIP         0xFFFF9EF1u
#define PLAYER_W      0xFFFF9EEEu
#define TIMER_9962    0xFFFF9962u
#define SCROLL_Y      0xFFFF973Cu
#define ENEMIES       0xFFFFA11Au   /* the first of the 16 enemy slots the quake and thunder scan */
#define FRAME         0xFFFF965Au
#define TASK_SP       0xFFFF9662u   /* the gameplay task's stack base (saved at $9014) */
#define PAL_FLAGS     0xFFFF8C56u
#define GFX_WANT      0xFFFF8E76u   /* a graphics block to load ($22F0) */

/* ---- entity fields (as offsets: e = $00-$FC) ---- */
#define ALIVE(e)      ((e) - 0x4000)   /* bit 7 in use; bit 6 drawn; bit 0 on screen */
#define ATTR(e)       ((e) - 0x3FFE)   /* bit 3: faces left */
#define X(e)          ((e) - 0x3800)
#define Y(e)          ((e) - 0x3700)
#define VEL_X(e)      ((e) - 0x3600)
#define VEL_Y(e)      ((e) - 0x35FE)
#define HALF_H(e)     ((e) - 0x34FD)
#define ACCEL_X(e)    ((e) - 0x3400)
#define ACCEL_Y(e)    ((e) - 0x33FE)
#define COLL(e)       ((e) - 0x3100)   /* bit 1 on the ground */
#define CAMERA(e)     ((e) - 0x3E00)
#define TILES(e)      ((e) - 0x3DFE)
#define ANIM_SET(e)   ((e) - 0x3D00)
#define ANIM_ID(e)    ((e) - 0x3CFE)
#define ANIM_RATE(e)  ((e) - 0x3CFD)
#define ANIM_BUSY(e)  ((e) - 0x3A00)   /* byte: non-zero while the animation runs */
#define STEP(e)       ((e) - 0x2BFF)   /* byte: a magic entity's step; bit 7 once begun */
#define TYPE(e)       ((e) - 0x2BFE)   /* word: magic entities 0 spirit, 2 rock, 4 spark, 6 bolt;
                                        * enemies: its high byte 0 for one magic can hit */
#define TARGET(e)     ((e) - 0x2CFE)   /* word: a spirit's enemy, or -1 */
#define TOUCH(e)      ((e) - 0x2AFD)
#define TIMER(e)      ((e) - 0x2800)
#define LIFE(e)       ((e) - 0x27FF)
#define ANGLE(e)      ((e) - 0x27FE)
#define SPEED(e)      ((e) - 0x27FD)
#define HEALTH(e)     ((e) - 0x2600)
#define RESIST(e)     ((e) - 0x2300)   /* bits 0-1 spirits, 2-3 quake, 4-5 thunder: 3 immune */
#define HIT_BY(e)     ((e) - 0x22FE)
#define MAGIC_HIT(e)  ((e) - 0x21FF)   /* bit 7 hit by magic, bit 0 from the left */
#define MAGIC_DMG(e)  ((e) - 0x21FE)

enum { SPIRIT = 0, ROCK = 2, SPARK = 4, BOLT = 6 };
#define SLOTS         0xC0              /* the magic's eight entity slots */

static Cpu *cpu;                      /* the call into C: a real stack for calls into the 68000 */

static Ent ent(uint32_t addr) { return (Ent)(int32_t)rdw(addr); }

static uint8_t dec8(uint32_t a) { uint8_t v = (uint8_t)(rd8(a) - 1); wr8(a, v); return v; }

static void with_entity(Ent e, uint32_t addr) { Cpu t = *cpu; t.a[4] = e; call_68k(&t, addr); }

static void call_at(uint32_t addr) { Cpu t = *cpu; call_68k(&t, addr); }

static Cpu call_d0(uint32_t addr, uint32_t d0)
{
    Cpu t = *cpu;
    t.d[0] = d0;
    call_68k(&t, addr);
    return t;
}

static void queue_sound(int id) { call_d0(0x366, (uint32_t)id); }
static void clear_entity(Ent e) { with_entity(e, 0xA36); }
static void load_gfx(uint16_t block) { wrw(GFX_WANT, block); call_at(0x22F0); }
static void set_anim(Ent e, uint8_t id) { wr8(ANIM_ID(e), id); clr8(e - 0x3FFD, 0x81); }   /* $7E8 */

void UpdateAnim(Cpu *), BodyOverlap(Cpu *), Accel(Cpu *), ApplyVelocity(Cpu *);
static void hand(void (*fn)(Cpu *), Ent e) { Cpu t = *cpu; t.a[4] = e; fn(&t); }

/* $11402: one use of the spell gone, and the HUD's count; $87CE: the meter drawn */
static void use_up(void) { call_d0(0x8778, rd8(KIND) & 0x7F); }
static void draw_meter(uint16_t v) { call_d0(0x87CE, v); }

/* $11410: the spell is over; the HUD drawn again */
static void end_spell(void) { wr8(ACTIVE, 0); call_at(0x76C4); }

static void freeze(void)   { call_at(0xA66); }
static void unfreeze(void) { call_at(0xA8E); }

/* $5986: the screen white; $55BA: its colours back */
static void flash_white(void)
{
    for (uint32_t a = 0xFFFF8BD6u; a < 0xFFFF8C56u; a++) wr8(a, 0xFF);
    set8(PAL_FLAGS, 0x84);
}
static void flash_off(void)
{
    for (uint32_t k = 0; k < 0x80; k++) wr8(0xFFFF8BD6u + k, rd8(0xFFFF8B56u + k));
    wr8(PAL_FLAGS, (uint8_t)((rd8(PAL_FLAGS) & 0x3F) | 0x81));
}

/* a magic entity's first frame: 1 the first time it is asked (bit 7 of its step) */
static int begins(Ent e)
{
    uint8_t s = rd8(STEP(e));
    wr8(STEP(e), s | 0x80);
    return !(s & 0x80);
}

/* magic damage, a quarter of the player's maximum health (spirits: 1/32), less by the
 * enemy's resistance to this magic (two bits at `shift`: 3 immune) */
static void hurt_by_magic(Ent a, uint16_t base, int shift)
{
    uint16_t r = (uint16_t)((rd16(RESIST(a)) >> shift) & 3), d = 0;
    if (r != 3) d = (uint16_t)(base >> (2 * r));
    wrw(MAGIC_DMG(a), d);
}

/* an enemy magic can hit: in use, on screen, flag 6, type byte (one kind of enemy) and alive */
static int target(Ent a)
{
    return (rd8(ALIVE(a)) & 0xC1) == 0xC1 && !rd8(TYPE(a)) && rdw(HEALTH(a));
}

/* $11B4A: is there an enemy for the fire spirits? The first such enemy decides: the original
 * returns with the flags of its health, so one with "negative" health counts as none. */
static int enemy_for_spirits(void)
{
    for (Ent a = 0x40; a < 0x80; a += 4)
        if (target(a)) return rdw(HEALTH(a)) > 0;
    return 0;
}

/* $11B76: the same for thunder, with any type byte up to 2 */
static int enemy_for_thunder(void)
{
    for (Ent a = 0x40; a < 0x80; a += 4)
        if ((rd8(ALIVE(a)) & 0xC1) == 0xC1 && rd8(TYPE(a)) <= 2 && rdw(HEALTH(a))) return rdw(HEALTH(a)) > 0;
    return 0;
}

/* $1252: about how far a is from e: the longer side plus 3/8 of the shorter */
static uint16_t distance(Ent e, Ent a)
{
    uint16_t dx = (uint16_t)(rd16(X(a)) - rd16(X(e))), dy = (uint16_t)(rd16(Y(a)) - rd16(Y(e)));
    if (dx & 0x8000) dx = (uint16_t)-dx;
    if (dy & 0x8000) dy = (uint16_t)-dy;
    uint16_t big = dx, small = dy;
    if (dy >= dx) { big = dy; small = dx; }
    return (uint16_t)(big + (small >> 1) - (small >> 3));
}

/* $11B0E: the nearest enemy to the spirit e, or -1 */
static int16_t nearest_enemy(Ent e)
{
    int16_t best = -1; uint16_t d = 0;
    for (Ent a = 0x40; a < 0x80; a += 4) {
        if (!target(a)) continue;
        uint16_t k = distance(e, a);
        if (best < 0 || k < d) { d = k; best = (int16_t)a; }
    }
    return best;
}

/* the spell's first frame: 1 the first time it is asked (KIND bit 7) */
static int spell_begins(void)
{
    uint8_t k = rd8(KIND);
    wr8(KIND, k | 0x80);
    return !(k & 0x80);
}

/* ---- the magic's entities ($11832) ---- */

/* $1194C, step 0 ($11962): a spirit sets off at its angle, aimed at the nearest enemy */
static void spirit_start(Ent e)
{
    queue_sound(0x24);
    Cpu t = *cpu;
    t.d[3] = (uint32_t)rd8(ANGLE(e)) << 8; t.d[4] = rd8(SPEED(e));
    call_68k(&t, 0xFC0);                            /* the angle's x and y at that speed */
    wrw(ACCEL_X(e), (uint16_t)t.d[0]); wrw(ACCEL_Y(e), (uint16_t)t.d[5]);
    wrw(VEL_X(e), (int16_t)t.d[0] >> 3); wrw(VEL_Y(e), (int16_t)t.d[5] >> 3);
    set_anim(e, 0);
    set8(ALIVE(e), 0x40);
    wr8(ATTR(e), 0x80);
    wr8(ANIM_RATE(e), 10);
    wrw(TARGET(e), -1);
    int16_t a = nearest_enemy(e);
    if (a >= 0) wrw(TARGET(e), a);
    wr8(TIMER(e), 8);
    wr8(LIFE(e), 0xF0);
    wr8(STEP(e), 2);
}

/* $11A5A: touching its enemy: both marked (the enemy takes the damage, from the side touched) */
static void spirit_touch(Ent e)
{
    int16_t a = rdw(TARGET(e));
    if (a < 0) return;
    Cpu t = *cpu; t.a[4] = e; t.a[2] = (uint32_t)(int32_t)a;
    BodyOverlap(&t);
    if (!(t.ccr & (CC_C | CC_Z))) return;           /* bhi: apart */
    uint8_t left = (uint8_t)t.d[4];                 /* 1: the spirit is left of it */
    hurt_by_magic((Ent)(int32_t)a, (uint16_t)(rd16(MAX_HEALTH) >> 5), 0);
    set8(MAGIC_HIT(a), (uint8_t)(0x80 | left));
    wrw(HIT_BY(a), (uint16_t)e);
    set8(TOUCH(e), (uint8_t)(0x84 | (left ^ 1)));
    wrw(HIT_BY(e), (uint16_t)a);
}

/* $11AA4: slowing down by 1/16 a frame, steering, moving */
static void spirit_move(Ent e)
{
    wrw(VEL_X(e), rdw(VEL_X(e)) - (rdw(VEL_X(e)) >> 4));
    wrw(VEL_Y(e), rdw(VEL_Y(e)) - (rdw(VEL_Y(e)) >> 4));
    hand(Accel, e);
    hand(ApplyVelocity, e);
}

static void spirit_gone(Ent e) { wr8(ALIVE(e), 0); wr8(COUNT_A, (uint8_t)(rd8(COUNT_A) + 1)); }

/* $119C4, step 2: flying for 240 frames. Every 4th frame (each spirit on its own) it steers
 * at its enemy, or at the next nearest when that one is gone. The original then steers that
 * frame at entity $80, where its search ended, not at the new enemy: kept. */
static void spirit_fly(Ent e)
{
    if (rd8(TIMER(e))) wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    else {
        if (!dec8(LIFE(e))) { spirit_gone(e); return; }
        int16_t a = rdw(TARGET(e));
        int steer = 1;
        if (a < 0 || !(rd8(ALIVE(a)) & 0x80) || !rdw(HEALTH(a))) {
            wrw(TARGET(e), -1);
            int16_t n = nearest_enemy(e);
            if (n < 0) steer = 0;
            else { wrw(TARGET(e), n); a = 0x80; }
        }
        if (steer && !(((rd16(FRAME) & 3) - ((e >> 2) & 3)) & 0xFFFF)) {
            wr8(TIMER(e), 4);
            Cpu t = *cpu;
            t.a[4] = e; t.a[2] = (uint32_t)(int32_t)a; t.d[4] = rd8(SPEED(e));
            call_68k(&t, 0x1216);                   /* towards it */
            wrw(ACCEL_X(e), (uint16_t)t.d[0]); wrw(ACCEL_Y(e), (uint16_t)t.d[5]);
        }
    }
    wr8(TOUCH(e), 0);
    spirit_touch(e);
    spirit_move(e);
    if (rdw(TARGET(e)) < 0 && !(rd8(ALIVE(e)) & 1)) { spirit_gone(e); return; }   /* lost, off screen */
    if (rd8(TOUCH(e))) wr8(STEP(e), 4);
}

/* $11AC2, step 4: bursting on the enemy */
static void spirit_burst(Ent e)
{
    if (begins(e)) { set_anim(e, 2); return; }
    if (!rd8(ANIM_BUSY(e))) spirit_gone(e);
}

static void spirit(Ent e)
{
    switch (rd8(STEP(e)) & 0x7F) {
    case 0: spirit_start(e); break;
    case 2: spirit_fly(e); break;
    case 4: spirit_burst(e); break;
    default:
        fprintf(stderr, "magic: spirit step %d is outside the original's table ($1195C)\n", rd8(STEP(e)) & 0x7F);
        exit(3);
    }
}

/* $118BE: a quake's rock falls until its animation ends */
static void rock(Ent e)
{
    if (begins(e)) {
        queue_sound(0x2F);
        set_anim(e, 0);
        set8(ALIVE(e), 0x40);
        wr8(ANIM_RATE(e), 10);
        return;
    }
    if (!rd8(ANIM_BUSY(e))) { wr8(ALIVE(e), 0); wr8(ROCKS_DOWN, (uint8_t)(rd8(ROCKS_DOWN) + 1)); }
}

/* $11868: a shield spark, at the player; its slot's bit in COUNT_B is cleared when it ends */
static void spark(Ent e)
{
    if (begins(e)) {
        queue_sound(0x30);
        wr8(ANIM_RATE(e), 10);
        set_anim(e, rd8(ANIM_ID(e)));
        set8(ALIVE(e), 0x40);
        hand(UpdateAnim, e);
    }
    if ((int8_t)dec8(TIMER(e)) < 0) wr8(TIMER(e), 0);
    Ent p = ent(PLAYER_W);
    wrw(X(e), rdw(X(p)));
    wrw(Y(e), rdw(Y(p)));
    if (!rd8(ANIM_BUSY(e))) {
        wr8(ALIVE(e), 0);
        clr8(COUNT_B, (uint8_t)(1 << ((rd8(ANIM_ID(e)) >> 1) & 7)));
    }
}

/* $118F0: the thunder bolt, three steps across the screen; then the strike (ACTIVE bit 0) */
static void bolt(Ent e)
{
    static const uint8_t at[3][2] = {{0x80, 0x80}, {0x40, 0xB0}, {0xC0, 0x98}};
    if (begins(e)) {
        wr8(TIMER(e), 3);
        wr8(ANIM_RATE(e), 10);
        wrw(CAMERA(e), 4);                          /* fixed on the screen */
        set_anim(e, 0);
        set8(ALIVE(e), 0x40);
    }
    int8_t n = (int8_t)(rd8(TIMER(e)) - 1);
    if (n < 0) { wrw(ALIVE(e), 0); set8(ACTIVE, 1); return; }
    if (n > 2) {
        fprintf(stderr, "magic: bolt step %d is outside the original's table ($11946)\n", n);
        exit(3);
    }
    wr8(TIMER(e), (uint8_t)n);
    wrw(X(e), at[n][0]);
    wrw(Y(e), at[n][1]);
}

static void run_entities(void)
{
    for (Ent e = SLOTS; e < SLOTS + 0x20; e += 4) {
        if (!(rd8(ALIVE(e)) & 0x80)) continue;
        switch (rd16(TYPE(e))) {
        case SPIRIT: spirit(e); break;
        case ROCK: rock(e); break;
        case SPARK: spark(e); break;
        case BOLT: bolt(e); break;
        default:
            fprintf(stderr, "magic: entity type %d is outside the original's table ($11860)\n", rd16(TYPE(e)));
            exit(3);
        }
    }
}

/* ---- starting ($11170) ---- */

static const uint8_t spirit_angles[14] = {0xB0, 0x50, 0xA0, 0x60, 0x90, 0x70, 0x80,   /* $113F4 */
                                          0xD0, 0x30, 0xE0, 0x20, 0xF0, 0x10, 0x00};  /* facing left */

/* $11338: seven spirits round the player, if seven slots are free (the use is gone either way) */
static void start_spirits(Ent p)
{
    if (!enemy_for_spirits()) return;
    use_up();
    Ent free[7]; int n = 0;
    for (Ent e = SLOTS; e < SLOTS + 0x20 && n < 7; e += 4)
        if (!(rd8(ALIVE(e)) & 0x80)) free[n++] = e;
    if (n < 7) return;
    wr8(ACTIVE, 0x80);
    const uint8_t *angle = spirit_angles + (rd8(ATTR(p)) & 8 ? 7 : 0);
    for (int i = 0; i < 7; i++) {
        Ent e = free[i];
        clear_entity(e);
        wr8(ANGLE(e), angle[i]);
        wrw(TYPE(e), SPIRIT);
        wr8(STEP(e), 0);
        wr8(SPEED(e), 0x80);
        wrw(ANIM_SET(e), 0x1400);
        wrw(X(e), rdw(X(p)));
        wrw(Y(e), rdw(Y(p)));
        wrw(TILES(e), 0x6E0);
        set8(ALIVE(e), 0x80);
    }
    wrw(COUNT_A, 7);                                /* gone 0, of 7 */
    load_gfx(0xF);
}

static void start_meter(void)
{
    use_up();
    wrw(METER, 0x2000);
    draw_meter(0x2000);
}

/* $11204: kind 12, in areas 2 and 3 only */
static void start_kind12(void)
{
    if ((rd8(EQUIP) & 0x10) || (rd8(0xFFFF9659u) & 4) || rd8(0xFFFF9668u) < 2 || rd8(0xFFFF9668u) > 3
        || rdw(TIMER_9962)) return;
    wrw(TIMER_9962, 0xA);
    load_gfx(0x11);
    Ent p = ent(PLAYER_W);
    wrw(0xFFFF9EECu, rdw(ANIM_SET(p)));
    wrw(TILES(p), 0x6E0);
    wrw(ANIM_SET(p), 0x140A);
    set_anim(p, 0);
}

/* $1125C-$1129A: a potion, if there is one: used up, then the hearts refill ($F288). Returns 1
 * if it handed over to the original's refill (off a task pass). */
void player_refill(Cpu *c);
static int potion(Cpu *c, int n)
{
    static const uint16_t amount[3] = {0x100, 0x300, 0x500};
    uint8_t item = (uint8_t)(43 + n);
    wrw(REFILL, n < 3 ? amount[n] : rdw(MAX_HEALTH));
    Cpu t = call_d0(0x23E4, item);               /* has it? (Z clear if so) */
    if (t.ccr & CC_Z) return 0;
    call_d0(0x24DC, item);                       /* used up */
    call_at(0x76C4);
    if (!task_on_pass(c)) { port_continue_at(c, 0xF288, 0); return 1; }
    player_refill(c);
    return 0;
}

/* $11170: start special action `kind` (A + left/right). Returns 1 if it handed over to the
 * original's code (c->ret set): the caller must return at once. */
int magic_start(Cpu *c, uint8_t kind)
{
    cpu = c;
    if ((rd8(ACTIVE) & 0x80) || (kind & 0x80)) return 0;
    if ((rd8(CTRL) & 4) && kind < 5) return 0;
    wr8(KIND, kind);
    Ent p = c->a[4];
    switch (kind) {
    case 0: start_spirits(p); break;
    case 1:                                         /* quake */
        wrw(METER, kind); use_up(); wr8(ACTIVE, 0x80); load_gfx(0x1D);
        break;
    case 2:                                         /* thunder */
        if (!enemy_for_thunder()) break;
        queue_sound(0x3A);
        wrw(METER, kind); use_up(); wr8(ACTIVE, 0x80); load_gfx(0x1A8);
        break;
    case 3:                                         /* power */
        start_meter();
        wr8(ACTIVE, 0x80);
        set8(FALL_FLAGS, 0x20);
        break;
    case 4: case 6: case 7:                         /* shield */
        queue_sound(0x4D);
        load_gfx(rd8(FORM) == 2 ? 0x129 : 0x12C);
        start_meter();
        wr8(ACTIVE, 0x80);
        for (Ent e = SLOTS; e < SLOTS + 0x20; e += 4) wr8(TIMER(e), 0);
        break;
    case 5:
        if (!rdw(TIMER_9962)) { use_up(); wrw(TIMER_9962, 0xB); }
        break;
    case 8: case 9: case 10: case 11:
        return potion(c, kind - 8);
    case 12: start_kind12(); break;
    default:
        fprintf(stderr, "magic: kind %d is outside the original's table ($1119A)\n", kind);
        exit(3);
    }
    return 0;
}

/* ---- each frame ($1141A) ---- */

static void wait_frame(Cpu *c, uint32_t resume) { task_wait_frame(c, resume); cpu = c; }

/* $11454: power, until the meter is empty */
static void power_frame(void)
{
    int16_t m = rdw(METER);
    if (!m) { clr8(FALL_FLAGS, 0x20); end_spell(); return; }
    m = (int16_t)(m - 8);
    if (m < 0) m = 0;
    wrw(METER, m);
    draw_meter((uint16_t)m);
}

/* $11474: the shield, until the meter is empty and its sparks are gone */
static void shield_frame(void)
{
    if (spell_begins()) {
        set8(FALL_FLAGS, 0x40);
        wr8(SHIELD_HITS, 0);
        wr8(COUNT_B, 0);
    }
    if (!rdw(METER)) {
        if (rd8(COUNT_B)) { run_entities(); return; }
        clr8(FALL_FLAGS, 0x40);
        end_spell();
        return;
    }
    if (rd8(COUNT_B)) run_entities();
    int16_t m = (int16_t)(rdw(METER) - 8);
    if (m < 0) m = 0;
    wrw(METER, m);
    draw_meter((uint16_t)m);
}

/* $11548: a spark's cost, by the attacker's kind (the player's HIT_BY) */
static void spark_cost(Ent p)
{
    Ent a = ent(HIT_BY(p));
    int16_t m = (int16_t)(rdw(METER) - (0x400 << (rd8(RESIST(a)) & 3)));
    wrw(METER, m < 0 ? 0 : m);
}

/* $114C0: the shield was hit (from the player's damage, $E06E): a spark for each hurt box hit,
 * while there is meter. Its 8-frame free period is cleared again by ClearEntity at once, so in
 * the original every spark costs: kept. */
void magic_shield_hit(Cpu *c)
{
    cpu = c;
    Ent p = c->a[4];
    uint8_t hits = rd8(SHIELD_HITS);
    for (int i = 0; i < 8 && rdw(METER); i++, hits >>= 1) {
        if (!(hits & 1)) continue;
        Ent e = SLOTS + 4 * (Ent)i;
        set8(COUNT_B, (uint8_t)(1 << i));
        wr8(ANIM_ID(e), (uint8_t)(2 * i));
        wrw(X(e), rdw(X(p)));
        wrw(Y(e), rdw(Y(p)));
        if (!rd8(TIMER(e))) { spark_cost(p); wr8(TIMER(e), 8); }
        clear_entity(e);
        wr8(STEP(e), 0);
        wrw(ANIM_SET(e), rd8(FORM) == 2 ? 0x1404 : 0x1402);
        wrw(TYPE(e), SPARK);
        set8(ALIVE(e), 0x80);
        wrw(TILES(e), 0x6E0);
    }
    wr8(SHIELD_HITS, 0);
}

/* $11708: the quake shakes the screen 4 pixels about SCROLL_HOME, 2 a frame */
static void shake(void)
{
    int16_t home = rdw(SCROLL_HOME), y = rdw(SCROLL_Y);
    if (!(rd8(ACTIVE) & 1)) {
        y = (int16_t)(y + 2); home = (int16_t)(home + 4);
        if (y >= home) { y = home; wr8(ACTIVE, rd8(ACTIVE) ^ 1); }
    } else {
        y = (int16_t)(y - 2); home = (int16_t)(home - 4);
        if (y < home) { y = home; wr8(ACTIVE, rd8(ACTIVE) ^ 1); }
    }
    wrw(SCROLL_Y, y);
    call_at(0x9CD2);                         /* the scroll applied */
}

/* $11AE0: from enemy *a, the next of *n on the ground: 1 if found (*a it, *n the rest) */
static int next_on_ground(Ent *a, uint8_t *n)
{
    for (;;) {
        if ((rd8(ALIVE(*a)) & 0xC1) == 0xC1 && !rd8(TYPE(*a)) && rdw(HEALTH(*a)) && (rd8(COLL(*a)) & 0xA) == 2)
            return 1;
        *a += 4;
        if (!--*n) return 0;
    }
}

/* $11682: a rock onto enemy a (its damage now); 0 if no slot is free */
static int drop_rock(Ent a)
{
    Ent e = SLOTS;
    while (rd8(ALIVE(e)) & 0x80) if ((e += 4) == SLOTS + 0x20) return 0;
    wr8(ROCKS, (uint8_t)(rd8(ROCKS) + 1));
    clear_entity(e);
    wr8(STEP(e), 0);
    wr8(ALIVE(e), 0x80);
    wrw(TYPE(e), ROCK);
    wrw(ANIM_SET(e), 0x1406);
    wrw(TILES(e), 0x6E0);
    wrw(X(e), rdw(X(a)));
    wrw(Y(e), rd8(HALF_H(a)) + rdw(Y(a)));
    if (rd8(ATTR(a)) & 8) set8(MAGIC_HIT(a), 1);
    set8(MAGIC_HIT(a), 0x80);
    hurt_by_magic(a, (uint16_t)(rd16(MAX_HEALTH) >> 2), 2);
    return 1;
}

/* the quake, frame by frame, from each point where it waits for the next frame */
static void quake_ending(Cpu *c);
static void quake_settling(Cpu *c);
static void quake_rocks(Cpu *c);
static void quake_shaking(Cpu *c);

/* $11590: the world stops and the screen shakes for 20 frames */
static void quake(Cpu *c)
{
    c->a[7] = rd32(TASK_SP);                        /* the gameplay frame is abandoned */
    freeze();
    wrw(SCROLL_HOME, rdw(SCROLL_Y));
    wr8(COUNT_B, 0x14);
    wr8(ROCKS, 0);
    wr8(ROCKS_DOWN, 0);
    queue_sound(0x2E);
    wait_frame(c, 0x115BC);
    quake_shaking(c);
}

static void quake_shaking(Cpu *c)                   /* $115BC */
{
    for (;; wait_frame(c, 0x115BC)) {
        shake();
        if (!dec8(COUNT_B)) break;
    }
    wrw(NEXT_ENEMY, rdw(ENEMIES));                  /* then the first 16 enemies, */
    wr8(COUNT_B, 0x10);
    wr8(ROCK_DELAY, 4);
    wait_frame(c, 0x115DE);
    quake_rocks(c);
}

static void quake_rocks(Cpu *c)                     /* $115DE: a rock every 12 frames */
{
    for (;; wait_frame(c, 0x115DE)) {
        shake();
        run_entities();
        if (dec8(ROCK_DELAY)) continue;
        wr8(ROCK_DELAY, 12);
        Ent a = ent(NEXT_ENEMY);
        uint8_t n = rd8(COUNT_B);
        if (!next_on_ground(&a, &n)) break;
        wrw(NEXT_ENEMY, (uint16_t)(a + 4));
        wr8(COUNT_B, --n);
        if (!drop_rock(a)) {                        /* no slot: that enemy again next time */
            wrw(NEXT_ENEMY, rdw(NEXT_ENEMY) - 4);
            dec8(COUNT_B);
            continue;
        }
        if (!n) break;
    }
    wait_frame(c, 0x11626);
    quake_settling(c);
}

static void quake_settling(Cpu *c)                  /* $11626: until every rock has landed */
{
    for (; rd8(ROCKS) != rd8(ROCKS_DOWN); wait_frame(c, 0x11626)) {
        shake();
        run_entities();
    }
    wait_frame(c, 0x1163E);
    quake_ending(c);
}

/* $1163E: the screen back to SCROLL_HOME, 2 a frame; then the world goes on, from the top of
 * the gameplay loop */
static void quake_ending(Cpu *c)
{
    for (;; wait_frame(c, 0x1163E)) {
        int16_t home = rdw(SCROLL_HOME), y = rdw(SCROLL_Y);
        int at_home = y == home;
        if (!at_home && y > home) { y = (int16_t)(y - 2); at_home = y <= home; }
        else if (!at_home) { y = (int16_t)(y + 2); at_home = y >= home; }
        wrw(SCROLL_Y, at_home ? home : y);
        call_at(0x9CD2);
        if (at_home) break;
    }
    unfreeze();
    end_spell();
    task_end_pass(c, 0x8FFC);
}

/* the 16 enemies from ENEMIES that thunder hits ($117BA) */
static void thunder_hits(void)
{
    Ent a = ent(ENEMIES);
    for (int i = 0; i < 16; i++, a += 4) {
        if (rd8(TYPE(a)) || (rd8(ALIVE(a)) & 0xC1) != 0xC1) continue;
        if (rd8(ATTR(a)) & 8) set8(MAGIC_HIT(a), 1);
        set8(MAGIC_HIT(a), 0x80);
        hurt_by_magic(a, (uint16_t)(rd16(MAX_HEALTH) >> 2), 4);
    }
}

/* $11792: the screen flashes white three times, 2 frames on and 2 off; then every enemy is
 * hit and the world goes on, from the top of the gameplay loop */
static void thunder_flashes(Cpu *c)
{
    for (;; wait_frame(c, 0x11792)) {
        if (dec8(COUNT_A)) continue;
        wr8(COUNT_A, 2);
        uint8_t was = rd8(ACTIVE) & 1;            /* toggled: 1 -> white, 0 -> colours back */
        wr8(ACTIVE, rd8(ACTIVE) ^ 1);
        if (was) { flash_white(); continue; }
        flash_off();
        if (!dec8(COUNT_B)) break;
    }
    thunder_hits();
    unfreeze();
    end_spell();
    clr8(PAL_FLAGS, 4);
    task_end_pass(c, 0x8FFC);
}

/* $11740: thunder: the bolt (slot $C0) crosses the screen; when it is done (ACTIVE bit 0) the
 * world stops ($11780) and the flashes begin */
static void thunder_frame(Cpu *c)
{
    if (spell_begins()) {
        clear_entity(SLOTS);
        wr8(STEP(SLOTS), 0);
        wr8(ALIVE(SLOTS), 0x80);
        wrw(TYPE(SLOTS), BOLT);
        wrw(ANIM_SET(SLOTS), 0x1408);
        wrw(TILES(SLOTS), 0x6E0);
        wrw(COUNT_A, 0x203);                        /* 3 flashes, 2 frames each */
    }
    if (!(rd8(ACTIVE) & 1)) { run_entities(); return; }
    if (!task_on_pass(c)) { port_continue_at(c, 0x11780, 0); return; }
    c->a[7] = rd32(TASK_SP);
    freeze();
    flash_white();
    wait_frame(c, 0x11792);
    thunder_flashes(c);
}

static void frame(Cpu *c)
{
    if (!(rd8(ACTIVE) & 0x80)) return;
    switch (rd8(KIND) & 0x7F) {
    case 0:                                         /* $11822: spirits, until all are gone */
        run_entities();
        if (rd8(COUNT_A) >= rd8(COUNT_B)) end_spell();
        break;
    case 1:
        if (!task_on_pass(c)) { port_continue_at(c, 0x11590, 0); return; }
        quake(c);
        break;
    case 2: case 5: thunder_frame(c); break;
    case 3: power_frame(); break;
    case 4: case 6: case 7: shield_frame(); break;
    default:
        fprintf(stderr, "magic: kind %d is outside the original's frame table ($11434)\n", rd8(KIND) & 0x7F);
        exit(3);
    }
}

void MagicStart(Cpu *c)      { magic_start(c, (uint8_t)c->d[0]); }
void MagicFrame(Cpu *c)      { cpu = c; frame(c); }
void MagicShieldHit(Cpu *c)  { magic_shield_hit(c); }
void QuakeShaking(Cpu *c)    { cpu = c; quake_shaking(c); }
void QuakeRocks(Cpu *c)      { cpu = c; quake_rocks(c); }
void QuakeSettling(Cpu *c)   { cpu = c; quake_settling(c); }
void QuakeEnding(Cpu *c)     { cpu = c; quake_ending(c); }
void ThunderFlashes(Cpu *c)  { cpu = c; thunder_flashes(c); }
