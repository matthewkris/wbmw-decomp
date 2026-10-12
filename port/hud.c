/* The HUD, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * The hearts ($8AA2): the player's health is a word, whole hearts in the high byte and quarters
 * in the top two bits of the low byte; the maximum ($FF9F00) likewise. The display has eight
 * cells, built in $FF95AA and drawn as two rows of two tiles each. Up to eight hearts: one cell
 * a heart (full, a quarter-filled one for the part, empty ones up to the maximum, then blanks).
 * Past eight, the first cells become a band (left end, middles, right end) and the rest full
 * hearts, still eight cells. */
#include "world.h"

#define CELLS         0xFFFF95AAu
#define MAX_HEALTH    0xFFFF9F00u
#define PLAYER_W      0xFFFF9EEEu
#define VDP_DATA      0x00C00000u
#define VDP_CTRL      0x00C00004u
#define TOP_TILES     0x8B62u       /* per cell code: the top row's two tiles, then ... */
#define BOTTOM_TILES  0x8B86u       /* ... the bottom row's */

enum { BLANK = 0x00, EMPTY = 0x04, PART = 0x04 /* + 4 a quarter */, FULL = 0x14,
       BAND_LEFT = 0x18, BAND = 0x1C, BAND_RIGHT = 0x20 };

/* the original's loops count a byte down to zero (subq.b; bne), so a count of 0 runs 256
 * times; kept, though the game's health never gets there */
static uint32_t put(uint32_t a, uint8_t code, uint8_t n)
{
    do wr8(a++, code); while (--n);
    return a;
}

static void draw_row(uint32_t vram_cmd, uint32_t tiles)
{
    wr32(VDP_CTRL, vram_cmd);
    for (int i = 0; i < 8; i++) wr32(VDP_DATA, rd32(tiles + rd8(CELLS + (uint32_t)i)));
}

void hearts(void)
{
    Ent p = (Ent)(int32_t)rdw(PLAYER_W);
    uint8_t whole = rd8(p - 0x2600), quarters = rd8(p - 0x25FF) >> 6;
    uint8_t shown = (uint8_t)(whole + (quarters ? 1 : 0));   /* hearts with anything in them */
    uint8_t band = 0, used = 0, full = shown;
    if (shown > 8) {
        band = (uint8_t)(shown - 8);
        used = (uint8_t)(band + 1);
        full = (uint8_t)(shown - (2 * band + 1));
    }
    used = (uint8_t)(used + full);
    if (quarters) full--;
    uint32_t a = CELLS;
    if (band) {
        wr8(a++, BAND_LEFT);
        if (--band) a = put(a, BAND, band);
        wr8(a++, BAND_RIGHT);
    }
    if (full) a = put(a, FULL, full);
    if (quarters) wr8(a++, (uint8_t)(PART + 4 * quarters));
    uint8_t max = rd8(MAX_HEALTH) <= 8 ? rd8(MAX_HEALTH) : 8;
    if (max > used) {
        a = put(a, EMPTY, (uint8_t)(max - used));
        used = max;
    }
    if (used < 8) put(a, BLANK, (uint8_t)(8 - used));
    draw_row(0x58440003u, TOP_TILES);
    draw_row(0x58840003u, BOTTOM_TILES);
}

/* $8AA2; leaves a4 the player, as the original does */
void Hearts(Cpu *c)
{
    hearts();
    c->a[4] = (Ent)(int32_t)rdw(PLAYER_W);
}

/* ---- the HUD bar's text: the magic slots, the spell's name and uses, the meter, the gold ----
 *
 * The bar is on the window plane; a text position is a word, column in the high byte and row
 * in the low ($83B0). Text is a string of bytes ending in 0, drawn as tiles $A0xx or $80xx. */

