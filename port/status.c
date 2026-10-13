/* The status screen, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The status screen").
 *
 * The screen ($7582 and the routines under it, $7294-$8C34) draws on the window plane through
 * the VDP's data port: text as tiles $8000 | character (plain) or $A000 | character (bright),
 * placed with the text position $83B0 (column in d7's high byte, row in its low). Its menus are
 * loops that wait a frame at a time; those come later. These are the routines under them, ported
 * with the registers and flags they leave, because their callers carry on with them (a1 past
 * the string drawn, d7 on the next row, the flags of a test).
 *
 * Batch T1, the primitives: the text position, strings, the menu's text places, the meters,
 * the button tests, numbers. */
#include "world.h"

#define VDP_DATA      0x00C00000u
#define VDP_CTRL      0x00C00004u
#define PAD_NEW       0xFFFF8A7Cu   /* byte: buttons pressed this frame */
#define FIXED_LEN     0xFFFF8CDEu   /* word: the length of the fixed-width names ($1E6A) */
#define MENU_ITEM     0xFFFF95E4u   /* word */
#define BAR_FULL      0xFFFF95C8u   /* byte: the meter's full cells */
#define BAR_REST      0xFFFF95B7u   /* byte: its empty cells */
#define TWO_DIGITS    0xFFFF95E1u   /* byte: the number $8552 prints */
#define DIGITS        0xFFFF959Fu   /* $84EC's digits, lowest first */
#define LIST_TOP      0xFFFF9612u   /* word: the list's first line shown */
#define LIST_LEN      0xFFFF9610u   /* word: the list's length */
#define PLAYER_LEVEL  0xFFFF9F00u

static Cpu *c;

/* $83B8: the VDP to write at column d0, row d1 of the window plane ($5800) */
static void text_at_rc(void)
{
    c->d[1] = set_w(c->d[1], c->d[1] << 5);
    uint16_t o = (uint16_t)(c->d[0] | c->d[1]);
    uint16_t a = (uint16_t)((o * 2) | 0x5800);
    c->d[0] = (uint32_t)a << 16 | 3;
    wr32(VDP_CTRL, c->d[0]);
    c->ccr = (uint16_t)((cc_move_l(c->ccr, c->d[0]) & ~CC_X) | (o & 0x8000 ? CC_X : 0));   /* X: add.w d0,d0 */
}

/* $83B0: the text position d7 (column high, row low) */
static void text_at(void)
{
    c->d[1] = c->d[7] & 0xFF;
    c->d[0] = set_w(c->d[0], (c->d[7] & 0xFFFF) >> 8);
    text_at_rc();
}

/* $7874: the string at a1 as tiles d1's high byte | character; a1 ends past its 0 */
static void string(void)
{
    for (;;) {
        c->d[1] = set_b(c->d[1], rd8(c->a[1]++));
        if (!(c->d[1] & 0xFF)) break;
        wr16(VDP_DATA, (uint16_t)c->d[1]);
    }
    c->ccr = tst_b(c->ccr, 0);
}
static void string_plain(void)  { c->d[1] = set_w(c->d[1], 0x8000); string(); }   /* $7870 */
static void string_bright(void) { c->d[1] = set_w(c->d[1], 0xA000); string(); }   /* $786A */

/* $7B70: a fixed-width name ($8CDE characters; 0 is 65536) */
static void fixed_name(void)
{
    c->d[0] = set_w(c->d[0], 0x8000);
    c->d[1] = set_w(c->d[1], rd16(FIXED_LEN));
    uint16_t cc = c->ccr;
    do {
        c->d[0] = set_b(c->d[0], rd8(c->a[1]++));
        wr16(VDP_DATA, (uint16_t)c->d[0]);
        c->d[1] = set_w(c->d[1], sub_w(&cc, 1, c->d[1]));
    } while (c->d[1] & 0xFFFF);
    c->ccr = cc;
}

