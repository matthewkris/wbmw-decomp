/* The enemies' helpers in the object region ($17000-$1B000): small routines the actions call
 * (actions.c names them in its defines), the room scripts and objects that live here, and the
 * ending's frames. Batch T48 on.
 *
 * Plain C (as from batch T42): each promises the stack pointer and what it writes, plus the
 * registers and flags its callers use. Some return from their caller when they act (addq #4,a7)
 * and go on into the enemy's next state ($1357E); their entries pop that frame too. actions.c
 * has its own inline copies of a few ($17132, $17418, $17704, $179CC) inside its actions; these
 * are the entries the original's callers reach. a4 is the entity; a2 another. */
#include "world.h"
#include "task.h"

static Cpu *c;
static void call(uint32_t addr) { call_68k(c, addr); }

static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }
static uint32_t e(int32_t off) { return c->a[4] + (uint32_t)off; }     /* -$xxxx(a4) */
static uint32_t o(int32_t off) { return c->a[2] + (uint32_t)off; }     /* -$xxxx(a2) */
static void put8(uint32_t a, uint32_t v) { wr8(a, (uint8_t)v); F_B(v); }
static void put16(uint32_t a, uint32_t v) { wr16(a, (uint16_t)v); F_W(v); }
static void neg16(uint32_t a) { wr16(a, (uint16_t)neg_w(&c->ccr, rd16(a))); }
static void partner(void) { c->a[4] = sx(rd16(e(-0x2CFE))); }          /* movea.w -$2CFE(a4),a4 */

#define SET_ANIM      0x07E8u
#define NEXT_STATE    0x1357Eu      /* d0: the state; the enemy's action runs */
/* the caller's frame dropped (addq #4,a7) and on into state d0 */
static void leave_to_state(uint32_t state) { c->a[7] += 4; c->d[0] = state; F_L(state); port_continue_at(c, NEXT_STATE, 0); }

/* $170F4 (across) and $170EE (down, a0 two bytes on): the speed swings by the acceleration; at
 * the limit (-$3300) the acceleration turns */
static void swing_at(uint32_t base)
{
    c->a[0] = base;
    c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x3600));
    c->d[1] = set_w(c->d[1], rd16(c->a[0] - 0x3400));
    c->d[2] = set_w(c->d[2], rd16(c->a[0] - 0x3300));
    c->d[0] = set_w(c->d[0], c->d[0] + c->d[1]);
    wr16(c->a[0] - 0x3600, (uint16_t)c->d[0]);
    if (c->d[1] & 0x8000) c->d[0] = set_w(c->d[0], (uint16_t)-c->d[0]);
    if ((int16_t)c->d[2] > (int16_t)c->d[0]) return;
    neg16(c->a[0] - 0x3400);
}
static void swing_x(void) { swing_at(sx(c->a[4])); }
static void swing_y(void) { swing_at(c->a[4] + 2); }
/* $17118: the hopper's start: speeds $100, phase 0, animation 6, a wait ($43DE) */
static void hopper_start(void)
{
    c->d[0] = set_w(c->d[0], 0x100);
    put16(e(-0x3300), 0x100);
    put16(e(-0x32FE), 0x100);
    put8(e(-0x31FF), 0);
    c->d[0] = 6; call(SET_ANIM);
    call(0x43DE);
}
/* $17132: a link of the chain. A follower (-$2D00: the link ahead) moves as that link moved two
 * frames ago (-$2A00, -$2900); the head wanders about its home by its offsets (-$27FE, -$27FD),
 * every 16 frames toward the target, and grows the chain (kind $2E, up to 5 links, -$25FE frames
 * apart). With no link on the screen (looked at every 16 frames) the head removes the chain and
 * its caller returns (1). */
