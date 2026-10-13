/* The message window and its helpers, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The
 * messages").
 *
 * Conversations, shops, signs and the "Found ..." boxes all go through the message
 * interpreter ($13BC). It draws in a window on plane A, whose place and size come from the
 * message's header ($276C, $27B0: column $8D92, row $8D94, inner width $8C9E and height $8CA0
 * in lines of two rows); characters are written with $2050 (column d6, row d7 inside the
 * window, scrolled with the plane: $8D9A, $8D9C). The interpreter itself waits for frames and
 * comes later; these are the routines under it, ported register for register with every
 * register and flag in the contract, as the status screen's are.
 *
 * Batch T5: the window (frame, characters, scrolling, the map redrawn behind it), its blinking
 * marks, numbers, names, the "Found" boxes, prices, and the things owned (testing, giving,
 * taking). */
#include "world.h"

#define VDP_DATA      0x00C00000u
#define VDP_CTRL      0x00C00004u
#define DATA_DIR      0x0001CC00u   /* the ROM's directory of data tables */
#define WIN_COL       0xFFFF8D92u   /* word: the window's column on the plane (in tiles) */
#define WIN_ROW       0xFFFF8D94u   /* word: its row */
#define WIN_W         0xFFFF8D96u   /* word: its width with the frame */
#define WIN_H         0xFFFF8D98u   /* word: its height with the frame */
#define WIN_X         0xFFFF8D9Au   /* word: its column on screen, scrolled (mod 64) */
#define WIN_Y         0xFFFF8D9Cu   /* word: its row on screen, scrolled (mod 32) */
#define TEXT_W        0xFFFF8C9Eu   /* word: the text's width */
#define TEXT_LINES    0xFFFF8CA0u   /* word: its lines */
#define TEXT_LINE     0xFFFF8C9Cu   /* word: the line being written */
#define TEXT_COL      0xFFFF8C9Au   /* word: the column being written */
#define MARKS         0xFFFF8CA5u   /* byte: bit 0 the "more" arrow shown, 2 the choice cursor, 1 the second choice */
#define MARK_COUNT    0xFFFF8CAAu   /* byte: frames to the marks' next blink */
#define NAME_LEN      0xFFFF8CDEu
#define NAME_TOP      0xFFFF8CE0u
#define BOX           0xFFFF8D12u   /* the "Found" box: two rows of 32 tiles */
#define SCROLL_X      0xFFFF9758u   /* word: plane A's scroll, in pixels */
#define SCROLL_Y      0xFFFF975Au

static Cpu *c;

/* the flags as the 68000's moves leave them (N, Z; V and C clear; X kept) */
static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static void f_bit(int on) { c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (on ? 0 : CC_Z)); }   /* btst, bset, bclr */
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }
static uint32_t swap32(uint32_t v) { uint32_t r = v << 16 | v >> 16; F_L(r); return r; }
static void cmp_byte(uint32_t s, uint32_t d) { uint16_t x = c->ccr & CC_X; sub_b(&c->ccr, s, d); c->ccr = (uint16_t)((c->ccr & ~CC_X) | x); }
static void cmp_word(uint32_t s, uint32_t d) { cmp_w(&c->ccr, s, d); }
static int mem_bit(uint32_t a, int bit) { int on = rd8(a) >> (bit & 7) & 1; f_bit(on); return on; }
static int mem_bclr(uint32_t a, int bit) { int on = mem_bit(a, bit); wr8(a, rd8(a) & ~(1 << (bit & 7))); return on; }
static int mem_bset(uint32_t a, int bit) { int on = mem_bit(a, bit); wr8(a, rd8(a) | 1 << (bit & 7)); return on; }
static uint8_t mem_subq_b(uint32_t a, uint32_t n) { uint8_t v = (uint8_t)sub_b(&c->ccr, n, rd8(a)); wr8(a, v); return v; }
static uint16_t mem_addq_w(uint32_t a, uint32_t n) { uint16_t v = (uint16_t)add_w(&c->ccr, n, rd16(a)); wr16(a, v); return v; }
static uint16_t mem_subq_w(uint32_t a, uint32_t n) { uint16_t v = (uint16_t)sub_w(&c->ccr, n, rd16(a)); wr16(a, v); return v; }
static void f_clr_z(void) { c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); }   /* clr */

/* $183E: d1 as decimal digits ending at a1 (a 0 there); a1 ends at the first digit */
static void num_text(void)
{
    wr8(c->a[1], 0);
    c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    do {
        c->d[0] = 0;
        for (c->d[2] = 31; ; ) {
            uint32_t x = c->d[1] >> 31;
            c->d[1] <<= 1;
            c->d[0] = set_b(c->d[0], (c->d[0] << 1) + x);
            if ((c->d[0] & 0xFF) >= 10) {
                c->d[0] = set_b(c->d[0], c->d[0] - 10);
                c->d[1] = set_b(c->d[1], c->d[1] + 1);
            }
            c->d[2] = set_w(c->d[2], c->d[2] - 1);
            if ((c->d[2] & 0xFFFF) == 0xFFFF) break;
        }
        c->d[0] = set_b(c->d[0], add_b(&c->ccr, 0x30, c->d[0]));
        wr8(--c->a[1], (uint8_t)c->d[0]);
        F_L(c->d[1]);
    } while (c->d[1]);
}

/* $2050: the tile d0 at column d6, row d7 of the window, on the plane as scrolled */
static void char_at(void)
{
    c->d[1] = set_w(c->d[1], ((c->d[7] + rd16(WIN_Y)) & 0x1F) << 6);
    c->d[2] = set_w(c->d[2], (c->d[6] + rd16(WIN_X)) & 0x3F);
    c->d[1] = set_w(c->d[1], ((c->d[1] + c->d[2]) * 2) | 0x4000);
    c->d[1] = (c->d[1] & 0xFFFF) << 16 | 3;
    wr32(VDP_CTRL, c->d[1]);
    wr16(VDP_DATA, (uint16_t)c->d[0]);
    c->ccr = (uint16_t)(((c->d[0] & 0x8000) ? CC_N : 0) | ((c->d[0] & 0xFFFF) ? 0 : CC_Z));   /* X: the adds, no carry */
}

/* $21E8: a row of the frame at row d7: d0 at the left, d5's low word inside, its high word at
 * the right */
static void window_row(void)
{
    c->d[6] = 0;
    char_at();
    c->d[6] = set_w(c->d[6], c->d[6] + 1);
    c->d[3] = set_w(c->d[3], rd16(WIN_W) - 3);
    for (;;) {
        c->d[0] = set_w(c->d[0], c->d[5]);
        char_at();
        c->d[6] = set_w(c->d[6], c->d[6] + 1);
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
        if ((c->d[3] & 0xFFFF) == 0xFFFF) break;
    }
    c->d[5] = c->d[5] << 16 | c->d[5] >> 16;
    c->d[0] = set_w(c->d[0], c->d[5]);
    char_at();
}

/* $2082: the text scrolled up a line: $8DA4 rows of $8DA2 tiles from column $8D9E, row $8DA0,
 * each read from $8DA6 rows below and written back (through VRAM reads into $FF8DA8) */
static void scroll_window(void)
{
    c->a[7] -= 4; wr32(c->a[7], c->a[2]);
    c->a[0] = VDP_DATA; c->a[1] = VDP_CTRL;
    c->d[4] = set_w(c->d[4], rd16(0xFFFF8DA6u) << 7);
    c->d[7] = set_w(c->d[7], rd16(0xFFFF8DA0u));
    c->d[6] = set_w(c->d[6], rd16(0xFFFF8DA4u));
    do {
        c->d[3] = set_w(c->d[3], ((c->d[7] + rd16(WIN_Y)) & 0x1F) << 7);
        c->d[2] = set_w(c->d[2], ((rd16(0xFFFF8D9Eu) + rd16(WIN_X)) * 2) | 0x4000);
        c->d[5] = set_w(c->d[5], rd16(0xFFFF8DA2u));
        c->d[1] = set_w(c->d[1], c->d[2]);
        c->a[2] = 0xFFFF8DA8u;
        do {
            c->d[2] = set_w(c->d[2], c->d[2] & 0x7E);
            do c->d[0] = set_w(c->d[0], rd16(c->a[1])); while (!(c->d[0] & 0x200));
            c->d[0] = set_w(c->d[0], ((c->d[3] + c->d[4]) | c->d[2]) & 0xFFE);
            c->d[0] = (c->d[0] & 0xFFFF) << 16 | 3;
            wr32(c->a[1], c->d[0]);
            wr16(c->a[2], rd16(c->a[0])); c->a[2] += 2;
            c->d[2] = set_w(c->d[2], c->d[2] + 2);
            c->d[5] = set_w(c->d[5], c->d[5] - 1);
        } while (c->d[5] & 0xFFFF);
        c->d[5] = set_w(c->d[5], rd16(0xFFFF8DA2u));
        c->a[2] = 0xFFFF8DA8u;
        do {
            c->d[1] = set_w(c->d[1], c->d[1] & 0x407E);
            c->d[0] = set_w(c->d[0], c->d[3] | c->d[1]);
            c->d[0] = (c->d[0] & 0xFFFF) << 16 | 3;
            wr32(c->a[1], c->d[0]);
            wr16(c->a[0], rd16(c->a[2])); c->a[2] += 2;
            c->d[1] = set_w(c->d[1], c->d[1] + 2);
            c->d[5] = set_w(c->d[5], c->d[5] - 1);
        } while (c->d[5] & 0xFFFF);
        c->d[7] = set_w(c->d[7], c->d[7] + 1);
        c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    } while (c->d[6] & 0xFFFF);
    c->a[2] = rd32(c->a[7]); c->a[7] += 4;
}