/* $7AAA: at the place of menu line d0 ($7ABC), the tile d2 (the line's cursor or its blank) */
static void line_mark(void)
{
    uint16_t cc = c->ccr;
    c->d[0] = set_w(c->d[0], add_w(&cc, c->d[0], c->d[0]));
    c->d[7] = set_w(c->d[7], rd16(0x7ABCu + (uint32_t)(int32_t)(int16_t)c->d[0]));   /* tbl(pc,d0.w): signed */
    text_at();
    wr16(VDP_DATA, (uint16_t)c->d[2]);
    c->ccr = cc_move_w(c->ccr, c->d[2]);
}

/* $7C56: the text position of the special-action menu's entry d0 / 2 ($7BFE); a1 its name */
static void menu_place(void)
{
    c->a[1] = 0x7BFEu + (uint32_t)(int32_t)rdw(0x7BFEu + (uint32_t)(int32_t)(int16_t)c->d[0]);
    c->d[7] = set_w(c->d[7], (uint32_t)rd8(c->a[1]) << 8 | rd8(c->a[1] + 1));
    c->a[1] += 2;
    text_at();
}

/* $7C4A: the entry MENU_ITEM bright */
static void menu_bright(void)
{
    uint16_t cc = c->ccr;
    c->d[0] = set_w(c->d[0], rd16(MENU_ITEM));
    c->d[0] = set_w(c->d[0], add_w(&cc, c->d[0], c->d[0]));
    c->ccr = cc;
    menu_place();
    string_bright();
}

/* $7C44: entry d0 / 2 plain, then MENU_ITEM bright */
static void menu_plain_bright(void)
{
    menu_place();
    string_plain();
    menu_bright();
}

/* $7C9A: d1's low byte copies of the tile d5 (none when it is 0; it ends $FF) */
static void cells(void)
{
    uint16_t cc = c->ccr;
    for (;;) {
        c->d[1] = set_b(c->d[1], sub_b(&cc, 1, c->d[1]));
        if (c->d[1] & 0x80) break;
        wr16(VDP_DATA, (uint16_t)c->d[5]);
    }
    c->ccr = cc;
}

/* $7C68: a meter of two rows under d7: BAR_FULL full cells then BAR_REST empty ones ($7CA0,
 * $7CA2: the tiles, bottom row first) */
static void meter(void)
{
    c->d[3] = 1;
    c->d[5] = set_w(c->d[5], 0x8700);
    for (;;) {
        c->d[7] = set_w(c->d[7], c->d[7] + 1);
        text_at();
        if (rd8(BAR_FULL)) {
            c->d[5] = set_b(c->d[5], rd8(0x7CA0u + (c->d[3] & 0xFFFF)));
            c->d[1] = set_b(c->d[1], rd8(BAR_FULL));
            cells();
        }
        c->d[5] = set_b(c->d[5], rd8(0x7CA2u + (c->d[3] & 0xFFFF)));
        c->d[1] = set_b(c->d[1], rd8(BAR_REST));
        cells();
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
        if ((c->d[3] & 0xFFFF) == 0xFFFF) break;
    }
}

/* $7D7A, $7D7E, $7D8A: d6 = -1 (mi, ne) if a button of d0 was pressed this frame, else 0 (eq) */
static void pressed(uint32_t mask)
{
    c->d[0] = set_b(mask, mask & rd8(PAD_NEW));     /* moveq #mask,d0; and.b */
    c->d[6] = c->d[0] & 0xFF ? 0xFFFFFFFFu : 0;
    c->ccr = cc_move_l(c->ccr, c->d[6]);
}

/* $83A4: the list's top + 4 against its length (cs: more below) */
static void more_below(void)
{
    uint16_t cc = c->ccr;
    c->d[0] = set_w(c->d[0], add_w(&cc, 4, rd16(LIST_TOP)));
    cmp_w(&cc, rd16(LIST_LEN), c->d[0]);
    c->ccr = cc;
}

/* $84AE: at d7, one blank (d0 ends $FFFF) */
static void blank(void)
{
    text_at();
    c->d[0] = 0;
    c->d[1] = set_w(c->d[1], 0x8020);
    wr16(VDP_DATA, 0x8020);
    c->d[0] = set_w(c->d[0], 0xFFFF);
    c->ccr = cc_move_w(c->ccr, 0x8020);
}

