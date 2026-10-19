/* The enemy library ($3000-$5000): the small routines the enemies' actions share. Batch T45 on.
 *
 * Plain C, as from batch T42: each promises the stack pointer and what it writes, and the
 * registers and flags its callers use (the C actions' notes name them: z, mi, cs, a register).
 * Several return from their caller when they act (addq #4,a7); their entries pop that frame too.
 * a4 is the entity throughout (its fields at -$xxxx(a4)); a2 another (the target at $A11C). */
#include "world.h"
#include "task.h"

static Cpu *c;
static void call(uint32_t addr) { Cpu *self = c; call_68k(c, addr); c = self; }   /* c kept: see port/rooms.c */

static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static void f_bit(int on) { c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (on ? 0 : CC_Z)); }
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }
static uint32_t e(int32_t off) { return c->a[4] + (uint32_t)off; }     /* -$xxxx(a4) */
static uint32_t o(int32_t off) { return c->a[2] + (uint32_t)off; }     /* -$xxxx(a2) */
static void put8(uint32_t a, uint32_t v) { wr8(a, (uint8_t)v); F_B(v); }
static void put16(uint32_t a, uint32_t v) { wr16(a, (uint16_t)v); F_W(v); }
static void or8(uint32_t a, uint8_t m) { uint8_t v = rd8(a) | m; wr8(a, v); F_B(v); }
static void and8(uint32_t a, uint8_t m) { uint8_t v = rd8(a) & m; wr8(a, v); F_B(v); }
static void neg_d(int r) { c->d[r] = set_w(c->d[r], neg_w(&c->ccr, c->d[r])); }
static void swap_a2a4(void) { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }
static void target(void) { c->a[2] = sx(rd16(0xFFFFA11Cu)); }          /* movea.w $A11C,a2 */
static int facing_left(void) { int on = rd8(e(-0x3FFE)) >> 3 & 1; f_bit(on); return on; }

#define SET_ANIM      0x07E8u
#define CLEAR_ENTITY  0x0A36u
#define RANDOM        0x05D8u

/* $41AA: a2 made a copy of this one: cleared, in use, its looks, place, home, health and kind */
static void copy_to_a2(void)
{
    swap_a2a4();
    put8(e(-0x4000), 0x80);
    call(CLEAR_ENTITY);
    swap_a2a4();
    static const int16_t w[] = { -0x3DFE, -0x3D00, -0x3800, -0x3700 };
    for (int k = 0; k < 4; k++) put16(o(w[k]), rd16(e(w[k])));
    wr32(o(-0x2700), rd32(e(-0x2700))); F_L(rd32(e(-0x2700)));
    put16(o(-0x2600), rd16(e(-0x2600)));
    put16(o(-0x2300), rd16(e(-0x2300)));
    wr32(o(-0x2100), rd32(e(-0x2100))); F_L(rd32(e(-0x2100)));
    put16(o(-0x2B00), rd16(e(-0x2B00)));
}
/* $41A2: a2 its partner (each one's -$2CFE the other) and a copy of it */
static void make_partner(void)
{
    wr16(e(-0x2CFE), (uint16_t)c->a[2]);
    wr16(o(-0x2CFE), (uint16_t)c->a[4]);
    copy_to_a2();
}
/* $421C: a bubble (item $204, or $210 by the random in d0) at its top, $10910 */
static void bubble_at_top(void)
{
    c->d[1] = set_w(c->d[1], 0x204);
    c->d[0] = set_w(c->d[0], c->d[0] & 3);
    if (c->d[0] & 0xFFFF) c->d[1] = set_w(c->d[1], 0x210);
    c->d[0] = set_w(c->d[0], c->d[1]);
    c->d[1] = (uint32_t)rd8(e(-0x34FE)) >> 1;
    if (facing_left()) neg_d(1);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(e(-0x3800)), c->d[1]));
    c->d[2] = rd8(e(-0x34FD));
    neg_d(2);
    c->d[2] = set_w(c->d[2], add_w(&c->ccr, rd16(e(-0x3700)), c->d[2]));
    call(0x10910);
}
/* $4218: a bubble */
static void bubble(void) { call(RANDOM); bubble_at_top(); }
/* $41F0: in water, every 64 frames, 1 in 4: a bubble */
static void maybe_bubble(void)
{
    f_bit(rd8(0xFFFF966Bu) & 1);
    if (!(rd8(0xFFFF966Bu) & 1)) return;
    c->d[0] = set_w(0x3F, 0x3F & rd16(0xFFFF965Au)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) return;
    call(RANDOM);
    c->d[1] = set_w(c->d[1], c->d[0] & 3); F_W(c->d[1]);
    if (c->d[1] & 0xFFFF) return;
    bubble_at_top();
}
/* $420E (unreferenced): in water, a bubble */
static void bubble_in_water(void)
{
    f_bit(rd8(0xFFFF966Bu) & 1);
    if (rd8(0xFFFF966Bu) & 1) bubble();
}
/* $425C: its speeds lose a sixteenth, then its speed held to SPEED ($B42) */
static void swim_limit(void)
{
    static const int16_t f[] = { -0x3600, -0x35FE };
    for (int k = 0; k < 2; k++) {
        c->d[1] = set_w(c->d[1], rd16(e(f[k])));
        c->d[1] = set_w(c->d[1], (uint16_t)((int16_t)c->d[1] >> 4));
        wr16(e(f[k]), (uint16_t)sub_w(&c->ccr, c->d[1] & 0xFFFF, rd16(e(f[k]))));
    }
    call(0xB42);
}
/* $4254: the same, then moved ($AB4) */
static void swim_move(void) { swim_limit(); call(0xAB4); }
/* $4274: its vertical acceleration toward the height d0 (give or take d1 by its direction), d2 */
static void bob_toward(void)
{
    if (rd16(e(-0x35FE)) & 0x8000) neg_d(1);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
    cmp_w(&c->ccr, rd16(e(-0x3700)), c->d[0]);
    if ((uint16_t)c->d[0] < rd16(e(-0x3700))) neg_d(2);
    put16(e(-0x33FE), c->d[2]);
}
/* $428C: a random one of eight bytes at a0 + d2: 0 nothing, negative an action ($13542), else
 * its state */
