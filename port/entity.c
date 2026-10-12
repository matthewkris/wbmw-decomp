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

/* ClearEntityAnim ($A20): the velocity, the position fractions and the sprite word cleared, the
 * camera and tile base cleared, the animation step $100. Out: d0 = 0; flags from the last move
 * (#$100: neither N nor Z). */
void ClearEntityAnim(Cpu *c)
{
    stack_residue(c, 0xA24);                        /* jsr $C1C */
    wr32(F(c, -0x3600), 0);
    wr16(F(c, -0x37FE), 0);
    wr16(F(c, -0x36FE), 0);
    wr16(F(c, -0x3FFE), 0);
    wr32(F(c, -0x3E00), 0);
    wr16(F(c, -0x39FE), 0x100);
    c->d[0] = 0;
    c->ccr = cc_move_w(c->ccr, 0x100);
}

/* ClearEntity ($A36): ClearEntityAnim, then the entity's state cleared (flags, collision,
 * attack, hit boxes, timers). Out: d0 = 0, Z set. */
void ClearEntity(Cpu *c)
{
    stack_residue(c, 0xA38);
    c->a[7] -= 4; ClearEntityAnim(c); c->a[7] += 4;
    static const int16_t words[] = {-0x3FFE, -0x2C00, -0x21FE};
    static const int16_t longs[] = {-0x3200, -0x3100, -0x3000, -0x2F00, -0x2B00, -0x2A00, -0x2900, -0x2800};
    for (int i = 0; i < 3; i++) wr16(F(c, words[i]), 0);
    for (int i = 0; i < 8; i++) wr32(F(c, longs[i]), 0);
    c->ccr = cc_move_l(c->ccr, 0);
}

/* FreezeEntities ($A66) / UnfreezeEntities ($A8E): every entity in use (bit 7) stopped (bit 5
 * set; bit 4 remembers whether it was already) or let go again. a4 ends at $100, d1.w at $FFFF;
 * d0 and the flags are the last byte handled. */
static void freeze_all(Cpu *c, int freeze)
{
    uint32_t d0 = c->d[0];
    uint16_t cc = c->ccr;
    for (uint32_t e = 0; e < 0x100; e += 4) {
        uint32_t a = ea(e, -0x4000);
        uint8_t f = rd8(a);
        d0 = set_b(d0, f);
        cc = tst_b(cc, f);
        if (!(f & 0x80)) continue;
        if (freeze) f = (f & 0x20) ? (uint8_t)(f | 0x30) : (uint8_t)((f | 0x20) & ~0x10);
        else f = (f & 0x10) ? (uint8_t)((f & ~0x30) | 0x20) : (uint8_t)(f & ~0x30);
        d0 = set_b(d0, f);
        wr8(a, f);
        cc = (uint16_t)((cc & CC_X) | (f & 0x80 ? CC_N : 0) | (f ? 0 : CC_Z));
    }
    c->d[0] = d0;
    c->d[1] = 0x0000FFFFu;
    c->a[4] = 0x100;
    c->ccr = cc;
}
void FreezeEntities(Cpu *c)   { freeze_all(c, 1); }
void UnfreezeEntities(Cpu *c) { freeze_all(c, 0); }

/* FindFreeEffect ($27E6): a4 = the first of the slots $E0-$FC not in use (N clear), or the
 * last one with N set; d0.w = how many slots were left after it ($FFFF: none). */
void FindFreeEffect(Cpu *c)
{
    uint32_t e = 0xE0, n = 7;
    for (;;) {
        uint8_t f = rd8(ea(e, -0x4000));
        c->ccr = tst_b(c->ccr, f);
        c->ccr &= (uint16_t)~(CC_V | CC_C);
        if (!(f & 0x80)) break;
        n = (n - 1) & 0xFFFF;
        if (n == 0xFFFF) break;
        e += 4;
    }
    c->d[0] = n;                                    /* moveq #7 cleared the upper word */
    c->a[4] = e;
}

/* CheckGrounded ($4362): Z set if on the ground (collision bit 1 without bit 3); d0 =
 * (collision & $A) - 2, as a byte */
void CheckGrounded(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[0] = sub_b(&cc, 2, rd8(F(c, -0x3100)) & 0xA);
    c->ccr = cc;
}