static int chain_link(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x2D00)));
    if (!(c->d[0] & 0x8000)) {
        c->a[2] = sx(c->d[0]);
        c->d[1] = rd32(e(-0x2A00));
        wr32(e(-0x2A00), rd32(e(-0x2900)));
        uint16_t dx = (uint16_t)(rd16(o(-0x3800)) - rd16(e(-0x3800)));
        uint16_t dy = (uint16_t)(rd16(o(-0x3700)) - rd16(e(-0x3700)));
        c->d[0] = (uint32_t)dx << 16 | dy;
        wr32(e(-0x2900), c->d[0]);
        c->d[0] = 4;
        c->d[1] = set_w(c->d[1], (uint16_t)(c->d[1] << 4));
        wr16(e(-0x35FE), (uint16_t)c->d[1]);
        c->d[1] = c->d[1] << 16 | c->d[1] >> 16;
        c->d[1] = set_w(c->d[1], (uint16_t)(c->d[1] << 4));
        wr16(e(-0x3600), (uint16_t)c->d[1]);
    } else {
        c->d[0] = 0xF & rd16(0xFFFF965Au);
        int wander = 1;
        if (!c->d[0]) {
            c->d[1] = set_w(c->d[1], c->a[4]);
            do {
                c->a[0] = sx(c->d[1]);
                c->d[0] = set_b(c->d[0], c->d[0] | rd8(c->a[0] - 0x4000));
                c->d[1] = set_w(c->d[1], rd16(c->a[0] - 0x2CFE));
            } while (!(c->d[1] & 0x8000));
            int seen = c->d[0] & 1;
            c->d[0] = set_b(c->d[0], (c->d[0] & 0xFF) >> 1);
            if (!seen) {
                c->a[0] = sx(c->a[4]);
                for (;;) {
                    c->d[1] = set_w(c->d[1], rd16(c->a[0] - 0x2CFE));
                    if (c->d[1] & 0x8000) break;
                    c->a[0] = sx(c->d[1]);
                    wr8(c->a[0] - 0x4000, 0);
                }
                call(0x4814);
                return 1;
            }
        }
        if (wander) {
            c->d[0] = set_b(c->d[0], rd8(e(-0x27FE))); c->d[0] = ext_w(c->d[0]);
            c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x2100)));
            call(0x16E56);
            c->d[0] = set_b(c->d[0], rd8(e(-0x27FD))); c->d[0] = ext_w(c->d[0]);
            c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x20FE)));
            call(0x16E72);
        }
    }
    call(0xAB4);
    call(0x44A6);
    c->d[0] = (((8 & rd8(e(-0x3FFE))) >> 2) + 0xC) & 0xFF;
    put8(e(-0x3CFD), c->d[0]);
    if ((rd16(e(-0x2CFE)) & 0x8000) && rd8(e(-0x25FE))) {
        uint8_t n = (uint8_t)(rd8(e(-0x25FE)) - 1);
        wr8(e(-0x25FE), n);
        if (!n) {
            if (rd8(e(-0x31FF)) >= 5) return 0;
            c->a[0] = 0xFFFFA394u;
            wr16(c->a[0], 0x2E); c->a[0] += 2;
            wr32(c->a[0], rd32(e(-0x2100))); c->a[0] += 4;
            wr16(c->a[0], rd16(e(-0x3DFE)));
            call(0x1A438);
            if (c->ccr & CC_N) { wr8(e(-0x25FE), (uint8_t)(rd8(e(-0x25FE)) + 1)); return 0; }
            c->d[1] = set_w(c->d[1], rd16(o(-0x2BFE)));
            call(0x4910);
            wr16(o(-0x3DFE), rd16(c->a[0]));
            wr32(o(-0x2100), rd32(e(-0x2100)));
            wr16(e(-0x2CFE), (uint16_t)c->a[2]);
            wr16(o(-0x2D00), (uint16_t)c->a[4]);
            wr8(o(-0x2CFE), 0xFF);
            wr8(o(-0x2B00), 0xFF);
            wr8(o(-0x30FE), 0xFF);
            put8(o(-0x2F00), 3);
            wr32(e(-0x2A00), 0);
            wr32(e(-0x2900), 0);
            c->d[0] = set_b(c->d[0], rd8(e(-0x31FF)) + 1);
            put8(o(-0x31FF), c->d[0]);
        }
    }
    if (!(rd16(e(-0x2D00)) & 0x8000)) return 0;
    uint8_t t = (uint8_t)(rd8(e(-0x2800)) - 1);
    wr8(e(-0x2800), t);
    if (t) return 0;
    put8(e(-0x2800), 0x10);
    c->a[2] = sx(rd16(0xFFFFA11Cu));
    c->d[1] = 0x10;
    c->d[0] = set_w(c->d[0], rd16(o(-0x3800)));
    if ((uint16_t)c->d[0] < rd16(e(-0x3800))) c->d[1] = set_b(c->d[1], (uint8_t)-0x10);
    {
        uint8_t v = rd8(e(-0x27FE)), d = (uint8_t)c->d[1];
        uint8_t r = (uint8_t)(v + d);
        wr8(e(-0x27FE), r);
        if ((int8_t)(v ^ r) < 0 && (int8_t)(d ^ r) < 0) {      /* overflow: back by twice */
            c->d[1] = set_b(c->d[1], (uint8_t)(d * 2));
            wr8(e(-0x27FE), (uint8_t)(r - (uint8_t)c->d[1]));
        }
    }
    c->d[1] = 0x10;
    c->d[0] = set_w(c->d[0], rd16(o(-0x3700)));
    if ((uint16_t)c->d[0] < rd16(e(-0x3700))) c->d[1] = set_b(c->d[1], (uint8_t)-0x10);
    {
        uint8_t r = (uint8_t)(rd8(e(-0x27FD)) + (uint8_t)c->d[1]);
        wr8(e(-0x27FD), r);
        if ((int8_t)r > (int8_t)0xE0) {
            c->d[1] = set_b(c->d[1], (uint8_t)(c->d[1] * 2));
            wr8(e(-0x27FD), (uint8_t)(r - (uint8_t)c->d[1]));
        }
    }
    return 0;
}
/* $1730A: words from a4 to a2 at the offsets listed at a0 (to a 0) */
static void copy_fields(void)
{
    for (;;) {
        c->d[0] = set_w(c->d[0], rd16(c->a[0])); c->a[0] += 2;
        if (!(c->d[0] & 0xFFFF)) { F_W(0); return; }
        c->d[0] = sx(c->d[0]);
        wr16(c->a[2] + c->d[0], rd16(c->a[4] + c->d[0]));
    }
}
/* $17418: |d0| (negative by the caller's flags) over 64 */
static void slow_of(void)
{
    if (c->ccr & CC_N) c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 6, 16));
}
/* $17420: off a wall (CF00 bit 0) VEL_X reversed and turned; off a floor or ceiling (bit 1)
 * VEL_Y reversed */