/* $2110: the window's frame drawn where the plane is scrolled to, and the sprites it covers
 * hidden (in front, animated: their ATTR bit 7 off, ALIVE bit 2 on, $225C brings them back) */
static void window_frame(void)
{
    wr16(WIN_X, (uint16_t)(((rd16(SCROLL_X) >> 3) + rd16(WIN_COL)) & 0x3F));
    wr16(WIN_Y, (uint16_t)(((rd16(SCROLL_Y) >> 3) + rd16(WIN_ROW)) & 0x1F));
    c->d[7] = 0;
    c->d[0] = set_w(c->d[0], 0x8014); c->d[5] = 0x88148015u;
    window_row();
    c->d[7] = 1;
    c->d[4] = set_w(c->d[4], rd16(WIN_H) - 3);
    for (;;) {
        c->d[0] = set_w(c->d[0], 0x8016); c->d[5] = 0x88168020u;
        window_row();
        c->d[7] = set_w(c->d[7], c->d[7] + 1);
        c->d[4] = set_w(c->d[4], c->d[4] - 1);
        if ((c->d[4] & 0xFFFF) == 0xFFFF) break;
    }
    c->d[0] = set_w(c->d[0], 0x9014); c->d[5] = 0x98149015u;
    window_row();
    c->d[4] = set_w(c->d[4], (rd16(WIN_COL) << 3) + rd16(SCROLL_X));
    c->d[6] = set_w(c->d[6], rd16(WIN_W) << 3);
    c->d[5] = set_w(c->d[5], (rd16(WIN_ROW) << 3) + rd16(SCROLL_Y));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, rd16(WIN_H), 3, 16));
    c->a[4] = 0;
    c->d[1] = 0;
    for (c->d[3] = 63; ; ) {
        uint32_t e = c->a[4];
        F_B(rd8(e - 0x4000));
        if (rd8(e - 0x4000) & 0x80) {
            F_B(rd8(e - 0x3FFE));
            if (rd8(e - 0x3FFE) & 0x80) {
                cmp_byte(4, rd8(e - 0x3CFD));
                if (rd8(e - 0x3CFD) >= 4) {
                    c->d[0] = set_w(c->d[0], rd16(e - 0x3800)); F_W(c->d[0]);
                    c->d[1] = set_b(c->d[1], rd8(e - 0x34FE)); F_B(c->d[1]);
                    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
                    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[4], c->d[0]));
                    if (!(c->ccr & (CC_C | CC_Z))) {
                        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
                        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
                        cmp_word(c->d[6], c->d[0]);
                        if ((int16_t)c->d[0] < (int16_t)c->d[6]) {
                            c->d[0] = set_w(c->d[0], rd16(e - 0x3700)); F_W(c->d[0]);
                            c->d[1] = set_b(c->d[1], rd8(e - 0x34FD)); F_B(c->d[1]);
                            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
                            c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[5], c->d[0]));
                            if (!(c->ccr & (CC_C | CC_Z))) {
                                c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
                                c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
                                cmp_word(c->d[7], c->d[0]);
                                if ((int16_t)c->d[0] < (int16_t)c->d[7]) {
                                    wr8(e - 0x3FFE, rd8(e - 0x3FFE) & 0x7F);
                                    wr8(e - 0x4000, rd8(e - 0x4000) | 4); F_B(rd8(e - 0x4000));
                                }
                            }
                        }
                    }
                }
            }
        }
        c->a[4] += 4;
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
        if ((c->d[3] & 0xFFFF) == 0xFFFF) break;
    }
}

/* $220A: the map's tile under window column d6, row d7 (MapBlockAt, the block's quarter) */
static void tile_under(void)
{
    c->a[3] = 0xFFFF9754u;
    c->d[1] = 0;
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, rd16(c->a[3] + 6), 3, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(WIN_ROW), c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[7], c->d[0]));
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 1, 16));
    uint32_t x = c->ccr & CC_X ? 1 : 0;
    c->d[1] = set_w(c->d[1], (c->d[1] << 1) | x);          /* roxl.w #1,d1 */
    c->d[0] = set_w(c->d[0], c->d[0] << 6);
    c->d[7] = c->d[7] << 16 | c->d[7] >> 16;
    c->d[7] = set_w(c->d[7], c->d[0]);
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, rd16(c->a[3] + 4), 3, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(WIN_COL), c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[6], c->d[0]));
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 1, 16));
    x = c->ccr & CC_X ? 1 : 0;
    c->d[1] = set_w(c->d[1], (c->d[1] << 2) | (x << 1));   /* roxl.w #2,d1 (d1 is 0 or 1) */
    c->ccr &= ~CC_X;
    c->d[6] = c->d[6] << 16 | c->d[6] >> 16;
    c->d[6] = set_w(c->d[6], c->d[0]);
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->d[1]);
    call_68k(c, 0x2542);                                    /* MapBlockAt */
    c->d[1] = c->d[0] & 0xFF;
    c->d[0] = set_w(c->d[0], c->d[0] & 0x300);
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 6, 16));
    c->a[0] = rd32(c->a[3] + 0x10 + sx(c->d[0]));
    c->d[1] = set_w(c->d[1], c->d[1] << 3);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(c->a[7]), c->d[1])); c->a[7] += 2;
    c->d[0] = set_w(c->d[0], rd16(c->a[0] + sx(c->d[1])));
    c->d[6] = c->d[6] << 16 | c->d[6] >> 16;
    c->d[7] = swap32(c->d[7]);
}

/* $225C: the map redrawn under the whole window; the sprites it hid shown again; then, if
 * $8A51 bit 0, $DE6A */
static void window_restore(void)
{
    c->d[7] = 0;
    do {
        c->d[6] = 0;
        do {
            tile_under();
            char_at();
            c->d[6] = set_w(c->d[6], c->d[6] + 1);
            cmp_word(rd16(WIN_W), c->d[6]);
        } while ((c->d[6] & 0xFFFF) < rd16(WIN_W));
        c->d[7] = set_w(c->d[7], c->d[7] + 1);
        cmp_word(rd16(WIN_H), c->d[7]);
    } while ((c->d[7] & 0xFFFF) < rd16(WIN_H));
    c->a[4] = 0;
    for (c->d[0] = 64; ; ) {
        F_B(rd8(c->a[4] - 0x4000));
        if ((rd8(c->a[4] - 0x4000) & 0x80) && mem_bclr(c->a[4] - 0x4000, 2)) {
            wr8(c->a[4] - 0x3FFE, rd8(c->a[4] - 0x3FFE) | 0x80); F_B(rd8(c->a[4] - 0x3FFE));
        }
        c->a[4] += 4;
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
        if ((c->d[0] & 0xFFFF) == 0xFFFF) break;
    }
    if (mem_bit(0xFFFF8A51u, 0)) call_68k(c, 0xDE6A);
}

/* $16C6: a new line: the next, or at the last the text scrolled up ($2082) and the last line
 * blanked */
static void new_line(void)
{
    wr16(TEXT_COL, 0);
    mem_addq_w(TEXT_LINE, 1);
    c->d[7] = set_w(c->d[7], rd16(TEXT_LINE)); F_W(c->d[7]);
    cmp_word(rd16(TEXT_LINES), c->d[7]);
    if ((c->d[7] & 0xFFFF) < rd16(TEXT_LINES)) return;
    wr16(TEXT_LINE, (uint16_t)sub_w(&c->ccr, 1, rd16(TEXT_LINE)));
    c->d[0] = set_w(c->d[0], rd16(TEXT_LINES));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    if (c->d[0] & 0xFFFF) {
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
        wr16(0xFFFF8DA4u, (uint16_t)c->d[0]);
        wr16(0xFFFF8DA2u, rd16(TEXT_W));
        wr16(0xFFFF8D9Eu, 1); wr16(0xFFFF8DA0u, 1); wr16(0xFFFF8DA6u, 2);
        scroll_window();
    }
    c->d[7] = set_w(c->d[7], rd16(TEXT_LINES));
    c->d[7] = set_w(c->d[7], sub_w(&c->ccr, 1, c->d[7]));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[7], c->d[7]));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
    c->d[0] = set_w(c->d[0], 0x8020);
    for (c->d[4] = 1; ; ) {
        c->d[3] = set_w(c->d[3], rd16(TEXT_W));
        c->d[6] = 1;
        do {
            char_at();
            c->d[6] = set_w(c->d[6], c->d[6] + 1);
            c->d[3] = set_w(c->d[3], sub_w(&c->ccr, 1, c->d[3]));
        } while (c->d[3] & 0xFFFF);
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
        c->d[4] = set_w(c->d[4], c->d[4] - 1);
        if ((c->d[4] & 0xFFFF) == 0xFFFF) break;
    }
}

/* the "more" arrow ($9013) under the text's middle: $173C hides it, $1754 shows it, $172E
 * blinks it every 20 frames */
static void more_draw(uint32_t tile)
{
    c->d[0] = set_w(c->d[0], tile);
    wr8(MARK_COUNT, 0x14);
    c->d[7] = set_w(c->d[7], rd16(TEXT_LINES));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[7], c->d[7]));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
    c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, rd16(TEXT_W), 1, 16));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    char_at();
}
static void more_hide(void) { if (mem_bclr(MARKS, 0)) more_draw(0x8020); }
static void more_show(void) { more_draw(0x9013); }
static void more_blink(void)
{
    if (mem_subq_b(MARK_COUNT, 1)) return;
    if (mem_bit(MARKS, 0)) { more_hide(); return; }
    if (mem_bset(MARKS, 0)) return;
    more_show();
}