/* $8522: d0 / 10 in d0, the remainder in d1, by 32 shifts */
static void div10(void)
{
    uint16_t cc = c->ccr;
    c->d[1] = 0;
    for (int i = 0; i < 32; i++) {
        uint32_t x = c->d[0] >> 31;
        c->d[0] <<= 1;
        c->d[1] = set_w(c->d[1], (c->d[1] << 1) + x);
        cmp_w(&cc, 10, c->d[1]);
        if (!(cc & CC_C)) {
            c->d[1] = set_w(c->d[1], (c->d[1] & 0xFFFF) - 10);
            c->d[0] = set_w(c->d[0], add_w(&cc, 1, c->d[0]));
        }
    }
    c->d[2] = set_w(31, 0xFFFF);
    c->ccr = cc;
}

/* $84EC: d0 (at most 999,999) as digits at DIGITS, lowest first; d3 their count */
static void digits(void)
{
    c->a[0] = DIGITS;
    c->d[3] = 0;
    if (c->d[0] >= 0xF423Fu) c->d[0] = 0xF423Fu;
    do {
        div10();
        c->d[3] = set_b(c->d[3], c->d[3] + 1);
        wr8(c->a[0]++, (uint8_t)c->d[1]);
    } while (c->d[0]);
    c->ccr = cc_move_l(c->ccr, 0);
}

/* $8552: a mark ($8017) where the VDP is, then at d7 the number TWO_DIGITS (under 100): two
 * digits, or one and a blank */
static void two_digits(void)
{
    wr16(VDP_DATA, 0x8017);
    text_at();
    c->d[2] = set_w(c->d[2], 0x8030);
    c->d[3] = rd8(TWO_DIGITS);
    c->d[4] = 0;
    c->a[1] = VDP_DATA;
    while ((c->d[3] & 0xFFFF) >= 10) {
        c->d[3] = set_w(c->d[3], c->d[3] - 10);
        c->d[4] = set_w(c->d[4], c->d[4] + 1);
    }
    uint16_t x = CC_X;                              /* addi.w #10 after the borrow: a carry */
    if (c->d[4] & 0xFFFF) {
        c->d[4] = set_w(c->d[4], c->d[4] | 0x8030);
        wr16(VDP_DATA, (uint16_t)c->d[4]);
        c->d[3] = set_w(c->d[3], c->d[3] | 0x8030);
        wr16(VDP_DATA, (uint16_t)c->d[3]);
        c->ccr = (uint16_t)((cc_move_w(c->ccr, c->d[3]) & ~CC_X) | x);
        return;
    }
    c->d[3] = set_w(c->d[3], c->d[3] | 0x8030);
    wr16(VDP_DATA, (uint16_t)c->d[3]);
    wr16(VDP_DATA, 0x8020);
    c->ccr = (uint16_t)((cc_move_w(c->ccr, 0x8020) & ~CC_X) | x);
}

/* $853C: the level (`$85CC` first), its label at (6, 6) and the number at (7, 6) */
static void level(void)
{
    call_68k(c, 0x85CC);
    c->d[7] = set_w(c->d[7], 0x606);
    text_at();
    wr8(TWO_DIGITS, rd8(PLAYER_LEVEL));
    c->d[7] = set_w(c->d[7], 0x706);
    two_digits();
}

/* ---- batch T2: the cursors, the tile loaders ---- */

#define QUEUE_SOUND   0x366u
#define LIST_LINE     0xFFFF9606u   /* word: the line chosen */
#define PAD_HELD      0xFFFF95D9u   /* byte: the buttons for the menus (pressed, or repeating) */
#define PAL_STEP      0xFFFF8A77u   /* byte: the palette's fade step */
#define GAME_FLAGS2   0xFFFF8006u   /* byte: bit 2, the palette to upload */
#define DRAW_LIST     0x83D0u

/* btst / bclr / bset #bit on a byte of memory: only Z changes */
static int bit_test(uint32_t a, int bit)
{
    int on = rd8(a) >> bit & 1;
    c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (on ? 0 : CC_Z));
    return on;
}
static int bit_clear(uint32_t a, int bit) { int on = bit_test(a, bit); wr8(a, rd8(a) & ~(1 << bit)); return on; }
static int bit_set(uint32_t a, int bit)   { int on = bit_test(a, bit); wr8(a, rd8(a) | 1 << bit); return on; }