static void bounce(void)
{
    c->d[0] = set_b(c->d[0], rd8(e(-0x3100)));
    if (c->d[0] & 1) {
        wr8(e(-0x3FFE), rd8(e(-0x3FFE)) ^ 8);
        neg16(e(-0x3600));
    }
    if (c->d[0] & 2) neg16(e(-0x35FE));
    c->d[0] = set_b(c->d[0], (c->d[0] & 0xFF) >> 2);
}
/* $174B0: d3 the angle to a2 ($108C), kept within an eighth of level the way it faces */
static void aim_level(void)
{
    call(0x108C);
    uint16_t a = (uint16_t)c->d[3], lim;
    int keep_if_above;
    if (!(rd8(e(-0x3FFE)) & 8)) { if (a & 0x8000) { lim = 0xE000; keep_if_above = 1; } else { lim = 0x2000; keep_if_above = 0; } }
    else { if (a & 0x8000) { lim = 0xA000; keep_if_above = 0; } else { lim = 0x6000; keep_if_above = 1; } }
    c->d[0] = set_w(c->d[0], lim);
    cmp_w(&c->ccr, lim, a);
    if (keep_if_above ? a >= lim : a <= lim) return;
    c->d[3] = set_w(c->d[3], lim);
}
/* $176E6: the sound stopped ($364), but not in area $12; $176F4: sound $13 there */
static void sound_stop_unless_12(void) { if (rd8(0xFFFF9668u) != 0x12) call(0x364); }
static void sound_13_unless_12(void) { if (rd8(0xFFFF9668u) != 0x12) { c->d[0] = 0x13; call(0x366); } }
/* $179AA: the pair's animations from $179C2 + d0: the partner's, then its own */
static void pair_anim(void)
{
    uint32_t a = 0x179C2 + sx(c->d[0]);
    partner();
    c->d[0] = set_b(c->d[0], rd8(a)); a++;
    c->a[0] = a;
    call(SET_ANIM);
    partner();
    c->d[0] = set_b(c->d[0], rd8(a));
    c->a[0] = a;
    call(SET_ANIM);
}
/* $179DE: the partner takes its health; the player against the partner ($E64) */
static void pair_touch(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x2600)));
    partner();
    put16(e(-0x2600), c->d[0]);
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    call(0xE64);
    partner();
}
/* $17A26: on the partner's body: $2C above it */
static void pair_on_body(void)
{
    c->a[0] = sx(rd16(e(-0x2CFE)));
    c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x3700) - 0x2C);
    put16(e(-0x3700), c->d[0]);
}
/* $179F8: the body (the partner) bobs toward its height ($2E above home, 4 more rising), moves;
 * this one on it */