/* the choice cursor ($87B5) before the choice ($8CAC, or $8CAD when MARKS bit 1) on the line:
 * $1782 hides it, $1790 shows it, $1774 blinks it */
static void choice_draw(uint32_t tile)
{
    c->d[0] = set_w(c->d[0], tile);
    wr8(MARK_COUNT, 0x14);
    c->d[7] = set_w(c->d[7], rd16(TEXT_LINE));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[7], c->d[7]));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 2, c->d[7]));
    c->d[6] = 0;
    c->d[6] = set_b(c->d[6], rd8(0xFFFF8CACu));
    if (mem_bit(MARKS, 1)) c->d[6] = set_b(c->d[6], rd8(0xFFFF8CADu));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    char_at();
}
static void choice_hide(void) { if (mem_bclr(MARKS, 2)) choice_draw(0x8020); }
static void choice_show(void) { if (!mem_bset(MARKS, 2)) choice_draw(0x87B5); }
static void choice_blink(void)
{
    if (mem_subq_b(MARK_COUNT, 1)) return;
    if (mem_bit(MARKS, 2)) choice_hide(); else choice_show();
}

/* $1D92: the price of thing d0 ($60-$65 are $20-$25's) from $1DAE */
static void price_of(void)
{
    c->d[0] = set_w(c->d[0], c->d[0] & 0xFF);
    if ((c->d[0] & 0xFF) >= 0x60 && (c->d[0] & 0xFF) < 0x66) c->d[0] = set_w(c->d[0], c->d[0] - 0x40);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    c->d[0] = rd32(0x1DAEu + sx(c->d[0]));
    F_L(c->d[0]);
}

/* $1ED0: a2 the text of thing d0 in table d3 (0-3) of the names ($1CC10) */
static void name_text(void)
{
    c->d[3] = set_w(c->d[3], c->d[3] & 3);
    c->d[3] = set_w(c->d[3], lsl_n(&c->ccr, c->d[3], 2, 16));
    c->a[2] = rd32(DATA_DIR + 0x10);
    c->a[2] = rd32(c->a[2] + sx(c->d[3]));
    c->d[0] = set_w(c->d[0], c->d[0] & 0xFF);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[2] += sx(rd16(c->a[2] + sx(c->d[0])));
}

/* $1E6A: the name of thing d0 (table 1) into NAME_TOP, two rows of characters (the marks
 * above in its second half), NAME_LEN long. A name can include others (bytes $0C-$0F: a table,
 * then the thing): they nest through the stack of texts at $FF8D00 */
static void name_of(void)
{
    wr16(0xFFFF8D10u, 0);
    wr16(NAME_LEN, 0);
    c->a[3] = NAME_TOP;
    c->d[3] = 13;
    name_text();
    for (;;) {
        c->d[3] = set_b(c->d[3], rd8(c->a[2]++)); F_B(c->d[3]);
        if (!(c->d[3] & 0xFF)) {
            c->d[0] = set_w(c->d[0], rd16(0xFFFF8D10u)); F_W(c->d[0]);
            if (!(c->d[0] & 0xFFFF)) return;
            c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 4, c->d[0]));
            c->a[0] = sx(c->d[0]);
            c->a[2] = rd32(c->a[0] - 0x7300);
            continue;
        }
        cmp_byte(0x10, c->d[3]);
        if ((c->d[3] & 0xFF) >= 0x10) {
            c->d[2] = 32;
            cmp_byte(0xFE, c->d[3]);
            if ((c->d[3] & 0xFF) >= 0xFE) {
                c->d[3] = set_b(c->d[3], sub_b(&c->ccr, 0x40, c->d[3]));
                c->d[2] = set_b(c->d[2], c->d[3]);
                c->d[3] = set_b(c->d[3], rd8(c->a[2]++));
            }
            wr8(c->a[3] + 0x10, (uint8_t)c->d[3]);
            wr8(c->a[3]++, (uint8_t)c->d[2]);
            mem_addq_w(NAME_LEN, 1);
            continue;
        }
        if ((c->d[3] & 0xFF) < 0x0C) for (;;) { }    /* the original hangs on a bad name ($1E9C) */
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        c->a[0] = sx(rd16(0xFFFF8D10u));
        wr32(c->a[0] - 0x7300, c->a[2]);
        c->a[0] += 4;
        wr16(0xFFFF8D10u, (uint16_t)c->a[0]);
        name_text();
    }
}

/* $1F8E: the text at a0 into the box at a2 (characters d2's high byte, the marks row $40 below);
 * bytes $FE, $FF set d2 for the next character */
static void box_text(void)
{
    c->d[2] = set_w(c->d[2], c->d[1]);
    for (;;) {
        c->d[0] = set_b(c->d[0], rd8(c->a[0]++)); F_B(c->d[0]);
        if (!(c->d[0] & 0xFF)) return;
        cmp_byte(0xFE, c->d[0]);
        if ((c->d[0] & 0xFF) >= 0xFE) {
            c->d[2] = set_b(c->d[2], c->d[0]);
            c->d[2] = set_b(c->d[2], sub_b(&c->ccr, 0x40, c->d[2]));
            continue;
        }
        wr16(c->a[2] + 0x40, (uint16_t)c->d[0]);
        wr16(c->a[2], (uint16_t)c->d[2]); c->a[2] += 2;
        c->d[2] = set_w(c->d[2], c->d[1]);
    }
}

/* $1FBC: a box row: d3, 30 of d2, d3 (at a0); $1FAC both rows, blank between the edges */
static void box_row(void)
{
    wr16(c->a[0], (uint16_t)c->d[3]); c->a[0] += 2;
    for (c->d[1] = 29; ; ) {
        wr16(c->a[0], (uint16_t)c->d[2]); c->a[0] += 2;
        c->d[1] = set_w(c->d[1], c->d[1] - 1);
        if ((c->d[1] & 0xFFFF) == 0xFFFF) break;
    }
    wr16(c->a[0], (uint16_t)c->d[3]); c->a[0] += 2;
    F_W(c->d[3]);
}
static void box_rows(void)
{
    c->a[0] = BOX;
    c->d[2] = set_w(c->d[2], 0x8020);
    c->d[3] = set_w(c->d[3], 0x87A1);
    box_row();
    box_row();
}

/* $1EEC: "Found the <name of thing d0>." centred in the box */
static void found_thing(void)
{
    name_of();
    box_rows();
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 6, rd16(NAME_LEN)));
    c->d[1] = 28;
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, c->d[0], c->d[1]));
    c->d[1] = set_w(c->d[1], c->d[1] & 0xFFFE);
    c->a[2] = BOX + sx(c->d[1]);
    c->d[1] = set_w(c->d[1], 0x8020);
    c->d[0] = set_w(c->d[0], c->d[1]);
    c->a[0] = 0x1FE6u;
    box_text();
    c->a[0] = NAME_TOP;
    c->d[0] = set_w(c->d[0], 0xA000);
    c->d[1] = set_w(c->d[1], rd16(NAME_LEN)); F_W(c->d[1]);
    while (c->d[1] & 0xFFFF) {
        c->d[0] = set_b(c->d[0], rd8(c->a[0] + 0x10));
        wr16(c->a[2] + 0x40, (uint16_t)c->d[0]);
        c->d[0] = set_b(c->d[0], rd8(c->a[0]++));
        wr16(c->a[2], (uint16_t)c->d[0]); c->a[2] += 2;
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 1, c->d[1]));
    }
    c->d[1] = set_w(c->d[1], 0x8020);
    c->d[0] = set_w(c->d[0], c->d[1]);
    c->a[0] = 0x1FFFu;
    box_text();
}

/* $1F46: "Found <d0> GOLD." centred in the box (the digits in a link frame) */
static void found_gold(void)
{
    uint32_t sp = c->a[7] - 4;
    wr32(sp, c->a[6]); c->a[6] = sp; c->a[7] = sp - 12;
    box_rows();
    c->d[1] = c->d[0];
    c->a[1] = c->a[6] - 2;
    c->a[0] = c->a[1];
    num_text();
    c->a[0] -= c->a[1];
    c->d[0] = set_w(c->d[0], c->a[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 5, c->d[0]));
    c->d[1] = 26;
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, c->d[0], c->d[1]));
    c->d[1] = set_w(c->d[1], c->d[1] & 0xFFFE);
    c->a[2] = BOX + sx(c->d[1]);
    c->d[1] = set_w(c->d[1], 0x8020);
    c->d[0] = set_w(c->d[0], c->d[1]);
    c->a[0] = 0x1FF1u; box_text();
    c->a[0] = c->a[1]; box_text();
    c->a[0] = 0x1FF8u; box_text();
    c->a[7] = c->a[6]; c->a[6] = rd32(c->a[7]); c->a[7] += 4;
}

/* $1FD4: the box shown ($7EEE) for a second */
static void found_show(void)
{
    c->a[0] = BOX;
    call_68k(c, 0x7EEE);
    wr16(0xFFFF965Cu, 0x3C); F_W(0x3C);
}
static void show_found_gold(void)                   /* $1FCA: its bsr's return on the stack, as the frame (and a1) sit under it */
{
    c->a[7] -= 4; wr32(c->a[7], 0x1FCEu);
    found_gold();
    c->a[7] += 4;
    found_show();
}
static void show_found(void)      { found_thing(); found_show(); }   /* $1FD0 */

/* $276C: message d0's text (a pointer at $8C7A) and window (a1), from the tables at $1CC14 and
 * $1CC18 */