static int random_state(void)
{
    call(RANDOM);
    c->d[0] = set_w(c->d[0], c->d[0] & 7);
    c->d[0] = set_w(c->d[0], c->d[0] + c->d[2]);
    c->d[0] = set_b(c->d[0], rd8(c->a[0] + sx(c->d[0]))); F_B(c->d[0]);
    if (!(c->d[0] & 0xFF)) return 0;
    if (c->d[0] & 0x80) { port_continue_at(c, 0x13542, 0); return 1; }
    put8(e(-0x2BFF), c->d[0]);
    return 0;
}
/* $4356: on the ground: slowed by $40 ($B92, here: its flags decide $42AA); else nz */
static void slow_on_ground(void)
{
    call(0x4362);
    if (!(c->ccr & CC_Z)) return;
    c->d[0] = 0x40;
    uint16_t v = rd16(e(-0x3600));
    c->d[1] = set_w(c->d[1], v);
    if (!v) { F_W(0); return; }
    uint16_t r = (uint16_t)(v & 0x8000 ? v + 0x40 : v - 0x40);
    if ((v & 0x8000) ? !(r & 0x8000) : (r & 0x8000)) c->d[1] = 0;   /* moveq #0,d1 */
    else c->d[1] = set_w(c->d[1], r);
    put16(e(-0x3600), c->d[1]);
}
/* $42AA: on the ground, facing the target, and while -$27FF runs (or once it is out): the caller
 * goes on. Otherwise it returns at once (1) */
static int grounded_wait(void)
{
    slow_on_ground();
    if (!(c->ccr & CC_Z)) return 1;
    call(0x44D6);
    F_B(rd8(e(-0x27FF)));
    if (!rd8(e(-0x27FF))) return 0;
    uint8_t n = (uint8_t)(rd8(e(-0x27FF)) - 1);
    wr8(e(-0x27FF), n); F_B(n);
    return n != 0;
}
/* $42C4 and $42CC: touching the player (and the sword), or the shield; then falling and moving */
static void contact_and_move(void) { call(0x4792); call(0x46D6); }
static void shield_and_move(void) { call(0x46F4); call(0x46D6); }
/* $4340: a random offset within d3 (d2: the remainder of the random by d3, negative half the time) */
static void random_spread(void)
{
    call(RANDOM);
    uint16_t num = (uint16_t)c->d[0], den = (uint16_t)c->d[3] ? (uint16_t)c->d[3] : 1;   /* (0 traps in the original) */
    uint16_t q = (uint16_t)(num / den), r = (uint16_t)(num % den);
    c->d[2] = (uint32_t)q << 16 | r;
    if (c->d[0] & 0x10000) c->d[2] = set_w(c->d[2], (uint16_t)-r);
}
/* $42EA: a piece of debris (look d6, sprite flags d1) in a free slot ($12812), about this one
 * within (d4, d5), kind $218, at once; mi if none (d0 0 and z otherwise) */
static void debris(void)
{
    call(0x12812);
    if (c->ccr & CC_N) return;
    swap_a2a4();
    call(CLEAR_ENTITY);
    put8(e(-0x4000), 0x80);
    put8(e(-0x3FFE), c->d[1]);
    put16(e(-0x3D00), c->d[6]);
    put8(e(-0x3CFD), 0xA);
    c->d[3] = set_w(c->d[3], c->d[4]);
    random_spread();
    c->d[2] = set_w(c->d[2], c->d[2] + rd16(o(-0x3800)));
    put16(e(-0x3800), c->d[2]);
    c->d[3] = set_w(c->d[3], c->d[5]);
    random_spread();
    c->d[2] = set_w(c->d[2], c->d[2] + rd16(o(-0x3700)));
    put16(e(-0x3700), c->d[2]);
    put16(e(-0x2BFE), 0x218);
    c->d[0] = 0;
    call(SET_ANIM);
    wr8(e(-0x3A00), 0xFF);
    or8(e(-0x4000), 0x40);
    swap_a2a4();
    c->d[0] = 0; F_L(0);
}
static void spawn_debris(void) { c->d[1] = 0; debris(); }                           /* $42E8 */
static void puff(uint16_t look)                                                     /* $42D4, $42DA */
{
    c->d[6] = set_w(c->d[6], look);
    c->d[0] = 0x3F; call(0x366);
    c->d[1] = 0xFFFFFF80u;
    debris();
}
static void puff_a(void) { puff(0x1208); }
static void puff_b(void) { puff(0x120A); }

/* $4388: backing off: VEL_X at twice its speed, away from where it faces */
static void back_off_now(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x3300)) * 2);
    if (!facing_left()) neg_d(0);
    put16(e(-0x3600), c->d[0]);
}
/* $437E: touched ($D503 bit 4): backing off */
static void back_off(void)
{
    f_bit(rd8(e(-0x2AFD)) >> 4 & 1);
    if (rd8(e(-0x2AFD)) & 0x10) back_off_now();
}
/* $436C (unreferenced): touched: state d0, backing off, and the caller returns (1) */
static int touched_state(void)
{
    f_bit(rd8(e(-0x2AFD)) >> 4 & 1);
    if (!(rd8(e(-0x2AFD)) & 0x10)) return 0;
    put8(e(-0x2BFF), c->d[0]);
    back_off_now();
    return 1;
}
/* $439E: hit: mi. A hit that hurts ($DE01 bit 7, not already $D503 bit 7) leaves its side in
 * $D503 bit 0 */
