/* Kernel routines: random numbers, VDP addressing, palettes.
 * Each function names the 68000 routine it replaces; comments give the instructions whose
 * side effects (registers, flags) it has to reproduce. */
#include "port.h"

/* Random ($5D8): next value of the generator in $FF8A52 (seed $2A6D365A when it is 0).
 * Out: d1 = new seed, d0 low word = (seed*41).lo + (seed*41).hi, d0 high word kept.
 * Flags from `move.l d1,($8a52)`, X from the `add.w d1,d0`. */
void Random(Cpu *c)
{
    uint32_t s = rd32(0xFF8A52);
    if (!s) s = 0x2A6D365A;
    uint32_t m = s * 41;                                  /* asl #2, add, asl #3, add */
    uint32_t lo = m & 0xFFFF, hi = m >> 16, sum = lo + hi;
    c->d[0] = set_w(s, sum);
    c->d[1] = (sum & 0xFFFF) << 16 | lo;
    wr32(0xFF8A52, c->d[1]);
    c->ccr = cc_move_l((uint16_t)((c->ccr & ~CC_X) | (sum > 0xFFFF ? CC_X : 0)), c->d[1]);
}

/* SetVRAMWrite ($5A0): d0 = VRAM address; writes the VDP command for a VRAM write and leaves
 * the command in d0. X ends clear (the last bit `lsr.w #2` shifts out is always 0). */
void SetVRAMWrite(Cpu *c)
{
    uint32_t t = c->d[0] << 2;
    t = set_w(t, ((t & 0xFFFF) >> 2) | 0x4000);
    t = (t << 16 | t >> 16);
    t = set_w(t, t & 3);
    c->d[0] = t;
    wr32(0xC00004, t);
    c->ccr = cc_move_l((uint16_t)(c->ccr & ~CC_X), t);
}

/* QueueSound ($366): put sound command d0 in the 16-entry ring at Z80 $1C30. Takes the Z80 bus,
 * waits while the driver's busy flag at Z80 $1C00 is set (releasing the bus and pausing between
 * tries), writes at ring[(head $1C2E + count $1C2F) & 15], bumps the count, releases the bus.
 * Flags from the final `move.w #0,($a11100)`; X from the `addq.b #1` on the count. */
void QueueSound(Cpu *c)
{
    c->a[0] = 0xA00000;
    for (;;) {
        wr16(0xA11100, 0x100);
        while (rd8(0xA11100) & 1) {}
        if (!rd8(0xA01C00)) break;
        wr16(0xA11100, 0);
        c->d[1] = 0x0000FFFF;                             /* moveq #31,d1; dbra to -1 */
    }
    uint16_t cc = c->ccr;
    uint32_t n = add_b(&cc, rd8(0xA01C2F), rd8(0xA01C2E));
    c->d[1] = set_w(c->d[1], 0x1C30 + (n & 0xF));       /* andi.w #$f; addi.w #$1c30 */
    wr8(0xA00000 + (c->d[1] & 0xFFFF), (uint8_t)c->d[0]);
    wr8(0xA01C2F, (uint8_t)add_b(&cc, 1, rd8(0xA01C2F)));
    wr16(0xA11100, 0);
    c->ccr = (uint16_t)((cc & CC_X) | CC_Z);
}

/* QueueSoundFF ($364): moveq #-1,d0, then QueueSound */
void QueueSoundFF(Cpu *c) { c->d[0] = 0xFFFFFFFF; QueueSound(c); }

/* SetGfxSlots ($234C): a0 = list. Fills the VRAM graphics slot list at $FF8E08 (one graphics
 * block id per 32 tiles); a1 is the write pointer and carries over from the caller.
 *   $00-$7F hi, lo   write the word hi:lo at a1
 *   $80-$BF          a1 = $FF8E08 + 2 * (n & $3F)
 *   $C0              skip a slot
 *   $C1-$F7 word     n & $3F consecutive ids from word
 *   $F8-$FE          (n & 7) + 1 empty slots ($FFFF), also in the "loaded" copy 128 bytes on
 *   $FF              end */
