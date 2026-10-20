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
static void call(uint32_t addr) { Cpu *self = c; call_68k(c, addr); c = self; }   /* c kept: see port/rooms.c */

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
/* $17302: the same from its loop's body: the field d0 copied first */
static void copy_fields_step(void)
{
    c->d[0] = sx(c->d[0]);
    wr16(c->a[2] + c->d[0], rd16(c->a[4] + c->d[0]));
    copy_fields();
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
ENTRY(CopyFieldsStep, copy_fields_step)    /* $17302 */
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

/* ---- batch T50: room script 18 (a flash), room script 17's setup and frame ---- */

/* $189EA: palette entry d0>>1 (CRAM $3A..., word d0 & $3E) keeps only the component(s) d0's low
 * two bits leave (the mask $FFF0 rotated), read and written back */
static void flash_colour(void)
{
    c->d[1] = 0x10000;
    c->d[1] = set_b(c->d[1], c->d[0]);
    c->d[1] = set_b(c->d[1], (c->d[1] & 0xFF) >> 1);
    c->d[1] = set_w(c->d[1], (c->d[1] & 0x3E) + 0x1D00);
    c->d[1] = c->d[1] << 16 | c->d[1] >> 16;
    c->a[0] = 0xC00000;
    wr32(0xC00004, c->d[1]);
    c->d[2] = set_w(c->d[2], rd16(0xC00000));
    c->d[3] = 0xFFFFFFF0u;
    uint16_t m = (uint16_t)c->d[3];
    int b0 = c->d[0] & 1; c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 1);
    if (!b0) m = (uint16_t)(m << 4 | m >> 12);
    int b1 = c->d[0] & 1; c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 1);
    if (!b1) m = (uint16_t)(m << 8 | m >> 8);
    c->d[3] = set_w(c->d[3], m);
    c->d[2] = set_w(c->d[2], c->d[2] & m);
    c->d[1] |= 0x40000000;
    wr32(0xC00004, c->d[1]);
    wr16(0xC00000, (uint16_t)c->d[2]);
}
/* $1898C: room script 18's setup: armed */
static void room18_setup(void) { put8(0xFFFF99DEu, 1); }
/* $18994: room script 18's frame. Armed and triggered (bit 0): $80 flashes of 2 frames from
 * colour $14 on (each $25 on), the graphics slots $8E36/$8EB6 freed; at the end colour script $C */
static void room18_frame(void)
{
    if (!(rd8(0xFFFF99DEu) & 2)) {
        if (!(rd8(0xFFFF99DEu) & 1)) return;
        wr32(0xFFFF99DEu, 0x02808014);
        c->d[0] = 0xFFFFFFFFu;
        wr16(0xFFFF8E36u, 0xFFFF);
        wr16(0xFFFF8EB6u, 0xFFFF);
        return;
    }
    uint8_t n = (uint8_t)(rd8(0xFFFF99DFu) - 1);
    wr8(0xFFFF99DFu, n);
    if (n) return;
    wr8(0xFFFF99DFu, 2);
    c->d[0] = set_b(c->d[0], rd8(0xFFFF99E1u));
    c->d[1] = set_b(c->d[1], c->d[0] + 0x25);
    wr8(0xFFFF99E1u, (uint8_t)c->d[1]);
    c->d[0] = set_b(c->d[0], c->d[0] ^ 0x15);
    flash_colour();
    n = (uint8_t)(rd8(0xFFFF99E0u) - 1);
    wr8(0xFFFF99E0u, n);
    if (n) return;
    put8(0xFFFF99DEu, 0);
    c->d[0] = 0xC;
    port_continue_at(c, 0x5658, 0);
}
/* $18A3E: placed beside the partner (2 ahead), facing as it, at its own height (-$36FD) above it,
 * its collision as the partner's */
static void beside_partner(void)
{
    c->a[0] = sx(rd16(e(-0x2CFE)));
    c->d[2] = set_b(c->d[2], rd8(c->a[0] - 0x3FFE));
    put8(e(-0x3FFE), c->d[2]);
    c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x3800));
    c->d[1] = 2;
    if (c->d[2] & 8) c->d[1] = set_w(c->d[1], (uint16_t)-2);
    c->d[0] = set_w(c->d[0], c->d[0] + c->d[1]);
    put16(e(-0x3800), c->d[0]);
    c->d[0] = set_w(c->d[0], (uint16_t)((int8_t)rd8(e(-0x36FD)) + rd16(c->a[0] - 0x3700)));
    put16(e(-0x3700), c->d[0]);
    put8(e(-0x3100), rd8(c->a[0] - 0x3100));
}
/* $18A32: the partner falls and moves ($46D6); this one beside it */
static void partner_fall_beside(void) { partner(); call(0x46D6); partner(); beside_partner(); }
/* $18A74: both blink ($451A) */
static void both_blink(void) { partner(); call(0x451A); partner(); call(0x451A); }
/* $18A96: the partner takes its health; the player against it, and its sword's touch unless the
 * other is in state 2 */
static void rider_touch(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x2600)));
    partner();
    put16(e(-0x2600), c->d[0]);
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    call(0xE64);
    c->a[0] = sx(rd16(e(-0x2CFE)));
    c->d[0] = 0x7F & rd8(c->a[0] - 0x2BFF);
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 2, c->d[0]));
    if (c->d[0] & 0xFF) call(0xC3E);
    partner();
}

/* room script 17: the boss's room ($99F8 its body, $99E8 its head, $99EE-$99F6 the parts) */
/* $196D6: entity a4 made from the 12 bytes at a2: its look, sprite flags, animation (at once),
 * home and place, graphics ($99FC), kind, animation rate; in use and drawn */
static void part_make(void)
{
    call(0xA36);
    wr16(e(-0x3D00), rd16(c->a[2])); c->a[2] += 2;
    wr8(e(-0x3FFE), rd8(c->a[2])); c->a[2]++;
    c->d[0] = set_b(c->d[0], rd8(c->a[2])); c->a[2]++;
    call(0x7FC);
    c->d[0] = rd32(c->a[2]); c->a[2] += 4;
    wr32(e(-0x2100), c->d[0]);
    wr16(e(-0x3700), (uint16_t)c->d[0]);
    c->d[0] = c->d[0] << 16 | c->d[0] >> 16;
    wr16(e(-0x3800), (uint16_t)c->d[0]);
    wr16(e(-0x3DFE), rd16(0xFFFF99FCu));
    wr16(e(-0x2BFE), rd16(c->a[2])); c->a[2] += 2;
    wr8(e(-0x3CFD), rd8(c->a[2])); c->a[2] += 2;
    put8(e(-0x4000), 0xC0);
}
/* $19612: room script 17's setup: its tiles, kind $6E's graphics, six parts (effects) and the
 * body (an enemy) from $1967C, the head, and the boss's spawn ($12650) */
static void room17_setup(void)
{
    c->d[0] = set_w(c->d[0], 0x5380);
    c->d[1] = set_w(c->d[1], 0x207C);
    c->d[2] = 8;
    call(0x22A8);
    wr16(0xFFFF9EDAu, 0);
    wr8(0xFFFFA38Eu, 0);
    wr8(0xFFFFA390u, 0);
    c->d[1] = 0x6E;
    call(0x4910);
    wr16(0xFFFF99FCu, rd16(c->a[0]));
    c->a[2] = 0x1967C;
    c->a[3] = 0xFFFF99EEu;
    for (c->d[7] = 5;; ) {
        call(0x27E6);
        wr16(c->a[3], (uint16_t)c->a[4]); c->a[3] += 2;
        part_make();
        c->d[7] = set_w(c->d[7], c->d[7] - 1);
        if ((c->d[7] & 0xFFFF) == 0xFFFF) break;
    }
    c->a[4] = c->a[2];
    call(0x12812);
    { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }
    wr16(c->a[3], (uint16_t)c->a[4]); c->a[3] += 2;
    part_make();
    wr32(e(-0x2900), 0x196D0);
    call(0x12812);
    wr16(0xFFFF99E8u, (uint16_t)c->a[2]);
    c->d[0] = 0xFFFFFFFFu;
    c->d[1] = set_w(c->d[1], 0x6E);
    c->d[6] = set_w(c->d[6], 0x10A0);
    c->d[7] = set_w(c->d[7], 0x1048);
    port_continue_at(c, 0x12650, 0);
}
/* $1979C: the tiles' wobble: $A391 swings ($A38E's sign, every $C0 frames) between -$40 and $40;
 * $A390 its phase; $9EDA the step; the tiles' frame by the phase ($19800) */
static void wobble(void)
{
    int8_t dir = (int8_t)rd8(0xFFFFA38Eu);
    if (!dir) return;
    int8_t sp = (int8_t)rd8(0xFFFFA391u);
    if (dir < 0) { if (sp > (int8_t)0xC0) wr8(0xFFFFA391u, (uint8_t)(sp - 4)); }
    else { if (sp < 0x40) wr8(0xFFFFA391u, (uint8_t)(sp + 4)); }
    c->d[0] = set_b(c->d[0], rd8(0xFFFFA391u));
    c->d[1] = set_b(c->d[1], rd8(0xFFFFA390u));
    c->d[0] = set_b(c->d[0], c->d[0] + c->d[1]);
    wr8(0xFFFFA390u, (uint8_t)c->d[0]);
    c->d[0] = set_w(c->d[0], c->d[0] & 0xE0);
    c->d[2] = set_w(c->d[2], c->d[0]);
    c->d[1] = set_b(c->d[1], c->d[1] & 0xE0);
    c->d[0] = set_b(c->d[0], (uint8_t)((int8_t)(uint8_t)(c->d[0] - c->d[1]) >> 5));
    wr8(0xFFFF9EDAu, (uint8_t)c->d[0]);
    uint8_t n = (uint8_t)(rd8(0xFFFFA38Fu) - 1);
    wr8(0xFFFFA38Fu, n);
    if (!n) { wr8(0xFFFFA38Fu, 0xC0); wr8(0xFFFFA38Eu, (uint8_t)-(int8_t)rd8(0xFFFFA38Eu)); }
    c->d[2] = set_w(c->d[2], (uint16_t)c->d[2] >> 4);
    c->d[1] = set_w(c->d[1], rd16(0x19800 + (c->d[2] & 0xFFFF)));
    c->d[0] = set_w(c->d[0], 0x5380);
    c->d[2] = 8;
    call(0x22A8);
}
/* $1985A: a free enemy slot ($12812) made kind $80C at ($1000, $1080) ($12650); kept in $A392 */
static void room17_extra(void)
{
    call(0x12812);
    if (c->ccr & CC_N) return;
    wr16(0xFFFFA392u, (uint16_t)c->a[2]);
    c->d[0] = 0xFFFFFFFFu;
    c->d[1] = set_w(c->d[1], 0x80C);
    c->d[6] = set_w(c->d[6], 0x1000);
    c->d[7] = set_w(c->d[7], 0x1080);
    port_continue_at(c, 0x12650, 0);
}
/* $19878: the boss's effects: a shudder of $99F6 ($99DE bit 4, $99EA frames), a white flash of
 * colours $8C2A ($99DE bit 5, $99EB frames), a tint of the head ($99DE bit 3, by $182AA) */