static void message_data(void)
{
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[0] = set_w(c->d[0], c->d[0] * 2);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
    c->a[1] = rd32(DATA_DIR + 0x14);
    c->d[1] = set_b(c->d[1], rd8(c->a[1] + 1 + sx(c->d[0])));
    c->d[1] = set_w(c->d[1], c->d[1] << 8);
    c->d[1] = set_b(c->d[1], rd8(c->a[1] + 2 + sx(c->d[0])));
    c->a[0] = c->a[1] + sx(c->d[1]);
    wr32(0xFFFF8C7Au, c->a[0]);
    c->d[1] = rd8(c->a[1] + sx(c->d[0]));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 2, 16));
    c->a[1] = rd32(DATA_DIR + 0x18) + sx(c->d[1]);
}

/* $2002: the window's place and size from a1, for drawing without its frame */
static void window_geometry(void)
{
    message_data();
    c->d[0] = rd8(c->a[1]++);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    wr16(WIN_COL, (uint16_t)c->d[0]); wr16(WIN_X, (uint16_t)c->d[0]);
    c->d[0] = rd8(c->a[1]++);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    wr16(WIN_ROW, (uint16_t)c->d[0]); wr16(WIN_Y, (uint16_t)c->d[0]);
    c->d[0] = rd8(c->a[1]++);
    wr16(TEXT_W, (uint16_t)c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[1]++));
    wr16(TEXT_LINES, (uint16_t)c->d[0]);
    wr8(0xFFFF8CA4u, 0x80); F_B(0x80);
}

/* $27B0: the window from a1 (column, row, width, lines) opened: its frame drawn */
static void window_at(void)
{
    F_W(rd16(0xFFFF965Cu));
    if (rd16(0xFFFF965Cu)) call_68k(c, 0x7F00);
    c->d[0] = 0;
    c->d[0] = set_b(c->d[0], rd8(c->a[1]++)); wr16(WIN_COL, (uint16_t)c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[1]++)); wr16(WIN_ROW, (uint16_t)c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[1]++)); wr16(TEXT_W, (uint16_t)c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 2, c->d[0])); wr16(WIN_W, (uint16_t)c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[1]++)); wr16(TEXT_LINES, (uint16_t)c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 3, c->d[0])); wr16(WIN_H, (uint16_t)c->d[0]);
    window_frame();
}

/* $279C: message d0's window opened, with its sound (79) */
static void open_window(void)
{
    message_data();
    c->d[0] = 79;
    call_68k(c, 0x366);
    window_at();
    wr8(0xFFFF8CA4u, 0x80); F_B(0x80);
}

/* $203A: in the cheaper towns ($8A8B), a price d0 times 5 / 6 */
static void cheaper(void)
{
    F_B(rd8(0xFFFF8A8Bu));
    if (!rd8(0xFFFF8A8Bu)) return;
    c->d[1] = c->d[0] & 0xFF;
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 2, 16));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[0], c->d[1]));
    uint32_t t = c->d[0]; c->d[0] = c->d[1]; c->d[1] = t;
    c->d[1] = 6;
    uint32_t q = c->d[0] / 6, r = c->d[0] % 6;
    c->d[0] = (r << 16) | (q & 0xFFFF);
    c->ccr = (uint16_t)((c->ccr & CC_X) | (q & 0x8000 ? CC_N : 0) | ((q & 0xFFFF) ? 0 : CC_Z));
}

/* things owned. Thing numbers: under $20 a bit of $95CA (the swords, armour, shields, boots
 * owned); $20-$25 a special action's count ($8A04); $26-$27 the slots' ($8A3A); $28-$5F an item's
 * count at $958C ($2510; $2B-$2E the potions' level in one cell, 18); $60 up a game flag. The
 * thresholds ($60, $28, $26, $20) are the bytes at $24D8, which is also a bra.s */
static void item_cell(void)                         /* $2510: a0 its count's cell */
{
    c->a[0] = 0xFFFF958Cu;
    cmp_byte(0x29, c->d[0]);
    if ((c->d[0] & 0xFF) < 0x29) c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 0x1C, c->d[0]));
    else {
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 0x30, c->d[0]));
        if (c->ccr & CC_C) {
            c->d[0] = set_b(c->d[0], add_b(&c->ccr, 0x17, c->d[0]));
            cmp_byte(0x12, c->d[0]);
            if ((c->d[0] & 0xFF) >= 0x12) { c->d[0] = 18; F_L(18); }
        }
    }
    c->a[0] += sx(c->d[0]);
}
static void flag_cell(void)                         /* $2536: a0 its bit's byte */
{
    c->a[0] = 0xFFFF95CAu;
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[1]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 3, 16));
    c->a[0] += sx(c->d[1]);
}
static uint8_t threshold(void) { return rd8(c->a[1]++); }
static void yes(void) { c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]); }
static void no(void)  { c->d[0] = 0; F_L(0); }

/* $23E4: whether thing d0 is owned (ne), or for item $29 if 99 are not yet (the bag) */
static void owns(void)
{
    c->d[0] = set_w(c->d[0], c->d[0] & 0xFF);
    c->a[1] = 0x24D8u;
    uint8_t t = threshold(); cmp_byte(t, c->d[0]);
    if ((c->d[0] & 0xFF) >= t) { c->d[0] = set_b(c->d[0], c->d[0] | 0xF8); F_B(c->d[0]); call_68k(c, 0x274C); return; }
    cmp_byte(0x29, c->d[0]);
    if ((c->d[0] & 0xFF) == 0x29) {
        cmp_byte(0x63, rd8(0xFFFF959Cu));
        if (rd8(0xFFFF959Cu) < 0x63) no(); else yes();
        return;
    }
    t = threshold(); cmp_byte(t, c->d[0]);
    if ((c->d[0] & 0xFF) >= t) {
        c->d[1] = set_w(c->d[1], c->d[0]);
        item_cell();
        cmp_byte(0x12, c->d[0]);
        if ((c->d[0] & 0xFF) < 0x12) { F_B(rd8(c->a[0])); return; }
        c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 0x2B, c->d[1]));
        F_B(rd8(c->a[0]));
        if (rd8(c->a[0]) & 0x80) { no(); return; }
        cmp_byte(rd8(c->a[0]), c->d[1]);
        if ((c->d[1] & 0xFF) > rd8(c->a[0])) no(); else yes();
        return;
    }
    c->a[1] += 1;
    t = threshold(); cmp_byte(t, c->d[0]);
    if ((c->d[0] & 0xFF) >= t) {
        c->d[0] = set_b(c->d[0], c->d[0] & 0xF);
        c->d[1] = 11;
        cmp_byte(5, c->d[0]);
        if ((c->d[0] & 0xFF) >= 5) c->d[1] = 0;
        c->a[0] = 0xFFFF95BEu;
        cmp_byte(rd8(c->a[0] + sx(c->d[0])), c->d[1]);
        if ((c->d[1] & 0xFF) >= rd8(c->a[0] + sx(c->d[0]))) no(); else yes();
        return;
    }
    flag_cell();
    mem_bit(c->a[0], (int)(c->d[0] & 7));
}

/* $245A: give thing d0 */
static void give(void)
{
    c->d[0] = set_w(c->d[0], c->d[0] & 0xFF);
    c->a[1] = 0x24D8u;
    uint8_t t = threshold(); cmp_byte(t, c->d[0]);
    if ((c->d[0] & 0xFF) >= t) {
        c->d[2] = set_b(c->d[2], c->d[0]);
        c->d[0] = set_b(c->d[0], c->d[0] | 0xF8);
        call_68k(c, 0x2752);                        /* SetGameFlag */
        c->d[0] = set_b(c->d[0], c->d[2]);
        c->d[0] = set_w(c->d[0], c->d[0] & 0xF);
        call_68k(c, 0x8A04);
        return;
    }
    t = threshold(); cmp_byte(t, c->d[0]);
    if ((c->d[0] & 0xFF) >= t) {
        c->d[1] = set_w(c->d[1], c->d[0]);
        item_cell();
        cmp_byte(0x12, c->d[0]);
        if ((c->d[0] & 0xFF) != 0x12) { wr8(c->a[0], (uint8_t)add_b(&c->ccr, 1, rd8(c->a[0]))); return; }
        c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 0x2B, c->d[1]));
        wr8(c->a[0], (uint8_t)c->d[1]);
        c->d[1] = set_b(c->d[1], add_b(&c->ccr, 8, c->d[1]));
        c->a[0] = 0xFFFF9F10u;
        for (c->d[0] = 1; ; ) {
            cmp_byte(8, rd8(c->a[0]));
            if (rd8(c->a[0]) >= 8) {
                cmp_byte(0xC, rd8(c->a[0]));
                if (rd8(c->a[0]) < 0xC) { wr8(c->a[0], (uint8_t)c->d[1]); call_68k(c, 0x76C4); return; }
            }
            c->a[0] += 1;
            c->d[0] = set_w(c->d[0], c->d[0] - 1);
            if ((c->d[0] & 0xFFFF) == 0xFFFF) return;
        }
    }
    t = threshold(); cmp_byte(t, c->d[0]);
    if ((c->d[0] & 0xFF) >= t) { call_68k(c, 0x8A3A); return; }
    t = threshold(); cmp_byte(t, c->d[0]);
    if ((c->d[0] & 0xFF) >= t) { c->d[0] = set_w(c->d[0], c->d[0] & 0xF); F_W(c->d[0]); call_68k(c, 0x8A04); return; }
    flag_cell();
    mem_bset(c->a[0], (int)(c->d[0] & 7));
}

