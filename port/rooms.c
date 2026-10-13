/* The rooms' data, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The rooms").
 *
 * A room has two layers, each described by a record ($FF9754 for plane A, $FF97D4 for plane B):
 * among its fields, the room's data at $34, its blocks' table at $1C and its map at $22. The data
 * starts with offsets to its packed blocks and its packed map.
 *
 * Batch T11, the unpackers:
 * - $9F6A, the blocks: a block is four tiles (a word each) and a byte, packed by a nibble each
 *   telling how its tiles follow from its first (in a run, the same palette and plane, or each
 *   given); 8 bytes a block in the table, the bytes $800 after.
 * - $A068, a layer's map: a chunk for each place used, each of 192 cells of blocks, packed as
 *   runs of one block over a bitmap of the cells, then the rest as 10-bit numbers four to five
 *   bytes, or with zero runs ($A1C4).
 * Batch T12, loading a room: its list of commands (layers, graphics, palettes, music, doors and
 * things), the layers' records, the ending's scenes and the room's music.
 * Batch T13, setting the layers up: how each scrolls and is drawn, the camera's parallax.
 * Batch T14, the layers each frame: the camera, the layers' moves, the blocks scrolling in.
 * Batch T15, the map's cells (finding, setting, drawing), the game's flags, rectangles of the map,
 * the start points and the palette's fade step.
 * Batch T16, the room's triggers: exits, doors, houses, chests, entrances, and what pressing up
 * at them does.
 * Batch T17, the room changes' helpers: the door's frame and blocks, the events' pictures.
 * Batch T18, the picture growing, the door's animations (they wait for frames), leaving a room.
 * Batch T19, the room changes' sequences: doors, warps, shops and houses.
 * Batch T20, arriving in a room: through a door, inside, walking in or out, fading in.
 * Batch T21, breaking and crumbling walls, the doors' records.
 * Ported register for register, with every register and flag in the contract. */
#include "world.h"
#include "task.h"

#define DATA_DIR      0x0001CC00u   /* the ROM's directory of data tables */
#define FILLED        0xFFFF970Cu   /* 24 bytes: a chunk's cells written (a bit each) */
#define ZERO_RUNS     0xFFFF9862u   /* $A1C4's output */

static Cpu *c;

static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static void f_bit(int on) { c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (on ? 0 : CC_Z)); }   /* btst, bset, bclr */
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }
static uint8_t next8(int r) { return rd8(c->a[r]++); }   /* (an)+, a byte */
static void put16(int r, uint32_t v) { wr16(c->a[r], (uint16_t)v); c->a[r] += 2; F_W(v); }   /* move.w dn,(an)+ */
static void and_w(int r, uint32_t m) { c->d[r] = set_w(c->d[r], c->d[r] & m); F_W(c->d[r]); }
static void and_b(int r, uint32_t m) { c->d[r] = set_b(c->d[r], c->d[r] & m); F_B(c->d[r]); }
static void or_w(int r, uint32_t v) { c->d[r] = set_w(c->d[r], c->d[r] | v); F_W(c->d[r]); }
static void or_b(int r, uint32_t v) { c->d[r] = set_b(c->d[r], c->d[r] | v); F_B(c->d[r]); }
static void move_b(int r, uint8_t v) { c->d[r] = set_b(c->d[r], v); F_B(v); }
static uint32_t roxl_w(uint16_t *ccr, uint32_t v, unsigned n)   /* roxl.w #1 (n is 1 here) */
{
    (void)n;
    uint16_t x = *ccr & CC_X ? 1 : 0, out = (uint16_t)(v >> 15 & 1), r = (uint16_t)(v << 1 | x);
    *ccr = (uint16_t)((out ? CC_X | CC_C : 0) | (r & 0x8000 ? CC_N : 0) | (r ? 0 : CC_Z));
    return r;
}
static int dbra(int r) { c->d[r] = set_w(c->d[r], c->d[r] - 1); return (c->d[r] & 0xFFFF) != 0xFFFF; }

/* $9F6A: the blocks at a3 into the table at a1 (and their bytes at a1 + $800). The first word is
 * the count; then a nibble a block (high first), then the blocks' bytes from half-way */
static void unpack_blocks(void)
{
    c->d[4] = set_w(c->d[4], rd16(c->a[3])); c->a[3] += 2; F_W(c->d[4]);
    if (!(c->d[4] & 0xFFFF)) return;
    c->a[0] = c->a[1] + 0x800;
    c->d[0] = set_w(c->d[0], c->d[4]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0]));
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 1, 16));
    c->a[2] = c->a[3] + sx(c->d[0]);
    c->d[3] = 0;
    c->d[4] = set_w(c->d[4], sub_w(&c->ccr, 1, c->d[4]));
    do {
        c->d[3] = set_w(c->d[3], c->d[3] ^ 1); F_W(c->d[3]);
        if (c->d[3] & 0xFFFF) {
            move_b(1, rd8(c->a[3]));
            c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 8));
        } else move_b(1, next8(3));
        and_b(1, 0xF);
        if (!(c->d[1] & 0xFF)) { c->a[0] += 1; c->a[1] += 8; continue; }   /* an empty block */
        c->d[1] = set_b(c->d[1], c->d[1] ^ 0xF); F_B(c->d[1]);
        c->d[0] = 0; F_L(0);
        f_bit(c->d[1] >> 3 & 1);
        if (!(c->d[1] & 8)) move_b(0, next8(2));                       /* its byte, or 0 */
        wr8(c->a[0]++, (uint8_t)c->d[0]); F_B(c->d[0]);
        move_b(0, next8(2));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
        move_b(0, next8(2));
        put16(1, c->d[0]);                                              /* the first tile */
        and_b(1, 7);
        if (!(c->d[1] & 0xFF)) {                                        /* 0: the other three given */
            for (int k = 0; k < 6; k++) { uint8_t v = next8(2); wr8(c->a[1]++, v); F_B(v); }
            continue;
        }
        c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 2, c->d[1]));
        if (!(c->d[1] & 0xFF)) {                                        /* 2: three numbers and the flips, in 2 bytes */
            and_w(0, 0xFFE0);
            move_b(1, next8(2)); and_w(1, 0x1F); or_w(1, c->d[0]); put16(1, c->d[1]);
            move_b(1, rd8(c->a[2])); and_w(1, 0x1F); or_w(1, c->d[0]); put16(1, c->d[1]);
            move_b(1, next8(2)); and_w(1, 0x60);
            c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 16));
            or_w(0, c->d[1]);
            move_b(1, rd8(c->a[2] - 2));
            c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 5, 8));
            or_b(0, c->d[1]);
            put16(1, c->d[0]);
            continue;
        }
        if (c->ccr & CC_C) {                                            /* 1: the next tiles in a 16-wide sheet */
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0])); put16(1, c->d[0]);
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0xF, c->d[0])); put16(1, c->d[0]);
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0])); put16(1, c->d[0]);
            continue;
        }
        c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 1, c->d[1]));
        if (!(c->d[1] & 0xFF)) {                                        /* 3: the low bytes given */
            for (int k = 0; k < 3; k++) { move_b(0, next8(2)); put16(1, c->d[0]); }
            continue;
        }
        and_w(0, 0xE7E0);                                               /* 4-7: numbers and flips, a byte each */
        for (int k = 0; k < 3; k++) {
            move_b(1, next8(2));
            move_b(2, (uint8_t)c->d[1]);
            and_w(2, 0x1F); or_w(2, c->d[0]);
            and_w(1, 0xC0);
            c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 5, 16));
            or_w(1, c->d[2]);
            put16(1, c->d[1]);
        }
    } while (dbra(4));
}

/* $9F64: a room's blocks (a4 its data: the first word the offset to them) */
static void unpack_room_blocks(void)
{
    c->d[4] = set_w(c->d[4], rd16(c->a[4])); F_W(c->d[4]);
    c->a[3] = c->a[4] + sx(c->d[4]);
    unpack_blocks();
}

/* $9F46: the blocks every room shares, three sets (from the directory's table 1) */
static void load_shared_blocks(void)
{
    c->a[4] = rd32(DATA_DIR + 4);
    c->a[3] = rd32(c->a[4]); c->a[4] += 4;
    c->a[1] = 0xFFFFE500u;
    unpack_blocks();
    c->a[3] = rd32(c->a[4]); c->a[4] += 4;
    c->a[1] = 0xFFFFEE00u;
    unpack_blocks();
    c->a[3] = rd32(c->a[4]);
    c->a[1] = 0xFFFFF700u;
    unpack_blocks();
}

/* $A1C4: a2's zero runs expanded to ZERO_RUNS (a2 left there): a count, a mask's length, the
 * mask (a bit set for a 0) and the bytes */
static void expand_zero_runs(void)
{
    c->a[1] = ZERO_RUNS;
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], next8(2));
    c->d[2] = 0;
    move_b(2, next8(2));
    c->a[0] = c->a[2] + sx(c->d[2]);
    while (dbra(2)) {
        c->d[0] = 7;
        move_b(3, next8(2));
        do {
            c->d[4] = 0; F_L(0);
            c->d[3] = set_b(c->d[3], add_b(&c->ccr, c->d[3], c->d[3]));
            if (!(c->ccr & CC_C)) move_b(4, next8(0));
            wr8(c->a[1]++, (uint8_t)c->d[4]); F_B(c->d[4]);
            c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 1, c->d[1]));
            if (!(c->d[1] & 0xFFFF)) goto done;
        } while (dbra(0));
    }
    while (dbra(1)) { uint8_t v = next8(0); wr8(c->a[1]++, v); F_B(v); }
done:
    c->a[2] = ZERO_RUNS;
}

/* $A068: the map of layer a3 (its record). Its map ($22, 64 bytes) gets a chunk number for each
 * place used (-1 the rest); each chunk's 192 cells go to $FF5000 + $180 a chunk (the layers'
 * chunks numbered from 0 or $10). A chunk: runs of one block (a count of runs, each a
 * block and the bitmap of the cells it fills, or zero runs first), then the cells left. a3 ends
 * as the word it had, sign-extended (pushed and popped as a word) */
static void unpack_layer_map(void)
{
    uint16_t a3_word = (uint16_t)c->a[3];
    c->a[7] -= 2; wr16(c->a[7], a3_word);           /* move.w a3,-(a7); link a6,#-10 */
    c->a[7] -= 4; wr32(c->a[7], c->a[6]); c->a[6] = c->a[7]; c->a[7] -= 10;
    uint8_t chunk_no, runs;
    uint16_t map;
    uint32_t chunk_at;
    move_b(0, rd8(c->a[3]));
    and_w(0, 1);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    chunk_no = (uint8_t)c->d[0]; F_B(chunk_no);
    c->a[0] = sx(rd16(c->a[3] + 0x22));
    map = (uint16_t)c->a[0];
    c->d[1] = 15;
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
    do { wr32(c->a[0], c->d[0]); c->a[0] += 4; F_L(c->d[0]); } while (dbra(1));
    c->a[4] = rd32(c->a[3] + 0x34);
    c->a[1] = rd32(c->a[3] + 0x1C);
    uint32_t a3 = c->a[3];
    unpack_room_blocks();
    c->a[3] = a3;
    c->d[0] = set_w(c->d[0], rd16(c->a[4] + 2)); F_W(c->d[0]);
    c->a[3] = c->a[4] + sx(c->d[0]);
    c->a[2] = c->a[4] + 4;
    uint32_t saved_a2;
next_place:                                         /* $A0A8: a place on the map and its chunk */
    move_b(0, next8(2));
    if (c->d[0] & 0x80) {
        c->a[7] = c->a[6]; c->a[6] = rd32(c->a[7]); c->a[7] += 4;   /* unlk a6 */
        c->a[3] = sx(rd16(c->a[7])); c->a[7] += 2;
        return;
    }
    c->d[0] = set_w(c->d[0], (uint32_t)(int32_t)(int8_t)c->d[0]); F_W(c->d[0]);
    c->a[0] = sx(c->d[0]);
    c->a[0] += sx(map);
    c->d[0] = 0;
    move_b(0, chunk_no);
    wr8(c->a[0], chunk_no); F_B(chunk_no);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_b(c->d[0], add_b(&c->ccr, chunk_no, c->d[0]));
    chunk_no = (uint8_t)add_b(&c->ccr, 1, chunk_no);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 7, 16));
    c->d[0] = (uint32_t)add_l(&c->ccr, 0xFF5000, c->d[0]);
    chunk_at = c->d[0];
    c->a[0] = FILLED;
    c->d[0] = 0;
    c->d[1] = 5;
    do { wr32(c->a[0], 0); c->a[0] += 4; F_L(0); } while (dbra(1));
    c->d[0] = 0;
    move_b(0, next8(2));
    saved_a2 = c->a[2];
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + sx(c->d[0]))); F_W(c->d[0]);
    c->a[2] = c->a[4] + sx(c->d[0]);
next_runs:                                          /* $A0F4 */
    c->d[5] = 0xFFFFFFC0u;                          /* -64: 192 cells to fill, counted to 0 */
    runs = next8(2); F_B(runs);
    if (!runs) goto rest;
    if (runs & 0x80) { expand_zero_runs(); goto next_runs; }
    goto run;
run_again:                                          /* $A106 */
    F_B(c->d[5]);
    if (!(c->d[5] & 0xFF)) goto put;
run:                                                /* $A10C: a run: a block over the cells of a bitmap */
    c->a[1] = chunk_at;
    move_b(2, next8(2));
    c->d[2] = set_w(c->d[2], lsl_n(&c->ccr, c->d[2], 8, 16));
    move_b(2, next8(2));
    c->d[4] = set_w(c->d[4], 0x17);
    c->a[0] = FILLED;
    c->d[0] = 1;
    c->d[1] = 0; F_L(0);
    for (;;) {
        f_bit(rd8(c->a[0]) >> (c->d[1] & 7) & 1);
        if (!(c->ccr & CC_Z)) goto skip;            /* written already: not in the bitmap */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
        if (!(c->d[0] & 0xFF)) { c->d[0] = 8; move_b(3, next8(2)); }
        c->d[3] = set_b(c->d[3], lsr_n(&c->ccr, c->d[3], 1, 8));
        if (c->ccr & CC_C) {
            wr16(c->a[1], (uint16_t)c->d[2]); F_W(c->d[2]);
            f_bit(rd8(c->a[0]) >> (c->d[1] & 7) & 1);
            wr8(c->a[0], rd8(c->a[0]) | 1 << (c->d[1] & 7));
            c->d[5] = set_b(c->d[5], sub_b(&c->ccr, 1, c->d[5]));
        }
    skip:
        c->a[1] += 2;
        c->d[1] = set_b(c->d[1], add_b(&c->ccr, 1, c->d[1]));
        f_bit(c->d[1] >> 3 & 1); c->d[1] &= ~8u;
        if (c->ccr & CC_Z) continue;
        c->a[0] += 1;
        if (!dbra(4)) break;
    }
    runs = (uint8_t)sub_b(&c->ccr, 1, runs);
    if (runs) goto run_again;
    F_B(c->d[5]);
    if (!(c->d[5] & 0xFF)) goto chunk_done;
rest:                                               /* $A152: the cells left, 10-bit numbers */
    c->d[3] = 0;
    c->d[4] = 0;
    c->a[0] = FILLED;
    c->a[1] = chunk_at;
cell:                                               /* $A15E */
    f_bit(rd8(c->a[0]) >> (c->d[3] & 7) & 1);
    if (!(c->ccr & CC_Z)) goto next_cell;
    switch (c->d[4] & 0xFFFF) {                     /* jmp tbl(pc,d4.w): where in the five bytes */
    case 0:
        c->d[0] = 0;
        move_b(0, next8(2));
        move_b(1, next8(2));
        c->d[1] = set_b(c->d[1], lsl_n(&c->ccr, c->d[1], 1, 8));
        c->d[0] = set_w(c->d[0], roxl_w(&c->ccr, c->d[0], 1));
        c->d[1] = set_b(c->d[1], lsl_n(&c->ccr, c->d[1], 1, 8));
        c->d[0] = set_w(c->d[0], roxl_w(&c->ccr, c->d[0], 1));
        break;
    case 2:
        c->d[0] = 0;
        move_b(0, (uint8_t)c->d[1]);
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
        move_b(1, rd8(c->a[2]));
        c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 8));
        or_b(0, c->d[1]);
        break;
    case 4:
        c->d[0] = 0;
        move_b(0, next8(2));
        c->d[0] = set_b(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 8));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
        move_b(1, rd8(c->a[2]));
        c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 8));
        or_b(0, c->d[1]);
        break;
    default:                                        /* 6 */
        move_b(0, next8(2));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
        and_w(0, 0x300);
        move_b(0, next8(2));
        break;
    }
put:                                                /* $A1A4 */
    wr16(c->a[1], (uint16_t)c->d[0]); F_W(c->d[0]);
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, 2, c->d[4]));
    and_w(4, 6);
    c->d[5] = set_b(c->d[5], sub_b(&c->ccr, 1, c->d[5]));
    if (!(c->d[5] & 0xFF)) goto chunk_done;
next_cell:                                          /* $A1B0 */
    c->a[1] += 2;
    c->d[3] = set_b(c->d[3], add_b(&c->ccr, 1, c->d[3]));
    f_bit(c->d[3] >> 3 & 1); c->d[3] &= ~8u;
    if (c->ccr & CC_Z) goto cell;
    c->a[0] += 1;
    goto cell;
chunk_done:                                         /* $A1BE */
    c->a[2] = saved_a2;
    goto next_place;
}

#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(UnpackBlocks, unpack_blocks)             /* $9F6A */
ENTRY(UnpackRoomBlocks, unpack_room_blocks)    /* $9F64 */
ENTRY(LoadSharedBlocks, load_shared_blocks)    /* $9F46 */
ENTRY(ExpandZeroRuns, expand_zero_runs)        /* $A1C4 */
ENTRY(UnpackLayerMap, unpack_layer_map)        /* $A068 */

/* ---- batch T12: loading a room ----
 * A room is a list of commands (by area $9668 and room $9669, from the directory's table 8):
 * its layers, graphics, palettes, music, doors and things, ended by $FF. LoadRoom ($9330)
 * resets the room's state, runs them, and finishes the graphics and music. */

#define AREA          0xFFFF9668u   /* byte */
#define ROOM          0xFFFF9669u   /* byte */
#define LAYER_A       0xFFFF9754u   /* the layers' records */
#define LAYER_B       0xFFFF97D4u
#define ROOM_COMMANDS 0x9442u       /* the commands' offsets */
#define TEST_FLAG     0x274Cu       /* TestGameFlag: d0 the flag (ne: set) */
#define SET_GFX_SLOTS 0x234Cu
#define LOAD_PALETTES 0x135Cu
#define STREAM_GFX    0x22F0u
#define OWNS          0x23E4u

static void call(uint32_t addr) { call_68k(c, addr); }
static uint32_t sub_l(uint16_t *ccr, uint32_t s, uint32_t d)   /* sub.l / subi.l */
{
    uint32_t r = d - s;
    int v = ((s ^ d) & (r ^ d) & 0x80000000u) != 0;
    *ccr = (uint16_t)((r & 0x80000000u ? CC_N : 0) | (r ? 0 : CC_Z) | (v ? CC_V : 0) | (s > d ? CC_C | CC_X : 0));
    return r;
}
static void cmp_byte(uint32_t s, uint32_t d) { uint16_t x = c->ccr & CC_X; sub_b(&c->ccr, s, d); c->ccr = (uint16_t)((c->ccr & ~CC_X) | x); }
static uint16_t next16(int r) { uint16_t v = rd16(c->a[r]); c->a[r] += 2; return v; }   /* (an)+, a word */
static uint32_t next32(int r) { uint32_t v = rd32(c->a[r]); c->a[r] += 4; return v; }
static void put8(uint32_t a, uint32_t v) { wr8(a, (uint8_t)v); F_B(v); }                /* move.b to memory */
static void put16m(uint32_t a, uint32_t v) { wr16(a, (uint16_t)v); F_W(v); }
static void put32m(uint32_t a, uint32_t v) { wr32(a, v); F_L(v); }
static void push32(uint32_t v) { c->a[7] -= 4; wr32(c->a[7], v); }
static uint32_t pop32(void) { uint32_t v = rd32(c->a[7]); c->a[7] += 4; return v; }
/* a call with a1-a2 kept on the stack (movem.l a1-a2,-(a7) ... (a7)+,a1-a2) */
static void call_keep_a1a2(uint32_t addr)
{
    c->a[7] -= 8; wr32(c->a[7], c->a[1]); wr32(c->a[7] + 4, c->a[2]);
    call(addr);
    c->a[1] = rd32(c->a[7]); c->a[2] = rd32(c->a[7] + 4); c->a[7] += 8;
}

/* $9622: layer a3's record: its buffers ($FFE500, $900 apart) and plane's fields by layer
 * ($965E for A, $966E for B) */
static void init_layer(void)
{
    c->a[0] = 0xFFFFE500u;
    put32m(c->a[3] + 0x10, c->a[0]);
    c->a[0] += 0x900;
    put32m(c->a[3] + 0x14, c->a[0]);
    c->a[0] += 0x900;
    put32m(c->a[3] + 0x18, c->a[0]);
    c->a[0] = 0x965Eu;
    f_bit(rd8(c->a[3]) & 1);
    if (rd8(c->a[3]) & 1) c->a[0] = 0x966Eu;
    put16m(c->a[3] + 0x20, next16(0));
    put16m(c->a[3] + 0x22, next16(0));
    put32m(c->a[3] + 0x1C, next32(0));
    put32m(c->a[3] + 0x28, next32(0));
    put32m(c->a[3] + 0x2C, next32(0));
}

/* $967E: both layers' records, the base graphics, the planes' scroll kept */
static void init_layers(void)
{
    wr8(0xFFFF9858u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->a[0] = 0x96BCu; call(SET_GFX_SLOTS);
    c->a[3] = LAYER_A; put8(c->a[3], 0); init_layer();
    c->a[3] = LAYER_B; put8(c->a[3], 1); init_layer();
    c->d[0] = rd32(0xFFFFC800u); F_L(c->d[0]);
    c->d[0] = sub_l(&c->ccr, 0x800000, c->d[0]);
    put32m(0xFFFF972Cu, c->d[0]);
    c->d[0] = rd32(0xFFFFC900u); F_L(c->d[0]);
    c->d[0] = sub_l(&c->ccr, 0x600000, c->d[0]);
    put32m(0xFFFF9730u, c->d[0]);
}

/* $946C: a layer command's record (d0 bit 1: B): its kind (d0), width and height in screens
 * ($38/$39, the limits $44/$46) and a word ($3A) */
static void setup_layer(void)
{
    c->a[3] = LAYER_A;
    and_w(0, 2);
    if (c->d[0] & 0xFFFF) c->a[3] = LAYER_B;
    move_b(0, next8(2));
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], next8(2));
    put8(c->a[3] + 0x38, c->d[1]);
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 4, 16));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 1, c->d[1]));
    put16m(c->a[3] + 0x44, c->d[1]);
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], next8(2));
    put8(c->a[3] + 0x39, c->d[1]);
    c->d[1] = set_b(c->d[1], add_b(&c->ccr, c->d[1], c->d[1]));
    c->d[1] = set_b(c->d[1], add_b(&c->ccr, rd8(c->a[3] + 0x39), c->d[1]));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 8, 16));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 0x40, c->d[1]));
    put16m(c->a[3] + 0x46, c->d[1]);
    put16m(c->a[3] + 0x3A, next16(2));
}

/* $9500: a pointer in the commands: a byte, then a long (if it is not 0) or an offset from the
 * room's list (a1) */
static void read_room_pointer(void)
{
    uint8_t v = next8(2); F_B(v);
    if (v) { c->a[0] = next32(2); return; }
    c->d[0] = set_w(c->d[0], next16(2)); F_W(c->d[0]);
    c->a[0] = c->a[1] + sx(c->d[0]);
}