static void take_hit(void)
{
    c->d[0] = set_b(c->d[0], rd8(e(-0x2AFD))); F_B(c->d[0]);
    if (c->d[0] & 0x80) return;
    c->d[1] = set_b(c->d[1], rd8(e(-0x21FF))); F_B(c->d[1]);
    if (!(c->d[1] & 0x80)) return;
    c->d[0] = set_b(c->d[0], (c->d[0] & 0xFE) | (c->d[1] & 1));
    put8(e(-0x2AFD), c->d[0]);
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
}
/* $43CC, $43DE, $43F0, $4402, $4414: timers and ranges from the random */
static void random_timer(void)    { call(RANDOM); c->d[0] = set_b(c->d[0], c->d[0] & 0x7F); c->d[0] = set_w(c->d[0], c->d[0] + 0x40); put8(e(-0x2800), c->d[0]); }
static void random_timer_32(void) { call(RANDOM); c->d[0] = set_w(c->d[0], (c->d[0] & 0x1F) + 0x20); put8(e(-0x2800), c->d[0]); }
static void random_delay(void)    { call(RANDOM); c->d[0] = set_b(c->d[0], (c->d[0] & 0x1F) + 0x10); put8(e(-0x27FF), c->d[0]); }
static void range_40(void)        { call(RANDOM); c->d[0] = set_w(c->d[0], (c->d[0] & 0x3F) + 0x40); put16(e(-0x31FE), c->d[0]); }
static void range_80(void)        { call(RANDOM); c->d[1] = set_w(c->d[1], c->d[0]); c->d[0] = set_w(c->d[0], (c->d[0] & 0x7F) + 0x80); put16(e(-0x31FE), c->d[0]); }

/* $4496: VEL_X d0 by its facing; $4492: at its speed */
static void set_vel_x_facing(void)
{
    if (facing_left()) neg_d(0);
    put16(e(-0x3600), c->d[0]);
}
static void walk_forward(void) { c->d[0] = set_w(c->d[0], rd16(e(-0x3300))); F_W(c->d[0]); set_vel_x_facing(); }
/* $448C: turned about, walking on */
static void turn(void)
{
    uint8_t v = rd8(e(-0x3FFE)); f_bit(v >> 3 & 1); wr8(e(-0x3FFE), (uint8_t)(v ^ 8));
    walk_forward();
}
/* $445C: facing away from its touch ($D503 bit 0), walking on */
static void face_away(void)
{
    f_bit(rd8(e(-0x2AFD)) & 1);
    if (rd8(e(-0x2AFD)) & 1) and8(e(-0x3FFE), 0xF7); else or8(e(-0x3FFE), 8);
    walk_forward();
}
/* the wall test: $CF00 & $41 == 1 (against a wall); z */
static int at_wall(void)
{
    c->d[0] = 0x41 & rd8(e(-0x3100));
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
    return !(c->d[0] & 0xFF);
}
static void turn_at_wall(void) { if (at_wall()) turn(); }                           /* $4438 */
static int bumped(void) { int on = rd8(e(-0x2AFD)) >> 2 & 1; f_bit(on); return on; }   /* $D503 bit 2 */
/* $44A6: facing the way it moves */
static void face_velocity(void)
{
    F_W(rd16(e(-0x3600)));
    if (rd16(e(-0x3600)) & 0x8000) or8(e(-0x3FFE), 8); else and8(e(-0x3FFE), 0xF7);
}
/* $44BC: VEL_X to reach a2 in d1 frames: (a2's x - its x) << 8 / d1 (the original's lsl.w keeps
 * only the low word's bits: kept), as divs does, unchanged on overflow */
static void velocity_toward(void)
{
    int16_t diff = (int16_t)(rd16(o(-0x3800)) - rd16(e(-0x3800)));
    uint32_t n = (diff < 0 ? 0xFFFF0000u : 0) | (uint16_t)((uint16_t)diff << 8);
    int16_t den = (int16_t)c->d[1] ? (int16_t)c->d[1] : 1;   /* (0 traps in the original) */
    int32_t q = (int32_t)n / den, r = (int32_t)n % den;
    if (q >= -32768 && q <= 32767) c->d[0] = (uint32_t)(uint16_t)r << 16 | (uint16_t)q;
    else c->d[0] = n;
    put16(e(-0x3600), c->d[0]);
}
/* $44DE: facing x d0 (d0: the distance, signed) */
static void face_x(void)
{
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(e(-0x3800)), c->d[0]));
    if (c->d[0] & 0x8000) or8(e(-0x3FFE), 8); else and8(e(-0x3FFE), 0xF7);
}
static void face_entity(void) { c->d[0] = set_w(c->d[0], rd16(o(-0x3800))); face_x(); }   /* $44DA */
static void face_player(void) { target(); face_entity(); }                            /* $44D6 */
static void face_and_walk(void) { face_player(); walk_forward(); }                    /* $44D0 */
/* $4508: VEL_Y d0, off the ground */
static void set_vel_y(void)
{
    and8(e(-0x3100), 0x75);
    put16(e(-0x35FE), c->d[0]);
}
static void jump(void) { c->d[0] = set_w(c->d[0], rd16(e(-0x32FE)) + 0x80); neg_d(0); set_vel_y(); }   /* $44F4 */
static void hop(void) { c->d[0] = set_w(c->d[0], rd16(e(-0x32FE)) >> 1); neg_d(0); set_vel_y(); }      /* $4500 */
/* $451A: blinking (30 frames, the flash $2101); $4514: frozen too */
static void start_blink(void)
{
    or8(e(-0x3FFD), 0x40);
    put16(e(-0x3900), 0x2101);
    put8(e(-0x2800), 0x1E);
}
static void start_blink_frozen(void) { or8(e(-0x4000), 0x20); start_blink(); }
/* $453C: knocked back at d0 ($100 in the air), away from the hit ($D503 bit 0) */
static void knock_back_at(void)
{
    c->d[1] = set_w(c->d[1], c->d[0]);
    call(0x4362);
    if (!(c->ccr & CC_Z)) c->d[1] = set_w(c->d[1], 0x100);
    f_bit(rd8(e(-0x2AFD)) & 1);
    if (!(rd8(e(-0x2AFD)) & 1)) neg_d(1);
    put16(e(-0x3600), c->d[1]);
}
static void knock_back(void) { c->d[0] = set_w(c->d[0], 0x400); knock_back_at(); }   /* $4558 */
/* $452E: thrown up at its jump speed and knocked back at $200 */
static void thrown_back(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x32FE)));
    neg_d(0);
    set_vel_y();
    c->d[0] = set_w(c->d[0], 0x200);
    knock_back_at();
}
/* $4562: d1 the distance to a2 across, d0 8 if a2 is to its left (else 0) */
static void dist_x_to_entity(void)
{
    c->d[0] = 8;
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(e(-0x3800)), rd16(o(-0x3800))));
    if (c->ccr & CC_C) { neg_d(1); return; }
    c->d[0] = 0; F_L(0);
}
static void dist_x_to_player(void) { target(); dist_x_to_entity(); }                 /* $455E */
/* $457A: d1 a2's y less its own (the flags of the sub.w); $4588 the same made positive */
static void dist_y(void) { c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(e(-0x3700)), rd16(o(-0x3700)))); }
static void dist_y_abs(void) { dist_y(); if (c->d[1] & 0x8000) neg_d(1); }
static void dist_y_to_target(void) { target(); dist_y(); }                            /* $4576 */
static void dist_y_abs_target(void) { target(); dist_y_abs(); }                       /* $4584 */
/* $45AC: nz if there is room to jump up the wall ahead (the cell $30 above, a step past its
 * front, is not solid: $30CE's d2 & $103 other than 3) */