static void pair_hover(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x20FE)) - 0x2E);
    partner();
    if (rd16(e(-0x35FE)) & 0x8000) c->d[0] = set_w(c->d[0], c->d[0] - 4);
    c->d[1] = 0x10;
    if ((uint16_t)c->d[0] < rd16(e(-0x3700))) c->d[1] = set_w(c->d[1], (uint16_t)-0x10);
    put16(e(-0x33FE), c->d[1]);
    call(0xB44);
    call(0xAD6);
    partner();
    pair_on_body();
}
/* $17A38: the partner's x its own */
static void pair_align_x(void)
{
    c->a[0] = sx(rd16(e(-0x2CFE)));
    put16(c->a[0] - 0x3800, rd16(e(-0x3800)));
}

#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(SwingY, swing_y)                     /* $170EE */
ENTRY(SwingX, swing_x)                     /* $170F4 */
ENTRY(HopperStart, hopper_start)           /* $17118 */
void ChainLink(Cpu *cpu) { c = cpu; int out = chain_link(); if (out) c->a[7] += 4; c->ret = 0; }   /* $17132 */
ENTRY(CopyFields, copy_fields)             /* $1730A */
ENTRY(SlowOf, slow_of)                     /* $17418 */
ENTRY(Bounce, bounce)                      /* $17420 */
ENTRY(AimLevel, aim_level)                 /* $174B0 */
ENTRY(SoundStopUnless12, sound_stop_unless_12)   /* $176E6 */
ENTRY(Sound13Unless12, sound_13_unless_12) /* $176F4 */
/* $17704: touched from behind ($D503 bit 2): TIMER 1, and the caller's action goes to state 5 */
void TouchedBack(Cpu *cpu)
{
    c = cpu; c->ret = 0;
    if (!(rd8(e(-0x2AFD)) & 4)) return;
    put8(e(-0x2800), 1);
    leave_to_state(5);
}
ENTRY(PairAnim, pair_anim)                 /* $179AA */
/* $179CC: touching the player and the sword; hit: state 2 (the caller's frame dropped); else as
 * $179DE */
void PairHit(Cpu *cpu)
{
    c = cpu;
    call(0x4792);
    call(0x439E);
    if (c->ccr & CC_N) { leave_to_state(2); return; }
    pair_touch();
    c->ret = 0;
}
ENTRY(PairTouch, pair_touch)               /* $179DE */
ENTRY(PairHover, pair_hover)               /* $179F8 */
ENTRY(PairOnBody, pair_on_body)            /* $17A26 */
ENTRY(PairAlignX, pair_align_x)            /* $17A38 */

/* ---- batch T49: room script 16 (a scrolling floor), spawning at an offset, the partner's place ----
 * Room script 16 ($C784/$C838's entry 16) moves the room's floor: $99E6 its position (16.16),
 * $99EA its speed, $99E4 this frame's step across. Its two platforms ($99EE, $99F0, made by
 * $11CE8) and the enemies ride along; when the floor runs past the screen's right edge
 * ($9778) the whole room shifts back by a block's worth. $99DE: bit 0 moving, bit 1 slowing,
 * bit 2 the exit drawn; once the player stands in the exit ($99F2, $99F4) its trigger ($1822A)
 * changes the room. */

#define FLOOR_POS     0xFFFF99E6u   /* long */
#define FLOOR_SPEED   0xFFFF99EAu   /* long */
#define FLOOR_STEP    0xFFFF99E4u   /* word */
#define FLOOR_FLAGS   0xFFFF99DEu