/* $24DC: take thing d0 (an item's count down; leaving its slot, $882C) */
static void take(void)
{
    c->d[0] = set_w(c->d[0], c->d[0] & 0xFF);
    cmp_byte(0x20, c->d[0]);
    if ((c->d[0] & 0xFF) < 0x20) { flag_cell(); mem_bclr(c->a[0], (int)(c->d[0] & 7)); return; }
    c->d[3] = set_w(c->d[3], c->d[0]);
    call_68k(c, 0x882C);
    c->d[0] = set_w(c->d[0], c->d[3]);
    c->d[1] = set_w(c->d[1], c->d[0]);
    item_cell();
    cmp_byte(0x12, c->d[0]);
    if ((c->d[0] & 0xFF) != 0x12) { wr8(c->a[0], (uint8_t)sub_b(&c->ccr, 1, rd8(c->a[0]))); return; }
    F_B(rd8(c->a[0]));
    if (rd8(c->a[0]) & 0x80) return;
    c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 0x2A, c->d[1]));
    wr8(c->a[0], (uint8_t)sub_b(&c->ccr, c->d[1], rd8(c->a[0])));
}

/* ---- batch T6: the message interpreter's core ----
 * $13BC runs once a frame while a message is open (MSG bit 7): it writes characters until a
 * delay, a "more" (waiting for a button), a choice or the end, and returns; its place in the
 * text (and any texts it has stepped into) is a stack of pointers at $FF8C7A, MSG_DEPTH bytes
 * deep. Here the core is C: the choice between two answers, the "more" wait, the delays, the
 * characters, the 12 basic codes and stepping into a name's text. The 51 further commands
 * (code $0B and a number, $15C8), the three-way choice (MSG bit 3, $1CBC) and the shop's menu
 * (MSG bit 2, the step at $8CDA) are still the original's: the C hands over to them, as the
 * original jumps to them, and they carry on there. */

#define MSG           0xFFFF8CA4u   /* byte: 7 open, 6 begun, 5 a choice, 4 no fast-forward, 3 the three-way choice, 2 the shop, 0 "more" */
#define MSG_DEPTH     0xFFFF8CA8u   /* word: the texts' stack, in bytes */
#define MSG_TEXTS     0xFFFF8C7Au   /* the stack of text pointers (-$7386 from the depth) */
#define MSG_DELAY     0xFFFF8CA2u   /* byte: frames after each character */
#define MSG_WAIT      0xFFFF8CA3u   /* byte: frames still to wait */
#define MSG_ATTR      0xFFFF8CA6u   /* word: the characters' tile attributes */
#define PAD_HELD_MSG  0xFFFF8A7Au   /* byte: buttons held */
#define PAD_NEW_MSG   0xFFFF8A7Cu   /* byte: buttons pressed this frame */

static int handed_over;
static void hand_over(uint32_t addr) { port_continue_at(c, addr, 0); handed_over = 1; }

/* $13AE: the character d0 at column d6 + 1, row d7 + 1 (inside the frame) */
static void char_in(void)
{
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
    char_at();
    c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    c->d[7] = set_w(c->d[7], sub_w(&c->ccr, 1, c->d[7]));
}

static uint32_t text_slot(void) { return sx(rd16(MSG_DEPTH)) - 0x7386; }   /* -$7386(a0), a0 the depth */

/* $1596: the place in the text kept, until the next frame */
static void msg_pause(void)
{
    c->a[0] = sx(rd16(MSG_DEPTH));
    wr32(c->a[0] - 0x7386, c->a[2]);
}

/* ---- batch T7: the interpreter's further commands ---- */

#define MSG_THING     0xFFFF8CAEu   /* byte: the thing a message is about (a byte $FF in a command means it) */
#define MSG_AMOUNT    0xFFFF8CB2u   /* long: a price or amount */
#define GOLD          0xFFFF962Cu   /* long: the gold */
#define QUIZ_LEFT     0xFFFF8CD4u   /* word: the quiz's frames to answer */
#define QUIZ_ROUND    0xFFFF8CD2u   /* byte: the question */
#define QUIZ_RIGHT    0xFFFF8CD3u   /* byte: the right answers */
#define MAX_HEALTH_W  0xFFFF9F00u
#define PLAYER_PTR    0xFFFF9EEEu

enum { CMD_ON, CMD_PAUSE, CMD_RETURN, CMD_HANDED };

/* $1568: step into text d2 of table d3 & 3 of the names (the place kept on the texts' stack) */
static void step_into(void)
{
    c->d[3] = set_w(c->d[3], c->d[3] & 3);
    c->d[3] = set_w(c->d[3], lsl_n(&c->ccr, c->d[3], 2, 16));
    c->a[1] = rd32(rd32(DATA_DIR + 0x10) + sx(c->d[3]));
    c->d[3] = set_b(c->d[3], c->d[2]);
    c->d[3] = set_w(c->d[3], add_w(&c->ccr, c->d[3], c->d[3]));
    c->d[2] = set_w(c->d[2], rd16(c->a[1] + sx(c->d[3]))); F_W(c->d[2]);
    c->a[0] = sx(rd16(MSG_DEPTH));
    wr32(c->a[0] - 0x7386, c->a[2]);
    c->a[0] += 4;
    wr16(MSG_DEPTH, (uint16_t)c->a[0]);
    c->a[2] = c->a[1] + sx(c->d[2]);
}

/* $168C: a jump in the text by the word at a2 */
static void jump_text(void)
{
    c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
    c->d[0] = set_w(c->d[0], c->d[0] << 8);
    c->d[0] = set_b(c->d[0], rd8(c->a[2]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    c->a[2] += sx(c->d[0]);
}
/* a test's outcome: jump by the word after it, or step past the word */
static void jump_if(int taken) { if (taken) jump_text(); else c->a[2] += 2; }

/* the byte at a2, or MSG_THING for $FF */
static void arg_thing(void)
{
    c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
    cmp_byte(0xFF, c->d[0]);
    if ((c->d[0] & 0xFF) == 0xFF) { c->d[0] = set_b(c->d[0], rd8(MSG_THING)); F_B(c->d[0]); }
}

/* a call the original makes with a2 kept on the stack (it is put back after) */
static void call_keep_a2(uint32_t addr)
{
    uint32_t a2 = c->a[2];
    c->a[7] -= 4; wr32(c->a[7], a2);
    call_68k(c, addr);
    c->a[2] = rd32(c->a[7]); c->a[7] += 4;
}
static void gold_shown(void) { call_keep_a2(0x85A8); }   /* $1886: the HUD's gold */

/* $1B50: d0 / d1 (16 bits by shifts): d0 the quotient, d2 the remainder */
static void div16(void)
{
    c->d[2] = 0;
    for (c->d[3] = 15; ; ) {
        uint32_t x = (c->d[0] >> 15) & 1;
        c->d[0] = set_w(c->d[0], c->d[0] << 1);
        c->d[2] = set_w(c->d[2], (c->d[2] << 1) + x);
        cmp_byte(c->d[1], c->d[2]);
        if ((c->d[2] & 0xFF) >= (c->d[1] & 0xFF)) {
            c->d[2] = set_b(c->d[2], sub_b(&c->ccr, c->d[1], c->d[2]));
            c->d[0] = set_b(c->d[0], add_b(&c->ccr, 1, c->d[0]));
        }
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
        if ((c->d[3] & 0xFFFF) == 0xFFFF) break;
    }
}

/* $1A90: the quiz's question QUIZ_ROUND: its text and three answers in random order ($1AD6,
 * $1AFE: the six orders) */
static void quiz_question(void)
{
    uint32_t a2 = c->a[2];
    c->a[7] -= 4; wr32(c->a[7], a2);
    c->d[0] = rd8(QUIZ_ROUND);
    c->a[0] = sx(c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[0] - 0x7336));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    c->a[1] = 0x1AD6u + sx(c->d[0]);
    c->a[2] = 0xFFFF8CC6u;
    wr8(c->a[2]++, rd8(c->a[1]));
    call_68k(c, 0x5D8);                             /* Random */
    c->d[1] = 6;
    div16();
    c->d[0] = set_b(c->d[0], rd8(0x1AFEu + sx(c->d[2])));
    c->d[1] = 2;
    c->a[0] = 0xFFFF8CCFu;
    for (;;) {
        c->d[2] = set_b(c->d[2], c->d[0]);
        c->d[2] = set_w(c->d[2], c->d[2] & 3);
        wr8(c->a[0]++, (uint8_t)c->d[2]);
        wr8(c->a[2]++, rd8(c->a[1] + 1 + sx(c->d[2])));
        c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 2, 16));
        c->d[1] = set_w(c->d[1], c->d[1] - 1);
        if ((c->d[1] & 0xFFFF) == 0xFFFF) break;
    }
    c->a[2] = rd32(c->a[7]); c->a[7] += 4;
}

/* $1B04: the quiz's six questions, from ten, in random order (into $8CCA) */
static void quiz_order(void)
{
    uint32_t sp = c->a[7] - 4;
    wr32(sp, c->a[6]); c->a[6] = sp; c->a[7] = sp - 10;
    c->a[0] = c->a[6] - 10;
    for (c->d[0] = 9; ; ) {
        wr8(c->a[0]++, 0);
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
        if ((c->d[0] & 0xFFFF) == 0xFFFF) break;
    }
    c->a[1] = 0xFFFF8CCAu;
    c->d[7] = 10;
    do {
        call_68k(c, 0x5D8);                         /* Random */
        c->d[1] = set_w(c->d[1], c->d[7]);
        div16();
        c->a[0] = c->a[6] - 10;
        c->d[1] = 0;
        for (;;) {                                  /* the d2-th still free */
            c->d[1] = set_b(c->d[1], add_b(&c->ccr, 1, c->d[1]));
            uint8_t used = rd8(c->a[0]++); F_B(used);
            if (used) continue;
            c->d[2] = set_b(c->d[2], sub_b(&c->ccr, 1, c->d[2]));
            if (!(c->d[2] & 0x80)) continue;
            break;
        }
        wr8(--c->a[0], 0xFF);
        c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 1, c->d[1]));
        wr8(c->a[1]++, (uint8_t)c->d[1]);
        c->d[7] = set_w(c->d[7], sub_w(&c->ccr, 1, c->d[7]));
        cmp_byte(5, c->d[7]);
    } while ((c->d[7] & 0xFF) >= 5);
    wr8(QUIZ_RIGHT, 0); wr8(QUIZ_ROUND, 0); f_clr_z();
    c->a[7] = c->a[6]; c->a[6] = rd32(c->a[7]); c->a[7] += 4;
}