static void room_above(void)
{
    c->d[6] = rd8(e(-0x34FE));
    c->d[6] = set_w(c->d[6], c->d[6] + 1);
    if (facing_left()) neg_d(6);
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, rd16(e(-0x3800)), c->d[6]));
    c->d[7] = rd8(e(-0x34FD));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, rd16(e(-0x3700)), c->d[7]));
    c->d[7] = set_w(c->d[7], sub_w(&c->ccr, 0x30, c->d[7]));
    call(0x30CE);
    c->d[2] = set_w(c->d[2], c->d[2] & 0x103);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 3, c->d[2]));
}
/* $45F4: cc when as far from home (-$2100) as d2 the way it faces (d1 - d2) */
static void past_range(void)
{
    c->d[1] = set_w(c->d[1], rd16(e(-0x3800)));
    if (!facing_left()) {
        c->d[2] = set_w(c->d[2], add_w(&c->ccr, rd16(e(-0x2100)), c->d[2]));
    } else {
        neg_d(2);
        c->d[2] = set_w(c->d[2], add_w(&c->ccr, rd16(e(-0x2100)), c->d[2]));
        uint32_t t = c->d[1]; c->d[1] = c->d[2]; c->d[2] = t;
    }
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, c->d[2], c->d[1]));
}
static void past_range_80(void) { c->d[2] = set_w(c->d[2], 0x80); past_range(); }   /* $45F0 */

#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
/* an entry that returns from its caller when fn says so (addq #4,a7 in the original) */
#define ENTRY_POP(name, fn) void name(Cpu *cpu) { c = cpu; if (fn()) c->a[7] += 4; c->ret = 0; }
ENTRY(MakePartner, make_partner)           /* $41A2 */
ENTRY(CopyToA2, copy_to_a2)                /* $41AA */
ENTRY(MaybeBubble, maybe_bubble)           /* $41F0 */
ENTRY(BubbleInWater, bubble_in_water)      /* $420E */
ENTRY(Bubble, bubble)                      /* $4218 */
ENTRY(SwimMove, swim_move)                 /* $4254 */
ENTRY(SwimLimit, swim_limit)               /* $425C */
ENTRY(BobToward, bob_toward)               /* $4274 */
void RandomState(Cpu *cpu) { c = cpu; if (!random_state()) c->ret = 0; }   /* $428C: may go on at $13542 */
ENTRY_POP(GroundedWait, grounded_wait)     /* $42AA */
ENTRY(ContactAndMove, contact_and_move)    /* $42C4 */
ENTRY(ShieldAndMove, shield_and_move)      /* $42CC */
ENTRY(PuffA, puff_a)                       /* $42D4 */
ENTRY(PuffB, puff_b)                       /* $42DA */
ENTRY(SpawnDebris, spawn_debris)           /* $42E8 */
ENTRY(RandomSpread, random_spread)         /* $4340 */
ENTRY(SlowOnGround, slow_on_ground)        /* $4356 */
ENTRY_POP(TouchedState, touched_state)     /* $436C */
ENTRY(BackOff, back_off)                   /* $437E */
ENTRY(TakeHit, take_hit)                   /* $439E */
/* $43BC: hit: the caller returns, and the action goes to $1357E with d0 2 */
void HitAction(Cpu *cpu)
{
    c = cpu; c->ret = 0;
    take_hit();
    if (!(c->ccr & CC_N)) return;
    c->d[0] = 2; F_L(2);
    c->a[7] += 4;
    port_continue_at(c, 0x1357E, 0);
}
ENTRY(RandomTimer, random_timer)           /* $43CC */
ENTRY(RandomTimer32, random_timer_32)      /* $43DE */
ENTRY(RandomDelay, random_delay)           /* $43F0 */
ENTRY(Range40, range_40)                   /* $4402 */
ENTRY(Range80, range_80)                   /* $4414 */
/* $4428: against a wall: turned, and the caller returns */
static int wall_turn_return(void) { if (!at_wall()) return 0; turn(); return 1; }
ENTRY_POP(WallTurnReturn, wall_turn_return)
ENTRY(TurnAtWall, turn_at_wall)            /* $4438 */
/* $4444: bumped ($D503 bit 2): facing away, and the caller returns */
static int bump_away_return(void) { if (!bumped()) return 0; face_away(); return 1; }
ENTRY_POP(BumpAwayReturn, bump_away_return)
static void bump_away(void) { if (bumped()) face_away(); }                         /* $4452 (unreferenced) */
ENTRY(BumpAway, bump_away)
static int bump_turn_return(void) { if (!bumped()) return 0; turn(); return 1; }   /* $4474 (unreferenced) */
ENTRY_POP(BumpTurnReturn, bump_turn_return)
static void bump_turn(void) { if (bumped()) turn(); }                              /* $4482 (unreferenced) */
ENTRY(BumpTurn, bump_turn)
ENTRY(FaceAway, face_away)                 /* $445C */
ENTRY(Turn, turn)                          /* $448C */
ENTRY(WalkForward, walk_forward)           /* $4492 */
ENTRY(SetVelXFacing, set_vel_x_facing)     /* $4496 */
ENTRY(FaceVelocity, face_velocity)         /* $44A6 */
ENTRY(VelocityToward, velocity_toward)     /* $44BC */
ENTRY(FaceAndWalk, face_and_walk)          /* $44D0 */
ENTRY(FacePlayer, face_player)             /* $44D6 */
ENTRY(FaceEntity, face_entity)             /* $44DA */
ENTRY(FaceX, face_x)                       /* $44DE */
ENTRY(Jump, jump)                          /* $44F4 */
ENTRY(Hop, hop)                            /* $4500 */
ENTRY(SetVelY, set_vel_y)                  /* $4508 */
ENTRY(StartBlinkFrozen, start_blink_frozen)   /* $4514 */
ENTRY(StartBlink, start_blink)             /* $451A */
ENTRY(ThrownBack, thrown_back)             /* $452E */
ENTRY(KnockBackAt, knock_back_at)          /* $453C */
ENTRY(KnockBack, knock_back)               /* $4558 */
ENTRY(DistXToPlayer, dist_x_to_player)     /* $455E */
ENTRY(DistXToEntity, dist_x_to_entity)     /* $4562 */
ENTRY(DistYToTarget, dist_y_to_target)     /* $4576 */
ENTRY(DistY, dist_y)                       /* $457A */
ENTRY(DistYAbsTarget, dist_y_abs_target)   /* $4584 */
ENTRY(DistYAbs, dist_y_abs)                /* $4588 */
/* $4592 (unreferenced): against a wall with no room above: the caller returns, action 5 */
void WallClimbAction(Cpu *cpu)
{
    c = cpu; c->ret = 0;
    if (!at_wall()) return;
    room_above();
    c->ret = 0;
    if (!(c->ccr & CC_Z)) return;
    c->a[7] += 4;
    c->d[0] = 5; F_L(5);
    port_continue_at(c, 0x13542, 0);
}
ENTRY(RoomAbove, room_above)               /* $45AC */
/* $45DC: past its range: turned, and the caller returns */
static int range_turn_return(void) { past_range(); if (c->ccr & CC_C) return 0; turn(); return 1; }
ENTRY_POP(RangeTurnReturn, range_turn_return)
static void range_turn(void) { past_range(); if (!(c->ccr & CC_C)) turn(); }      /* $45E8 (unreferenced) */
ENTRY(RangeTurn, range_turn)
ENTRY(PastRange80, past_range_80)          /* $45F0 */
ENTRY(PastRange, past_range)               /* $45F4 */