/* $18044: a platform (a1) riding the floor */
static void floor_platform(void)
{
    put8(c->a[1] - 0x6072, 0x80);
    put8(c->a[1] - 0x6071, 1);
    put16(c->a[1] - 0x601C, 0x1048);
    wr8(c->a[1] - 0x6070, 0xFF);
    wr32(c->a[1] - 0x5FCA, 0);
    put16(c->a[1] - 0x5F76, 8);
    put16(c->a[1] - 0x5F74, 0x48);
}
/* $1806C: the floor's position for the layer ($985A) */
static void floor_to_layer(void) { wr32(0xFFFF985Au, rd32(FLOOR_POS)); F_L(rd32(FLOOR_POS)); }
/* $18024: the layer (a3) one column on */
static void layer_column(void)
{
    c->d[0] = rd8(c->a[3] + 0x38);
    wr8(c->a[3] + 0x38, (uint8_t)(rd8(c->a[3] + 0x38) + 1));
    c->a[0] = sx(rd16(c->a[3] + 0x22));
    wr8(c->a[0] + sx(c->d[0]), rd8(c->a[0]));
    wr8(c->a[3] + 0x44, (uint8_t)(rd8(c->a[3] + 0x44) + 0x10));
    wr16(c->a[3] + 0x24, (uint16_t)(rd16(c->a[3] + 0x24) + 0x100));
}
/* $17FEC: room script 16's setup: the floor at $1000, the layer a column on, two platforms */
static void room16_setup(void)
{
    wr8(0xFFFF9858u, rd8(0xFFFF9858u) | 1);
    wr32(FLOOR_POS, 0x10000000);
    floor_to_layer();
    c->a[3] = 0xFFFF9754u;
    layer_column();
    call(0x11CE8);
    wr16(0xFFFF99EEu, (uint16_t)c->a[1]);
    wr16(c->a[1] - 0x601E, 0xFF8);
    floor_platform();
    call(0x11CE8);
    wr16(0xFFFF99F0u, (uint16_t)c->a[1]);
    wr16(c->a[1] - 0x601E, 0x1108);
    floor_platform();
}
/* the riders of the floor (in use, not held by -$3E00): x, -$2E00 and home moved by d */
static void ride(uint32_t slot, int16_t d)
{
    if (!(rd8(slot - 0x4000) & 0x80) || rd16(slot - 0x3E00)) return;
    static const int16_t f[] = { -0x3800, -0x2E00, -0x2100 };
    for (int k = 0; k < 3; k++) wr16(slot + (uint32_t)(int32_t)f[k], (uint16_t)(rd16(slot + (uint32_t)(int32_t)f[k]) + d));
}
/* $18074: the floor moved by its speed; past the screen's edge the room shifts back ($975C,
 * $975E, every entity and platform); the platforms and enemies ride the step */
static void floor_scroll(void)
{
    if (rd8(0xFFFF9659u) & 0x20) return;
    c->d[1] = rd32(FLOOR_POS);
    c->d[0] = c->d[1];
    c->d[1] += rd32(FLOOR_SPEED);
    c->d[7] = c->d[1];
    c->d[0] = c->d[0] << 16 | c->d[0] >> 16;
    c->d[1] = c->d[1] << 16 | c->d[1] >> 16;
    c->d[1] = set_w(c->d[1], c->d[1] - c->d[0]);
    wr16(FLOOR_STEP, (uint16_t)c->d[1]);
    c->d[1] = (uint32_t)rd16(0xFFFF9778u) << 16;
    if (c->d[1] < c->d[7]) {
        c->d[1] -= 0x10000000;
        c->d[7] -= c->d[1];
        c->d[1] = c->d[1] << 16 | c->d[1] >> 16;
        c->d[0] = set_w(c->d[0], (uint16_t)c->d[1] >> 4);
        wr16(0xFFFF975Eu, (uint16_t)(rd16(0xFFFF975Eu) - 0x10));
        c->d[0] = set_w(c->d[0], c->d[0] - rd16(0xFFFF975Eu));
        wr16(0xFFFF975Cu, (uint16_t)(rd16(0xFFFF975Cu) - (uint16_t)c->d[0]));
        for (uint32_t s = 0; s < 0x100; s += 4) ride(s, (int16_t)-(int16_t)c->d[1]);
        c->a[4] = 0x100; c->d[0] = 0xFFFF;
        for (uint32_t s = 0; s < 24; s += 4)
            if (rd8(s - 0x6072) & 0x80) wr16(s - 0x601E, (uint16_t)(rd16(s - 0x601E) - (uint16_t)c->d[1]));
        c->a[0] = 24;
    }
    wr32(FLOOR_POS, c->d[7]);
    c->a[0] = sx(rd16(0xFFFF99EEu));
    c->a[1] = sx(rd16(0xFFFF99F0u));
    c->d[7] = set_w(c->d[7], rd16(FLOOR_STEP));
    wr16(c->a[0] - 0x5FCA, (uint16_t)c->d[7]);
    wr16(c->a[1] - 0x5FCA, (uint16_t)c->d[7]);
    if (!(c->d[7] & 0xFFFF)) return;
    wr16(c->a[0] - 0x601E, (uint16_t)(rd16(c->a[0] - 0x601E) + (uint16_t)c->d[7]));
    wr16(c->a[1] - 0x601E, (uint16_t)(rd16(c->a[1] - 0x601E) + (uint16_t)c->d[7]));
    uint32_t base = sx(rd16(0xFFFFA11Au));
    for (int k = 0; k < 16; k++) ride(base + 4 * k, (int16_t)c->d[7]);
    c->a[4] = base + 64; c->d[0] = 0xFFFF;
}
/* $1813E: room script 16's frame. Moving: speeding up by $800 to $18000, or (slowing) down to 0;
 * the floor moved; its speed for the layer ($980E; in area $F mirrored, $980F). Stopped and
 * slowing: the exit drawn once ($2856: $1824C, or $18242 in area $F with game flag $44), then
 * the player standing in it arms the trigger $1822A */
