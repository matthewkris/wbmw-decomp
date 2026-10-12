/* The angle maths ($EE2-$1278): sines, velocities at an angle, the direction from one entity
 * to another, a distance. Everything that calls these reads their registers and flags, so each
 * follows the original's steps exactly (the comments say what the steps compute).
 *
 * Angles are words: $4000 a quarter turn, 0 pointing along +x (the sine table's first half is
 * cos). Speeds are 8.8; a velocity component is speed * sin / $4000 (the muls, asl.l #2, swap). */
#include "port.h"

#define SINE_TABLE    0xF26u          /* 65 words: sin of 0 .. $4000 in steps of $100 */
#define COARSE_SIN    0xFE2u          /* 32 directions ($800 apart): y, then x at +$10 */
#define COARSE_COS    0xFF2u
#define DIR16_TABLE   0x106Cu         /* the coarse direction for each octant and slope */
#define TAN_TABLE     0x1102u         /* tangents for the fine direction's binary search */

static uint16_t tst_long(uint16_t cc, uint32_t v) { return cc_move_l(cc, v); }

/* muls.w src,dst: the signed 16x16 product; N and Z from it, V and C cleared, X kept */
static uint32_t muls(uint16_t *cc, uint32_t src, uint32_t dst)
{
    uint32_t r = (uint32_t)((int32_t)(int16_t)src * (int32_t)(int16_t)dst);
    *cc = tst_long(*cc, r);
    return r;
}

/* swap: the halves exchanged; N and Z of the long, V and C cleared */
static uint32_t swap(uint16_t *cc, uint32_t v)
{
    uint32_t r = v << 16 | v >> 16;
    *cc = tst_long(*cc, r);
    return r;
}

/* speed (d4) * sin: muls d4,dX; asl.l #2,dX; swap dX */
static uint32_t scale(uint16_t *cc, uint32_t sin, uint32_t speed)
{
    uint32_t r = muls(cc, speed, sin);
    r = asl_n(cc, r, 2, 32);
    return swap(cc, r);
}

/* $EE6: d0.w = sin(d3) (in -$4000..$4000), interpolated from the table: the angle folded into
 * the first quarter, the table entry for its high byte, plus half and a quarter of the step
 * to the next entry by the low byte's top two bits; negative for a negative angle */
static void sine(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t d0 = c->d[0], d1 = c->d[1], d2 = c->d[2], d3 = c->d[3];
    d2 = set_w(d2, d3); cc = tst_w(cc, d2);                         /* move.w d3,d2 */
    if (d2 & 0x8000) d2 = set_w(d2, neg_w(&cc, d2));                /* bpl; neg.w d2 */
    cmp_w(&cc, 0x4000, d2);
    if (cc & CC_Z) {                                                /* a quarter turn: $4000 */
        d0 = set_w(d0, d2); cc = tst_w(cc, d0);
    } else {
        if (!(cc & CC_C)) {                                         /* past it: $8000 - angle */
            d2 = set_w(d2, (d2 ^ 0x7FFF)); cc = tst_w(cc, d2);
            d2 = set_w(d2, add_w(&cc, 1, d2));
        }
        d1 = set_w(d1, d2); cc = tst_w(cc, d1);
        d1 = set_w(d1, lsr_n(&cc, d1, 8, 16));
        d1 = set_w(d1, add_w(&cc, d1, d1));
        d0 = set_w(d0, rd16(SINE_TABLE + (uint32_t)(int32_t)(int16_t)d1)); cc = tst_w(cc, d0);
        d1 = set_w(d1, rd16(SINE_TABLE + 2 + (uint32_t)(int32_t)(int16_t)d1)); cc = tst_w(cc, d1);
        d1 = set_w(d1, sub_w(&cc, d0, d1));                         /* the step to the next */
        d1 = set_w(d1, lsr_n(&cc, d1, 1, 16));
        cc = tst_b(cc, d2);
        if (d2 & 0x80) d0 = set_w(d0, add_w(&cc, d1, d0));          /* + half of it */
        d1 = set_w(d1, lsr_n(&cc, d1, 1, 16));
        d2 = set_b(d2, add_b(&cc, d2, d2));
        if (d2 & 0x80) d0 = set_w(d0, add_w(&cc, d1, d0));          /* + a quarter */
    }
    cc = tst_w(cc, d3);
    if (d3 & 0x8000) d0 = set_w(d0, neg_w(&cc, d0));
    c->d[0] = d0; c->d[1] = d1; c->d[2] = d2; c->ccr = cc;
}

