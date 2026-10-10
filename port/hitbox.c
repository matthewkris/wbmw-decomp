/* Hitboxes and contact ($C36-$EE0). Entities: a4 and a2. A box list is a count byte, then
 * 4-byte boxes: signed x and y offsets from the entity, half-width, half-height. Fields:
 *   -$3800/-$3700 X/Y   -$3FFE bit 3 face left (x offsets mirrored), bit 4 flip y
 *   -$3500/-$34FF body half-width/height   -$2A00 attack boxes   -$2900 hurt boxes
 *   -$2AFD contact flags   -$2700 contact damage   -$2400 attack damage   -$23FE damage taken
 *   -$2500/-$24FE the other's X/Y at contact   -$22FE the other entity
 * Two boxes overlap when |dx| <= wa + wb and |dy| <= ha + hb; the routines return the last
 * subtraction's flags, and callers test them with bls (overlap) / bhi (apart).
 * Stack frames (link) and the return addresses of internal calls are written through a
 * simulated stack pointer, in the original's order, so the words below SP match. */
#include "port.h"

#define F(r, disp) ea((r), (disp))
#define SEXT(w) ((uint32_t)(int32_t)(int16_t)((w) & 0xFFFF))

static uint32_t neg_b(uint16_t *cc, uint32_t v) { return sub_b(cc, v, 0); }
static void or_mem_b(uint16_t *cc, uint32_t a, uint32_t v) { uint32_t r = (rd8(a) | v) & 0xFF; wr8(a, (uint8_t)r); *cc = tst_b(*cc, r); }
static void and_mem_b(uint16_t *cc, uint32_t a, uint32_t v) { uint32_t r = rd8(a) & v & 0xFF; wr8(a, (uint8_t)r); *cc = tst_b(*cc, r); }
static void move_mem_w(uint16_t *cc, uint32_t dst, uint32_t v) { wr16(dst, (uint16_t)v); *cc = tst_w(*cc, v); }
static int btst_mem(uint16_t *cc, uint32_t a, int bit)
{
    int set = rd8(a) >> bit & 1;
    *cc = (uint16_t)((*cc & ~CC_Z) | (set ? 0 : CC_Z));
    return set;
}

/* one axis of a box against a box (or a body): the distance between the two centres, minus
 * the two half-sizes; the caller's d-registers are set exactly as the 68000 leaves them */
static void axis_dist(uint16_t *cc, uint32_t *da, uint32_t *db)
{
    cmp_w(cc, *db, *da);
    if (*cc & CC_C) { uint32_t t = *da; *da = *db; *db = t; }              /* exg */
    *da = set_w(*da, sub_w(cc, *db, *da));
}

/* BoxPairOverlap ($CA0): box a0 of entity a2 against box a1 of entity a4.
 * Original bug kept: for a1's y offset the byte is sign-extended before it is negated, so a
 * flipped offset of -8 becomes -248, not 8. */
void BoxPairOverlap(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t a2 = c->a[2], a4 = c->a[4], d0 = c->d[0], d1 = c->d[1], d2 = c->d[2], d3 = c->d[3];
    d0 = set_b(d0, rd8(c->a[1])); cc = tst_b(cc, d0);
    if (btst_mem(&cc, F(a4, -0x3FFE), 3)) d0 = set_b(d0, neg_b(&cc, d0));
    d0 = ext_w(d0); cc = tst_w(cc, d0);
    d0 = set_w(d0, add_w(&cc, rd16(F(a4, -0x3800)), d0));
    d1 = set_b(d1, rd8(c->a[0])); cc = tst_b(cc, d1);
    if (btst_mem(&cc, F(a2, -0x3FFE), 3)) d1 = set_b(d1, neg_b(&cc, d1));
    d1 = ext_w(d1); cc = tst_w(cc, d1);
    d1 = set_w(d1, add_w(&cc, rd16(F(a2, -0x3800)), d1));
    axis_dist(&cc, &d0, &d1);
    d1 = 0; d2 = 0; cc = tst_l(cc, 0);
    d1 = set_b(d1, rd8(c->a[0] + 2)); d2 = set_b(d2, rd8(c->a[1] + 2)); cc = tst_b(cc, d2);
    d1 = set_w(d1, add_w(&cc, d2, d1));
    d0 = set_w(d0, sub_w(&cc, d1, d0));
    if (!(cc & (CC_C | CC_Z))) goto out;                                    /* bhi: apart in x */
    d1 = set_b(d1, rd8(c->a[1] + 1)); cc = tst_b(cc, d1);
    d1 = ext_w(d1); cc = tst_w(cc, d1);
    if (btst_mem(&cc, F(a4, -0x3FFE), 4)) d1 = set_b(d1, neg_b(&cc, d1));   /* after ext: the bug */
    d1 = set_w(d1, add_w(&cc, rd16(F(a4, -0x3700)), d1));
    d2 = set_b(d2, rd8(c->a[0] + 1)); cc = tst_b(cc, d2);
    if (btst_mem(&cc, F(a2, -0x3FFE), 4)) d2 = set_b(d2, neg_b(&cc, d2));
    d2 = ext_w(d2); cc = tst_w(cc, d2);
    d2 = set_w(d2, add_w(&cc, rd16(F(a2, -0x3700)), d2));
    axis_dist(&cc, &d1, &d2);
    d2 = 0; d3 = 0; cc = tst_l(cc, 0);
    d2 = set_b(d2, rd8(c->a[0] + 3)); d3 = set_b(d3, rd8(c->a[1] + 3)); cc = tst_b(cc, d3);
    d2 = set_w(d2, add_w(&cc, d3, d2));
    d1 = set_w(d1, sub_w(&cc, d2, d1));
out:
    c->d[0] = d0; c->d[1] = d1; c->d[2] = d2; c->d[3] = d3; c->ccr = cc;
}

