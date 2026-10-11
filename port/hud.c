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