/* $EE2: cos(d3) = sin(d3 + $4000); d3 keeps the added quarter */
static void cosine(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[3] = set_w(c->d[3], add_w(&cc, 0x4000, c->d[3]));
    c->ccr = cc;
    sine(c);
}

void Cosine(Cpu *c) { cosine(c); }
void Sine(Cpu *c)   { sine(c); }

/* $FA8: the velocity at angle d3, speed d4: d5 = speed * sin, d0 = speed * cos */
void VelocityAt(Cpu *c)
{
    stack_residue(c, 0xFAC);
    sine(c);
    uint16_t cc = c->ccr;
    c->d[0] = scale(&cc, c->d[0], c->d[4]);
    c->d[5] = set_w(c->d[5], c->d[0]); cc = tst_w(cc, c->d[5]);
    c->ccr = cc;
    stack_residue(c, 0xFB8);
    cosine(c);
    cc = c->ccr;
    c->d[0] = scale(&cc, c->d[0], c->d[4]);
    c->ccr = cc;
}

/* $FC0: the same for one of 32 directions (d3's top five bits, rounded), from a small table:
 * d5 = speed * sin, d0 = speed * cos; d3 = the direction * 2 */
void VelocityAtCoarse(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t d3 = set_w(c->d[3], add_w(&cc, 0x400, c->d[3]));
    d3 = lsl_n(&cc, d3, 6, 32);
    d3 = swap(&cc, d3);
    d3 = set_w(d3, d3 & 0x3E); cc = tst_w(cc, d3);
    uint32_t d5 = set_w(c->d[5], rd16(COARSE_SIN + (d3 & 0xFFFF))); cc = tst_w(cc, d5);
    d5 = scale(&cc, d5, c->d[4]);
    uint32_t d0 = set_w(c->d[0], rd16(COARSE_COS + (d3 & 0xFFFF))); cc = tst_w(cc, d0);
    d0 = scale(&cc, d0, c->d[4]);
    c->d[0] = d0; c->d[3] = d3; c->d[5] = d5; c->ccr = cc;
}

/* d0, d1 = a2's x and y less a4's ($1032, $108C) */
static void offset_to_a2(Cpu *c, uint16_t *cc)
{
    c->d[0] = set_w(c->d[0], rd16(ea(c->a[2], -0x3800))); *cc = tst_w(*cc, c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(cc, rd16(ea(c->a[4], -0x3800)), c->d[0]));
    c->d[1] = set_w(c->d[1], rd16(ea(c->a[2], -0x3700))); *cc = tst_w(*cc, c->d[1]);
    c->d[1] = set_w(c->d[1], sub_w(cc, rd16(ea(c->a[4], -0x3700)), c->d[1]));
}

/* the octant: d2 = 2 if dx < 0, + 4 if dy < 0, with d0, d1 made positive */
static void octant(Cpu *c, uint16_t *cc)
{
    c->d[2] = 0; *cc = tst_long(*cc, 0);
    *cc = tst_w(*cc, c->d[0]);
    if (c->d[0] & 0x8000) { c->d[2] = set_w(c->d[2], add_w(cc, 2, c->d[2])); c->d[0] = set_w(c->d[0], neg_w(cc, c->d[0])); }
    *cc = tst_w(*cc, c->d[1]);
    if (c->d[1] & 0x8000) { c->d[2] = set_w(c->d[2], add_w(cc, 4, c->d[2])); c->d[1] = set_w(c->d[1], neg_w(cc, c->d[1])); }
}