/* $15C8's commands: code $0B, then a number. They answer how the text goes on */
static int command(void)
{
    c->d[0] = rd8(c->a[2]++);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] = 0x15C8u + sx(rd16(0x15C8u + sx(c->d[0])));
    uint32_t a0;
    switch ((c->d[0] & 0xFFFF) >> 1) {
    case 0:                                         /* the frame's part ends here */
        return CMD_RETURN;
    case 1:                                         /* step into table d3, the thing at $8CAF */
        c->d[3] = set_b(c->d[3], rd8(c->a[2]++));
        c->d[2] = set_b(c->d[2], rd8(0xFFFF8CAFu));
        step_into();
        return CMD_ON;
    case 2:                                         /* a choice of two: the answers' columns */
        wr8(MSG, rd8(MSG) | 0x20);
        wr8(0xFFFF8CACu, rd8(c->a[2]++));
        wr8(0xFFFF8CADu, rd8(c->a[2]++));
        wr8(MARKS, rd8(MARKS) & 0xFD);
        wr8(MARKS, rd8(MARKS) | 4); F_B(rd8(MARKS));
        c->a[0] = sx(rd16(MSG_DEPTH));
        wr32(c->a[0] - 0x7386, c->a[2]);
        choice_draw(0x87B5);
        return CMD_RETURN;
    case 3:                                         /* the gold short of the amount: jump */
        c->d[0] = rd32(GOLD); F_L(c->d[0]);
        { uint16_t x = c->ccr & CC_X; uint32_t amt = rd32(MSG_AMOUNT), r = c->d[0] - amt;
          c->ccr = (uint16_t)(x | (r & 0x80000000u ? CC_N : 0) | (r ? 0 : CC_Z) | (c->d[0] < amt ? CC_C : 0)
                              | ((((c->d[0] ^ amt) & (c->d[0] ^ r)) >> 31) ? CC_V : 0)); }
        if (c->d[0] < rd32(MSG_AMOUNT)) jump_text(); else c->a[2] += 2;
        return CMD_ON;
    case 4:                                         /* the amount: a thing's price */
        arg_thing();
        price_of();
        wr32(MSG_AMOUNT, c->d[0]);
        return CMD_ON;
    case 5:                                         /* the amount, as digits (a text stepped into) */
        c->d[1] = rd32(MSG_AMOUNT);
        c->a[1] = 0xFFFF8CC0u;
        num_text();
        c->a[0] = sx(rd16(MSG_DEPTH));
        wr32(c->a[0] - 0x7386, c->a[2]);
        c->a[0] += 4;
        wr16(MSG_DEPTH, (uint16_t)c->a[0]);
        c->a[2] = c->a[1];
        return CMD_ON;
    case 6:                                         /* the thing's name */
        c->d[3] = 13;
        c->d[2] = set_b(c->d[2], rd8(MSG_THING));
        cmp_byte(0x60, c->d[2]);
        if ((c->d[2] & 0xFF) >= 0x60) {
            cmp_byte(0x66, c->d[2]);
            if ((c->d[2] & 0xFF) < 0x66) c->d[2] = set_b(c->d[2], sub_b(&c->ccr, 0x40, c->d[2]));
        }
        step_into();
        return CMD_ON;
    case 7:                                         /* pay the amount */
        c->d[0] = rd32(MSG_AMOUNT);
        wr32(GOLD, rd32(GOLD) - c->d[0]);
        gold_shown();
        return CMD_ON;
    case 8:                                         /* give a thing */
        arg_thing();
        call_keep_a2(0x245A);
        wr16(0xFFFF9994u, rd16(0xFFFF9994u) | 0x4000); F_W(rd16(0xFFFF9994u));
        return CMD_ON;
    case 9:                                         /* the speaker gone */
        c->a[0] = sx(rd16(0xFFFF8CB0u));
        wr8(c->a[0] - 0x4000, 0); f_clr_z();
        return CMD_ON;
    case 10:
        wr8(MSG_THING, rd8(c->a[2]++));
        return CMD_ON;
    case 11:
        call_68k(c, 0x1C494);
        return CMD_ON;
    case 12: wr8(0xFFFF9BF3u, rd8(0xFFFF9BF3u) | 3); F_B(rd8(0xFFFF9BF3u)); return CMD_ON;
    case 13: wr8(0xFFFF9BF3u, rd8(0xFFFF9BF3u) | 1); F_B(rd8(0xFFFF9BF3u)); return CMD_ON;
    case 14:
        wr8(0xFFFF965Eu, rd8(c->a[2]++));
        return CMD_ON;
    case 15:                                        /* the amount from the table at $1906 */
        c->d[0] = 0;
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        cmp_byte(0xFF, c->d[0]);
        if ((c->d[0] & 0xFF) == 0xFF) c->d[0] = set_b(c->d[0], rd8(0xFFFF965Eu));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
        wr32(MSG_AMOUNT, rd32(0x1906u + sx(c->d[0]))); F_L(rd32(0x1906u + sx(c->d[0])));
        return CMD_ON;
    case 16:                                        /* no gold */
        wr32(GOLD, 0); f_clr_z();
        gold_shown();
        return CMD_ON;
    case 17:                                        /* a game flag's bit set: jump */
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        call_68k(c, 0x2758);                        /* GameFlagBit */
        jump_if(mem_bit(c->a[0] + sx(c->d[1]), (int)(c->d[0] & 7)));
        return CMD_ON;
    case 18:                                        /* a game flag set: jump */
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        call_68k(c, 0x274C);                        /* TestGameFlag */
        jump_if(!(c->ccr & CC_Z));
        return CMD_ON;
    case 19:
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        call_68k(c, 0x2758);
        mem_bset(c->a[0] + sx(c->d[1]), (int)(c->d[0] & 7));
        return CMD_ON;
    case 20:
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        call_68k(c, 0x2752);                        /* SetGameFlag */
        return CMD_ON;
    case 21:
        call_keep_a2(0x1C330);
        return CMD_ON;
    case 22:
        wr8(0xFFFF8CC3u, rd8(c->a[2]++));
        return CMD_ON;
    case 23:                                        /* a thing owned: jump */
        arg_thing();
        owns();
        jump_if(!(c->ccr & CC_Z));
        return CMD_ON;
    case 24:                                        /* a pause of d0 frames */
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        cheaper();
        wr8(MSG_WAIT, (uint8_t)c->d[0]);
        return CMD_PAUSE;
    case 25:
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        call_68k(c, 0x366);                         /* QueueSound */
        return CMD_ON;
    case 26:                                        /* the ocarina's tune ($2AAE waits for frames): still the original's */
        hand_over(c->a[0]);
        return CMD_HANDED;
    case 27:
        wr8(0xFFFF8CC4u, rd8(c->a[2]++));
        wr8(0xFFFF8CC5u, rd8(c->a[2]++));
        return CMD_ON;
    case 28:
        c->d[3] = set_b(c->d[3], rd8(0xFFFF8CC4u));
        c->d[2] = set_b(c->d[2], rd8(0xFFFF8CC5u));
        step_into();
        return CMD_ON;
    case 29:
        c->d[0] = set_w(c->d[0], rd16(0xFFFF968Au));
        call_68k(c, 0xF906);
        return CMD_ON;
    case 30:                                        /* the cursor's place */
        c->d[0] = 0;
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++)); wr16(TEXT_COL, (uint16_t)c->d[0]);
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++)); wr16(TEXT_LINE, (uint16_t)c->d[0]); F_W(c->d[0]);
        return CMD_ON;
    case 31:                                        /* step into a quiz answer's text */
        c->d[3] = 12;
        c->d[0] = rd8(c->a[2]++);
        c->a[0] = sx(c->d[0]);
        c->d[2] = set_b(c->d[2], rd8(c->a[0] - 0x733A));
        step_into();
        return CMD_ON;
    case 32:                                        /* the text cleared */
        c->d[0] = set_w(c->d[0], 0x8020);
        c->d[7] = 1;
        c->d[5] = set_w(c->d[5], rd16(TEXT_LINES));
        c->d[5] = set_w(c->d[5], add_w(&c->ccr, c->d[5], c->d[5]));
        c->d[5] = set_w(c->d[5], add_w(&c->ccr, 1, c->d[5]));
        do {
            c->d[6] = 1;
            c->d[4] = set_w(c->d[4], rd16(TEXT_W));
            do {
                char_at();
                c->d[6] = set_w(c->d[6], c->d[6] + 1);
                c->d[4] = set_w(c->d[4], sub_w(&c->ccr, 1, c->d[4]));
            } while (c->d[4] & 0xFFFF);
            c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
            c->d[5] = set_w(c->d[5], sub_w(&c->ccr, 1, c->d[5]));
        } while (c->d[5] & 0xFFFF);
        wr16(TEXT_COL, (uint16_t)c->d[5]); wr16(TEXT_LINE, (uint16_t)c->d[5]); F_W(c->d[5]);
        return CMD_ON;
    case 33:                                        /* the quiz's answer awaited (240 frames) */
        wr16(QUIZ_LEFT, 0xF0);
        mem_bset(MSG, 3);
        return CMD_PAUSE;
    case 34: quiz_question(); return CMD_ON;
    case 35: quiz_order(); return CMD_ON;
    case 36:                                        /* by the right answers: the n-th of a list of jumps */
        c->d[0] = rd8(QUIZ_RIGHT);
        for (;;) {
            c->d[1] = set_b(c->d[1], rd8(c->a[2]++));
            c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 8, 16));
            c->d[1] = set_b(c->d[1], rd8(c->a[2]++));
            c->d[0] = set_w(c->d[0], c->d[0] - 1);
            if ((c->d[0] & 0xFFFF) == 0xFFFF) break;
        }
        c->a[2] = c->a[2] - 2 + sx(c->d[1]);
        return CMD_ON;
    case 37:                                        /* the amount: the long at a2; the frame's part ends */
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        c->d[0] = c->d[0] << 16 | c->d[0] >> 16;
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        wr32(MSG_AMOUNT, c->d[0]); F_L(c->d[0]);
        return CMD_RETURN;
    case 38:                                        /* gain the amount */
        wr32(GOLD, rd32(GOLD) + rd32(MSG_AMOUNT));
        gold_shown();
        return CMD_ON;
    case 39: {                                      /* jump by how many of the best things are owned */
        wr32(MSG_AMOUNT, 0);
        uint32_t a2 = c->a[2];
        c->a[7] -= 4; wr32(c->a[7], a2);
        for (c->d[0] = 7; ; ) {
            c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->d[0]);
            owns();
            if (!(c->ccr & CC_Z)) mem_addq_w(0xFFFF8CB4u, 1);
            c->d[0] = set_w(c->d[0], rd16(c->a[7])); c->a[7] += 2;
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
            cmp_word(0x20, c->d[0]);
            if ((c->d[0] & 0xFFFF) >= 0x20) break;
        }
        c->a[2] = rd32(c->a[7]); c->a[7] += 4;
        c->d[0] = set_w(c->d[0], rd16(0xFFFF8CB4u));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
        c->a[2] += sx(c->d[0]);
        jump_text();
        return CMD_ON;
    }
    case 40:                                        /* the inn: the heal each frame ($1BE6) */
        wr8(MSG, rd8(MSG) | 4);
        wr32(0xFFFF8CDAu, 0x1BE6);
        c->d[0] = 61; call_68k(c, 0x366);
        return CMD_PAUSE;
    case 41:                                        /* take the thing */
        c->d[0] = rd8(MSG_THING);
        call_keep_a2(0x24DC);
        return CMD_ON;
    case 42:                                        /* $959C up to the byte: jump */
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        cmp_byte(rd8(0xFFFF959Cu), c->d[0]);
        jump_if((c->d[0] & 0xFF) <= rd8(0xFFFF959Cu));
        return CMD_ON;
    case 43:                                        /* the most hearts already: jump */
        cmp_word(0xE00, rd16(MAX_HEALTH_W));
        jump_if(rd16(MAX_HEALTH_W) >= 0xE00);
        return CMD_ON;
    case 44: {                                      /* every special action at 12: jump */
        int all = 1;
        for (uint32_t k = 0; k < 5 && all; k++) { cmp_byte(rd8(0xFFFF95BEu + k), 12); all = rd8(0xFFFF95BEu + k) == 12; }
        c->d[0] = 12;
        jump_if(all);
        return CMD_ON;
    }
    case 45:                                        /* health full: jump */
        a0 = sx(rd16(PLAYER_PTR));
        c->a[0] = a0;
        c->d[0] = set_w(c->d[0], rd16(a0 - 0x2600)); F_W(c->d[0]);
        cmp_word(rd16(MAX_HEALTH_W), c->d[0]);
        jump_if((c->d[0] & 0xFFFF) >= rd16(MAX_HEALTH_W));
        return CMD_ON;
    case 46:
        call_68k(c, 0x8E9C);
        c->a[2] += 2;
        return CMD_ON;
    case 47:                                        /* $9F32 not negative: jump */
        F_W(rd16(0xFFFF9F32u));
        jump_if(!(rd16(0xFFFF9F32u) & 0x8000));
        return CMD_ON;
    case 48:                                        /* a heart more (up to 14) */
        cmp_word(0xE00, rd16(MAX_HEALTH_W));
        if (rd16(MAX_HEALTH_W) < 0xE00) {
            wr16(MAX_HEALTH_W, (uint16_t)add_w(&c->ccr, 0x100, rd16(MAX_HEALTH_W)));
            c->a[0] = sx(rd16(PLAYER_PTR));
            wr16(c->a[0] - 0x2600, (uint16_t)add_w(&c->ccr, 0x100, rd16(c->a[0] - 0x2600)));
            call_keep_a2(0x8AA2);
        }
        return CMD_ON;
    case 49:
        call_keep_a2(0x76C4);
        return CMD_ON;
    default:                                        /* 50: the text's lines */
        c->d[0] = 0;
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        wr16(TEXT_LINES, (uint16_t)c->d[0]); F_W(c->d[0]);
        return CMD_ON;
    }
}

