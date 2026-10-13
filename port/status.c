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
#include "task.h"

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

/* ---- batch T3: the icons, the list, the pages ---- */

#define COPY_RAW_TILES 0x22A8u
#define NAME_OF        0x1E6Au      /* the name of thing d0 in NAME_TOP/NAME_BOTTOM, FIXED_LEN long */
#define NAME_TOP       0xFFFF8CE0u
#define NAME_BOTTOM    0xFFFF8CF0u
#define PAGE           0xFFFF95FAu  /* word: the page shown (0 the stats; 1-4 the item lists; 5, 6) */
#define MENU_OF        0xFFFF95E6u  /* word: the menu's own line, while a sub-menu runs */
#define LIST           0xFFFF9608u  /* the list's entries: a thing's number, bit 7 the one in use */
#define NO_REDRAW_ARROWS 0xFFFF95DBu

static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static void f_clr(void) { c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); }
static void cmp_byte(uint32_t s, uint32_t d) { uint16_t x = c->ccr & CC_X; sub_b(&c->ccr, s, d); c->ccr = (uint16_t)((c->ccr & ~CC_X) | x); }
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }   /* a word, as an index */

/* $876C: d5 copies of the tile d4 (0 is 65536); $8762 blanks, $875C zeros */
static void fill_d4(void)
{
    do {
        wr16(VDP_DATA, (uint16_t)c->d[4]);
        c->d[5] = set_w(c->d[5], sub_w(&c->ccr, 1, c->d[5]));
    } while (c->d[5] & 0xFFFF);
}
static void fill_blank(void) { c->d[4] = set_w(c->d[4], 0x8020); F_W(0x8020); fill_d4(); }
static void fill_zero(void)  { c->d[4] = set_w(c->d[4], 0); F_W(0); fill_d4(); }

/* $85FC: at d7 the tiles d6 and d6 + 2 (a1 the VDP's data port) */
static void tile_pair(void)
{
    text_at();
    wr16(c->a[1], (uint16_t)c->d[6]);
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 2, c->d[6]));
    wr16(c->a[1], (uint16_t)c->d[6]);
    F_W(c->d[6]);
}
/* $85F0: a 2 x 2 icon from tile d6 at d7 (the tiles go d6, d6+2 / d6+1, d6+3) */
static void tile_square(void)
{
    c->a[1] = VDP_DATA;
    tile_pair();
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
    c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    tile_pair();
}
/* $8608: two rows of two tiles from a0 at d7 */
static void tile_rows(void)
{
    text_at();
    uint32_t v = rd32(c->a[0]); c->a[0] += 4;
    wr32(VDP_DATA, v); F_L(v);
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
    text_at();
    v = rd32(c->a[0]); c->a[0] += 4;
    wr32(VDP_DATA, v); F_L(v);
}
/* $85CE: the mark d0 / 2 ($85EC its place, $85DC its tiles); $85CC the third */
static void mark(void)
{
    c->d[7] = set_w(c->d[7], rd16(0x85ECu + sx(c->d[0])));
    c->a[0] = 0x85DCu;
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    c->a[0] += sx(c->d[0]);
    tile_rows();
}
static void mark_2(void) { c->d[0] = 2; F_L(2); mark(); }

/* $8656: the icon of thing d3 (a byte; $FF none) at d7 from tile d6; d0 nonzero: its graphics
 * copied in first ($2000 + its tiles' index, 32 tiles, by CopyRawTiles) */
static void icon_of(void);
static void icon_tiles(uint32_t table)
{
    c->a[1] = table;
    if (table != 0x8C80u) c->a[1] += sx(c->d[3]);
    F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) {
        c->d[1] = 0;
        c->d[1] = set_w(c->d[1], 0x2000);
        c->d[1] = set_b(c->d[1], add_b(&c->ccr, rd8(c->a[1]), c->d[1]));
        c->d[2] = 32;
        call_68k(c, COPY_RAW_TILES);
    }
    tile_square();
}
static void icon_rest(void)                         /* $8678: a shield or armour; 4 its picture */
{
    cmp_w(&c->ccr, 4, c->d[3]);
    if ((c->d[3] & 0xFFFF) == 4) { c->a[0] = 0x7BAAu; tile_rows(); return; }
    icon_tiles(0x8690u);
}
static void icon_of(void)
{
    cmp_byte(0xFF, c->d[3]);
    if ((c->d[3] & 0xFF) == 0xFF) { icon_tiles(0x8C80u); return; }
    cmp_w(&c->ccr, 8, c->d[3]);
    if ((c->d[3] & 0xFFFF) < 8) { icon_tiles(0x869Cu); return; }
    c->d[3] = set_w(c->d[3], sub_w(&c->ccr, 8, c->d[3]));
    icon_rest();
}
/* $8620, $863C: the A + left and A + right slots' icons (graphics copied when the slot's thing
 * changed since last drawn) */
static void slot_icon_at(uint32_t pos, uint32_t tile, uint32_t slot, uint32_t shown, uint32_t copy)
{
    c->d[7] = set_w(c->d[7], pos);
    c->d[6] = set_w(c->d[6], tile);
    c->d[0] = 0; c->d[3] = 0;
    c->d[3] = set_b(c->d[3], rd8(slot));
    cmp_byte(rd8(shown), c->d[3]);
    if ((c->d[3] & 0xFF) != rd8(shown)) { c->d[0] = set_w(c->d[0], copy); F_W(copy); }
    icon_of();
}
static void left_slot(void)  { slot_icon_at(0x1601, 0x87E0, 0xFFFF9F10u, 0xFFFF95DCu, 0xFC00); }   /* $8620 */
static void right_slot(void) { slot_icon_at(0x1C01, 0x87E4, 0xFFFF9F11u, 0xFFFF95DDu, 0xFC80); }  /* $863C */
/* $8666: the items page's icon of the thing in $95EE (none if negative) */
static void items_icon(void)
{
    c->d[7] = set_w(c->d[7], 0x1605);
    c->d[0] = set_w(c->d[0], 0xFD00);
    c->d[6] = set_w(c->d[6], 0x87E8);
    c->d[3] = set_w(c->d[3], rd16(0xFFFF95EEu));
    F_W(c->d[3]);
    if (c->d[3] & 0x8000) { icon_tiles(0x8C80u); return; }
    icon_rest();
}

/* $86BC: the view after the screen: the player's camera limits by where it is ($8720 by
 * $95D1, $8750 in water, $874A in a shop) */