/* ---- batch T46: ranges and edges, touching the player, defeat and despawning, damage ---- */

#define PLAYER_ENT    0xFFFF9EEEu   /* word: the player's slot */
#define FALL_FLAGS    0xFFFF9F03u   /* bit 6: the shield is up; bit 5: the hit counts double */
#define DAMAGE_SHOWN  0xFFFF9628u   /* words: the enemy's maximum and health, for the HUD */

/* $4616: cs if the target ($A11C) is within d2 of its home (-$2100) across (d1: the distance) */
static void target_from_home(void)
{
    target();
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(e(-0x2100)), rd16(o(-0x3800))));
    if (c->ccr & CC_C) neg_d(1);
    cmp_w(&c->ccr, c->d[2], c->d[1]);
}
/* $4646: on a moving platform (-$2D00), 0 at its end the way it moves, 1 otherwise; -1 (mi) when
 * not on one */
static void edge_ahead(void)
{
    c->d[0] = set_b(c->d[0], rd8(e(-0x3100)) & 0x8A);
    cmp_b(&c->ccr, 0x82, c->d[0]);
    if ((c->d[0] & 0xFF) != 0x82) { c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]); return; }
    c->a[0] = sx(rd16(e(-0x2D00)));
    c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x601E));
    int16_t v = (int16_t)rd16(e(-0x3600));
    int one;
    if (!v) one = 1;
    else if (v > 0) {
        c->d[0] = set_w(c->d[0], c->d[0] + rd16(c->a[0] - 0x5F76));
        one = (uint16_t)c->d[0] >= rd16(e(-0x3800));
    } else {
        c->d[0] = set_w(c->d[0], c->d[0] - rd16(c->a[0] - 0x5F76));
        one = (uint16_t)c->d[0] < rd16(e(-0x3800));
    }
    c->d[0] = one ? 1 : 0; F_L(c->d[0]);
}
/* $4686: 0 (z) if there is no floor ahead the way it moves ($30DA's d2 bit 3 clear); 1 standing */
static void ground_ahead(void)
{
    c->d[6] = rd8(e(-0x34FE));
    int16_t v = (int16_t)rd16(e(-0x3600));
    if (!v) { c->d[0] = 1; F_L(1); return; }
    if (v < 0) neg_d(6);
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, rd16(e(-0x3800)), c->d[6]));
    c->d[7] = rd8(e(-0x34FD));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, rd16(e(-0x3700)), c->d[7]));
    c->d[6] = set_w(c->d[6], (uint16_t)c->d[6] >> 4);
    c->d[7] = set_w(c->d[7], (c->d[7] & 0xFFF0) << 2);
    c->a[3] = 0xFFFF9754u;
    call(0x30DA);
    f_bit(c->d[2] >> 3 & 1);
}
/* the edge test of $462A: z when it would walk off (its platform's end, or no floor) */
static int at_edge(void)
{
    edge_ahead();
    if (c->ccr & CC_N) ground_ahead();
    return (c->ccr & CC_Z) != 0;
}
static int edge_turn_return(void) { if (!at_edge()) return 0; turn(); return 1; }   /* $462A */
static void edge_turn(void) { if (at_edge()) turn(); }                             /* $463A (unreferenced) */
/* $46C6: ACCEL_X d0 by its facing; $46BE: a quarter of its speed */
static void accel_facing(void)
{
    if (facing_left()) neg_d(0);
    put16(e(-0x3400), c->d[0]);
}
static void fly_facing(void) { c->d[0] = (uint32_t)rd16(e(-0x3300)) >> 2; accel_facing(); }
/* $46DA: moved and kept out of the walls ($3FAC, $3F58), splashing in water when on the screen
 * ($DD7C), its sprite ($2FD0); $46D6: falling first ($B44) */