static void room17_effects(void)
{
    if (rd8(0xFFFF99DEu) & 0x10) {
        c->a[4] = sx(rd16(0xFFFF99F6u));
        uint8_t n = (uint8_t)(rd8(0xFFFF99EAu) - 1);
        wr8(0xFFFF99EAu, n);
        if (!n) {
            wr8(0xFFFF99DEu, rd8(0xFFFF99DEu) & 0xEF);
            wr16(e(-0x3800), rd16(e(-0x2100)));
            put16(e(-0x3700), rd16(e(-0x20FE)));
        } else {
            c->d[0] = 2;
            if (n & 1) c->d[0] = set_w(c->d[0], (uint16_t)-2);
            c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x2100)));
            put16(e(-0x3800), c->d[0]);
        }
    }
    if (rd8(0xFFFF99DEu) & 0x20) {
        c->a[0] = 0xFFFF8C2Au;
        c->d[1] = 4;
        uint8_t n = (uint8_t)(rd8(0xFFFF99EBu) - 1);
        wr8(0xFFFF99EBu, n);
        int white = 0;
        if (!n) wr8(0xFFFF99DEu, rd8(0xFFFF99DEu) & 0xDF);
        else white = n & 1;
        if (white) {
            c->d[0] = set_w(c->d[0], 0xEEE);
            for (int k = 0; k < 5; k++) { wr16(c->a[0], 0xEEE); c->a[0] += 2; }
        } else {
            c->a[1] = 0xFFFF8BAAu;
            for (int k = 0; k < 5; k++) { wr16(c->a[0], rd16(c->a[1])); c->a[0] += 2; c->a[1] += 2; }
        }
        c->d[1] = 0xFFFF;
        wr8(0xFFFF8C56u, (uint8_t)((rd8(0xFFFF8C56u) & 0x3F) | 0x81));
    }
    if (!(rd8(0xFFFF99DEu) & 8)) return;
    uint8_t n = (uint8_t)(rd8(0xFFFF99EBu) - 1);
    wr8(0xFFFF99EBu, n);
    if (!n) { wr8(0xFFFF99DEu, rd8(0xFFFF99DEu) & 0xF7); return; }
    if (!(n & 1)) return;
    c->a[4] = sx(rd16(0xFFFF99E8u));
    c->d[0] = set_b(c->d[0], rd8(0xFFFF99ECu));
    c->d[1] = c->d[0] & 0xFF;
    c->d[0] = set_b(c->d[0], (c->d[0] + 0x25) & 0x3F);
    wr8(0xFFFF99ECu, (uint8_t)c->d[0]);
    c->d[1] = set_b(c->d[1], c->d[1] ^ 0x15);
    c->d[2] = 3;
    wr16(e(-0x3DFE), (uint16_t)(rd16(e(-0x3DFE)) + 0x20));
    call(0x182AA);
    wr16(e(-0x3DFE), (uint16_t)(rd16(e(-0x3DFE)) - 0x20));
}
/* $19710: room script 17's frame: the wobble, its head ($99FA, $19B96), the parts ($19AC0); when
 * the boss is beaten ($19B4E) the body falls beside the player and the player is held ($9F05);
 * its states ($19946), the effects; then, the room left (bit 0 of $99E6), the next scene */
static void room17_frame(void)
{
    wobble();
    c->d[0] = set_w(c->d[0], rd16(0xFFFF99FAu));
    if (!(c->d[0] & 0x8000)) { c->a[4] = sx(c->d[0]); call(0x19B96); }
    call(0x19AC0);
    call(0x19B4E);
    if (!(c->ccr & CC_Z)) {
        wr8(0xFFFF99DEu, rd8(0xFFFF99DEu) | 3);
        c->a[4] = sx(rd16(0xFFFF99F8u));
        wr16(e(-0x3DFE), 0);
        c->d[0] = 0x18;
        c->a[0] = sx(rd16(0xFFFF9EEEu));
        if (rd16(c->a[0] - 0x3800) >= 0x1050) { wr8(e(-0x3FFE), 0); c->d[0] = set_w(c->d[0], (uint16_t)-0x18); }
        c->d[0] = set_w(c->d[0], c->d[0] + rd16(c->a[0] - 0x3800));
        put16(e(-0x3800), c->d[0]);
        put16(e(-0x35FE), 0x2000);
        put8(e(-0x2800), 5);
        put8(e(-0x31FF), 0);
        put8(0xFFFF9F05u, 8);
    }
    call(0x19946);
    c->a[7] -= 4; wr32(c->a[7], 0x19772);           /* bsr: $182AA's buffer is placed by the stack */
    room17_effects();
    c->a[7] += 4;
    if (rd8(0xFFFF99E6u)) {
        if (rd8(0xFFFF8C56u) & 0x10) wr8(0xFFFF965Fu, rd8(0xFFFF965Fu) | 8);
    } else if (rd8(0xFFFF965Fu) & 4) {
        wr8(0xFFFF99E6u, 0xFF);
        call(0x5842);
    }
}

ENTRY(Room18Setup, room18_setup)           /* $1898C */
void Room18Frame(Cpu *cpu) { c = cpu; c->ret = 0; room18_frame(); if (c->ret != 0x5658) c->ret = 0; }   /* $18994 */
ENTRY(FlashColour, flash_colour)           /* $189EA */
ENTRY(PartnerAnim2, partner_anim)          /* $18A24 */
ENTRY(PartnerFallBeside, partner_fall_beside)   /* $18A32 */
ENTRY(BesidePartner, beside_partner)       /* $18A3E */
ENTRY(BothBlink, both_blink)               /* $18A74 */
/* $18A84: touching the player and the sword; hit: state 2 (the caller's frame dropped); else as
 * $18A96 */
void RiderHit(Cpu *cpu)
{
    c = cpu;
    call(0x4792);
    call(0x439E);
    if (c->ccr & CC_N) { leave_to_state(2); return; }
    rider_touch();
    c->ret = 0;
}
ENTRY(RiderTouch, rider_touch)             /* $18A96 */
ENTRY(PartnerGrounded2, partner_grounded)  /* $18AC2 */
ENTRY(PartnerAtWall2, partner_at_wall)     /* $18AD2 */
void Room17Setup(Cpu *cpu) { c = cpu; room17_setup(); }   /* $19612: goes on at $12650 */
ENTRY(PartMake, part_make)                 /* $196D6 */
ENTRY(Room17Frame, room17_frame)           /* $19710 */
ENTRY(Wobble, wobble)                      /* $1979C */
void Room17Extra(Cpu *cpu) { c = cpu; room17_extra(); if (c->ret != 0x12650) c->ret = 0; }   /* $1985A */
ENTRY(Room17Effects, room17_effects)       /* $19878 */

/* ---- batch T51: room script 17's states, parts, head and debris ---- */

static int count8(uint32_t a) { uint8_t n = (uint8_t)(rd8(a) - 1); wr8(a, n); return n; }   /* subq.b #1: nonzero */
static void next_step(void) { wr8(e(-0x31FF), (uint8_t)(rd8(e(-0x31FF)) + 2)); }
static void anim(uint32_t n) { c->d[0] = n; F_L(n); call(SET_ANIM); }

/* $19946: once the boss is beaten ($99DE bit 1), the body ($99F8) by its step (-$31FF, the table
 * at $19964): it falls, the player is held, the room's door scene ($9C16 $70) plays, the body
 * walks to x $1090, jumps, lands, a second scene ($71), a sound, then it blinks away and the
 * parts come back ($A117) */
static void room17_states(void)
{
    if (!(rd8(0xFFFF99DEu) & 2)) return;
    c->a[4] = sx(rd16(0xFFFF99F8u));
    c->d[0] = rd8(e(-0x31FF));
    c->a[0] = 0x19964 + sx(rd16(0x19964 + c->d[0]));
    switch (c->a[0]) {
    case 0x1997A:
        call(0xAD6);
        if (count8(e(-0x2800))) return;
        next_step(); put8(e(-0x2800), 3); anim(2);
        return;
    case 0x19994:
        if (count8(e(-0x2800))) return;
        next_step(); put8(0xFFFF9F05u, 6); wr32(0xFFFF9F22u, 0x19AB8); anim(0);
        return;
    case 0x199B2:
        next_step(); put8(e(-0x2800), 0x1E); put8(0xFFFF9F05u, 8);
        return;
    case 0x199C4:
    case 0x19A54:
        if (count8(e(-0x2800))) return;
        next_step(); wr16(0xFFFF9C16u, c->a[0] == 0x199C4 ? 0x70 : 0x71); put16(0xFFFF9962u, 8);
        return;
    case 0x199DC:
        next_step(); wr8(e(-0x3FFE), rd8(e(-0x3FFE)) & 0xF7); put16(e(-0x3600), 0x200); anim(6);
        return;
    case 0x199F2:
        call(0xAB8);
        if (rd16(e(-0x3800)) < 0x1090) return;
        next_step();
        wr8(0xFFFF99DEu, rd8(0xFFFF99DEu) | 4);
        wr32(e(-0x3600), 0xFE00FC00u);
        put8(e(-0x2F00), 3);
        put16(e(-0x32FE), 0x400);
        put16(e(-0x33FE), 0x40);
        anim(8);
        return;
    case 0x19A28:
        call(0x46D6);
        call(0x4362);
        if (!(c->ccr & CC_Z)) return;
        next_step(); put8(e(-0x2800), 0x10);
        return;
    case 0x19A3E:
        if (count8(e(-0x2800))) return;
        next_step(); put8(e(-0x2800), 0x1E); anim(0xA);
        return;
    case 0x19A6C:
        next_step();
        c->d[0] = 0x52; call(0x366);
        wr8(e(-0x3FFD), rd8(e(-0x3FFD)) | 0x40);
        put16(e(-0x3900), 0x6001);
        put16(e(-0x2800), 0x710);
        return;
    case 0x19A8A:
        if (count8(e(-0x27FF))) return;
        if (count8(e(-0x2800))) {
            c->d[0] = 0x10;
            wr8(e(-0x27FF), 0x10);
            wr8(e(-0x3900), (uint8_t)(rd8(e(-0x3900)) - 0x10));
            return;
        }
        wr8(0xFFFF99DEu, rd8(0xFFFF99DEu) & 0xFD);
        wr8(0xFFFFA117u, 0x3C);
        wr8(e(-0x4000), 0);
        put8(0xFFFF9F05u, 0);
        return;
    default: call(c->a[0]); return;                 /* past the table: as the original jumps */
    }
}
/* $19AB8 (put at $9F22 by step 2): $9F0B 8 */
static void room17_hold(void) { put8(0xFFFF9F0Bu, 8); }
/* $19AC0: the two part timers ($A117, $A118): when one runs out, a free part of the four ($99EE
 * by the random) not busy, whose place ($19DBA) is free ($A112), gets an enemy of kind $70
 * ($12684) at that place; else the timer waits 16 more */