/* subq.b #1 on a byte of memory, its flags; nonzero */
static int count_down(uint32_t a)
{
    uint16_t cc = c->ccr;
    uint8_t v = (uint8_t)sub_b(&cc, 1, rd8(a));
    wr8(a, v); c->ccr = cc;
    return v;
}

/* $8200: the tile d6 at column d4, row d5 */
static void mark_at(void)
{
    c->d[7] = set_b(c->d[7], c->d[4]);
    c->d[7] = set_w(c->d[7], c->d[7] << 8);
    c->d[7] = set_b(c->d[7], c->d[5]);
    text_at();
    wr16(VDP_DATA, (uint16_t)c->d[6]);
    c->ccr = cc_move_w(c->ccr, c->d[6]);
}

/* a blink's state byte: bit 0 on, bit 1 shown; the next byte counts to its next change (20
 * frames). $8200 draws it: d6 the tile, d4/d5 the place */
static void blink_timer(uint32_t st) { wr8(st + 1, 0x14); }

/* the upper scroll arrow (more of the list above), at (22, 5): $80C4 hides it, $80D4 shows it */
static void up_hide(void)
{
    if (!bit_clear(0xFFFF9620u, 1)) return;
    c->d[6] = set_w(c->d[6], 0x8020);
    blink_timer(0xFFFF9620u);
    c->d[4] = set_w(c->d[4], 0x16); c->d[5] = set_w(c->d[5], 5);
    mark_at();
}
static void up_show(void)
{
    if (bit_test(0xFFFF9620u, 1)) up_hide();
    bit_set(0xFFFF9620u, 1);
    c->d[6] = set_w(c->d[6], 0x8013);
    blink_timer(0xFFFF9620u);
    c->d[4] = set_w(c->d[4], 0x16); c->d[5] = set_w(c->d[5], 5);
    mark_at();
}
/* $80A8: its blink, each frame */
static void up_blink(void)
{
    if (!bit_test(0xFFFF9620u, 0)) return;
    if (count_down(0xFFFF9621u)) return;
    if (bit_test(0xFFFF9620u, 1)) up_hide(); else up_show();
}

/* the lower scroll arrow (more below), at (22, 14): $8116 hides it, $8126 shows it */
static void down_hide(void)
{
    if (!bit_clear(0xFFFF9622u, 1)) return;
    c->d[6] = set_w(c->d[6], 0x8020);
    blink_timer(0xFFFF9622u);
    c->d[4] = 22; c->d[5] = 14;
    mark_at();
}
static void down_show(void)
{
    if (bit_test(0xFFFF9622u, 1)) down_hide();
    bit_set(0xFFFF9622u, 1);
    c->d[6] = set_w(c->d[6], 0x9013);
    blink_timer(0xFFFF9622u);
    c->d[4] = 22; c->d[5] = 14;
    mark_at();
}
/* $80FA: its blink */
static void down_blink(void)
{
    if (!bit_test(0xFFFF9622u, 0)) return;
    if (count_down(0xFFFF9623u)) return;
    if (bit_test(0xFFFF9622u, 1)) down_hide(); else down_show();
}

/* the menu cursor ($87B5) at the place in $9624 (column, row), last drawn at $9626: $81C0
 * hides it, $81D6 shows it (moved) */
