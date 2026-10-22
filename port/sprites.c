/* Sprite building: BuildSprites ($53AA), run once a frame by the main loop, and
 * AddEntitySprites ($54C4). The sprite table is built in RAM at $FF2800 and copied to VRAM
 * during VBlank. RAM used ($FFxxxx):
 *   8AE8 16 bucket tails, 8B08 16 bucket heads (entity offsets, -1 empty), by -$3CFD
 *   8B28/8B2A sprite Y window, 8B2E X limit, 8B30 sprites so far (= last link), 8B31 budget,
 *   8B32/8B33 bucket counters, 8B34 camera table (indexed by -$3E00), 8B50 tile base,
 *   8B52 end of the table, 8AE4 an extra two-sprite pair (0 = none), 8A77 bit 7 its placement
 * Formats in docs/NOTES.md (Sprites); labels are the original's addresses. */
#include "port.h"

#define F(c, disp) ea((c)->a[4], (disp))
#define SEXT(w) ((uint32_t)(int32_t)(int16_t)((w) & 0xFFFF))

void UpdateAnim(Cpu *c);
int port_wide_now(void);            /* rooms.c: widescreen's pixels beyond each side here, or 0 (world.h) */

/* AddEntitySprites ($54C4): append the pieces of entity a4's mapping to the table at a3,
 * placed relative to the camera, flipped by the entity's flip bits, tile base added (unless
 * bit 3 of -$3FFD), pieces outside the window dropped. Returns Z set when the 64-sprite budget
 * ran out (callers stop), N set when the mapping ended. */