static void room17_parts(void)
{
    for (c->d[7] = 0; (uint16_t)c->d[7] < 2; c->d[7] = set_w(c->d[7], c->d[7] + 1)) {
        c->a[0] = sx(c->d[7]);
        uint32_t timer = c->a[0] - 0x5EE9;
        if (!rd8(timer) || count8(timer)) continue;
        call(0x12812);
        if (c->ccr & CC_N) goto wait16;
        call(0x5D8);
        c->d[0] = set_w(c->d[0], c->d[0] & 6);
        c->a[0] = sx(c->d[0]);
        c->a[4] = sx(rd16(c->a[0] - 0x6612));
        if (rd8(e(-0x31FF))) goto wait16;
        c->d[1] = set_w(c->d[1], c->d[0] * 3);
        c->a[0] = 0x19DBA + sx(c->d[1]);
        c->d[1] = rd8(c->a[0]);
        c->a[1] = sx(c->d[1]);
        if (rd8(c->a[1] - 0x5EEE)) goto wait16;
        wr8(e(-0x31FF), (uint8_t)(rd8(e(-0x31FF)) + 1));
        wr8(c->a[1] - 0x5EEE, 0xFF);
        c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->d[7]);
        c->a[7] -= 4; wr32(c->a[7], c->a[0]);
        { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }
        c->d[1] = set_w(c->d[1], 0x70);
        wr8(e(-0x2B00), 0xFF);
        wr16(e(-0x2BFE), 0x70);
        call(0x12684);
        c->a[0] = rd32(c->a[7]); c->a[7] += 4;
        c->d[7] = set_w(c->d[7], rd16(c->a[7])); c->a[7] += 2;
        put8(o(-0x36FD), rd8(c->a[0])); c->a[0]++;
        put8(o(-0x25FE), rd8(c->a[0])); c->a[0]++;
        put16(o(-0x3800), rd16(c->a[0])); c->a[0] += 2;
        put16(o(-0x3700), rd16(c->a[0]));
        put8(o(-0x25FD), c->d[7]);
        continue;
    wait16:
        c->a[0] = sx(c->d[7]);
        wr8(c->a[0] - 0x5EE9, 0x10);
    }
}
/* $19B4E: -1 (nz) when the boss is beaten: not yet marked ($99DE bit 0), $99ED at 2 or more, the
 * player's health $100 or less, and no enemy but kinds $70 and $6E (or none) about */
static void room17_beaten(void)
{
    c->d[0] = 0; F_L(0);
    if (rd8(0xFFFF99DEu) & 1) return;
    if (rd8(0xFFFF99EDu) < 2) return;
    c->a[0] = sx(rd16(0xFFFF9EEEu));
    if (rd16(c->a[0] - 0x2600) > 0x100) return;
    c->a[0] = sx(rd16(0xFFFFA11Au));
    for (int k = 0; k < 16; k++, c->a[0] += 4) {
        if (!(rd8(c->a[0] - 0x4000) & 0x80)) continue;
        uint16_t kind = rd16(c->a[0] - 0x2BFE);
        c->d[1] = set_w(c->d[1], kind);
        if ((kind & 0x8000) || kind == 0x70 || kind == 0x6E) continue;
        c->d[0] = 0; F_L(0);
        return;
    }
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
}
/* $19C34: a piece of debris (a free slot, $12812) within d4 across and d5 down of this one, with
 * the boss's graphics, set up ($19D60), drawn; sound $3F */
static void room17_debris(void)
{
    call(0x12812);
    if (c->ccr & CC_N) return;
    { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }
    call(0xA36);
    call(0x5D8);
    c->d[1] = c->d[0];
    c->d[0] = set_w(c->d[0], c->d[0] & c->d[4]);
    if (c->d[0] & 0x10000) c->d[0] = set_w(c->d[0], (uint16_t)-c->d[0]);
    c->d[0] = set_w(c->d[0], c->d[0] + rd16(o(-0x3800)));
    put16(e(-0x3800), c->d[0]);
    c->d[1] >>= 8;
    c->d[1] = set_w(c->d[1], c->d[1] & c->d[5]);
    if (c->d[1] & 0x10000) c->d[1] = set_w(c->d[1], (uint16_t)-c->d[1]);
    c->d[1] = set_w(c->d[1], c->d[1] + rd16(o(-0x3700)));
    put16(e(-0x3700), c->d[1]);
    wr16(e(-0x3DFE), rd16(0xFFFF99FCu));
    call(0x19D60);
    put8(e(-0x4000), 0xC0);
    c->d[0] = 0x3F; call(0x366);
    { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }
}
/* $19B96: the head ($99FA). Once the body lands ($99DE bit 2) it blinks away, $A frames of debris;
 * before, not yet beaten: it hurts the player ($E64, $C3E), and every $5A frames ($F after a
 * first) spits a fireball (kind $222 at $19C32, $18256) partnered with it */
static void room17_head(void)
{
    if (rd8(0xFFFF99DEu) & 4) {
        wr8(0xFFFF99DEu, rd8(0xFFFF99DEu) & 0xFB);
        wr8(e(-0x31FF), (uint8_t)(rd8(e(-0x31FF)) + 1));
        put8(e(-0x2800), 0xA);
        wr8(e(-0x3FFD), rd8(e(-0x3FFD)) | 0x40);
        put16(e(-0x3900), 1);
    } else if (rd8(e(-0x31FF))) {
        if (count8(e(-0x27FF))) return;
    } else {
        if (rd8(0xFFFF99DEu) & 2) return;
        c->a[2] = sx(rd16(0xFFFF9EEEu));
        put16(e(-0x2700), 0x2000);
        call(0xE64);
        call(0xC3E);
        wr8(e(-0x2800), (uint8_t)(rd8(e(-0x2800)) + 1));
        c->d[0] = rd8(e(-0x27FF)) ? 0xF : 0x5A;
        if ((uint8_t)c->d[0] >= rd8(e(-0x2800))) return;
        put8(e(-0x2800), 0);
        c->d[0] = set_w(c->d[0], 0x222);
        c->a[0] = 0x19C32;
        call(0x18256);
        if (c->ccr & CC_N) return;
        wr16(e(-0x2CFE), (uint16_t)c->a[2]);
        put16(o(-0x2CFE), c->a[4]);
        return;
    }
    c->d[0] = set_b(c->d[0], rd8(e(-0x2800)));
    if (!(c->d[0] & 0xFF)) {
        put8(e(-0x4000), 0);
        wr8(0xFFFF99FAu, 0xFF);
        return;
    }
    c->d[0] = 6;
    if (!count8(e(-0x2800))) c->d[0] = 0x10;
    put8(e(-0x27FF), c->d[0]);
    c->d[4] = 0xF;
    c->d[5] = 7;
    c->a[7] -= 4; wr32(c->a[7], 0x19C30);
    room17_debris();
    c->a[7] += 4;
}

ENTRY(Room17States, room17_states)         /* $19946 */
ENTRY(Room17Hold, room17_hold)             /* $19AB8 */
ENTRY(Room17Parts, room17_parts)           /* $19AC0 */
ENTRY(Room17Beaten, room17_beaten)         /* $19B4E */
ENTRY(Room17Head, room17_head)             /* $19B96 */
ENTRY(Room17Debris, room17_debris)         /* $19C34 */

/* $19DD2: the touch tests at a point ahead by its direction (-$27FE, the table at $19E1C): the
 * player's touch and sword ($4792; $4778 alone in state 2), then hit ($439E) */