static void room16_frame(void)
{
    uint8_t f = rd8(FLOOR_FLAGS);
    if (f & 1) {
        if (f & 2) {
            uint32_t s = rd32(FLOOR_SPEED) - 0x800;
            wr32(FLOOR_SPEED, s);
            if (!s || (int32_t)s < 0) {
                if ((int32_t)s < 0) wr32(FLOOR_SPEED, 0);
                wr8(FLOOR_FLAGS, rd8(FLOOR_FLAGS) & 0xFE);
            }
        } else if (rd32(FLOOR_SPEED) < 0x18000) wr32(FLOOR_SPEED, rd32(FLOOR_SPEED) + 0x800);
        floor_scroll();
        floor_to_layer();
        c->d[0] = rd32(FLOOR_SPEED) << 6;
        c->d[0] = c->d[0] << 16 | c->d[0] >> 16;
        wr8(0xFFFF980Eu, (uint8_t)c->d[0]);
        if (rd8(0xFFFF9668u) == 0xF) {
            c->d[0] = set_b(c->d[0], (uint8_t)-c->d[0]);
            wr8(0xFFFF980Fu, (uint8_t)c->d[0]);
        }
        return;
    }
    if (!(f & 2)) return;
    if (rd8(0xFFFF9668u) == 0xF && !(rd16(0xFFFF9994u) & 0x8000)) return;
    wr8(FLOOR_FLAGS, rd8(FLOOR_FLAGS) | 4);
    if (!(f & 4)) {
        c->d[6] = set_w(0x70, 0x70 + rd16(0xFFFF9758u));
        c->d[6] = set_w(c->d[6], c->d[6] & 0xFFF0);
        wr16(0xFFFF99F2u, (uint16_t)c->d[6]);
        wr16(0xFFFF99F4u, 0x1090);
        c->d[6] = set_w(c->d[6], ((uint16_t)c->d[6] >> 4) - 1);
        c->d[7] = set_w(c->d[7], 0x41C0);
        c->a[0] = 0x1824C;
        if (rd8(0xFFFF9668u) == 0xF) {
            c->d[0] = 0x44; call(0x2752);
            c->a[0] = 0x18242;
        }
        call(0x2856);
        return;
    }
    c->a[0] = sx(rd16(0xFFFF9EEEu));
    int16_t dx = (int16_t)(rd16(0xFFFF99F2u) - rd16(c->a[0] - 0x3800));
    if (dx < 0) dx = (int16_t)-dx;
    c->d[0] = set_w(c->d[0], (uint16_t)dx);
    if ((uint16_t)dx >= 4) return;
    c->d[0] = (uint32_t)rd8(c->a[0] - 0x34FD) + rd16(c->a[0] - 0x3700);
    c->d[0] &= 0xFFFF;
    if ((uint16_t)c->d[0] != rd16(0xFFFF99F4u)) return;
    wr8(0xFFFF9966u, rd8(0xFFFF9966u) | 1);
    wr8(0xFFFF9980u, 0xFF);
    wr32(0xFFFF998Au, 0x1822A);
}
/* $1822A (room script 16's exit, when up is pressed there): to start point $41 (area $F) or $74 */
static void room16_exit(void)
{
    c->d[0] = rd8(0xFFFF9668u) == 0xF ? 0x41 : 0x74;
    wr16(0xFFFF9676u, (uint16_t)c->d[0]);
    put16(0xFFFF9962u, 0xD);
}