static void cursor_hide(void)
{
    if (!bit_clear(0xFFFF9614u, 1)) return;
    c->d[4] = set_b(c->d[4], rd8(0xFFFF9626u));
    c->d[5] = set_b(c->d[5], rd8(0xFFFF9627u));
    c->d[6] = set_w(c->d[6], 0x8020);
    blink_timer(0xFFFF9614u);
    mark_at();
}
static void cursor_show(void)
{
    if (bit_test(0xFFFF9614u, 1)) cursor_hide();
    bit_set(0xFFFF9614u, 1);
    c->d[4] = set_b(c->d[4], rd8(0xFFFF9624u));
    c->d[5] = set_b(c->d[5], rd8(0xFFFF9625u));
    wr8(0xFFFF9626u, (uint8_t)c->d[4]);
    wr8(0xFFFF9627u, (uint8_t)c->d[5]);
    c->d[6] = set_w(c->d[6], 0x87B5);
    blink_timer(0xFFFF9614u);
    mark_at();
}
/* $81A8: its blink */
static void cursor_blink(void)
{
    if (!bit_test(0xFFFF9614u, 0)) return;
    if (count_down(0xFFFF9615u)) return;
    if (bit_test(0xFFFF9614u, 1)) cursor_hide(); else cursor_show();
}

static void sound(uint32_t n) { c->d[0] = n; call_68k(c, QUEUE_SOUND); }

/* $8212: up or down the list (PAD_HELD bits 0, 1) within its length, scrolled to keep the line
 * among the four shown ($83D0 redraws it); the cursor on it, a click */
static void list_move(void)
{
    uint16_t cc = c->ccr;
    c->d[3] = set_w(c->d[3], rd16(LIST_LINE));
    c->ccr = cc_move_w(c->ccr, c->d[3]);
    if (bit_test(PAD_HELD, 0)) {
        cc = c->ccr;
        c->d[3] = set_w(c->d[3], sub_w(&cc, 1, c->d[3]));
        c->ccr = cc;
        if (cc & CC_N) return;
    } else {
        if (!bit_test(PAD_HELD, 1)) return;
        cc = c->ccr;
        c->d[3] = set_w(c->d[3], add_w(&cc, 1, c->d[3]));
        cmp_w(&cc, rd16(LIST_LEN), c->d[3]);
        c->ccr = cc;
        if (!(cc & CC_C)) return;
    }
    wr16(LIST_LINE, (uint16_t)c->d[3]);
    cc = c->ccr;
    c->d[0] = set_w(c->d[0], sub_w(&cc, rd16(LIST_TOP), c->d[3]));
    c->ccr = cc;
    if (cc & CC_C) {
        wr16(LIST_TOP, (uint16_t)c->d[3]);
        call_68k(c, DRAW_LIST);
    } else {
        cmp_w(&cc, 4, c->d[0]);
        c->ccr = cc;
        if (!(cc & CC_C)) {
            c->d[0] = set_w(c->d[0], c->d[3]);
            cc = c->ccr;
            c->d[0] = set_w(c->d[0], sub_w(&cc, 3, c->d[0]));
            wr16(LIST_TOP, (uint16_t)c->d[0]);
            c->ccr = cc_move_w(cc, c->d[0]);
            call_68k(c, DRAW_LIST);
        }
    }
    cc = c->ccr;
    c->d[3] = set_w(c->d[3], rd16(LIST_LINE));
    c->d[3] = set_w(c->d[3], sub_w(&cc, rd16(LIST_TOP), c->d[3]));
    c->d[3] = set_w(c->d[3], add_w(&cc, c->d[3], c->d[3]));
    c->d[3] = set_w(c->d[3], add_w(&cc, 7, c->d[3]));
    wr8(0xFFFF9625u, (uint8_t)c->d[3]);
    c->ccr = cc;
    cursor_show();
    sound(37);
}

/* $802A: 32 tiles from 32 bytes at a0: the low nibble a tile, the high one its palette and flip
 * ($804A) */