static void touch_ahead(void)
{
    c->d[0] = 0x10;
    c->d[0] = set_b(c->d[0], (c->d[0] + rd8(e(-0x27FE))) & 0xE0);
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 4);
    c->d[1] = set_w(c->d[1], (uint16_t)(int8_t)rd8(0x19E1C + c->d[0]));
    c->a[7] -= 2; wr16(c->a[7], rd16(e(-0x3800)));
    c->a[7] -= 2; wr16(c->a[7], rd16(e(-0x3700)));
    wr16(e(-0x3800), (uint16_t)(rd16(e(-0x3800)) + (uint16_t)c->d[1]));
    c->d[1] = set_w(c->d[1], (uint16_t)(int8_t)rd8(0x19E1D + c->d[0]));
    wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) + (uint16_t)c->d[1]));
    c->a[0] = 0x4792;
    c->d[0] = set_b(c->d[0], rd8(e(-0x2BFF)));
    c->d[0] = set_b(c->d[0], c->d[0] * 2);
    if ((c->d[0] & 0xFF) == 4) c->a[0] = 0x4778;
    call(c->a[0]);
    wr16(e(-0x3700), rd16(c->a[7])); c->a[7] += 2;
    wr16(e(-0x3800), rd16(c->a[7])); c->a[7] += 2;
    call(0x439E);
}
/* $19E2C: the animation for its direction (-$27FE / 16 + 1, even), unless it has it */
static void dir_anim(void)
{
    c->d[0] = set_b(c->d[0], ((rd8(e(-0x27FE)) >> 4) + 1) & 0xE);
    if ((c->d[0] & 0xFF) == rd8(e(-0x3CFE))) { c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); return; }
    call(SET_ANIM);
}
/* $19E44: z when at its target point (-$36FD into the table at $19DA6) */
static void at_target(void)
{
    c->d[0] = (uint32_t)rd8(e(-0x36FD)) << 2;
    c->a[0] = 0x19DA6;
    c->d[1] = set_w(c->d[1], rd16(e(-0x3800)));
    cmp_w(&c->ccr, rd16(c->a[0] + c->d[0]), c->d[1]);
    if ((c->d[1] & 0xFFFF) != rd16(c->a[0] + c->d[0])) return;
    c->d[1] = set_w(c->d[1], rd16(e(-0x3700)));
    cmp_w(&c->ccr, rd16(c->a[0] + c->d[0] + 2), c->d[1]);
}
/* $19E64: a pixel by its way (-$25FE: 0 right, 1 down, 2 left, else up) */
static void step_by_dir(void)
{
    c->d[0] = set_b(c->d[0], rd8(e(-0x25FE)));
    uint8_t w = (uint8_t)c->d[0];
    if (!w) { wr16(e(-0x3800), (uint16_t)(rd16(e(-0x3800)) + 1)); return; }
    c->d[0] = set_b(c->d[0], w - 2);
    if (w < 2) { wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) + 1)); return; }
    if (w == 2) { wr16(e(-0x3800), (uint16_t)(rd16(e(-0x3800)) - 1)); return; }
    wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) - 1));
}
/* $19E88: the direction (-$27FE) 4 nearer the wanted one (-$27FD), its animation; z when there */
static void turn_toward(void)
{
    c->d[2] = 4;
    c->d[0] = set_b(c->d[0], rd8(e(-0x27FE)));
    c->d[1] = set_b(c->d[1], rd8(e(-0x27FD)));
    cmp_b(&c->ccr, c->d[0], c->d[1]);
    if ((c->d[0] & 0xFF) == (c->d[1] & 0xFF)) return;
    if (c->ccr & CC_N) c->d[2] = set_b(c->d[2], (uint8_t)-4);
    c->d[0] = set_b(c->d[0], c->d[0] + c->d[2]);
    wr8(e(-0x27FE), (uint8_t)c->d[0]);
    dir_anim();
    c->d[0] = set_b(c->d[0], rd8(e(-0x27FE)));
    cmp_b(&c->ccr, rd8(e(-0x27FD)), c->d[0]);
}
/* $1A1C6: a room object that rises 32 and sinks back (-$31FF its step, the table at $1A1D8):
 * up 2 a frame for 16, a pause of $30, down 2 a frame for 16, then idle */
static void lifter(void)
{
    c->d[0] = (uint32_t)rd8(e(-0x31FF)) * 2;
    c->a[0] = 0x1A1D8 + sx(rd16(0x1A1D8 + c->d[0]));
    switch (c->a[0]) {
    case 0x1A1E0:
        wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) - 2));
        wr8(e(-0x2800), (uint8_t)(rd8(e(-0x2800)) + 1));
        if (rd8(e(-0x2800)) < 0x10) return;
        wr8(e(-0x31FF), (uint8_t)(rd8(e(-0x31FF)) + 1));
        put8(e(-0x27FF), 0x30);
        return;
    case 0x1A1FC:
        if (count8(e(-0x27FF))) return;
        wr8(e(-0x31FF), (uint8_t)(rd8(e(-0x31FF)) + 1));
        return;
    case 0x1A208:
        wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) + 2));
        if (count8(e(-0x2800))) return;
        put8(e(-0x31FF), 0);
        return;
    case 0x1A216: return;
    default: call(c->a[0]); return;                 /* past the table: as the original jumps */
    }
}
/* $1A438: an enemy in a free slot ($12812, a2) from the record at $A394 (kind, x, y, graphics),
 * no spawn point, set up ($12684); mi if no slot */
static void spawn_from_a394(void)
{
    call(0x12812);
    if (c->ccr & CC_N) return;
    { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }
    c->a[0] = 0xFFFFA394u;
    c->d[1] = set_w(c->d[1], rd16(c->a[0])); c->a[0] += 2;
    wr16(e(-0x2BFE), (uint16_t)c->d[1]);
    wr16(e(-0x3800), rd16(c->a[0])); c->a[0] += 2;
    wr16(e(-0x3700), rd16(c->a[0])); c->a[0] += 2;
    wr16(e(-0x3DFE), rd16(c->a[0])); c->a[0] += 2;
    wr8(e(-0x2B00), 0xFF);
    wr8(e(-0x2F00), 0);
    call(0x12684);
}

ENTRY(TouchAhead, touch_ahead)             /* $19DD2 */
ENTRY(DirAnim, dir_anim)                   /* $19E2C */
ENTRY(AtTarget, at_target)                 /* $19E44 */
ENTRY(StepByDir, step_by_dir)              /* $19E64 */
ENTRY(TurnToward, turn_toward)             /* $19E88 */
ENTRY(Lifter, lifter)                      /* $1A1C6 */
ENTRY(SpawnFromA394, spawn_from_a394)      /* $1A438 */
/* batch N5a: two helpers the C called by address. $EC08 (in the player's states): thrown up at
 * speed d0 (the stance bits kept, -$3100 & $75); $19D60/$19D64 (the diver's): its pose ($161E,
 * picture $0A), the timer $218, animation 0 or 2 now ($7FC) */
static void throw_up(void)
{
    uint8_t v = (uint8_t)(rd8(e(-0x3100)) & 0x75);
    wr8(e(-0x3100), v);
    wr16(e(-0x35FE), (uint16_t)c->d[0]);
    c->ccr = (uint16_t)((c->ccr & CC_X) | (c->d[0] & 0x8000 ? CC_N : 0) | ((c->d[0] & 0xFFFF) ? 0 : CC_Z));
}
static void diver_pose(uint32_t anim)
{
    c->d[0] = anim;
    wr8(e(-0x3FFE), (uint8_t)(rd8(e(-0x3FFE)) | 0x80));
    wr16(e(-0x3D00), 0x161E);
    wr8(e(-0x3CFD), 0x0A);
    wr16(e(-0x2BFE), 0x218);
    call(0x7FC);
}
static void diver_pose0(void) { diver_pose(0); }
static void diver_pose2(void) { diver_pose(2); }
ENTRY(ThrowUp, throw_up)                   /* $EC08 */
ENTRY(DiverPose0, diver_pose0)             /* $19D60 */
ENTRY(DiverPose2, diver_pose2)             /* $19D64 */

/* ---- batch T52: the ending's helpers ----
 * The ending (its task at $1AA3C, which waits, stays the original's until a session reaches it)
 * scrolls a starfield ($A39C: the way), its pictures, the story's text up plane B ($A3BA, $A3AA)
 * and the credits' lines on plane A ($A3B6, $A39D-$A3A0). */

/* $1AD62: the stars (slots 0-$EC) moved down or up ($AD6), wrapping at the screen's edge */
static void ending_stars(void)
{
    for (c->a[4] = 0; c->a[4] != 0xF0; c->a[4] += 4) {
        if (rd8(0xFFFFA39Cu) & 0x80) { if (rd16(e(-0x3700)) == 0xE0) wr16(e(-0x3700), 0xFFF0); }
        else if (rd16(e(-0x3700)) == 0xFFF0) wr16(e(-0x3700), 0x110);
        call(0xAD6);
    }
}
/* $1AD98: slot d0 made from the five words at a0 (x, y, look, speeds), palette d1, held ($4),
 * animation d2, drawn; its graphics streamed ($22F0) */
static void ending_sprite(void)
{
    c->a[4] = sx(c->d[0]);
    call(0xA36);
    static const int16_t f[] = { -0x3800, -0x3700, -0x3D00, -0x3600, -0x35FE };
    for (int k = 0; k < 5; k++) { wr16(e(f[k]), rd16(c->a[0])); c->a[0] += 2; }
    put8(e(-0x3FFE), 0x80);
    put8(e(-0x3CFD), c->d[1]);
    put16(e(-0x3E00), 4);
    c->d[0] = set_b(c->d[0], c->d[2]);
    call(SET_ANIM);
    put8(e(-0x4000), 0xC0);
    call(0x22F0);
}
/* $1AE48 and $1AE4E: the random by $FF or by 6 */
static void end_rand_255(void) { c->d[4] = set_w(c->d[4], 0xFF); call(0x5D8); c->d[0] = set_w(c->d[0], c->d[0] & c->d[4]); F_W(c->d[0]); }
static void end_rand_6(void) { c->d[4] = 6; call(0x5D8); c->d[0] = set_w(c->d[0], c->d[0] & c->d[4]); F_W(c->d[0]); }
/* $1ADD2: 60 stars at random places (a fixed seed), palettes $A-$1E, speeds by kind and way */
static void ending_stars_setup(void)
{
    wr32(0xFFFF8A52u, 0xA082E6C8u);
    c->d[2] = 0xA;
    c->d[0] = 0;
    for (c->a[4] = 0; c->a[4] < 0xF0; c->a[4] += 4) {
        call(0xA36);
        end_rand_255(); put16(e(-0x3800), c->d[0]);
        end_rand_255(); put16(e(-0x3700), c->d[0]);
        put16(e(-0x3D00), 0x162A);
        put8(e(-0x3CFD), c->d[2]);
        put16(e(-0x3E00), 4);
        end_rand_6();
        if ((uint16_t)c->d[0] >= 6) c->d[0] = 4;
        wr16(e(-0x35FE), rd16(((rd8(0xFFFFA39Cu) & 0x80) ? 0x1AE42 : 0x1AE3C) + (c->d[0] & 0xFFFF)));
        call(SET_ANIM);
        put8(e(-0x4000), 0xC0);
        c->d[2] = set_w(c->d[2], c->d[2] + 2);
        if ((uint16_t)c->d[2] >= 0x20) c->d[2] = 0xA;
    }
}
/* $1AFF6 and its entries: the VDP to write at d0 (a cell of a row of plane A, B or the window) */
static void vdp_at(void) { c->d[0] = c->d[0] << 16 | c->d[0] >> 16; c->d[0] = set_w(c->d[0], 3); wr32(0xC00004, c->d[0]); }
static void vdp_row_window(void) { c->d[0] = set_w(c->d[0], ((c->d[0] & 0x1F) << 7) | 0x6000); vdp_at(); }   /* $1AFDC */
static void vdp_row_at(void) { c->d[0] = set_w(c->d[0], (((c->d[0] & 0x1F) << 7) + c->d[1]) | 0x4000); vdp_at(); }   /* $1AFEA */
static void vdp_row_b(void) { c->d[1] = 0xC; vdp_row_at(); }                                                 /* $1AFE8 */
/* $1AE58: one row (64 cells) of the story's text from $A3BA: (kind, count, ...) runs: 0 blanks,
 * 2 a tile and its successors, 9 empty, else a tile once more; $FF the end ($A3A2) */