/* $18256: an enemy of kind d0 at the offset (a0's two bytes, mirrored by its facing and flip)
 * from this one, with its looks ($1A438): a2, mi if none */
static void spawn_at_offset(void)
{
    wr16(0xFFFFA394u, (uint16_t)c->d[0]);
    c->d[0] = set_w(c->d[0], (uint16_t)(int8_t)rd8(c->a[0])); c->a[0]++;
    if (rd8(e(-0x3FFE)) & 8) c->d[0] = set_w(c->d[0], (uint16_t)-c->d[0]);
    c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x3800)));
    wr16(0xFFFFA396u, (uint16_t)c->d[0]);
    c->d[0] = set_w(c->d[0], (uint16_t)(int8_t)rd8(c->a[0]));
    if (rd8(e(-0x3FFE)) & 0x10) c->d[0] = set_w(c->d[0], (uint16_t)-c->d[0]);
    c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x3700)));
    wr16(0xFFFFA398u, (uint16_t)c->d[0]);
    wr16(0xFFFFA39Au, rd16(e(-0x3DFE)));
    call(0x1A438);
}
/* $18290: d2 of its graphics' slots in $8E08 (and the row after) marked free */
static void gfx_slots_free(void)
{
    c->a[0] = 0xFFFF8E08u + ((uint32_t)rd16(e(-0x3DFE)) >> 4);
    c->d[0] = 0xFFFFFFFFu;
    do {
        wr16(c->a[0] + 0x80, 0xFFFF);
        wr16(c->a[0], 0xFFFF); c->a[0] += 2;
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
    } while (c->d[2] & 0xFFFF);
}
/* $182AA: d2 tiles of its graphics (VRAM -$3DFE * 32) read back, masked to the colour d1 picks
 * ($FFF rotated), written again; the buffer is on the stack, from a7 + d2*64 as the original's;
 * the VDP's increment 32 meanwhile ($708), then 2 */
