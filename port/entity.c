/* Entity physics. a4 = entity offset (0-$FC, step 4); fields are 16-bit displacements from it
 * into the arrays at $FFC000-$FFDFFF (see docs/NOTES.md, Entities). */
#include "port.h"

#define F(c, disp) ea((c)->a[4], (disp))

/* One axis of ApplyVelocity: add the 8.8 velocity to the 16.8 position.
 *   move.b vel_frac,d0; add.b d0,pos_frac        X = carry out of the fraction
 *   move.w pos,dP; move.b vel_int,d0; ext.w d0; addx.w dP,d0; move.w d0,pos; move.w dP,prev
 * Out: d0 low word = new position, dP low word = old position, X = carry of the addx,
 * N and Z from the old position, V and C clear. */
static void apply_axis(Cpu *c, int16_t vel, int16_t pos, int16_t prev, int dp)
{
    uint32_t vf = rd8(F(c, vel + 1)), pf = rd8(F(c, pos + 2));
    uint32_t sum = pf + vf;
    wr8(F(c, pos + 2), (uint8_t)sum);
    uint32_t x = sum > 0xFF;
    uint32_t old = rd16(F(c, pos));
    uint32_t vi = (uint32_t)(int32_t)(int8_t)rd8(F(c, vel)) & 0xFFFF;
    uint32_t r = vi + old + x;
    c->d[0] = set_w(set_b(c->d[0], vf), r);
    c->d[dp] = set_w(c->d[dp], old);
    wr16(F(c, pos), (uint16_t)r);
    wr16(F(c, prev), (uint16_t)old);
    c->ccr = cc_move_w((uint16_t)(r > 0xFFFF ? CC_X : 0), old);
}

/* ApplyVelX ($AB8) and ApplyVelY ($AD6) */
void ApplyVelX(Cpu *c) { apply_axis(c, -0x3600, -0x3800, -0x2E00, 2); }
void ApplyVelY(Cpu *c) { apply_axis(c, -0x35FE, -0x3700, -0x2DFE, 3); }

/* ApplyVelocity ($AB4): bsr ApplyVelY; swap d0; then falls into ApplyVelX.
 * Leaves d0 = new Y << 16 | new X. */
void ApplyVelocity(Cpu *c)
{
    stack_residue(c, 0xAB6);
    ApplyVelY(c);
    c->d[0] = c->d[0] << 16 | c->d[0] >> 16;
    ApplyVelX(c);
}

/* FrictionX ($B92) / FrictionY ($BAC): move the velocity toward 0 by d0, stopping at 0.
 *   move.w vel,d1; beq out; bpl pos; add.w d0,d1; bmi store; bra zero
 *   pos: sub.w d0,d1; bpl store; zero: moveq #0,d1; store: move.w d1,vel; out: rts */
static void friction(Cpu *c, int16_t vel)
{
    uint32_t v = rd16(F(c, vel)), d0 = c->d[0] & 0xFFFF, r, x = c->ccr & CC_X;
    c->d[1] = set_w(c->d[1], v);
    if (v == 0) { c->ccr = cc_move_w(c->ccr, 0); return; }
    if (v & 0x8000) { r = v + d0; x = r > 0xFFFF ? CC_X : 0; r &= 0xFFFF; if (!(r & 0x8000)) r = 0x10000; }
    else { r = (v - d0) & 0x1FFFF; x = d0 > v ? CC_X : 0; r &= 0xFFFF; if (r & 0x8000) r = 0x10000; }
    if (r == 0x10000) c->d[1] = 0;                        /* moveq #0,d1 clears all 32 bits */
    else c->d[1] = set_w(c->d[1], r);
    wr16(F(c, vel), (uint16_t)c->d[1]);
    c->ccr = cc_move_w((uint16_t)x, c->d[1]);
}

void FrictionX(Cpu *c) { friction(c, -0x3600); }
void FrictionY(Cpu *c) { friction(c, -0x35FE); }