#define TEXT_LEN      0xFFFF95E8u   /* the length of the last string measured ($1B004) */
#define METER_SHOWN   0xFFFF95F8u   /* the meter's drawn level (its high byte) */
#define DIGITS        0xFFFF959Fu   /* a number's digits, least significant first */
#define GOLD          0xFFFF962Cu
#define MAGIC_LEFT    0xFFFF9F10u   /* the special action for A + left, and right; $FF none */
#define MAGIC_SHOWN   0xFFFF95DCu   /* ... and the ones whose icons are loaded */
#define USES          0xFFFF95B8u   /* + kind: uses left */
#define NAMES         0x7BFEu       /* the special actions' names: offsets, then strings */

void CopyRawTiles(Cpu *);
static Cpu *hud;                      /* the call into C: a stack for the routines it calls */

/* $83B8: the VDP to write at column col, row row of the window plane */
static void text_at_rc(uint16_t col, uint16_t row)
{
    uint16_t a = (uint16_t)((((uint16_t)(row << 5) | col) * 2) | 0x5800);
    wr32(VDP_CTRL, (uint32_t)a << 16 | 3);
}
static void text_at(uint16_t pos) { text_at_rc(pos >> 8, pos & 0xFF); }   /* $83B0 */

/* $8762, $875C: n copies of a tile (n = 0 writes 65536, as the original's loop does) */
static void fill(uint16_t tile, uint16_t n)
{
    do wr16(VDP_DATA, tile); while (--n);
}

/* $786A, $7870: a string as tiles base | byte */
static uint32_t put_text(uint32_t s, uint16_t base)
{
    for (uint8_t ch; (ch = rd8(s++)); ) wr16(VDP_DATA, (uint16_t)((base & 0xFF00) | ch));
    return s;
}

/* $1B004: a string's length, kept in TEXT_LEN */
static uint16_t text_len(uint32_t s)
{
    uint16_t n = 0;
    while (rd8(s + n)) n++;
    wrw(TEXT_LEN, n);
    return n;
}

/* $882C: a special action with no uses left leaves the A + left/right slot it was in ($FF),
 * remembering it there; item kinds $28 and $2B up are numbered as their special actions */
static uint8_t used_up(uint8_t kind)
{
    if (kind == 0x28) kind = 12;
    else if (kind >= 0x2B) kind = (uint8_t)(kind - 0x23);
    for (int side = 1; side >= 0; side--)
        if (rd8(MAGIC_LEFT + (uint32_t)side) == kind) {
            wr8(MAGIC_LEFT + (uint32_t)side, 0xFF);
            wr8(MAGIC_SHOWN + (uint32_t)side, kind);
            break;
        }
    return kind;
}

/* $8778: one use of special action `kind` gone; its name shown centred in the bar's ten
 * columns (the extra column on the left), the row below cleared */
void magic_used(uint8_t kind)
{
    uint32_t count = USES + kind;
    wr8(count, (uint8_t)(rd8(count) - 1));
    if (!rd8(count)) kind = used_up(kind);
    uint32_t name = NAMES + (uint32_t)(int32_t)rdw(NAMES + 2u * kind) + 2;
    text_at(0x1501);
    uint16_t pad = (uint16_t)(10 - text_len(name));
    if (!pad) put_text(name, 0xA000);
    else {
        fill(0x8020, (uint16_t)((pad >> 1) + (pad & 1)));
        put_text(name, 0xA000);
        if (pad >> 1) fill(0x8020, pad >> 1);
    }
    text_at(0x1502);
    fill(0x8020, 10);
}

/* $87CE: the magic meter, $2000 full: eight cells of $400, each full ($879A), half ($879B) or
 * empty ($879C); drawn only when its level (the high byte) changes */
void magic_meter(uint16_t level)
{
    if (!level) return;
    if (level >= 0x2000) level = 0x2000;
    text_at(0x1602);
    level &= 0xFF00;
    if (level == rd16(METER_SHOWN)) return;
    wrw(METER_SHOWN, level);
    int16_t left = 7;
    uint16_t tile = 0x879A;
    for (uint16_t step = 0x400; step >= 0x200; step >>= 1, tile++)
        while (level >= step) { level = (uint16_t)(level - step); wr16(VDP_DATA, tile); left--; }
    for (; left >= 0; left--) wr16(VDP_DATA, 0x879C);
}