static void enemy_move(void)
{
    call(0x3FAC);
    call(0x3F58);
    f_bit(rd8(e(-0x4000)) & 1);
    if (rd8(e(-0x4000)) & 1) call(0xDD7C);
    call(0x2FD0);
}
static void fall_and_move(void) { call(0xB44); enemy_move(); }
/* $492E: kind d0's maximum health (the table at $1BA6C, 3 bytes a kind) times 32; $4928 its own */
static void health_of(void)
{
    c->a[0] = 0x1BA6C;
    c->a[0] += sx(rd16(c->a[0]));
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[1] * 2);
    c->d[0] = set_w(c->d[0], c->d[0] + c->d[1]);
    c->d[0] = set_b(c->d[0], rd8(c->a[0] + sx(c->d[0])));
    c->d[0] = set_w(c->d[0], (c->d[0] & 0xFF) << 5);
    c->ccr = (uint16_t)(c->d[0] & 0xFFFF ? 0 : CC_Z);
}
static void max_health(void) { c->d[0] = rd8(e(-0x2BFD)); health_of(); }
/* $4948: when hurt by the player (its kind 0, $D503 bit 2), its health shown on the HUD */
static void damage_shown(void)
{
    F_B(rd8(e(-0x2BFE)));
    if (rd8(e(-0x2BFE))) return;
    f_bit(rd8(e(-0x2AFD)) >> 2 & 1);
    if (!(rd8(e(-0x2AFD)) & 4)) return;
    max_health();
    wr16(DAMAGE_SHOWN, (uint16_t)c->d[0]);
    put16(DAMAGE_SHOWN + 2, rd16(e(-0x2600)));
}
/* $472A: z when the player (a2) is on the side it faces */
static int player_ahead(void)
{
    c->d[0] = 0;
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(e(-0x3800)), rd16(o(-0x3800))));
    if (c->ccr & CC_C) c->d[0] = 8;
    c->d[1] = 8 & rd8(e(-0x3FFE));
    c->d[0] = set_b(c->d[0], c->d[0] ^ c->d[1]); F_B(c->d[0]);
    return !(c->d[0] & 0xFF);
}
/* $4742: with the shield up: the shield's touch ($D2C, $C36); a hit marks the shield's side in
 * $9F13 (by the angle, $1042) and itself as hit by it, no damage */
static void shield_hit(void)
{
    call(0xD2C);
    call(0xC36);
    F_B(rd8(e(-0x2AFD)));
    if (!rd8(e(-0x2AFD))) return;
    c->d[0] = set_w(c->d[0], rd16(e(-0x3800)) - rd16(o(-0x3800)));
    c->d[1] = set_w(c->d[1], rd16(e(-0x3700)) - rd16(o(-0x3700)));
    call(0x1042);
    uint16_t w = (uint16_t)c->d[3];
    c->d[3] = set_w(c->d[3], (uint16_t)(w << 3 | w >> 13));
    wr8(0xFFFF9F13u, (uint8_t)(rd8(0xFFFF9F13u) | 1 << (c->d[3] & 7)));
    or8(e(-0x2AFD), 0x80);
    put16(e(-0x23FE), 0);
}
static int shield_up(void) { c->a[2] = sx(rd16(PLAYER_ENT)); int on = rd8(FALL_FLAGS) >> 6 & 1; f_bit(on); return on; }
/* $46F4: touching the player, or the shield: the hits each way where each faces the other */
static void shield_contact(void)
{
    if (shield_up()) shield_hit();
    else {
        if (player_ahead()) { call(0xC36); if (!(c->ccr & CC_Z)) goto second; }
        call(0xE64);
        call(0xD6E);
    }
second:
    if (player_ahead()) { call(0xC3E); if (!(c->ccr & CC_Z)) return; }
    call(0xD76);
    damage_shown();
}
/* $4778: the player (or the shield) against it */
static void player_touch(void)
{
    if (shield_up()) shield_hit(); else call(0xE64);
    damage_shown();
}
/* $4792: touching, and the sword */
static void player_contact_all(void)
{
    if (shield_up()) shield_hit(); else call(0xE64);
    call(0xD76);
    damage_shown();
}
/* $47AE: the shield, or touching unless the sword's touch ($D2C) hit */
static void player_contact_shield(void)
{
    if (shield_up()) shield_hit();
    else { call(0xD2C); if (c->ccr & CC_Z) call(0xE64); }
    damage_shown();
}
/* $47D2: the enemies frozen ($A14A): kinds 0-2 in use: 2 and undrawn ones gone, drawn ones in
 * state 1 */
static void freeze_enemies(void)
{
    wr8(0xFFFFA14Au, 0xFF);
    c->a[2] = sx(rd16(0xFFFFA11Au));
    c->d[2] = 0xF;
    for (int k = 0; k < 16; k++) {
        if (rd8(o(-0x4000)) & 0x80) {
            uint8_t kind = rd8(o(-0x2BFE));
            if (kind <= 2) {
                if (kind == 2 || !(rd8(o(-0x4000)) & 0x40)) wr16(o(-0x4000), 0);
                else {
                    c->d[0] = 0x7F & rd8(o(-0x2BFF));
                    if ((c->d[0] & 0xFF) != 1) wr8(o(-0x2BFF), 1);
                }
            }
        }
        c->a[2] += 4;
    }
    c->d[2] = 0xFFFF;
}
/* $4814: gone; its spawn point (-$2B00) can make another (its count, unless done) */
static void despawn(void)
{
    put16(e(-0x4000), 0);
    c->d[0] = set_w(c->d[0], rd16(e(-0x2B00))); F_W(c->d[0]);
    if (c->d[0] & 0x8000) return;
    c->a[0] = sx(c->d[0]);
    F_B(rd8(c->a[0] - 0x5DB4));
    if (rd8(c->a[0] - 0x5DB4) & 0x80) return;
    wr8(c->a[0] - 0x5DB1, (uint8_t)(rd8(c->a[0] - 0x5DB1) + 1));
}
/* $482C: defeated: counted ($A148); its spawn point's count, and when all are beaten its flag
 * (room or game, $A33E) and its prize ($A33C: $FE none); else, unless frozen, a random drop from
 * its kind's three ($1C170: $FF none); a drop of $80 up is a big item ($F80 more) */
