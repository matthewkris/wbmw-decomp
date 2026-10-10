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