static void view_limits(void)
{
    c->a[0] = 0x874Au;
    if (!bit_test(0xFFFF9EF1u, 4)) {
        c->a[0] = 0x8750u;
        if (bit_test(0xFFFF966Bu, 0)) {
            cmp_byte(3, rd8(0xFFFF95D1u));
            if (rd8(0xFFFF95D1u) == 3) c->a[0] += 6;
        } else {
            c->d[0] = rd8(0xFFFF95D1u);
            c->d[1] = set_w(c->d[1], c->d[0]);
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
            c->a[0] = 0x8720u + sx(c->d[0]);
        }
    }
    c->d[0] = rd8(c->a[0]++);
    c->d[0] = c->d[0] << 16 | (c->d[0] >> 16);
    c->d[0] = set_b(c->d[0], rd8(c->a[0]++));
    wr32(0xFFFF9EFAu, c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[0]++));
    c->d[0] = ext_w(c->d[0]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));      /* asl.w #4: V aside, as lsl */
    wr16(0xFFFF9EF2u, (uint16_t)c->d[0]);
    c->d[0] = rd8(c->a[0]++);
    wr16(0xFFFF9EF4u, (uint16_t)c->d[0]);
    c->d[0] = rd8(c->a[0]++);
    c->d[0] = c->d[0] << 16 | (c->d[0] >> 16);
    c->d[0] = set_b(c->d[0], rd8(c->a[0]++));
    c->d[0] = lsl_n(&c->ccr, c->d[0], 4, 32);
    wr32(0xFFFF9EF6u, c->d[0]);
    F_L(c->d[0]);
}

/* $8272: the page's title in its box ($82B6 the names, $82B2 the place) */
static void title(void)
{
    c->d[0] = set_w(c->d[0], rd16(PAGE));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[1] = 0x82B6u + sx(rd16(0x82B6u + sx(c->d[0])));
    c->d[1] = 0;
    cmp_w(&c->ccr, 10, c->d[0]);
    if (!(c->ccr & CC_N)) c->d[1] = 2;
    c->d[7] = set_w(c->d[7], rd16(0x82B2u + c->d[1]));
    text_at();
    c->a[0] = VDP_DATA;
    wr32(VDP_DATA, 0x87AE87AFu);
    wr16(VDP_DATA, 0x8020);
    string_bright();
    wr16(VDP_DATA, 0x8020);
    wr32(VDP_DATA, 0x8FAF8FAEu);
    F_L(0x8FAF8FAEu);
}

/* $8326: the list's scroll arrows by its top and length */
static void arrows(void)
{
    F_W(rd16(LIST_TOP));
    if (!rd16(LIST_TOP)) {
        if (bit_test(0xFFFF9620u, 0)) { up_hide(); wr8(0xFFFF9620u, 0); f_clr(); }
    } else if (!bit_set(0xFFFF9620u, 0)) {
        more_below();
        if (c->ccr & CC_Z) { down_hide(); wr8(0xFFFF9622u, 0); f_clr(); up_show(); return; }
        bit_set(0xFFFF9622u, 0);
        wr8(0xFFFF9623u, 0x14); F_B(0x14);
        down_show();
        up_show();
        return;
    }
    more_below();
    if (!(c->ccr & CC_C)) {
        if (bit_test(0xFFFF9622u, 0)) { down_hide(); wr8(0xFFFF9622u, 0); f_clr(); }
        return;
    }
    if (bit_set(0xFFFF9622u, 0)) return;
    down_show();
    F_W(rd16(LIST_TOP));
    if (!rd16(LIST_TOP)) return;
    bit_set(0xFFFF9620u, 0);
    wr8(0xFFFF9621u, 0x14); F_B(0x14);
    up_show();
}

/* $82F4: the list of the menu's things: the bits of its byte ($95E6's, at -$6A36) are the
 * things owned, the one in use ($-6A32) marked; then the arrows */
static void build_list(void)
{
    c->a[0] = sx(rd16(MENU_OF));
    c->d[1] = set_b(c->d[1], rd8(c->a[0] - 0x6A36));
    c->d[3] = set_b(c->d[3], rd8(c->a[0] - 0x6A32));
    c->d[2] = 0; c->d[0] = 0;
    c->a[0] = LIST;
    do {
        if (c->d[1] & 1) {
            c->d[2] = set_w(c->d[2], c->d[2] + 1);
            wr8(c->a[0], (uint8_t)c->d[0]);
            if ((c->d[0] & 0xFF) == (c->d[3] & 0xFF)) wr8(c->a[0], rd8(c->a[0]) | 0x80);
            c->a[0]++;
        }
        c->d[0] = set_b(c->d[0], c->d[0] + 1);
        c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 1, 8));
    } while (c->d[1] & 0xFF);
    wr16(LIST_LEN, (uint16_t)c->d[2]);
    F_W(c->d[2]);
    arrows();
}

/* $8476: a list line at d7: the name row at a0, centred in 14 columns (pads in the frame) */
static void list_row(uint16_t left, uint16_t right, uint16_t base)
{
    text_at();
    if (left) {
        c->d[0] = set_w(c->d[0], left); c->d[1] = set_w(c->d[1], 0x8020);
        do { wr16(c->a[1], 0x8020); c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0])); } while (c->d[0] & 0xFFFF);
    } else F_W(0);
    c->d[1] = set_w(c->d[1], base);
    c->d[0] = set_w(c->d[0], rd16(FIXED_LEN));
    do {
        c->d[1] = set_b(c->d[1], rd8(c->a[0]++));
        wr16(c->a[1], (uint16_t)c->d[1]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    } while (c->d[0] & 0xFFFF);
    c->d[0] = set_w(c->d[0], right);
    F_W(right);
    if (right) {
        c->d[1] = set_w(c->d[1], 0x8020);
        do { wr16(c->a[1], 0x8020); c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0])); } while (c->d[0] & 0xFFFF);
    }
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
}

/* $83D0: the list's four lines from its top (names by $1E6A, the one in use bright), blank
 * lines after its end; then the arrows, unless NO_REDRAW_ARROWS */