static void enemy_defeated(void)
{
    wr16(0xFFFFA148u, (uint16_t)(rd16(0xFFFFA148u) + 1));
    c->d[0] = set_w(c->d[0], rd16(e(-0x2B00)));
    if (c->d[0] & 0x8000) goto gone;
    c->a[3] = sx(c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[3] - 0x5DB2) + 1);
    wr8(c->a[3] - 0x5DB2, (uint8_t)c->d[0]);
    if ((c->d[0] & 0xFF) == rd8(c->a[3] - 0x5DB4)) {
        c->d[0] = set_b(c->d[0], rd8(c->a[3] - 0x5CC1));
        uint8_t how = rd8(c->a[3] - 0x5CC2);
        if (!(how & 0x80)) {
            if (how) call(0x2752);
            else {
                call(0x2758);
                uint32_t a = c->a[0] + sx(c->d[1]);
                wr8(a, (uint8_t)(rd8(a) | 1 << (c->d[0] & 7)));
            }
        }
        c->d[0] = rd16(c->a[3] - 0x5CC4);
        if (!(c->d[0] & 0x8000)) {
            if ((c->d[0] & 0xFFFF) == 0xFE) goto gone;
            goto drop;
        }
    }
    if (rd8(0xFFFFA14Au) & 0x80) goto gone;
    c->a[0] = 0x1C170 + 2 * rd8(e(-0x2BFD));
    call(RANDOM);
    for (c->d[1] = 2;; ) {
        int out = c->d[0] & 1; c->d[0] = set_w(c->d[0], (c->d[0] & 0xFFFF) >> 1);
        if (out) break;
        out = c->d[0] & 1; c->d[0] = set_w(c->d[0], (c->d[0] & 0xFFFF) >> 1);
        if (out) break;
        c->a[0] += 1;
        c->d[1] = set_w(c->d[1], c->d[1] - 1);
        if ((c->d[1] & 0xFFFF) == 0xFFFF) break;
    }
    c->d[0] = rd8(c->a[0]);
    if (c->d[0] == 0xFF) goto gone;
drop:
    if (c->d[0] & 0x80) c->d[0] = set_w(c->d[0], c->d[0] + 0xF80);
    put8(e(-0x27FF), 0);
    c->d[1] = set_w(c->d[1], rd16(e(-0x3800)));
    c->d[2] = set_w(c->d[2], rd16(e(-0x3700)));
    call(0x10910);
    return;
gone:
    put16(e(-0x4000), 0);
}
/* $48C4: off the screen and not held there, out of the water's reach: despawned, and the caller
 * returns (1). Floating or swimming ones need the screen's margin ($40 across, $30 down) */
static int offscreen_despawn(void)
{
    if (rd8(e(-0x4000)) & 1) return 0;
    if (rd8(e(-0x3200)) & 1) return 0;
    if (rd8(0xFFFF966Bu) & 0x20) return 0;
    c->d[0] = 6 & rd8(e(-0x3200));
    if ((c->d[0] & 0xFF) && (c->d[0] & 0xFF) != 6) {
        call(0xBEE);
        if (c->ccr & CC_Z) return 0;
    } else {
        c->d[2] = 0x40;
        c->d[0] = set_w(c->d[0], rd16(e(-0x3800)));
        call(0xBD0);
        if (!(c->ccr & CC_N)) {
            c->d[2] = 0x30;
            c->d[0] = set_w(c->d[0], rd16(e(-0x3700)));
            call(0xBFE);
            if (!(c->ccr & CC_N)) return 0;
        }
    }
    despawn();
    return 1;
}
/* $4910: a0 the entry after kind d1 in the room's list of kinds ($A122, $A142 of them); a kind
 * not in it stops the game (the original loops for ever: left to it) */
static int room_kind(void)
{
    c->a[0] = 0xFFFFA122u;
    c->d[0] = set_w(c->d[0], rd16(0xFFFFA142u));
    for (;;) {
        uint16_t k = rd16(c->a[0]); c->a[0] += 2;
        cmp_w(&c->ccr, k, c->d[1]);
        if (k == (uint16_t)c->d[1]) return 0;
        c->a[0] += 2;
        c->d[0] = set_b(c->d[0], c->d[0] - 1);
        if (!(c->d[0] & 0xFF)) { port_continue_at(c, 0x4922, 0); return 1; }
    }
}
/* $4964: d0 scaled by its level's divisor (-$26FE into $4972) over 64 */
static void scale_damage(void)
{
    c->d[1] = set_w(c->d[1], rd16(e(-0x26FE)));
    c->d[1] = set_b(c->d[1], rd8(0x4972 + sx(c->d[1])));
    c->d[0] = (uint32_t)(uint16_t)c->d[0] * (uint16_t)c->d[1];
    c->d[0] >>= 6;
}
/* $4986: hurt by the player: sound $1F, its maximum for the HUD; the sword's damage (scaled), on
 * a doubled hit less the double's time ($9F16); any magic's damage; its health less that, at 0
 * beaten (z) */