static void ending_text_row(void)
{
    c->a[0] = rd32(0xFFFFA3BAu);
    c->a[1] = 0xC00000;
    c->d[0] = 0;
    for (;;) {
        c->d[1] = set_b(c->d[1], rd8(c->a[0])); c->a[0]++;
        if ((c->d[1] & 0xFF) == 0xFF) { put8(0xFFFFA3A2u, 1); break; }
        c->d[2] = rd8(c->a[0]); c->a[0]++;
        c->d[1] = set_b(c->d[1], c->d[1] & 0xF);
        uint8_t k = (uint8_t)c->d[1];
        if (!k) c->d[3] = set_w(c->d[3], 0x8020);
        else if (k == 2) {
            c->d[3] = set_b(c->d[3], rd8(c->a[0])); c->a[0]++;
            c->d[3] = set_w(c->d[3], (uint16_t)(c->d[3] << 8));
            c->d[3] = set_b(c->d[3], rd8(c->a[0])); c->a[0]++;
        } else if (k == 9) c->d[3] = 0;
        else {
            c->d[3] = set_b(c->d[3], c->d[2]);
            c->d[3] = set_w(c->d[3], (uint16_t)(c->d[3] << 8));
            c->d[3] = set_b(c->d[3], rd8(c->a[0])); c->a[0]++;
            c->d[2] = 0;
        }
        do {
            uint16_t t = (uint16_t)c->d[3];
            wr16(0xC00000, t);
            if (t && t != 0x8020) c->d[3] = set_w(c->d[3], t + 1);
            c->d[0] = set_w(c->d[0], c->d[0] + 1);
            c->d[2] = set_w(c->d[2], c->d[2] - 1);
        } while ((c->d[2] & 0xFFFF) != 0xFFFF);
        if ((int8_t)(uint8_t)((uint8_t)c->d[0] - 0x40) >= 0) break;
    }
    wr32(0xFFFFA3BAu, c->a[0]);
}
/* $1AEC2 (down) and $1AEC6 (up): every 8 lines ($A3AB) the text's row ($A39E, 0-$1F) one on;
 * d7 -1 when it moved */
static void text_row_step(int8_t d)
{
    c->d[1] = (uint32_t)(int32_t)d;
    c->d[7] = 0;
    c->d[0] = 0xF & rd8(0xFFFFA3ABu);
    if ((c->d[0] & 0xFF) && (c->d[0] & 0xFF) != 8) { c->d[0] = set_b(c->d[0], c->d[0] - 8); return; }
    c->d[0] = 0;
    c->d[2] = set_b(c->d[2], 0x20);
    uint8_t r = (uint8_t)(rd8(0xFFFFA39Eu) + (uint8_t)d);
    wr8(0xFFFFA39Eu, r);
    if (r & 0x80) wr8(0xFFFFA39Eu, 0x1F);
    else if (!(0x20 > r)) wr8(0xFFFFA39Eu, 0);
    c->d[7] = 0xFFFFFFFFu;
}
static void text_down(void) { text_row_step(1); }
static void text_up(void) { text_row_step(-1); }
/* $1AEF6: past line $F80, the next row of text at its row of the window */
static void ending_text_next(void)
{
    if (rd16(0xFFFFA3AAu) < 0xF80) return;
    c->d[0] = set_b(c->d[0], rd8(0xFFFFA39Eu));
    vdp_row_window();
    ending_text_row();
}
/* $1B004: $95E8 the length of the string at a1 */
static void str_len(void)
{
    c->a[2] = c->a[1];
    wr16(0xFFFF95E8u, 0);
    while (rd8(c->a[2]++)) wr16(0xFFFF95E8u, (uint16_t)(rd16(0xFFFF95E8u) + 1));
}
/* $1AF5A: the credits' next line at its row: centred ($875C), white or (a heading) yellow
 * ($7874), then its pause: a byte with bit 7 its frames, else 2 (3 after a heading); $99 the end */
static void ending_credit_line(void)
{
    c->d[0] = set_b(c->d[0], rd8(0xFFFFA39Du));
    vdp_row_b();
    c->a[1] = rd32(0xFFFFA3B6u);
    str_len();
    c->d[0] = 0x14;
    c->d[0] = set_w(c->d[0], c->d[0] - rd16(0xFFFF95E8u));
    uint32_t x = c->d[0] & 1;
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 1);
    c->d[5] = (uint16_t)(c->d[0] + x);
    call(0x875C);
    c->d[1] = set_w(c->d[1], rd8(0xFFFFA3A0u) ? 0xE000 : 0x8000);
    call(0x7874);
    if (c->d[0] & 0xFFFF) { c->d[5] = set_w(c->d[5], c->d[0]); call(0x875C); }
    if (rd8(c->a[1]) == 0x99) { put8(0xFFFFA3A0u, 0xFF); return; }
    c->d[0] = set_b(c->d[0], rd8(c->a[1])); c->a[1]++;
    if (c->d[0] & 0x80) {
        c->d[0] = set_b(c->d[0], c->d[0] & 0x1F);
        put8(0xFFFFA39Fu, c->d[0]);
        put8(0xFFFFA3A0u, 1);
    } else {
        put8(0xFFFFA39Fu, 2);
        if (rd8(0xFFFFA3A0u)) { put8(0xFFFFA3A0u, 0); put8(0xFFFFA39Fu, 3); }
        c->a[1] -= 1;
    }
    wr32(0xFFFFA3B6u, c->a[1]);
}
/* $1AF0C: every 8 lines ($A3A9) the credits' row ($A39D) on, and when its pause is over the next
 * line; the row after it cleared ($875C, 20 blanks) */
static void ending_credits_step(void)
{
    c->d[0] = set_b(c->d[0], rd8(0xFFFFA3A9u) & 0xF);
    if ((c->d[0] & 0xFF) && (c->d[0] & 0xFF) != 8) return;
    wr8(0xFFFFA39Fu, (uint8_t)(rd8(0xFFFFA39Fu) - 1));
    wr8(0xFFFFA39Du, (uint8_t)(rd8(0xFFFFA39Du) + 1));
    c->d[2] = 0x20;
    if (!(0x20 > rd8(0xFFFFA39Du))) wr8(0xFFFFA39Du, 0);
    if (!(rd8(0xFFFFA3A0u) & 0x80) && !rd8(0xFFFFA39Fu)) {
        c->a[7] -= 4; wr32(c->a[7], 0x1AF40);
        ending_credit_line();
        c->a[7] += 4;
    }
    c->d[0] = set_b(c->d[0], rd8(0xFFFFA39Du) + 1);
    if ((uint8_t)c->d[0] >= 0x20) c->d[0] = 0;
    vdp_row_b();
    c->d[5] = 0x14;
    call(0x875C);
}

ENTRY(EndingStars, ending_stars)           /* $1AD62 */
ENTRY(EndingSprite, ending_sprite)         /* $1AD98 */
ENTRY(EndingStarsSetup, ending_stars_setup)   /* $1ADD2 */
ENTRY(EndRand255, end_rand_255)            /* $1AE48 */
ENTRY(EndRand6, end_rand_6)                /* $1AE4E */
ENTRY(EndingTextRow, ending_text_row)      /* $1AE58 */
ENTRY(TextDown, text_down)                 /* $1AEC2 */
ENTRY(TextUp, text_up)                     /* $1AEC6 */
ENTRY(EndingTextNext, ending_text_next)    /* $1AEF6 */
ENTRY(EndingCreditsStep, ending_credits_step) /* $1AF0C */
ENTRY(EndingCreditLine, ending_credit_line)   /* $1AF5A */
ENTRY(VdpRowWindow, vdp_row_window)        /* $1AFDC */
ENTRY(VdpRowB, vdp_row_b)                  /* $1AFE8 */
ENTRY(VdpRowAt, vdp_row_at)                /* $1AFEA */
ENTRY(StrLen, str_len)                     /* $1B004 */

/* ---- batch T79: the ending ----
 * One task, the ending scene's ($4F58): the stars and the story's text scrolled up with the
 * credits ($1AA3C), then a script ($1B016, its commands at $1B076 from $1B0A2: scenes of the
 * friends met, each a room, its people and a message, a fade), then the finale ($1B332): the
 * last words, the Sega logo's colours, the end, and on to $4F66. Its waits that keep their
 * caller's return address in $A3BE ($1B250, $1B30A, $1B804, $1B904, $1B91C, $1B950) are given the
 * original's, as rooms.c does for the continuations' stack.
 *
 * $1B2FE yields from a subroutine: the original's Yield ($400) then returns to its caller in the
 * same pass, so it only marks the task's resume address ($1B302), which the next real wait
 * overwrites. Here too. */

#define END_COUNT    0xFFFFA3ACu    /* word: frames to wait */
#define END_ACTOR    0xFFFFA3B0u    /* word: the entity a wait moves */
#define END_SCRIPT   0xFFFFA3B2u    /* long: the script's place */
#define END_ROWS     0xFFFFA3B6u    /* long: the logo's rows */
#define END_TEXT     0xFFFFA3BAu    /* long: the story's text */
#define END_RET      0xFFFFA3BEu    /* long: a wait's return address */

void call_68k_ret(Cpu *t, uint32_t addr, uint32_t ret);

/* the task waits for the next frame, the original's Yield at its return address */
static void end_wait(uint32_t resume) { Cpu *me = c; task_wait_frame(me, resume); c = me; }
/* $1B2FE's Yield from a subroutine: the resume address marked, back to the caller at once */
static void end_mark(uint32_t resume)
{
    c->a[5] = sx(rd16(0xFFFF8A4Cu));
    c->a[0] = resume;
    wr32(c->a[5] + 0xC, resume);
}
static void sprite_at(uint32_t rec, uint32_t d0w, uint32_t d1, uint32_t d2)   /* lea, move.w, moveq, moveq; bsr $1AD98 */
{
    c->a[0] = rec; c->d[0] = set_w(c->d[0], d0w); c->d[1] = d1; c->d[2] = d2;
    ending_sprite();
}

/* $1B91C: $A3AC frames; $1B904 the same plus one, with the stars; $1B950 plus one, the entity
 * $A3B0 moved each frame. ret: where the original goes on ($A3BE) */