static void draw_list(void)
{
    uint32_t sp = c->a[7] - 4;                      /* link a6,#-12 */
    wr32(sp, c->a[6]); c->a[6] = sp; c->a[7] = sp - 12;
    c->d[7] = set_w(c->d[7], 0xF06);
    c->d[0] = set_w(c->d[0], rd16(LIST_LEN));
    c->d[2] = set_w(c->d[2], rd16(LIST_TOP));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[2], c->d[0]));
    c->d[1] = 4;
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if (!(c->ccr & CC_C)) { c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]); }
    uint16_t lines = (uint16_t)c->d[0];
    wr16(c->a[6] - 2, lines);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, c->d[0], c->d[1]));
    uint16_t blanks = (uint16_t)c->d[1];
    wr16(c->a[6] - 4, blanks);
    c->a[0] = LIST + sx(c->d[2]);
    wr16(c->a[6] - 6, (uint16_t)c->a[0]);
    do {
        c->a[0] = sx(rd16(c->a[6] - 6));
        c->d[1] = set_w(c->d[1], 0x8000);
        c->d[0] = set_b(c->d[0], rd8(c->a[0]++));
        F_B(c->d[0]);
        if (c->d[0] & 0x80) { c->d[1] = set_w(c->d[1], 0xA000); wr16(c->a[6] - 6, (uint16_t)c->a[0]); }
        uint16_t base = (uint16_t)c->d[1];
        wr16(c->a[6] - 8, base);
        c->d[0] = set_w(c->d[0], c->d[0] & 7);
        F_W(c->d[0]);
        wr16(c->a[6] - 6, (uint16_t)c->a[0]);
        c->d[1] = set_w(c->d[1], rd16(PAGE));
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 1, c->d[1]));
        c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 3, 16));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
        call_68k(c, NAME_OF);
        c->d[0] = 0;
        c->d[1] = 14;
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(FIXED_LEN), c->d[1]));
        c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 1, 16));
        uint32_t x = c->ccr & CC_X ? 1 : 0;           /* addx.w d1,d0: the odd column right */
        uint16_t r = (uint16_t)(c->d[1] + x);
        c->d[0] = set_w(c->d[0], r);
        c->ccr = (uint16_t)((c->ccr & ~(CC_X | CC_N | CC_V | CC_C)) | (r & 0x8000 ? CC_N : 0) | (r ? 0 : 0) );
        if (r) c->ccr &= ~CC_Z;
        uint16_t left = (uint16_t)c->d[1], right = r;
        wr16(c->a[6] - 10, left); wr16(c->a[6] - 12, right);
        c->a[1] = VDP_DATA;
        c->a[0] = NAME_TOP; list_row(left, right, base);
        c->a[0] = NAME_BOTTOM; list_row(left, right, base);
        lines = (uint16_t)sub_w(&c->ccr, 1, rd16(c->a[6] - 2));
        wr16(c->a[6] - 2, lines);
    } while (lines);
    F_W(blanks);
    while (blanks) {
        blank(); blank();
        blanks = (uint16_t)sub_w(&c->ccr, 1, rd16(c->a[6] - 4));
        wr16(c->a[6] - 4, blanks);
    }
    c->a[7] = c->a[6]; c->a[6] = rd32(c->a[7]); c->a[7] += 4;   /* unlk */
    F_B(rd8(NO_REDRAW_ARROWS));
    if (rd8(NO_REDRAW_ARROWS)) return;
    arrows();
}

/* $76B4: a list page: the list, the title, the lines */
static void list_page(void)
{
    build_list();
    c->d[7] = set_w(c->d[7], 0xF04);
    title();
    draw_list();
}

/* $7808: the page's panel blanked (9 rows of 17 from (13, 6)) */
static void clear_panel(void)
{
    c->d[7] = set_w(c->d[7], 0xD06);
    for (c->d[3] = 8; ; ) {
        text_at();
        c->d[5] = 17;
        fill_blank();
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
        if ((c->d[3] & 0xFFFF) == 0xFFFF) break;
    }
}
/* $7E72: 13 blanks at d7 */
static void blank_13(void) { text_at(); c->d[5] = 13; fill_blank(); }

/* $757A: the line chosen, doubled (an index) */
static void line_x2(void)
{
    c->d[0] = set_w(c->d[0], rd16(LIST_LINE));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
}

/* $7820: into the sub-menu of line LIST_LINE: its page (1-4 a list, 5 the magic, 6 the items),
 * the cursor at its first line */
static void open_line(void)
{
    wr16(LIST_TOP, 0); f_clr();
    c->d[0] = set_w(c->d[0], rd16(LIST_LINE));
    wr16(LIST_LINE, 0); f_clr();
    wr16(MENU_OF, (uint16_t)c->d[0]);
    c->a[0] = sx(c->d[0]);
    wr8(0xFFFF95D6u, rd8(c->a[0] - 0x6A32)); F_B(rd8(c->a[0] - 0x6A32));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0]));
    wr16(PAGE, (uint16_t)c->d[0]); F_W(c->d[0]);
    cmp_w(&c->ccr, 5, c->d[0]);
    uint16_t p = (uint16_t)c->d[0];
    if (p < 5) { list_page(); wr16(0xFFFF9624u, 0xE07); F_W(0xE07); }
    else if (p == 5) { wr8(0xFFFF9614u, 0); f_clr(); cursor_show(); return; }
    else { wr16(0xFFFF9624u, 0x206); F_W(0x206); }
    wr8(0xFFFF9614u, 1); F_B(1);
    cursor_show();
}

/* $77CA: the main menu: the panel cleared, the title, its six lines ($7D6E places, $82CB names),
 * the cursor on the line chosen */
static void main_menu(void)
{
    clear_panel();
    title();
    c->a[0] = 0x7D6Eu;
    c->a[1] = 0x82CBu;
    for (c->d[2] = 5; ; ) {
        c->d[7] = set_w(c->d[7], rd16(c->a[0])); c->a[0] += 2; F_W(c->d[7]);
        text_at();
        string_plain();
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
        if ((c->d[2] & 0xFFFF) == 0xFFFF) break;
    }
    line_x2();
    c->a[0] = 0x7D6Eu + sx(c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[0]++));
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
    wr8(0xFFFF9624u, (uint8_t)c->d[0]); F_B(c->d[0]);
    wr8(0xFFFF9625u, rd8(c->a[0]++)); F_B(rd8(c->a[0] - 1));
    wr8(0xFFFF9614u, 1); F_B(1);
    cursor_show();
}

/* $7D00: the cursor moved round the menu by the d-pad ($7D4A: the line each way from each) */
static void menu_move(void)
{
    for (c->d[2] = 3; ; ) {
        c->d[0] = set_b(c->d[0], rd8(0x7D46u + sx(c->d[2]))); F_B(c->d[0]);
        if (bit_test(0xFFFF95D9u, (int)(c->d[0] & 7))) break;
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
        if ((c->d[2] & 0xFFFF) == 0xFFFF) return;
    }
    click();
    c->d[0] = set_w(c->d[0], rd16(LIST_LINE));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    c->d[2] = set_w(c->d[2], add_w(&c->ccr, c->d[0], c->d[2]));
    c->d[0] = set_b(c->d[0], rd8(0x7D4Au + sx(c->d[2]))); F_B(c->d[0]);
    wr16(LIST_LINE, (uint16_t)c->d[0]); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    F_W(rd16(PAGE));
    if (rd16(PAGE)) {
        wr16(0xFFFF9624u, rd16(0x7D62u + sx(c->d[0]))); F_W(rd16(0x7D62u + sx(c->d[0])));
    } else {
        wr16(0xFFFF9624u, rd16(0x7D6Eu + sx(c->d[0]))); F_W(rd16(0x7D6Eu + sx(c->d[0])));
        wr8(0xFFFF9624u, (uint8_t)sub_b(&c->ccr, 1, rd8(0xFFFF9624u)));
    }
    cursor_show();
}

/* $7CA4: take off the thing of the line chosen if it is in a slot (A + left, right): its slot
 * emptied, its name plain, the slot's icon redrawn; d6 0 (eq) if so, else -1 */
