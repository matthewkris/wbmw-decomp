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