/* $1CBC: the quiz's answer: A, B or C (or the time running out): "Correct.", "Incorrect." or
 * "Time's up." stepped into, with its sound; the text cleared for it */
static void quiz_answer(void)
{
    int answered = 1;
    if (mem_bit(PAD_NEW_MSG, 6)) c->d[0] = 0;
    else if (mem_bit(PAD_NEW_MSG, 4)) c->d[0] = 1;
    else if (mem_bit(PAD_NEW_MSG, 5)) c->d[0] = 2;
    else answered = 0;
    if (!answered) {
        if (mem_subq_w(QUIZ_LEFT, 1)) return;
        wr8(0xFFFF8CC2u, rd8(0xFFFF8CC2u) | 0x40);
        c->a[2] = 0x1D72u;
        c->d[0] = 53;
    } else {
        F_L(c->d[0]);
        c->a[0] = sx(c->d[0]);
        c->d[7] = set_w(c->d[7], c->d[0]);
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[7], c->d[7]));
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 6, c->d[7]));
        c->d[6] = 2;
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0xA010, c->d[0]));
        char_at();
        F_B(rd8(c->a[0] - 0x7331));
        if (rd8(c->a[0] - 0x7331)) {
            wr8(0xFFFF8CC2u, rd8(0xFFFF8CC2u) | 0x40);
            c->a[2] = 0x1D7Du;
            c->d[0] = 53;
        } else {
            wr8(0xFFFF8CC2u, rd8(0xFFFF8CC2u) | 0x80);
            c->a[2] = 0x1D88u;
            wr8(QUIZ_RIGHT, (uint8_t)add_b(&c->ccr, 1, rd8(QUIZ_RIGHT)));
            c->d[0] = 52;
        }
    }
    call_68k(c, 0x366);                             /* QueueSound */
    c->a[0] = sx(rd16(MSG_DEPTH)) + 4;
    wr32(c->a[0] - 0x7386, c->a[2]);
    wr16(MSG_DEPTH, (uint16_t)c->a[0]);
    wr16(TEXT_COL, 2);
    wr16(TEXT_LINE, 0);
    c->d[0] = set_w(c->d[0], 0x8020);
    c->d[7] = 1;
    c->d[5] = 4;
    do {
        c->d[6] = 1;
        c->d[4] = set_w(c->d[4], rd16(TEXT_W));
        do {
            char_at();
            c->d[6] = set_w(c->d[6], c->d[6] + 1);
            c->d[4] = set_w(c->d[4], sub_w(&c->ccr, 1, c->d[4]));
        } while (c->d[4] & 0xFFFF);
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
        c->d[5] = set_w(c->d[5], sub_w(&c->ccr, 1, c->d[5]));
    } while (c->d[5] & 0xFFFF);
    wr8(QUIZ_ROUND, (uint8_t)add_b(&c->ccr, 1, rd8(QUIZ_ROUND)));
    mem_bclr(MSG, 3);
}

/* $1BE6: the inn's heal, $10 a frame up to the most; then the message goes on (sound 62); the
 * hearts drawn ($8AA2) */
static void inn_heal(void)
{
    c->a[0] = sx(rd16(PLAYER_PTR));
    wr16(c->a[0] - 0x2600, (uint16_t)add_w(&c->ccr, 0x10, rd16(c->a[0] - 0x2600)));
    c->d[0] = set_w(c->d[0], rd16(MAX_HEALTH_W));
    cmp_word(rd16(c->a[0] - 0x2600), c->d[0]);
    if ((c->d[0] & 0xFFFF) <= rd16(c->a[0] - 0x2600)) {
        wr16(c->a[0] - 0x2600, (uint16_t)c->d[0]);
        wr8(MSG, rd8(MSG) & 0xFB);
        c->d[0] = 62; call_68k(c, 0x366);
    }
    call_68k(c, 0x8AA2);
}