static void tile_row(void)
{
    c->d[2] = 31;
    for (;;) {
        c->d[0] = rd8(c->a[0]++);
        c->d[1] = set_w(c->d[1], c->d[0]);
        c->d[0] = set_b(c->d[0], c->d[0] & 0xF);
        c->d[1] = set_b(c->d[1], (c->d[1] & 0xFF) >> 4);
        c->d[1] = set_b(c->d[1], c->d[1] * 2);
        c->d[0] = set_w(c->d[0], c->d[0] | rd16(0x804Au + (uint32_t)(int32_t)(int16_t)c->d[1]));
        wr16(VDP_DATA, (uint16_t)c->d[0]);
        c->ccr = (uint16_t)(cc_move_w(c->ccr, c->d[0]) & ~CC_X);     /* X: the add.b (no carry) */
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
        if ((c->d[2] & 0xFFFF) == 0xFFFF) break;
    }
}
/* $8024: a row of them at row d1 */
static void tile_row_at(void)
{
    c->d[0] = 0;
    text_at_rc();
    tile_row();
}
/* $8010: 32 long words (64 tiles) from a0 at row d1 */
static void tiles_at(void)
{
    c->d[0] = 0;
    text_at_rc();
    for (c->d[2] = 31; ; ) {
        uint32_t v = rd32(c->a[0]); c->a[0] += 4;
        wr32(VDP_DATA, v);
        c->ccr = cc_move_l(c->ccr, v);
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
        if ((c->d[2] & 0xFFFF) == 0xFFFF) break;
    }
}
static void row_at(uint32_t data, uint32_t row) { c->a[0] = data; c->d[1] = row; tile_row_at(); }
static void row_next(uint32_t data) { c->a[0] = data; tile_row(); }

/* $7F76: the frame's lower part (rows 4-7) */
static void frame_lower(void)
{
    row_at(0x8D22, 4); row_next(0x8D22); row_next(0x8DC2); row_next(0x8D42);
}
/* $7F54: the frame from the top (rows 0-3), then $7F76 */
static void frame_full(void)
{
    row_at(0x8CC2, 0); row_next(0x8E02); row_next(0x8D22); row_next(0x8DE2);
    frame_lower();
}
/* $7FBA: rows 14 and 15 */
static void frame_foot(void)
{
    row_at(0x8DA2, 14); row_at(0x8CE2, 15);
}
/* $7F98: the stats page's frame (rows 4-13), then $7FBA */
static void frame_stats(void)
{
    row_at(0x8D02, 4);
    for (c->d[3] = 8; ; ) {
        row_next(0x8D02);
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
        if ((c->d[3] & 0xFFFF) == 0xFFFF) break;
    }
    row_at(0x8D62, 7);
    frame_foot();
}
/* $7FCE: the items page's frame */
static void frame_items(void)
{
    row_at(0x8C82, 4);
    for (c->d[3] = 8; ; ) {
        row_next(0x8C82);
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
        if ((c->d[3] & 0xFFFF) == 0xFFFF) break;
    }
    row_next(0x8D82); row_next(0x8CA2);
}
/* $7FF6: 64 tiles from a0 at row 8, then a row */
static void frame_box(void)
{
    c->d[1] = 8; tiles_at(); row_next(0x8CA2);
}
/* $8000: rows 8 and 9 */
static void frame_pause(void)
{
    row_at(0x8D82, 8); row_at(0x8CA2, 9);
}

/* $7F00: the palette's step 8, to upload */
static void palette_8(void)
{
    wr8(PAL_STEP, 8);
    uint8_t v = rd8(GAME_FLAGS2) | 4;
    wr8(GAME_FLAGS2, v);
    c->ccr = tst_b(c->ccr, v);
}
/* $7EEE: the box ($7FF6), the palette's step $B */
static void box_dim(void)
{
    frame_box();
    wr8(PAL_STEP, 0x0B);
    uint8_t v = rd8(GAME_FLAGS2) | 4;
    wr8(GAME_FLAGS2, v);
    c->ccr = tst_b(c->ccr, v);
}

/* $7E7C: the stats page: its frame, the level and the labels AP, DP, SP ($84DE) at (2, 9-13) */
static void stats_page(void)
{
    frame_stats();
    frame_foot();
    level();
    c->d[7] = set_w(c->d[7], 0x209);
    c->a[1] = 0x84DEu;
    for (c->d[3] = 2; ; ) {
        text_at();
        string_bright();
        uint16_t cc = c->ccr;
        c->d[7] = set_b(c->d[7], add_b(&cc, 2, c->d[7]));
        c->ccr = cc;
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
        if ((c->d[3] & 0xFFFF) == 0xFFFF) break;
    }
}