/* $94B8: the layer's data (a4), kind and its record's long from $94CC */
static void layer_data(void)
{
    put32m(c->a[3] + 0x34, c->a[4]);
    put8(c->a[3] + 1, c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    put32m(c->a[3] + 0x30, rd32(0x94CCu + sx(c->d[0])));
}

/* the commands, by their handlers' addresses */
static void cmd_layer(void)        /* $94AA, 0-1: a layer, its data at an offset */
{
    setup_layer();
    c->d[1] = set_w(c->d[1], next16(2)); F_W(c->d[1]);
    c->a[4] = c->a[1] + sx(c->d[1]);
    layer_data();
}
static void cmd_layer_at(void)     /* $94B4, 16-17: a layer, its data at an address */
{
    setup_layer();
    c->a[4] = next32(2);
    layer_data();
}
static void cmd_gfx(void)          /* $9510, 2: the graphics */
{
    read_room_pointer();
    put32m(0xFFFF9672u, c->a[0]);
    c->a[3] = c->a[1];
    call(SET_GFX_SLOTS);
    c->a[1] = c->a[3];
}
static void cmd_palettes(void)     /* $9520, 3 */
{
    put8(0xFFFF9670u, next8(2));
    put16m(0xFFFF966Cu, next16(2));
}
static void cmd_palette2(void) { put8(0xFFFF9671u, next8(2)); }          /* $952A, 10 */
static void cmd_4(void) { read_room_pointer(); call_keep_a1a2(0x23A6); }  /* $9530, 4 */
static void cmd_5(void) { read_room_pointer(); put32m(0xFFFF996Cu, c->a[0]); }   /* $9540 */
static void cmd_6(void) { put8(0xFFFF966Bu, next8(2)); }                  /* $9548: under water */
static void cmd_7(void)            /* $954E, 7 */
{
    c->d[5] = 0;
    c->d[5] = set_b(c->d[5], next8(2));
    c->d[4] = set_w(c->d[4], next16(2));
    c->d[6] = 0;
    c->d[6] = set_b(c->d[6], next8(2));
    c->d[6] = set_w(c->d[6], lsl_n(&c->ccr, c->d[6], 4, 16));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 0xF00, c->d[6]));
    c->d[7] = 0;
    c->d[7] = set_b(c->d[7], next8(2));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 4, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0xF00, c->d[7]));
    call_keep_a1a2(0x11BF0);
}
static void cmd_8(void)            /* $9578, 8: as $9500, kept at $A11E */
{
    uint8_t v = next8(2); F_B(v);
    if (v) c->a[0] = next32(2);
    else { c->d[0] = set_w(c->d[0], next16(2)); F_W(c->d[0]); c->a[0] = c->a[1] + sx(c->d[0]); }
    put32m(0xFFFFA11Eu, c->a[0]);
}
static void cmd_9(void) { put8(0xFFFFA0E0u, next8(2)); }                  /* $958C */
static void cmd_11(void)           /* $9592, 11 */
{
    read_room_pointer();
    c->d[6] = 0;
    c->d[6] = set_b(c->d[6], next8(2));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 0x100, c->d[6]));
    c->d[7] = 0;
    c->d[7] = set_b(c->d[7], next8(2));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 6, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x4000, c->d[7]));
    call(0x2974);
}
static void cmd_12(void)           /* $95AC, 12 */
{
    c->d[0] = 0;
    move_b(0, next8(2));
    call_keep_a1a2(0x5658);
}
static void cmd_13(void)           /* $95C0, 13 */
{
    c->d[0] = 0;
    move_b(0, next8(2));
    put16m(0xFFFF9682u, c->d[0]);
}
static void cmd_14(void) { put8(0xFFFF9685u, next8(2)); }                 /* $95CA */
static void cmd_15(void)           /* $95D0, 15 */
{
    put8(0xFFFF9688u, next8(2));
    put16m(0xFFFF9686u, next16(2));
}
static void cmd_18(void)           /* $95DA, 18 */
{
    c->d[0] = 0;
    move_b(0, next8(2));
    put16m(0xFFFF968Au, c->d[0]);
    cmp_w(&c->ccr, rd16(0xFFFF9F32u), c->d[0]);
    if ((c->d[0] & 0xFFFF) != rd16(0xFFFF9F32u)) call(0xF880);
}
static void cmd_19(void)           /* $95EE, 19 */
{
    c->a[2] += 1;
    push32(c->a[1]); F_L(c->a[1]);
    c->a[0] = c->a[2];
    call(0xB49C);
    c->a[2] += 8;
    c->a[1] = pop32();
}
static void cmd_20(void)           /* $95FE, 20: a thing in a free slot ($11CE8) */
{
    call(0x11CE8);
    if (c->ccr & CC_N) return;
    put8(c->a[1] - 0x6072, 0x80);
    put8(c->a[1] - 0x6071, next8(2));
    put32m(c->a[1] - 0x601E, next32(2));
    put32m(c->a[1] - 0x5F76, next32(2));
    wr32(c->a[1] - 0x5FCA, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    wr8(c->a[1] - 0x6070, 0xFF);
}

static void room_command(uint32_t at)
{
    switch (at) {
    case 0x94AA: cmd_layer(); break;
    case 0x94B4: cmd_layer_at(); break;
    case 0x9510: cmd_gfx(); break;
    case 0x9520: cmd_palettes(); break;
    case 0x952A: cmd_palette2(); break;
    case 0x9530: cmd_4(); break;
    case 0x9540: cmd_5(); break;
    case 0x9548: cmd_6(); break;
    case 0x954E: cmd_7(); break;
    case 0x9578: cmd_8(); break;
    case 0x958C: cmd_9(); break;
    case 0x9592: cmd_11(); break;
    case 0x95AC: cmd_12(); break;
    case 0x95C0: cmd_13(); break;
    case 0x95CA: cmd_14(); break;
    case 0x95D0: cmd_15(); break;
    case 0x95DA: cmd_18(); break;
    case 0x95EE: cmd_19(); break;
    case 0x95FE: cmd_20(); break;
    default: call(at); break;                       /* past the table: as the original jumps */
    }
}

/* $9330: the room: its state reset, its commands run; then the things' graphics, and the music
 * and palette by the game's flags */
static void load_room(void)
{
    init_layers();
    call(0x11BBC);
    call(0x289A);
    call(0x29A4);
    call(0xB4FA);
    call(0xB852);
    c->d[0] = 0; F_L(0);
    wr8(0xFFFF966Bu, 0); wr32(0xFFFF996Cu, 0); wr8(0xFFFF9966u, 0); wr16(0xFFFF9962u, 0);
    wr32(0xFFFFA11Eu, 0); wr8(0xFFFFA0E0u, 0); wr16(0xFFFF966Cu, 0); wr8(0xFFFF9685u, 0);
    wr16(0xFFFF9980u, 0); wr32(0xFFFF9734u, 0); wr32(0xFFFF9738u, 0);
    put32m(0xFFFF966Eu, 0x10000);
    wr8(0xFFFF9854u, 0);
    put16m(0xFFFF9EDAu, 0x1FF);
    wr16(0xFFFF974Au, 0); wr8(0xFFFF9BB6u, 0); wr8(0xFFFF9667u, 0); wr16(0xFFFF9994u, 0); F_L(0);
    move_b(1, rd8(AREA));
    cmp_byte(rd8(0xFFFF9684u), c->d[1]);
    if ((c->d[1] & 0xFF) != rd8(0xFFFF9684u)) { put8(0xFFFF9684u, c->d[1]); put16m(0xFFFF9992u, c->d[0]); }
    c->d[0] = 0xFFFFFFFFu;
    wr16(0xFFFF9682u, 0xFFFF); wr16(0xFFFF968Au, 0xFFFF); wr16(0xFFFF9686u, 0xFFFF);
    wr16(0xFFFF9748u, 0xFFFF); wr16(0xFFFF9C18u, 0xFFFF);
    c->d[0] = 0;
    move_b(0, rd8(AREA));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    c->d[0] = add_l(&c->ccr, DATA_DIR + 0x20, c->d[0]);
    c->a[1] = rd32(c->d[0]);
    c->d[0] = 0;
    move_b(0, rd8(ROOM));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], rd16(c->a[1] + sx(c->d[0]))); F_W(c->d[0]);
    c->a[2] = c->a[1] + sx(c->d[0]);
    for (;;) {
        c->d[0] = 0;
        move_b(0, next8(2));
        cmp_byte(0xFF, c->d[0]);
        if ((c->d[0] & 0xFF) == 0xFF) break;
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
        c->a[0] = ROOM_COMMANDS + sx(rd16(ROOM_COMMANDS + sx(c->d[0])));
        c->a[7] -= 4; wr32(c->a[7], 0x93FA);    /* jsr (a0) */
        room_command(c->a[0]);
        c->a[7] += 4;
    }
    call(0xF5FC);
    call(0x137C8);
    call(STREAM_GFX);
    F_B(rd8(0xFFFF9685u));
    if (rd8(0xFFFF9685u)) {
        c->d[0] = 49; F_L(49);
        call(OWNS);
        if (c->ccr & CC_Z) put8(0xFFFF9670u, 0x76);
    }
    F_W(rd16(0xFFFF9686u));
    if (rd16(0xFFFF9686u) & 0x8000) return;
    move_b(0, rd8(0xFFFF9688u));
    call(TEST_FLAG);
    if (!(c->ccr & CC_Z)) { wr8(0xFFFF9686u, 0xFF); return; }
    put8(0xFFFF9689u, rd8(0xFFFF9671u));
    put8(0xFFFF9671u, 0x76);
}

/* $96D6: one of the ending's scenes (d0, from the directory's table 7): its two layers' data,
 * a palette, the graphics, and both maps */
static void load_scene(void)
{
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->a[0] = rd32(DATA_DIR + 0x1C);
    c->a[0] += sx(c->d[0]);
    push32(c->a[0]); F_L(c->a[0]);
    init_layers();
    wr32(0xFFFF9758u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->a[1] = pop32();
    put32m(0xFFFF9788u, next32(1));
    put32m(0xFFFF9808u, next32(1));
    c->a[0] = 0xFFFF966Au;
    c->a[0] -= 2; put16m(c->a[0], next16(1));
    c->a[0] -= 2; put16m(c->a[0], 1);
    push32(c->a[1]); F_L(c->a[1]);
    call(LOAD_PALETTES);
    c->a[0] = pop32();
    put16m(0xFFFF8B56u, next16(0));
    c->a[0] = rd32(c->a[0]);
    call(SET_GFX_SLOTS);
    c->a[3] = LAYER_A;
    c->a[7] -= 4; wr32(c->a[7], 0x9718); unpack_layer_map(); c->a[7] += 4;
    c->a[3] = LAYER_B;
    unpack_layer_map();
}

/* $92D4: the room's music ($9660), from the directory's table 3 by area and room: $00-$7F and
 * $F0-$FF as they are; $EF by $DAC8 (85 or 15); others pick by a game flag from the pairs at $932A */
static void room_music(void)
{
    c->a[0] = rd32(DATA_DIR + 0xC);
    c->d[0] = 0;
    move_b(0, rd8(AREA));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    move_b(0, rd8(ROOM));
    move_b(0, rd8(c->a[0] + sx(c->d[0])));
    if (c->d[0] & 0x80) {
        cmp_byte(0xF0, c->d[0]);
        if ((c->d[0] & 0xFF) < 0xF0) {
            cmp_byte(0xEF, c->d[0]);
            if ((c->d[0] & 0xFF) >= 0xEF) {
                call(0xDAC8);
                c->d[0] = c->ccr & CC_Z ? 15 : 85; F_L(c->d[0]);
            } else {
                and_w(0, 0x7F);
                c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
                c->a[1] = 0x932Au + sx(c->d[0]);
                move_b(0, next8(1));
                call(TEST_FLAG);
                if (!(c->ccr & CC_Z)) c->a[1] += 1;
                move_b(0, rd8(c->a[1]));
            }
        }
    }
    put8(0xFFFF9660u, c->d[0]);
}

ENTRY(LoadRoom, load_room)                /* $9330 */
ENTRY(InitLayers, init_layers)            /* $967E */
ENTRY(InitLayer, init_layer)              /* $9622 */
ENTRY(SetupLayer, setup_layer)            /* $946C */
ENTRY(ReadRoomPointer, read_room_pointer) /* $9500 */
ENTRY(LoadScene, load_scene)              /* $96D6 */
ENTRY(RoomMusic, room_music)              /* $92D4 */
ENTRY(RoomLayer, cmd_layer)               /* $94AA */
ENTRY(RoomLayerAt, cmd_layer_at)          /* $94B4 */
ENTRY(RoomGfx, cmd_gfx)                   /* $9510 */
ENTRY(RoomPalettes, cmd_palettes)         /* $9520 */
ENTRY(RoomPalette2, cmd_palette2)         /* $952A */
ENTRY(RoomCmd4, cmd_4)                    /* $9530 */
ENTRY(RoomCmd5, cmd_5)                    /* $9540 */
ENTRY(RoomCmd6, cmd_6)                    /* $9548 */
ENTRY(RoomCmd7, cmd_7)                    /* $954E */
ENTRY(RoomCmd8, cmd_8)                    /* $9578 */
ENTRY(RoomCmd9, cmd_9)                    /* $958C */
ENTRY(RoomCmd11, cmd_11)                  /* $9592 */
ENTRY(RoomCmd12, cmd_12)                  /* $95AC */
ENTRY(RoomCmd13, cmd_13)                  /* $95C0 */
ENTRY(RoomCmd14, cmd_14)                  /* $95CA */
ENTRY(RoomCmd15, cmd_15)                  /* $95D0 */
ENTRY(RoomCmd18, cmd_18)                  /* $95DA */
ENTRY(RoomCmd19, cmd_19)                  /* $95EE */
ENTRY(RoomCmd20, cmd_20)                  /* $95FE */

/* ---- batch T13: setting a room's layers up ----
 * Each layer's record has a kind for each axis and for its drawing ($30-$33): how it scrolls
 * (with the camera, at a parallax, fixed, or moving by itself) and how its map is laid out (as
 * screens, or one strip repeated). $981A sets a layer up by them after unpacking its map; the
 * position goes to its scroll ($3C/$3E, and for plane A the HUD's offsets $8B34/$8B36). */

#define CAMERA_X      0xFFFF972Cu   /* long: the camera, 16.16 */
#define CAMERA_Y      0xFFFF9730u

/* asr: like lsr, but the sign is kept */
static uint32_t asr_n(uint16_t *ccr, uint32_t v, unsigned n, unsigned bits)
{
    uint64_t mask = (1ull << bits) - 1, x = v & mask;
    int64_t sv = (int64_t)(x << (64 - bits)) >> (64 - bits);
    uint32_t r = n >= bits ? (sv < 0 ? (uint32_t)mask : 0) : (uint32_t)((uint64_t)(sv >> n) & mask);
    uint16_t cf = n == 0 ? 0 : n > bits ? (sv < 0) : (uint16_t)((uint64_t)(sv >> (n - 1)) & 1);
    uint16_t xf = n == 0 ? (*ccr & CC_X) : (cf ? CC_X : 0);
    *ccr = (uint16_t)(xf | (cf ? CC_C : 0) | (r >> (bits - 1) & 1 ? CC_N : 0) | (r ? 0 : CC_Z));
    return r;
}
static uint32_t neg_b(uint32_t v) { return sub_b(&c->ccr, v, 0); }

/* $9AC4: a camera position (d0, 16.16 above $1000) scaled by the parallax d1 (a shift: right if
 * positive), kept between $1000 and the limit d2 */
static void parallax(void)
{
    c->d[0] = sub_l(&c->ccr, 0x10000000, c->d[0]);
    F_B(c->d[1]);
    if (!(c->d[1] & 0x80)) c->d[0] = asr_n(&c->ccr, c->d[0], c->d[1] & 63, 32);
    else {
        c->d[1] = set_b(c->d[1], neg_b(c->d[1]));
        c->d[0] = asl_n(&c->ccr, c->d[0], c->d[1] & 63, 32);
        c->d[1] = set_b(c->d[1], neg_b(c->d[1]));
    }
    c->d[0] = add_l(&c->ccr, 0x10000000, c->d[0]);
    c->d[0] = c->d[0] << 16 | c->d[0] >> 16; F_L(c->d[0]);
    cmp_w(&c->ccr, 0x1000, c->d[0]);
    if ((c->d[0] & 0xFFFF) < 0x1000) { c->d[0] = set_w(c->d[0], 0x1000); F_W(0x1000); }
    cmp_w(&c->ccr, c->d[2], c->d[0]);
    if ((c->d[0] & 0xFFFF) > (c->d[2] & 0xFFFF)) { c->d[0] = set_w(c->d[0], c->d[2]); F_W(c->d[2]); }
}

/* $9A8E, $9AAA: the layer's x ($4) or y ($6) from the camera at its parallax ($3A/$3B), within its
 * limit ($24/$26) */
static void layer_x(void)
{
    c->d[0] = rd32(CAMERA_X); F_L(c->d[0]);
    move_b(1, rd8(c->a[3] + 0x3A));
    c->d[2] = set_w(c->d[2], rd16(c->a[3] + 0x24)); F_W(c->d[2]);
    parallax();
    put16m(c->a[3] + 4, c->d[0]);
}
static void layer_y(void)
{
    c->d[0] = rd32(CAMERA_Y); F_L(c->d[0]);
    move_b(1, rd8(c->a[3] + 0x3B));
    c->d[2] = set_w(c->d[2], rd16(c->a[3] + 0x26)); F_W(c->d[2]);
    parallax();
    put16m(c->a[3] + 6, c->d[0]);
}

/* $9D16, $9E2E: the layer's scroll from its x or y; plane A's also kept for the moves since
 * ($9740/$9742; the y's from $9744 as the original has it) and the HUD's offsets */
static void scroll_x(void)
{
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 4)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
    and_w(0, 0x1FF);
    put16m(c->a[3] + 0x3C, c->d[0]);
    f_bit(rd8(c->a[3]) & 1);
    if (rd8(c->a[3]) & 1) return;
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 4)); F_W(c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(0xFFFF9744u), c->d[1]));
    put16m(0xFFFF9740u, c->d[1]);
    put16m(0xFFFF9744u, c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x80, c->d[0]));
    put16m(0xFFFF8B34u, c->d[0]);
}
static void scroll_y(void)
{
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 6)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x40, c->d[0]));
    and_w(0, 0xFF);
    put16m(c->a[3] + 0x3E, c->d[0]);
    f_bit(rd8(c->a[3]) & 1);
    if (rd8(c->a[3]) & 1) return;
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 6)); F_W(c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(0xFFFF9744u), c->d[1]));
    put16m(0xFFFF9742u, c->d[1]);
    put16m(0xFFFF9746u, c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0xC0, c->d[0]));
    put16m(0xFFFF8B36u, c->d[0]);
}

/* $9B6C: a moving layer's thing (the slot at $74) placed by the layer's position */
static void mover_place(void)
{
    c->a[1] = sx(rd16(c->a[3] + 0x74));
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 4)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[3] + 0x70), c->d[0]));
    put16m(c->a[1] - 0x601E, c->d[0]);
    wr16(c->a[1] - 0x5FCA, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->d[1] = set_w(c->d[1], rd16(c->a[3] + 6)); F_W(c->d[1]);
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 0x58)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[3] + 0x72), c->d[0]));
    put16m(c->a[1] - 0x601C, c->d[0]);
}

/* $98F4: a fixed layer's y: its $3B in 16ths, from $1000 */
static void fixed_y(void)
{
    c->d[0] = 0;
    move_b(0, rd8(c->a[3] + 0x3B));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    put16m(c->a[3] + 6, c->d[0]);
}

static void axis_y(uint32_t at);

/* the x kinds ($30: tbl $9868) */
static void x_parallax(void)        /* $9884: at a parallax; the limit by the room's width */
{
    move_b(0, rd8(0xFFFF978Cu));
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
    c->d[1] = 8;
    c->d[1] = set_b(c->d[1], sub_b(&c->ccr, rd8(c->a[3] + 0x3A), c->d[1]));
    if (!(c->d[1] & 0x80)) c->d[0] = set_w(c->d[0], asl_n(&c->ccr, c->d[0], c->d[1] & 63, 16));
    else {
        c->d[1] = set_b(c->d[1], neg_b(c->d[1]));
        c->d[0] = set_w(c->d[0], asr_n(&c->ccr, c->d[0], c->d[1] & 63, 16));
    }
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    put16m(c->a[3] + 0x24, c->d[0]);
    layer_x();
    scroll_x();
}
static void x_fixed(void)           /* $98A6 */
{
    c->d[0] = 0;
    move_b(0, rd8(c->a[3] + 0x3A));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    put16m(c->a[3] + 4, c->d[0]);
    scroll_x();
}
static void x_moving(void)          /* $98BA: from 0, moved each frame */
{
    c->d[0] = 0;
    put16m(c->a[3] + 4, 0);
    put16m(c->a[3] + 0x40, 0);
    scroll_x();
}

/* the y kinds ($31: tbl $9872) */
static void y_parallax(void)        /* $98C8 */
{
    c->d[0] = 0;
    move_b(0, rd8(0xFFFF978Du));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_b(c->d[0], add_b(&c->ccr, rd8(0xFFFF978Du), c->d[0]));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 6, 16));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0xA0, c->d[0]));
    move_b(1, rd8(c->a[3] + 0x3B));
    if (c->d[1] & 0x80) {
        c->d[1] = set_b(c->d[1], neg_b(c->d[1]));
        c->d[0] = set_w(c->d[0], asl_n(&c->ccr, c->d[0], c->d[1] & 63, 16));
    } else c->d[0] = set_w(c->d[0], asr_n(&c->ccr, c->d[0], c->d[1] & 63, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    put16m(c->a[3] + 0x26, c->d[0]);
    layer_y();
    scroll_y();
}
static void y_fixed(void) { fixed_y(); scroll_y(); }   /* $9906 */
static void y_moving(void)          /* $990C */
{
    c->d[0] = 0;
    put16m(c->a[3] + 6, 0);
    put16m(c->a[3] + 0x42, 0);
    scroll_y();
}
static void y_mover(void)           /* $991A: carried by a thing, by a script ($9988 by $3B) */
{
    c->d[0] = 0;
    wr32(c->a[3] + 0x58, 0); wr32(c->a[3] + 0x5C, 0); put16m(c->a[3] + 0x6C, 0);
    move_b(0, rd8(c->a[3] + 0x3B));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[2] = 0x9988u;
    c->a[0] = c->a[2];
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    put8(c->a[3] + 0x3B, next8(0));
    put8(c->a[3] + 0x77, next8(0));
    call(0x11CE8);
    put16m(c->a[3] + 0x74, c->a[1]);
    put8(c->a[1] - 0x6072, 0x80);
    c->a[0] += 1;
    put8(c->a[1] - 0x6071, next8(0));
    put32m(c->a[3] + 0x70, next32(0));
    put32m(c->a[1] - 0x5F76, next32(0));
    put32m(c->a[3] + 0x68, c->a[0]);
    c->d[0] = 0;
    wr16(c->a[1] - 0x5FCA, 0); wr16(c->a[1] - 0x5FC8, 0); wr16(c->a[3] + 0x7A, 0);
    put16m(c->a[3] + 0x6C, 1);
    move_b(0, rd8(c->a[3] + 0x77));
    c->a[0] = 0x9872u + sx(rd16(0x9872u + sx(c->d[0])));
    push32(0x9984);                                 /* jsr (a0) */
    axis_y(c->a[0]);
    c->a[7] += 4;
    mover_place();
}
static void axis_y(uint32_t at)
{
    switch (at) {
    case 0x98C8: y_parallax(); break;
    case 0x9906: y_fixed(); break;
    case 0x990C: y_moving(); break;
    case 0x991A: y_mover(); break;
    default: call(at); break;
    }
}

/* the drawing kinds ($32: tbl $987A for columns, $33: $987E for rows) */
static void columns_screens(void)   /* $99AC: the columns shown, from the x */
{
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 4)); F_W(c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], asr_n(&c->ccr, c->d[0], 4, 16));
    put16m(c->a[3] + 8, c->d[0]);
    and_w(1, 0xF);
    c->d[1] = set_b(c->d[1], (c->d[1] & 0xFFFF) ? 0 : 0xFF);   /* seq */
    c->d[1] = set_b(c->d[1], add_b(&c->ccr, 0x11, c->d[1]));
    put16m(c->a[3] + 0xA, c->d[1]);
}
static void columns_strip(void)     /* $99C8: one screen wide, repeated: the map's empty places filled */
{
    c->a[0] = sx(rd16(c->a[3] + 0x22));
    c->d[0] = 7;
    do {
        uint8_t v = rd8(c->a[0] + 1); F_B(v);
        if (v & 0x80) put8(c->a[0] + 1, rd8(c->a[0]));
        c->a[0] += 8;
    } while (dbra(0));
    put16m(c->a[3] + 8, 0x100);
    put8(c->a[3] + 0x38, 2);
    c->d[0] = 32;
    put16m(c->a[3] + 0x44, 32);
    put16m(c->a[3] + 0xA, 32);
}
static void rows_screens(void)      /* $99F6: the rows shown, from the y */
{
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 6)); F_W(c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
    and_w(0, 0xFFF0);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    put16m(c->a[3] + 0xC, c->d[0]);
    c->d[0] = set_w(c->d[0], 0x280); F_W(0x280);
    and_w(1, 0xF);
    if (c->d[1] & 0xFFFF) { c->d[0] = set_w(c->d[0], 0x2C0); F_W(0x2C0); }
    put16m(c->a[3] + 0xE, c->d[0]);
}
static void rows_strip(void)        /* $9A1A */
{
    c->a[0] = sx(rd16(c->a[3] + 0x22));
    c->d[0] = 7;
    do {
        uint8_t v = rd8(c->a[0] + 8); F_B(v);
        if (v & 0x80) put8(c->a[0] + 8, rd8(c->a[0]));
        c->a[0] += 1;
    } while (dbra(0));
    put16m(c->a[3] + 0xC, 0x4000);
    put8(c->a[3] + 0x39, 2);
    c->d[0] = set_w(c->d[0], 0x400);
    put16m(c->a[3] + 0x46, 0x400);
    put16m(c->a[3] + 0xE, 0x400);
}
static void rows_wrap(void)         /* $9A4A: as screens, wrapping vertically */
{
    uint8_t v = rd8(c->a[3]) | 2; wr8(c->a[3], v); F_B(v);
    put16m(c->a[3] + 0x46, 0x7FC0);
    rows_screens();
}

static void layer_kind(uint32_t at)
{
    switch (at) {
    case 0x9884: x_parallax(); break;
    case 0x98A6: x_fixed(); break;
    case 0x98BA: x_moving(); break;
    case 0x99AC: columns_screens(); break;
    case 0x99C8: columns_strip(); break;
    case 0x99F6: rows_screens(); break;
    case 0x9A1A: rows_strip(); break;
    case 0x9A4A: rows_wrap(); break;
    default: axis_y(at); break;
    }
}

/* $981A: layer a3: its map, then its kinds ($30-$33) */
static void layer_setup(void)
{
    c->a[7] -= 8; wr32(c->a[7], c->a[1]); wr32(c->a[7] + 4, c->a[2]);
    c->a[7] -= 4; wr32(c->a[7], 0x9822); unpack_layer_map(); c->a[7] += 4;
    c->a[1] = rd32(c->a[7]); c->a[2] = rd32(c->a[7] + 4); c->a[7] += 8;
    static const uint32_t table[4] = { 0x9868, 0x9872, 0x987A, 0x987E }, ret[4] = { 0x9836, 0x9846, 0x9856, 0x9866 };
    for (int k = 0; k < 4; k++) {
        c->d[0] = 0;
        move_b(0, rd8(c->a[3] + 0x30 + (uint32_t)k));
        c->a[0] = table[k] + sx(rd16(table[k] + sx(c->d[0])));
        push32(ret[k]);                             /* jsr (a0) */
        layer_kind(c->a[0]);
        c->a[7] += 4;
    }
}

