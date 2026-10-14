/* The enemy library ($3000-$5000): the small routines the enemies' actions share. Batch T45 on.
 *
 * Plain C, as from batch T42: each promises the stack pointer and what it writes, and the
 * registers and flags its callers use (the C actions' notes name them: z, mi, cs, a register).
 * Several return from their caller when they act (addq #4,a7); their entries pop that frame too.
 * a4 is the entity throughout (its fields at -$xxxx(a4)); a2 another (the target at $A11C). */
#include "world.h"
#include "task.h"

static Cpu *c;
static void call(uint32_t addr) { call_68k(c, addr); }

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