/* AccelX ($AF4) / AccelY ($B44): add the acceleration to the velocity. If the axis's bit in the
 * physics mode (-$2F00) is clear, simply add. If set, clamp to +-limit: accelerating past the
 * limit stops at it, and an entity already beyond the limit decelerates by half the
 * acceleration's size, toward the limit, never past it.
 *   move.w acc,d0; btst #bit,mode; bne clamp; add.w vel,d0; bra store
 *   clamp: move.w lim,d2; move.w vel,d1; tst.w d0; beq out; bmi neg
 *     cmp.w d2,d1; blt add_le; beq out; neg.w d0; asr.w #1,d0; bra add_ge
 *   neg: neg.w d2; cmp.w d2,d1; bgt add_ge; neg.w d0; asr.w #1,d0
 *   add_le: add.w d1,d0; cmp.w d2,d0; ble store; bra clamp_to
 *   add_ge: add.w d1,d0; cmp.w d2,d0; bge store
 *   clamp_to: move.w d2,d0
 *   store: move.w d0,vel; out: rts */
static void accel(Cpu *c, int16_t acc, int16_t vel, int16_t lim, int bit)
{
    uint16_t cc = c->ccr;
    uint32_t d0 = c->d[0], d1 = c->d[1], d2 = c->d[2];
#define NV ((cc & CC_N ? 1 : 0) ^ (cc & CC_V ? 1 : 0))
    d0 = set_w(d0, rd16(F(c, acc))); cc = tst_w(cc, d0);
    int clamp = rd8(F(c, -0x2F00)) >> bit & 1;
    cc = (uint16_t)((cc & ~CC_Z) | (clamp ? 0 : CC_Z));                   /* btst */
    if (!clamp) {
        d0 = set_w(d0, add_w(&cc, rd16(F(c, vel)), d0));
        goto store;
    }
    d2 = set_w(d2, rd16(F(c, lim))); cc = tst_w(cc, d2);
    d1 = set_w(d1, rd16(F(c, vel))); cc = tst_w(cc, d1);
    cc = tst_w(cc, d0);
    if (!(d0 & 0xFFFF)) goto out;
    if (d0 & 0x8000) {
        d2 = set_w(d2, neg_w(&cc, d2));
        cmp_w(&cc, d2, d1);
        if (!(cc & CC_Z) && !NV) goto add_ge;                             /* bgt */
        d0 = set_w(d0, neg_w(&cc, d0)); d0 = set_w(d0, asr_w1(&cc, d0));
        goto add_le;
    }
    cmp_w(&cc, d2, d1);
    if (NV) goto add_le;                                                  /* blt */
    if (cc & CC_Z) goto out;                                              /* beq */
    d0 = set_w(d0, neg_w(&cc, d0)); d0 = set_w(d0, asr_w1(&cc, d0));
    goto add_ge;
add_le:
    d0 = set_w(d0, add_w(&cc, d1, d0)); cmp_w(&cc, d2, d0);
    if ((cc & CC_Z) || NV) goto store;                                    /* ble */
    goto clamp_to;
add_ge:
    d0 = set_w(d0, add_w(&cc, d1, d0)); cmp_w(&cc, d2, d0);
    if (!NV) goto store;                                                  /* bge */
clamp_to:
    d0 = set_w(d0, d2); cc = tst_w(cc, d2);
store:
    wr16(F(c, vel), (uint16_t)d0); cc = tst_w(cc, d0);
out:
    c->d[0] = d0; c->d[1] = d1; c->d[2] = d2; c->ccr = cc;
#undef NV
}

void AccelX(Cpu *c) { accel(c, -0x3400, -0x3600, -0x3300, 0); }
void AccelY(Cpu *c) { accel(c, -0x33FE, -0x35FE, -0x32FE, 1); }

/* Accel ($B42): bsr AccelX, then falls into AccelY */
void Accel(Cpu *c)
{
    stack_residue(c, 0xB44);
    AccelX(c);
    AccelY(c);
}