/* $9808: both layers, plane A's and B's moves cleared first */
static void layers_setup(void)
{
    wr32(0xFFFF973Cu, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->a[3] = LAYER_A;
    push32(0x9812); layer_setup(); c->a[7] += 4;
    wr32(0xFFFF9740u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->a[3] = LAYER_B;
    layer_setup();
}

ENTRY(LayersSetup, layers_setup)           /* $9808 */
ENTRY(LayerSetup, layer_setup)             /* $981A */
ENTRY(LayerXParallax, x_parallax)          /* $9884 */
ENTRY(LayerXFixed, x_fixed)                /* $98A6 */
ENTRY(LayerXMoving, x_moving)              /* $98BA */
ENTRY(LayerYParallax, y_parallax)          /* $98C8 */
ENTRY(LayerFixedY, fixed_y)                /* $98F4 */
ENTRY(LayerYFixed, y_fixed)                /* $9906 */
ENTRY(LayerYMoving, y_moving)              /* $990C */
ENTRY(LayerYMover, y_mover)                /* $991A */
ENTRY(LayerColumns, columns_screens)       /* $99AC */
ENTRY(LayerColumnsStrip, columns_strip)    /* $99C8 */
ENTRY(LayerRows, rows_screens)             /* $99F6 */
ENTRY(LayerRowsStrip, rows_strip)          /* $9A1A */
ENTRY(LayerRowsWrap, rows_wrap)            /* $9A4A */
ENTRY(LayerX, layer_x)                     /* $9A8E */
ENTRY(LayerY, layer_y)                     /* $9AAA */
ENTRY(Parallax, parallax)                  /* $9AC4 */
ENTRY(MoverPlace, mover_place)             /* $9B6C */
ENTRY(ScrollX, scroll_x)                   /* $9D16 */
ENTRY(ScrollY, scroll_y)                   /* $9E2E */

/* ---- batch T14: the layers each frame ----
 * The camera follows the player ($9720, leading toward a second entity at $9748 if there is
 * one, and limited by the room's height when $9858 bit 3 is set); each layer then moves by its
 * kinds, draws the columns and rows of blocks that scroll into view ($9D46, $9E60), and the
 * scroll goes to the VDP's tables ($9C92). */

#define MAP_BLOCK_AT  0x2542u       /* d6, d7: the block there (a0, d1; d4 negative: none) */
#define DRAW_BLOCK    0x2694u       /* d0: a block drawn at d6, d7 on layer a3 */
#define QUEUE_SOUND   0x0366u

static void link(int32_t disp) { push32(c->a[6]); c->a[6] = c->a[7]; c->a[7] += (uint32_t)disp; }
static void unlk(void) { c->a[7] = c->a[6]; c->a[6] = pop32(); }
static uint16_t mem_sub_w(uint32_t a, uint32_t v) { uint16_t r = (uint16_t)sub_w(&c->ccr, v, rd16(a)); wr16(a, r); return r; }
static uint16_t mem_add_w(uint32_t a, uint32_t v) { uint16_t r = (uint16_t)add_w(&c->ccr, v, rd16(a)); wr16(a, r); return r; }
static uint32_t swap_l(uint32_t v) { uint32_t r = v << 16 | v >> 16; F_L(r); return r; }

/* $9720: the camera ($972C, $9730: 16.16, from the player at $FF9EEE, or $985A/$985E when
 * $9858 says so) plus the shakes ($9734, $9738) */
static void camera(void)
{
    f_bit(rd8(0xFFFF9659u) >> 5 & 1);
    if (rd8(0xFFFF9659u) & 0x20) return;
    c->a[0] = sx(rd16(0xFFFF9EEEu));
    c->d[0] = rd32(0xFFFF985Au); F_L(c->d[0]);
    f_bit(rd8(0xFFFF9858u) & 1);
    if (!(rd8(0xFFFF9858u) & 1)) {
        c->d[0] = rd32(c->a[0] - 0x3800); F_L(c->d[0]);
        c->d[0] = sub_l(&c->ccr, 0x800000, c->d[0]);
        c->d[2] = set_w(c->d[2], rd16(0xFFFF9748u)); F_W(c->d[2]);
        int lead = 0;                               /* 0 none, 1 up, -1 down */
        if (c->d[2] & 0x8000) {
            F_W(rd16(0xFFFF974Au));
            if (rd16(0xFFFF974Au)) lead = rd16(0xFFFF974Au) & 0x8000 ? 1 : -1;
            else goto add;
        } else {
            c->a[1] = sx(c->d[2]);
            c->d[2] = set_w(c->d[2], rd16(c->a[1] - 0x3800));
            c->d[2] = set_w(c->d[2], sub_w(&c->ccr, rd16(c->a[0] - 0x3800), c->d[2]));
            if (c->d[2] & 0x8000) {
                cmp_w(&c->ccr, 0xFFC0, c->d[2]);
                if ((int16_t)c->d[2] < -64) c->d[2] = 0xFFFFFFC0u;
            } else {
                cmp_w(&c->ccr, 0x40, c->d[2]);
                if ((int16_t)c->d[2] > 64) c->d[2] = 64;
            }
            cmp_w(&c->ccr, rd16(0xFFFF974Au), c->d[2]);
            if ((c->d[2] & 0xFFFF) != rd16(0xFFFF974Au)) lead = (int16_t)c->d[2] < (int16_t)rd16(0xFFFF974Au) ? -1 : 1;
        }
        if (lead > 0) mem_add_w(0xFFFF974Au, 1);
        else if (lead < 0) mem_sub_w(0xFFFF974Au, 1);
        c->d[0] = swap_l(c->d[0]);
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF974Au), c->d[0]));
        c->d[0] = swap_l(c->d[0]);
    }
add:
    c->d[0] = add_l(&c->ccr, rd32(0xFFFF9734u), c->d[0]);
    put32m(CAMERA_X, c->d[0]);
    c->d[1] = rd32(0xFFFF985Eu); F_L(c->d[1]);
    f_bit(rd8(0xFFFF9858u) >> 1 & 1);
    if (!(rd8(0xFFFF9858u) & 2)) {
        c->d[1] = rd32(c->a[0] - 0x3700); F_L(c->d[1]);
        c->d[2] = 0;
        move_b(2, rd8(c->a[0] - 0x34FD));
        c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 0x70, c->d[2]));
        c->d[2] = swap_l(c->d[2]);
        c->d[1] = add_l(&c->ccr, c->d[2], c->d[1]);
    }
    c->d[2] = rd32(0xFFFF9738u); F_L(c->d[2]);
    f_bit(rd8(0xFFFF9858u) >> 3 & 1);
    if (rd8(0xFFFF9858u) & 8) {
        c->d[1] = swap_l(c->d[1]);
        c->d[3] = 0x1008; F_L(0x1008);
        cmp_w(&c->ccr, c->d[3], c->d[1]);
        int clamp = (c->d[1] & 0xFFFF) < 0x1008;
        if (!clamp) {
            c->d[3] = set_w(c->d[3], rd16(0xFFFF977Au)); F_W(c->d[3]);
            c->d[3] = set_w(c->d[3], sub_w(&c->ccr, 8, c->d[3]));
            cmp_w(&c->ccr, c->d[3], c->d[1]);
            clamp = (c->d[1] & 0xFFFF) >= (c->d[3] & 0xFFFF);
        }
        if (clamp) { c->d[1] = c->d[3]; F_L(c->d[3]); }
        c->d[2] = swap_l(c->d[2]);
        c->d[2] = set_w(c->d[2], neg_w(&c->ccr, c->d[2]));
        if ((c->d[2] & 0xFFFF) && !(c->d[2] & 0x8000)) c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 1, c->d[2]));
        put16m(0xFFFF9738u, c->d[2]);
        c->d[2] = swap_l(c->d[2]);
        c->d[1] = swap_l(c->d[1]);
    }
    c->d[1] = add_l(&c->ccr, c->d[2], c->d[1]);
    put32m(CAMERA_Y, c->d[1]);
}

/* $9A58: the whole of layer a3 drawn: rows from $C ($E of them, in $40s), columns from $8 ($A) */
static void draw_layer(void)
{
    link(-4);
    c->d[7] = set_w(c->d[7], rd16(c->a[3] + 0xC)); F_W(c->d[7]);
    put16m(c->a[6] - 4, rd16(c->a[3] + 0xE));
    do {
        c->d[6] = set_w(c->d[6], rd16(c->a[3] + 8)); F_W(c->d[6]);
        put16m(c->a[6] - 2, rd16(c->a[3] + 0xA));
        do {
            call(MAP_BLOCK_AT);
            call(DRAW_BLOCK);
            c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
        } while (mem_sub_w(c->a[6] - 2, 1));
        c->d[0] = 64; F_L(64);
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[0], c->d[7]));
    } while (mem_sub_w(c->a[6] - 4, c->d[0]));
    unlk();
}

/* $9AA4, $9ABE: a layer at a parallax: from the camera, its scroll */
static void x_follow(void) { push32(0x9AA6); layer_x(); c->a[7] += 4; scroll_x(); }
static void y_follow(void) { push32(0x9AC0); layer_y(); c->a[7] += 4; scroll_y(); }

/* $9AF2, $9B18, $9B46: a layer moving by itself: its speed ($3A/$3B, in 16ths of a pixel) added
 * to its position and fraction ($40/$42); $9B18 also moves with plane A ($9740) */
static void move_axis(uint32_t speed_at, uint32_t pos_at, uint32_t frac_at, int with_a, void (*scroll)(void))
{
    move_b(1, rd8(c->a[3] + speed_at));
    c->d[1] = set_w(c->d[1], (uint32_t)(int32_t)(int8_t)c->d[1]); F_W(c->d[1]);
    c->d[1] = swap_l(c->d[1]);
    c->d[1] &= 0xFFFF0000u; c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->d[1] = asr_n(&c->ccr, c->d[1], 4, 32);
    if (with_a) {
        c->d[1] = swap_l(c->d[1]);
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(0xFFFF9740u), c->d[1]));
        c->d[1] = swap_l(c->d[1]);
    }
    c->d[2] = set_w(c->d[2], rd16(c->a[3] + pos_at)); F_W(c->d[2]);
    c->d[2] = swap_l(c->d[2]);
    c->d[2] = set_w(c->d[2], rd16(c->a[3] + frac_at)); F_W(c->d[2]);
    c->d[2] = add_l(&c->ccr, c->d[1], c->d[2]);
    put16m(c->a[3] + frac_at, c->d[2]);
    c->d[2] = swap_l(c->d[2]);
    put16m(c->a[3] + pos_at, c->d[2]);
    scroll();
}
static void x_drift(void) { move_axis(0x3A, 4, 0x40, 0, scroll_x); }        /* $9AF2 */
static void x_drift_a(void) { move_axis(0x3A, 4, 0x40, 1, scroll_x); }      /* $9B18 */
static void y_drift(void) { move_axis(0x3B, 6, 0x42, 0, scroll_y); }        /* $9B46 */

/* $9B96: the mover's thing's y speed ($5FC8) from its last y ($6E) */
static void mover_delta(void)
{
    c->d[1] = set_w(c->d[1], rd16(c->a[3] + 0x6E)); F_W(c->d[1]);
    put16m(c->a[3] + 0x6E, c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
    put16m(c->a[1] - 0x5FC8, c->d[0]);
}

/* $9BA6: a layer carried by a thing (a lift): its y by its kind ($77: $9C58), then the script
 * ($68: counts of frames and speeds; a 0 count jumps; a rise first bumps, with sound 57 when it
 * stops), the thing placed, and whether the player stands on it ($9854 bit 0) */
static void mover_step(void)
{
    c->d[0] = 0;
    move_b(0, rd8(c->a[3] + 0x77));
    c->a[0] = 0x9C58u + sx(rd16(0x9C58u + sx(c->d[0])));
    push32(0x9BB6);                                 /* jsr (a0) */
    if (c->a[0] == 0x9AAAu) layer_y();
    else if (c->a[0] == 0x98F4u) fixed_y();
    else call(c->a[0]);
    c->a[7] += 4;
    c->d[1] = rd32(c->a[3] + 0x5C); F_L(c->d[1]);
    c->d[0] = 0;
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 0x7A)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) {
        c->d[1] = add_l(&c->ccr, c->d[0], c->d[1]);
        put32m(c->a[3] + 0x5C, c->d[1]);
    }
    c->d[0] = rd32(c->a[3] + 0x58); F_L(c->d[0]);
    c->d[0] = add_l(&c->ccr, c->d[1], c->d[0]);
    put32m(c->a[3] + 0x58, c->d[0]);
    c->d[0] = swap_l(c->d[0]);
    mem_sub_w(c->a[3] + 6, c->d[0]);
    if (!mem_sub_w(c->a[3] + 0x6C, 1)) {
        F_W(rd16(c->a[3] + 0x7A));
        if (rd16(c->a[3] + 0x7A)) { c->d[0] = 57; F_L(57); call(QUEUE_SOUND); }
        wr16(c->a[3] + 0x7A, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
        c->a[0] = rd32(c->a[3] + 0x68);
        for (;;) {
            put16m(c->a[3] + 0x6C, next16(0));
            if (rd16(c->a[3] + 0x6C)) break;
            c->a[0] = rd32(c->a[0]);
        }
        c->d[0] = set_w(c->d[0], next16(0));
        c->d[0] = (uint32_t)(int32_t)(int16_t)c->d[0]; F_L(c->d[0]);
        c->d[0] = asl_n(&c->ccr, c->d[0], 8, 32);
        put32m(c->a[3] + 0x5C, c->d[0]);
        if (c->d[0] && !(c->d[0] & 0x80000000u)) {
            mem_sub_w(c->a[3] + 0x5E, 0x100);
            put16m(c->a[3] + 0x7A, 0x4000);
        }
        wr32(c->a[3] + 0x68, c->a[0]);
    }
    push32(0x9C1E); mover_place(); c->a[7] += 4;
    push32(0x9C22); mover_delta(); c->a[7] += 4;
    push32(0x9C26); scroll_y(); c->a[7] += 4;
    uint8_t v = rd8(0xFFFF9854u) & 0xFE; wr8(0xFFFF9854u, v); F_B(v);
    c->a[1] = sx(rd16(c->a[3] + 0x74));
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    c->d[0] = 9;
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[1] - 0x601C), c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[1] - 0x5F74), c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(c->a[4] - 0x3700), c->d[0]));
    if (c->d[0] & 0x8000) c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
    c->d[1] = 9;
    c->d[1] = set_b(c->d[1], add_b(&c->ccr, rd8(c->a[4] - 0x34FD), c->d[1]));
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if ((c->d[0] & 0xFFFF) < (c->d[1] & 0xFFFF)) { v = rd8(0xFFFF9854u) | 1; wr8(0xFFFF9854u, v); F_B(v); }
}

/* $9C5E: layer a3 moved by its x and y kinds ($30: $9C80, $31: $9C8A) */
static void layer_move_kind(uint32_t at)
{
    switch (at) {
    case 0x9AA4: x_follow(); break;
    case 0x9AF2: x_drift(); break;
    case 0x9B18: x_drift_a(); break;
    case 0x9ABE: y_follow(); break;
    case 0x9B46: y_drift(); break;
    case 0x9BA6: mover_step(); break;
    case 0x9D0A: break;                             /* fixed: nothing */
    default: call(at); break;
    }
}
static void layer_move(void)
{
    static const uint32_t table[2] = { 0x9C80, 0x9C8A }, ret[2] = { 0x9C6E, 0x9C7E };
    for (int k = 0; k < 2; k++) {
        c->d[0] = 0;
        move_b(0, rd8(c->a[3] + 0x30 + (uint32_t)k));
        c->a[0] = table[k] + sx(rd16(table[k] + sx(c->d[0])));
        push32(ret[k]);
        layer_move_kind(c->a[0]);
        c->a[7] += 4;
    }
}

/* $9C92: both layers' scroll to the VDP's tables (horizontal at $FF2400/$FF2600, vertical
 * $8A8C/$8AB4), with the screen's shake ($973C/$973E) */
static void scroll_out(void)
{
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9790u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF973Cu), c->d[0]));
    put16m(0xFFFF2400u, c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9792u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF973Eu), c->d[0]));
    put16m(0xFFFF8A8Cu, c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9810u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF973Cu), c->d[0]));
    put16m(0xFFFF2600u, c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9812u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF973Eu), c->d[0]));
    put16m(0xFFFF8AB4u, c->d[0]);
    uint8_t v = rd8(0xFFFF8006u) | 8; wr8(0xFFFF8006u, v); F_B(v);
}

/* $9DC8: a column of blocks (d6) drawn down from row $C, $E of it, wrapping each $300 of
 * rows; its counts are in the caller's frame (a6 - 6: rows left, a6 - 2: to the wrap) */
static void draw_column(void)
{
    put16m(c->a[6] - 6, rd16(c->a[3] + 0xE));
    c->d[7] = set_w(c->d[7], rd16(c->a[3] + 0xC)); F_W(c->d[7]);
    for (;;) {
        c->d[0] = 0;
        c->d[0] = set_w(c->d[0], c->d[7]); F_W(c->d[7]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x4000, c->d[0]));
        uint32_t q = c->d[0] / 0x300, r = c->d[0] % 0x300;   /* divu.w #$300 */
        c->d[0] = r << 16 | q;
        c->ccr = (uint16_t)((c->ccr & CC_X) | (q & 0x8000 ? CC_N : 0) | (q ? 0 : CC_Z));
        c->d[0] = swap_l(c->d[0]);
        c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x300, c->d[0]));
        put16m(c->a[6] - 2, c->d[0]);
        call(MAP_BLOCK_AT);
        F_B(c->d[4]);
        if (!(c->d[4] & 0x80)) {
            c->a[4] = c->a[0] + sx(c->d[1]) + 0x20;
            for (;;) {
                call(DRAW_BLOCK);
                c->d[0] = 64; F_L(64);
                c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[0], c->d[7]));
                if (!mem_sub_w(c->a[6] - 6, c->d[0])) return;
                if (!mem_sub_w(c->a[6] - 2, c->d[0])) break;
                c->d[0] = set_w(c->d[0], rd16(c->a[4])); F_W(c->d[0]);
                c->a[4] += 0x20;
            }
        } else {
            for (;;) {
                c->d[0] = 0; F_L(0);
                call(DRAW_BLOCK);
                c->d[0] = 64; F_L(64);
                c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[0], c->d[7]));
                if (!mem_sub_w(c->a[6] - 6, c->d[0])) return;
                if (!mem_sub_w(c->a[6] - 2, c->d[0])) break;
            }
        }
    }
}

/* $9EF0: a row of blocks (d7) drawn across from column $8, $A of it, wrapping each 16 */
static void draw_row(void)
{
    put16m(c->a[6] - 4, rd16(c->a[3] + 0xA));
    c->d[6] = set_w(c->d[6], rd16(c->a[3] + 8)); F_W(c->d[6]);
    for (;;) {
        c->d[0] = 0;
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, c->d[6], c->d[0]));
        and_w(0, 0xF);
        if (!(c->d[0] & 0xFFFF)) c->d[0] = 16;
        put16m(c->a[6] - 2, c->d[0]);
        call(MAP_BLOCK_AT);
        F_B(c->d[4]);
        if (!(c->d[4] & 0x80)) {
            c->a[4] = c->a[0] + sx(c->d[1]) + 2;
            for (;;) {
                call(DRAW_BLOCK);
                c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
                if (!mem_sub_w(c->a[6] - 4, 1)) return;
                if (!mem_sub_w(c->a[6] - 2, 1)) break;
                c->d[0] = set_w(c->d[0], next16(4)); F_W(c->d[0]);
            }
        } else {
            for (;;) {
                c->d[0] = 0; F_L(0);
                call(DRAW_BLOCK);
                c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
                if (!mem_sub_w(c->a[6] - 4, 1)) return;
                if (!mem_sub_w(c->a[6] - 2, 1)) break;
            }
        }
    }
}

/* $9D46: the columns that scrolled into view drawn (to the right, or left), up to 32 kept ($8, $A) */
static void columns_in(void)
{
    link(-6);
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 4)); F_W(c->d[0]);
    c->d[2] = set_w(c->d[2], c->d[0]); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], asr_n(&c->ccr, c->d[0], 4, 16));
    c->d[6] = set_w(c->d[6], rd16(c->a[3] + 8)); F_W(c->d[6]);
    cmp_w(&c->ccr, c->d[6], c->d[0]);
    if ((c->d[0] & 0xFFFF) >= (c->d[6] & 0xFFFF)) {
        and_w(2, 0xF);
        c->d[2] = set_b(c->d[2], (c->d[2] & 0xFFFF) ? 0 : 0xFF);
        c->d[1] = 17;
        c->d[1] = set_b(c->d[1], add_b(&c->ccr, c->d[2], c->d[1]));
        c->d[2] = set_w(c->d[2], c->d[0]); F_W(c->d[0]);
        c->d[2] = set_w(c->d[2], add_w(&c->ccr, c->d[1], c->d[2]));
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, rd16(c->a[3] + 0xA), c->d[6]));
        c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[6], c->d[2]));
        if (!(c->ccr & (CC_C | CC_Z))) {
            put16m(c->a[6] - 4, c->d[2]);
            do {
                push32(0x9D78); draw_column(); c->a[7] += 4;
                c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
                mem_add_w(c->a[3] + 0xA, 1);
            } while (mem_sub_w(c->a[6] - 4, 1));
            c->d[0] = set_w(c->d[0], rd16(c->a[3] + 0xA)); F_W(c->d[0]);
            c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x20, c->d[0]));
            if (!(c->ccr & (CC_C | CC_Z))) {
                mem_add_w(c->a[3] + 8, c->d[0]);
                put16m(c->a[3] + 0xA, 0x20);
            }
        }
    } else {
        c->d[3] = set_w(c->d[3], c->d[6]); F_W(c->d[6]);
        c->d[3] = set_w(c->d[3], sub_w(&c->ccr, c->d[0], c->d[3]));
        put16m(c->a[6] - 4, c->d[3]);
        do {
            c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
            mem_sub_w(c->a[3] + 8, 1);
            push32(0x9DAA); draw_column(); c->a[7] += 4;
            mem_add_w(c->a[3] + 0xA, 1);
        } while (mem_sub_w(c->a[6] - 4, 1));
        c->d[0] = set_w(c->d[0], rd16(c->a[3] + 0xA)); F_W(c->d[0]);
        cmp_w(&c->ccr, 0x20, c->d[0]);
        if ((c->d[0] & 0xFFFF) > 0x20) put16m(c->a[3] + 0xA, 0x20);
    }
    unlk();
}

/* $9E60: the rows that scrolled into view drawn (down, or up), up to $400 kept ($C, $E) */
static void rows_in(void)
{
    link(-6);
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + 6)); F_W(c->d[0]);
    c->d[2] = set_w(c->d[2], c->d[0]); F_W(c->d[0]);
    and_w(0, 0xFFF0);
    c->d[0] = set_w(c->d[0], asl_n(&c->ccr, c->d[0], 2, 16));
    c->d[7] = set_w(c->d[7], rd16(c->a[3] + 0xC)); F_W(c->d[7]);
    cmp_w(&c->ccr, c->d[7], c->d[0]);
    if ((c->d[0] & 0xFFFF) >= (c->d[7] & 0xFFFF)) {
        c->d[1] = set_w(c->d[1], 0x280); F_W(0x280);
        and_w(2, 0xF);
        if (c->d[2] & 0xFFFF) { c->d[1] = set_w(c->d[1], 0x2C0); F_W(0x2C0); }
        c->d[2] = set_w(c->d[2], c->d[0]); F_W(c->d[0]);
        c->d[2] = set_w(c->d[2], add_w(&c->ccr, c->d[1], c->d[2]));
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, rd16(c->a[3] + 0xE), c->d[7]));
        c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[7], c->d[2]));
        if (!(c->ccr & (CC_C | CC_Z))) {
            put16m(c->a[6] - 6, c->d[2]);
            do {
                push32(0x9E9A); draw_row(); c->a[7] += 4;
                c->d[0] = 64; F_L(64);
                c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[0], c->d[7]));
                mem_add_w(c->a[3] + 0xE, c->d[0]);
            } while (mem_sub_w(c->a[6] - 6, c->d[0]));
            c->d[0] = set_w(c->d[0], rd16(c->a[3] + 0xE)); F_W(c->d[0]);
            c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x400, c->d[0]));
            if (!(c->ccr & (CC_C | CC_Z))) {
                mem_add_w(c->a[3] + 0xC, c->d[0]);
                put16m(c->a[3] + 0xE, 0x400);
            }
        }
    } else {
        c->d[3] = set_w(c->d[3], c->d[7]); F_W(c->d[7]);
        c->d[3] = set_w(c->d[3], sub_w(&c->ccr, c->d[0], c->d[3]));
        put16m(c->a[6] - 6, c->d[3]);
        c->d[0] = 64; F_L(64);
        do {
            c->d[7] = set_w(c->d[7], sub_w(&c->ccr, c->d[0], c->d[7]));
            mem_sub_w(c->a[3] + 0xC, c->d[0]);
            mem_add_w(c->a[3] + 0xE, c->d[0]);
            push32(0x9ED4); draw_row(); c->a[7] += 4;
            c->d[0] = 64; F_L(64);
        } while (mem_sub_w(c->a[6] - 6, c->d[0]));
        c->d[0] = set_w(c->d[0], rd16(c->a[3] + 0xE)); F_W(c->d[0]);
        cmp_w(&c->ccr, 0x400, c->d[0]);
        if ((c->d[0] & 0xFFFF) > 0x400) put16m(c->a[3] + 0xE, 0x400);
    }
    unlk();
}

/* $9CE6: layer a3 each frame: moved, then its new columns ($32: $9D0C) and rows ($33: $9D10) */
static void layer_frame(void)
{
    push32(0x9CEA); layer_move(); c->a[7] += 4;
    static const uint32_t table[2] = { 0x9D0C, 0x9D10 }, ret[2] = { 0x9CFA, 0x9D0A };
    for (int k = 0; k < 2; k++) {
        c->d[0] = 0;
        move_b(0, rd8(c->a[3] + 0x32 + (uint32_t)k));
        c->a[0] = table[k] + sx(rd16(table[k] + sx(c->d[0])));
        push32(ret[k]);
        if (c->a[0] == 0x9D46u) columns_in();
        else if (c->a[0] == 0x9E60u) rows_in();
        else if (c->a[0] != 0x9D0Au) call(c->a[0]);
        c->a[7] += 4;
    }
}

/* $9CD2: each frame of the game: the camera, both layers, the scroll out */
static void layers_frame(void)
{
    push32(0x9CD6); camera(); c->a[7] += 4;
    c->a[3] = LAYER_A; push32(0x9CDC); layer_frame(); c->a[7] += 4;
    c->a[3] = LAYER_B; push32(0x9CE2); layer_frame(); c->a[7] += 4;
    push32(0x9CE4); scroll_out(); c->a[7] += 4;
}

/* $97F0: both layers drawn whole (a room's start), the scroll out, the HUD's panel ($7544) */
static void layers_draw(void)
{
    c->a[3] = LAYER_A; push32(0x97F8); draw_layer(); c->a[7] += 4;
    c->a[3] = LAYER_B; push32(0x9800); draw_layer(); c->a[7] += 4;
    push32(0x9804); scroll_out(); c->a[7] += 4;
    call(0x7544);
}

ENTRY(Camera, camera)                      /* $9720 */
ENTRY(LayersDraw, layers_draw)             /* $97F0 */
ENTRY(DrawLayer, draw_layer)               /* $9A58 */
ENTRY(LayerXFollow, x_follow)              /* $9AA4 */
ENTRY(LayerYFollow, y_follow)              /* $9ABE */
ENTRY(LayerXDrift, x_drift)                /* $9AF2 */
ENTRY(LayerXDriftA, x_drift_a)             /* $9B18 */
ENTRY(LayerYDrift, y_drift)                /* $9B46 */
ENTRY(MoverDelta, mover_delta)             /* $9B96 */
ENTRY(MoverStep, mover_step)               /* $9BA6 */
ENTRY(LayerMove, layer_move)               /* $9C5E */
ENTRY(ScrollOut, scroll_out)               /* $9C92 */
ENTRY(LayersFrame, layers_frame)           /* $9CD2 */
ENTRY(LayerFrame, layer_frame)             /* $9CE6 */
ENTRY(ColumnsIn, columns_in)               /* $9D46 */
ENTRY(DrawColumn, draw_column)             /* $9DC8 */
ENTRY(RowsIn, rows_in)                     /* $9E60 */
ENTRY(DrawRow, draw_row)                   /* $9EF0 */

