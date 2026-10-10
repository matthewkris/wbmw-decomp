/* Entity animation: SetAnim, ResetAnimTimer, SetAnimNow and the script interpreter UpdateAnim
 * ($7E8-$A18). Formats in docs/NOTES.md (Sprites). Fields, as displacements from a4:
 *   -$4000 flags      -$3FFE flip/move flags   -$3FFD animation state bits
 *   -$3F00 mapping    -$3D00/-$3CFF/-$3CFE set/group/animation (byte offsets)
 *   -$3C00 script pointer  -$3B00 loop point  -$3A00 timer (8.8)  -$39FE speed
 *   -$3900/-$38FF blink pattern/counter   -$2C00 frame count */
#include "port.h"

#define F(c, disp) ea((c)->a[4], (disp))
#define ANIM 0x000A0000u

/* SetAnim ($7E8): animation number d0; restart on the next update */
void SetAnim(Cpu *c)
{
    wr8(F(c, -0x3CFE), (uint8_t)c->d[0]);
    uint32_t v = rd8(F(c, -0x3FFD)) & 0x7E;
    wr8(F(c, -0x3FFD), (uint8_t)v);
    c->ccr = tst_b(c->ccr, v);
}

/* ResetAnimTimer ($7F4): timer = speed */
void ResetAnimTimer(Cpu *c)
{
    uint32_t v = rd16(F(c, -0x39FE));
    wr16(F(c, -0x3A00), (uint16_t)v);
    c->ccr = tst_w(c->ccr, v);
}

/* the interpreter, from loc_81C (start == 1: look the animation up and start it) or from the
 * per-frame continuation; cc carries the condition codes through, as the 68000's do */