static void damage_enemy(void)
{
    c->d[0] = 0x1F; call(0x366);
    max_health();
    wr16(DAMAGE_SHOWN, (uint16_t)c->d[0]);
    c->d[0] = 0;
    c->d[2] = set_w(c->d[2], rd16(e(-0x2600)));
    if ((rd8(e(-0x2AFD)) & 0x80) && rd16(e(-0x23FE))) {
        c->d[0] = set_w(c->d[0], rd16(e(-0x23FE)));
        scale_damage();
        if ((rd8(FALL_FLAGS) & 0x20) && (rd8(e(-0x2AFD)) & 8) && rd16(0xFFFF9F16u)) {
            c->d[1] = set_w(c->d[1], rd16(0xFFFF9F16u));
            c->d[0] = set_w(c->d[0], c->d[0] * 2);
            c->d[3] = set_w(c->d[3], (rd16(e(-0x2300)) & 0xC0) >> 5);
            int16_t t = (int16_t)((uint16_t)c->d[1] - rd16(0x49F0 + (c->d[3] & 0xFFFF)));
            c->d[1] = t < 0 ? 0 : set_w(c->d[1], (uint16_t)t);
            wr16(0xFFFF9F16u, (uint16_t)c->d[1]);
        }
    }
    if (rd8(e(-0x21FF)) & 0x80) c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x21FE)));
    if ((uint16_t)c->d[2] > (uint16_t)c->d[0]) c->d[2] = set_w(c->d[2], c->d[2] - c->d[0]);
    else c->d[2] = 0;
    wr16(e(-0x2600), (uint16_t)c->d[2]);
    put16(DAMAGE_SHOWN + 2, c->d[2]);
}

ENTRY(TargetFromHome, target_from_home)    /* $4616 */
ENTRY_POP(EdgeTurnReturn, edge_turn_return)   /* $462A */
ENTRY(EdgeTurn, edge_turn)                 /* $463A */
ENTRY(EdgeAhead, edge_ahead)               /* $4646 */
ENTRY(GroundAhead, ground_ahead)           /* $4686 */
ENTRY(FlyFacing, fly_facing)               /* $46BE */
ENTRY(AccelFacing, accel_facing)           /* $46C6 */
ENTRY(FallAndMove, fall_and_move)          /* $46D6 */
ENTRY(EnemyMove, enemy_move)               /* $46DA */
ENTRY(ShieldContact, shield_contact)       /* $46F4 */
static void player_ahead_entry(void) { player_ahead(); }
ENTRY(PlayerAhead, player_ahead_entry)     /* $472A */
ENTRY(ShieldHit, shield_hit)               /* $4742 */
ENTRY(PlayerTouch, player_touch)           /* $4778 */
ENTRY(PlayerContactAll, player_contact_all)   /* $4792 */
ENTRY(PlayerContactShield, player_contact_shield)   /* $47AE */
ENTRY(FreezeEnemies, freeze_enemies)       /* $47D2 */
ENTRY(Despawn, despawn)                    /* $4814 */
ENTRY(EnemyDefeated, enemy_defeated)       /* $482C */
ENTRY_POP(OffscreenDespawn, offscreen_despawn)   /* $48C4 */
void RoomKind(Cpu *cpu) { c = cpu; if (!room_kind()) c->ret = 0; }   /* $4910 */
ENTRY(MaxHealth, max_health)               /* $4928 */
ENTRY(HealthOf, health_of)                 /* $492E */
ENTRY(DamageShown, damage_shown)           /* $4948 */
ENTRY(ScaleDamage, scale_damage)           /* $4964 */
ENTRY(DamageEnemy, damage_enemy)           /* $4986 */

/* ---- batch T47: the plane A prefixes, a direction's offset, a slope at the feet ---- */

/* $30CE: $30D2 (the cell's attributes at pixel d6, d7) on plane A */
static void tile_at_a(void) { c->a[3] = 0xFFFF9754u; call(0x30D2); }
/* $3F54: $3F58 (the tile below) on plane A */
static void probe_below_a(void) { c->a[3] = 0xFFFF9754u; call(0x3F58); }
/* $3E20: the signed offset (the table at $3E32) for d1's direction (bits 4-7) */
static void dir_offset(void)
{
    c->d[1] = set_w(c->d[1], (c->d[1] & 0xF0) >> 4);
    c->a[0] = 0x3E32;
    c->d[1] = set_b(c->d[1], rd8(c->a[0] + sx(c->d[1])));
    c->d[1] = ext_w(c->d[1]); F_W(c->d[1]);
}
/* $33B8: moving, a slope at its feet the way it moves ($30D2's d2 bit 0 right, 1 left): pushed
 * up the slope's height (-$3000) and marked on it ($CF00 bits 0, 2) */
static void slope_at_feet(void)
{
    F_W(rd16(e(-0x3600)));
    if (!rd16(e(-0x3600))) return;
    c->a[7] -= 4; wr32(c->a[7], c->a[6]); c->a[6] = c->a[7]; c->a[7] -= 2;   /* link a6,#-2 */
    uint8_t left = (rd16(e(-0x3600)) & 0x8000) ? 1 : 0;
    wr8(c->a[6] - 2, left);
    c->d[6] = set_w(c->d[6], rd16(e(-0x3800)));
    c->d[7] = (uint8_t)(rd8(e(-0x34FD)) - 1);
    c->d[7] = set_w(c->d[7], c->d[7] + rd16(e(-0x3700)));
    call(0x30D2);
    c->d[1] = set_b(c->d[1], rd8(c->a[6] - 2));
    if (c->d[2] >> (c->d[1] & 31) & 1) {
        c->d[0] = set_w(c->d[0], rd16(e(-0x3800)));
        if (!(c->d[1] & 0xFF)) {
            c->d[0] = set_w(c->d[0], (c->d[0] & 0xF) + 1);
            neg_d(0);
            or8(e(-0x3100), 1);
        } else {
            neg_d(0);
            c->d[0] = set_w(c->d[0], c->d[0] & 0xF);
            or8(e(-0x3100), 5);
        }
        put16(e(-0x3000), c->d[0]);
    }
    c->a[7] = c->a[6]; c->a[6] = rd32(c->a[7]); c->a[7] += 4;                    /* unlk a6 */
}

ENTRY(TileAtA, tile_at_a)                  /* $30CE */
ENTRY(SlopeAtFeet, slope_at_feet)          /* $33B8 */
ENTRY(DirOffset, dir_offset)               /* $3E20 */
ENTRY(ProbeBelowA, probe_below_a)          /* $3F54 */