/* ---- batch T15: the map's cells, the game's flags, rectangles of the map ----
 * A layer's cells live in its chunks at $FF5000 ($180 a chunk, 192 cells of 16 x 12): the cell
 * at column d6 (from $100) and row d7 (from $4000, $40 a row) is found through the layer's map
 * ($22) of places; a place is 16 columns by 12 rows ($300). Drawing a block puts its four tiles
 * (from the layer's tables at $10) on the layer's plane ($20) at the column and row mod 32. */

#define CHUNKS        0x00FF5000u
#define RECT          0xFFFF974Cu   /* words: column, row, width, height of a rectangle of cells */

/* $260A: a0 at the cell (d6, d7) of layer a3 */
static void cell_at(void)
{
    c->d[2] = set_w(c->d[2], c->d[6]); F_W(c->d[6]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 0x100, c->d[2]));
    c->d[3] = set_w(c->d[3], c->d[7]); F_W(c->d[7]);
    c->d[3] = set_w(c->d[3], sub_w(&c->ccr, 0x4000, c->d[3]));
    c->d[5] = 0;
    c->d[4] = set_w(c->d[4], 0xC00); F_W(0xC00);
    cmp_w(&c->ccr, c->d[4], c->d[3]);
    if ((c->d[3] & 0xFFFF) >= 0xC00) { c->d[3] = set_w(c->d[3], sub_w(&c->ccr, c->d[4], c->d[3])); c->d[5] = 32; F_L(32); }
    c->d[4] = set_w(c->d[4], lsr_n(&c->ccr, c->d[4], 1, 16));
    cmp_w(&c->ccr, c->d[4], c->d[3]);
    if ((c->d[3] & 0xFFFF) >= (c->d[4] & 0xFFFF)) {
        c->d[3] = set_w(c->d[3], sub_w(&c->ccr, c->d[4], c->d[3]));
        c->d[5] = set_w(c->d[5], add_w(&c->ccr, 0x10, c->d[5]));
    }
    c->d[4] = set_w(c->d[4], lsr_n(&c->ccr, c->d[4], 1, 16));
    cmp_w(&c->ccr, c->d[4], c->d[3]);
    if ((c->d[3] & 0xFFFF) >= (c->d[4] & 0xFFFF)) {
        c->d[3] = set_w(c->d[3], sub_w(&c->ccr, c->d[4], c->d[3]));
        c->d[5] = set_b(c->d[5], add_b(&c->ccr, 8, c->d[5]));
    }
    c->d[4] = set_w(c->d[4], c->d[2]); F_W(c->d[2]);
    c->d[4] = set_w(c->d[4], lsr_n(&c->ccr, c->d[4], 4, 16));
    or_w(4, c->d[5]);
    c->a[0] = sx(rd16(c->a[3] + 0x22));
    move_b(4, rd8(c->a[0] + sx(c->d[4])));
    c->d[5] = set_w(c->d[5], c->d[4]); F_W(c->d[4]);
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, c->d[4], c->d[4]));
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, c->d[5], c->d[4]));
    c->d[4] = set_w(c->d[4], lsl_n(&c->ccr, c->d[4], 7, 16));
    c->d[3] = set_w(c->d[3], lsr_n(&c->ccr, c->d[3], 1, 16));
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, c->d[3], c->d[4]));
    and_w(2, 0xF);
    c->d[2] = set_w(c->d[2], add_w(&c->ccr, c->d[2], c->d[2]));
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, c->d[2], c->d[4]));
    c->a[0] = CHUNKS + sx(c->d[4]);
}

/* $2694: block d0 (bit 15 in front: its tiles' priority set) drawn on layer a3's plane at
 * (d6, d7) */
static void draw_block(void)
{
    c->d[2] = set_w(c->d[2], c->d[0]); F_W(c->d[0]);
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], c->d[0]);
    and_w(0, 0x300);
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 6, 16));
    c->a[0] = rd32(c->a[3] + 0x10 + sx(c->d[0]));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 3, 16));
    c->a[0] += sx(c->d[1]);
    c->a[1] = 0x00C00000u;
    c->d[1] = 31;
    and_w(1, c->d[6]);
    c->d[3] = set_w(c->d[3], c->d[7]); F_W(c->d[7]);
    and_w(3, 0x3C0);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[3], c->d[1]));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 2, 16));
    or_w(1, rd16(c->a[3] + 0x20));
    c->d[1] = swap_l(c->d[1]);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, 3, c->d[1]));
    put32m(c->a[1] + 4, c->d[1]);
    f_bit(c->d[1] >> 23 & 1); c->d[1] |= 1u << 23;
    F_W(c->d[2]);
    if (!(c->d[2] & 0x8000)) {
        put32m(c->a[1], next32(0));
        put32m(c->a[1] + 4, c->d[1]);
        put32m(c->a[1], rd32(c->a[0]));
        return;
    }
    c->d[3] = 0x80008000u; F_L(c->d[3]);
    c->d[0] = next32(0); F_L(c->d[0]);
    c->d[0] |= c->d[3]; F_L(c->d[0]);
    put32m(c->a[1], c->d[0]);
    put32m(c->a[1] + 4, c->d[1]);
    c->d[3] |= rd32(c->a[0]); F_L(c->d[3]);
    put32m(c->a[1], c->d[3]);
}

/* $2666: cell (d6, d7) set to d0; $2692 and drawn; $2672: drawn if inside what is shown
 * ($8-$E); $2670 and set first; $266C on plane A */
static void cell_set(void) { push32(0x2668); cell_at(); c->a[7] += 4; put16m(c->a[0], c->d[0]); }
static void draw_if_shown(void)
{
    c->d[1] = set_w(c->d[1], c->d[6]); F_W(c->d[6]);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(c->a[3] + 8), c->d[1]));
    if (c->ccr & CC_C) return;
    cmp_w(&c->ccr, rd16(c->a[3] + 0xA), c->d[1]);
    if (!(c->ccr & CC_C)) return;
    c->d[1] = set_w(c->d[1], c->d[7]); F_W(c->d[7]);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(c->a[3] + 0xC), c->d[1]));
    if (c->ccr & CC_C) return;
    cmp_w(&c->ccr, rd16(c->a[3] + 0xE), c->d[1]);
    if (c->ccr & CC_C) draw_block();
}
static void cell_set_draw_if_shown(void) { push32(0x2672); cell_set(); c->a[7] += 4; draw_if_shown(); }   /* $2670 */
static void cell_set_a(void) { c->a[3] = LAYER_A; cell_set_draw_if_shown(); }                           /* $266C */
static void cell_set_draw(void) { push32(0x2694); cell_set(); c->a[7] += 4; draw_block(); }             /* $2692 */

/* $26F0: the byte of the block at (d6, d7) (d1: from its table + $800) */
static void block_byte(void)
{
    call(MAP_BLOCK_AT);
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
    and_w(1, 0x300);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 6, 16));
    c->a[0] = rd32(c->a[3] + 0x10 + sx(c->d[1]));
    c->d[1] = set_w(c->d[1], 0x800); F_W(0x800);
    move_b(1, (uint8_t)c->d[0]);
    move_b(1, rd8(c->a[0] + sx(c->d[1])));
}

/* $270C: flag d0's byte (d2) and bit (d1): under $20 from $99BA, else $99BE; $2728 tests it,
 * $2730 sets it */
static void flag2_at(void)
{
    c->d[2] = 0;
    c->d[2] = set_b(c->d[2], c->d[0]);
    c->d[2] = set_w(c->d[2], lsr_n(&c->ccr, c->d[2], 3, 16));
    move_b(1, (uint8_t)c->d[0]);
    and_w(1, 7);
    c->a[0] = 0xFFFF99BAu;
    cmp_byte(0x20, c->d[0]);
    if ((c->d[0] & 0xFF) >= 0x20) c->a[0] = 0xFFFF99BEu;
}
static void flag2_test(void) { push32(0x272A); flag2_at(); c->a[7] += 4; f_bit(rd8(c->a[0] + sx(c->d[2])) >> (c->d[1] & 7) & 1); }
static void flag2_set(void)
{
    push32(0x2732); flag2_at(); c->a[7] += 4;
    uint32_t a = c->a[0] + sx(c->d[2]);
    f_bit(rd8(a) >> (c->d[1] & 7) & 1); wr8(a, rd8(a) | 1 << (c->d[1] & 7));
}

/* $2738: game flag d0's byte (a0, from $99BE) and bit (d1); $274C tests it, $2752 sets it */
static void flag_addr(void)
{
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], c->d[0]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 3, 16));
    c->a[0] = 0xFFFF99BEu + sx(c->d[1]);
    move_b(1, (uint8_t)c->d[0]);
    and_w(1, 7);
}
static void flag_test(void) { push32(0x274E); flag_addr(); c->a[7] += 4; f_bit(rd8(c->a[0]) >> (c->d[1] & 7) & 1); }
static void flag_set(void)
{
    push32(0x2754); flag_addr(); c->a[7] += 4;
    f_bit(rd8(c->a[0]) >> (c->d[1] & 7) & 1); wr8(c->a[0], rd8(c->a[0]) | 1 << (c->d[1] & 7));
}
/* $2758: the room flags' byte ($9992, the bytes in pairs swapped) and bit of d0 */
static void room_flag_bit(void)
{
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], c->d[0]);
    and_w(0, 7);
    c->a[0] = 0xFFFF9992u;
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 3, 16));
    c->d[1] = set_w(c->d[1], c->d[1] ^ 1); F_W(c->d[1]);
}

/* the rectangle at RECT of plane A's cells, row by row: $A1FE saves it to $FF3100, $A23C marks
 * it (bit 15: in front), $A276 restores it, $A2B4 redraws it */
static void rect_each(int what)
{
    link(-4);
    c->a[3] = LAYER_A;
    if (what == 0 || what == 2) c->a[1] = 0x00FF3100u;
    c->d[7] = set_w(c->d[7], rd16(RECT + 2)); F_W(c->d[7]);
    put16m(c->a[6] - 4, rd16(RECT + 6));
    do {
        c->d[6] = set_w(c->d[6], rd16(RECT)); F_W(c->d[6]);
        put16m(c->a[6] - 2, rd16(RECT + 4));
        do {
            if (what == 3) {
                call(MAP_BLOCK_AT);
                push32(0xA2D8); draw_if_shown(); c->a[7] += 4;
            } else {
                push32(what == 0 ? 0xA224 : what == 1 ? 0xA25C : 0xA29C); cell_at(); c->a[7] += 4;
                if (what == 0) { put16m(c->a[1], rd16(c->a[0])); c->a[1] += 2; }
                else if (what == 1) { uint16_t v = rd16(c->a[0]) | 0x8000; wr16(c->a[0], v); F_W(v); }
                else { put16m(c->a[0], rd16(c->a[1])); c->a[1] += 2; }
            }
            c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
        } while (mem_sub_w(c->a[6] - 2, 1));
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    } while (mem_sub_w(c->a[6] - 4, 1));
    unlk();
}
static void rect_save(void) { rect_each(0); }      /* $A1FE */
static void rect_mark(void) { rect_each(1); }      /* $A23C */
static void rect_restore(void) { rect_each(2); }   /* $A276 */
static void rect_redraw(void) { rect_each(3); }    /* $A2B4 */

/* $A2EE: d4 x d5 blocks from (d6, d7 in rows) straight to VRAM at d3 (a VDP address, $100 a
 * row), through MapBlockAtRaw: for the ending's pictures */
static void blocks_to_vram(void)
{
    link(-14);
    c->d[3] = lsl_n(&c->ccr, c->d[3], 2, 32);
    c->d[3] = set_w(c->d[3], lsr_n(&c->ccr, c->d[3], 2, 16));
    or_w(3, 0x4000);
    put16m(c->a[6] - 14, c->d[3]);
    c->d[3] = swap_l(c->d[3]);
    and_w(3, 3);
    put16m(c->a[6] - 10, c->d[3]);
    put16m(c->a[6] - 2, c->d[4]);
    put16m(c->a[6] - 6, c->d[5]);
    put16m(c->a[6] - 8, c->d[6]);
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 6, 16));
    c->a[2] = 0x00C00000u;
    do {
        put16m(c->a[6] - 4, rd16(c->a[6] - 2));
        put16m(c->a[6] - 12, rd16(c->a[6] - 14));
        c->d[6] = set_w(c->d[6], rd16(c->a[6] - 8)); F_W(c->d[6]);
        do {
            c->d[2] = set_w(c->d[2], c->d[6]); F_W(c->d[6]);
            c->d[3] = set_w(c->d[3], c->d[7]); F_W(c->d[7]);
            call(0x2570);
            c->d[2] = set_w(c->d[2], c->d[0]); F_W(c->d[0]);
            c->d[1] = 0;
            c->d[1] = set_b(c->d[1], c->d[0]);
            and_w(0, 0x300);
            c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 6, 16));
            c->a[0] = rd32(c->a[3] + 0x10 + sx(c->d[0]));
            c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 3, 16));
            c->a[0] += sx(c->d[1]);
            c->d[0] = rd32(c->a[6] - 12); F_L(c->d[0]);
            put32m(c->a[2] + 4, c->d[0]);
            put32m(c->a[2], next32(0));
            c->d[0] = add_l(&c->ccr, 0x800000, c->d[0]);
            put32m(c->a[2] + 4, c->d[0]);
            put32m(c->a[2], rd32(c->a[0]));
            c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
            mem_add_w(c->a[6] - 12, 4);
        } while (mem_sub_w(c->a[6] - 4, 1));
        mem_add_w(c->a[6] - 14, 0x100);
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    } while (mem_sub_w(c->a[6] - 6, 1));
    unlk();
}

/* $A3B4: d0 and d1 (bytes) as positions: 8 pixels each, from $1000 */
static void to_position(void)
{
    and_w(0, 0xFF);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 3, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    and_w(1, 0xFF);
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 3, 16));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, 0x1000, c->d[1]));
}
/* $A3A4: a place (two bytes at a2) as the player's start ($967A, $967C) */
static void start_at(void)
{
    move_b(0, next8(2));
    move_b(1, next8(2));
    push32(0xA3AA); to_position(); c->a[7] += 4;
    put16m(0xFFFF967Au, c->d[0]);
    put16m(0xFFFF967Cu, c->d[1]);
}
/* $A37E: start point $9676 (from the directory's table 0): the area and room, a kind ($9678),
 * the player's place, a word ($996A) */
static void start_point(void)
{
    c->a[1] = rd32(DATA_DIR);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9676u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] = sx(c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(c->a[1] + sx(c->d[0]))); F_W(c->d[0]);
    c->a[2] = c->a[1] + sx(c->d[0]);
    put16m(0xFFFF9668u, next16(2));
    put16m(0xFFFF9678u, next16(2));
    push32(0xA39E); start_at(); c->a[7] += 4;
    put16m(0xFFFF996Au, next16(2));
}
/* $A3CA: under water, entity a4's word at $CA02 doubled (asl) */
static void water_double(void)
{
    f_bit(rd8(0xFFFF966Bu) & 1);
    if (!(rd8(0xFFFF966Bu) & 1)) return;
    uint32_t a = c->a[4] - 0x35FE;
    wr16(a, (uint16_t)asl_n(&c->ccr, rd16(a), 1, 16));
}

/* $92A4: a step of the fade to the room's palette (by $9666 / 4): the 15 colours of $8BB8 to
 * $8C38 through $594A, then the palette marked for the VBlank */
static void fade_step(void)
{
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], rd8(0xFFFF9666u));
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 16));
    c->d[2] = set_w(c->d[2], c->d[1]); F_W(c->d[1]);
    c->d[2] = set_w(c->d[2], asl_n(&c->ccr, c->d[2], 4, 16));
    c->d[3] = set_w(c->d[3], c->d[2]); F_W(c->d[2]);
    c->d[3] = set_w(c->d[3], asl_n(&c->ccr, c->d[3], 4, 16));
    c->a[0] = 0xFFFF8BB8u;
    c->a[1] = 0xFFFF8C38u;
    c->d[0] = 14;
    do call(0x594A); while (dbra(0));
    uint8_t v = rd8(0xFFFF8C56u) & 0x3F; wr8(0xFFFF8C56u, v);
    v |= 0x81; wr8(0xFFFF8C56u, v); F_B(v);
}

ENTRY(CellAt, cell_at)                     /* $260A */
ENTRY(CellSet, cell_set)                   /* $2666 */
ENTRY(CellSetA, cell_set_a)                /* $266C */
ENTRY(CellSetShown, cell_set_draw_if_shown) /* $2670 */
ENTRY(DrawIfShown, draw_if_shown)          /* $2672 */
ENTRY(CellSetDraw, cell_set_draw)          /* $2692 */
ENTRY(DrawBlock, draw_block)               /* $2694 */
ENTRY(BlockByte, block_byte)               /* $26F0 */
ENTRY(Flag2At, flag2_at)                   /* $270C */
ENTRY(Flag2Test, flag2_test)               /* $2728 */
ENTRY(Flag2Set, flag2_set)                 /* $2730 */
ENTRY(GameFlagAddr, flag_addr)             /* $2738 */
ENTRY(TestGameFlag, flag_test)             /* $274C */
ENTRY(SetGameFlag, flag_set)               /* $2752 */
ENTRY(RoomFlagBit, room_flag_bit)          /* $2758 */
ENTRY(RectSave, rect_save)                 /* $A1FE */
ENTRY(RectMark, rect_mark)                 /* $A23C */
ENTRY(RectRestore, rect_restore)           /* $A276 */
ENTRY(RectRedraw, rect_redraw)             /* $A2B4 */
ENTRY(BlocksToVram, blocks_to_vram)        /* $A2EE */
ENTRY(StartPoint, start_point)             /* $A37E */
ENTRY(StartAt, start_at)                   /* $A3A4 */
ENTRY(ToPosition, to_position)             /* $A3B4 */
ENTRY(WaterDouble, water_double)           /* $A3CA */
ENTRY(FadeStep, fade_step)                 /* $92A4 */

/* ---- batch T16: the room's triggers ----
 * A room's triggers ($996C, from its command 5) are a list read each frame by $A3D8 with the
 * player in a4: zones of $10 x $10 cells (the size set by $C0-$CF and $D0-$DF, the origin by
 * some events) and what touching them does: exits to another room, doors, the shops' and
 * houses' entrances, sounds. A trigger that fires usually stores a routine at $998A (or $998E)
 * and sets $9980 (or $9981); the game calls it when the player presses up there. Commands $E0
 * and up are events ($A490, not yet C), $FF the end. */

#define TRIGGERS      0xFFFF996Cu   /* long: the list, or 0 */
#define ZONE_W        0xFFFF9974u   /* word: a zone's width, height */
#define ZONE_H        0xFFFF9976u
#define ZONE_X        0xFFFF9978u   /* word: the zones' origin */
#define ZONE_Y        0xFFFF997Au
#define ROOM_CHANGE   0xFFFF9962u   /* word: what the game does next (0 nothing) */
#define CHECK_FLAG2   0x2728u

/* $A914: the zone's place from its byte at a2 (low nibble x, high y, in 16s) */
static void zone_place(void)
{
    move_b(0, rd8(c->a[2]));
    move_b(1, (uint8_t)c->d[0]);
    and_w(0, 0xF);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(ZONE_X), c->d[0]));
    and_w(1, 0xF0);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(ZONE_Y), c->d[1]));
}

/* $A92C: whether the player is in the zone (cs: yes; d2, d3 its place in it) */
static void in_zone(void)
{
    move_b(1, rd8(c->a[2]));
    c->d[0] = 15;
    and_w(0, c->d[1]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(ZONE_X), c->d[0]));
    c->d[2] = set_w(c->d[2], rd16(c->a[4] - 0x3800)); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[0], c->d[2]));
    if (c->ccr & CC_C) { c->ccr &= ~CC_C; return; }
    cmp_w(&c->ccr, rd16(ZONE_W), c->d[2]);
    if (!(c->ccr & CC_C)) return;
    and_w(1, 0xF0);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(ZONE_Y), c->d[1]));
    c->d[3] = set_w(c->d[3], rd16(c->a[4] - 0x3700)); F_W(c->d[3]);
    c->d[3] = set_w(c->d[3], sub_w(&c->ccr, c->d[1], c->d[3]));
    if (c->ccr & CC_C) { c->ccr &= ~CC_C; return; }
    cmp_w(&c->ccr, rd16(ZONE_H), c->d[3]);
}

/* $A962: an exit: to area and room (the bytes after the trigger's), the player at its start
 * place there, offset by d0, d1 */
static void exit_to(void)
{
    put16m(0xFFFF967Eu, c->d[0]);
    put16m(0xFFFF9680u, c->d[1]);
    c->a[2] += 1;
    put8(AREA, next8(2));
    put8(ROOM, next8(2));
    push32(0xA978); start_at(); c->a[7] += 4;
    push32(0xA97C); water_double(); c->a[7] += 4;
    put8(0xFFFF9678u, 2);
    put8(0xFFFF966Au, 1);
    put16m(ROOM_CHANGE, 1);
}

/* $A88C: d1 the distance to the player's x, less 4 (cs: within 4) */
static void near_x(void)
{
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[1]));
    if (c->d[1] & 0x8000) c->d[1] = set_w(c->d[1], neg_w(&c->ccr, c->d[1]));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 4, c->d[1]));
}

/* the player's feet: y + height ($CB03) */
static uint32_t feet(int r)
{
    c->d[r] = 0;
    move_b(r, rd8(c->a[4] - 0x34FD));
    c->d[r] = set_w(c->d[r], add_w(&c->ccr, rd16(c->a[4] - 0x3700), c->d[r]));
    return c->d[r] & 0xFFFF;
}
static void armed(uint32_t routine, uint32_t cell, uint32_t on)   /* a routine for up */
{
    put32m(cell, routine);
    wr8(on, 0xFF);
}

/* the commands under $C0 ($A4D0); each returns 1 to go on to the next trigger ($A46E), 0 to
 * leave $A3D8 */
static int trig_exit(int kind)       /* $A4EE, $A506, $A51E, $A52E: exits by zone */
{
    push32(kind == 0 ? 0xA4F2 : kind == 1 ? 0xA50A : kind == 2 ? 0xA522 : 0xA532); in_zone(); c->a[7] += 4;
    if (!(c->ccr & CC_C)) return 1;
    switch (kind) {
    case 0: case 3:                                 /* kept across, from the zone's middle */
        c->d[4] = set_w(c->d[4], rd16(ZONE_W)); F_W(c->d[4]);
        c->d[4] = set_w(c->d[4], lsr_n(&c->ccr, c->d[4], 1, 16));
        c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[4], c->d[2]));
        c->d[0] = set_w(c->d[0], c->d[2]); F_W(c->d[2]);
        c->d[1] = 0; F_L(0);
        break;
    case 1:                                         /* kept down */
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(ZONE_H), c->d[1]));
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(c->a[4] - 0x3700), c->d[1]));
        c->d[1] = set_w(c->d[1], neg_w(&c->ccr, c->d[1]));
        c->d[0] = 0; F_L(0);
        break;
    default:
        c->d[0] = 0; c->d[1] = 0; F_L(0);
        break;
    }
    if (kind != 3) { exit_to(); return 0; }
    push32(0xA546); exit_to(); c->a[7] += 4;
    move_b(0, next8(2));                            /* and a word added to the player's $CA02 */
    c->d[0] = set_w(c->d[0], asl_n(&c->ccr, c->d[0], 8, 16));
    move_b(0, next8(2));
    mem_add_w(c->a[4] - 0x35FE, c->d[0]);
    return 0;
}
static int trig_warp(void)           /* $A552: to a start point at once */
{
    put8(0xFFFF9C16u, next8(2));
    put8(0xFFFF9C17u, next8(2));
    put16m(ROOM_CHANGE, 8);
    return 0;
}
static int trig_door(void)           /* $A562: a door the player stands in: up goes to start point $1-$2 */
{
    push32(0xA566); zone_place(); c->a[7] += 4;
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 4, c->d[0]));
    c->d[2] = set_w(c->d[2], rd16(c->a[4] - 0x3800)); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[0], c->d[2]));
    if (c->ccr & CC_C) return 1;
    c->d[0] = set_w(c->d[0], rd16(ZONE_W)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 8, c->d[0]));
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[0], c->d[2]));
    if (!(c->ccr & CC_C)) return 1;
    feet(0);
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if ((c->d[0] & 0xFFFF) != (c->d[1] & 0xFFFF)) return 1;
    put8(0xFFFF9982u, rd8(c->a[2] + 1));
    put8(0xFFFF9983u, rd8(c->a[2] + 2));
    armed(0xA5AA, 0xFFFF998Au, 0xFFFF9980u);
    return 1;
}
static int trig_house(void)          /* $A5BC: a house's door (within 4 across, 8 more for kind 8) */
{
    push32(0xA5C0); zone_place(); c->a[7] += 4;
    cmp_byte(8, rd8(c->a[2] + 1));
    if (rd8(c->a[2] + 1) == 8) c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
    c->d[2] = set_w(c->d[2], c->d[0]); F_W(c->d[0]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[2]));
    if (c->d[2] & 0x8000) c->d[2] = set_w(c->d[2], neg_w(&c->ccr, c->d[2]));
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 4, c->d[2]));
    if (!(c->ccr & CC_C)) return 1;
    feet(2);
    cmp_w(&c->ccr, c->d[2], c->d[1]);
    if ((c->d[1] & 0xFFFF) != (c->d[2] & 0xFFFF)) return 1;
    put16m(0xFFFF9982u, c->d[0]);
    put16m(0xFFFF9984u, c->d[1]);
    put32m(0xFFFF9986u, c->a[2]);
    put32m(0xFFFF998Au, 0xA60A);
    c->a[0] = 0xA60Au;
    wr8(0xFFFF9980u, 0xFF);
    return 1;
}
static int trig_chest(void)          /* $A612: a chest, if its flag ($1) is not set */
{
    F_B(rd8(0xFFFF9A00u));
    if (rd8(0xFFFF9A00u) & 0x80) return 1;
    move_b(0, rd8(c->a[2] + 1));
    call(CHECK_FLAG2);
    if (!(c->ccr & CC_Z)) return 1;
    push32(0xA62A); zone_place(); c->a[7] += 4;
    c->d[2] = set_w(c->d[2], c->d[0]); F_W(c->d[0]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[2]));
    if (c->d[2] & 0x8000) c->d[2] = set_w(c->d[2], neg_w(&c->ccr, c->d[2]));
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 8, c->d[2]));
    if (!(c->ccr & CC_C)) return 1;
    feet(2);
    cmp_w(&c->ccr, c->d[2], c->d[1]);
    if ((c->d[1] & 0xFFFF) != (c->d[2] & 0xFFFF)) return 1;
    put16m(0xFFFF9982u, c->d[0]);
    put16m(0xFFFF9984u, c->d[1]);
    put32m(0xFFFF9986u, c->a[2]);
    armed(0xA68E, 0xFFFF998Au, 0xFFFF9980u);
    return 1;
}
static int trig_entrance(void)       /* $A79A: a start point's entrance (d6 the point, d7 a word) */
{
    c->d[6] = set_b(c->d[6], rd8(c->a[2]));
    c->d[6] = set_w(c->d[6], lsl_n(&c->ccr, c->d[6], 8, 16));
    c->d[6] = set_b(c->d[6], rd8(c->a[2] + 1));
    c->d[7] = set_b(c->d[7], rd8(c->a[2] + 2));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 8, 16));
    c->d[7] = set_b(c->d[7], rd8(c->a[2] + 3)); F_B(c->d[7]);
    c->a[0] = rd32(DATA_DIR);
    c->d[0] = set_w(c->d[0], c->d[6]); F_W(c->d[6]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    c->d[0] = 0;
    move_b(0, rd8(c->a[0] + 5));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 3, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    feet(1);
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if ((c->d[0] & 0xFFFF) != (c->d[1] & 0xFFFF)) return 1;
    c->d[1] = 0;
    move_b(1, rd8(c->a[0] + 4));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 3, 16));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, 0x1000, c->d[1]));
    c->d[0] = 0;
    move_b(0, rd8(c->a[0] + 2));
    switch (c->d[0] & 0xFFFF) {                     /* jmp tbl(pc,d0.w): the point's kind */
    case 4: case 0xE: case 0x10: case 0x12: goto by_x;
    case 8:                                         /* a shop's: not while in one */
        f_bit(rd8(0xFFFF9EF1u) >> 4 & 1);
        if (rd8(0xFFFF9EF1u) & 0x10) goto by_x;
        return 1;
    case 0xA:                                       /* to the left, facing it */
        f_bit(rd8(0xFFFF9F0Bu) >> 2 & 1);
        if (!(rd8(0xFFFF9F0Bu) & 4)) return 1;
        f_bit(rd8(0xFFFF9F02u) >> 6 & 1);
        if (!(rd8(0xFFFF9F02u) & 0x40)) return 1;
        c->d[0] = 0;
        move_b(0, rd8(c->a[4] - 0x34FE));
        c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[0]));
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 8, c->d[0]));
        cmp_w(&c->ccr, c->d[1], c->d[0]);
        if ((c->d[0] & 0xFFFF) != (c->d[1] & 0xFFFF)) return 1;
        put16m(ROOM_CHANGE, 6);
        goto keep;
    case 0xC:                                       /* to the right */
        f_bit(rd8(0xFFFF9F0Bu) >> 3 & 1);
        if (!(rd8(0xFFFF9F0Bu) & 8)) return 1;
        f_bit(rd8(0xFFFF9F02u) >> 6 & 1);
        if (!(rd8(0xFFFF9F02u) & 0x40)) return 1;
        c->d[0] = 0;
        move_b(0, rd8(c->a[4] - 0x34FE));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[0]));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
        cmp_w(&c->ccr, c->d[1], c->d[0]);
        if ((c->d[0] & 0xFFFF) != (c->d[1] & 0xFFFF)) return 1;
        put16m(ROOM_CHANGE, 7);
        goto keep;
    case 0x14:                                      /* by x, its own routine */
        push32(0xA804); near_x(); c->a[7] += 4;
        if (!(c->ccr & CC_C)) return 1;
        put32m(0xFFFF998Au, 0xA8A6);
        goto arm;
    case 0: case 2: case 6: return 1;
    default: call(0xA7EC + (c->d[0] & 0xFFFF)); return 1;   /* past the table, as the original */
    }