static void end_wait_count_at(uint32_t ret, uint32_t at)
{
    if (at) goto resumed;
    wr32(END_RET, ret);
    do { end_wait(0x1B924); resumed: ; } while (wr16(END_COUNT, (uint16_t)(rd16(END_COUNT) - 1)), rd16(END_COUNT));
    c->a[0] = rd32(END_RET);
}
static void end_wait_stars_at(uint32_t ret, uint32_t at)
{
    if (at) goto resumed;
    wr32(END_RET, ret);
    for (;;) {
        end_wait(0x1B90C);
resumed:
        ending_stars();
        uint16_t n = rd16(END_COUNT); wr16(END_COUNT, (uint16_t)(n - 1));
        if (!n) break;                                  /* bcc: until it passes 0 */
    }
    c->a[0] = rd32(END_RET);
}
static void end_wait_move_at(uint32_t ret, uint32_t at)
{
    if (at) goto resumed;
    wr32(END_RET, ret);
    for (;;) {
        end_wait(0x1B958);
resumed:
        c->a[4] = sx(rd16(END_ACTOR));
        call(0xAB4);
        uint16_t n = rd16(END_COUNT); wr16(END_COUNT, (uint16_t)(n - 1));
        if (!n) break;
    }
    c->a[0] = rd32(END_RET);
}
/* $1B804: a fade out, waited for */
static void end_fade_at(uint32_t ret, uint32_t at)
{
    if (at) goto resumed;
    wr32(END_RET, ret);
    call(0x5842);
    do { end_wait(0x1B812); resumed: ; } while (!(rd8(0xFFFF8C56u) & 0x10));
    c->a[0] = rd32(END_RET);
}
/* $1B30A: the scene's message ($7A + $A3A6, its window by $2002), run to its end */
static void end_message_at(uint32_t ret, uint32_t at)
{
    if (at == 0x1B322) goto r1B322;
    if (at == 0x1B326) goto r1B326;
    wr32(END_RET, ret);
    c->d[0] = set_w(c->d[0], rd16(0xFFFFA3A6u) + 0x7A);
    call(0x2002);
    wr16(0xFFFFA3A6u, (uint16_t)(rd16(0xFFFFA3A6u) + 1));
    do {
        end_wait(0x1B322);
r1B322:
        call_68k_ret(c, 0x13BC, 0x1B326);
r1B326: ;
    } while (rd8(0xFFFF8CA4u) & 0x80);
    c->a[0] = rd32(END_RET);
}
/* $1B2FE: see above; its count ($A3AC) is not reached */
static void end_mark_wait(void) { end_mark(0x1B302); }
/* $1B302, a pass resumed there (no later wait marked another address): the count down, a pass
 * each, then rts to the scheduler as well */
static void end_mark_resumed(void)
{
    wr16(END_COUNT, (uint16_t)(rd16(END_COUNT) - 1));
    if (rd16(END_COUNT)) end_mark(0x1B302);
}
/* $1B250: the last scene's figure ($5C0's graphics in entity 4) walks in from the top for 11
 * frames, then turns ($16) */
static void end_walk_in_at(uint32_t ret, uint32_t at)
{
    if (at == 0x1B2C4) goto r1B2C4;
    wr32(END_RET, ret);
    c->d[0] = set_w(c->d[0], 0xB80); c->d[2] = set_w(c->d[2], 0x20); c->d[1] = set_w(c->d[1], 0xC1C);
    call(0x22A8);
    wr16(0xFFFF8E64u, 0x50);
    call(0x22F0);
    c->a[4] = 4;
    call(0xA36);
    wr8(c->a[4] - 0x3FFD, 0);
    wr16(c->a[4] - 0x3800, 0x48);
    wr16(c->a[4] - 0x3700, 0xFF00);
    wr16(c->a[4] - 0x3DFE, 0x5C0);
    wr16(c->a[4] - 0x3D00, 0);
    wr8(c->a[4] - 0x3CFD, 2);
    wr16(c->a[4] - 0x3E00, 4);
    wr32(c->a[4] - 0x3600, 0x2000);
    c->d[0] = 0x1C; call(0x7E8);
    wr8(c->a[4] - 0x4000, 0xC0);
    wr16(END_COUNT, 0xB);
    wr16(END_ACTOR, 4);
    do {
        end_wait(0x1B2C4);
r1B2C4:
        c->a[4] = sx(rd16(END_ACTOR));
        call(0xAD6);
        wr16(END_COUNT, (uint16_t)(rd16(END_COUNT) - 1));
    } while (rd16(END_COUNT));
    call(0x7F4);
    wr16(END_COUNT, 3);
    c->a[7] -= 4; wr32(c->a[7], 0x1B2DE); end_mark_wait(); c->a[7] += 4;
    c->a[4] = 4;
    wr16(c->a[4] - 0x3DFE, 0);
    c->d[0] = 0x16; call(0x7E8);
    c->a[7] -= 4; wr32(c->a[7], 0x1B2EE); end_mark_wait(); c->a[7] += 4;
    c->a[4] = 4;
    c->d[0] = 0; call(0x7FC);
    c->a[0] = rd32(END_RET);
}
static void end_wait_count(uint32_t ret) { end_wait_count_at(ret, 0); }
static void end_wait_stars(uint32_t ret) { end_wait_stars_at(ret, 0); }
static void end_wait_move(uint32_t ret) { end_wait_move_at(ret, 0); }
static void end_fade(uint32_t ret) { end_fade_at(ret, 0); }
static void end_message(uint32_t ret) { end_message_at(ret, 0); }
static void end_walk_in(uint32_t ret) { end_walk_in_at(ret, 0); }

/* $1B778: the screen cleared ($5188), the ending's graphics slots ($1B796) and palettes */
static void end_screen(void)
{
    call(0x5188);
    c->a[0] = 0x1B796; call(0x234C);
    call(0x22F0);
    c->a[0] = 0x1B792; call(0x135C);
}
/* $1B7A4: the scene's room ($96D6, by $A3A6) */
static void end_scene(void) { c->d[0] = set_w(c->d[0], rd16(0xFFFFA3A6u)); call(0x96D6); }
/* $1B7AE, $1B7B4: plane A's or B's map cleared and drawn from the script's word and four bytes */
static void end_layer(uint32_t layer)
{
    c->a[3] = layer;
    c->a[0] = rd32(END_SCRIPT);
    c->d[3] = set_w(c->d[3], rd16(c->a[0])); c->a[0] += 2;
    c->d[0] = set_w(c->d[0], 0x3FF);
    c->d[1] = 0x40000003u;
    c->a[1] = 0xC00000;
    if (c->d[3] & 0x2000) c->d[1] = 0x60000003u;
    wr32(0xC00004, c->d[1]);
    do { wr32(0xC00000, 0x80208020u); c->d[0] = set_w(c->d[0], c->d[0] - 1); } while ((c->d[0] & 0xFFFF) != 0xFFFF);
    c->d[4] = rd8(c->a[0]); c->a[0]++;
    c->d[5] = rd8(c->a[0]); c->a[0]++;
    c->d[6] = rd8(c->a[0]); c->a[0]++;
    c->d[7] = rd8(c->a[0]); c->a[0]++;
    wr32(END_SCRIPT, c->a[0]);
    call(0xA2EE);
}
static void end_layer_a(void) { end_layer(0xFFFF9754u); }
static void end_layer_b(void) { end_layer(0xFFFF97D4u); }
/* $1B820: graphics slots by the scene ($1B830, by $A3AE) */
static void end_gfx(void)
{
    c->a[0] = 0x1B830;
    c->d[0] = set_w(c->d[0], rd16(0xFFFFA3AEu));
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    call(0x234C);
}
/* $1B8CE: a coordinate: the script's byte * 2 - $80 */
static void end_coord(void)
{
    c->d[0] = rd8(c->a[0]); c->a[0]++;
    c->d[0] = set_w(c->d[0], (c->d[0] << 1) - 0x80);
}
/* $1B882: a person (entity d1) placed from the script: its kind, place, looks and animation */
static void end_actor(void)
{
    c->a[0] = rd32(END_SCRIPT);
    c->a[4] = sx(c->d[1]);
    call(0xA36);
    wr8(c->a[4] - 0x3FFE, rd8(c->a[0])); c->a[0]++;
    end_coord(); wr16(c->a[4] - 0x3800, (uint16_t)c->d[0]);
    end_coord(); wr16(c->a[4] - 0x3700, (uint16_t)c->d[0]);
    wr8(c->a[4] - 0x3D00, rd8(c->a[0])); c->a[0]++;
    wr8(c->a[4] - 0x3CFF, rd8(c->a[0])); c->a[0]++;
    wr8(c->a[4] - 0x3CFD, rd8(c->a[0])); c->a[0]++;
    wr16(c->a[4] - 0x3E00, 4);
    c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
    c->d[1] = 0;
    wr16(c->a[4] - 0x39FE, 0);
    c->d[1] = rd8(c->a[0]); c->a[0]++;
    c->d[1] = set_w(c->d[1], c->d[1] << 4);
    wr16(c->a[4] - 0x3DFE, (uint16_t)c->d[1]);
    wr32(END_SCRIPT, c->a[0]);
    call(0x7E8);
    wr8(c->a[4] - 0x4000, 0xC0);
}
/* $1B8DA: two more rows of the logo ($A3B6) at row $A3A3 of the window, columns 14 and 15 */
static void end_rows(void)
{
    c->d[2] = 1;
    c->a[0] = rd32(END_ROWS);
    c->d[1] = 0;
    c->d[0] = 0xE;
    c->d[1] = set_b(c->d[1], rd8(0xFFFFA3A3u));
    do {
        vdp_row_at();
        wr16(0xC00000, rd16(c->a[0])); c->a[0] += 2;
        c->d[0] = 0xE;
        c->d[0] = set_w(c->d[0], c->d[0] + 1);
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
    } while ((c->d[2] & 0xFFFF) != 0xFFFF);
    wr8(0xFFFFA3A3u, (uint8_t)(rd8(0xFFFFA3A3u) + 2));
    wr32(END_ROWS, c->a[0]);
}
/* $1B5CC: the figure in entity $F0 from the record a0, its frame d2 */
static void end_sprite_f0(void)
{
    wr8(0xFFFFC0F0u, 0);
    c->d[0] = set_w(c->d[0], 0xF0);
    c->d[1] = 0;
    ending_sprite();
}

static void end_finale(uint32_t at);
/* $1B016: the script: a word under $8000 waits so many frames, a command byte and its byte
 * else ($FF: the finale) */