static void run(Cpu *c, int start, uint16_t cc)
{
    uint32_t d0 = c->d[0], d1 = c->d[1], a1 = c->a[1], sp0 = c->a[7];
    c->a[0] = ANIM;
    if (start) {
        d0 = rd8(F(c, -0x3D00)); cc = tst_b(cc, d0);                       /* moveq #0; move.b set */
        d1 = set_w(d1, rd16(ANIM + (uint32_t)(int32_t)(int16_t)d0)); cc = tst_w(cc, d1);
        d0 = set_b(d0, rd8(F(c, -0x3CFF))); cc = tst_b(cc, d0);
        d1 = set_w(d1, add_w(&cc, d0, d1));
        d1 = set_w(d1, rd16(ANIM + (uint32_t)(int32_t)(int16_t)d1)); cc = tst_w(cc, d1);
        d0 = set_b(d0, rd8(F(c, -0x3CFE))); cc = tst_b(cc, d0);
        d1 = set_w(d1, add_w(&cc, d0, d1));
        d1 = set_w(d1, rd16(ANIM + (uint32_t)(int32_t)(int16_t)d1)); cc = tst_w(cc, d1);
        a1 = ANIM + (uint32_t)(int32_t)(int16_t)d1;
        wr32(F(c, -0x3B00), a1);
        wr16(F(c, -0x3A00), 0); cc = (uint16_t)((cc & CC_X) | CC_Z);
        wr8(F(c, -0x2C00), 0);
        goto script;
    }
    /* continue: frozen entities and held frames only update their state bits */
    cc = (uint16_t)((cc & ~CC_Z) | (rd8(F(c, -0x4000)) & 0x20 ? 0 : CC_Z));   /* btst #5 */
    if (rd8(F(c, -0x4000)) & 0x20) goto state;
    cc = tst_w(cc, rd16(F(c, -0x3A00)));
    if (!rd16(F(c, -0x3A00))) goto state;
    d0 = set_w(d0, rd16(F(c, -0x39FE))); cc = tst_w(cc, d0);
    {
        uint32_t t = sub_w(&cc, d0, rd16(F(c, -0x3A00)));
        wr16(F(c, -0x3A00), (uint16_t)t);
        if (!(cc & CC_C)) {
            cc = tst_b(cc, t >> 8);
            if (t >> 8) goto state;                                          /* tst.b timer hi; bne */
        }
    }
    wr8(F(c, -0x2C00), (uint8_t)add_b(&cc, 1, rd8(F(c, -0x2C00))));
    a1 = rd32(F(c, -0x3C00));
script:
    for (;;) {
        d0 = set_b(d0, rd8(a1++)); cc = tst_b(cc, d0);
        cmp_b(&cc, 0xF0, d0);
        if (d0 & 0xFF) { if ((d0 & 0xFF) < 0xF0) goto frame; } else goto frame;
        /* command: andi.w #$f; lsl.w #2; pea loc_878; move.l handler,-(a7); rts */
        unsigned cmd = d0 & 0xF;
        d0 = set_w(d0, cmd); cc = tst_w(cc, d0);
        d0 = set_w(d0, cmd << 2);
        cc = (uint16_t)(cc_nz_w(cmd << 2));                                 /* lsl.w #2: X = C = 0 */
        static const uint32_t handler[16] = {
            0x958, 0x958, 0x958, 0x958, 0x958, 0x958, 0x958, 0x958,
            0x95A, 0x962, 0x96A, 0x990, 0x99A, 0x9FC, 0xA0A, 0xA10 };
        wr32(sp0 - 4, 0x878);
        wr32(sp0 - 8, handler[cmd]);
        switch (cmd) {
        case 8: case 9: {
            uint32_t v = rd8(F(c, -0x3FFE)) | (cmd == 8 ? 1 : 2);
            wr8(F(c, -0x3FFE), (uint8_t)v); cc = tst_b(cc, v);
            break;
        }
        case 10:                                                             /* move by (dx, dy) */
            for (int axis = 0; axis < 2; axis++) {
                d0 = ext_w(set_b(d0, rd8(a1++))); cc = tst_w(cc, d0);
                int bit = axis ? 4 : 3;
                cc = (uint16_t)((cc & ~CC_Z) | (rd8(F(c, -0x3FFE)) >> bit & 1 ? 0 : CC_Z));
                if (rd8(F(c, -0x3FFE)) >> bit & 1) d0 = set_w(d0, neg_w(&cc, d0));
                int16_t pos = axis ? -0x3700 : -0x3800;
                wr16(F(c, pos), (uint16_t)add_w(&cc, d0, rd16(F(c, pos))));
            }
            break;
        case 11: {                                                           /* box offset */
            uint32_t v = rd8(a1++); wr8(F(c, -0x3500), (uint8_t)v);
            v = rd8(a1++); wr8(F(c, -0x34FF), (uint8_t)v); cc = tst_b(cc, v);
            break;
        }
        case 12: {                                                           /* box size, keeping the feet still */
            d0 = set_b(d0, rd8(a1++)); cc = tst_b(cc, d0);
            int solid = rd8(F(c, -0x4000)) & 0x40;
            cc = (uint16_t)((cc & ~CC_Z) | (solid ? 0 : CC_Z));
            if (solid) {
                int b0 = rd8(F(c, -0x3100)) & 1;
                cc = (uint16_t)((cc & ~CC_Z) | (b0 ? 0 : CC_Z));
                if (b0) {
                    d1 = set_b(d1, rd8(F(c, -0x34FE))); cc = tst_b(cc, d1);
                    d1 = set_b(d1, sub_b(&cc, d0, d1));
                    if (cc & CC_C) {
                        d1 = ext_w(d1); cc = tst_w(cc, d1);
                        int b2 = rd8(F(c, -0x3100)) & 4;
                        cc = (uint16_t)((cc & ~CC_Z) | (b2 ? 0 : CC_Z));
                        if (b2) d1 = set_w(d1, neg_w(&cc, d1));
                        wr16(F(c, -0x3800), (uint16_t)add_w(&cc, d1, rd16(F(c, -0x3800))));
                    }
                }
            }
            wr8(F(c, -0x34FE), (uint8_t)d0); cc = tst_b(cc, d0);
            d0 = set_b(d0, rd8(a1++)); cc = tst_b(cc, d0);
            solid = rd8(F(c, -0x4000)) & 0x40;
            cc = (uint16_t)((cc & ~CC_Z) | (solid ? 0 : CC_Z));
            if (solid) {
                int adjust;
                int f2 = rd8(F(c, -0x3FFE)) & 4;
                cc = (uint16_t)((cc & ~CC_Z) | (f2 ? 0 : CC_Z));
                if (f2) adjust = 1;
                else {
                    int b1 = rd8(F(c, -0x3100)) & 2;
                    cc = (uint16_t)((cc & ~CC_Z) | (b1 ? 0 : CC_Z));
                    if (!b1) adjust = 0;
                    else {
                        int b3 = rd8(F(c, -0x3100)) & 8;
                        cc = (uint16_t)((cc & ~CC_Z) | (b3 ? 0 : CC_Z));
                        adjust = !b3;
                    }
                }
                if (adjust) {
                    d1 = set_b(d1, rd8(F(c, -0x34FD))); cc = tst_b(cc, d1);
                    d1 = ext_w(set_b(d1, sub_b(&cc, d0, d1))); cc = tst_w(cc, d1);
                    wr16(F(c, -0x3700), (uint16_t)add_w(&cc, d1, rd16(F(c, -0x3700))));
                }
            }
            wr8(F(c, -0x34FD), (uint8_t)d0); cc = tst_b(cc, d0);
            break;
        }
        case 13:                                                             /* stop */
            wr16(F(c, -0x3A00), 0); cc = (uint16_t)((cc & CC_X) | CC_Z);
            wr32(F(c, -0x3C00), a1); cc = tst_w(cc, 0);
            cc = (uint16_t)((cc & CC_X) | (a1 & 0x80000000u ? CC_N : 0) | (a1 ? 0 : CC_Z));
            goto save;
        case 14:                                                             /* mark the loop point */
            wr32(F(c, -0x3B00), a1);
            cc = (uint16_t)((cc & CC_X) | (a1 & 0x80000000u ? CC_N : 0) | (a1 ? 0 : CC_Z));
            break;
        case 15:                                                             /* loop */
            wr8(F(c, -0x2C00), 0); cc = (uint16_t)((cc & CC_X) | CC_Z);
            a1 = rd32(F(c, -0x3B00));
            break;
        default: break;                                                      /* $F0-$F7: nothing */
        }
    }
frame:
    /* a frame: duration in d0.b, then a signed word offset to the mapping */
    d1 = set_b(d1, rd8(a1++)); cc = tst_b(cc, d1);
    cc = (uint16_t)((cc & ~CC_X) | (d1 & 0x100 ? CC_X : 0));               /* lsl.w #8,d1 */
    d1 = set_w(d1, (d1 & 0xFF) << 8);
    d1 = set_b(d1, rd8(a1++));
    d1 = (uint32_t)(int32_t)(int16_t)(d1 & 0xFFFF);                          /* ext.l */
    {
        uint64_t s = (uint64_t)d1 + ANIM;                                    /* add.l a0,d1 */
        cc = (uint16_t)((s > 0xFFFFFFFFu) ? CC_X : 0);
        d1 = (uint32_t)s;
    }
    wr32(F(c, -0x3F00), d1);
    cc = tst_b(cc, d0);
    if (!(d0 & 0xFF)) {                                                      /* duration 0: hold */
        wr16(F(c, -0x3A00), 0); cc = (uint16_t)((cc & CC_X) | CC_Z);
        goto save;
    }
    d0 = set_b(d0, add_b(&cc, rd8(F(c, -0x3A00)), d0));
    wr8(F(c, -0x3A00), (uint8_t)d0); cc = tst_b(cc, d0);
    if (!(d0 & 0xFF) || (d0 & 0xFF) >= 0xF0) {
        cmp_b(&cc, 0xF0, d0);
        goto script;
    }
    cmp_b(&cc, 0xF0, d0);
save:
    wr32(F(c, -0x3C00), a1);
state:
    /* animation state bits: hide during the blink off-phase, flash, mark updated */
    d0 = set_b(d0, rd8(F(c, -0x3FFD)));
    d0 = set_b(d0, d0 & 0xFB);
    if (d0 & 0x10) {
        if (rd8(F(c, -0x4000)) & 2) wr8(F(c, -0x4000), (uint8_t)(rd8(F(c, -0x4000)) | 1));
        d0 |= 4;
    } else if (d0 & 0x40) {
        uint32_t n = sub_b(&cc, 1, rd8(F(c, -0x38FF)));
        wr8(F(c, -0x38FF), (uint8_t)n);
        if (!n) {
            d1 = set_b(d1, rd8(F(c, -0x3900)));
            int was = d0 & 0x20;
            d0 ^= 0x20;                                                      /* bchg #5,d0 */
            if (!was) {
                cc = (uint16_t)((cc & ~CC_X) | (d1 & 8 ? CC_X : 0));         /* lsr.b #4: bit 3 out last */
                d1 = set_b(d1, (d1 & 0xFF) >> 4);
            } else {
                d1 = set_b(d1, d1 & 0x0F);
                d0 |= 4;
            }
            d1 = set_b(d1, add_b(&cc, 1, d1));
            wr8(F(c, -0x38FF), (uint8_t)d1);
        } else if (!(d0 & 0x20)) d0 |= 4;
    }
    d0 = set_b(d0, d0 | 0x80);
    wr8(F(c, -0x3FFD), (uint8_t)d0);
    c->d[0] = d0; c->d[1] = d1; c->a[1] = a1;
    c->ccr = (uint16_t)((cc & CC_X) | CC_N);                                 /* move.b d0 with bit 7 set */
}

/* SetAnimNow ($7FC): animation number d0, started at once */
void SetAnimNow(Cpu *c)
{
    wr8(F(c, -0x3CFE), (uint8_t)c->d[0]);
    wr8(F(c, -0x3FFD), (uint8_t)(rd8(F(c, -0x3FFD)) | 1));
    run(c, 1, c->ccr);
}

/* UpdateAnim ($80E): start the animation if it has not started (bit 0 of -$3FFD), else advance
 * its timer and script */
void UpdateAnim(Cpu *c)
{
    uint32_t s = rd8(F(c, -0x3FFD));
    wr8(F(c, -0x3FFD), (uint8_t)(s | 1));
    run(c, !(s & 1), c->ccr);
}