/* $8E22: the screen's sprite records at $FF3300: 24 bytes from $8E3E, 36 zero */
static void sprite_init(void)
{
    c->a[0] = 0xFF3300u;
    c->a[1] = 0x8E3Eu;
    c->d[0] = 24;
    uint16_t cc = c->ccr;
    do { wr8(c->a[0]++, rd8(c->a[1]++)); c->d[0] = set_w(c->d[0], sub_w(&cc, 1, c->d[0])); } while (c->d[0] & 0xFFFF);
    for (c->d[1] = 35; ; ) {
        wr8(c->a[0]++, (uint8_t)c->d[0]);
        cc = (uint16_t)((cc & CC_X) | CC_Z);
        c->d[1] = set_w(c->d[1], c->d[1] - 1);
        if ((c->d[1] & 0xFFFF) == 0xFFFF) break;
    }
    c->ccr = cc;
}

static void click_back(void) { sound(38); }     /* $8BAA */
static void click(void)      { sound(37); }     /* $8BB0 */

/* call_68k leaves its mark in ret: cleared, as none of these goes on elsewhere */
#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(TextAt, text_at)                 /* $83B0 */
ENTRY(TextAtRC, text_at_rc)            /* $83B8 */
ENTRY(String, string)                  /* $7874 */
ENTRY(StringPlain, string_plain)       /* $7870 */
ENTRY(StringBright, string_bright)     /* $786A */
ENTRY(FixedName, fixed_name)           /* $7B70 */
ENTRY(LineMark, line_mark)             /* $7AAA */
ENTRY(MenuPlace, menu_place)           /* $7C56 */
ENTRY(MenuBright, menu_bright)         /* $7C4A */
ENTRY(MenuPlainBright, menu_plain_bright)   /* $7C44 */
ENTRY(Cells, cells)                    /* $7C9A */
ENTRY(Meter, meter)                    /* $7C68 */
void PressedB(Cpu *cpu)   { c = cpu; pressed(0xFFFFFF80u); }   /* $7D7A: B */
void PressedBS(Cpu *cpu)  { c = cpu; pressed(0xFFFFFF90u); }   /* $7D7E: B or Start */
void PressedAC(Cpu *cpu)  { c = cpu; pressed(0x60u); }         /* $7D8A: A or C */
ENTRY(MoreBelow, more_below)           /* $83A4 */
ENTRY(Blank, blank)                    /* $84AE */
ENTRY(Div10, div10)                    /* $8522 */
ENTRY(Digits, digits)                  /* $84EC */
ENTRY(TwoDigits, two_digits)           /* $8552 */
ENTRY(Level, level)                    /* $853C */
ENTRY(UpBlink, up_blink)        /* $80A8 */
ENTRY(UpHide, up_hide)          /* $80C4 */
ENTRY(DownBlink, down_blink)    /* $80FA */
ENTRY(DownHide, down_hide)      /* $8116 */
ENTRY(DownShow, down_show)      /* $8126 */
ENTRY(CursorBlink, cursor_blink) /* $81A8 */
ENTRY(CursorHide, cursor_hide)  /* $81C0 */
ENTRY(CursorShow, cursor_show)  /* $81D6 */
ENTRY(ListMove, list_move)      /* $8212 */
ENTRY(TileRowAt, tile_row_at)   /* $8024 */
ENTRY(TileRow, tile_row)        /* $802A */
ENTRY(TilesAt, tiles_at)        /* $8010 */
ENTRY(FrameFull, frame_full)    /* $7F54 */
ENTRY(FrameLower, frame_lower)  /* $7F76 */
ENTRY(FrameStats, frame_stats)  /* $7F98 */
ENTRY(FrameFoot, frame_foot)    /* $7FBA */
ENTRY(FrameItems, frame_items)  /* $7FCE */
ENTRY(FrameBox, frame_box)      /* $7FF6 */
ENTRY(FramePause, frame_pause)  /* $8000 */
ENTRY(Palette8, palette_8)      /* $7F00 */
ENTRY(BoxDim, box_dim)          /* $7EEE */
ENTRY(StatsPage, stats_page)    /* $7E7C */
ENTRY(SpriteInit, sprite_init)  /* $8E22 */
ENTRY(ClickBack, click_back)    /* $8BAA */
ENTRY(Click, click)             /* $8BB0 */