/* $1042: one of 16 directions for the offset d0, d1 (from the octant and whether the shorter
 * side is under half the longer), as an angle in d3 */
static void direction16(Cpu *c)
{
    uint16_t cc = c->ccr;
    octant(c, &cc);
    cmp_w(&cc, c->d[1], c->d[0]);
    if (!(cc & CC_C)) {                                             /* |dx| >= |dy| */
        c->d[2] = set_w(c->d[2], add_w(&cc, 8, c->d[2]));
        uint32_t t = c->d[0]; c->d[0] = c->d[1]; c->d[1] = t;
    }
    c->d[0] = set_w(c->d[0], add_w(&cc, c->d[0], c->d[0]));
    cmp_w(&cc, c->d[1], c->d[0]);
    if (!(cc & CC_C)) { c->d[2] = set_w(c->d[2], c->d[2] | 0x10); cc = tst_w(cc, c->d[2]); }
    c->d[3] = set_w(c->d[3], rd16(DIR16_TABLE + (uint32_t)(int32_t)(int16_t)c->d[2])); cc = tst_w(cc, c->d[3]);
    c->ccr = cc;
}

void Direction16(Cpu *c) { direction16(c); }
void Direction16To(Cpu *c)
{
    uint16_t cc = c->ccr;
    offset_to_a2(c, &cc);
    c->ccr = cc;
    direction16(c);
}

/* $109C: the direction of the offset d0, d1 as an angle in d3 (to 1/1024 of a turn): the
 * tangent (shorter / longer side, as a fraction) found by a binary search of the table, then
 * mirrored into its octant */
static void direction(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t d0, d1, d2, d3 = c->d[3];
    octant(c, &cc);
    d0 = c->d[0]; d1 = c->d[1]; d2 = c->d[2];
    cmp_w(&cc, d0, d1);
    if (!(cc & CC_C)) {                                             /* |dy| >= |dx| */
        d2 = set_w(d2, add_w(&cc, 1, d2));
        uint32_t t = d0; d0 = d1; d1 = t;
    }
    d1 = swap(&cc, d1);
    d1 &= 0xFFFF0000u; cc = (uint16_t)((cc & CC_X) | CC_Z);          /* clr.w d1 */
    d3 = 0; cc = (uint16_t)((cc & CC_X) | CC_Z);                     /* moveq #0,d3 */
    cc = tst_w(cc, d0);
    if (d0 & 0xFFFF) {
        uint32_t q = d1 / (d0 & 0xFFFF), r = d1 % (d0 & 0xFFFF);    /* divu.w d0,d1 */
        if (q < 0x10000) { d1 = (q & 0xFFFF) | r << 16; cc = (uint16_t)((cc & CC_X) | (q & 0x8000 ? CC_N : 0) | (q ? 0 : CC_Z)); }
        else cc |= CC_V;                                            /* 45 degrees: d1 kept */
        cmp_w(&cc, rd16(TAN_TABLE + (d3 & 0xFFFF)), d1);
        if (!(cc & CC_C)) {
            uint32_t bit = 0x80;                                    /* moveq-free: move.w #$80,d0 */
            d0 = set_w(d0, bit); cc = tst_w(cc, d0);
            for (;;) {
                d3 = set_w(d3, (d3 | d0) & 0xFFFF); cc = tst_w(cc, d3);
                cmp_w(&cc, rd16(TAN_TABLE + (d3 & 0xFFFF)), d1);
                if (cc & CC_C) { d3 = set_w(d3, (d3 ^ d0) & 0xFFFF); cc = tst_w(cc, d3); }
                d0 = set_w(d0, lsr_n(&cc, d0, 1, 16));
                cc = (uint16_t)((cc & ~CC_Z) | (d0 & 1 ? 0 : CC_Z));   /* btst #0,d0 */
                if (d0 & 1) break;
            }
            d3 = set_w(d3, add_w(&cc, 2, d3));
        }
        static const uint16_t flip[3] = {0x1FE, 0x3FE, 0x7FE};
        for (int i = 0; i < 3; i++) {                                /* into the octant */
            d2 = set_w(d2, lsr_n(&cc, d2, 1, 16));
            if (cc & CC_C) {
                d3 = set_w(d3, (d3 ^ flip[i]) & 0xFFFF); cc = tst_w(cc, d3);
                d3 = set_w(d3, add_w(&cc, 2, d3));
            }
        }
        d3 = set_w(d3, lsl_n(&cc, d3, 5, 16));
    }
    c->d[0] = d0; c->d[1] = d1; c->d[2] = d2; c->d[3] = d3; c->ccr = cc;
}

