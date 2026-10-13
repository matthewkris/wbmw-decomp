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