/* BoxVsBody ($DB6): box a0 of entity a2 against the body of entity a4 */
void BoxVsBody(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t a2 = c->a[2], a4 = c->a[4], d0 = c->d[0], d1 = c->d[1], d2 = c->d[2], d3 = c->d[3];
    d0 = set_w(d0, rd16(F(a4, -0x3800))); cc = tst_w(cc, d0);
    d1 = set_b(d1, rd8(c->a[0])); cc = tst_b(cc, d1);
    if (btst_mem(&cc, F(a2, -0x3FFE), 3)) d1 = set_b(d1, neg_b(&cc, d1));
    d1 = ext_w(d1); cc = tst_w(cc, d1);
    d1 = set_w(d1, add_w(&cc, rd16(F(a2, -0x3800)), d1));
    axis_dist(&cc, &d0, &d1);
    d1 = 0; d2 = 0; cc = tst_l(cc, 0);
    d1 = set_b(d1, rd8(F(a4, -0x3500))); d2 = set_b(d2, rd8(c->a[0] + 2)); cc = tst_b(cc, d2);
    d1 = set_w(d1, add_w(&cc, d2, d1));
    d0 = set_w(d0, sub_w(&cc, d1, d0));
    if (!(cc & (CC_C | CC_Z))) goto out;
    d1 = set_w(d1, rd16(F(a4, -0x3700))); cc = tst_w(cc, d1);
    d2 = set_b(d2, rd8(c->a[0] + 1)); cc = tst_b(cc, d2);
    if (btst_mem(&cc, F(a2, -0x3FFE), 4)) d2 = set_b(d2, neg_b(&cc, d2));
    d2 = ext_w(d2); cc = tst_w(cc, d2);
    d2 = set_w(d2, add_w(&cc, rd16(F(a2, -0x3700)), d2));
    axis_dist(&cc, &d1, &d2);
    d2 = 0; d3 = 0; cc = tst_l(cc, 0);
    d2 = set_b(d2, rd8(F(a4, -0x34FF))); d3 = set_b(d3, rd8(c->a[0] + 3)); cc = tst_b(cc, d3);
    d2 = set_w(d2, add_w(&cc, d3, d2));
    d1 = set_w(d1, sub_w(&cc, d2, d1));
out:
    c->d[0] = d0; c->d[1] = d1; c->d[2] = d2; c->d[3] = d3; c->ccr = cc;
}