static void take_off(void)
{
    c->d[1] = set_w(c->d[1], rd16(LIST_LINE)); F_W(c->d[1]);
    cmp_w(&c->ccr, 5, rd16(MENU_OF));
    if (rd16(MENU_OF) != 5) {
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[1], c->d[1]));
        c->a[0] = sx(c->d[1]);
        c->d[1] = set_w(c->d[1], rd16(c->a[0] - 0x6A12)); F_W(c->d[1]);
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, 8, c->d[1]));
    }
    for (c->d[2] = 1; ; ) {
        c->a[0] = sx(c->d[2]);
        cmp_byte(rd8(c->a[0] - 0x60F0), c->d[1]);
        if (rd8(c->a[0] - 0x60F0) == (c->d[1] & 0xFF)) {
            wr8(c->a[0] - 0x6A24, rd8(c->a[0] - 0x60F0));
            c->d[0] = 0xFFFFFFFFu;
            wr8(c->a[0] - 0x60F0, 0xFF);
            wr8(c->a[0] - 0x6A1E, 0xFF); F_B(0xFF);
            cmp_byte(8, c->d[1]);
            if ((c->d[1] & 0xFF) < 8) { line_x2(); menu_place(); string_plain(); }
            F_W(c->d[2]);
            if (c->d[2] & 0xFFFF) right_slot(); else left_slot();
            c->d[6] = 0; F_L(0);
            return;
        }
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
        if ((c->d[2] & 0xFFFF) == 0xFFFF) break;
    }
    c->d[6] = 0xFFFFFFFFu; F_L(c->d[6]);
}

/* $7BB2: the special actions' page: six names ($7C0A, the slotted ones bright), each with its
 * meter of uses (left of the most); the title */
static void magic_page(void)
{
    frame_items();
    c->d[4] = 0;
    c->a[1] = 0x7C0Au;
    do {
        c->d[7] = set_b(c->d[7], rd8(c->a[1]++));
        c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 8, 16));
        c->d[7] = set_b(c->d[7], rd8(c->a[1]++));
        text_at();
        cmp_byte(rd8(0xFFFF9F10u), c->d[4]);
        int bright = (c->d[4] & 0xFF) == rd8(0xFFFF9F10u);
        if (!bright) { cmp_byte(rd8(0xFFFF9F11u), c->d[4]); bright = (c->d[4] & 0xFF) == rd8(0xFFFF9F11u); }
        if (bright) string_bright(); else string_plain();
        c->a[0] = sx(c->d[4]);
        wr8(BAR_FULL, rd8(c->a[0] - 0x6A48)); F_B(rd8(c->a[0] - 0x6A48));
        c->d[0] = set_b(c->d[0], rd8(c->a[0] - 0x6A42));
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, rd8(BAR_FULL), c->d[0]));
        wr8(BAR_REST, (uint8_t)c->d[0]); F_B(c->d[0]);
        meter();
        c->d[4] = set_w(c->d[4], add_w(&c->ccr, 1, c->d[4]));
        cmp_w(&c->ccr, 6, c->d[4]);
    } while ((c->d[4] & 0xFFFF) < 6);
    title();
}

/* $7B86: the next place (a3); a picture from a0 if the byte at a2 is set, else skipped */
static void maybe_picture(void)
{
    c->d[7] = set_w(c->d[7], rd16(c->a[3])); c->a[3] += 2; F_W(c->d[7]);
    F_B(rd8(c->a[2]));
    if (rd8(c->a[2])) { tile_rows(); return; }
    c->a[0] += 8;
}

/* $7AC0: the items page: the twelve things owned ($-6A74) by name ($1E6A, fixed width, at
 * $7B0A); then the pictures and counts ($7B92 places, $7B9A pictures, $959C counts) and the icon
 * of the thing in use */
static void items_page(void)
{
    frame_items();
    c->d[2] = 0;
    do {
        c->a[0] = sx(c->d[2]);
        c->d[4] = set_b(c->d[4], rd8(c->a[0] - 0x6A74)); F_B(c->d[4]);
        if (c->d[4] & 0xFF) {
            c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->d[2]);
            c->d[0] = set_w(c->d[0], c->d[2] | 0x30); F_W(c->d[0]);
            call_68k(c, NAME_OF);
            c->d[2] = set_w(c->d[2], rd16(c->a[7])); c->a[7] += 2; F_W(c->d[2]);
            F_W(rd16(FIXED_LEN));
            if (rd16(FIXED_LEN)) {
                c->d[0] = set_w(c->d[0], c->d[2]);
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
                c->d[7] = set_w(c->d[7], rd16(0x7B0Au + sx(c->d[0]))); F_W(c->d[7]);
                text_at();
                c->a[1] = NAME_TOP; fixed_name();
                c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
                text_at();
                c->a[1] = NAME_BOTTOM; fixed_name();
            }
        }
        c->d[2] = set_w(c->d[2], add_w(&c->ccr, 1, c->d[2]));
        cmp_w(&c->ccr, 12, c->d[2]);
    } while ((c->d[2] & 0xFFFF) < 12);
    c->a[0] = 0x7B9Au;
    c->a[2] = 0xFFFF959Cu;
    c->a[3] = 0x7B92u;
    c->d[7] = set_w(c->d[7], rd16(c->a[3])); c->a[3] += 2;
    wr8(TWO_DIGITS, rd8(c->a[2]++)); F_B(rd8(c->a[2] - 1));
    tile_rows();
    c->d[7] = set_w(c->d[7], rd16(c->a[3])); c->a[3] += 2; F_W(c->d[7]);
    text_at();
    c->d[7] = set_w(c->d[7], 0x506); F_W(0x506);
    two_digits();
    maybe_picture();
    wr16(0xFFFF95F0u, 0xFFFF); F_W(0xFFFF);
    c->a[2] -= 5;
    F_B(rd8(c->a[2]));
    if (rd8(c->a[2])) { wr16(0xFFFF95F0u, 4); F_W(4); }
    maybe_picture();
    title();
    c->a[2] += 6;
    c->d[3] = set_b(c->d[3], rd8(c->a[2]));
    c->d[3] = ext_w(c->d[3]);
    wr16(0xFFFF95EEu, (uint16_t)c->d[3]); F_W(c->d[3]);
    if (!(c->d[3] & 0x8000)) items_icon();
}

/* $76E8: the stats: AP, DP, SP from the things in use ($77A2 their points, by slot and thing;
 * the shield's DP only when the player can hold one) as bars of five ($8018 full) */