by_x:
    push32(0xA872); near_x(); c->a[7] += 4;
    if (!(c->ccr & CC_C)) return 1;
    put32m(0xFFFF998Au, 0xA898);
arm:
    wr8(0xFFFF9980u, 0xFF);
keep:
    put16m(0xFFFF9982u, c->d[6]);
    put16m(0xFFFF9984u, c->d[7]);
    return 1;
}
static int trig_event_a(void)        /* $A8B4: an event ($A990 with its word): up runs it */
{
    c->d[7] = set_b(c->d[7], rd8(c->a[2]));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 8, 16));
    c->d[7] = set_b(c->d[7], rd8(c->a[2] + 1));
    call(0xA990);
    if (!(c->ccr & CC_Z)) return 1;
    put16m(0xFFFF9982u, c->d[7]);
    armed(0xA8D8, 0xFFFF998Au, 0xFFFF9980u);
    return 1;
}
static int trig_event_b(void)        /* $A8E6: the event in hand ($9964), the second routine */
{
    c->d[7] = set_w(c->d[7], rd16(0xFFFF9964u)); F_W(c->d[7]);
    call(0xA990);
    if (!(c->ccr & CC_Z)) return 1;
    armed(0xA902, 0xFFFF998Eu, 0xFFFF9981u);
    return 1;
}
static int trig_sound(void)          /* $A90A */
{
    move_b(0, rd8(c->a[2]));
    call(QUEUE_SOUND);
    return 1;
}

static int trigger(uint32_t at)
{
    switch (at) {
    case 0xA4EE: return trig_exit(0);
    case 0xA506: return trig_exit(1);
    case 0xA51E: return trig_exit(2);
    case 0xA52E: return trig_exit(3);
    case 0xA552: return trig_warp();
    case 0xA562: return trig_door();
    case 0xA5BC: return trig_house();
    case 0xA612: return trig_chest();
    case 0xA46E: return 1;
    case 0xA79A: return trig_entrance();
    case 0xA8B4: return trig_event_a();
    case 0xA8E6: return trig_event_b();
    case 0xA90A: return trig_sound();
    default: call(at); return 0;                    /* past the table: as the original jumps */
    }
}

/* $A3D8: the room's triggers, each frame (not while $9659 bit 6); the armed routines are
 * dropped unless re-armed this frame ($9966) */
static void triggers(void)
{
    f_bit(rd8(0xFFFF9659u) >> 6 & 1);
    if (rd8(0xFFFF9659u) & 0x40) return;
    uint8_t v = rd8(0xFFFF9966u); f_bit(v & 1); wr8(0xFFFF9966u, v & ~1);
    if (!(v & 1)) { wr8(0xFFFF9980u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); }
    v = rd8(0xFFFF9966u); f_bit(v >> 1 & 1); wr8(0xFFFF9966u, v & ~2);
    if (!(v & 2)) { wr8(0xFFFF9981u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); }
    c->d[0] = rd32(TRIGGERS); F_L(c->d[0]);
    if (!c->d[0]) return;
    c->a[2] = c->d[0];
    put16m(ZONE_X, 0x1000);
    put16m(ZONE_Y, 0x1000);
    for (;;) {
        c->d[0] = 16; F_L(16);                      /* $A40E */
        put16m(ZONE_W, 16);
        put16m(ZONE_H, 16);
        for (;;) {                                  /* $A418 */
            c->d[0] = 0;
            move_b(0, next8(2));
            cmp_byte(0xC0, c->d[0]);
            if ((c->d[0] & 0xFF) < 0xC0) break;
            cmp_byte(0xD0, c->d[0]);
            if ((c->d[0] & 0xFF) < 0xD0) {
                and_w(0, 0xF);
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0]));
                c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
                put16m(ZONE_W, c->d[0]);
                continue;
            }
            cmp_byte(0xE0, c->d[0]);
            if ((c->d[0] & 0xFF) < 0xE0) {
                and_w(0, 0xF);
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0]));
                c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
                put16m(ZONE_H, c->d[0]);
                continue;
            }
            and_w(0, 0x1F);                         /* an event */
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
            c->a[0] = 0xA490u + sx(rd16(0xA490u + sx(c->d[0])));
            push32(0xA45A);                         /* jsr (a0) */
            if (c->a[0] == 0xABB8u) {               /* $1F, the end: its return popped, out of $A3D8 */
                c->a[7] += 4;
                c->d[0] = 0; F_L(0);
                return;
            }
            if (c->a[0] != 0xA47Eu) call(c->a[0]);
            c->a[7] += 4;
            F_W(rd16(ROOM_CHANGE));
            if (rd16(ROOM_CHANGE)) return;
        }
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));   /* $A462: a trigger */
        c->a[0] = 0xA4D0u + sx(rd16(0xA4D0u + sx(c->d[0])));
        if (!trigger(c->a[0])) return;
        c->d[0] = 0;                                /* $A46E: past it, by its length ($A480) */
        move_b(0, rd8(c->a[2] - 1));
        move_b(0, rd8(0xA480u + sx(c->d[0])));
        c->a[2] += sx(c->d[0]);
    }
}

/* the routines the triggers arm, run by the game when the player presses up */
static void up_warp(void)            /* $A5AA: a door to start point $9982 */
{
    put16m(ROOM_CHANGE, 8);
    put16m(0xFFFF9C16u, rd16(0xFFFF9982u));
    call(0xDF80);
}
static void up_house(void) { put16m(ROOM_CHANGE, 5); }   /* $A60A */
/* $A666: a chest opening at (d6, d7): thing d0's picture ($A6CA), sound 44 */
static void chest_open(void)
{
    put16m(0xFFFF9A00u, 0x8000);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] = 0xA6CAu + sx(rd16(0xA6CAu + sx(c->d[0])));
    put32m(0xFFFF9A02u, c->a[0]);
    put16m(0xFFFF9A06u, c->d[6]);
    c->d[7] = set_w(c->d[7], sub_w(&c->ccr, 0x10, c->d[7]));
    put16m(0xFFFF9A08u, c->d[7]);
    c->d[0] = 44; F_L(44);
    call(QUEUE_SOUND);
}
static void up_chest(void)           /* $A68E: the chest's flag set, opened, its two cells changed */
{
    c->a[2] = rd32(0xFFFF9986u);
    move_b(0, rd8(c->a[2] + 1));
    push32(0xA69A); flag2_set(); c->a[7] += 4;
    c->d[0] = 0;
    move_b(0, rd8(c->a[2] + 2));
    c->d[6] = set_w(c->d[6], rd16(0xFFFF9982u)); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(0xFFFF9984u)); F_W(c->d[7]);
    push32(0xA6AA); chest_open(); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, c->d[6], 4, 16));
    c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    c->d[7] = set_w(c->d[7], sub_w(&c->ccr, 0x10, c->d[7]));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 2, 16));
    c->a[3] = LAYER_A;
    c->d[0] = set_w(c->d[0], 0x164); F_W(0x164);
    push32(0xA6C0); cell_set_draw(); c->a[7] += 4;
    c->d[0] = set_w(c->d[0], 0x165); F_W(0x165);
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    cell_set_draw();
}
static void up_entrance(void)        /* $A898 */
{
    put16m(0xFFFF9676u, rd16(0xFFFF9984u));
    put16m(ROOM_CHANGE, 2);
}
static void up_entrance2(void)       /* $A8A6 */
{
    put16m(0xFFFF9676u, rd16(0xFFFF9984u));
    put16m(ROOM_CHANGE, 0xC);
}
static void up_event_a(void)         /* $A8D8 */
{
    put16m(0xFFFF9964u, rd16(0xFFFF9982u));
    put16m(ROOM_CHANGE, 3);
}
static void up_event_b(void) { put16m(ROOM_CHANGE, 4); }   /* $A902 */
static void zone_place_e(void) { zone_place(); }
static void in_zone_e(void) { in_zone(); }
static void near_x_e(void) { near_x(); }

/* the triggers' handlers, entered by jmp (a0) from $A3D8: one that goes on carries on through
 * the list at $A46E, as the original's bra does */
#define TRIGGER(name, call_) void name(Cpu *cpu) { c = cpu; if (call_) port_continue_at(c, 0xA46E, 0); else c->ret = 0; }
TRIGGER(TrigExitX, trig_exit(0))           /* $A4EE */
TRIGGER(TrigExitY, trig_exit(1))           /* $A506 */
TRIGGER(TrigExit, trig_exit(2))            /* $A51E */
TRIGGER(TrigExitAdd, trig_exit(3))         /* $A52E */
TRIGGER(TrigWarp, trig_warp())             /* $A552 */
TRIGGER(TrigDoor, trig_door())             /* $A562 */
TRIGGER(TrigHouse, trig_house())           /* $A5BC */
TRIGGER(TrigChest, trig_chest())           /* $A612 */
TRIGGER(TrigEntrance, trig_entrance())     /* $A79A */
TRIGGER(TrigEventA, trig_event_a())        /* $A8B4 */
TRIGGER(TrigEventB, trig_event_b())        /* $A8E6 */
TRIGGER(TrigSound, trig_sound())           /* $A90A */
ENTRY(Triggers, triggers)                  /* $A3D8 */
ENTRY(ZonePlace, zone_place_e)             /* $A914 */
ENTRY(InZone, in_zone_e)                   /* $A92C */
ENTRY(ExitTo, exit_to)                     /* $A962 */
ENTRY(NearX, near_x_e)                     /* $A88C */
ENTRY(UpWarp, up_warp)                     /* $A5AA */
ENTRY(UpHouse, up_house)                   /* $A60A */
ENTRY(ChestOpen, chest_open)               /* $A666 */
ENTRY(UpChest, up_chest)                   /* $A68E */
ENTRY(UpEntrance, up_entrance)             /* $A898 */
ENTRY(UpEntrance2, up_entrance2)           /* $A8A6 */
ENTRY(UpEventA, up_event_a)                /* $A8D8 */
ENTRY(UpEventB, up_event_b)                /* $A902 */

/* ---- batch T17: the room changes' helpers ----
 * Going through a door or into a house draws a door's frame (entity 8) over the doorway while
 * the screen changes: its place is kept at $9BC6/$9BC8 (pixels) with its size in 8s at
 * $9BCA/$9BCC, and the four blocks under it at $9BEA-$9BF0. An event's picture (a shop's or a
 * house's inside, from the directory's table 2 by $9964) is a rectangle of 10-bit cells packed
 * four to five bytes ($997C), swapped onto plane A over what was there ($A1FE keeps it). */

#define INIT_ENTITY   0x0A1Au
#define SET_ANIM      0x07E8u
#define DOOR          0xFFFF9BC6u   /* words: x, y, width and height (in 8s) of the door's frame */
#define PICTURE       0xFFFF997Cu   /* long: the event's packed cells */

/* $B8D4: whether the block at (d6, d7) is a cracked wall (blocks $49-$4B, or $2B4 once broken;
 * d0 -1, and a cracked one becomes $2B4), inside the room's columns */
static void cracked_at(void)
{
    c->d[1] = set_w(c->d[1], 0x100); F_W(0x100);
    cmp_w(&c->ccr, c->d[1], c->d[6]);
    if ((c->d[6] & 0xFFFF) < 0x100) goto no;
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9798u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
    cmp_w(&c->ccr, c->d[0], c->d[6]);
    if ((c->d[6] & 0xFFFF) >= (c->d[0] & 0xFFFF)) goto no;
    call(MAP_BLOCK_AT);
    cmp_w(&c->ccr, 0x2B4, c->d[0]);
    if ((c->d[0] & 0xFFFF) == 0x2B4) goto yes;
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x49, c->d[0]));
    if (c->ccr & CC_C) goto no;
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 2, c->d[0]));
    if (!(c->ccr & (CC_C | CC_Z))) goto no;
    c->d[0] = set_w(c->d[0], 0x2B4); F_W(0x2B4);
    push32(0xB906); cell_set_draw_if_shown(); c->a[7] += 4;
yes:
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
    return;
no:
    c->d[0] = 0; F_L(0);
}

/* $B90A: the continuations' stack ($9C14) emptied */
static void reset_continuations(void) { put16m(0xFFFF9C14u, 0x9BF4); }

/* $B912: the door's frame (entity 8, picture $400 | d0) at the door's place, its middle */
static void door_frame(void)
{
    c->a[4] = 8;
    c->d[1] = set_w(c->d[1], 0x400);
    c->d[1] = set_b(c->d[1], c->d[0]);
    put16m(c->a[4] - 0x3D00, c->d[1]);
    call(INIT_ENTITY);
    c->d[0] = set_w(c->d[0], rd16(DOOR + 4)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 3, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(DOOR), c->d[0]));
    put16m(c->a[4] - 0x3800, c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(DOOR + 6)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 3, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(DOOR + 2), c->d[0]));
    put16m(c->a[4] - 0x3700, c->d[0]);
    c->d[0] = 0; F_L(0);
    call(SET_ANIM);
}

/* $B94A, $B946: the picture's frame (entity 8, picture $400 | $9BF2, or 2) round the event's
 * rectangle ($9BDA/$9BDC, its size in 16s at $9BE2/$9BE4) */
static void picture_frame_with(void)
{
    c->a[4] = 8;
    c->d[1] = set_w(c->d[1], 0x400);
    c->d[1] = set_b(c->d[1], c->d[0]);
    put16m(c->a[4] - 0x3D00, c->d[1]);
    call(INIT_ENTITY);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9BE2u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0]));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF9BDAu), c->d[0]));
    put16m(c->a[4] - 0x3800, c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9BE4u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0]));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF9BDCu), c->d[0]));
    put16m(c->a[4] - 0x3700, c->d[0]);
    c->d[0] = 0; F_L(0);
    call(SET_ANIM);
}
static void picture_frame(void) { move_b(0, rd8(0xFFFF9BF2u)); picture_frame_with(); }   /* $B94A */
static void picture_frame_2(void) { c->d[0] = 2; F_L(2); picture_frame_with(); }       /* $B946 */

/* $B986: event $9964's picture set up from its record (the directory's table 2, 16 bytes each):
 * its place and size, the frame's picture, its triggers (the room's kept at $9970), its things'
 * list ($A11E), the cells' rectangle; then what is under it saved ($A1FE) */
static void picture_setup(void)
{
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9964u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    uint16_t m = rd16(0xFFFF9994u) & 0xBF7F; wr16(0xFFFF9994u, m); F_W(m);
    c->a[0] = rd32(DATA_DIR + 8);
    uint32_t r = c->a[0] + sx(c->d[0]);
    c->d[4] = set_w(c->d[4], rd16(r)); F_W(c->d[4]);
    put16m(0xFFFF9BDAu, c->d[4]);
    c->d[1] = set_w(c->d[1], c->d[4]); F_W(c->d[4]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 16));
    put16m(RECT, c->d[1]);
    c->d[5] = set_w(c->d[5], rd16(r + 2)); F_W(c->d[5]);
    put16m(0xFFFF9BDCu, c->d[5]);
    c->d[1] = set_w(c->d[1], c->d[5]); F_W(c->d[5]);
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 2, 16));
    put16m(RECT + 2, c->d[1]);
    c->d[1] = 0;
    move_b(1, rd8(r + 4));
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, c->d[1], c->d[4]));
    put16m(0xFFFF9BCEu, c->d[4]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 16));
    put16m(0xFFFF9BE2u, c->d[1]);
    c->d[1] = 0;
    move_b(1, rd8(r + 5));
    c->d[5] = set_w(c->d[5], add_w(&c->ccr, c->d[1], c->d[5]));
    put16m(0xFFFF9BD0u, c->d[5]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 16));
    put16m(0xFFFF9BE4u, c->d[1]);
    put8(0xFFFF9BF2u, rd8(r + 6));
    c->d[1] = 2; F_L(2);
    F_B(rd8(r + 7));
    if (rd8(r + 7)) { c->d[1] = 1; F_L(1); }
    put16m(0xFFFF9BD2u, c->d[1]);
    put16m(0xFFFF9BD4u, c->d[1]);
    c->d[1] = set_w(c->d[1], rd16(r + 8)); F_W(c->d[1]);
    c->d[1] = (uint32_t)(int32_t)(int16_t)c->d[1]; F_L(c->d[1]);
    if (c->d[1]) c->d[1] = add_l(&c->ccr, c->a[0], c->d[1]);
    put32m(0xFFFFA11Eu, c->d[1]);
    put32m(0xFFFF9970u, rd32(TRIGGERS));
    c->a[1] = c->a[0];
    c->a[1] += sx(rd16(r + 0xC));
    put32m(TRIGGERS, c->a[1]);
    c->a[0] += sx(rd16(r + 0xE));
    c->d[0] = 0;
    move_b(0, next8(0));
    put16m(0xFFFF9BDEu, c->d[0]);
    put16m(RECT + 4, c->d[0]);
    move_b(0, next8(0));
    put16m(0xFFFF9BE0u, c->d[0]);
    put16m(RECT + 6, c->d[0]);
    put32m(PICTURE, c->a[0]);
    rect_save();
}

/* $BA92: d6, d7 the door's cell; a3 plane A */
static void door_cell(void)
{
    c->d[6] = set_w(c->d[6], rd16(DOOR)); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(DOOR + 2)); F_W(c->d[7]);
    c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, c->d[6], 4, 16));
    and_w(7, 0xFFF0);
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 2, 16));
    c->a[3] = LAYER_A;
}

/* $BA34: the door's blocks (1 or 2 across and down) kept at $9BEA-$9BF0 and blanked (block 1);
 * $BAA8 puts them back */
static int door_wide(void) { cmp_w(&c->ccr, 1, rd16(DOOR + 4)); return rd16(DOOR + 4) != 1; }
static int door_tall(void) { cmp_w(&c->ccr, 1, rd16(DOOR + 6)); return rd16(DOOR + 6) != 1; }
static void blank_one(uint32_t keep)
{
    call(MAP_BLOCK_AT);
    put16m(keep, c->d[0]);
    c->d[0] = 1; F_L(1);
}
static void door_blank(void)
{
    push32(0xBA36); door_cell(); c->a[7] += 4;
    blank_one(0xFFFF9BEAu);
    push32(0xBA44); cell_set_draw_if_shown(); c->a[7] += 4;
    if (door_wide()) {
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
        blank_one(0xFFFF9BECu);
        push32(0xBA5C); cell_set_draw_if_shown(); c->a[7] += 4;
        c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    }
    if (!door_tall()) return;
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    blank_one(0xFFFF9BEEu);
    push32(0xBA78); cell_set_draw_if_shown(); c->a[7] += 4;
    if (!door_wide()) return;
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    blank_one(0xFFFF9BF0u);
    cell_set_draw_if_shown();
}
static void put_one(uint32_t keep) { c->d[0] = set_w(c->d[0], rd16(keep)); F_W(c->d[0]); }
static void door_restore(void)
{
    push32(0xBAAA); door_cell(); c->a[7] += 4;
    put_one(0xFFFF9BEAu);
    push32(0xBAB2); cell_set_draw_if_shown(); c->a[7] += 4;
    if (door_wide()) {
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
        put_one(0xFFFF9BECu);
        push32(0xBAC4); cell_set_draw_if_shown(); c->a[7] += 4;
        c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    }
    if (!door_tall()) return;
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    put_one(0xFFFF9BEEu);
    push32(0xBADA); cell_set_draw_if_shown(); c->a[7] += 4;
    if (!door_wide()) return;
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    put_one(0xFFFF9BF0u);
    cell_set_draw_if_shown();
}

/* $BB2C: cell (d6, d7) of the picture: 10 bits from its packed bytes ($997C, $9BDE across) */
static void picture_cell(void)
{
    c->a[1] = rd32(PICTURE);
    c->d[0] = set_w(c->d[0], c->d[7]); F_W(c->d[7]);
    c->d[1] = set_w(c->d[1], rd16(0xFFFF9BDEu)); F_W(c->d[1]);
    c->d[1] = (c->d[1] & 0xFFFF) * (c->d[0] & 0xFFFF); F_L(c->d[1]);   /* mulu.w */
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[6], c->d[1]));
    c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]);
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 2, 16));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[0], c->d[1]));
    c->d[0] = 3;
    and_w(0, c->d[1]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 16));
    F_W(c->d[0]);
    uint32_t a = c->a[1] + sx(c->d[1]);
    if (!(c->d[0] & 0xFFFF)) {
        move_b(0, rd8(a));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
        move_b(1, rd8(a + 1));
        c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 6, 8));
    } else {
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 2, c->d[0]));
        if (c->ccr & CC_C) {
            c->d[0] = 63;
            and_b(0, rd8(a));
            c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
            move_b(1, rd8(a + 1));
            c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 8));
        } else if (!(c->ccr & CC_Z)) {
            c->d[0] = 3;
            and_b(0, rd8(a));
            c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
            move_b(0, rd8(a + 1));
            return;
        } else {
            c->d[0] = 15;
            and_b(0, rd8(a));
            c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 6, 16));
            move_b(1, rd8(a + 1));
            c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 8));
        }
    }
    or_b(0, c->d[1]);
}

/* $BB8E: cell (d6, d7) of the picture swapped in on plane A (at the rectangle's place, $974C) */
static void picture_cell_put(void)
{
    c->d[0] = set_w(c->d[0], c->d[6]); F_W(c->d[6]);
    c->d[6] = swap_l(c->d[6]);
    c->d[6] = set_w(c->d[6], c->d[0]); F_W(c->d[0]);
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, rd16(RECT), c->d[6]));
    c->d[0] = set_w(c->d[0], c->d[7]); F_W(c->d[7]);
    c->d[7] = swap_l(c->d[7]);
    c->d[7] = set_w(c->d[7], c->d[0]); F_W(c->d[0]);
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 6, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, rd16(RECT + 2), c->d[7]));
    push32(0xBBA8); cell_at(); c->a[7] += 4;
    c->d[6] = swap_l(c->d[6]);
    c->d[7] = swap_l(c->d[7]);
    push32(0xBBB0); picture_cell(); c->a[7] += 4;
    put16m(c->a[0], c->d[0]);
}

/* $BAEE: the picture's frame's corner cells (1 or 2 across and down, from $9BE2/$9BE4) */
static void picture_corner(void)
{
    c->a[3] = LAYER_A;
    c->d[6] = set_w(c->d[6], rd16(0xFFFF9BE2u)); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(0xFFFF9BE4u)); F_W(c->d[7]);
    push32(0xBAFE); picture_cell_put(); c->a[7] += 4;
    cmp_w(&c->ccr, 1, rd16(0xFFFF9BD2u));
    if (rd16(0xFFFF9BD2u) != 1) {
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
        push32(0xBB0C); picture_cell_put(); c->a[7] += 4;
        c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    }
    cmp_w(&c->ccr, 1, rd16(0xFFFF9BD4u));
    if (rd16(0xFFFF9BD4u) == 1) return;
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
    push32(0xBB1C); picture_cell_put(); c->a[7] += 4;
    cmp_w(&c->ccr, 1, rd16(0xFFFF9BD2u));
    if (rd16(0xFFFF9BD2u) == 1) return;
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    picture_cell_put();
}