/* BodyOverlap ($E9A): body of a4 against body of a2; d4 = 1 when a4 is left of a2 */
void BodyOverlap(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t a2 = c->a[2], a4 = c->a[4], d0 = c->d[0], d1 = c->d[1], d2 = c->d[2], d3 = c->d[3];
    c->d[4] = 0; cc = tst_l(cc, 0);
    d0 = set_w(d0, rd16(F(a4, -0x3800))); cc = tst_w(cc, d0);
    d1 = set_w(d1, rd16(F(a2, -0x3800))); cc = tst_w(cc, d1);
    cmp_w(&cc, d1, d0);
    if (cc & CC_C) { uint32_t t = d0; d0 = d1; d1 = t; c->d[4] = 1; cc = tst_l(cc, 1); }
    d0 = set_w(d0, sub_w(&cc, d1, d0));
    d1 = 0; d2 = 0; cc = tst_l(cc, 0);
    d1 = set_b(d1, rd8(F(a4, -0x3500))); d2 = set_b(d2, rd8(F(a2, -0x3500))); cc = tst_b(cc, d2);
    d1 = set_w(d1, add_w(&cc, d2, d1));
    d0 = set_w(d0, sub_w(&cc, d1, d0));
    if (!(cc & (CC_C | CC_Z))) goto out;
    d1 = set_w(d1, rd16(F(a4, -0x3700))); cc = tst_w(cc, d1);
    d2 = set_w(d2, rd16(F(a2, -0x3700))); cc = tst_w(cc, d2);
    axis_dist(&cc, &d1, &d2);
    d2 = 0; d3 = 0; cc = tst_l(cc, 0);
    d2 = set_b(d2, rd8(F(a4, -0x34FF))); d3 = set_b(d3, rd8(F(a2, -0x34FF))); cc = tst_b(cc, d3);
    d2 = set_w(d2, add_w(&cc, d3, d2));
    d1 = set_w(d1, sub_w(&cc, d2, d1));
out:
    c->d[0] = d0; c->d[1] = d1; c->d[2] = d2; c->d[3] = d3; c->ccr = cc;
}

/* ContactSide ($E4A): clear bit 0 of both contact flags; d4 = 1 when a4 is left of a2 */
void ContactSide(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[0] = 0xFFFFFFFE; cc = tst_l(cc, c->d[0]);
    and_mem_b(&cc, F(c->a[4], -0x2AFD), 0xFE);
    and_mem_b(&cc, F(c->a[2], -0x2AFD), 0xFE);
    c->d[4] = 0; cc = tst_l(cc, 0);
    c->d[0] = set_w(c->d[0], rd16(F(c->a[4], -0x3800))); cc = tst_w(cc, c->d[0]);
    cmp_w(&cc, rd16(F(c->a[2], -0x3800)), c->d[0]);
    if (cc & CC_C) { c->d[4] = 1; cc = tst_l(cc, 1); }
    c->ccr = cc;
}

/* ResolveContact ($E1A): mark the side, and give each entity the other's position and id */
void ResolveContact(Cpu *c)
{
    uint32_t sp = c->a[7];
    wr32(sp - 4, 0xE1C); c->a[7] = sp - 4; ContactSide(c); c->a[7] = sp;   /* bsr ContactSide */
    uint16_t cc = c->ccr;
    uint32_t a2 = c->a[2], a4 = c->a[4];
    or_mem_b(&cc, F(a2, -0x2AFD), c->d[4] & 0xFF);
    c->d[4] ^= 1; cc = (uint16_t)((cc & ~CC_Z) | (c->d[4] & 1 ? CC_Z : 0)); /* bchg: Z = old bit clear */
    or_mem_b(&cc, F(a4, -0x2AFD), c->d[4] & 0xFF);
    move_mem_w(&cc, F(a4, -0x2500), rd16(F(a2, -0x3800)));
    move_mem_w(&cc, F(a2, -0x2500), rd16(F(a4, -0x3800)));
    move_mem_w(&cc, F(a4, -0x24FE), rd16(F(a2, -0x3700)));
    move_mem_w(&cc, F(a2, -0x24FE), rd16(F(a4, -0x3700)));
    move_mem_w(&cc, F(a2, -0x22FE), a4);
    move_mem_w(&cc, F(a4, -0x22FE), a2);
    c->ccr = cc;
}

/* HitboxOverlap ($C3E): every attack box of a2 against every hurt box of a4; on the first
 * overlap mark both (a2 |= $40, a4 |= $10), resolve the contact and return d0 = -1, else 0.
 * Frame: link a6,#-10 with -6 hurt boxes left, -4 attack box count, -2 attack boxes left,
 * -10 first attack box. */