/* $14F4: the text from a2 until something waits; 0 when the frame's part is done, 1 handed over */
static void msg_text(void)
{
    for (;;) {
        c->d[3] = set_b(c->d[3], rd8(c->a[2]++));
        cmp_byte(0x10, c->d[3]);
        uint8_t ch = (uint8_t)c->d[3];
        if (ch >= 0x10) {                           /* a character (two rows: its mark above) */
            c->d[6] = set_w(c->d[6], rd16(TEXT_COL)); F_W(c->d[6]);
            cmp_word(rd16(TEXT_W), c->d[6]);
            if ((c->d[6] & 0xFFFF) >= rd16(TEXT_W)) {
                new_line();
                c->d[3] = set_b(c->d[3], rd8(c->a[2] - 1)); F_B(c->d[3]);
            }
            c->d[0] = set_w(c->d[0], rd16(MSG_ATTR));
            c->d[0] = set_b(c->d[0], 0x20); F_B(0x20);
            cmp_byte(0xFE, c->d[3]);
            if ((c->d[3] & 0xFF) >= 0xFE) {
                c->d[3] = set_b(c->d[3], sub_b(&c->ccr, 0x40, c->d[3]));
                c->d[0] = set_b(c->d[0], c->d[3]);
                c->d[3] = set_b(c->d[3], rd8(c->a[2]++));
            }
            c->d[6] = set_w(c->d[6], rd16(TEXT_COL));
            c->d[7] = set_w(c->d[7], rd16(TEXT_LINE));
            c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[7], c->d[7]));
            char_in();
            c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
            c->d[0] = set_b(c->d[0], c->d[3]);
            char_in();
            c->d[0] = set_b(c->d[0], rd8(0xFFFF8CC3u));
            wr8(0xFFFF8CC2u, (uint8_t)(rd8(0xFFFF8CC2u) | c->d[0]));
            mem_addq_w(TEXT_COL, 1);
        } else if (ch >= 0x0C) {                    /* $0C-$0F: step into text d2 of table d3 & 3 */
            c->d[2] = set_b(c->d[2], rd8(c->a[2]++));
            step_into();
            continue;
        } else {
            c->d[0] = ch;
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
            c->a[0] = 0x15A0u + sx(rd16(0x15A0u + sx(c->d[0])));
            switch (ch) {
            case 0:                                 /* the end of a text: back out, or the message is over */
                c->d[0] = set_w(c->d[0], rd16(MSG_DEPTH)); F_W(c->d[0]);
                if (!(c->d[0] & 0xFFFF)) { wr8(MSG, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); return; }
                c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 4, c->d[0]));
                wr16(MSG_DEPTH, (uint16_t)c->d[0]);
                c->a[0] = sx(c->d[0]);
                c->a[2] = rd32(c->a[0] - 0x7386);
                continue;
            case 1:                                 /* the attributes */
                c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
                c->d[0] = set_w(c->d[0], c->d[0] << 8);
                wr16(MSG_ATTR, (uint16_t)c->d[0]);
                continue;
            case 2:                                 /* a new line */
                new_line();
                break;
            case 3:                                 /* the delay per character */
                c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
                cheaper();
                wr8(MSG_DELAY, (uint8_t)c->d[0]);
                continue;
            case 4:                                 /* spaces */
                c->d[0] = rd8(c->a[2]++);
                mem_addq_w(TEXT_COL, c->d[0]);
                continue;
            case 5:                                 /* "more": wait for a button */
                wr8(MSG, rd8(MSG) | 1);
                more_show();
                mem_bset(MARKS, 0);
                msg_pause();
                return;
            case 6:                                 /* a jump in the text */
                jump_text();
                continue;
            case 7:
                wr8(MSG, rd8(MSG) | 0x10); F_B(rd8(MSG));
                continue;
            case 8:
                wr8(MSG, rd8(MSG) & 0xEF); F_B(rd8(MSG));
                continue;
            case 9:                                 /* a new line, indented */
                new_line();
                c->d[0] = rd8(0xFFFF8CABu);
                mem_addq_w(TEXT_COL, c->d[0]);
                continue;
            case 10:                                /* the indent */
                wr8(0xFFFF8CABu, rd8(c->a[2]++));
                continue;
            default: {                              /* 11: a further command ($15C8) */
                int r = command();
                if (r == CMD_ON) continue;
                if (r == CMD_PAUSE) msg_pause();
                return;                             /* CMD_RETURN, CMD_HANDED */
            }
            }
        }
        wr8(MSG_WAIT, rd8(MSG_DELAY)); F_B(rd8(MSG_DELAY));   /* $1546 */
        if (rd8(MSG_DELAY)) { msg_pause(); return; }
    }
}

/* $13BC: a frame of the open message */
static void msg_step(void)
{
    F_B(rd8(MSG));
    if (!(rd8(MSG) & 0x80)) return;
    if (!mem_bset(MSG, 6)) {                        /* its first frame */
        wr8(MARKS, 0); wr16(MSG_DEPTH, 0); wr32(TEXT_COL, 0); wr16(MSG_DELAY, 0);
        wr8(0xFFFF8CC2u, 0); wr8(0xFFFF8CC3u, 1); wr16(MSG_ATTR, 0x8000); F_W(0x8000);
    }
    if (mem_bit(MSG, 2)) {                          /* a step of its own: the inn's heal ($1BE6) */
        c->a[0] = rd32(0xFFFF8CDAu);
        if (c->a[0] == 0x1BE6) inn_heal(); else hand_over(c->a[0]);
        return;
    }
    if (mem_bit(MSG, 3)) { quiz_answer(); return; }
    if (mem_bit(MSG, 5)) {                          /* a choice of two: left/right, A/B/C */
        choice_blink();
        int change = 0;
        if (mem_bit(PAD_NEW_MSG, 2)) change = mem_bit(MARKS, 1);
        else if (mem_bit(PAD_NEW_MSG, 3)) change = !mem_bit(MARKS, 1);
        if (change) {
            c->d[0] = 37; call_68k(c, 0x366);
            choice_hide();
            uint8_t m = rd8(MARKS);
            f_bit(m >> 1 & 1);
            wr8(MARKS, m ^ 2);
            choice_show();
        }
        int chosen = 0;
        if (mem_bit(PAD_NEW_MSG, 4)) {              /* B: the second */
            if (!mem_bit(MARKS, 1)) { choice_hide(); mem_bset(MARKS, 1); }
            chosen = 1;
        } else if (mem_bit(PAD_NEW_MSG, 6) || mem_bit(PAD_NEW_MSG, 5)) chosen = 1;
        if (!chosen) return;
        c->d[0] = 38; call_68k(c, 0x366);
        wr8(MSG, rd8(MSG) & 0xDF); F_B(rd8(MSG));
        choice_show();
        c->a[0] = sx(rd16(MSG_DEPTH));
        c->a[2] = rd32(c->a[0] - 0x7386);
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        c->d[0] = set_w(c->d[0], c->d[0] << 8);
        c->d[0] = set_b(c->d[0], rd8(c->a[2]++));
        if (mem_bit(MARKS, 1)) {                    /* the second: past the first's text */
            c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 2, c->d[0]));
            c->a[2] += sx(c->d[0]);
        }
        wr32(c->a[0] - 0x7386, c->a[2]);
    }
    if (mem_bit(MSG, 0)) {                          /* "more": a button goes on */
        more_blink();
        c->d[0] = set_b(c->d[0], rd8(PAD_NEW_MSG) & 0x70); F_B(c->d[0]);
        if (!(c->d[0] & 0xFF)) return;
        mem_bclr(MSG, 0);
        more_hide();
    }
    F_B(rd8(MSG_WAIT));
    if (rd8(MSG_WAIT)) {                            /* a delay: C held fast-forwards, unless MSG bit 4 */
        if (!mem_bit(MSG, 4) && mem_bit(PAD_HELD_MSG, 5)) { wr8(MSG_WAIT, 0); F_B(0); }
        else if (mem_subq_b(MSG_WAIT, 1)) return;
    }
    c->a[0] = sx(rd16(MSG_DEPTH));
    c->a[2] = rd32(c->a[0] - 0x7386);
    msg_text();
}

void MsgStep(Cpu *cpu) { c = cpu; handed_over = 0; msg_step(); if (!handed_over) c->ret = 0; }   /* $13BC */

/* call_68k leaves its mark in ret: cleared, as none of these goes on elsewhere */
#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(NumText, num_text)                /* $183E */
ENTRY(CharAt, char_at)                  /* $2050 */
ENTRY(WindowRow, window_row)            /* $21E8 */
ENTRY(ScrollWindow, scroll_window)      /* $2082 */
ENTRY(WindowFrame, window_frame)        /* $2110 */
ENTRY(TileUnder, tile_under)            /* $220A */
ENTRY(WindowRestore, window_restore)    /* $225C */
ENTRY(NewLine, new_line)                /* $16C6 */
ENTRY(MoreBlink, more_blink)            /* $172E */
ENTRY(MoreHide, more_hide)              /* $173C */
ENTRY(MoreShow, more_show)              /* $1754 */
ENTRY(ChoiceBlink, choice_blink)        /* $1774 */
ENTRY(ChoiceHide, choice_hide)          /* $1782 */
ENTRY(ChoiceShow, choice_show)          /* $1790 */
ENTRY(PriceOf, price_of)                /* $1D92 */
ENTRY(NameText, name_text)              /* $1ED0 */
ENTRY(NameOf, name_of)                  /* $1E6A */
ENTRY(BoxText, box_text)                /* $1F8E */
ENTRY(BoxRow, box_row)                  /* $1FBC */
ENTRY(BoxRows, box_rows)                /* $1FAC */
ENTRY(FoundThing, found_thing)          /* $1EEC */
ENTRY(FoundGold, found_gold)            /* $1F46 */
ENTRY(ShowFoundGold, show_found_gold)   /* $1FCA */
ENTRY(ShowFound, show_found)            /* $1FD0 */
ENTRY(MessageData, message_data)        /* $276C */
ENTRY(WindowGeometry, window_geometry)  /* $2002 */
ENTRY(WindowAt, window_at)              /* $27B0 */
ENTRY(OpenWindow, open_window)          /* $279C */
ENTRY(Cheaper, cheaper)                 /* $203A */
ENTRY(ItemCell, item_cell)              /* $2510 */
ENTRY(FlagCell, flag_cell)              /* $2536 */
ENTRY(Owns, owns)                       /* $23E4 */
ENTRY(Give, give)                       /* $245A */
ENTRY(Take, take)                       /* $24DC */
ENTRY(CharIn, char_in)                  /* $13AE */