void AddEntitySprites(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t d0 = c->d[0], d1 = c->d[1], d2 = c->d[2], d3 = c->d[3], d4 = c->d[4], d5 = c->d[5], d6 = c->d[6];
    uint32_t a0 = 0xFFFF8B34 + SEXT(rd16(F(c, -0x3E00)));                  /* camera for this entity */
    d2 = set_w(d2, sub_w(&cc, rd16(a0), rd16(F(c, -0x3800)))); a0 += 2;
    d3 = set_w(d3, sub_w(&cc, rd16(a0), rd16(F(c, -0x3700)))); a0 += 2;
    d1 = set_w(d1, rd16(F(c, -0x3FFE))); cc = tst_w(cc, d1);
    d1 = set_w(d1, d1 & 0xF800); cc = tst_w(cc, d1);                       /* flip and priority bits */
    d0 = 0; cc = tst_l(cc, 0);
    int fixed = rd8(F(c, -0x3FFD)) & 8;
    cc = (uint16_t)((cc & ~CC_Z) | (fixed ? 0 : CC_Z));
    if (!fixed) { d0 = set_w(d0, rd16(F(c, -0x3DFE))); cc = tst_w(cc, d0); }
    wr16(0xFF8B50, (uint16_t)d0); cc = tst_w(cc, d0);
    a0 = rd32(F(c, -0x3F00));
    for (;;) {
        d6 = set_b(d6, rd8(a0++)); cc = tst_b(cc, d6);                     /* $54F6: size, or end */
        if (d6 & 0x80) break;
        d6 = ext_w(d6); cc = tst_w(cc, d6);
        d5 = set_b(d5, rd8(a0++)); cc = tst_b(cc, d5);
        d5 = set_w(d5, lsl_n(&cc, d5, 8, 16));
        d5 = set_b(d5, rd8(a0++)); cc = tst_b(cc, d5);
        d5 = set_w(d5, d5 ^ d1); cc = tst_w(cc, d5);                         /* eor.w: flip the tile */
        d5 = set_w(d5, add_w(&cc, rd16(0xFF8B50), d5));
        d5 = d5 << 16 | d5 >> 16; cc = tst_l(cc, d5);
        d5 = set_b(d5, rd8(a0++)); cc = tst_b(cc, d5);                     /* x offset */
        d4 = set_b(d4, rd8(a0++)); cc = tst_b(cc, d4);                     /* y offset */
        d6 = set_w(d6, add_w(&cc, d6, d6));
        d0 = set_w(d0, d6); cc = tst_w(cc, d0);
        d0 = set_w(d0, d0 & 0x18); cc = tst_w(cc, d0);
        d0 = set_w(d0, add_w(&cc, 8, d0));                                  /* width in pixels */
        d5 = ext_w(d5); cc = tst_w(cc, d5);
        cc = (uint16_t)((cc & ~CC_Z) | (d1 & 0x800 ? 0 : CC_Z));
        if (d1 & 0x800) { d5 = set_w(d5, neg_w(&cc, d5)); d5 = set_w(d5, sub_w(&cc, d0, d5)); }
        d5 = set_w(d5, add_w(&cc, d2, d5));                                 /* $5526 */
        cmp_w(&cc, rd16(0xFF8B2E), d5);
        if (!(cc & CC_C)) continue;                                         /* off the right */
        d0 = set_w(d0, add_w(&cc, d5, d0));
        cmp_w(&cc, (uint16_t)(0x80 - port_wide_now()), d0);
        if (cc & CC_C) continue;                                            /* off the left (widescreen: of the wider view) */
        d6 = set_w(d6, lsl_n(&cc, d6, 2, 16));
        d0 = set_w(d0, d6); cc = tst_w(cc, d0);
        d0 = set_w(d0, d0 & 0x18); cc = tst_w(cc, d0);
        d0 = set_w(d0, add_w(&cc, 8, d0));                                  /* height in pixels */
        d4 = ext_w(d4); cc = tst_w(cc, d4);
        cc = (uint16_t)((cc & ~CC_Z) | (d1 & 0x1000 ? 0 : CC_Z));
        if (d1 & 0x1000) { d4 = set_w(d4, neg_w(&cc, d4)); d4 = set_w(d4, sub_w(&cc, d0, d4)); }
        d4 = set_w(d4, add_w(&cc, d3, d4));                                 /* $554C */
        cmp_w(&cc, rd16(0xFF8B2A), d4);
        if (!(cc & CC_C)) continue;                                         /* below */
        d0 = set_w(d0, add_w(&cc, d4, d0));
        cmp_w(&cc, rd16(0xFF8B28), d0);
        if (cc & CC_C) continue;                                            /* above */
        wr16(c->a[3], (uint16_t)d4); c->a[3] += 2; cc = tst_w(cc, d4);    /* y */
        d6 = set_w(d6, lsl_n(&cc, d6, 5, 16));                              /* size into the high byte */
        d6 = set_b(d6, rd8(0xFF8B30)); cc = tst_b(cc, d6);
        d6 = set_b(d6, add_b(&cc, 1, d6));                                  /* link = sprite number + 1 */
        wr8(0xFF8B30, (uint8_t)d6); cc = tst_b(cc, d6);
        wr16(c->a[3], (uint16_t)d6); c->a[3] += 2; cc = tst_w(cc, d6);
        wr32(c->a[3], d5); c->a[3] += 4; cc = tst_l(cc, d5);              /* attribute, x */
        { uint32_t f = rd8(F(c, -0x4000)) | 1; wr8(F(c, -0x4000), (uint8_t)f); cc = tst_b(cc, f); }
        uint32_t left = sub_b(&cc, 1, rd8(0xFF8B31));
        wr8(0xFF8B31, (uint8_t)left);
        if (!left) break;                                                   /* budget used up: Z set */
    }
    c->d[0] = d0; c->d[1] = d1; c->d[2] = d2; c->d[3] = d3; c->d[4] = d4; c->d[5] = d5; c->d[6] = d6;
    c->a[0] = a0; c->ccr = cc;
}

/* BuildSprites ($53AA): sort the visible entities into 16 buckets by -$3CFD (running each one's
 * animation first), then add their sprites bucket by bucket, with the extra pair from $8AE4
 * after the second bucket; end the link list. */