void HitboxOverlap(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t sp0 = c->a[7], a2 = c->a[2], a4 = c->a[4];
    c->d[0] = rd32(F(a2, -0x2A00)); cc = tst_l(cc, c->d[0]);
    if (!c->d[0]) goto none;
    c->a[0] = c->d[0];
    c->d[0] = rd32(F(a4, -0x2900)); cc = tst_l(cc, c->d[0]);
    if (!c->d[0]) goto none;
    c->a[1] = c->d[0];
    wr32(sp0 - 4, c->a[6]); c->a[6] = sp0 - 4;                              /* link a6,#-10 */
    uint32_t a6 = c->a[6], sp = a6 - 10;
    { uint32_t v = rd8(c->a[1]++); wr8(a6 - 6, (uint8_t)v); cc = tst_b(cc, v); }
    c->d[0] = set_b(c->d[0], rd8(c->a[0]++)); cc = tst_b(cc, c->d[0]);
    wr8(a6 - 4, (uint8_t)c->d[0]); wr8(a6 - 2, (uint8_t)c->d[0]); cc = tst_b(cc, c->d[0]);
    wr32(a6 - 10, c->a[0]); cc = tst_l(cc, c->a[0]);
    for (;;) {
        wr32(sp - 4, 0xC66); c->ccr = cc; BoxPairOverlap(c); cc = c->ccr;   /* bsr */
        if (cc & (CC_C | CC_Z)) {                                           /* bls: hit, $C84 */
            or_mem_b(&cc, F(a2, -0x2AFD), 0x40);
            or_mem_b(&cc, F(a4, -0x2AFD), 0x10);
            wr32(sp - 4, 0xC94); c->a[7] = sp - 4; c->ccr = cc;
            ResolveContact(c); cc = c->ccr;
            c->a[6] = rd32(a6); c->a[7] = sp0;                             /* unlk */
            c->d[0] = 0xFFFFFFFF; c->ccr = tst_l(cc, c->d[0]);
            return;
        }
        c->a[0] += 4;
        { uint32_t n = sub_b(&cc, 1, rd8(a6 - 2)); wr8(a6 - 2, (uint8_t)n); if (n) continue; }
        c->a[1] += 4;
        { uint32_t v = rd8(a6 - 4); wr8(a6 - 2, (uint8_t)v); cc = tst_b(cc, v); }
        c->a[0] = rd32(a6 - 10);
        { uint32_t n = sub_b(&cc, 1, rd8(a6 - 6)); wr8(a6 - 6, (uint8_t)n); if (n) continue; }
        break;
    }
    c->a[6] = rd32(a6); c->a[7] = sp0;                                     /* unlk */
none:
    c->d[0] = 0; c->ccr = tst_l(cc, 0);
}

/* HitboxOverlapSwap ($C36): HitboxOverlap with a2 and a4 exchanged */
void HitboxOverlapSwap(Cpu *c)
{
    uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t;
    wr32(c->a[7] - 4, 0xC3A); c->a[7] -= 4; HitboxOverlap(c); c->a[7] += 4;
    t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t;
}

/* BodyVsHurtBoxes ($D2C): a2's hurt boxes against a4's body, unless a4 has contact flag bit 5;
 * on a hit a2 |= $10, a4 |= 2, resolve, d0 = -1; else d0 = 0. Frame: link a6,#-2 (boxes left). */
void BodyVsHurtBoxes(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t sp0 = c->a[7], a2 = c->a[2], a4 = c->a[4];
    if (btst_mem(&cc, F(a4, -0x2AFD), 5)) goto none;
    c->d[0] = rd32(F(a2, -0x2900)); cc = tst_l(cc, c->d[0]);
    if (!c->d[0]) goto none;
    c->a[0] = c->d[0];
    wr32(sp0 - 4, c->a[6]); c->a[6] = sp0 - 4;                              /* link a6,#-2 */
    uint32_t a6 = c->a[6], sp = a6 - 2;
    { uint32_t v = rd8(c->a[0]++); wr8(a6 - 2, (uint8_t)v); cc = tst_b(cc, v); }
    for (;;) {
        wr32(sp - 4, 0xD46); c->ccr = cc; BoxVsBody(c); cc = c->ccr;
        if (cc & (CC_C | CC_Z)) {
            or_mem_b(&cc, F(a2, -0x2AFD), 0x10);
            or_mem_b(&cc, F(a4, -0x2AFD), 0x02);
            wr32(sp - 4, 0xD62); c->a[7] = sp - 4; c->ccr = cc;
            ResolveContact(c); cc = c->ccr;
            c->a[6] = rd32(a6); c->a[7] = sp0;
            c->d[0] = 0xFFFFFFFF; c->ccr = tst_l(cc, c->d[0]);
            return;
        }
        c->a[0] += 4;
        uint32_t n = sub_b(&cc, 1, rd8(a6 - 2)); wr8(a6 - 2, (uint8_t)n);
        if (!n) break;
    }
    c->a[6] = rd32(a6); c->a[7] = sp0;
none:
    c->d[0] = 0; c->ccr = tst_l(cc, 0);
}