/* $85FC/$85F0: a 2x2 icon of tiles t, t+2 over t+1, t+3 at pos */
static void icon(uint16_t pos, uint16_t t)
{
    text_at(pos);
    wr16(VDP_DATA, t); wr16(VDP_DATA, (uint16_t)(t + 2));
    text_at((uint16_t)(pos + 1));
    wr16(VDP_DATA, (uint16_t)(t + 1)); wr16(VDP_DATA, (uint16_t)(t + 3));
}

/* $8608: two rows of two tiles from a table */
static void two_rows(uint16_t pos, uint32_t tiles)
{
    text_at(pos);
    wr32(VDP_DATA, rd32(tiles));
    text_at((uint16_t)(pos + 1));
    wr32(VDP_DATA, rd32(tiles + 4));
}

/* $8656: the icon of special action `kind` for an A + left/right slot at pos (tiles t); when its
 * graphics are not loaded yet (vram != 0), they are copied to vram first */
static void slot_icon(uint16_t pos, uint16_t t, uint16_t vram, uint16_t kind)
{
    uint32_t pic;
    if ((kind & 0xFF) == 0xFF) pic = 0x8C80;                       /* empty */
    else if (kind < 8) pic = 0x869C + kind;
    else {
        kind = (uint16_t)(kind - 8);
        if (kind == 4) { two_rows(pos, 0x7BAA); return; }
        pic = 0x8690 + kind;
    }
    if (vram) {
        Cpu t2 = *hud;                                              /* for its stack */
        t2.d[0] = vram; t2.d[1] = (uint16_t)(0x2000 | rd8(pic)); t2.d[2] = 32;
        CopyRawTiles(&t2);
    }
    icon(pos, t);
}

/* $8620, $863C: the icons of the A + left and A + right slots, their graphics loaded if the
 * special action changed since they were */
static void left_icon(void)
{
    uint8_t k = rd8(MAGIC_LEFT);
    slot_icon(0x1601, 0x87E0, k == rd8(MAGIC_SHOWN) ? 0 : 0xFC00, k);
}
static void right_icon(void)
{
    uint8_t k = rd8(MAGIC_LEFT + 1);
    slot_icon(0x1C01, 0x87E4, k == rd8(MAGIC_SHOWN + 1) ? 0 : 0xFC80, k);
}

/* $85CE: one of the bar's fixed pictures (0: ..., 2: ...): two rows of two tiles */
static void picture(uint16_t which)
{
    two_rows(rd16(0x85EC + which), 0x85DC + 4u * which);
}

/* $76C4: the bar drawn again: the name rows blank, the slots' icons, the picture between */
void hud_redraw(void)
{
    for (uint16_t row = 0x1501; row <= 0x1502; row++) { text_at(row); fill(0x8020, 10); }
    left_icon();
    picture(0);
    right_icon();
}

/* $84EC: a number's decimal digits into DIGITS (least significant first, at most 999,999);
 * returns how many */
static uint16_t digits(uint32_t n)
{
    if (n >= 999999) n = 999999;
    uint16_t count = 0;
    do { wr8(DIGITS + count, (uint8_t)(n % 10)); n /= 10; count++; } while (n);
    return count;
}

/* $85A8: the gold, right-aligned in six columns ending at column 29 */
void gold_display(void)
{
    text_at(0x1806);
    fill(0x8020, 6);
    uint16_t n = digits(rd32(GOLD));
    text_at_rc((uint16_t)(30 - n), 6);
    for (uint16_t i = n; i; i--) wr16(VDP_DATA, (uint16_t)(0x8030 + rd8(DIGITS + i - 1)));
}

void MagicUsed(Cpu *c)   { hud = c; magic_used((uint8_t)c->d[0]); }
void SlotEmptied(Cpu *c) { hud = c; c->d[0] = set_b(c->d[0], used_up((uint8_t)c->d[0])); }   /* $882C */
void MagicMeter(Cpu *c)  { hud = c; magic_meter((uint16_t)c->d[0]); }
void HudRedraw(Cpu *c)   { hud = c; hud_redraw(); }
void GoldDisplay(Cpu *c) { hud = c; gold_display(); }