static void stats(void)
{
    c->d[3] = 0;
    wr16(0xFFFF95B4u, 0); wr8(0xFFFF95B6u, 0); wr8(0xFFFF95C4u, 0); f_clr();
    c->a[0] = 0xFFFF95CEu;
    if (!bit_test(0xFFFF9EF1u, 4)) {
        cmp_byte(4, rd8(c->a[0]));
        if (rd8(c->a[0]) >= 4) wr8(0xFFFF95C4u, 0xFF);
    }
    for (c->d[0] = 3; ; ) {
        c->d[1] = 7;
        int skip = 0;
        if (!bit_test(0xFFFF9EF1u, 4)) {
            c->d[1] = 0;
            c->d[1] = set_b(c->d[1], rd8(c->a[0]++)); F_B(c->d[1]);
            skip = (c->d[1] & 0x80) != 0;
        }
        if (!skip) {
            c->a[1] = 0x77A2u + sx(rd16(0x77A2u + sx(c->d[3])));
            c->a[1] += sx(c->d[1]);
            c->a[2] = 0xFFFF95B5u;
            cmp_w(&c->ccr, 3, c->d[0]);
            if ((c->d[0] & 0xFFFF) == 3) c->a[2] -= 1;
            c->d[1] = set_b(c->d[1], rd8(c->a[1]++));
            c->d[2] = set_b(c->d[2], c->d[1]);
            c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 8));
            wr8(c->a[2], (uint8_t)add_b(&c->ccr, c->d[1], rd8(c->a[2]))); c->a[2]++;
            c->d[2] = set_b(c->d[2], c->d[2] & 0xF); F_B(c->d[2]);
            cmp_w(&c->ccr, 1, c->d[0]);
            int add2 = 1;
            if ((c->d[0] & 0xFFFF) == 1) { F_B(rd8(0xFFFF95C4u)); if (rd8(0xFFFF95C4u) & 0x80) add2 = 0; }
            if (add2) { wr8(c->a[2], (uint8_t)add_b(&c->ccr, c->d[2], rd8(c->a[2]))); c->a[2]++; }
        }
        c->d[3] = set_w(c->d[3], add_w(&c->ccr, 2, c->d[3]));
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
        if ((c->d[0] & 0xFFFF) == 0xFFFF) break;
    }
    c->d[7] = set_w(c->d[7], 0x500);
    c->a[0] = 0xFFFF95B4u;
    c->d[2] = 0;
    do {
        c->d[7] = set_b(c->d[7], rd8(0x779Eu + sx(c->d[2])));
        text_at();
        c->d[1] = 5;
        c->d[0] = set_b(c->d[0], rd8(c->a[0]++)); F_B(c->d[0]);
        int blanks = 1;
        if (c->d[0] & 0xFF) {
            cmp_byte(5, c->d[0]);
            if ((c->d[0] & 0xFF) >= 5) c->d[0] = 5;
            c->d[1] = set_b(c->d[1], sub_b(&c->ccr, c->d[0], c->d[1]));
            do {
                wr16(VDP_DATA, 0x8018);
                c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
            } while (c->d[0] & 0xFF);
            F_B(c->d[1]);
            blanks = (c->d[1] & 0xFF) != 0;
        }
        if (blanks)
            do {
                wr16(VDP_DATA, 0x8020);
                c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 1, c->d[1]));
            } while (c->d[1] & 0xFF);
        c->d[2] = set_w(c->d[2], add_w(&c->ccr, 1, c->d[2]));
        cmp_w(&c->ccr, 3, c->d[2]);
    } while ((c->d[2] & 0xFFFF) != 3);
}

/* ---- batch T4: the menus ----
 * The screen's menus wait for the next frame part-way through. The original's menu routines
 * keep their return addresses in RAM ($9630-$9654), not on the stack, so that Yield (which
 * returns to the scheduler) can be called from any depth of them; the C versions here are
 * ordinary functions that write the same addresses (where the original would return: after its
 * call) and wait with task_wait_frame at the original's resume addresses (port/task.h). */

#define SLOT_ADDR      0xFFFF8A4Cu
#define CURSOR_SPRITE  0x000Cu          /* the entity of the sprite cursor beside the slots */
#define CLEAR_ENTITY   0xA36u
#define SET_ANIM       0x7E8u
#define WINDOW_REDRAW  0x89B6u
#define GOLD_LABEL     0x8596u
#define CHOOSING       0xFFFF95E0u      /* byte: a slot (A + left/right) being chosen */
#define CHOSEN_SLOT    0xFFFF95E4u      /* word: 0 left, 1 right */

static void yield_at(uint32_t resume) { Cpu *me = c; task_wait_frame(me, resume); c = me; }
static void ret_to(uint32_t cell, uint32_t addr) { wr32(cell, addr); }

/* $805A: the next frame, then the menus' buttons ($95D9: pressed, or held 16 frames then every
 * 8), and the three blinks */
static void wait_input(uint32_t ret)
{
    ret_to(0xFFFF9634u, ret);
    yield_at(0x8062);
    c->d[0] = set_b(c->d[0], rd8(PAD_NEW)); F_B(c->d[0]);
    if (c->d[0] & 0xFF) {
        wr8(0xFFFF95D8u, 0x10);
        wr8(0xFFFF95D7u, rd8(0xFFFF8A7Au));
    } else {
        c->d[0] = set_b(c->d[0], rd8(0xFFFF8A7Au)); F_B(c->d[0]);
        if (c->d[0] & 0xFF) {
            cmp_byte(rd8(0xFFFF95D7u), c->d[0]);
            if ((c->d[0] & 0xFF) == rd8(0xFFFF95D7u) && !count_down(0xFFFF95D8u)) {
                wr8(0xFFFF95D8u, 8);
            } else {
                wr8(0xFFFF95D7u, (uint8_t)c->d[0]);
                c->d[0] = 0;
            }
        }
    }
    wr8(PAD_HELD, (uint8_t)c->d[0]); F_B(c->d[0]);
    cursor_blink();
    up_blink();
    down_blink();
}

/* $7952, $794E: the sprite cursor beside the slot chosen (x by the slot, $799A; y d6) */
static void cursor_sprite(void)
{
    c->a[4] = CURSOR_SPRITE;
    call_68k(c, CLEAR_ENTITY);
    line_x2();
    uint32_t e = c->a[4];
    wr16(e - 0x3800, rd16(0x799Au + sx(c->d[0])));
    wr16(e - 0x3700, (uint16_t)c->d[6]);
    wr16(e - 0x3E00, 4);
    wr8(e - 0x3FFD, rd8(e - 0x3FFD) | 0x40);
    wr16(e - 0x3900, 0xFF01);
    wr8(e - 0x3CFD, 0);
    wr8(e - 0x3FFE, 0x80);
    wr16(e - 0x3D00, 0x1602);
    c->d[0] = 0;
    call_68k(c, SET_ANIM);
    wr8(c->a[4] - 0x4000, 0xC0); F_B(0xC0);
}
static void cursor_sprite_top(void)  { c->d[6] = 16; F_L(16); cursor_sprite(); }    /* $794E */
static void cursor_sprite_low(void)  { c->d[6] = 48; F_L(48); cursor_sprite(); }    /* $7952 */
/* $7924, $7938: left or right pressed: the other slot, the sprite cursor there */
static void slot_flip(void (*draw)(void))
{
    c->d[0] = 12 & rd8(PAD_HELD); F_B(c->d[0]);
    if (!c->d[0]) return;
    click();
    wr16(LIST_LINE, rd16(LIST_LINE) ^ 1); F_W(rd16(LIST_LINE));
    draw();
}
static void slot_flip_top(void) { slot_flip(cursor_sprite_top); }   /* $7924 */
static void slot_flip_low(void) { slot_flip(cursor_sprite_low); }   /* $7938 */

/* $799E: choosing a slot with left/right (the sprite cursor; CHOOSING: from the top, the line
 * kept in CHOSEN_SLOT); B or Start: d6 -1, A or C: d6 0 */