/* BodyVsAttackBoxes ($D76): a2's attack boxes against a4's body, unless a4 has contact flag
 * bit 6; on a hit a2 |= $20, a4 |= $88, a4 takes a2's attack damage, resolve. */
void BodyVsAttackBoxes(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t sp0 = c->a[7], a2 = c->a[2], a4 = c->a[4];
    if (btst_mem(&cc, F(a4, -0x2AFD), 6)) { c->ccr = cc; return; }
    c->d[0] = rd32(F(a2, -0x2A00)); cc = tst_l(cc, c->d[0]);
    if (!c->d[0]) { c->ccr = cc; return; }
    c->a[0] = c->d[0];
    wr32(sp0 - 4, c->a[6]); c->a[6] = sp0 - 4;
    uint32_t a6 = c->a[6], sp = a6 - 2;
    { uint32_t v = rd8(c->a[0]++); wr8(a6 - 2, (uint8_t)v); cc = tst_b(cc, v); }
    for (;;) {
        wr32(sp - 4, 0xD92); c->ccr = cc; BoxVsBody(c); cc = c->ccr;
        if (cc & (CC_C | CC_Z)) {
            or_mem_b(&cc, F(a2, -0x2AFD), 0x20);
            or_mem_b(&cc, F(a4, -0x2AFD), 0x88);
            move_mem_w(&cc, F(a4, -0x23FE), rd16(F(a2, -0x2400)));
            wr32(sp - 4, 0xDB2); c->a[7] = sp - 4; c->ccr = cc;
            ResolveContact(c); cc = c->ccr;
            break;
        }
        c->a[0] += 4;
        uint32_t n = sub_b(&cc, 1, rd8(a6 - 2)); wr8(a6 - 2, (uint8_t)n);
        if (!n) break;
    }
    c->a[6] = rd32(a6); c->a[7] = sp0;
    c->ccr = cc;
}

/* BodyVsAttackBoxesSwap ($D6E): BodyVsAttackBoxes with a2 and a4 exchanged */
void BodyVsAttackBoxesSwap(Cpu *c)
{
    uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t;
    wr32(c->a[7] - 4, 0xD72); c->a[7] -= 4; BodyVsAttackBoxes(c); c->a[7] += 4;
    t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t;
}

/* CheckBodyContact ($E64): bodies of a4 and a2 touching (unless a2's contact flags already have
 * bit 7): each takes the other's contact damage, a2 |= $84 + side, a4 |= 4 + other side. */
void CheckBodyContact(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t a2 = c->a[2], a4 = c->a[4];
    cc = tst_b(cc, rd8(F(a2, -0x2AFD)));
    if (rd8(F(a2, -0x2AFD)) & 0x80) { c->ccr = cc; return; }
    wr32(c->a[7] - 4, 0xE6C); c->a[7] -= 4; c->ccr = cc; BodyOverlap(c); c->a[7] += 4; cc = c->ccr;
    if (!(cc & (CC_C | CC_Z))) { c->ccr = cc; return; }                    /* bhi: apart */
    c->d[3] = set_w(c->d[3], c->d[4]); cc = tst_w(cc, c->d[4]);
    move_mem_w(&cc, F(a4, -0x23FE), rd16(F(a2, -0x2700)));
    c->d[3] = set_b(c->d[3], c->d[3] | 0x84); cc = tst_b(cc, c->d[3]);
    or_mem_b(&cc, F(a2, -0x2AFD), c->d[3] & 0xFF);
    move_mem_w(&cc, F(a2, -0x23FE), rd16(F(a4, -0x2700)));
    move_mem_w(&cc, F(a2, -0x22FE), a4);
    c->d[4] ^= 1; cc = (uint16_t)((cc & ~CC_Z) | (c->d[4] & 1 ? CC_Z : 0));
    c->d[4] = set_b(c->d[4], c->d[4] | 4); cc = tst_b(cc, c->d[4]);
    or_mem_b(&cc, F(a4, -0x2AFD), c->d[4] & 0xFF);
    move_mem_w(&cc, F(a4, -0x22FE), a2);
    c->ccr = cc;
}