/* $BBB4: the whole picture swapped in, row by row */
static void picture_put(void)
{
    link(-4);
    c->a[3] = LAYER_A;
    c->a[1] = rd32(PICTURE);
    c->d[7] = 0;
    put16m(c->a[6] - 4, rd16(RECT + 6));
    do {
        c->d[6] = 0;
        put16m(c->a[6] - 2, rd16(RECT + 4));
        do {
            push32(0xBBD4); picture_cell(); c->a[7] += 4;
            push32(0xBBD6); picture_cell_put(); c->a[7] += 4;
            c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
        } while (mem_sub_w(c->a[6] - 2, 1));
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 1, c->d[7]));
    } while (mem_sub_w(c->a[6] - 4, 1));
    unlk();
}

ENTRY(CrackedAt, cracked_at)               /* $B8D4 */
ENTRY(ResetContinuations, reset_continuations)   /* $B90A */
ENTRY(DoorFrame, door_frame)               /* $B912 */
ENTRY(PictureFrame2, picture_frame_2)      /* $B946 */
ENTRY(PictureFrame, picture_frame)         /* $B94A */
ENTRY(PictureSetup, picture_setup)         /* $B986 */
ENTRY(DoorBlank, door_blank)               /* $BA34 */
ENTRY(DoorCell, door_cell)                 /* $BA92 */
ENTRY(DoorRestore, door_restore)           /* $BAA8 */
ENTRY(PictureCorner, picture_corner)       /* $BAEE */
ENTRY(PictureCell, picture_cell)           /* $BB2C */
ENTRY(PictureCellPut, picture_cell_put)    /* $BB8E */
ENTRY(PicturePut, picture_put)             /* $BBB4 */

/* ---- batch T18: the picture growing, the door's animation, leaving a room ----
 * An event's picture opens from its middle: each step ($BBEA, by $9BE6) redraws the edges of a
 * rectangle a cell larger each way, with $9BE8 or'ed into its blocks (bit 15 in front, while it
 * opens; 0 to take it away). The door's frame then plays its opening or closing animation
 * ($BD0A, $BD52), waiting for it a frame at a time; those two keep their caller's return address
 * on the game's continuation stack ($9C14) while they wait, as the ocarina's tune does. */

#define CONTINUATIONS 0xFFFF9C14u   /* word: the continuations' stack pointer (from $9BF4) */
#define RESET_ANIM    0x07F4u       /* ResetAnimTimer */

/* $BCAC: a row (d7) of the rectangle's cells redrawn from column -2(a6) to -4(a6), each block
 * with $9BE8; $BCDA a column (d6) from row -6(a6) to -8(a6). Their counts are in the caller's
 * frame (a6 - 10) */
static void grow_row(void)
{
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 6, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, rd16(RECT + 2), c->d[7]));
    c->d[0] = set_w(c->d[0], rd16(c->a[6] - 4)); F_W(c->d[0]);
    c->d[6] = set_w(c->d[6], rd16(c->a[6] - 2)); F_W(c->d[6]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[6], c->d[0]));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, rd16(RECT), c->d[6]));
    put16m(c->a[6] - 10, c->d[0]);
    do {
        call(MAP_BLOCK_AT);
        or_w(0, rd16(0xFFFF9BE8u));
        push32(0xBCD0); draw_if_shown(); c->a[7] += 4;
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    } while (!(mem_sub_w(c->a[6] - 10, 1) & 0x8000));
}
static void grow_column(void)
{
    c->d[0] = set_w(c->d[0], rd16(c->a[6] - 8)); F_W(c->d[0]);
    c->d[7] = set_w(c->d[7], rd16(c->a[6] - 6)); F_W(c->d[7]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[7], c->d[0]));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 6, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, rd16(RECT + 2), c->d[7]));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, rd16(RECT), c->d[6]));
    put16m(c->a[6] - 10, c->d[0]);
    do {
        call(MAP_BLOCK_AT);
        or_w(0, rd16(0xFFFF9BE8u));
        push32(0xBCFE); draw_if_shown(); c->a[7] += 4;
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    } while (!(mem_sub_w(c->a[6] - 10, 1) & 0x8000));
}

/* $BBEA: step $9BE6 of the picture growing: its edges (those still inside the rectangle: bits
 * 0-3 of d0 for top, bottom, left, right) redrawn */
static void picture_grow(void)
{
    link(-12);
    c->a[3] = LAYER_A;
    put8(c->a[6] - 12, 0xF);
    uint32_t step = 0xFFFF9BE6u;
    /* left */
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9BE2u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(step), c->d[0]));
    if (c->d[0] & 0x8000) { uint8_t v = rd8(c->a[6] - 12) & 0xB; wr8(c->a[6] - 12, v); F_B(v); c->d[0] = 0; F_L(0); }
    put16m(c->a[6] - 2, c->d[0]);
    /* right */
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9BE2u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF9BD2u), c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(step), c->d[0]));
    c->d[1] = set_w(c->d[1], rd16(0xFFFF9BDEu)); F_W(c->d[1]);
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if ((c->d[0] & 0xFFFF) >= (c->d[1] & 0xFFFF)) {
        uint8_t v = rd8(c->a[6] - 12) & 7; wr8(c->a[6] - 12, v); F_B(v);
        c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    }
    put16m(c->a[6] - 4, c->d[0]);
    /* top */
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9BE4u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(step), c->d[0]));
    if (c->d[0] & 0x8000) { uint8_t v = rd8(c->a[6] - 12) & 0xE; wr8(c->a[6] - 12, v); F_B(v); c->d[0] = 0; F_L(0); }
    put16m(c->a[6] - 6, c->d[0]);
    /* bottom */
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9BE4u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF9BD4u), c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(step), c->d[0]));
    c->d[1] = set_w(c->d[1], rd16(0xFFFF9BE0u)); F_W(c->d[1]);
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if ((c->d[0] & 0xFFFF) >= (c->d[1] & 0xFFFF)) {
        uint8_t v = rd8(c->a[6] - 12) & 0xD; wr8(c->a[6] - 12, v); F_B(v);
        c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    }
    put16m(c->a[6] - 8, c->d[0]);
    f_bit(rd8(c->a[6] - 12) & 1);
    if (rd8(c->a[6] - 12) & 1) { c->d[7] = set_w(c->d[7], rd16(c->a[6] - 6)); F_W(c->d[7]); push32(0xBC7A); grow_row(); c->a[7] += 4; }
    f_bit(rd8(c->a[6] - 12) >> 1 & 1);
    if (rd8(c->a[6] - 12) & 2) { c->d[7] = set_w(c->d[7], rd16(c->a[6] - 8)); F_W(c->d[7]); push32(0xBC88); grow_row(); c->a[7] += 4; }
    f_bit(rd8(c->a[6] - 12) >> 2 & 1);
    if (rd8(c->a[6] - 12) & 4) { c->d[6] = set_w(c->d[6], rd16(c->a[6] - 2)); F_W(c->d[6]); push32(0xBC96); grow_column(); c->a[7] += 4; }
    f_bit(rd8(c->a[6] - 12) >> 3 & 1);
    if (rd8(c->a[6] - 12) & 8) { c->d[6] = set_w(c->d[6], rd16(c->a[6] - 4)); F_W(c->d[6]); push32(0xBCA4); grow_column(); c->a[7] += 4; }
    move_b(0, rd8(c->a[6] - 12));
    unlk();
}

/* $BD0A, $BD52: the door's frame plays its animation (8 opening, $16 closing), with sounds 35 and
 * 34; it waits a frame at a time, first while the frame is still moving ($C600), then for the
 * animation's end ($C608). The caller's return address (ret) waits on the continuations' stack,
 * as in the original, and is popped (a0) at the end */
static void wait_frame(uint32_t resume) { Cpu *me = c; task_wait_frame(me, resume); c = me; }
static void door_animation(uint32_t ret, int closing)
{
    c->a[0] = sx(rd16(CONTINUATIONS));
    wr32(c->a[0], ret); c->a[0] += 4; F_L(ret);
    put16m(CONTINUATIONS, c->a[0]);
    c->d[0] = 35; F_L(35);
    call(QUEUE_SOUND);
    uint32_t base = closing ? 0xBD66 : 0xBD1E;
    for (;;) {
        wait_frame(base);
        c->a[4] = 8;
        F_B(rd8(c->a[4] - 0x3A00));
        if (!rd8(c->a[4] - 0x3A00)) break;
    }
    put8(c->a[4] - 0x3CFD, closing ? 0x16 : 8);
    call(RESET_ANIM);
    for (;;) {
        wait_frame(base + 0x1A);
        F_B(rd8(0xFFFFC608u));
        if (!rd8(0xFFFFC608u)) break;
    }
    c->d[0] = 34; F_L(34);
    call(QUEUE_SOUND);
    c->a[1] = sx(rd16(CONTINUATIONS));
    c->a[1] -= 4;
    c->a[0] = rd32(c->a[1]);
    put16m(CONTINUATIONS, c->a[1]);
}
/* from the original's code: the return address taken off the stack, the animation, then on at
 * the popped address (jmp (a0)) */
static void door_animation_68k(int closing)
{
    uint32_t ret = rd32(c->a[7]); c->a[7] += 4;
    door_animation(ret, closing);
    port_continue_at(c, c->a[0], 0);
}

/* $BD9A: leaving a room: the things, effects and items stopped ($966A 3) */
static void room_leave(void)
{
    put8(0xFFFF966Au, 3);
    call(0xF99C);
    call(0x13786);
    call(0x11BA4);
    call(0x1115E);
}

ENTRY(PictureGrow, picture_grow)           /* $BBEA */
ENTRY(GrowRow, grow_row)                   /* $BCAC */
ENTRY(GrowColumn, grow_column)             /* $BCDA */
ENTRY(RoomLeave, room_leave)               /* $BD9A */
/* the two that wait leave by port_continue_at, so ret is theirs to set */
/* a routine that waits for frames runs only on a task pass; reached outside one (a pass the harness
 * finished on the 68000), it goes on in the original at its own address (port/task.h, CT_WAITS) */
#define NEEDS_PASS(self) do { if (!task_in_pass()) { port_leave_to_68k(c, (self)); return; } } while (0)
void DoorOpen(Cpu *cpu)  { c = cpu; NEEDS_PASS(0xBD0A); door_animation_68k(0); }   /* $BD0A */
void DoorClose(Cpu *cpu) { c = cpu; NEEDS_PASS(0xBD52); door_animation_68k(1); }   /* $BD52 */

/* ---- batch T19: the room changes' sequences: doors, warps, shops and houses ----
 * When a trigger's routine sets $9962, the game's task jumps (from its table at $91F4) to one of
 * these. They run on the task's coroutine, wait for frames at the original's resume addresses,
 * and leave as the original does: back to the game's loop ($8FFC), to the next room ($9100), or
 * to the exits' code ($F76C). $BDB0, $BDC0, $BE50 and $BF80 are not yet registered: no session
 * runs them yet. */

#define GAME_LOOP     0x8FFC
#define NEXT_ROOM     0x9100
#define EXIT_ROOM     0xF76C
#define PLAYER_ENT    0xFFFF9EEEu
#define DOOR_KEEP     0xDF80u       /* the player and camera kept still */
#define DOOR_FREE     0xDF90u
#define SPAWN_ENEMIES 0x12848u
#define APPLY_VELOCITY 0x0AB4u

static void leave_to(uint32_t addr) { port_continue_at(c, addr, 0); }
static void player_a4(void) { c->a[4] = sx(rd16(PLAYER_ENT)); }

/* $C128, $C152: the frame's animation waited for (a sound first; $C152 restarts it), the
 * caller's return address on the continuations' stack meanwhile */
static void frame_wait(uint32_t ret, int restart)
{
    c->a[0] = sx(rd16(CONTINUATIONS));
    wr32(c->a[0], ret); c->a[0] += 4; F_L(ret);
    put16m(CONTINUATIONS, c->a[0]);
    c->d[0] = restart ? 34 : 35; F_L(c->d[0]);
    call(QUEUE_SOUND);
    if (restart) { c->a[4] = 8; call(RESET_ANIM); }
    do {
        wait_frame(restart ? 0xC16E : 0xC13C);
        c->a[4] = 8;
        F_B(rd8(c->a[4] - 0x3A00));
    } while (rd8(c->a[4] - 0x3A00));
    c->a[1] = sx(rd16(CONTINUATIONS));
    c->a[1] -= 4;
    c->a[0] = rd32(c->a[1]);
    put16m(CONTINUATIONS, c->a[1]);
}

/* $C0C2: walking into a building from start point $9676: the room left, the door's frame at the
 * point (one block across, two down), its picture $C, the routine a0 kept for the frame ($9F22) */
static void walk_in_setup(void)
{
    put8(0xFFFF9F05u, 6);
    put32m(0xFFFF9F22u, c->a[0]);
    call(0xF99C); call(0x13786); call(0x11BA4); call(0x1115E);
    c->a[0] = rd32(DATA_DIR);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9676u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    move_b(0, rd8(c->a[0] + 4));
    move_b(1, rd8(c->a[0] + 5));
    push32(0xC0F8); to_position(); c->a[7] += 4;
    put16m(0xFFFF9BD6u, c->d[0]);
    put16m(0xFFFF9BD8u, c->d[1]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 8, c->d[0]));
    put16m(DOOR, c->d[0]);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 0x20, c->d[1]));
    put16m(DOOR + 2, c->d[1]);
    put16m(DOOR + 4, 1);
    put16m(DOOR + 6, 2);
    c->d[0] = 12; F_L(12);
    push32(0xC120); door_frame(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 0x1E);
}

/* $BE18: the door at (d0, d1), d2 pixels each way: its frame (picture d3) opens, then the next room */
static void door_then_next(void)
{
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[2], c->d[0]));
    put16m(DOOR, c->d[0]);
    c->d[2] = set_w(c->d[2], add_w(&c->ccr, c->d[2], c->d[2]));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, c->d[2], c->d[1]));
    put16m(DOOR + 2, c->d[1]);
    c->d[2] = set_w(c->d[2], lsr_n(&c->ccr, c->d[2], 4, 16));
    put16m(DOOR + 4, c->d[2]);
    put16m(DOOR + 6, c->d[2]);
    call(DOOR_KEEP);
    move_b(0, (uint8_t)c->d[3]);
    push32(0xBE3A); door_frame(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 0x16);
    wait_frame(0xBE44);
    push32(0xBE48); door_blank(); c->a[7] += 4;
    door_animation(0xBE4C, 0);
    leave_to(NEXT_ROOM);
}

/* $BDB0 (11, from an item): leaving for a start point, by the exits' code */
static void seq_exit(void)
{
    push32(0xBDB2); room_leave(); c->a[7] += 4;
    call(0x8F98);
    uint8_t v = rd8(0xFFFF965Fu) | 1; wr8(0xFFFF965Fu, v); F_B(v);
    leave_to(EXIT_ROOM);
}
/* $BDC0 (13, from the door object at $1822A): its door kept at $99F2/$99F4 (picture 6, or 0 in
 * area $F) */
static void seq_kept_door(void)
{
    push32(0xBDC4); reset_continuations(); c->a[7] += 4;
    push32(0xBDC6); room_leave(); c->a[7] += 4;
    c->d[0] = set_w(c->d[0], rd16(0xFFFF99F2u)); F_W(c->d[0]);
    c->d[1] = set_w(c->d[1], rd16(0xFFFF99F4u)); F_W(c->d[1]);
    c->d[2] = 16;
    c->d[3] = 0; F_L(0);
    cmp_byte(0xF, rd8(AREA));
    if (rd8(AREA) != 0xF) { c->d[3] = 6; F_L(6); }
    door_then_next();
}
/* $BDDE (2): start point $9982's door (none for kind $10: an exit) */
static void seq_door(void)
{
    push32(0xBDE2); reset_continuations(); c->a[7] += 4;
    push32(0xBDE4); room_leave(); c->a[7] += 4;
    c->a[0] = rd32(DATA_DIR);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9982u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    cmp_byte(0x10, rd8(c->a[0] + 2));
    if (rd8(c->a[0] + 2) == 0x10) { leave_to(EXIT_ROOM); return; }
    move_b(0, rd8(c->a[0] + 4));
    move_b(1, rd8(c->a[0] + 5));
    push32(0xBE0A); to_position(); c->a[7] += 4;
    move_b(3, rd8(c->a[0] + 3));
    c->d[2] = 16; F_L(16);
    cmp_byte(8, c->d[3]);
    if ((c->d[3] & 0xFF) == 8) { c->d[2] = 8; F_L(8); }
    door_then_next();
}
/* $BE50 (12): a warp, after 20 frames */
static void seq_warp(void)
{
    push32(0xBE54); room_leave(); c->a[7] += 4;
    call(DOOR_KEEP);
    put16m(0xFFFF9C1Cu, 0x14);
    do wait_frame(0xBE62); while (mem_sub_w(0xFFFF9C1Cu, 1));
    leave_to(NEXT_ROOM);
}
/* $BE6C (3): into a shop or house: its picture swapped in behind a frame that opens, its things,
 * then the picture growing from the middle every 3 frames */
static void seq_inside(void)
{
    push32(0xBE70); reset_continuations(); c->a[7] += 4;
    call(DOOR_KEEP);
    uint8_t v = rd8(0xFFFF9659u); f_bit(v >> 4 & 1); wr8(0xFFFF9659u, v | 0x10);
    call(0x136A6); call(0xF99C); call(0x108FA);
    push32(0xBE8A); picture_setup(); c->a[7] += 4;
    push32(0xBE8E); rect_mark(); c->a[7] += 4;
    push32(0xBE92); picture_frame(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 0x16);
    wait_frame(0xBE9C);
    push32(0xBEA0); picture_corner(); c->a[7] += 4;
    push32(0xBEA4); rect_redraw(); c->a[7] += 4;
    v = rd8(0xFFFF9659u); f_bit(v & 1); wr8(0xFFFF9659u, v | 1);
    call(SPAWN_ENEMIES);
    call(0x133B2);
    v = rd8(0xFFFF9659u); f_bit(v & 1); wr8(0xFFFF9659u, v & ~1);
    door_animation(0xBEBC, 0);
    push32(0xBEC0); picture_frame_2(); c->a[7] += 4;
    wr16(0xFFFF9BE8u, 0); wr16(0xFFFF9BE6u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    push32(0xBECC); picture_put(); c->a[7] += 4;
    do {
        wait_frame(0xBED0); wait_frame(0xBED4); wait_frame(0xBED8);
        mem_add_w(0xFFFF9BE6u, 1);
        push32(0xBEE0); picture_grow(); c->a[7] += 4;
    } while (!(c->ccr & CC_Z));
    wait_frame(0xBEE6); wait_frame(0xBEEA);
    wr16(ROOM_CHANGE, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}
/* $BEF2 (4): out of it: the picture shrinking back (from the largest step still inside), the
 * frame closing, the room's triggers back */
static void seq_outside(void)
{
    push32(0xBEF6); reset_continuations(); c->a[7] += 4;
    call(DOOR_FREE); call(0xF99C); call(0x108FA);
    push32(0xBF06); rect_restore(); c->a[7] += 4;
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9BE2u)); F_W(c->d[0]);
    c->d[1] = set_w(c->d[1], rd16(0xFFFF9BDEu)); F_W(c->d[1]);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, c->d[0], c->d[1]));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(0xFFFF9BD2u), c->d[1]));
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if ((c->d[0] & 0xFFFF) < (c->d[1] & 0xFFFF)) { c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]); }
    c->d[1] = set_w(c->d[1], rd16(0xFFFF9BE4u)); F_W(c->d[1]);
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if ((c->d[0] & 0xFFFF) < (c->d[1] & 0xFFFF)) { c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]); }
    c->d[2] = set_w(c->d[2], rd16(0xFFFF9BE0u)); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[1], c->d[2]));
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, rd16(0xFFFF9BD4u), c->d[2]));
    cmp_w(&c->ccr, c->d[2], c->d[0]);
    if ((c->d[0] & 0xFFFF) < (c->d[2] & 0xFFFF)) { c->d[0] = set_w(c->d[0], c->d[2]); F_W(c->d[2]); }
    put16m(0xFFFF9BE6u, c->d[0]);
    put16m(0xFFFF9BE8u, 0x8000);
    do {
        wait_frame(0xBF42); wait_frame(0xBF46); wait_frame(0xBF4A);
        push32(0xBF4E); picture_grow(); c->a[7] += 4;
    } while (mem_sub_w(0xFFFF9BE6u, 1));
    push32(0xBF58); picture_frame(); c->a[7] += 4;
    door_animation(0xBF5C, 1);
    call(0x13748);
    uint8_t v = rd8(0xFFFF9659u); f_bit(v >> 4 & 1); wr8(0xFFFF9659u, v & ~0x10);
    wait_frame(0xBF6A);
    wr8(0xFFFFC008u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    put32m(TRIGGERS, rd32(0xFFFF9970u));
    push32(0xBF78); rect_redraw(); c->a[7] += 4;
    wr16(ROOM_CHANGE, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}
/* $BF80 (5): a house's door: the frame opens, the player walks through to the place inside (by
 * $127A's speed for $200), the frame closes behind */
static void seq_house(void)
{
    push32(0xBF84); reset_continuations(); c->a[7] += 4;
    call(DOOR_KEEP); call(0xF99C); call(0x108FA);
    c->a[2] = rd32(0xFFFF9986u);
    move_b(0, rd8(c->a[2] + 1));
    c->d[1] = 16; F_L(16);
    cmp_byte(8, c->d[0]);
    if ((c->d[0] & 0xFF) == 8) { c->d[1] = 8; F_L(8); }
    c->d[2] = set_w(c->d[2], rd16(0xFFFF9982u)); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[1], c->d[2]));
    put16m(DOOR, c->d[2]);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[1], c->d[1]));
    c->d[2] = set_w(c->d[2], rd16(0xFFFF9984u)); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[1], c->d[2]));
    put16m(DOOR + 2, c->d[2]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 16));
    put16m(DOOR + 4, c->d[1]);
    put16m(DOOR + 6, c->d[1]);
    push32(0xBFC6); door_frame(); c->a[7] += 4;
    wait_frame(0xBFCA);
    push32(0xBFCE); door_blank(); c->a[7] += 4;
    door_animation(0xBFD2, 0);
    player_a4();
    uint8_t v = rd8(c->a[4] - 0x3FFD) | 0x10; wr8(c->a[4] - 0x3FFD, v); F_B(v);
    wait_frame(0xBFE0);
    push32(0xBFE4); door_restore(); c->a[7] += 4;
    wr16(0xFFFFC008u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    player_a4();
    call(0xC1C);
    c->a[2] = rd32(0xFFFF9986u);
    move_b(0, rd8(c->a[2] + 2));
    move_b(1, rd8(c->a[2] + 3));
    push32(0xC000); to_position(); c->a[7] += 4;
    c->d[2] = 16; F_L(16);
    cmp_byte(1, rd8(DOOR + 4));                     /* the word's high byte, as the original reads it */
    if (rd8(DOOR + 4) != 1) {
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[2], c->d[0]));
        c->d[2] = 32; F_L(32);
    }
    c->d[4] = set_w(c->d[4], c->d[0]); F_W(c->d[0]);
    c->d[4] = set_w(c->d[4], sub_w(&c->ccr, rd16(DOOR), c->d[4]));
    put16m(DOOR, c->d[0]);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, c->d[2], c->d[1]));
    c->d[5] = set_w(c->d[5], c->d[1]); F_W(c->d[1]);
    c->d[5] = set_w(c->d[5], sub_w(&c->ccr, rd16(DOOR + 2), c->d[5]));
    put16m(DOOR + 2, c->d[1]);
    player_a4();
    c->d[0] = set_w(c->d[0], c->d[4]); F_W(c->d[4]);
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[4]));
    put16m(0xFFFF9C20u, c->d[4]);
    c->d[1] = set_w(c->d[1], c->d[5]); F_W(c->d[5]);
    c->d[5] = set_w(c->d[5], add_w(&c->ccr, rd16(c->a[4] - 0x3700), c->d[5]));
    put16m(0xFFFF9C22u, c->d[5]);
    c->d[2] = set_w(c->d[2], 0x200); F_W(0x200);
    call(0x127A);
    put16m(c->a[4] - 0x3600, c->d[4]);
    put16m(c->a[4] - 0x35FE, c->d[5]);
    c->d[2] = set_w(c->d[2], lsr_n(&c->ccr, c->d[2], 1, 16));
    put16m(0xFFFF9C1Cu, c->d[2]);
    for (;;) {
        wait_frame(0xC056);
        player_a4();
        call(APPLY_VELOCITY);
        push32(0xC062); layers_frame(); c->a[7] += 4;
        F_W(rd16(0xFFFF9C1Cu));
        if (!rd16(0xFFFF9C1Cu)) break;
        if (!mem_sub_w(0xFFFF9C1Cu, 1)) break;
    }
    player_a4();
    call(0xC1C);
    put16m(c->a[4] - 0x3800, rd16(0xFFFF9C20u));
    put16m(c->a[4] - 0x3700, rd16(0xFFFF9C22u));
    c->a[4] = 8;
    c->a[2] = rd32(0xFFFF9986u);
    move_b(0, rd8(c->a[2] + 1));
    push32(0xC092); door_frame(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 8);
    wait_frame(0xC09C);
    push32(0xC0A0); door_blank(); c->a[7] += 4;
    player_a4();
    call(DOOR_FREE);
    v = rd8(c->a[4] - 0x3FFD) & 0xEF; wr8(c->a[4] - 0x3FFD, v); F_B(v);
    door_animation(0xC0B2, 1);
    push32(0xC0B6); door_restore(); c->a[7] += 4;
    wr8(0xFFFFC008u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    wr16(ROOM_CHANGE, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}

ENTRY(WalkInSetup, walk_in_setup)          /* $C0C2 */
/* the sequences and waits leave by port_continue_at, so ret is theirs to set */
void SeqExit(Cpu *cpu)      { c = cpu; NEEDS_PASS(0xBDB0); seq_exit(); }        /* $BDB0 */
void SeqKeptDoor(Cpu *cpu)  { c = cpu; NEEDS_PASS(0xBDC0); seq_kept_door(); }   /* $BDC0 */
void SeqDoor(Cpu *cpu)      { c = cpu; NEEDS_PASS(0xBDDE); seq_door(); }        /* $BDDE */
void SeqWarp(Cpu *cpu)      { c = cpu; NEEDS_PASS(0xBE50); seq_warp(); }        /* $BE50 */
void SeqInside(Cpu *cpu)    { c = cpu; NEEDS_PASS(0xBE6C); seq_inside(); }      /* $BE6C */
void SeqOutside(Cpu *cpu)   { c = cpu; NEEDS_PASS(0xBEF2); seq_outside(); }     /* $BEF2 */
void SeqHouse(Cpu *cpu)     { c = cpu; NEEDS_PASS(0xBF80); seq_house(); }       /* $BF80 */
static void frame_wait_68k(int restart)
{
    uint32_t ret = rd32(c->a[7]); c->a[7] += 4;
    frame_wait(ret, restart);
    port_continue_at(c, c->a[0], 0);
}
void FrameWait(Cpu *cpu)    { c = cpu; NEEDS_PASS(0xC128); frame_wait_68k(0); }   /* $C128 */
void FrameRestart(Cpu *cpu) { c = cpu; NEEDS_PASS(0xC152); frame_wait_68k(1); }   /* $C152 */

/* ---- batch T20: arriving in a room ----
 * After loading a room the game's task jumps by how the player arrived ($9678, its table at
 * $8F6C) to one of these: through a door (its frame closing behind), into a shop or house (its
 * picture already in), walking out of a building, or a plain fade in. Walking into a building
 * ($C184) is a room change ($9962 6). Their helpers keep their callers' return addresses on the
 * continuations' stack, as the original's do. $C614, $C67E and $C6F6 are not yet registered: no
 * session runs them yet. */

static void push_cont(uint32_t ret)
{
    c->a[0] = sx(rd16(CONTINUATIONS));
    wr32(c->a[0], ret); c->a[0] += 4; F_L(ret);
    put16m(CONTINUATIONS, c->a[0]);
}
static void pop_cont(void)
{
    c->a[1] = sx(rd16(CONTINUATIONS));
    c->a[1] -= 4;
    c->a[0] = rd32(c->a[1]);
    put16m(CONTINUATIONS, c->a[1]);
}

/* $C572: the door's frame round the player (one block for a kind 8 arrival, else two), closed,
 * the blocks under it blanked */
static void door_at_player(uint32_t ret)
{
    push_cont(ret);
    c->d[1] = 2;
    move_b(0, rd8(0xFFFF9679u));
    cmp_byte(8, c->d[0]);
    if ((c->d[0] & 0xFF) == 8) { c->d[1] = 1; F_L(1); }
    put16m(DOOR + 4, c->d[1]);
    put16m(DOOR + 6, c->d[1]);
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 3, 16));
    player_a4();
    c->d[2] = set_w(c->d[2], rd16(c->a[4] - 0x3800)); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[1], c->d[2]));
    put16m(DOOR, c->d[2]);
    feet(2);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[1], c->d[1]));
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[1], c->d[2]));
    put16m(DOOR + 2, c->d[2]);
    push32(0xC5B8); door_frame(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 8);
    push32(0xC5C2); door_blank(); c->a[7] += 4;
    call(DOOR_FREE);
    pop_cont();
}
/* $C5D2: the screen faded in, waiting for the fade's end */
static void fade_in_wait(uint32_t ret)
{
    push_cont(ret);
    call(0x581A);
    do {
        wait_frame(0xC5E4);
        f_bit(rd8(0xFFFF8C56u) >> 4 & 1);
    } while (!(rd8(0xFFFF8C56u) & 0x10));
    pop_cont();
}
/* $C634: arriving inside: the event $996A's picture swapped in and its things spawned */
static void inside_setup(uint32_t ret)
{
    push_cont(ret);
    uint8_t v = rd8(0xFFFF9659u); f_bit(v >> 4 & 1); wr8(0xFFFF9659u, v | 0x10);
    call(0x136A6);
    c->d[0] = 0;
    move_b(0, rd8(0xFFFF996Au));
    put16m(0xFFFF9964u, c->d[0]);
    push32(0xC656); picture_setup(); c->a[7] += 4;
    v = rd8(0xFFFF9659u); f_bit(v & 1); wr8(0xFFFF9659u, v | 1);
    call(SPAWN_ENEMIES);
    call(0x133B2);
    v = rd8(0xFFFF9659u); f_bit(v & 1); wr8(0xFFFF9659u, v & ~1);
    push32(0xC66E); picture_put(); c->a[7] += 4;
    push32(0xC672); rect_redraw(); c->a[7] += 4;
    pop_cont();
}