static void choose_slot(uint32_t ret)
{
    ret_to(0xFFFF964Cu, ret);
    F_B(rd8(CHOOSING));
    if (rd8(CHOOSING)) {
        wr16(CHOSEN_SLOT, rd16(LIST_LINE));
        wr16(LIST_LINE, 0); f_clr();
        cursor_sprite_top();
    }
    for (;;) {
        wait_input(0x79B8);
        pressed(0xFFFFFF90u);
        if (c->d[6]) { wr8(0xFFFFC00Cu, 0); f_clr(); break; }
        F_B(rd8(CHOOSING));
        if (rd8(CHOOSING)) slot_flip_top(); else slot_flip_low();
        if (!(c->ccr & CC_Z)) continue;
        pressed(0x60);
        if (!c->d[6]) continue;
        c->d[6] = 0; F_L(0);
        wr8(0xFFFFC00Cu, 0); f_clr();
        break;
    }
    click_back();
}

/* $79EE: put the special action of the line chosen in a slot: the slot's old one taken out
 * (its name plain), the new one's name bright, the slot's icon redrawn */
static void put_in_slot(uint32_t ret)
{
    ret_to(0xFFFF9640u, ret);
    wr8(CHOOSING, 1); F_B(1);
    choose_slot(0x79FA);
    F_W(c->d[6]);
    if (!(c->d[6] & 0x8000)) {
        c->a[0] = sx(rd16(LIST_LINE));
        wr8(c->a[0] - 0x6A24, rd8(c->a[0] - 0x60F0));
        wr8(c->a[0] - 0x60F0, rd8(0xFFFF95E5u));
        c->d[0] = 0;
        c->d[0] = set_b(c->d[0], rd8(c->a[0] - 0x6A1E)); F_B(c->d[0]);
        if (c->d[0] & 0x80) menu_bright();
        else { c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0])); menu_plain_bright(); }
        wr8(c->a[0] - 0x6A1E, rd8(0xFFFF95E5u)); F_B(rd8(0xFFFF95E5u));
        F_W(rd16(LIST_LINE));
        if (rd16(LIST_LINE)) right_slot(); else left_slot();
    }
    wr16(LIST_LINE, rd16(CHOSEN_SLOT)); F_W(rd16(CHOSEN_SLOT));
}

/* $7A44: put the item of the items page in a slot */
static void item_in_slot(uint32_t ret)
{
    ret_to(0xFFFF9654u, ret);
    wr8(CHOOSING, 1); F_B(1);
    choose_slot(0x7A52);
    F_W(c->d[6]);
    if (c->d[6] & 0xFFFF) {
        c->d[6] = 0; F_L(0);
    } else {
        c->d[0] = set_w(c->d[0], rd16(0xFFFF95EEu)); F_W(c->d[0]);
        F_W(rd16(CHOSEN_SLOT));
        if (rd16(CHOSEN_SLOT)) { c->d[0] = set_w(c->d[0], rd16(0xFFFF95F0u)); F_W(c->d[0]); }
        c->a[0] = sx(rd16(LIST_LINE));
        wr8(c->a[0] - 0x6A24, rd8(c->a[0] - 0x60F0));
        c->d[0] = set_b(c->d[0], add_b(&c->ccr, 8, c->d[0]));
        wr8(c->a[0] - 0x60F0, (uint8_t)c->d[0]); F_B(c->d[0]);
        c->a[0] = sx(rd16(CHOSEN_SLOT));
        c->a[0] += sx(c->a[0]);
        c->d[1] = set_w(c->d[1], rd16(c->a[0] - 0x6A12)); F_W(c->d[1]);
        F_W(rd16(LIST_LINE));
        if (!rd16(LIST_LINE)) left_slot(); else right_slot();
        c->d[6] = 0xFFFFFFFFu; F_L(c->d[6]);
    }
    c->d[2] = set_w(c->d[2], 0x8020);
    c->d[0] = set_w(c->d[0], rd16(CHOSEN_SLOT)); F_W(c->d[0]);
    line_mark();
    wr16(LIST_LINE, rd16(CHOSEN_SLOT)); F_W(rd16(CHOSEN_SLOT));
}

/* $7882: the items page's choosing: a line with the sprite cursor; one that can be used is
 * marked and put in a slot ($7A44); d6 -1 when done */
static void items_choose(uint32_t ret)
{
    ret_to(0xFFFF9650u, ret);
    for (;;) {
        wr8(CHOOSING, 0); f_clr();
        cursor_sprite_low();
        choose_slot(0x7892);
        F_W(c->d[6]);
        if (c->d[6] & 0x8000) break;
        line_x2();
        c->a[0] = sx(c->d[0]);
        F_W(rd16(c->a[0] - 0x6A12));
        if (rd16(c->a[0] - 0x6A12) & 0x8000) { sound(53); continue; }
        take_off();
        if (c->ccr & CC_Z) continue;
        c->d[2] = set_w(c->d[2], 0x87B5);
        c->d[0] = set_w(c->d[0], rd16(LIST_LINE)); F_W(c->d[0]);
        if (c->d[0] & 0xFFFF) c->d[2] = set_w(c->d[2], 0x8FB5);
        line_mark();
        item_in_slot(0x78C8);
        F_W(c->d[6]);
        if (c->d[6] & 0xFFFF) break;
    }
    click_back();
    wr16(LIST_LINE, rd16(MENU_OF)); F_W(rd16(MENU_OF));
}

/* $78DC: the special actions' page: A or C on one with uses puts it in a slot; B or Start
 * leaves (d6 -1) */
static void magic_choose(uint32_t ret)
{
    ret_to(0xFFFF9644u, ret);
    pressed(0xFFFFFF90u);
    if (!c->d[6]) {
        pressed(0x60);
        if (!c->d[6]) return;
        c->a[0] = sx(rd16(LIST_LINE));
        F_B(rd8(c->a[0] - 0x6A48));
        if (!rd8(c->a[0] - 0x6A48)) { c->d[6] = 0; F_L(0); sound(53); return; }
        take_off();
        if (c->ccr & CC_Z) { click_back(); return; }
        click_back();
        put_in_slot(0x7906);
    }
    wr16(LIST_LINE, rd16(MENU_OF)); F_W(rd16(MENU_OF));
    c->d[6] = 0xFFFFFFFFu; F_L(c->d[6]);
    click_back();
}

/* $7D9C: a list page's buttons: up/down; A or C wears the thing of the line (its byte in the
 * list menu's -$6A33, the list re-marked, the stats and lines redrawn); B or Start leaves (d6
 * -1; arrows hidden). A thing of kind 7 (none) only buzzes */