static void end_script(uint32_t at)
{
    if (at == 0x1B074) goto next_command;
    if (at == 0x1B046) goto counting;
    c->d[0] = 0x16; call(0x366);
    wr16(0xFFFFA3A6u, 0);
    wr16(0xFFFFA3AEu, 0);
    wr32(END_SCRIPT, 0x1B0A2);
    for (;;) {
next_command:                                           /* $1B02C ($1B074: bra there) */
        c->a[0] = rd32(END_SCRIPT);
        c->d[0] = rd8(c->a[0]); c->a[0]++;
        if (!(c->d[0] & 0x80)) {
            c->d[0] = set_w(c->d[0], c->d[0] << 8);
            c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
            wr16(END_COUNT, (uint16_t)c->d[0]);
            wr32(END_SCRIPT, c->a[0]);
            do { end_wait(0x1B046); counting: ; } while (wr16(END_COUNT, (uint16_t)(rd16(END_COUNT) - 1)), rd16(END_COUNT));
            continue;
        }
        if ((c->d[0] & 0xFF) == 0xFF) { end_finale(0); return; }
        c->d[0] = set_b(c->d[0], c->d[0] & 0xF);
        if ((c->d[0] & 0xFF) == 8) wr16(0xFFFFA3AEu, (uint16_t)(rd16(0xFFFFA3AEu) + 2));
        c->d[1] = rd8(c->a[0]); c->a[0]++;
        wr32(END_SCRIPT, c->a[0]);
        c->d[0] = set_w(c->d[0], c->d[0] << 2);
        c->a[1] = rd32(0x1B076 + (c->d[0] & 0xFFFF));
        switch (c->a[1]) {                              /* jsr (a1): back at $1B074 */
        case 0x1B7A4: end_scene(); break;
        case 0x1B7AE: end_layer_a(); break;
        case 0x1B7B4: end_layer_b(); break;
        case 0x1B804: end_fade(0x1B074); break;
        case 0x1B882: end_actor(); break;
        case 0x1B820: end_gfx(); break;
        case 0x1B30A: end_message(0x1B074); break;
        case 0x1B250: end_walk_in(0x1B074); break;
        default: call(c->a[1]); break;
        }
    }
}

/* $1B332: the finale: the screen again, the story's last text scrolled in and out, the Sega
 * logo's rows and colours, the figure's last poses, then the end ($A3A4 cleared) and $4F66 */
static void end_finale(uint32_t at)
{
    switch (at) {
    case 0x1B366: goto r1B366; case 0x1B374: goto r1B374; case 0x1B3B8: goto r1B3B8; case 0x1B3DA: goto r1B3DA;
    case 0x1B424: case 0x1B542: goto moves;
    case 0x1B478: goto r1B478; case 0x1B4C4: goto r1B4C4; case 0x1B50E: goto r1B50E; case 0x1B5AA: goto r1B5AA;
    case 0x1B5B8: goto r1B5B8;
    }
    end_screen();
    wr8(0xFFFFA39Cu, 0xFF);
    ending_stars_setup();
    c->a[0] = 0x1B96C; wr32(END_TEXT, c->a[0]);
    c->d[0] = 0xC; vdp_row_window();
    c->d[4] = 0xF;
    do { ending_text_row(); c->d[4] = set_w(c->d[4], c->d[4] - 1); } while ((c->d[4] & 0xFFFF) != 0xFFFF);
    call(0x581A);
    wr16(END_COUNT, 0xA4);
    end_wait_count(0x1B366);
r1B366:
    wr16(0xFFFFA3AAu, 0);
    wr8(0xFFFFA39Eu, 0x1E);
    for (;;) {
        end_wait(0x1B374);
r1B374:
        ending_stars();
        wr16(0xFFFFA3AAu, (uint16_t)(rd16(0xFFFFA3AAu) - 1));
        if (rd16(0xFFFFA3AAu) == 0xFF30) break;
        text_up();
        if (!(c->d[7] & 0x8000)) {
            c->d[0] = set_b(c->d[0], rd8(0xFFFFA39Eu));
            vdp_row_window();
            c->d[5] = 0x1F; call(0x875C);
        }
        wr8(0xFFFF8006u, rd8(0xFFFF8006u) | 8);
        wr16(0xFFFF8AB4u, rd16(0xFFFFA3AAu));
    }
    wr16(END_COUNT, 0x12C);
    end_wait_stars(0x1B3B8);
r1B3B8:
    c->a[0] = 0x1B61A; wr32(END_ROWS, c->a[0]);
    wr8(0xFFFFA3A3u, 0);
    do end_rows(); while (rd8(0xFFFFA3A3u) < 0x40);
    wr16(END_COUNT, 0x168);
    end_wait_count(0x1B3DA);
r1B3DA:
    wr8(0xFFFFA3A3u, 0);
    c->a[0] = 0x1B75A; c->d[2] = 0; end_sprite_f0();
    c->d[0] = 0x14; call(0x5658);
    wr8(0xFFFFC0F0u, rd8(0xFFFFC0F0u) | 0x20);
    wr32(END_SCRIPT, 0x1B5DC);
    for (;;) {                                          /* $1B3FE: the figure's moves */
moves:                                                  /* ($1B424, $1B542: bra there) */
        c->a[4] = 0xF0;
        c->a[0] = rd32(END_SCRIPT);
        c->d[0] = set_w(c->d[0], rd16(c->a[0])); c->a[0] += 2;
        if (!(c->d[0] & 0x8000)) {                      /* a move: frames, then its velocity */
            wr16(END_COUNT, (uint16_t)c->d[0]);
            wr16(c->a[4] - 0x3600, rd16(c->a[0])); c->a[0] += 2;
            wr16(c->a[4] - 0x35FE, rd16(c->a[0])); c->a[0] += 2;
            wr32(END_SCRIPT, c->a[0]);
            wr16(END_ACTOR, 0xF0);
            end_wait_move(0x1B424);
            continue;
        }
        c->d[6] = 0;
        c->d[6] = set_b(c->d[6], c->d[0]);
        c->d[0] = set_w(c->d[0], (c->d[0] & 0xFFFF) >> 8);
        c->d[0] = set_w(c->d[0], c->d[0] & 0xF);
        uint16_t op = (uint16_t)c->d[0];
        if (op == 0) {                                  /* $1B4A4: a pose; $FF: the logo's colours */
            wr32(END_SCRIPT, c->a[0]);
            c->a[0] = 0x1B764; c->d[2] = 4; end_sprite_f0();
            if ((c->d[6] & 0xFF) != 0xFF) { wr16(END_COUNT, (uint16_t)c->d[6]); end_wait_count(0x1B542); continue; }
            wr16(END_COUNT, 7);
            end_wait_count(0x1B4C4);
r1B4C4:
            wr8(0xFFFFC0F0u, rd8(0xFFFFC0F0u) | 0x20);
            c->a[0] = 0x1B71A;
            c->d[1] = set_w(c->d[1], 0x16);
            c->d[0] = 2;
            wr8(0xFFFFA3A3u, 2);
            c->d[2] = 3;
            do {
                vdp_row_at();
                c->d[5] = 7;
                do { wr16(0xC00000, rd16(c->a[0])); c->a[0] += 2; c->d[5] = set_w(c->d[5], c->d[5] - 1); } while ((c->d[5] & 0xFFFF) != 0xFFFF);
                wr8(0xFFFFA3A3u, (uint8_t)(rd8(0xFFFFA3A3u) + 1));
                c->d[0] = rd8(0xFFFFA3A3u);
                c->d[2] = set_w(c->d[2], c->d[2] - 1);
            } while ((c->d[2] & 0xFFFF) != 0xFFFF);
            wr8(0xFFFFA39Du, 0x18);
            wr16(END_ACTOR, 3);
            do {
                wr16(END_COUNT, 0x14);
                end_wait_count(0x1B50E);
r1B50E:
                c->d[1] = set_b(c->d[1], rd8(0xFFFFA39Du));
                c->d[0] = 3;
                vdp_row_at();
                wr32(0xC00000, 0x80488041u);
                wr8(0xFFFFA39Du, (uint8_t)(rd8(0xFFFFA39Du) + 4));
                wr16(END_ACTOR, (uint16_t)(rd16(END_ACTOR) - 1));
            } while (rd16(END_ACTOR));
            wr8(0xFFFFC0F0u, rd8(0xFFFFC0F0u) & ~0x20);
            wr16(END_COUNT, 0x29);
            end_wait_count(0x1B542);
            continue;
        }
        if (op == 3) { wr32(END_SCRIPT, c->a[0]); wr8(0xFFFFC0F2u, 0x80); continue; }
        if (op == 5) {                                  /* $1B554: the logo's rows blanked */
            wr32(END_SCRIPT, c->a[0]);
            c->d[1] = set_w(c->d[1], 0x16);
            c->d[0] = 2;
            wr8(0xFFFFA3A3u, 2);
            c->d[2] = 3;
            do {
                vdp_row_at();
                c->d[5] = 7;
                do { wr16(0xC00000, 0x8000); c->d[5] = set_w(c->d[5], c->d[5] - 1); } while ((c->d[5] & 0xFFFF) != 0xFFFF);
                wr8(0xFFFFA3A3u, (uint8_t)(rd8(0xFFFFA3A3u) + 1));
                c->d[0] = rd8(0xFFFFA3A3u);
                c->d[2] = set_w(c->d[2], c->d[2] - 1);
            } while ((c->d[2] & 0xFFFF) != 0xFFFF);
            continue;
        }
        if (op == 6) { wr32(END_SCRIPT, c->a[0]); wr8(0xFFFFC0F0u, rd8(0xFFFFC0F0u) & ~0x20); continue; }
        if (op == 2) {                                  /* $1B596: the last pose, five seconds of stars, the end */
            wr32(END_SCRIPT, c->a[0]);
            c->a[0] = 0x1B76E; c->d[2] = 6; end_sprite_f0();
            wr16(END_COUNT, (uint16_t)c->d[6]);
            end_wait_count(0x1B5AA);
r1B5AA:
            wr8(0xFFFFC0F0u, 0);
            wr16(END_COUNT, 0x12C);
            do {
                end_wait(0x1B5B8);
r1B5B8:
                ending_stars();
                wr16(END_COUNT, (uint16_t)(rd16(END_COUNT) - 1));
            } while (rd16(END_COUNT));
            wr8(0xFFFFA3A4u, 0);
            port_continue_at(c, 0x4F66, 0);
            return;
        }
        wr32(END_SCRIPT, c->a[0]);                      /* else: a walk, its frames and velocity */
        wr8(0xFFFFC0F2u, 0x88);
        if (!(c->d[6] & 0xFFFF)) continue;
        wr16(END_COUNT, rd16(c->a[0])); c->a[0] += 2;
        wr16(c->a[4] - 0x3600, rd16(c->a[0])); c->a[0] += 2;
        wr16(c->a[4] - 0x35FE, rd16(c->a[0])); c->a[0] += 2;
        wr32(END_SCRIPT, c->a[0]);
        wr8(0xFFFFA3A3u, 0);
        for (;;) {                                      /* $1B474: and the logo's rows as it passes */
            end_wait(0x1B478);
r1B478:
            c->a[4] = 0xF0;
            c->d[7] = set_w(c->d[7], rd16(c->a[4] - 0x3800));
            call(0xAB4);
            wr16(END_COUNT, (uint16_t)(rd16(END_COUNT) - 1));
            if (!rd16(END_COUNT)) break;
            if (c->d[7] & 0x8000) continue;
            c->d[7] = set_w(c->d[7], c->d[7] & 7);
            if (c->d[7] & 0xFFFF) continue;
            if (rd16(0xFFFFC8F0u) >= 0xF0) continue;
            end_rows();
        }
    }
}