/* the arrivals ($8F6C) */
static void arrive_door(void)        /* $C5F8 (2, 4): through a door, its frame closing */
{
    push32(0xC5FC); reset_continuations(); c->a[7] += 4;
    door_at_player(0xC600);
    fade_in_wait(0xC604);
    door_animation(0xC608, 1);
    push32(0xC60C); door_restore(); c->a[7] += 4;
    wr8(0xFFFFC008u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}
static void arrive_still(void)       /* $C614 (10): a fade in, then 20 frames */
{
    push32(0xC618); reset_continuations(); c->a[7] += 4;
    call(DOOR_FREE);
    fade_in_wait(0xC620);
    put16m(0xFFFF9C1Cu, 0x14);
    do wait_frame(0xC62A); while (mem_sub_w(0xFFFF9C1Cu, 1));
    leave_to(GAME_LOOP);
}
static void arrive_inside(void)      /* $C67E (3): inside a shop or house */
{
    push32(0xC682); reset_continuations(); c->a[7] += 4;
    inside_setup(0xC686);
    push32(0xC68A); picture_frame_2(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 8);
    fade_in_wait(0xC694);
    leave_to(GAME_LOOP);
}
static void arrive_walk_out(void)    /* $C698 (5): walking out of a building, to its door's right */
{
    push32(0xC69C); reset_continuations(); c->a[7] += 4;
    player_a4();
    mem_sub_w(c->a[4] - 0x3800, 0x18);
    fade_in_wait(0xC6AA);
    c->a[0] = 0xC6EEu;
    push32(0xC6B2); walk_in_setup(); c->a[7] += 4;
    wait_frame(0xC6B6);
    push32(0xC6BA); door_blank(); c->a[7] += 4;
    frame_wait(0xC6BE, 0);
    for (;;) {
        wait_frame(0xC6C2);
        call(0xDD08);
        player_a4();
        c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x3800)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(0xFFFF9BD6u), c->d[0]));
        cmp_w(&c->ccr, 0x14, c->d[0]);
        if ((int16_t)c->d[0] > 0x14) break;
    }
    frame_wait(0xC6DE, 1);
    push32(0xC6E2); door_restore(); c->a[7] += 4;
    wr8(0xFFFFC008u, 0); wr8(0xFFFF9F05u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}
static void arrive_inside_door(void) /* $C6F6 (6, 7): inside, through its door */
{
    push32(0xC6FA); reset_continuations(); c->a[7] += 4;
    inside_setup(0xC6FE);
    door_at_player(0xC702);
    fade_in_wait(0xC706);
    door_animation(0xC70A, 1);
    push32(0xC70E); door_restore(); c->a[7] += 4;
    push32(0xC712); picture_frame_2(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 8);
    leave_to(GAME_LOOP);
}
static void arrive_fade(void)        /* $C71C (8): a fade in, then the exits' arrival ($F7EA) */
{
    push32(0xC720); reset_continuations(); c->a[7] += 4;
    fade_in_wait(0xC724);
    leave_to(0xF7EA);
}
static void arrive_talk(void)        /* $C728 (9): inside, then a conversation after 30 frames ($C1E8) */
{
    push32(0xC72C); reset_continuations(); c->a[7] += 4;
    inside_setup(0xC730);
    push32(0xC734); picture_frame_2(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 8);
    fade_in_wait(0xC73E);
    uint8_t v = rd8(0xFFFF965Fu); f_bit(v & 1); wr8(0xFFFF965Fu, v & ~1);
    if (v & 1) { leave_to(0xF7EA); return; }
    put16m(0xFFFF9C32u, 0x1E);
    do wait_frame(0xC752); while (mem_sub_w(0xFFFF9C32u, 1));
    leave_to(0xC1E8);
}
/* $C184 (6): walking into a building to start point $9982 (going left), then $9984's room */
static void seq_walk_in(void)
{
    push32(0xC188); reset_continuations(); c->a[7] += 4;
    put16m(0xFFFF9676u, rd16(0xFFFF9982u));
    c->a[0] = 0xC1E0u;
    push32(0xC196); walk_in_setup(); c->a[7] += 4;
    player_a4();
    c->d[0] = 0; F_L(0);
    call(SET_ANIM);
    wait_frame(0xC1A4);
    push32(0xC1A8); door_blank(); c->a[7] += 4;
    frame_wait(0xC1AC, 0);
    for (;;) {
        wait_frame(0xC1B0);
        call(0xDD08);
        player_a4();
        c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x3800)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(0xFFFF9BD6u), c->d[0]));
        cmp_w(&c->ccr, 0xFFE8, c->d[0]);
        if ((int16_t)c->d[0] < -0x18) break;
    }
    frame_wait(0xC1CC, 1);
    put16m(0xFFFF9676u, rd16(0xFFFF9984u));
    wr8(0xFFFF9F05u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    put8(0xFFFF966Au, 3);
    leave_to(NEXT_ROOM);
}
/* $C1E0, $C6EE: the walking's buttons for the frame's routine ($9F22): left, right */
static void walk_left(void) { put8(0xFFFF9F0Bu, 4); }
static void walk_right(void) { put8(0xFFFF9F0Bu, 8); }

ENTRY(WalkLeft, walk_left)                 /* $C1E0 */
ENTRY(WalkRight, walk_right)               /* $C6EE */
/* those that wait, or go on at a popped address, leave by port_continue_at */
static void cont_68k(void (*fn)(uint32_t)) { uint32_t ret = rd32(c->a[7]); c->a[7] += 4; fn(ret); port_continue_at(c, c->a[0], 0); }
void DoorAtPlayer(Cpu *cpu) { c = cpu; cont_68k(door_at_player); }   /* $C572 */
void FadeInWait(Cpu *cpu)   { c = cpu; NEEDS_PASS(0xC5D2); cont_68k(fade_in_wait); }     /* $C5D2 */
void InsideSetup(Cpu *cpu)  { c = cpu; cont_68k(inside_setup); }     /* $C634 */
void ArriveDoor(Cpu *cpu)       { c = cpu; NEEDS_PASS(0xC5F8); arrive_door(); }          /* $C5F8 */
void ArriveStill(Cpu *cpu)      { c = cpu; NEEDS_PASS(0xC614); arrive_still(); }         /* $C614 */
void ArriveInside(Cpu *cpu)     { c = cpu; NEEDS_PASS(0xC67E); arrive_inside(); }        /* $C67E */
void ArriveWalkOut(Cpu *cpu)    { c = cpu; NEEDS_PASS(0xC698); arrive_walk_out(); }      /* $C698 */
void ArriveInsideDoor(Cpu *cpu) { c = cpu; NEEDS_PASS(0xC6F6); arrive_inside_door(); }   /* $C6F6 */
void ArriveFade(Cpu *cpu)       { c = cpu; NEEDS_PASS(0xC71C); arrive_fade(); }          /* $C71C */
void ArriveTalk(Cpu *cpu)       { c = cpu; NEEDS_PASS(0xC728); arrive_talk(); }          /* $C728 */
void SeqWalkIn(Cpu *cpu)        { c = cpu; NEEDS_PASS(0xC184); seq_walk_in(); }          /* $C184 */

/* ---- batch T21: breaking walls, crumbling walls, the door records ----
 * In a room with breakable floors ($966B bit 3) the player's downward thrust on a cracked tile
 * (attribute $D) breaks it, or the one beside it under the player's width: a crumble effect
 * (kind 2) takes its place. Cracked walls ($9B96: up to four records of 8 bytes, to $9BB6)
 * crumble outward a block at a time every 3 frames. The doors of command 19 are 16 records at
 * $FF9A96 (4 bytes apart, with more fields further on). */

#define TILE_ATTR_AT  0x30DAu       /* d6, d7: the tile's attribute (d1) and flags (d2) on layer a3 */
#define FIND_EFFECT   0x27E6u       /* a4 a free effect (mi: none) */
#define CLEAR_ENTITY  0x0A36u
#define WALLS         0xFFFF9B96u   /* the crumbling walls' records */

/* $B400: a crumble effect at (d6, d7) (d5 its word), with sound 75 */
static void break_spawn(void)
{
    call(FIND_EFFECT);
    if (c->ccr & CC_N) return;
    call(CLEAR_ENTITY);
    put16m(c->a[4] - 0x2600, c->d[5]);
    put16m(c->a[4] - 0x2500, c->d[6]);
    put16m(c->a[4] - 0x24FE, c->d[7]);
    put16m(c->a[4] - 0x2BFE, 2);
    put8(c->a[4] - 0x4000, 0x80);
    c->d[0] = 75; F_L(75);
    call(QUEUE_SOUND);
}
/* $B394, $B3C8: the tile left (or right) of the player's, if the player's width reaches it:
 * broken if cracked (d0 -1), d0 -1 too if it is solid (flags bit 2) */
static void break_side(int right)
{
    c->d[0] = 0;
    move_b(0, rd8(c->a[4] - 0x34FE));
    c->d[1] = set_w(c->d[1], rd16(c->a[4] - 0x3800)); F_W(c->d[1]);
    and_w(1, 0xF);
    if (!right) {
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, c->d[0], c->d[1]));
        if (!(c->ccr & CC_C)) goto no;
        c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    } else {
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[0], c->d[1]));
        cmp_w(&c->ccr, 0x10, c->d[1]);
        if ((c->d[1] & 0xFFFF) < 0x10) goto no;
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    }
    call(TILE_ATTR_AT);
    cmp_byte(0xD, c->d[1]);
    if ((c->d[1] & 0xFF) == 0xD) {
        c->d[5] = 0; F_L(0);
        push32(right ? 0xB3FC : 0xB3C4); break_spawn(); c->a[7] += 4;
        goto yes;
    }
    c->d[6] = set_w(c->d[6], right ? sub_w(&c->ccr, 1, c->d[6]) : add_w(&c->ccr, 1, c->d[6]));
    f_bit(c->d[2] >> 2 & 1);
    if (c->d[2] & 4) goto yes;
no:
    c->d[0] = 0; F_L(0);
    return;
yes:
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
}
static void break_left(void) { break_side(0); }    /* $B394 */
static void break_right(void) { break_side(1); }   /* $B3C8 */

/* $B32E: each frame: the player thrusting down ($CF00 & $A is 2) onto a tile's top in a room with
 * breakable floors: the tile below broken if cracked, else the nearer side's, then the other's */
static void break_check(void)
{
    f_bit(rd8(0xFFFF966Bu) >> 3 & 1);
    if (!(rd8(0xFFFF966Bu) & 8)) return;
    player_a4();
    move_b(0, rd8(c->a[4] - 0x3100));
    and_w(0, 0xA);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 2, c->d[0]));
    if (c->d[0] & 0xFFFF) return;
    feet(7);
    c->d[0] = set_w(c->d[0], c->d[7]); F_W(c->d[7]);
    and_w(0, 0xF);
    if (c->d[0] & 0xFFFF) return;
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 2, 16));
    c->d[6] = set_w(c->d[6], rd16(c->a[4] - 0x3800)); F_W(c->d[6]);
    c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, c->d[6], 4, 16));
    c->a[3] = LAYER_A;
    call(TILE_ATTR_AT);
    cmp_byte(0xD, c->d[1]);
    if ((c->d[1] & 0xFF) == 0xD) {
        c->d[5] = 0; F_L(0);
        push32(0xB392); break_spawn(); c->a[7] += 4;
        return;
    }
    f_bit(c->d[2] >> 2 & 1);
    if (c->d[2] & 4) return;
    c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x3800)); F_W(c->d[0]);
    and_w(0, 0xF);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 8, c->d[0]));
    if (c->ccr & CC_C) {                            /* the left half: the left first */
        push32(0xB382); break_left(); c->a[7] += 4;
        if (!(c->ccr & CC_Z)) return;
        break_right();
    } else {
        push32(0xB388); break_right(); c->a[7] += 4;
        if (!(c->ccr & CC_Z)) return;
        break_left();
    }
}

/* $B49C: a door's record from command 19's bytes at a0, in the first free slot (d0 -1; none: as
 * the slots' search left it) */
static void door_add(void)
{
    c->a[1] = 0;
    c->d[0] = 15;
    for (;;) {
        F_B(rd8(c->a[1] - 0x656A));
        if (!(rd8(c->a[1] - 0x656A) & 0x80)) break;
        c->a[1] += 4;
        if (!dbra(0)) return;
    }
    move_b(0, next8(0));
    put8(c->a[1] - 0x6567, next8(0));
    c->d[1] = 0xFFFFFF80u;
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
    if (c->ccr & (CC_C | CC_Z)) {                   /* not bhi */
        if (c->ccr & CC_Z) c->d[1] = 0xFFFFFFE0u;   /* kind 1 */
        else c->d[1] = 0xFFFFFFC0u;                 /* kind 0 */
    }
    move_b(0, next8(0));
    if (c->d[0] & 0x80) {
        and_b(0, 0x7F);
        c->d[1] = set_b(c->d[1], add_b(&c->ccr, 1, c->d[1]));
    }
    put8(c->a[1] - 0x6569, c->d[0]);
    put8(c->a[1] - 0x656A, c->d[1]);
    put8(c->a[1] - 0x6568, next8(0));
    c->d[0] = set_w(c->d[0], 0x100); F_W(0x100);
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
    move_b(0, next8(0));
    put16m(c->a[1] - 0x652A, c->d[0]);
    move_b(1, next8(0));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 6, 16));
    put16m(c->a[1] - 0x6528, c->d[1]);
    put8(c->a[1] - 0x64E8, next8(0));
    put8(c->a[1] - 0x64E7, next8(0));
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
}
/* $B4FA: the doors' records emptied */
static void doors_clear(void)
{
    c->d[0] = 0;
    c->a[0] = 0;
    c->d[1] = 15;
    do { put8(c->a[0] - 0x656A, 0); c->a[0] += 4; } while (dbra(1));
}
/* $B852: the crumbling walls' records emptied */
static void crumble_clear(void)
{
    c->a[0] = WALLS;
    c->d[0] = 0;
    c->d[1] = 3;
    do { put8(c->a[0], 0); c->a[0] += 8; } while (dbra(1));
}
/* $B864: each crumbling wall every 3 frames: the block at its step's distance either way
 * cracked away ($B8D4), each side stopping (bits 0, 1) where none is left, for 4 steps */
static void crumble_walls(void)
{
    c->a[4] = WALLS;
    c->a[3] = LAYER_A;
    do {
        F_B(rd8(c->a[4]));
        if (!(rd8(c->a[4]) & 0x80)) goto next;
        { uint8_t v = (uint8_t)sub_b(&c->ccr, 1, rd8(c->a[4] + 1)); wr8(c->a[4] + 1, v); if (v) goto next; }
        put8(c->a[4] + 1, 3);
        c->d[6] = set_w(c->d[6], rd16(c->a[4] + 4)); F_W(c->d[6]);
        c->d[7] = set_w(c->d[7], rd16(c->a[4] + 6)); F_W(c->d[7]);
        c->d[0] = set_w(c->d[0], rd16(c->a[4] + 2)); F_W(c->d[0]);
        if (!(c->d[0] & 0xFFFF)) {                  /* its own block first */
            push32(0xB88C); cracked_at(); c->a[7] += 4;
            if (!(c->ccr & CC_Z)) goto step;
            goto done;
        }
        f_bit(rd8(c->a[4]) >> 1 & 1);
        if (!(rd8(c->a[4]) & 2)) {                  /* to the right */
            c->d[6] = set_w(c->d[6], add_w(&c->ccr, c->d[0], c->d[6]));
            push32(0xB89C); cracked_at(); c->a[7] += 4;
            if (c->ccr & CC_Z) { uint8_t v = (uint8_t)add_b(&c->ccr, 2, rd8(c->a[4])); wr8(c->a[4], v); }
        }
        f_bit(rd8(c->a[4]) & 1);
        if (!(rd8(c->a[4]) & 1)) {                  /* to the left */
            c->d[6] = set_w(c->d[6], rd16(c->a[4] + 4)); F_W(c->d[6]);
            c->d[6] = set_w(c->d[6], sub_w(&c->ccr, rd16(c->a[4] + 2), c->d[6]));
            push32(0xB8B0); cracked_at(); c->a[7] += 4;
            if (c->ccr & CC_Z) { uint8_t v = (uint8_t)add_b(&c->ccr, 1, rd8(c->a[4])); wr8(c->a[4], v); }
        }
        c->d[0] = 3;
        and_b(0, rd8(c->a[4]));
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 3, c->d[0]));
        if (!(c->d[0] & 0xFF)) goto done;
    step:
        mem_add_w(c->a[4] + 2, 1);
        cmp_w(&c->ccr, 4, rd16(c->a[4] + 2));
        if (rd16(c->a[4] + 2) < 4) goto next;
    done:
        wr8(c->a[4], 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    next:
        c->a[4] += 8;
        cmp_l(&c->ccr, sx(rd16(0xFFFF9BB6u)), c->a[4]);
    } while (c->ccr & CC_C);
}

ENTRY(BreakCheck, break_check)             /* $B32E */
ENTRY(BreakLeft, break_left)               /* $B394 */
ENTRY(BreakRight, break_right)             /* $B3C8 */
ENTRY(BreakSpawn, break_spawn)             /* $B400 */
ENTRY(DoorAdd, door_add)                   /* $B49C */
ENTRY(DoorsClear, doors_clear)             /* $B4FA */
ENTRY(CrumbleClear, crumble_clear)         /* $B852 */
ENTRY(CrumbleWalls, crumble_walls)         /* $B864 */

/* ---- batch T22: the gates (the doors' records) each frame ----
 * Command 19's records (DoorAdd) are gates: a bar of blocks (tile $2B0 + its direction) that
 * slides out from its place a block at a time to its length and back, with a 2-cell end that
 * flickers through 3 frames. Each waits open, shuts, waits shut (bit 0) throwing sparks, opens
 * and so on; a wait of 0 lasts for ever. A gate tied to a flag (bit 6: a game flag with bit 5,
 * else a room flag) opens for good once the flag is set. Bit 1: moving, bit 2: opening. Records
 * 4 bytes apart, a4 their offset. */

#define G_FLAGS       0xFFFF9A96u   /* bit 7: in use, 0: shut */
#define G_DIR         0xFFFF9A97u   /* 0 right, 1 down, 2 left, 3 up */
#define G_LENGTH      0xFFFF9A98u   /* in blocks, shut */
#define G_FLAG        0xFFFF9A99u
#define G_X           0xFFFF9AD6u   /* its cell */
#define G_Y           0xFFFF9AD8u
#define G_TIMER       0xFFFF9B16u   /* frames to the next step */
#define G_PHASE       0xFFFF9B17u   /* the end's frame, 0-2 */
#define G_WAIT_SHUT   0xFFFF9B18u
#define G_WAIT_OPEN   0xFFFF9B19u
#define G_WAIT        0xFFFF9B56u
#define G_SPARK       0xFFFF9B57u   /* frames to the next spark */
#define G_OUT         0xFFFF9B58u   /* blocks out */
#define RANDOM        0x05D8u
#define SET_MAP_BLOCKS 0x2812u      /* d0 x d1 blocks from d2 at (d6, d7) */
#define SPAWN_ITEM    0x1090Cu

static const uint8_t gate_steps[10] = { 0x10, 0x0C, 0x0A, 0x08, 0x06, 0x05, 0x04, 0x03, 0x02, 0x00 };   /* $B732: frames a step, by blocks out */
static const uint16_t gate_ends[4][3][2] = {                 /* $B6D2: the end's two cells by direction and frame */
    { { 0x2AA, 0x2AB }, { 0x2AE, 0x2AF }, { 0x2AC, 0x2AD } },
    { { 0x298, 0x299 }, { 0x29A, 0x29B }, { 0x29C, 0x29D } },
    { { 0x2A4, 0x2A5 }, { 0x2A8, 0x2A9 }, { 0x2A6, 0x2A7 } },
    { { 0x29E, 0x29F }, { 0x2A0, 0x2A1 }, { 0x2A2, 0x2A3 } },
};
static uint32_t g(uint32_t field) { return c->a[4] + field; }
static void g_or(uint32_t field, uint8_t m) { uint8_t v = rd8(g(field)) | m; wr8(g(field), v); F_B(v); }
static void g_and(uint32_t field, uint8_t m) { uint8_t v = rd8(g(field)) & m; wr8(g(field), v); F_B(v); }
static uint8_t g_dec(uint32_t field) { uint8_t v = (uint8_t)sub_b(&c->ccr, 1, rd8(g(field))); wr8(g(field), v); return v; }