static void list_buttons(void)
{
    list_move();
    pressed(0xFFFFFF90u);
    if (!c->d[6]) {
        pressed(0x60);
        if (!c->d[6]) return;
        c->a[0] = sx(rd16(LIST_LINE));
        c->d[0] = set_b(c->d[0], rd8(c->a[0] - 0x69F8));
        c->d[0] = set_b(c->d[0], c->d[0] & 7);
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 7, c->d[0]));
        if (!(c->d[0] & 0xFF)) { sound(53); return; }
        c->d[0] = set_b(c->d[0], rd8(c->a[0] - 0x69F8)); F_B(c->d[0]);
        if (c->d[0] & 0x80) goto leave;
        c->a[0] = sx(rd16(PAGE));
        wr8(0xFFFF95D6u, rd8(c->a[0] - 0x6A33));
        wr8(c->a[0] - 0x6A33, (uint8_t)c->d[0]);
        c->a[0] = LIST;
        c->d[1] = set_w(c->d[1], rd16(LIST_LEN));
        do {
            c->d[2] = set_b(c->d[2], rd8(c->a[0]) & 0x7F);
            cmp_byte(c->d[0], c->d[2]);
            if ((c->d[2] & 0xFF) == (c->d[0] & 0xFF)) c->d[2] = set_b(c->d[2], c->d[2] | 0x80);
            wr8(c->a[0]++, (uint8_t)c->d[2]);
            c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 1, c->d[1]));
        } while (c->d[1] & 0xFFFF);
    }
    stats();
    draw_list();
leave:
    up_hide(); wr8(0xFFFF9620u, 0); f_clr();
    down_hide(); wr8(0xFFFF9622u, 0); f_clr();
    click_back();
    c->d[6] = 0xFFFFFFFFu; F_L(c->d[6]);
}

/* $7E12: the line just worn blinks (12 frames each way) for $50 frames or until a button */
static void blink_worn(uint32_t ret)
{
    ret_to(0xFFFF9648u, ret);
    wr8(NO_REDRAW_ARROWS, 0x50);
    wr8(0xFFFF95DEu, 0x0C);
    wr8(0xFFFF95DFu, 0); f_clr();
    for (;;) {
        yield_at(0x7E2A);
        c->d[0] = set_b(0xFFFFFFF0u, 0xF0 & rd8(PAD_NEW)); F_B(c->d[0]);
        if (c->d[0] & 0xFF) break;
        if (!count_down(0xFFFF95DEu)) {
            wr8(0xFFFF95DEu, 0x0C);
            uint8_t v = rd8(0xFFFF95DFu);
            wr8(0xFFFF95DFu, v ^ 1);
            if (v & 1) draw_list();
            else {
                c->d[7] = set_w(c->d[7], 0xF06);
                c->d[0] = set_w(c->d[0], rd16(LIST_LINE));
                c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(LIST_TOP), c->d[0]));
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
                c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[0], c->d[7]));
                blank_13();
                c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
                blank_13();
            }
        }
        uint8_t n = (uint8_t)sub_b(&c->ccr, 1, rd8(NO_REDRAW_ARROWS));
        wr8(NO_REDRAW_ARROWS, n);
        if (c->ccr & CC_C) break;
    }
    wr8(NO_REDRAW_ARROWS, 0); f_clr();
}

/* $7E8C, $7EBA: the screen fading in (palette steps 9-$10) and out ($F-8; the frame's lower part
 * and the gold label back) */
static void fade_in(uint32_t ret)
{
    ret_to(0xFFFF963Cu, ret);
    stats_page();
    wr8(0xFFFF95DAu, 9);
    do {
        wait_input(0x7E9C);
        wr8(PAL_STEP, rd8(0xFFFF95DAu));
        set8(GAME_FLAGS2, 4);
        wr8(0xFFFF95DAu, (uint8_t)add_b(&c->ccr, 1, rd8(0xFFFF95DAu)));
        cmp_byte(0x11, rd8(0xFFFF95DAu));
    } while (rd8(0xFFFF95DAu) != 0x11);
}
static void fade_out(uint32_t ret)
{
    ret_to(0xFFFF963Cu, ret);
    wr8(0xFFFF95DAu, 0x0F);
    do {
        wait_input(0x7EC8);
        wr8(PAL_STEP, rd8(0xFFFF95DAu));
        set8(GAME_FLAGS2, 4);
        wr8(0xFFFF95DAu, (uint8_t)sub_b(&c->ccr, 1, rd8(0xFFFF95DAu)));
        cmp_byte(7, rd8(0xFFFF95DAu));
    } while (rd8(0xFFFF95DAu) != 7);
    frame_lower();
    call_68k(c, GOLD_LABEL);
}

/* $8BB6: the screen's setup: which pages can be used, by the things in use ($8C1E) */
static void screen_setup(void)
{
    wr8(0xFFFF9EF1u, rd8(0xFFFF9EF1u) & 0xDF);
    wr16(0xFFFF95F4u, 0x60);
    wr16(0xFFFF95F6u, 0x80); F_W(0x80);
    if (bit_test(0xFFFF9EF1u, 4)) return;
    c->a[0] = 0xFFFF95CEu;
    c->d[5] = set_b(c->d[5], rd8(c->a[0]));
    c->d[2] = 0;
    wr16(0xFFFF95F6u, 0); f_clr();
    do {
        c->d[0] = rd8(c->a[0]++); F_B(c->d[0]);
        if (!(c->d[0] & 0x80)) {
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[2], c->d[0]));
            c->d[3] = rd8(0x8C1Eu + sx(c->d[0]));
            c->d[3] = set_w(c->d[3], lsl_n(&c->ccr, c->d[3], 4, 16));
            F_W(c->d[2]);
            if (!(c->d[2] & 0xFFFF)) { wr16(0xFFFF95F4u, (uint16_t)c->d[3]); F_W(c->d[3]); }
            else {
                cmp_byte(4, c->d[5]);
                if ((c->d[5] & 0xFF) >= 4) {
                    cmp_w(&c->ccr, 0xE, c->d[2]);
                    if ((c->d[2] & 0xFFFF) == 0xE) goto done;
                }
                wr16(0xFFFF95F6u, (uint16_t)add_w(&c->ccr, c->d[3], rd16(0xFFFF95F6u)));
            }
        }
        c->d[2] = set_w(c->d[2], add_w(&c->ccr, 7, c->d[2]));
        cmp_w(&c->ccr, 0x15, c->d[2]);
    } while ((c->d[2] & 0xFFFF) != 0x15);
done:
    cmp_byte(5, rd8(0xFFFF95CEu));
    if (rd8(0xFFFF95CEu) == 5) { wr8(0xFFFF9EF1u, rd8(0xFFFF9EF1u) | 0x20); F_B(rd8(0xFFFF9EF1u)); }
}

/* $8C34: the menus' state reset: the things in use kept, the slots' things (if special actions) */
static void menu_reset(void)
{
    wr32(0xFFFF95D2u, rd32(0xFFFF95CEu));
    wr8(NO_REDRAW_ARROWS, 0); wr8(0xFFFF9614u, 0); wr8(0xFFFF9620u, 0); wr8(0xFFFF9622u, 0);
    wr16(PAGE, 0); wr16(LIST_LINE, 0); wr16(LIST_TOP, 0); f_clr();
    c->d[1] = 0xFFFFFFFFu;
    wr8(0xFFFF95E2u, 0xFF); F_B(0xFF);
    c->d[0] = 0;
    c->d[0] = set_b(c->d[0], rd8(0xFFFF9F10u)); F_B(c->d[0]);
    cmp_byte(8, c->d[0]);
    if ((c->d[0] & 0xFF) < 8) { wr8(0xFFFF95E2u, (uint8_t)c->d[0]); F_B(c->d[0]); }
    wr8(0xFFFF95E3u, 0xFF); F_B(0xFF);
    c->d[0] = set_b(c->d[0], rd8(0xFFFF9F11u)); F_B(c->d[0]);
    cmp_byte(8, c->d[0]);
    if ((c->d[0] & 0xFF) < 8) { wr8(0xFFFF95E3u, (uint8_t)c->d[0]); F_B(c->d[0]); }
}