void Direction(Cpu *c) { direction(c); }
void DirectionTo(Cpu *c)
{
    uint16_t cc = c->ccr;
    offset_to_a2(c, &cc);
    c->ccr = cc;
    direction(c);
}

/* the velocity, stored: d0 into x, d5 into y */
static void store_velocity(Cpu *c)
{
    wr16(ea(c->a[4], -0x3600), (uint16_t)c->d[0]);
    wr16(ea(c->a[4], -0x35FE), (uint16_t)c->d[5]);
    c->ccr = tst_w(c->ccr, c->d[5]);
}

/* $1208: velocity at angle d3, speed d4 */
void SetVelocityAt(Cpu *c)
{
    stack_residue(c, 0x120C);
    c->a[7] -= 4; VelocityAt(c); c->a[7] += 4;
    store_velocity(c);
}

/* $1216: d0, d5 = speed d4 towards a2 (d6 = the angle) */
void VelocityTo(Cpu *c)
{
    stack_residue(c, 0x121A);
    c->a[7] -= 4; DirectionTo(c); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], c->d[3]); c->ccr = tst_w(c->ccr, c->d[6]);
    VelocityAt(c);
}

/* $1234: velocity towards a2 at speed d4, coarse (32 directions); $123A: at angle d3 */
void SetVelocityAtCoarse(Cpu *c)
{
    stack_residue(c, 0x123E);
    c->a[7] -= 4; VelocityAtCoarse(c); c->a[7] += 4;
    store_velocity(c);
}
void SetVelocityToCoarse(Cpu *c)
{
    stack_residue(c, 0x1238);
    c->a[7] -= 4; DirectionTo(c); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], c->d[3]); c->ccr = tst_w(c->ccr, c->d[6]);
    SetVelocityAtCoarse(c);
}

/* $1248: d0, d5 = speed d4 towards a2, coarse */
void VelocityToCoarse(Cpu *c)
{
    stack_residue(c, 0x124C);
    c->a[7] -= 4; DirectionTo(c); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], c->d[3]); c->ccr = tst_w(c->ccr, c->d[6]);
    VelocityAtCoarse(c);
}

/* $1252: about how far a2 is from a4: the longer side plus 3/8 of the shorter */
void Distance(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t d0 = c->d[0], d1 = c->d[1];
    d0 = set_w(d0, rd16(ea(c->a[2], -0x3800))); cc = tst_w(cc, d0);
    d0 = set_w(d0, sub_w(&cc, rd16(ea(c->a[4], -0x3800)), d0));
    if (d0 & 0x8000) d0 = set_w(d0, neg_w(&cc, d0));
    d1 = set_w(d1, rd16(ea(c->a[2], -0x3700))); cc = tst_w(cc, d1);
    d1 = set_w(d1, sub_w(&cc, rd16(ea(c->a[4], -0x3700)), d1));
    if (d1 & 0x8000) d1 = set_w(d1, neg_w(&cc, d1));
    cmp_w(&cc, d0, d1);
    if (!(cc & CC_C)) { uint32_t t = d0; d0 = d1; d1 = t; }
    d1 = set_w(d1, lsr_n(&cc, d1, 1, 16));
    d0 = set_w(d0, add_w(&cc, d1, d0));
    d1 = set_w(d1, lsr_n(&cc, d1, 2, 16));
    d0 = set_w(d0, sub_w(&cc, d1, d0));
    c->d[0] = d0; c->d[1] = d1; c->ccr = cc;
}