static void tint_tiles(void)
{
    c->d[0] = 0x20; call(0x708);
    c->a[2] = c->a[7];
    c->d[2] = set_w(c->d[2], (uint16_t)(c->d[2] << 5));
    c->d[0] = set_w(c->d[0], (uint16_t)(c->d[2] * 2));
    c->a[7] += sx(c->d[0]);
    c->d[2] = set_w(c->d[2], c->d[2] - 1);
    c->d[5] = set_w(c->d[5], (uint16_t)(rd16(e(-0x3DFE)) << 5));
    c->d[3] = (uint32_t)(uint16_t)c->d[5] << 2;
    c->d[4] = set_w(c->d[4], 0xFFF);
    if (c->d[1] & 1) c->d[4] = set_w(c->d[4], (uint16_t)((uint16_t)c->d[4] >> 4 | (uint16_t)c->d[4] << 12));
    c->d[1] = set_w(c->d[1], (uint16_t)c->d[1] >> 1);
    if (c->d[1] & 1) {
        c->d[1] &= ~1u;
        c->d[4] = set_w(c->d[4], (uint16_t)((uint16_t)c->d[4] >> 8 | (uint16_t)c->d[4] << 8));
    }
    c->d[5] = set_w(c->d[5], (c->d[5] | c->d[1]) & 0x3FFF);
    c->d[3] = set_w(c->d[3], c->d[5]);
    c->a[1] = 0xC00000;
    c->d[3] = c->d[3] << 16 | c->d[3] >> 16;
    wr32(0xC00004, c->d[3]);
    c->a[0] = c->a[7];
    c->d[5] = set_w(c->d[5], c->d[2]);
    do {
        c->d[0] = set_w(c->d[0], rd16(0xC00000) & (uint16_t)c->d[4]);
        wr16(c->a[0], (uint16_t)c->d[0]); c->a[0] += 2;
        c->d[5] = set_w(c->d[5], c->d[5] - 1);
    } while ((c->d[5] & 0xFFFF) != 0xFFFF);
    c->d[3] |= 0x40000000;
    wr32(0xC00004, c->d[3]);
    c->a[0] = c->a[7];
    do {
        wr16(0xC00000, rd16(c->a[0])); c->a[0] += 2;
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
    } while ((c->d[2] & 0xFFFF) != 0xFFFF);
    c->a[7] = c->a[2];
    c->d[0] = 2; call(0x708);
}
/* $18314: the partner's animation d0 */
static void partner_anim(void) { partner(); call(SET_ANIM); partner(); }
/* $18332: placed on the partner by its animation (the table at $18372: x, y, sizes) */
static void on_partner(void)
{
    c->a[0] = sx(rd16(e(-0x2CFE)));
    c->d[0] = (uint32_t)rd8(c->a[0] - 0x3CFE) * 2;
    c->d[1] = set_w(c->d[1], (uint16_t)(int8_t)rd8(0x18372 + c->d[0]));
    if (rd8(c->a[0] - 0x3FFE) & 8) c->d[1] = set_w(c->d[1], (uint16_t)-c->d[1]);
    c->d[1] = set_w(c->d[1], c->d[1] + rd16(c->a[0] - 0x3800));
    put16(e(-0x3800), c->d[1]);
    c->d[1] = set_w(c->d[1], (uint16_t)(int8_t)rd8(0x18373 + c->d[0]));
    c->d[1] = set_w(c->d[1], c->d[1] + rd16(c->a[0] - 0x3700));
    put16(e(-0x3700), c->d[1]);
    put8(e(-0x3500), rd8(0x18374 + c->d[0]));
    put8(e(-0x34FF), rd8(0x18375 + c->d[0]));
}
/* $18322: the partner falls and moves ($B44, $46DA); this one on it */
static void partner_move(void)
{
    partner();
    call(0xB44);
    call(0x46DA);
    partner();
    on_partner();
}
/* $1839C: the mount (partner) takes its health and not its touch; the player against it */
static void mount_touch(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x2600)));
    partner();
    put16(e(-0x2600), c->d[0]);
    put8(e(-0x2AFD), 0);
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    call(0xE64);
    partner();
}
/* $183BA: z when the partner (a2) is on the ground; $183C8: its wall bit (btst: z clear at one) */
static void partner_grounded(void)
{
    c->a[2] = sx(rd16(e(-0x2CFE)));
    c->d[1] = 0xA & rd8(o(-0x3100));
    c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 2, c->d[1]));
}
static void partner_at_wall(void)
{
    c->a[2] = sx(rd16(e(-0x2CFE)));
    c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (rd8(o(-0x3100)) & 1 ? 0 : CC_Z));
}
/* $183D4: the screen shaken ($9738 -5), sound $39 */
static void shake(void)
{
    put16(0xFFFF9738u, 0xFFFB);
    c->d[0] = 0x39; call(0x366);
}

ENTRY(Room16Setup, room16_setup)           /* $17FEC */
ENTRY(FloorScroll, floor_scroll)           /* $18074 */
ENTRY(Room16Frame, room16_frame)           /* $1813E */
ENTRY(Room16Exit, room16_exit)             /* $1822A */
ENTRY(SpawnAtOffset, spawn_at_offset)      /* $18256 */
ENTRY(GfxSlotsFree, gfx_slots_free)        /* $18290 */
ENTRY(TintTiles, tint_tiles)               /* $182AA */
ENTRY(PartnerAnim, partner_anim)           /* $18314 */
ENTRY(PartnerMove, partner_move)           /* $18322 */
ENTRY(OnPartner, on_partner)               /* $18332 */
/* $1838A: touching the player and the sword; hit: state 2 (the caller's frame dropped); else
 * as $1839C */
void MountHit(Cpu *cpu)
{
    c = cpu;
    call(0x4792);
    call(0x439E);
    if (c->ccr & CC_N) { leave_to_state(2); return; }
    mount_touch();
    c->ret = 0;
}
ENTRY(MountTouch, mount_touch)             /* $1839C */
ENTRY(PartnerGrounded, partner_grounded)   /* $183BA */
ENTRY(PartnerAtWall, partner_at_wall)      /* $183C8 */
ENTRY(Shake, shake)                        /* $183D4 */