/* $75F8: whether a main menu line can be opened (ne: no, with a buzz): the swords' page not under
 * water, the shields' not with a two-handed sword, nothing in a shop */
static int line_refused(void)
{
    switch (rd16(LIST_LINE)) {
    case 0:
        if (bit_test(0xFFFF966Bu, 0)) goto no;
        break;
    case 2:
        F_B(rd8(0xFFFF95CCu));
        if (!rd8(0xFFFF95CCu)) goto no;
        cmp_byte(4, rd8(0xFFFF95CEu));
        if (rd8(0xFFFF95CEu) >= 4) goto no;
        break;
    case 4: case 5:
        c->d[0] = 0; F_L(0);
        return 0;
    }
    return bit_test(0xFFFF9EF1u, 4);                 /* in a shop */
no:
    sound(53);
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
    return 1;
}

/* $763E, $766E, $7684: a line's page, until B or Start; then the stats page back */
static void line_page(void)
{
    ret_to(0xFFFF9638u, 0x75D8);
    open_line();
    switch (rd16(MENU_OF)) {
    case 4:
        items_page();
        do items_choose(0x767E); while (!(c->d[6] & 0x8000));
        break;
    case 5:
        magic_page();
        do {
            wait_input(0x7694);
            menu_move();
            magic_choose(0x769C);
        } while (!(c->d[6] & 0xFFFF));
        break;
    default:                                        /* 0-3: a list */
        do {
            wait_input(0x764A);
            list_buttons();
        } while (!(c->ccr & CC_N));
        c->a[0] = sx(rd16(MENU_OF));
        c->d[0] = set_b(c->d[0], rd8(0xFFFF95D6u));
        cmp_byte(rd8(c->a[0] - 0x6A32), c->d[0]);
        if ((c->d[0] & 0xFF) != rd8(c->a[0] - 0x6A32)) blink_worn(0x7662);
        wr16(LIST_LINE, rd16(MENU_OF));
        clear_panel();
        goto back;
    }
    stats_page();
    yield_at(0x76A8);
back:
    stats();
    wr16(PAGE, 0); f_clr();
    c->a[0] = rd32(0xFFFF9638u);
}

/* $7582: the status screen, from the game's frame when Start is pressed: fade in, the main menu
 * and its pages until B or Start, fade out; then back where it was called from */
static void status_screen(void)
{
    ret_to(0xFFFF9630u, rd32(c->a[7]));
    c->a[7] += 4;
    screen_setup();
    menu_reset();
    fade_in(0x7592);
    stats();
    for (;;) {
        main_menu();
        for (;;) {
            wait_input(0x759E);
            menu_move();
            pressed(0xFFFFFF90u);
            if (c->d[6]) goto close;
            pressed(0x60);
            if (!c->d[6]) continue;
            line_x2();
            c->a[0] = 0x75F8u + sx(rd16(0x75F8u + sx(c->d[0])));   /* jsr (a0): the check */
            if (!line_refused()) break;
            sound(53);
        }
        click_back();
        clear_panel();
        line_x2();
        c->a[0] = 0x7604u + sx(rd16(0x7604u + sx(c->d[0])));   /* jsr (a0): the page */
        line_page();
    }
close:
    view_limits();
    screen_setup();
    fade_out(0x75EE);
    call_68k(c, WINDOW_REDRAW);
    c->a[0] = rd32(0xFFFF9630u);
    port_continue_at(c, c->a[0], 0);
}

/* $7F0E: the pause screen (when $9659 bit 2 is set instead of the status screen): PAUSE until B */
static void pause_screen(void)
{
    ret_to(0xFFFF963Cu, rd32(c->a[7]));
    c->a[7] += 4;
    frame_pause();
    c->d[7] = set_w(c->d[7], 0xE08);
    text_at();
    c->a[1] = 0x7F4Eu;
    string_plain();
    wr8(PAL_STEP, 0x0A);
    set8(GAME_FLAGS2, 4);
    do {
        yield_at(0x7F36);
        pressed(0xFFFFFF80u);
    } while (!c->d[6]);
    wr8(PAL_STEP, 8);
    set8(GAME_FLAGS2, 4);
    c->a[0] = rd32(0xFFFF963Cu);
    port_continue_at(c, c->a[0], 0);
}

/* the two that wait: they leave by port_continue_at, so ret is theirs to set */
void StatusScreen(Cpu *cpu) { c = cpu; status_screen(); }   /* $7582 */
void PauseScreen(Cpu *cpu)  { c = cpu; pause_screen(); }    /* $7F0E */

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
ENTRY(FillZero, fill_zero)      /* $875C */
ENTRY(FillBlank, fill_blank)    /* $8762 */
ENTRY(TilePair, tile_pair)      /* $85FC */
ENTRY(TileRows, tile_rows)      /* $8608 */
ENTRY(Mark, mark)               /* $85CE */
ENTRY(MarkTwo, mark_2)          /* $85CC */
ENTRY(LeftSlot, left_slot)      /* $8620 */
ENTRY(RightSlot, right_slot)    /* $863C */
ENTRY(ItemsIcon, items_icon)    /* $8666 */
ENTRY(ViewLimits, view_limits)  /* $86BC */
ENTRY(Title, title)             /* $8272 */
ENTRY(BuildList, build_list)    /* $82F4 */
ENTRY(DrawList, draw_list)      /* $83D0 */
ENTRY(ListPage, list_page)      /* $76B4 */
ENTRY(ClearPanel, clear_panel)  /* $7808 */
ENTRY(Blank13, blank_13)        /* $7E72 */
ENTRY(LineX2, line_x2)          /* $757A */
ENTRY(OpenLine, open_line)      /* $7820 */
ENTRY(MainMenu, main_menu)      /* $77CA */
ENTRY(MenuMove, menu_move)      /* $7D00 */
ENTRY(TakeOff, take_off)        /* $7CA4 */
ENTRY(MagicPage, magic_page)    /* $7BB2 */
ENTRY(ItemsPage, items_page)    /* $7AC0 */
ENTRY(MaybePicture, maybe_picture) /* $7B86 */
ENTRY(Stats, stats)             /* $76E8 */
ENTRY(CursorSpriteTop, cursor_sprite_top) /* $794E */
ENTRY(CursorSpriteLow, cursor_sprite_low) /* $7952 */
ENTRY(SlotFlipTop, slot_flip_top) /* $7924 */
ENTRY(SlotFlipLow, slot_flip_low) /* $7938 */
ENTRY(ListButtons, list_buttons) /* $7D9C */
ENTRY(ScreenSetup, screen_setup) /* $8BB6 */
ENTRY(MenuReset, menu_reset)    /* $8C34 */
