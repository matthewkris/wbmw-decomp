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