/* $1AA3C: the ending's task: the screen, the stars, the hero and the princess's figures moved in
 * ($1AACE, $1AAD8), then the story's text and the credits scrolled up together until line $820,
 * the hero walked off, a fade, then the script */
static void end_main(uint32_t at)
{
    switch (at) {
    case 0x1AA78: goto r1AA78; case 0x1AA9C: goto r1AA9C; case 0x1AABE: goto r1AABE; case 0x1AB28: goto r1AB28;
    case 0x1AB8C: goto r1AB8C; case 0x1ABBA: goto r1ABBA; case 0x1ABC4: goto r1ABC4; case 0x1ABCE: goto r1ABCE;
    }
    wr8(0xFFFFA39Cu, 0);
    end_screen();
    ending_stars_setup();
    sprite_at(0x1AACE, 0xF0, 0, 0);
    sprite_at(0x1AAD8, 0xF4, 2, 4);
    call(0x581A);
    wr16(END_COUNT, 0x10);
    end_wait_count(0x1AA78);
r1AA78:
    wr8(0xFFFFC0F4u, 0);
    sprite_at(0x1AAD8, 0xF4, 0, 0);
    wr16(END_COUNT, 0x80);
    wr16(END_ACTOR, 0xF4);
    end_wait_move(0x1AA9C);
r1AA9C:
    wr8(0xFFFFC0F0u, 0);
    wr8(0xFFFFC0F4u, 0);
    sprite_at(0x1AACE, 0xF0, 2, 2);
    wr16(END_COUNT, 0x48);
    end_wait_count(0x1AABE);
r1AABE:
    wr8(0xFFFFC0F0u, 0);
    c->d[0] = 1;
    wr8(0xFFFFA39Fu, 1); wr8(0xFFFFA3A0u, 1);
    wr16(0xFFFFA3A8u, 0); wr16(0xFFFFA3AAu, 0);
    wr8(0xFFFFA3A1u, 0); wr8(0xFFFFA3A2u, 0);
    c->d[0] = 0x1D;
    wr8(0xFFFFA39Du, 0x1D); wr8(0xFFFFA39Eu, 0x1D);
    c->a[0] = 0x1ABD2; wr32(END_ROWS, c->a[0]);
    c->a[0] = 0x1B96C; wr32(END_TEXT, c->a[0]);
    wr8(0xFFFFA3A4u, 0xFF);
    for (;;) {
        end_wait(0x1AB28);
r1AB28:
        ending_stars();
        uint8_t h = rd8(0xFFFFA3A1u); wr8(0xFFFFA3A1u, (uint8_t)(h + 0x80));
        if (h & 0x80) {                                 /* every other frame: the credits */
            ending_credits_step();
            wr16(0xFFFFA3A8u, (uint16_t)(rd16(0xFFFFA3A8u) + 1));
            wr8(0xFFFF8006u, rd8(0xFFFF8006u) | 8);
            wr16(0xFFFF8A8Cu, rd16(0xFFFFA3A8u));
            if (rd16(0xFFFFA3A8u) == 0x820) break;
        }
        if (rd8(0xFFFFA3A2u)) continue;
        text_down();
        if (c->d[7] & 0xFFFF) ending_text_next();
        wr16(0xFFFFA3AAu, (uint16_t)(rd16(0xFFFFA3AAu) + 1));
        wr8(0xFFFF8006u, rd8(0xFFFF8006u) | 8);
        wr16(0xFFFF8AB4u, rd16(0xFFFFA3AAu));
    }
    sprite_at(0x1AAE2, 0xF0, 0, 2);
    do {
        end_wait(0x1AB8C);
r1AB8C:
        c->a[4] = 0xF0;
        call(0xAD6);
    } while ((int16_t)(rd16(c->a[4] - 0x3700) - 0x68) < 0);
    wr8(0xFFFFC0F0u, 0);
    sprite_at(0x1AAD8, 0xF0, 0, 6);
    wr16(END_COUNT, 0x80);
    end_wait_count(0x1ABBA);
r1ABBA:
    c->d[0] = 0xFFFFFFFAu; call(0x366);
    end_fade(0x1ABC4);
r1ABC4:
    wr16(END_COUNT, 0x80);
    end_wait_count(0x1ABCE);
r1ABCE:
    end_script(0);
}

/* the entries: those that wait run on a task pass only; those that keep a return address take
 * it from the stack and go on there (a0), as the original's jmp (a0) */
#define END_NEEDS_PASS(self) do { if (!task_in_pass()) { port_leave_to_68k(c, (self)); return; } } while (0)
#define END_KEEPS(name, fn, self) void name(Cpu *cpu) { c = cpu; END_NEEDS_PASS(self); uint32_t r = rd32(c->a[7]); c->a[7] += 4; fn(r); port_continue_at(c, c->a[0], 0); }
END_KEEPS(EndWaitCount, end_wait_count, 0x1B91C)  /* $1B91C */
END_KEEPS(EndWaitStars, end_wait_stars, 0x1B904)  /* $1B904 */
END_KEEPS(EndWaitMove, end_wait_move, 0x1B950)    /* $1B950 */
END_KEEPS(EndFade, end_fade, 0x1B804)             /* $1B804 */
END_KEEPS(EndMessage, end_message, 0x1B30A)       /* $1B30A */
END_KEEPS(EndWalkIn, end_walk_in, 0x1B250)        /* $1B250 */
void EndMain(Cpu *cpu)   { c = cpu; END_NEEDS_PASS(0x1AA3C); end_main(0); }      /* $1AA3C: it ends at $4F66 */
void EndScript(Cpu *cpu) { c = cpu; END_NEEDS_PASS(0x1B016); end_script(0); }    /* $1B016 */
void EndFinale(Cpu *cpu) { c = cpu; END_NEEDS_PASS(0x1B332); end_finale(0); }    /* $1B332 */
ENTRY(EndMarkWait, end_mark_wait)          /* $1B2FE */
/* batch N3g: the ending started where it waited or where a wait goes on (a loaded state) */
#define END_CONT_AT(name, fn, at) void name(Cpu *cpu) { c = cpu; END_NEEDS_PASS(at); fn(0, at); port_continue_at(c, c->a[0], 0); }
END_CONT_AT(EndWaitCountAt1B924, end_wait_count_at, 0x1B924)
END_CONT_AT(EndWaitStarsAt1B90C, end_wait_stars_at, 0x1B90C)
END_CONT_AT(EndWaitMoveAt1B958, end_wait_move_at, 0x1B958)
END_CONT_AT(EndFadeAt1B812, end_fade_at, 0x1B812)
END_CONT_AT(EndMessageAt1B322, end_message_at, 0x1B322)
END_CONT_AT(EndMessageAt1B326, end_message_at, 0x1B326)
END_CONT_AT(EndWalkInAt1B2C4, end_walk_in_at, 0x1B2C4)
#define END_AT(name, fn, at) void name(Cpu *cpu) { c = cpu; END_NEEDS_PASS(at); fn(at); }
END_AT(EndScriptAt1B046, end_script, 0x1B046)
END_AT(EndScriptAt1B074, end_script, 0x1B074)
END_AT(EndMainAt1AA78, end_main, 0x1AA78)
END_AT(EndMainAt1AA9C, end_main, 0x1AA9C)
END_AT(EndMainAt1AABE, end_main, 0x1AABE)
END_AT(EndMainAt1AB28, end_main, 0x1AB28)
END_AT(EndMainAt1AB8C, end_main, 0x1AB8C)
END_AT(EndMainAt1ABBA, end_main, 0x1ABBA)
END_AT(EndMainAt1ABC4, end_main, 0x1ABC4)
END_AT(EndMainAt1ABCE, end_main, 0x1ABCE)
END_AT(EndFinaleAt1B366, end_finale, 0x1B366)
END_AT(EndFinaleAt1B374, end_finale, 0x1B374)
END_AT(EndFinaleAt1B3B8, end_finale, 0x1B3B8)
END_AT(EndFinaleAt1B3DA, end_finale, 0x1B3DA)
END_AT(EndFinaleAt1B424, end_finale, 0x1B424)
END_AT(EndFinaleAt1B478, end_finale, 0x1B478)
END_AT(EndFinaleAt1B4C4, end_finale, 0x1B4C4)
END_AT(EndFinaleAt1B50E, end_finale, 0x1B50E)
END_AT(EndFinaleAt1B542, end_finale, 0x1B542)
END_AT(EndFinaleAt1B5AA, end_finale, 0x1B5AA)
END_AT(EndFinaleAt1B5B8, end_finale, 0x1B5B8)
void EndMarkWaitAt1B302(Cpu *cpu) { c = cpu; END_NEEDS_PASS(0x1B302); end_mark_resumed(); c->ret = 0; }
ENTRY(EndScreen, end_screen)               /* $1B778 */
ENTRY(EndScene, end_scene)                 /* $1B7A4 */
ENTRY(EndLayerA, end_layer_a)              /* $1B7AE */
ENTRY(EndLayerB, end_layer_b)              /* $1B7B4 */
ENTRY(EndGfx, end_gfx)                     /* $1B820 */
ENTRY(EndActor, end_actor)                 /* $1B882 */
ENTRY(EndCoord, end_coord)                 /* $1B8CE */
ENTRY(EndRows, end_rows)                   /* $1B8DA */
ENTRY(EndSpriteF0, end_sprite_f0)          /* $1B5CC */