void SetGfxSlots(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t d0 = c->d[0], d1 = c->d[1];
    for (;;) {
        uint32_t b = rd8(c->a[0]++);
        d0 = set_b(d0, b);
        if (b < 0x80) {
            wr8(c->a[1]++, (uint8_t)b);
            wr8(c->a[1]++, rd8(c->a[0]++));
            continue;
        }
        if (b < 0xC0) {
            d0 = set_w(d0, b & 0x3F);
            d0 = set_w(d0, add_w(&cc, d0, d0));                           /* add.w d0,d0 */
            c->a[1] = 0xFFFF8E08 + (uint32_t)(int32_t)(int16_t)(d0 & 0xFFFF);
            continue;
        }
        if (b == 0xC0) { c->a[1] += 2; continue; }
        if (b == 0xFF) break;
        if (b < 0xF8) {
            d0 = set_w(d0, b & 0x3F);
            d0 = set_w(d0, sub_w(&cc, 1, d0));                            /* subq.w #1,d0 */
            d1 = set_b(d1, rd8(c->a[0]++));
            cc = (uint16_t)((cc & ~CC_X) | (d1 & 0x100 ? CC_X : 0));     /* lsl.w #8: bit 8 goes out last */
            d1 = set_w(d1, (d1 & 0xFF) << 8);
            d1 = set_b(d1, rd8(c->a[0]++));
            for (;;) {
                wr16(c->a[1], (uint16_t)d1); c->a[1] += 2;
                d1 = set_w(d1, add_w(&cc, 1, d1));                        /* addq.w #1,d1 */
                d0 = set_w(d0, (d0 - 1) & 0xFFFF);                        /* dbra */
                if ((d0 & 0xFFFF) == 0xFFFF) break;
            }
            continue;
        }
        d0 = set_w(d0, b & 7);
        d1 = 0xFFFFFFFF;                                                  /* moveq #-1,d1 */
        for (;;) {
            wr16(c->a[1] + 0x80, 0xFFFF);
            wr16(c->a[1], 0xFFFF); c->a[1] += 2;
            d0 = set_w(d0, (d0 - 1) & 0xFFFF);
            if ((d0 & 0xFFFF) == 0xFFFF) break;
        }
    }
    c->d[0] = d0; c->d[1] = d1;
    c->ccr = (uint16_t)((cc & CC_X) | CC_Z);                              /* cmpi.b #$ff,d0: equal */
}

/* UnpackPalette ($1370): d0 = record number, a1 = destination. Writes colours 1-15 of one
 * palette line as CRAM words: record = 17 bytes at $599C + 17 * d0, a word of high blue bits
 * (LSB first) then 15 bytes BBGGGRRR. */
void UnpackPalette(Cpu *c)
{
    uint32_t d0 = set_w(c->d[0], (c->d[0] & 0xFFFF) * 17) + 0x599C;
    uint32_t a2 = d0;
    uint32_t d3 = set_w(c->d[3], (uint32_t)rd8(a2) << 8 | rd8(a2 + 1));
    a2 += 2;
    uint32_t d1 = 0, d2 = c->d[2], col = 0, x = 0;
    for (int i = 0; i < 15; i++) {
        uint32_t b = rd8(a2++);
        d2 = set_b(d2, b);
        col = (b & 7) << 1;
        d1 = (b & 0x38) << 2;
        col |= d1;
        d2 = set_w(d2, (b & 0xC0) << 3);
        col |= d2 & 0xFFFF;
        x = d3 & 1;                                       /* lsr.w #1,d3: the bit out goes to X and C */
        d3 = set_w(d3, (d3 & 0xFFFF) >> 1);
        if (x) col |= 0x800;
        wr16(c->a[1], (uint16_t)col);
        c->a[1] += 2;
    }
    c->d[0] = col; c->d[1] = d1; c->d[2] = d2; c->d[3] = d3;
    c->d[4] = 0x0000FFFF;                                 /* moveq #14,d4 then dbra to -1 */
    c->a[2] = a2;
    c->ccr = cc_move_w((uint16_t)((c->ccr & ~CC_X) | (x ? CC_X : 0)), col);
}

/* LoadPalettes ($135C): a0 = four record numbers, one per palette line; fills the target
 * palette at $FF8B56 from colour 1 of line 0 ($FF8B58), skipping each line's colour 0. */
void LoadPalettes(Cpu *c)
{
    c->a[1] = 0xFFFF8B58;                                 /* lea ($ffff8b58).w,a1: sign-extended */
    for (int line = 0; line < 4; line++) {
        c->d[0] = rd8(c->a[0]++);                         /* moveq #0,d0; move.b (a0)+,d0 */
        stack_residue(c, 0x1368);                         /* bsr.s UnpackPalette from $1366 */
        UnpackPalette(c);
        c->a[1] += 2;
    }
    c->d[5] = 0x0000FFFF;                                 /* moveq #3,d5 then dbra to -1 */
}