/* $B7A6: the next spark in 12 to 139 frames */
static void gate_spark_time(void)
{
    call(RANDOM);
    and_b(0, 0x7F);
    c->d[0] = set_b(c->d[0], add_b(&c->ccr, 0xC, c->d[0]));
    put8(g(G_SPARK), c->d[0]);
}
/* $B724: the step's frames by how far out it is */
static void gate_timer(void)
{
    c->d[0] = 0;
    move_b(0, rd8(g(G_OUT)));
    put8(g(G_TIMER), gate_steps[c->d[0]]);
}
/* $B67E: the end's next frame drawn; $B68C (first) its frame 0 */
static void gate_end_frame(uint32_t phase)
{
    put8(g(G_PHASE), phase);
    c->d[0] = set_w(c->d[0], phase);
    c->d[6] = set_w(c->d[6], rd16(g(G_X))); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(g(G_Y))); F_W(c->d[7]);
    uint32_t dir = rd8(g(G_DIR));
    c->d[1] = dir * 6;
    c->d[2] = set_w(c->d[2], dir);
    c->d[0] = set_w(c->d[0], (dir * 6 + phase * 2) * 2);
    c->a[0] = 0xB6D4u + (c->d[0] & 0xFFFF);
    c->d[0] = set_w(c->d[0], gate_ends[dir][phase][0]);
    c->a[7] -= 2; wr16(c->a[7], gate_ends[dir][phase][1]);
    if (dir & 1) {                                  /* up or down: side by side */
        push32(0xB6C0); cell_set_a(); c->a[7] += 4;
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    } else {
        push32(0xB6C8); cell_set_a(); c->a[7] += 4;
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    }
    c->d[0] = set_w(c->d[0], rd16(c->a[7])); c->a[7] += 2; F_W(c->d[0]);
    cell_set_draw_if_shown();
}
static void gate_end(void) { c->d[0] = 0; F_L(0); gate_end_frame(0); }   /* $B68C */
static void gate_end_next(void)
{
    c->d[0] = 0;
    move_b(0, rd8(g(G_PHASE)));
    c->d[0] = set_b(c->d[0], add_b(&c->ccr, 1, c->d[0]));
    cmp_byte(3, c->d[0]);
    if ((c->d[0] & 0xFF) < 3) gate_end_frame(c->d[0] & 0xFF);
    else gate_end();
}
/* $B702: its flag (none: eq) */
static void gate_flag(void)
{
    f_bit(rd8(g(G_FLAGS)) >> 6 & 1);
    if (!(rd8(g(G_FLAGS)) & 0x40)) return;
    move_b(0, rd8(g(G_FLAG)));
    f_bit(rd8(g(G_FLAGS)) >> 5 & 1);
    if (rd8(g(G_FLAGS)) & 0x20) { flag_test(); return; }
    push32(0xB71E); room_flag_bit(); c->a[7] += 4;
    f_bit(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1);
}
/* $B754: the block d0 at blocks out from its place, then the end's next frame */
static void gate_cell(void)
{
    c->d[6] = set_w(c->d[6], rd16(g(G_X))); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(g(G_Y))); F_W(c->d[7]);
    c->a[7] -= 2; wr16(c->a[7], c->d[0]);
    c->d[1] = 0;
    move_b(1, rd8(g(G_OUT)));
    move_b(2, rd8(g(G_DIR)));
    int beside;                                     /* up or down: the block's two cells side by side */
    switch (rd8(g(G_DIR))) {
    case 0: c->d[6] = set_w(c->d[6], add_w(&c->ccr, c->d[1], c->d[6])); beside = 0; break;
    case 1:
        c->d[2] = set_b(c->d[2], 0);
        c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 6, 16));
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[1], c->d[7])); beside = 1; break;
    case 2: c->d[2] = set_b(c->d[2], 0); c->d[6] = set_w(c->d[6], sub_w(&c->ccr, c->d[1], c->d[6])); beside = 0; break;
    default:
        c->d[2] = set_b(c->d[2], c->d[2] - 2);
        c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 6, 16));
        c->d[7] = set_w(c->d[7], sub_w(&c->ccr, c->d[1], c->d[7])); beside = 1; break;
    }
    if (beside) {
        push32(0xB790); cell_set_a(); c->a[7] += 4;
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    } else {
        push32(0xB782); cell_set_a(); c->a[7] += 4;
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    }
    c->d[0] = set_w(c->d[0], rd16(c->a[7])); c->a[7] += 2; F_W(c->d[0]);
    push32(0xB798); cell_set_draw_if_shown(); c->a[7] += 4;
    gate_end_next();
}
/* $B744: a block further out */
static void gate_grow(void)
{
    wr8(g(G_OUT), (uint8_t)add_b(&c->ccr, 1, rd8(g(G_OUT))));
    c->d[0] = 0;
    move_b(0, rd8(g(G_DIR)));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], 0x2B0 + rd8(g(G_DIR))); F_W(c->d[0]);
    gate_cell();
}
/* $B79C: a block back in */
static void gate_shrink(void)
{
    c->d[0] = 0; F_L(0);
    push32(0xB7A0); gate_cell(); c->a[7] += 4;
    g_dec(G_OUT);
}
/* $B7B8: a shut gate's sparks, at random along its end */
static void gate_sparks(void)
{
    f_bit(rd8(g(G_FLAGS)) & 1);
    if (!(rd8(g(G_FLAGS)) & 1)) return;
    if (g_dec(G_SPARK)) return;
    call(RANDOM);
    c->d[1] = set_w(c->d[1], rd16(g(G_X))); F_W(c->d[1]);
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 4, 16));
    c->d[2] = set_w(c->d[2], rd16(g(G_Y))); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], lsr_n(&c->ccr, c->d[2], 2, 16));
    c->d[4] = set_w(c->d[4], c->d[0]); F_W(c->d[0]);
    and_w(4, 0xF);
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, 8, c->d[4]));
    c->d[3] = 0;
    move_b(3, rd8(g(G_DIR)));
    c->d[3] = set_w(c->d[3], add_w(&c->ccr, c->d[3], c->d[3]));
    static const uint16_t at[4] = { 0xB806, 0xB80E, 0xB816, 0xB81C };
    c->a[0] = at[rd8(g(G_DIR))];
    switch (rd8(g(G_DIR))) {                        /* along the end's far side */
    case 0: c->d[1] = set_w(c->d[1], add_w(&c->ccr, 0x12, c->d[1])); c->d[2] = set_w(c->d[2], add_w(&c->ccr, c->d[4], c->d[2])); break;
    case 1: c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[4], c->d[1])); c->d[2] = set_w(c->d[2], add_w(&c->ccr, 0x12, c->d[2])); break;
    case 2: c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 2, c->d[1])); c->d[2] = set_w(c->d[2], add_w(&c->ccr, c->d[4], c->d[2])); break;
    default: c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[4], c->d[1])); c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 2, c->d[2])); break;
    }
    int big = c->d[0] >> 8 & 1;
    c->d[3] = set_w(c->d[3], big ? 0x210 : 0x204); F_W(c->d[3]);
    f_bit(big);
    c->d[0] = set_w(c->d[0], c->d[3]); F_W(c->d[3]);
    call(SPAWN_ITEM);
    gate_spark_time();
}
/* $B526: a gate as its room loads: gone if its flag is set; one starting shut drawn shut */
static void gate_setup(void)
{
    push32(0xB52A); gate_end(); c->a[7] += 4;
    push32(0xB52E); gate_flag(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) { put8(g(G_FLAGS), 0); return; }
    f_bit(rd8(g(G_FLAGS)) & 1);
    if (!(rd8(g(G_FLAGS)) & 1)) {
        put8(g(G_WAIT), rd8(g(G_WAIT_OPEN)));
        put8(g(G_OUT), 0);
        return;
    }
    put8(g(G_WAIT), rd8(g(G_WAIT_SHUT)));
    c->d[1] = 0;
    move_b(1, rd8(g(G_LENGTH)));
    put8(g(G_OUT), c->d[1]);
    c->d[0] = 0;
    move_b(0, rd8(g(G_DIR)));
    uint32_t dir = c->d[0];
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[6] = set_w(c->d[6], rd16(g(G_X))); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(g(G_Y))); F_W(c->d[7]);
    c->d[2] = set_w(c->d[2], 0x2B0 + dir); F_W(c->d[2]);
    static const uint16_t at[4] = { 0xB588, 0xB594, 0xB58C, 0xB59A };
    c->a[0] = at[dir];
    switch (dir) {                                  /* the whole bar: d0 x d1 blocks */
    case 0: c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6])); goto across;
    case 2: c->d[6] = set_w(c->d[6], sub_w(&c->ccr, c->d[1], c->d[6]));
    across:
        c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]);
        c->d[1] = 2; F_L(2);
        break;
    case 1: c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7])); goto down;
    default:
        c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]);
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 6, 16));
        c->d[7] = set_w(c->d[7], sub_w(&c->ccr, c->d[0], c->d[7]));
    down:
        c->d[0] = 2; F_L(2);
        break;
    }
    call(SET_MAP_BLOCKS);
    push32(0xB584); gate_timer(); c->a[7] += 4;
    gate_spark_time();
}
/* $B50C: every gate as the room loads */
static void gates_setup(void)
{
    c->a[4] = 0;
    c->d[0] = 15; F_L(15);
    do {
        F_B(rd8(g(G_FLAGS)));
        if (rd8(g(G_FLAGS)) & 0x80) {
            c->a[7] -= 2; wr16(c->a[7], c->d[0]); F_W(c->d[0]);
            push32(0xB51C); gate_setup(); c->a[7] += 4;
            c->d[0] = set_w(c->d[0], rd16(c->a[7])); c->a[7] += 2; F_W(c->d[0]);
        }
        c->a[4] += 4;
    } while (dbra(0));
}
/* $B5C6: a gate each frame */
static void gate_run(void)
{
    push32(0xB5CA); gate_sparks(); c->a[7] += 4;
    f_bit(rd8(g(G_FLAGS)) >> 1 & 1);
    if (rd8(g(G_FLAGS)) & 2) {                      /* moving */
        if (g_dec(G_TIMER)) return;
        f_bit(rd8(g(G_FLAGS)) >> 2 & 1);
        if (!(rd8(g(G_FLAGS)) & 4)) {               /* shutting */
            push32(0xB5E4); gate_grow(); c->a[7] += 4;
            move_b(0, rd8(g(G_OUT)));
            cmp_byte(rd8(g(G_LENGTH)), c->d[0]);
            if ((c->d[0] & 0xFF) < rd8(g(G_LENGTH))) { gate_timer(); return; }
            put8(g(G_WAIT), rd8(g(G_WAIT_SHUT)));
            g_and(G_FLAGS, 0xFD);
            gate_timer();
            return;
        }
        push32(0xB604); gate_shrink(); c->a[7] += 4;    /* opening */
        F_B(rd8(g(G_OUT)));
        if (rd8(g(G_OUT))) { gate_timer(); return; }
        push32(0xB610); gate_flag(); c->a[7] += 4;
        if (!(c->ccr & CC_Z)) { put8(g(G_FLAGS), 0); return; }
        put8(g(G_WAIT), rd8(g(G_WAIT_OPEN)));
        g_and(G_FLAGS, 0xF8);
        return;
    }
    push32(0xB62A); gate_flag(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) {                         /* its flag set: gone if open, else opening */
        f_bit(rd8(g(G_FLAGS)) & 1);
        if (!(rd8(g(G_FLAGS)) & 1)) { put8(g(G_FLAGS), 0); return; }
        g_or(G_FLAGS, 6);
        return;
    }
    F_B(rd8(g(G_WAIT)));
    if (rd8(g(G_WAIT))) {                           /* waiting */
        f_bit(rd8(g(G_FLAGS)) & 1);
        if (!(rd8(g(G_FLAGS)) & 1)) {               /* open: then shut */
            if (g_dec(G_WAIT)) return;
            put8(g(G_TIMER), 1);
            g_or(G_FLAGS, 3);
            gate_spark_time();
            return;
        }
        if (g_dec(G_WAIT)) goto flicker;            /* shut: then open */
        g_or(G_FLAGS, 6);
        return;
    }
flicker:
    if (g_dec(G_TIMER)) return;
    push32(0xB67E); gate_timer(); c->a[7] += 4;
    gate_end_next();
}
/* $B5AC: every gate each frame */
static void gates_run(void)
{
    c->a[4] = 0;
    c->d[0] = 15; F_L(15);
    do {
        F_B(rd8(g(G_FLAGS)));
        if (rd8(g(G_FLAGS)) & 0x80) {
            c->a[7] -= 2; wr16(c->a[7], c->d[0]); F_W(c->d[0]);
            push32(0xB5BC); gate_run(); c->a[7] += 4;
            c->d[0] = set_w(c->d[0], rd16(c->a[7])); c->a[7] += 2; F_W(c->d[0]);
        }
        c->a[4] += 4;
    } while (dbra(0));
}

/* $B82A: a crumbling wall at (d6, d7) in the first free record, stopping on one side (d0 bit 3:
 * the left) */
static void crumble_add(void)
{
    c->a[0] = WALLS;
    c->d[1] = 3; F_L(3);
    for (;;) {
        F_B(rd8(c->a[0]));
        if (!(rd8(c->a[0]) & 0x80)) break;
        c->a[0] += 8;
        if (!dbra(1)) return;
    }
    c->d[1] = 8; F_L(8);
    and_w(0, 8);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 5, 16));
    or_w(0, 0x8001);
    put16(0, c->d[0]);
    put16(0, 0);
    put16(0, c->d[6]);
    put16(0, c->d[7]);
}

ENTRY(GatesSetup, gates_setup)             /* $B50C */
ENTRY(GateSetup, gate_setup)               /* $B526 */
ENTRY(GatesRun, gates_run)                 /* $B5AC */
ENTRY(GateRun, gate_run)                   /* $B5C6 */
ENTRY(GateEnd, gate_end)                   /* $B68C */
ENTRY(GateFlag, gate_flag)                 /* $B702 */
ENTRY(GateTimer, gate_timer)               /* $B724 */
ENTRY(GateGrow, gate_grow)                 /* $B744 */
ENTRY(GateCell, gate_cell)                 /* $B754 */
ENTRY(GateShrink, gate_shrink)             /* $B79C */
ENTRY(GateSparkTime, gate_spark_time)      /* $B7A6 */
ENTRY(GateSparks, gate_sparks)             /* $B7B8 */
ENTRY(CrumbleAdd, crumble_add)             /* $B82A */

/* ---- batch T23: three room objects and a shattered block ----
 * The room's objects run each frame from $AEA2 (eight slots, a4 $E0-$FC), by kind ($D402, the
 * table at $AECC): kind 2 a broken floor's block falling, kind 8 a lift that throws the player
 * up, kind 14 a thing drifting off to the right from the player. Bit 6 of the flags: begun;
 * $D401 the step. */

#define APPLY_VEL_X   0x0AB8u
#define APPLY_VEL_Y   0x0AD6u
#define ACCEL_Y       0x0B44u
#define FLOOR_UNDER   0x3F54u       /* $9EDE: the tile under the entity's feet (bit 11: solid) */
#define THROW_UP      0xEC08u       /* the entity a4 thrown up at speed d0 */

static uint32_t e(uint32_t field) { return c->a[4] + field; }   /* an entity's field: $FFxxxx + a4 */
static void e_or(uint32_t field, uint8_t m) { uint8_t v = rd8(e(field)) | m; wr8(e(field), v); F_B(v); }
static uint8_t e_inc(uint32_t field, uint32_t n) { uint8_t v = (uint8_t)add_b(&c->ccr, n, rd8(e(field))); wr8(e(field), v); return v; }
static uint8_t e_dec(uint32_t field) { uint8_t v = (uint8_t)sub_b(&c->ccr, 1, rd8(e(field))); wr8(e(field), v); return v; }
static uint32_t abs_diff_w(uint32_t a, uint32_t b)   /* sub.w b,a; bcc; neg.w */
{
    uint32_t d = sub_w(&c->ccr, b, a);
    if (c->ccr & CC_C) d = neg_w(&c->ccr, d);
    return d;
}

/* $B0CC: the falling block's cell, on plane A */
static void fall_cell(void)
{
    c->d[6] = set_w(c->d[6], rd16(e(0xFFFFDB00u))); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(e(0xFFFFDB02u))); F_W(c->d[7]);
    c->a[3] = LAYER_A;
}
/* $B00A, kind 2: a broken floor's block ($B400: its cell at $DB00, its look at $DA00). It takes
 * the block's place, then on the next frame empties the cell and falls until it lands, when it
 * crumbles away */
static void room_obj_fall(void)
{
    static const uint8_t looks[6] = { 0x06, 0x02, 0x06, 0x06, 0x04, 0x00 };   /* $B070: the animation set and its animation */
    f_bit(rd8(e(0xFFFFC000u)) >> 6 & 1);
    if (!(rd8(e(0xFFFFC000u)) & 0x40)) {
        c->d[1] = set_w(c->d[1], rd16(e(0xFFFFDA00u))); F_W(c->d[1]);
        uint32_t k = c->d[1] & 0xFFFF;
        put8(e(0xFFFFC300u), looks[k]);
        put8(e(0xFFFFC301u), looks[k + 1]);
        move_b(0, looks[k + 2]);
        call(SET_ANIM);
        put8(e(0xFFFFC303u), 0x14);
        c->d[0] = set_w(c->d[0], rd16(e(0xFFFFDB00u))); F_W(c->d[0]);   /* the cell's middle */
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
        put16m(e(0xFFFFC800u), c->d[0]);
        c->d[0] = set_w(c->d[0], rd16(e(0xFFFFDB02u))); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 2, 16));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
        put16m(e(0xFFFFC900u), c->d[0]);
        put16m(e(0xFFFFCC02u), 0x41);                   /* gravity, falling speed */
        put16m(e(0xFFFFCD02u), 0x480);
        e_or(0xFFFFD100u, 2);
        e_or(0xFFFFC000u, 0x40);
        put8(e(0xFFFFCE02u), 1);
        push32(0xB068); fall_cell(); c->a[7] += 4;
        c->d[0] = set_w(c->d[0], 0x268); F_W(0x268);    /* the cell kept solid this frame */
        cell_set();
        return;
    }
    move_b(0, rd8(e(0xFFFFD401u)));
    if (!(c->d[0] & 0xFF)) {                            /* step 0: the cell emptied when the wait ends */
        if (e_dec(0xFFFFCE02u)) goto falls;
        e_inc(0xFFFFD401u, 1);
        push32(0xB088); fall_cell(); c->a[7] += 4;
        c->d[0] = 0; F_L(0);
        push32(0xB08E); cell_set_draw_if_shown(); c->a[7] += 4;
    } else {
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
        if (c->d[0] & 0xFF) {                           /* step 2: gone when its animation ends */
            F_B(rd8(e(0xFFFFC600u)));
            if (rd8(e(0xFFFFC600u))) return;
            put8(e(0xFFFFC000u), 0);
            return;
        }
    }
    f_bit(rd8(e(0xFFFFC000u)) & 1);                     /* step 1: falling while shown */
    if (!(rd8(e(0xFFFFC000u)) & 1)) { put8(e(0xFFFFC000u), 0); return; }
falls:
    call(ACCEL_Y);
    call(APPLY_VEL_Y);
    call(FLOOR_UNDER);
    f_bit(rd8(0xFFFF9EDFu) >> 3 & 1);
    if (!(rd8(0xFFFF9EDFu) & 8)) return;
    e_inc(0xFFFFD401u, 1);                              /* landed: crumbles */
    put16m(e(0xFFFFC300u), 0x1208);
    c->d[0] = 0; F_L(0);
    call(SET_ANIM);
}

/* $B0DA, kind 8: a lift at one of four places ($D300), on the floor at $10B0. Every 64 frames
 * it checks for the player within 16 pixels across (or, for the others, waits its time at $D800),
 * then rises. The player on it as it rises is thrown up (and its rider at $D302 told), with sound
 * 51; it falls back to the floor */
static void room_obj_lift(void)
{
    static const uint16_t xs[4] = { 0x1500, 0x1530, 0x1560, 0x1590 };      /* $B128 */
    static const uint16_t rises[4] = { 0xFB00, 0xFA80, 0xFA00, 0xF980 };   /* $B180 */
    f_bit(rd8(e(0xFFFFC000u)) >> 6 & 1);
    if (!(rd8(e(0xFFFFC000u)) & 0x40)) {
        call(CLEAR_ENTITY);
        c->d[0] = set_w(c->d[0], rd16(e(0xFFFFD300u))); F_W(c->d[0]);
        put16m(e(0xFFFFC800u), xs[(c->d[0] & 0xFFFF) / 2]);
        put16m(e(0xFFFFC900u), 0x10B0);
        put16m(e(0xFFFFCC02u), 0x40);
        put8(e(0xFFFFD100u), 0);
        put8(e(0xFFFFD800u), 0);
        put16m(e(0xFFFFC202u), 0x420);
        put16m(e(0xFFFFC300u), 0xA14);
        put8(e(0xFFFFC303u), 0x16);
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        put8(e(0xFFFFD401u), 0);
        e_or(0xFFFFC000u, 0x40);
        return;
    }
    move_b(0, rd8(e(0xFFFFD401u)));
    if (!(c->d[0] & 0xFF)) {                            /* step 0: waiting */
        F_W(rd16(e(0xFFFFD300u)));
        if (rd16(e(0xFFFFD300u))) {
            F_B(rd8(e(0xFFFFD800u)));
            if (!rd8(e(0xFFFFD800u))) return;
            if (e_dec(0xFFFFD800u)) return;
        } else {
            c->d[0] = set_w(c->d[0], rd16(0xFFFF965Au)); F_W(c->d[0]);
            and_w(0, 0x3F);
            if (c->d[0] & 0xFFFF) return;
            c->a[2] = sx(rd16(PLAYER_ENT));
            c->d[0] = set_w(c->d[0], rd16(e(0xFFFFC800u))); F_W(c->d[0]);
            c->d[0] = set_w(c->d[0], abs_diff_w(c->d[0], rd16(c->a[2] - 0x3800)));
            cmp_w(&c->ccr, 0x10, c->d[0]);
            if ((c->d[0] & 0xFFFF) < 0x10) return;
        }
        e_inc(0xFFFFD401u, 1);                          /* rising */
        c->d[0] = set_w(c->d[0], rd16(e(0xFFFFD300u))); F_W(c->d[0]);
        put16m(e(0xFFFFCA02u), rises[(c->d[0] & 0xFFFF) / 2]);
        c->d[0] = 2; F_L(2);
        call(SET_ANIM);
        return;
    }
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
    if (!(c->d[0] & 0xFF)) {                            /* step 1: rising, the player on it thrown */
        call(ACCEL_Y);
        call(APPLY_VEL_Y);
        c->a[2] = sx(rd16(PLAYER_ENT));
        cmp_w(&c->ccr, 0x10A0, rd16(e(0xFFFFC900u)));
        if (rd16(e(0xFFFFC900u)) >= 0x10A0) goto falling;
        c->d[0] = set_w(c->d[0], rd16(c->a[2] - 0x3800)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], abs_diff_w(c->d[0], rd16(e(0xFFFFC800u))));
        cmp_w(&c->ccr, 0x12, c->d[0]);
        if ((c->d[0] & 0xFFFF) >= 0x12) goto falling;
        c->d[2] = 0x16; F_L(0x16);
        c->d[0] = set_w(c->d[0], rd16(c->a[2] - 0x3700)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[2], c->d[0]));
        cmp_w(&c->ccr, rd16(e(0xFFFFC900u)), c->d[0]);
        if ((c->d[0] & 0xFFFF) < rd16(e(0xFFFFC900u))) goto falling;
        c->d[1] = set_w(c->d[1], rd16(c->a[2] - 0x2DFE)); F_W(c->d[1]);
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[2], c->d[1]));
        cmp_w(&c->ccr, rd16(e(0xFFFFD202u)), c->d[1]);
        if ((c->d[1] & 0xFFFF) >= rd16(e(0xFFFFD202u))) goto falling;
        put16m(e(0xFFFFC900u), c->d[0]);
        e_inc(0xFFFFD401u, 1);
        c->d[0] = set_w(c->d[0], rd16(e(0xFFFFD302u))); F_W(c->d[0]);
        if (c->d[0] & 0xFFFF) { c->a[0] = sx(c->d[0]); put8(c->a[0] - 0x2800, 0x18); }
        c->d[0] = set_w(c->d[0], 0xFA00); F_W(0xFA00);
        uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t;
        call(THROW_UP);
        t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t;
        c->d[0] = 0x33; F_L(0x33);
        call(QUEUE_SOUND);
        put16m(e(0xFFFFCA02u), 0);
        c->d[0] = 4; F_L(4);
        call(SET_ANIM);
        return;
    falling:
        F_W(rd16(e(0xFFFFCA02u)));
        if (rd16(e(0xFFFFCA02u)) & 0x8000) return;
    } else {                                            /* step 2: falling back */
        call(ACCEL_Y);
        call(APPLY_VEL_Y);
    }
    c->d[0] = set_w(c->d[0], 0x10B0); F_W(0x10B0);      /* down at the floor: waiting again */
    cmp_w(&c->ccr, rd16(e(0xFFFFC900u)), c->d[0]);
    if (rd16(e(0xFFFFC900u)) <= 0x10B0) return;
    put16m(e(0xFFFFC900u), c->d[0]);
    put16m(e(0xFFFFD202u), c->d[0]);
    c->d[0] = 0; F_L(0);
    put8(e(0xFFFFD401u), 0);
    put8(e(0xFFFFD800u), 0);
    call(SET_ANIM);
    put8(e(0xFFFFD401u), 0);
}

/* $B236, kind 14: a thing set off from the player (8 across, 2 down) drifting right, its frame
 * on every 4 frames to the fourth, gone at $1050 across */
static void room_obj_drift(void)
{
    f_bit(rd8(e(0xFFFFC000u)) >> 6 & 1);
    if (!(rd8(e(0xFFFFC000u)) & 0x40)) {
        call(CLEAR_ENTITY);
        put16m(e(0xFFFFC300u), 0x1622);
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        put8(e(0xFFFFC303u), 0xE);
        e_or(0xFFFFC002u, 0x80);
        c->a[0] = sx(rd16(PLAYER_ENT));
        c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x3800)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
        put16m(e(0xFFFFC800u), c->d[0]);
        c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x3700)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 2, c->d[0]));
        put16m(e(0xFFFFC900u), c->d[0]);
        put16m(e(0xFFFFD800u), 4);
        put16m(e(0xFFFFCA00u), 0x200);
        e_or(0xFFFFC000u, 0x40);
        return;
    }
    call(APPLY_VEL_X);
    cmp_w(&c->ccr, 0x1050, rd16(e(0xFFFFC800u)));
    if (rd16(e(0xFFFFC800u)) >= 0x1050) { put8(e(0xFFFFC000u), 0); return; }
    if (e_dec(0xFFFFD801u)) return;
    put8(e(0xFFFFD801u), 4);
    e_inc(0xFFFFD800u, 2);
    move_b(0, rd8(e(0xFFFFD800u)));
    cmp_byte(8, c->d[0]);
    if ((c->d[0] & 0xFF) >= 8) return;
    mem_add_w(e(0xFFFFCA00u), 1);
    call(SET_ANIM);
}

/* $B2BA: the block at cell (d6, d7) of plane A shattered: emptied, and four pieces ($206-$20C)
 * thrown from its quarters while there are free items */
static void block_shatter(void)
{
    link(-4);
    c->a[7] -= 2; wr16(c->a[7], c->a[4]);
    put16m(c->a[6] - 2, c->d[6]);
    put16m(c->a[6] - 4, c->d[7]);
    c->d[0] = 0; F_L(0);
    push32(0xB2CE); cell_set_a(); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], lsl_n(&c->ccr, c->d[6], 4, 16));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 4, c->d[6]));
    put16m(c->a[6] - 2, c->d[6]);
    c->d[7] = set_w(c->d[7], lsr_n(&c->ccr, c->d[7], 2, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 4, c->d[7]));
    put16m(c->a[6] - 4, c->d[7]);
    for (uint32_t k = 0; k < 4; k++) {
        c->d[1] = set_w(c->d[1], rd16(c->a[6] - 2)); F_W(c->d[1]);
        if (k & 1) c->d[1] = set_w(c->d[1], add_w(&c->ccr, 8, c->d[1]));
        c->d[2] = set_w(c->d[2], rd16(c->a[6] - 4)); F_W(c->d[2]);
        if (k & 2) c->d[2] = set_w(c->d[2], add_w(&c->ccr, 8, c->d[2]));
        c->d[0] = set_w(c->d[0], 0x206 + 2 * k); F_W(c->d[0]);
        call(SPAWN_ITEM);
        if (!(c->ccr & CC_Z)) break;
    }
    c->a[4] = sx(rd16(c->a[7])); c->a[7] += 2;
    unlk();
}

ENTRY(RoomObjFall, room_obj_fall)          /* $B00A */
ENTRY(FallCell, fall_cell)                 /* $B0CC */
ENTRY(RoomObjLift, room_obj_lift)          /* $B0DA */
ENTRY(RoomObjDrift, room_obj_drift)        /* $B236 */
ENTRY(BlockShatter, block_shatter)         /* $B2BA */