void BuildSprites(Cpu *c)
{
    uint32_t sp0 = c->a[7];
    uint16_t cc = c->ccr;
    c->a[0] = 0xFFFF8AE8;
    c->d[0] = 0xFFFFFFFF; c->d[1] = 15;
    do { wr32(c->a[0], c->d[0]); c->a[0] += 4; cc = tst_l(cc, c->d[0]); }
    while ((c->d[1] = set_w(c->d[1], (c->d[1] - 1) & 0xFFFF), (c->d[1] & 0xFFFF) != 0xFFFF));
    c->a[4] = 0;
    do {
        { uint32_t f = rd8(F(c, -0x4000)) & 0xFE; wr8(F(c, -0x4000), (uint8_t)f); cc = tst_b(cc, f); }
        c->d[0] = set_b(c->d[0], rd8(F(c, -0x4000))); cc = tst_b(cc, c->d[0]);
        c->d[0] = set_b(c->d[0], c->d[0] & 0xC0); cc = tst_b(cc, c->d[0]);
        cmp_b(&cc, 0xC0, c->d[0]);
        if ((c->d[0] & 0xFF) != 0xC0) goto next;                            /* not active and visible */
        {
            uint32_t s = rd8(F(c, -0x3FFD));
            wr8(F(c, -0x3FFD), (uint8_t)(s & 0x7F));
            cc = (uint16_t)((cc & ~CC_Z) | (s & 0x80 ? 0 : CC_Z));
            if (!(s & 0x80)) {                                              /* not updated this frame yet */
                wr32(sp0 - 4, 0x53DC); c->a[7] = sp0 - 4; c->ccr = cc;
                UpdateAnim(c); cc = c->ccr; c->a[7] = sp0;
                uint32_t v = rd8(F(c, -0x3FFD)) & 0x7F;
                wr8(F(c, -0x3FFD), (uint8_t)v); cc = tst_b(cc, v);
            }
        }
        cc = (uint16_t)((cc & ~CC_Z) | (rd8(F(c, -0x3FFD)) & 4 ? 0 : CC_Z));  /* $53E2 */
        if (rd8(F(c, -0x3FFD)) & 4) goto next;                              /* hidden this frame */
        wr16(F(c, -0x38FE), 0xFFFF); cc = tst_w(cc, 0xFFFF);
        c->d[0] = set_b(c->d[0], rd8(F(c, -0x3CFD))); cc = tst_b(cc, c->d[0]);
        c->d[0] = ext_w(c->d[0]); cc = tst_w(cc, c->d[0]);
        c->a[0] = 0xFFFF8AE8;
        c->d[1] = set_w(c->d[1], rd16(c->a[0] + SEXT(c->d[0]))); cc = tst_w(cc, c->d[1]);
        if (c->d[1] & 0x8000) {                                             /* empty bucket: set the head */
            wr16(c->a[0] + 0x20 + SEXT(c->d[0]), (uint16_t)c->a[4]); cc = tst_w(cc, c->a[4]);
        } else {                                                            /* link after the tail */
            c->a[1] = SEXT(c->d[1]);
            wr16(c->a[1] - 0x38FE, (uint16_t)c->a[4]); cc = tst_w(cc, c->a[4]);
        }
        wr16(c->a[0] + SEXT(c->d[0]), (uint16_t)c->a[4]); cc = tst_w(cc, c->a[4]);
next:
        c->a[4] += 4;                                                       /* $5410 */
        cmp_l(&cc, 0x100, c->a[4]);
    } while (cc & CC_C);
    wr16(0xFF8B2E, (uint16_t)(0x180 + port_wide_now())); wr16(0xFF8B28, 0x80); wr16(0xFF8B2A, 0x160);   /* widescreen: the right of the wider view */
    wr8(0xFF8B30, 0); wr8(0xFF8B31, 0x40); cc = tst_b(cc, 0x40);
    c->a[3] = 0x00FF2800; c->a[2] = 0xFFFF8B08;
    wr8(0xFF8B32, 2); wr8(0xFF8B33, 0x10); cc = tst_b(cc, 0x10);
    for (;;) {
        c->d[0] = set_w(c->d[0], rd16(c->a[2])); c->a[2] += 2; cc = tst_w(cc, c->d[0]);   /* $544C */
        while (!(c->d[0] & 0x8000)) {
            c->a[4] = SEXT(c->d[0]);                                        /* $5450 */
            wr32(sp0 - 4, 0x5454); c->a[7] = sp0 - 4; c->ccr = cc;
            AddEntitySprites(c); cc = c->ccr; c->a[7] = sp0;
            if (cc & CC_Z) goto full;                                       /* beq $54BA */
            c->d[0] = set_w(c->d[0], rd16(F(c, -0x38FE))); cc = tst_w(cc, c->d[0]);
        }
        {                                                                   /* $545C */
            uint32_t n = sub_b(&cc, 1, rd8(0xFF8B32)); wr8(0xFF8B32, (uint8_t)n);
            if (!n) {
                cmp_b(&cc, 3, rd8(0xFF8B31));
                if (cc & CC_C) goto done;
                c->d[0] = set_w(c->d[0], rd16(0xFF8AE4)); cc = tst_w(cc, c->d[0]);
                if (c->d[0] & 0xFFFF) {                                     /* the extra pair */
                    c->d[2] = set_w(c->d[2], c->d[0]); cc = tst_w(cc, c->d[0]);
                    c->d[0] = c->d[0] << 16 | c->d[0] >> 16; cc = tst_l(cc, c->d[0]);
                    c->d[0] = set_w(c->d[0], 0x300); cc = tst_w(cc, 0x300);
                    c->d[0] = set_b(c->d[0], rd8(0xFF8B30)); cc = tst_b(cc, c->d[0]);
                    c->d[0] = set_b(c->d[0], add_b(&cc, 1, c->d[0]));
                    wr32(c->a[3], c->d[0]); c->a[3] += 4; cc = tst_l(cc, c->d[0]);
                    c->d[1] = 16; wr32(c->a[3], 16); c->a[3] += 4; cc = tst_l(cc, 16);
                    c->d[0] = set_b(c->d[0], add_b(&cc, 1, c->d[0]));
                    wr32(c->a[3], c->d[0]); c->a[3] += 4; cc = tst_l(cc, c->d[0]);
                    c->d[1] = 0; wr32(c->a[3], 0); c->a[3] += 4; cc = tst_l(cc, 0);
                    wr8(0xFF8B30, (uint8_t)c->d[0]); cc = tst_b(cc, c->d[0]);
                    wr8(0xFF8B31, (uint8_t)sub_b(&cc, 2, rd8(0xFF8B31)));
                    cc = tst_b(cc, rd8(0xFF8A77));
                    if (rd8(0xFF8A77) & 0x80) { wr16(0xFF8B2A, (uint16_t)c->d[2]); cc = tst_w(cc, c->d[2]); }
                    else {                                  /* the top: below the mask (the HUD) */
                        c->d[2] = set_w(c->d[2], add_w(&cc, 0x20, c->d[2]));
                        /* widescreen: not raised, as the HUD covers only the game's width; what is
                         * above it shows at the sides (the renderer hides it within the width) */
                        if (!port_wide_now()) wr16(0xFF8B28, (uint16_t)c->d[2]);
                        cc = tst_w(cc, c->d[2]);
                    }
                }
            }
        }
        {                                                                   /* $54A8 */
            uint32_t n = sub_b(&cc, 1, rd8(0xFF8B33)); wr8(0xFF8B33, (uint8_t)n);
            if (!n) break;
        }
    }
done:                                                                       /* $54AE */
    cc = tst_b(cc, rd8(0xFF8B30));
    if (!rd8(0xFF8B30)) {
        c->d[0] = 0; cc = tst_l(cc, 0);
        wr32(c->a[3], 0); cc = tst_l(cc, 0);
        goto end;
    }
full:                                                                       /* $54BA: end the link list */
    wr8(c->a[3] - 5, 0); cc = (uint16_t)((cc & CC_X) | CC_Z);
end:
    wr32(0xFF8B52, c->a[3]); cc = tst_l(cc, c->a[3]);
    c->ccr = cc;
}
