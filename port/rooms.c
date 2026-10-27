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
#include <stdio.h>
#include <stdlib.h>
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
static int pack_blocks(uint32_t at, uint32_t to)   /* the files' block table at `at`, into `to` */
{
    const uint8_t *r = port_pack_find ? port_pack_find('B', at) : NULL;
    if (!r) return 0;
    unsigned n = r[0] << 8 | r[1];
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *b = r + 2 + 10 * i;
        if (!b[0]) continue;                        /* an empty block: left as it was */
        for (int k = 0; k < 8; k++) wr8(to + 8 * i + k, b[1 + k]);
        wr8(to + 0x800 + i, b[9]);
    }
    return 1;
}

static void unpack_blocks(void)
{
    if (pack_blocks(c->a[3], c->a[1])) return;
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
    const uint8_t *L = port_pack_find ? port_pack_find('L', c->a[4]) : NULL;
    if (L) {                                        /* the files: the map and its chunks decoded already */
        pack_blocks((uint32_t)L[0] << 24 | L[1] << 16 | L[2] << 8 | L[3], c->a[1]);
        for (int i = 0; i < L[4]; i++, chunk_no++) {
            const uint8_t *p = L + 5 + 385 * i;
            wr8(sx(map) + (int8_t)p[0], chunk_no);
            uint32_t at = 0xFF5000u + 0x180u * chunk_no;
            for (int k = 0; k < 384; k++) wr8(at + k, p[1 + k]);
        }
        c->a[7] = c->a[6]; c->a[6] = rd32(c->a[7]); c->a[7] += 4;   /* unlk a6 */
        c->a[3] = sx(rd16(c->a[7])); c->a[7] += 2;
        return;
    }
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

/* c is restored after: an entry of this file reached inside (with a copy of the registers, as
 * port/player.c passes) sets it to its own, which a routine that waits must not keep */
static void call(uint32_t addr) { Cpu *self = c; call_68k(c, addr); c = self; }
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
int rt_dispatch(Cpu *c, uint32_t target);
void rt_leave(Cpu *c, uint32_t from, uint32_t target);
/* a call to a routine that keeps its return address in RAM (the continuations' stack at $9C14)
 * and goes on there later: the original's return address pushed, so that RAM is the same, and
 * the routine's C version run; 1 if control came back there (by rts or by going on at it), 0 if
 * it went elsewhere (c->ret says where, the stack as it left it) */
static int call_ret(uint32_t addr, uint32_t ret)
{
    Cpu *self = c;
    const PortRoutine *p = 0;
    for (int i = 0; i < port_routine_count && !p; i++) if (port_routines[i].addr == addr) p = &port_routines[i];
    push32(ret);
    c->ret = 0;
    if (p) {
        p->fn(c); c = self;
        if (!c->ret) c->ret = pop32();
        else if (c->ret != ret) {                       /* it went on elsewhere: as m68k_rt.h's rt_after_hand */
            uint32_t t = c->ret;
            if (t & RT_TO_68K) rt_leave(c, 0, t & 0xFFFFFF);
            else if (!rt_dispatch(c, t)) rt_leave(c, 0, t);
        }
    } else if (!rt_dispatch(c, addr)) rt_leave(c, 0, addr);
    c = self;
    if (c->ret != ret) return 0;
    c->ret = 0;
    return 1;
}
/* the same where the routine always comes back (the message interpreter, the player, the ocarina) */
static void call_back(uint32_t addr, uint32_t ret)
{
    if (call_ret(addr, ret)) return;
    fprintf(stderr, "call_back: $%06X did not come back to $%06X (control went to $%06X)\n", (unsigned)addr, (unsigned)ret, (unsigned)c->ret);
    exit(3);
}
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

/* widescreen (world.h): the pixels beyond each side in this room, or 0. RAM is read only when
 * the host asks for it, so the harness's runs are the original's. */
int port_wide_px;
int (*port_wide_room)(int area, int room);
int port_wide_now(void)
{
    if (!port_wide_px || !port_wide_room) return 0;
    return port_wide_room(rd8(0xFFFF9668u), rd8(0xFFFF9669u)) ? port_wide_px : 0;
}
/* a narrow room widened by its painted sides (port_wide_room answers 2): the sides are walked
 * into, its exits beyond its columns moved out by as much (trig_exit) */
int port_wide_sides(void)
{
    if (!port_wide_px || !port_wide_room) return 0;
    return port_wide_room(rd8(0xFFFF9668u), rd8(0xFFFF9669u)) == 2 ? port_wide_px : 0;
}
static int wide_layer(void) { return port_wide_now(); }

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
    int w = wide_layer();
    if (w && !rd8(LAYER_A + 0x3A) && rd16(LAYER_A + 0x24) - 0x1000 >= 2 * w) {
        /* widescreen: the camera kept where plane A's wider view stays inside its map, so that
         * every layer, at its own parallax, stops where plane A does (a slower plane B went on
         * sliding at the room's ends after plane A had stopped) */
        uint16_t x = (uint16_t)(c->d[0] >> 16), lo = (uint16_t)(0x1000 + w), hi = (uint16_t)(rd16(LAYER_A + 0x24) - w);
        if (x < lo) c->d[0] = (uint32_t)lo << 16;
        else if (x >= hi) c->d[0] = (uint32_t)hi << 16;
    }
    move_b(1, rd8(c->a[3] + 0x3A));
    c->d[2] = set_w(c->d[2], rd16(c->a[3] + 0x24)); F_W(c->d[2]);
    parallax();
    if (w && (int)(c->d[2] & 0xFFFF) - 0x1000 >= 2 * w) {    /* widescreen: the wider view inside the layer's map */
        uint16_t x = (uint16_t)c->d[0], lo = (uint16_t)(0x1000 + w), hi = (uint16_t)((c->d[2] & 0xFFFF) - w);
        c->d[0] = set_w(c->d[0], x < lo ? lo : x > hi ? hi : x);
    }
    put16m(c->a[3] + 4, c->d[0]);
}
/* widescreen: the lines of the map shown above the play field (under the wide HUD's one row) */
#define WIDE_ABOVE 32
static void layer_y(void)
{
    c->d[0] = rd32(CAMERA_Y); F_L(c->d[0]);
    if (wide_layer() && !rd8(LAYER_A + 0x3B) && rd16(LAYER_A + 0x26) - 0x1000 >= WIDE_ABOVE) {
        /* widescreen: the camera it reads kept WIDE_ABOVE lines below the map's top, so the lines
         * under the HUD's row always show the map; it scrolled into them, a strip of sky coming
         * and going as Wonder Boy jumped near a room's top (area 5's town) */
        uint16_t y = (uint16_t)(c->d[0] >> 16), lo = 0x1000 + WIDE_ABOVE;
        if (y < lo) c->d[0] = (uint32_t)lo << 16;
    }
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
    int w = wide_layer();
    if (w) {                                        /* widescreen: those of the wider view */
        int x = rd16(c->a[3] + 4), lo = (x - w) >> 4, hi = (x + 256 + w + 15) >> 4;
        put16m(c->a[3] + 8, (uint16_t)lo);
        put16m(c->a[3] + 0xA, (uint16_t)(hi - lo));
        return;
    }
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
/* widescreen: the rows of the play field (its 160 lines from the y) and of the 64 lines beside the
 * HUD above it, down to the map's top, or above it where the layer has those rows painted (a place
 * below its height: map.c, margin_row): [*lo, *hi) */
static void rows_wide(int *lo, int *hi)
{
    int y = rd16(c->a[3] + 6), least = port_margin_top(c) ? 0x1000 - 12 * 16 : 0x1000;
    int top = y - 64 < least ? least : y - 64;
    *lo = top >> 4; *hi = ((y + 159) >> 4) + 1;
}
static void rows_screens(void)      /* $99F6: the rows shown, from the y */
{
    if (wide_layer()) {
        int lo, hi;
        rows_wide(&lo, &hi);
        put16m(c->a[3] + 0xC, (uint16_t)(lo << 6));
        c->d[0] = set_w(c->d[0], (uint16_t)((hi - lo) << 6));
        put16m(c->a[3] + 0xE, (uint16_t)((hi - lo) << 6));
        return;
    }
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
    port_margin_draw = wide_layer() != 0;           /* widescreen: the sides from a margin screen, where painted (map.c) */
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
    port_margin_draw = 0;
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
        if ((int16_t)(c->d[7] - 0x4000) < 0)        /* widescreen: rows above the top (map.c's margin_row), read as far as row 0 */
            c->d[0] = set_w(c->d[0], (uint16_t)(0x4000 - (c->d[7] & 0xFFFF)));
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

/* widescreen: the columns of the wider view [lo, hi) not drawn yet, to the right then to the
 * left, keeping at most the plane's 32 ($8: the first, $A: how many) */
static void columns_in_wide(int w)
{
    port_margin_draw = 1;                           /* the sides from a margin screen, where painted (map.c) */
    link(-6);
    int x = rd16(c->a[3] + 4), lo = (x - w) >> 4, hi = (x + 256 + w + 15) >> 4;
    int first = rd16(c->a[3] + 8), n = rd16(c->a[3] + 0xA);
    while (first + n < hi) {
        c->d[6] = set_w(c->d[6], (uint16_t)(first + n));
        push32(0x9D78); draw_column(); c->a[7] += 4;
        n++;
    }
    if (n > 32) { first += n - 32; n = 32; }
    while (lo < first) {
        first--;
        c->d[6] = set_w(c->d[6], (uint16_t)first);
        push32(0x9DAA); draw_column(); c->a[7] += 4;
        n++;
    }
    if (n > 32) n = 32;
    put16m(c->a[3] + 8, (uint16_t)first);
    put16m(c->a[3] + 0xA, (uint16_t)n);
    unlk();
    port_margin_draw = 0;
}

/* $9D46: the columns that scrolled into view drawn (to the right, or left), up to 32 kept ($8, $A) */
static void columns_in(void)
{
    int w = wide_layer();
    if (w) { columns_in_wide(w); return; }
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

/* widescreen: the rows of rows_wide not drawn yet, below then above, keeping at most the plane's
 * 16 ($C: the first, $E: how many, both in 64s) */
static void rows_in_wide(void)
{
    port_margin_draw = 1;
    link(-6);
    int lo, hi;
    rows_wide(&lo, &hi);
    int first = rd16(c->a[3] + 0xC) >> 6, n = rd16(c->a[3] + 0xE) >> 6;
    while (first + n < hi) {
        c->d[7] = set_w(c->d[7], (uint16_t)((first + n) << 6));
        push32(0x9E9A); draw_row(); c->a[7] += 4;
        n++;
    }
    if (n > 16) { first += n - 16; n = 16; }
    while (lo < first) {
        first--;
        c->d[7] = set_w(c->d[7], (uint16_t)(first << 6));
        push32(0x9ED4); draw_row(); c->a[7] += 4;
        n++;
    }
    if (n > 16) n = 16;
    put16m(c->a[3] + 0xC, (uint16_t)(first << 6));
    put16m(c->a[3] + 0xE, (uint16_t)(n << 6));
    unlk();
    port_margin_draw = 0;
}

/* $9E60: the rows that scrolled into view drawn (down, or up), up to $400 kept ($C, $E) */
static void rows_in(void)
{
    if (wide_layer()) { rows_in_wide(); return; }
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

/* $A92C: whether the player is in the zone (cs: yes; d2, d3 its place in it). zone_dx moves the
 * zone across (widescreen: an exit beyond a room's painted side; 0 in the original) */
static int zone_dx;
static void in_zone(void)
{
    move_b(1, rd8(c->a[2]));
    c->d[0] = 15;
    and_w(0, c->d[1]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(ZONE_X), c->d[0]));
    c->d[2] = set_w(c->d[2], (uint16_t)(rd16(c->a[4] - 0x3800) - zone_dx)); F_W(c->d[2]);
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
    int w = port_wide_sides();
    if (w) {                                        /* widescreen: an exit beyond the room's columns, out past its painted side */
        int x = rd16(ZONE_X) + (rd8(c->a[2]) & 15) * 16, right = 0x1000 + (rd16(LAYER_A + 0x44) + 1) * 16;
        zone_dx = x >= right ? w : x + rd16(ZONE_W) <= 0x1000 ? -w : 0;
    }
    push32(kind == 0 ? 0xA4F2 : kind == 1 ? 0xA50A : kind == 2 ? 0xA522 : 0xA532); in_zone(); c->a[7] += 4;
    zone_dx = 0;
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
static void door_animation_at(uint32_t ret, int closing, uint32_t at)
{
    uint32_t base = closing ? 0xBD66 : 0xBD1E;
    if (at == base) goto opening;
    if (at == base + 0x1A) goto moving;
    c->a[0] = sx(rd16(CONTINUATIONS));
    wr32(c->a[0], ret); c->a[0] += 4; F_L(ret);
    put16m(CONTINUATIONS, c->a[0]);
    c->d[0] = 35; F_L(35);
    call(QUEUE_SOUND);
    for (;;) {
        wait_frame(base);
opening:
        c->a[4] = 8;
        F_B(rd8(c->a[4] - 0x3A00));
        if (!rd8(c->a[4] - 0x3A00)) break;
    }
    put8(c->a[4] - 0x3CFD, closing ? 0x16 : 8);
    call(RESET_ANIM);
    for (;;) {
        wait_frame(base + 0x1A);
moving:
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
static void door_animation(uint32_t ret, int closing) { door_animation_at(ret, closing, 0); }
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
    c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);       /* as its last clr.w leaves them ($1115E promises no flags) */
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
/* batch N3c: resumed in their loops (a loaded state), then on at the address kept on the
 * continuations' stack */
#define DOOR_AT(name, closing, at) void name(Cpu *cpu) { c = cpu; NEEDS_PASS(at); door_animation_at(0, closing, at); port_continue_at(c, c->a[0], 0); }
DOOR_AT(DoorOpenAtBD1E, 0, 0xBD1E)
DOOR_AT(DoorOpenAtBD38, 0, 0xBD38)
DOOR_AT(DoorCloseAtBD66, 1, 0xBD66)
DOOR_AT(DoorCloseAtBD80, 1, 0xBD80)

/* ---- batch T19: the room changes' sequences: doors, warps, shops and houses ----
 * When a trigger's routine sets $9962, the game's task jumps (from its table at $91F4) to one of
 * these. They run on the task's coroutine, wait for frames at the original's resume addresses,
 * and leave as the original does: back to the game's loop ($8FFC), to the next room ($9100), or
 * to the exits' code ($F76C). */

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
static void frame_wait_at(uint32_t ret, int restart, uint32_t at)
{
    if (at) goto resumed;
    c->a[0] = sx(rd16(CONTINUATIONS));
    wr32(c->a[0], ret); c->a[0] += 4; F_L(ret);
    put16m(CONTINUATIONS, c->a[0]);
    c->d[0] = restart ? 34 : 35; F_L(c->d[0]);
    call(QUEUE_SOUND);
    if (restart) { c->a[4] = 8; call(RESET_ANIM); }
    do {
        wait_frame(restart ? 0xC16E : 0xC13C);
resumed:
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
static void door_then_next(uint32_t at)
{
    if (at == 0xBE44) goto rBE44;
    if (at == 0xBE4C) goto rBE4C;
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
rBE44:
    push32(0xBE48); door_blank(); c->a[7] += 4;
    door_animation(0xBE4C, 0);
rBE4C:
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
    door_then_next(0);
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
    door_then_next(0);
}
/* $BE50 (12): a warp, after 20 frames */
static void seq_warp(uint32_t at)
{
    if (at == 0xBE62) goto rBE62;
    push32(0xBE54); room_leave(); c->a[7] += 4;
    call(DOOR_KEEP);
    put16m(0xFFFF9C1Cu, 0x14);
    do { wait_frame(0xBE62); rBE62: ; } while (mem_sub_w(0xFFFF9C1Cu, 1));
    leave_to(NEXT_ROOM);
}
/* $BE6C (3): into a shop or house: its picture swapped in behind a frame that opens, its things,
 * then the picture growing from the middle every 3 frames */
static void seq_inside(uint32_t at)
{
    switch (at) {
    case 0xBE9C: goto rBE9C; case 0xBEBC: goto rBEBC; case 0xBED0: goto rBED0; case 0xBED4: goto rBED4;
    case 0xBED8: goto rBED8; case 0xBEE6: goto rBEE6; case 0xBEEA: goto rBEEA;
    }
    push32(0xBE70); reset_continuations(); c->a[7] += 4;
    call(DOOR_KEEP);
    uint8_t v = rd8(0xFFFF9659u); f_bit(v >> 4 & 1); wr8(0xFFFF9659u, v | 0x10);
    call(0x136A6); call(0xF99C); call(0x108FA);
    push32(0xBE8A); picture_setup(); c->a[7] += 4;
    push32(0xBE8E); rect_mark(); c->a[7] += 4;
    push32(0xBE92); picture_frame(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 0x16);
    wait_frame(0xBE9C);
rBE9C:
    push32(0xBEA0); picture_corner(); c->a[7] += 4;
    push32(0xBEA4); rect_redraw(); c->a[7] += 4;
    v = rd8(0xFFFF9659u); f_bit(v & 1); wr8(0xFFFF9659u, v | 1);
    call(SPAWN_ENEMIES);
    call(0x133B2);
    v = rd8(0xFFFF9659u); f_bit(v & 1); wr8(0xFFFF9659u, v & ~1);
    door_animation(0xBEBC, 0);
rBEBC:
    push32(0xBEC0); picture_frame_2(); c->a[7] += 4;
    wr16(0xFFFF9BE8u, 0); wr16(0xFFFF9BE6u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    push32(0xBECC); picture_put(); c->a[7] += 4;
    do {
        wait_frame(0xBED0);
rBED0:  wait_frame(0xBED4);
rBED4:  wait_frame(0xBED8);
rBED8:
        mem_add_w(0xFFFF9BE6u, 1);
        push32(0xBEE0); picture_grow(); c->a[7] += 4;
    } while (!(c->ccr & CC_Z));
    wait_frame(0xBEE6);
rBEE6:
    wait_frame(0xBEEA);
rBEEA:
    wr16(ROOM_CHANGE, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}
/* $BEF2 (4): out of it: the picture shrinking back (from the largest step still inside), the
 * frame closing, the room's triggers back */
static void seq_outside(uint32_t at)
{
    switch (at) {
    case 0xBF42: goto rBF42; case 0xBF46: goto rBF46; case 0xBF4A: goto rBF4A; case 0xBF5C: goto rBF5C;
    case 0xBF6A: goto rBF6A;
    }
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
        wait_frame(0xBF42);
rBF42:  wait_frame(0xBF46);
rBF46:  wait_frame(0xBF4A);
rBF4A:
        push32(0xBF4E); picture_grow(); c->a[7] += 4;
    } while (mem_sub_w(0xFFFF9BE6u, 1));
    push32(0xBF58); picture_frame(); c->a[7] += 4;
    door_animation(0xBF5C, 1);
rBF5C:
    call(0x13748);
    uint8_t v = rd8(0xFFFF9659u); f_bit(v >> 4 & 1); wr8(0xFFFF9659u, v & ~0x10);
    wait_frame(0xBF6A);
rBF6A:
    wr8(0xFFFFC008u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    put32m(TRIGGERS, rd32(0xFFFF9970u));
    push32(0xBF78); rect_redraw(); c->a[7] += 4;
    wr16(ROOM_CHANGE, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}
/* $BF80 (5): a house's door: the frame opens, the player walks through to the place inside (by
 * $127A's speed for $200), the frame closes behind */
static void seq_house(uint32_t at)
{
    switch (at) {
    case 0xBFCA: goto rBFCA; case 0xBFD2: goto rBFD2; case 0xBFE0: goto rBFE0; case 0xC056: goto rC056;
    case 0xC09C: goto rC09C; case 0xC0B2: goto rC0B2;
    }
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
rBFCA:
    push32(0xBFCE); door_blank(); c->a[7] += 4;
    door_animation(0xBFD2, 0);
rBFD2:
    player_a4();
    uint8_t v = rd8(c->a[4] - 0x3FFD) | 0x10; wr8(c->a[4] - 0x3FFD, v); F_B(v);
    wait_frame(0xBFE0);
rBFE0:
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
rC056:
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
rC09C:
    push32(0xC0A0); door_blank(); c->a[7] += 4;
    player_a4();
    call(DOOR_FREE);
    v = rd8(c->a[4] - 0x3FFD) & 0xEF; wr8(c->a[4] - 0x3FFD, v); F_B(v);
    door_animation(0xC0B2, 1);
rC0B2:
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
void SeqWarp(Cpu *cpu)      { c = cpu; NEEDS_PASS(0xBE50); seq_warp(0); }        /* $BE50 */
void SeqInside(Cpu *cpu)    { c = cpu; NEEDS_PASS(0xBE6C); seq_inside(0); }      /* $BE6C */
void SeqOutside(Cpu *cpu)   { c = cpu; NEEDS_PASS(0xBEF2); seq_outside(0); }     /* $BEF2 */
void SeqHouse(Cpu *cpu)     { c = cpu; NEEDS_PASS(0xBF80); seq_house(0); }       /* $BF80 */
static void frame_wait(uint32_t ret, int restart) { frame_wait_at(ret, restart, 0); }
static void frame_wait_68k(int restart)
{
    uint32_t ret = rd32(c->a[7]); c->a[7] += 4;
    frame_wait(ret, restart);
    port_continue_at(c, c->a[0], 0);
}
void FrameWait(Cpu *cpu)    { c = cpu; NEEDS_PASS(0xC128); frame_wait_68k(0); }   /* $C128 */
void FrameRestart(Cpu *cpu) { c = cpu; NEEDS_PASS(0xC152); frame_wait_68k(1); }   /* $C152 */
void FrameWaitAtC13C(Cpu *cpu)    { c = cpu; NEEDS_PASS(0xC13C); frame_wait_at(0, 0, 0xC13C); port_continue_at(c, c->a[0], 0); }   /* batch N3c */
void FrameRestartAtC16E(Cpu *cpu) { c = cpu; NEEDS_PASS(0xC16E); frame_wait_at(0, 1, 0xC16E); port_continue_at(c, c->a[0], 0); }

/* ---- batch T20: arriving in a room ----
 * After loading a room the game's task jumps by how the player arrived ($9678, its table at
 * $8F6C) to one of these: through a door (its frame closing behind), into a shop or house (its
 * picture already in), walking out of a building, or a plain fade in. Walking into a building
 * ($C184) is a room change ($9962 6). Their helpers keep their callers' return addresses on the
 * continuations' stack, as the original's do. $C67E is not registered: no start point has its
 * kind (6), so the game never runs it. */

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
static void fade_in_wait_at(uint32_t ret, uint32_t at)
{
    if (at == 0xC5E4) goto rC5E4;
    push_cont(ret);
    call(0x581A);
    do {
        wait_frame(0xC5E4);
rC5E4:
        f_bit(rd8(0xFFFF8C56u) >> 4 & 1);
    } while (!(rd8(0xFFFF8C56u) & 0x10));
    pop_cont();
}
static void fade_in_wait(uint32_t ret) { fade_in_wait_at(ret, 0); }
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
static void arrive_door(uint32_t at)        /* $C5F8 (2, 4): through a door, its frame closing */
{
    switch (at) {
    case 0xC600: goto rC600;
    case 0xC604: goto rC604;
    case 0xC608: goto rC608;
    }
    push32(0xC5FC); reset_continuations(); c->a[7] += 4;
    door_at_player(0xC600);
rC600:
    fade_in_wait(0xC604);
rC604:
    door_animation(0xC608, 1);
rC608:
    push32(0xC60C); door_restore(); c->a[7] += 4;
    wr8(0xFFFFC008u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}
static void arrive_still(uint32_t at)       /* $C614 (10): a fade in, then 20 frames */
{
    switch (at) {
    case 0xC620: goto rC620;
    case 0xC62A: goto rC62A;
    }
    push32(0xC618); reset_continuations(); c->a[7] += 4;
    call(DOOR_FREE);
    fade_in_wait(0xC620);
rC620:
    put16m(0xFFFF9C1Cu, 0x14);
    do { wait_frame(0xC62A); rC62A: ; } while (mem_sub_w(0xFFFF9C1Cu, 1));
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
static void arrive_walk_out(uint32_t at)    /* $C698 (5): walking out of a building, to its door's right */
{
    switch (at) {
    case 0xC6AA: goto rC6AA;
    case 0xC6B6: goto rC6B6;
    case 0xC6BE: goto rC6BE;
    case 0xC6C2: goto rC6C2;
    case 0xC6C6: goto rC6C6;
    case 0xC6DE: goto rC6DE;
    }
    push32(0xC69C); reset_continuations(); c->a[7] += 4;
    player_a4();
    mem_sub_w(c->a[4] - 0x3800, 0x18);
    fade_in_wait(0xC6AA);
rC6AA:
    c->a[0] = 0xC6EEu;
    push32(0xC6B2); walk_in_setup(); c->a[7] += 4;
    wait_frame(0xC6B6);
rC6B6:
    push32(0xC6BA); door_blank(); c->a[7] += 4;
    frame_wait(0xC6BE, 0);
rC6BE:
    for (;;) {
        wait_frame(0xC6C2);
rC6C2:
        call_back(0xDD08, 0xC6C6);
rC6C6:
        player_a4();
        c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x3800)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(0xFFFF9BD6u), c->d[0]));
        cmp_w(&c->ccr, 0x14, c->d[0]);
        if ((int16_t)c->d[0] > 0x14) break;
    }
    frame_wait(0xC6DE, 1);
rC6DE:
    push32(0xC6E2); door_restore(); c->a[7] += 4;
    wr8(0xFFFFC008u, 0); wr8(0xFFFF9F05u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    leave_to(GAME_LOOP);
}
static void arrive_inside_door(uint32_t at) /* $C6F6 (6, 7): inside, through its door */
{
    switch (at) {
    case 0xC6FE: goto rC6FE;
    case 0xC702: goto rC702;
    case 0xC706: goto rC706;
    case 0xC70A: goto rC70A;
    }
    push32(0xC6FA); reset_continuations(); c->a[7] += 4;
    inside_setup(0xC6FE);
rC6FE:
    door_at_player(0xC702);
rC702:
    fade_in_wait(0xC706);
rC706:
    door_animation(0xC70A, 1);
rC70A:
    push32(0xC70E); door_restore(); c->a[7] += 4;
    push32(0xC712); picture_frame_2(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 8);
    leave_to(GAME_LOOP);
}
static void arrive_fade(uint32_t at)        /* $C71C (8): a fade in, then the exits' arrival ($F7EA) */
{
    switch (at) {
    case 0xC724: goto rC724;
    }
    push32(0xC720); reset_continuations(); c->a[7] += 4;
    fade_in_wait(0xC724);
rC724:
    leave_to(0xF7EA);
}
static void arrive_talk(uint32_t at)        /* $C728 (9): inside, then a conversation after 30 frames ($C1E8) */
{
    switch (at) {
    case 0xC730: goto rC730;
    case 0xC73E: goto rC73E;
    case 0xC752: goto rC752;
    }
    push32(0xC72C); reset_continuations(); c->a[7] += 4;
    inside_setup(0xC730);
rC730:
    push32(0xC734); picture_frame_2(); c->a[7] += 4;
    put8(c->a[4] - 0x3CFD, 8);
    fade_in_wait(0xC73E);
rC73E:
    uint8_t v = rd8(0xFFFF965Fu); f_bit(v & 1); wr8(0xFFFF965Fu, v & ~1);
    if (v & 1) { leave_to(0xF7EA); return; }
    put16m(0xFFFF9C32u, 0x1E);
    do { wait_frame(0xC752); rC752: ; } while (mem_sub_w(0xFFFF9C32u, 1));
    leave_to(0xC1E8);
}
/* $C184 (6): walking into a building to start point $9982 (going left), then $9984's room */
static void seq_walk_in(uint32_t at)
{
    switch (at) {
    case 0xC1A4: goto rC1A4; case 0xC1AC: goto rC1AC; case 0xC1B0: goto rC1B0; case 0xC1B4: goto rC1B4;
    case 0xC1CC: goto rC1CC;
    }
    push32(0xC188); reset_continuations(); c->a[7] += 4;
    put16m(0xFFFF9676u, rd16(0xFFFF9982u));
    c->a[0] = 0xC1E0u;
    push32(0xC196); walk_in_setup(); c->a[7] += 4;
    player_a4();
    c->d[0] = 0; F_L(0);
    call(SET_ANIM);
    wait_frame(0xC1A4);
rC1A4:
    push32(0xC1A8); door_blank(); c->a[7] += 4;
    frame_wait(0xC1AC, 0);
rC1AC:
    for (;;) {
        wait_frame(0xC1B0);
rC1B0:
        call_back(0xDD08, 0xC1B4);
rC1B4:
        player_a4();
        c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x3800)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(0xFFFF9BD6u), c->d[0]));
        cmp_w(&c->ccr, 0xFFE8, c->d[0]);
        if ((int16_t)c->d[0] < -0x18) break;
    }
    frame_wait(0xC1CC, 1);
rC1CC:
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
void ArriveDoor(Cpu *cpu)       { c = cpu; NEEDS_PASS(0xC5F8); arrive_door(0); }          /* $C5F8 */
void ArriveStill(Cpu *cpu)      { c = cpu; NEEDS_PASS(0xC614); arrive_still(0); }         /* $C614 */
void ArriveInside(Cpu *cpu)     { c = cpu; NEEDS_PASS(0xC67E); arrive_inside(); }        /* $C67E */
void ArriveWalkOut(Cpu *cpu)    { c = cpu; NEEDS_PASS(0xC698); arrive_walk_out(0); }      /* $C698 */
void ArriveInsideDoor(Cpu *cpu) { c = cpu; NEEDS_PASS(0xC6F6); arrive_inside_door(0); }   /* $C6F6 */
void ArriveFade(Cpu *cpu)       { c = cpu; NEEDS_PASS(0xC71C); arrive_fade(0); }          /* $C71C */
void ArriveTalk(Cpu *cpu)       { c = cpu; NEEDS_PASS(0xC728); arrive_talk(0); }          /* $C728 */
void SeqWalkIn(Cpu *cpu)        { c = cpu; NEEDS_PASS(0xC184); seq_walk_in(0); }          /* $C184 */

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

/* ---- batch T24: the rooms' scripts (indexes 0 and 1), a dropped thing, the drops ----
 * Some rooms have a script of their own (room command 13: $9682, -1 for none). $C75C runs its
 * setup as the room loads (the table at $C784); $C7E6 runs its part each frame (the table at
 * $C838), on the continuations' stack. Index 0 is area 15's room 1, index 1 area 12's room 9 and
 * area 17's rooms 14 and 16. */

#define ROOM_SCRIPT   0xFFFF9682u
#define SCRIPT_VARS   0xFFFF99DEu   /* $99DE-$99FD: the script's own, cleared as the room loads */
#define DROPS         0xFFFF9A00u   /* bit 7: dropping; $9A01 the wait, $9A02 the list, $9A06 at */
#define ROOM_OBJECTS  0xAEA2u
#define FREEZE        0x0A66u
#define UNFREEZE      0x0A8Eu

static uint8_t mem_dec8(uint32_t a) { uint8_t v = (uint8_t)sub_b(&c->ccr, 1, rd8(a)); wr8(a, v); return v; }
static void mem_or8(uint32_t a, uint8_t m) { uint8_t v = rd8(a) | m; wr8(a, v); F_B(v); }
static void mem_and8(uint32_t a, uint8_t m) { uint8_t v = rd8(a) & m; wr8(a, v); F_B(v); }
static int mem_bclr(uint32_t a, int bit) { uint8_t v = rd8(a); f_bit(v >> bit & 1); wr8(a, (uint8_t)(v & ~(1 << bit))); return v >> bit & 1; }

/* $B42A (from $1091C: an item of $1000 or more): a thing dropped at (d1, d2) for the player to
 * pick up, which starts room change 9 ($9962). Item $40 is $102A once $9F00 reaches $E00; $102A
 * or $2A already owned is item $1A instead. d0 -1: a room change is under way */
static void drop_thing(void)
{
    cmp_byte(0x40, c->d[0]);
    if ((c->d[0] & 0xFF) == 0x40) {
        cmp_w(&c->ccr, 0xE00, rd16(0xFFFF9F00u));
        if (rd16(0xFFFF9F00u) < 0xE00) goto drop;
        c->d[0] = set_w(c->d[0], 0x102A); F_W(0x102A);
    } else {
        cmp_byte(0x2A, c->d[0]);
        if ((c->d[0] & 0xFF) != 0x2A) goto drop;
    }
    c->a[7] -= 8;                                       /* movem.w d0-d2/d6 */
    wr16(c->a[7], c->d[0]); wr16(c->a[7] + 2, c->d[1]); wr16(c->a[7] + 4, c->d[2]); wr16(c->a[7] + 6, c->d[6]);
    call(OWNS);
    c->d[0] = sx(rd16(c->a[7])); c->d[1] = sx(rd16(c->a[7] + 2)); c->d[2] = sx(rd16(c->a[7] + 4)); c->d[6] = sx(rd16(c->a[7] + 6));
    c->a[7] += 8;
    if (!(c->ccr & CC_Z)) {
        c->d[0] = 0x1A; F_L(0x1A);
        call(0x10916);                                  /* $1090C's item from d0 at (d1, d2), d6 kept */
        return;
    }
drop:
    F_W(rd16(0xFFFF9962u));
    if (rd16(0xFFFF9962u)) { c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]); return; }
    c->d[3] = set_w(c->d[3], c->d[0]); F_W(c->d[0]);
    c->a[3] = c->a[4];
    call(FIND_EFFECT);
    if (c->ccr & CC_N) { c->a[4] = c->a[3]; return; }
    put8(c->a[4] - 0x4000, 0x80);
    put16m(c->a[4] - 0x3800, c->d[1]);
    put16m(c->a[4] - 0x3700, c->d[2]);
    and_w(3, 0xFF);
    put16m(c->a[4] - 0x2300, c->d[3]);
    put16m(0xFFFF9968u, c->a[4]);
    put16m(c->a[4] - 0x2BFE, 4);                        /* a room object doing nothing */
    put16m(0xFFFF9962u, 9);
    c->d[0] = 0; F_L(0);
    c->a[4] = c->a[3];
}

/* $C75C: as a room loads: the script's variables cleared and its setup run */
static void room_script_setup(void)
{
    static const uint32_t setups[24] = {
        0xC8F4, 0xCA56, 0xCB5C, 0xCB6C, 0xCB7C, 0xCCF2, 0xCDDE, 0xC7E4, 0xC7E4, 0xCF48, 0xCFE8, 0xD00C,
        0xD194, 0xD388, 0xD71A, 0xD72A, 0x17FEC, 0x19612, 0x1898C, 0xC7E4, 0xD976, 0xDA00, 0xDA90, 0xDA9A,
    };
    c->d[0] = 0; F_L(0);
    c->d[1] = 7; F_L(7);
    c->a[0] = SCRIPT_VARS;
    do { put32m(c->a[0], c->d[0]); c->a[0] += 4; } while (dbra(1));
    c->d[0] = set_w(c->d[0], rd16(ROOM_SCRIPT)); F_W(c->d[0]);
    if (!(c->d[0] & 0x8000)) {
        uint32_t i = c->d[0] & 0xFFFF;
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
        c->a[0] = setups[i];
        call(c->a[0]);
    }
    put16m(DROPS, 0);
}

/* $C89A, each frame: things dropped one after another from the list at $9A02 (an item, then
 * the frames to the next; 0 ends it; items from $80 are $1000 on), at ($9A06, $9A08) */
static void drops(uint32_t ret)
{
    push_cont(ret);
    F_B(rd8(DROPS));
    if (!(rd8(DROPS) & 0x80)) goto done;
    F_B(rd8(DROPS + 1));
    if (rd8(DROPS + 1) && mem_dec8(DROPS + 1)) goto done;
    c->a[0] = rd32(DROPS + 2);
    c->d[0] = 0;
    move_b(0, rd8(c->a[0]));
    if (c->d[0] & 0x80) c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0xF80, c->d[0]));
    c->d[1] = set_w(c->d[1], rd16(DROPS + 6)); F_W(c->d[1]);
    c->d[2] = set_w(c->d[2], rd16(DROPS + 8)); F_W(c->d[2]);
    call(SPAWN_ITEM);
    if (c->ccr & CC_N) goto done;                       /* no free item: again next frame */
    c->a[0] = rd32(DROPS + 2) + 1;
    move_b(0, rd8(c->a[0])); c->a[0]++;
    put32m(DROPS + 2, c->a[0]);
    put8(DROPS + 1, c->d[0]);
    if (!(c->d[0] & 0xFF)) put8(DROPS, c->d[0]);
done:
    pop_cont();
}

/* index 0: area 15's room 1. $C8F4 as it loads: the block at ($104, $4480) gone until game flag
 * 0, and $9695 6 once flag 1 is set */
static void room0_setup(void)
{
    c->d[0] = 0; F_L(0);
    push32(0xC8FA); flag_test(); c->a[7] += 4;
    if (c->ccr & CC_Z) {
        c->a[3] = LAYER_A;
        c->d[0] = 0; F_L(0);
        c->d[6] = set_w(c->d[6], 0x104); F_W(0x104);
        c->d[7] = set_w(c->d[7], 0x4480); F_W(0x4480);
        push32(0xC90E); cell_set(); c->a[7] += 4;
    }
    c->d[0] = 1; F_L(1);
    push32(0xC914); flag_test(); c->a[7] += 4;
    if (c->ccr & CC_Z) return;
    put8(0xFFFF9695u, 6);
}
/* $C9B4: 22 room objects (kind 0, each its own look from $CA40) let out every 8 frames, the
 * others frozen, until they are all gone */
static void room0_release_at(uint32_t ret, uint32_t at)
{
    static const uint8_t looks[22] = { 0x24, 0x1B, 0x18, 0x06, 0x27, 0x12, 0x09, 0x1E, 0x15, 0x0C, 0x00,
                                       0x21, 0x0F, 0x03, 0x30, 0x39, 0x2A, 0x3F, 0x33, 0x36, 0x2D, 0x3C };
    if (!at) push_cont(ret);
    if (at == 0xCA2E) goto rCA2E;
    call(FREEZE);
    put32m(0xFFFF99E0u, 0);                             /* $99E0 how many, $99E2 the wait */
    for (;;) {
        cmp_w(&c->ccr, 0x16, rd16(0xFFFF99E0u));
        if (rd16(0xFFFF99E0u) >= 0x16) {                /* all out: until none is left */
            c->a[4] = 0xE0;
            c->d[0] = 7; F_L(7);
            do {
                F_B(rd8(c->a[4] - 0x4000));
                if (rd8(c->a[4] - 0x4000) & 0x80) {
                    F_W(rd16(c->a[4] - 0x2BFE));
                    if (!rd16(c->a[4] - 0x2BFE)) goto next;
                }
                c->a[4] += 4;
            } while (dbra(0));
            break;
        }
        F_W(rd16(0xFFFF99E2u));
        if (rd16(0xFFFF99E2u)) { mem_sub_w(0xFFFF99E2u, 1); goto next; }
        call(FIND_EFFECT);
        if (c->ccr & CC_N) goto next;
        put16m(0xFFFF99E2u, 8);
        put8(c->a[4] - 0x4000, 0x80);
        put16m(c->a[4] - 0x2BFE, 0);
        c->d[0] = set_w(c->d[0], rd16(0xFFFF99E0u)); F_W(c->d[0]);
        put8(c->a[4] - 0x25FE, looks[c->d[0] & 0xFFFF]);
        mem_add_w(0xFFFF99E0u, 1);
        c->d[0] = 7; F_L(7);
        c->d[0] = set_b(c->d[0], 7 & rd8(0xFFFF99E0u)); F_B(c->d[0]);   /* the count's high byte: a sound every time */
        if (c->d[0] & 0xFF) goto next;
        c->d[0] = 0x4C; F_L(0x4C);
        call(QUEUE_SOUND);
    next:
        call(ROOM_OBJECTS);
        wait_frame(0xCA2E);
rCA2E: ;
    }
    call(UNFREEZE);
    pop_cont();
}
static void room0_release(uint32_t ret) { room0_release_at(ret, 0); }
/* $C91E, each frame: $9995 bit 4 sets game flag 0 and puts the block back ($B3); bit 5 sets
 * flag 1 and lets the room objects out; bit 0, once, the effects at $C982 */
static void room0_frame_at(uint32_t ret, uint32_t at)
{
    if (!at) push_cont(ret);
    if (at == 0xC958) goto rC958;
    if (mem_bclr(0xFFFF9995u, 4)) {
        c->d[0] = 0; F_L(0);
        push32(0xC936); flag_set(); c->a[7] += 4;
        c->d[0] = set_w(c->d[0], 0xB3); F_W(0xB3);
        c->d[6] = set_w(c->d[6], 0x104); F_W(0x104);
        c->d[7] = set_w(c->d[7], 0x4480); F_W(0x4480);
        push32(0xC946); cell_set_a(); c->a[7] += 4;
    }
    if (mem_bclr(0xFFFF9995u, 5)) {
        c->d[0] = 1; F_L(1);
        push32(0xC954); flag_set(); c->a[7] += 4;
        room0_release(0xC958);
rC958:
    }
    f_bit(rd8(SCRIPT_VARS) & 1);
    if (!(rd8(SCRIPT_VARS) & 1)) {
        f_bit(rd8(0xFFFF9995u) & 1);
        if (rd8(0xFFFF9995u) & 1) {
            mem_or8(SCRIPT_VARS, 1);
            c->a[0] = 0xC982;
            call(0x27FA);
        }
    }
    pop_cont();
}
static void room0_frame(uint32_t ret) { room0_frame_at(ret, 0); }

/* index 1: area 12's room 9, area 17's rooms 14 and 16. $CA56 as it loads: $9F05 6 and the
 * player's frame routine $DB36 */
static void room1_setup(void)
{
    put8(0xFFFF9F05u, 6);
    put32m(0xFFFF9F22u, 0xDB36);
}
/* $CB1C: the player's animation 0 or 2 by $99DF bit 4, which flips */
static void room1_flip(void)
{
    c->d[1] = 0x10; F_L(0x10);
    move_b(0, rd8(0xFFFF99DFu));
    { uint8_t v = rd8(0xFFFF99DFu) ^ 0x10; wr8(0xFFFF99DFu, v); F_B(v); }
    and_b(0, 0x10);
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 3, 16));
    c->a[4] = sx(rd16(PLAYER_ENT));
    call(SET_ANIM);
}
/* $CB46: the palette's colour $8C18 put in */
static void room1_colour(void)
{
    mem_and8(0xFFFF8C56u, 0x3F);
    mem_or8(0xFFFF8C56u, 0x81);
}
/* $CB32: the next of four flashing colours */
static void room1_flash(void)
{
    static const uint16_t colours[4] = { 0x0EEE, 0x00EE, 0x0EEE, 0x0E0E };   /* $CB54 */
    c->d[0] = set_w(c->d[0], rd16(0xFFFF99E4u)); F_W(c->d[0]);
    put16m(0xFFFF8C18u, colours[(c->d[0] & 0xFFFF) / 2]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 2, c->d[0]));
    and_w(0, 6);
    put16m(0xFFFF99E4u, c->d[0]);
    room1_colour();
}
/* $CA66, each frame: once (unless $A0DE is negative), the player changes: sound 68, its look
 * flipping 6, 12 and 24 times, each 8, 4 and 2 frames, with the screen flashing; then its form
 * ($9EF1 bit 4) changed and set up again */
static void room1_frame_at(uint32_t ret, uint32_t at)
{
    static const uint8_t flips[3] = { 6, 12, 24 }, frames[3] = { 8, 4, 2 };   /* $CB16, $CB19 */
    if (!at) push_cont(ret);
    switch (at) {
    case 0xCAC0: goto rCAC0;
    case 0xCAE2: goto rCAE2;
    case 0xCB06: goto rCB06;
    }
    F_B(rd8(SCRIPT_VARS));
    if (rd8(SCRIPT_VARS) & 0x80) goto done;
    F_W(rd16(0xFFFFA0DEu));
    if (rd16(0xFFFFA0DEu) & 0x8000) goto done;
    c->d[0] = 0x44; F_L(0x44);
    call(QUEUE_SOUND);
    wr8(SCRIPT_VARS, 0xFF);
    put8(SCRIPT_VARS + 1, rd8(0xFFFF9EF1u));
    c->a[4] = sx(rd16(PLAYER_ENT));
    put16m(c->a[4] - 0x3D00, 0x161C);
    put16m(c->a[4] - 0x3DFE, 0x6E0);
    c->d[0] = 0; F_L(0);
    for (;;) {
        put16m(0xFFFF99E0u, c->d[0]);
        put8(0xFFFF99E2u, flips[c->d[0] & 0xFFFF]);
        do {
            c->d[0] = set_w(c->d[0], rd16(0xFFFF99E0u)); F_W(c->d[0]);
            put8(0xFFFF99E3u, frames[c->d[0] & 0xFFFF]);
            push32(0xCABA); room1_flip(); c->a[7] += 4;
            do {
                push32(0xCABC); room1_flash(); c->a[7] += 4;
                wait_frame(0xCAC0);
rCAC0: ;
            } while (mem_dec8(0xFFFF99E3u));
        } while (mem_dec8(0xFFFF99E2u));
        c->d[0] = set_w(c->d[0], rd16(0xFFFF99E0u)); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0]));
        cmp_w(&c->ccr, 3, c->d[0]);
        if ((c->d[0] & 0xFFFF) >= 3) break;
    }
    put16m(0xFFFF8C18u, 0);
    push32(0xCADE); room1_colour(); c->a[7] += 4;
    wait_frame(0xCAE2);
rCAE2:
    { uint8_t v = rd8(0xFFFF9EF1u) ^ 0x10; wr8(0xFFFF9EF1u, v); F_B(v); }
    call(0xF5FC);
    mem_or8(0xFFFF9F02u, 0x10);
    c->a[4] = sx(rd16(PLAYER_ENT));
    put16m(c->a[4] - 0x3DFE, 0);
    call(0x86BC);
    call(0x8BB6);
    call_back(0xDD08, 0xCB06);
rCB06:
    put8(0xFFFF9F05u, 0);
done:
    pop_cont();
}
static void room1_frame(uint32_t ret) { room1_frame_at(ret, 0); }

ENTRY(DropThing, drop_thing)               /* $B42A */
ENTRY(RoomScriptSetup, room_script_setup)  /* $C75C */
ENTRY(Room0Setup, room0_setup)             /* $C8F4 */
ENTRY(Room1Setup, room1_setup)             /* $CA56 */
ENTRY(Room1Flip, room1_flip)               /* $CB1C */
ENTRY(Room1Flash, room1_flash)             /* $CB32 */
ENTRY(Room1Colour, room1_colour)           /* $CB46 */
static void nothing(void) {}
ENTRY(ScriptNone, nothing)                 /* $C7E4: no setup */
ENTRY(ScriptFrameNone, nothing)            /* $C898: nothing each frame */
/* on the continuations' stack: they leave by port_continue_at */
void Drops(Cpu *cpu)        { c = cpu; cont_68k(drops); }                                  /* $C89A */
void Room0Frame(Cpu *cpu)   { c = cpu; NEEDS_PASS(0xC91E); cont_68k(room0_frame); }        /* $C91E */
void Room0Release(Cpu *cpu) { c = cpu; NEEDS_PASS(0xC9B4); cont_68k(room0_release); }      /* $C9B4 */
void Room1Frame(Cpu *cpu)   { c = cpu; NEEDS_PASS(0xCA66); cont_68k(room1_frame); }        /* $CA66 */

/* ---- batch T25: the rooms' scripts 2-4 and 14: a lift between two floors ----
 * Indexes 2, 3 and 4 (area 15's rooms 3, 8 and 9) each have a lift (a room object of kind $1A,
 * made by $11BF0) between two floors, with a switch at each floor (two cells a switch, $CCD2).
 * $99E0 is the lift's x, $99E2 and $99E4 the floors, $99DE bit 0 the lift at the lower floor
 * and $99DF which side each switch is on (bit 0 the upper's, bit 1 the lower's). Index 14 (area
 * 16's room 3) runs the same each frame. */

/* $CC8E: switch d0's cells (0-3: the lift there, 4-7: called; bit 1 the lower floor's) */
static void lift_switch(void)
{
    static const uint16_t cells[8][2] = {
        { 0x2E2, 0x2E3 }, { 0x2E4, 0x2E5 }, { 0x2E2, 0x2E3 }, { 0x2E4, 0x2E5 },
        { 0x2E0, 0x2E0 }, { 0x2E1, 0x2E1 }, { 0x2E0, 0x2E0 }, { 0x2E1, 0x2E1 },
    };
    c->d[6] = set_w(c->d[6], rd16(0xFFFF99E0u)); F_W(c->d[6]);
    c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, c->d[6], 4, 16));
    c->d[7] = set_w(c->d[7], rd16(0xFFFF99E2u)); F_W(c->d[7]);
    c->d[1] = 0; F_L(0);
    f_bit(c->d[0] >> 1 & 1);
    if (c->d[0] & 2) {
        c->d[7] = set_w(c->d[7], rd16(0xFFFF99E4u)); F_W(c->d[7]);
        c->d[1] = 1; F_L(1);
    }
    f_bit(rd8(0xFFFF99DFu) >> (c->d[1] & 7) & 1);
    if (rd8(0xFFFF99DFu) >> (c->d[1] & 7) & 1) c->d[6] = set_w(c->d[6], add_w(&c->ccr, 4, c->d[6]));   /* right of the lift */
    else c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    c->d[7] = set_w(c->d[7], sub_w(&c->ccr, 0x20, c->d[7]));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 2, 16));
    uint32_t i = c->d[0] & 7;
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    c->a[0] = 0xCCD4u + 4 * i;
    c->d[0] = set_w(c->d[0], cells[i][0]); F_W(c->d[0]);
    c->a[7] -= 4; wr32(c->a[7], c->a[0]); F_L(c->a[0]);
    push32(0xCCC6); cell_set_a(); c->a[7] += 4;
    c->a[0] = pop32();
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    c->d[0] = set_w(c->d[0], cells[i][1]); F_W(c->d[0]); c->a[0] += 2;
    cell_set_draw_if_shown();
}
/* $CC1C: the lift from the four words at a0 (x, the floors, $99DE): made at the floor nearer
 * the player, its switches drawn */
static void lift_setup(void)
{
    c->d[6] = set_w(c->d[6], next16(0)); put16m(0xFFFF99E0u, c->d[6]);
    c->d[7] = set_w(c->d[7], next16(0)); put16m(0xFFFF99E2u, c->d[7]);
    c->d[0] = set_w(c->d[0], next16(0)); put16m(0xFFFF99E4u, c->d[0]);
    put16m(SCRIPT_VARS, next16(0));
    put16m(0xFFFF99E6u, 0);
    put8(0xFFFFA0E0u, 8);
    c->d[4] = set_w(c->d[4], c->d[0]); F_W(c->d[0]);
    c->d[4] = set_w(c->d[4], sub_w(&c->ccr, c->d[7], c->d[4]));   /* the height between, its half */
    c->d[1] = set_w(c->d[1], c->d[4]); F_W(c->d[4]);
    c->d[4] = set_w(c->d[4], lsl_n(&c->ccr, c->d[4], 4, 16));
    move_b(4, 6);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 1, 16));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[7], c->d[1]));
    c->a[4] = sx(rd16(PLAYER_ENT));
    cmp_w(&c->ccr, rd16(c->a[4] - 0x3700), c->d[1]);
    if ((c->d[1] & 0xFFFF) < rd16(c->a[4] - 0x3700)) {   /* the player below half-way: the lower floor */
        mem_or8(SCRIPT_VARS, 1);
        uint32_t t = c->d[0]; c->d[0] = c->d[7]; c->d[7] = t;
        or_w(4, 0x8000);
    }
    c->d[5] = 0x1A; F_L(0x1A);
    call(0x11BF0);
    f_bit(rd8(SCRIPT_VARS) & 1);
    if (!(rd8(SCRIPT_VARS) & 1)) {
        c->d[0] = 2; F_L(2);
        f_bit(rd8(0xFFFF99DFu) >> 1 & 1);
        if (rd8(0xFFFF99DFu) & 2) { c->d[0] = 3; F_L(3); }
    } else {
        c->d[0] = 0; F_L(0);
        f_bit(rd8(0xFFFF99DFu) & 1);
        if (rd8(0xFFFF99DFu) & 1) { c->d[0] = 1; F_L(1); }
    }
    lift_switch();
}
static void room2_setup(void) { c->a[0] = 0xCB64; lift_setup(); }   /* $CB5C: x $1060, floors $1060 and $12B0 */
static void room3_setup(void) { c->a[0] = 0xCB74; lift_setup(); }   /* $CB6C: $1080 and $11F0 */
static void room4_setup(void) { c->a[0] = 0xCB84; lift_setup(); }   /* $CB7C: the same, switches the other way */
/* $CB8C, each frame: the lift's object sets $9995 bit 0 as it leaves a floor (both switches
 * show it moving; after 30 frames $9994 bit 2) and bit 1 as it arrives (30 frames later the
 * switches show where it is) */
static void lift_frame(void)
{
    if (mem_bclr(0xFFFF9995u, 0)) {
        put8(0xFFFF99E7u, 0x1E);
        put8(0xFFFF9F05u, 4);
        f_bit(rd8(SCRIPT_VARS) & 1);
        if (rd8(SCRIPT_VARS) & 1) {
            c->d[0] = 2; F_L(2);
            f_bit(rd8(0xFFFF99DFu) >> 1 & 1);
            if (rd8(0xFFFF99DFu) & 2) { c->d[0] = 3; F_L(3); }
        } else {
            c->d[0] = 0; F_L(0);
            f_bit(rd8(0xFFFF99DFu) & 1);
            if (rd8(0xFFFF99DFu) & 1) { c->d[0] = 1; F_L(1); }
        }
        push32(0xCBC6); lift_switch(); c->a[7] += 4;
    }
    if (mem_bclr(0xFFFF9995u, 1)) put8(0xFFFF99E6u, 0x1E);
    else {
        F_B(rd8(0xFFFF99E6u));
        if (rd8(0xFFFF99E6u) && !mem_dec8(0xFFFF99E6u)) {
            f_bit(rd8(SCRIPT_VARS) & 1);
            if (!(rd8(SCRIPT_VARS) & 1)) {
                c->d[0] = 6; F_L(6);
                f_bit(rd8(0xFFFF99DFu) >> 1 & 1);
                if (rd8(0xFFFF99DFu) & 2) { c->d[0] = 7; F_L(7); }
            } else {
                c->d[0] = 4; F_L(4);
                f_bit(rd8(0xFFFF99DFu) & 1);
                if (rd8(0xFFFF99DFu) & 1) { c->d[0] = 5; F_L(5); }
            }
            push32(0xCC08); lift_switch(); c->a[7] += 4;
        }
    }
    F_B(rd8(0xFFFF99E7u));
    if (!rd8(0xFFFF99E7u) || mem_dec8(0xFFFF99E7u)) return;
    { uint16_t v = (uint16_t)(rd16(0xFFFF9994u) | 4); wr16(0xFFFF9994u, v); F_W(v); }
}

ENTRY(Room2Setup, room2_setup)             /* $CB5C */
ENTRY(Room3Setup, room3_setup)             /* $CB6C */
ENTRY(Room4Setup, room4_setup)             /* $CB7C */
ENTRY(LiftFrame, lift_frame)               /* $CB8C */
ENTRY(LiftSetup, lift_setup)               /* $CC1C */
ENTRY(LiftSwitch, lift_switch)             /* $CC8E */

/* ---- batch T26: the rooms' scripts 5-10 ----
 * Index 5 (area 7's room 6): four switches to hit in order; index 6 (area 11's room 0): a wall
 * that crumbles once room flag 0 is set; 7 and 8 (area 11's rooms 1 and 2): effects let out by
 * $9995; 9 (area 16's rooms 0-2): a picture struck at random above the back layer; 10 (area 1's
 * room 4): four lifts. The effects' lists go to $27FA. */

#define SPAWN_EFFECTS 0x27FAu       /* the effects listed at a0 */

/* index 5. $CCF2 as it loads: the switches' effects, once room flag 15 is set */
static void room5_setup(void)
{
    c->d[0] = 0xF; F_L(0xF);
    push32(0xCCF8); room_flag_bit(); c->a[7] += 4;
    f_bit(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1);
    if (!(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1)) return;
    c->a[0] = 0xCD08;
    call(SPAWN_EFFECTS);
}
/* $CD34, each frame: the switch hit ($9994 bits 0-3) the next in the order 1, 2, 0, 3 turns
 * ($216); a wrong one lets the effects at $CDC4 out (once, $9994 bit 4). The fourth sets room
 * flag 15 and breaks three blocks */
static void room5_frame(void)
{
    static const uint8_t order[4] = { 1, 2, 0, 3 };              /* $CDC0 */
    static const uint16_t xs[4] = { 0x107, 0x10D, 0x113, 0x119 };  /* $CDC4: the switches, at $4080 */
    c->d[0] = 0xF; F_L(0xF);
    push32(0xCD3A); room_flag_bit(); c->a[7] += 4;
    f_bit(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1);
    if (rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1) goto done;
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9994u)); F_W(c->d[0]);
    f_bit(c->d[0] >> 4 & 1);
    if (c->d[0] & 0x10) goto done;
    c->d[1] = 0; F_L(0);
    move_b(1, rd8(SCRIPT_VARS));
    move_b(1, order[c->d[1]]);
    { uint32_t b = 1u << (c->d[1] & 31); f_bit((c->d[0] & b) != 0); int was = (c->d[0] & b) != 0; c->d[0] &= ~b;
      if (!was) {                                    /* not the next: a wrong one? */
          and_w(0, 0xF);
          if (!(c->d[0] & 0xFFFF)) goto done;
          c->a[0] = 0xCDC4;
          call(SPAWN_EFFECTS);
          { uint16_t v = (uint16_t)(rd16(0xFFFF9994u) | 0x10); wr16(0xFFFF9994u, v); F_W(v); }
          goto done;
      } }
    wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
    c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[1]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[6] = set_w(c->d[6], xs[c->d[1] & 0xFFFF]); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], 0x4080); F_W(0x4080);
    c->d[0] = set_w(c->d[0], 0x216); F_W(0x216);
    push32(0xCD8C); cell_set_a(); c->a[7] += 4;
    cmp_byte(4, rd8(SCRIPT_VARS));
    if (rd8(SCRIPT_VARS) < 4) goto done;
    c->d[0] = 0xF; F_L(0xF);                            /* all four: the way opens */
    push32(0xCD9A); room_flag_bit(); c->a[7] += 4;
    { uint32_t a = c->a[0] + sx(c->d[1]); uint8_t v = rd8(a); f_bit(v >> (c->d[0] & 7) & 1); wr8(a, (uint8_t)(v | 1 << (c->d[0] & 7))); }
    c->d[5] = 0; F_L(0);
    c->d[6] = set_w(c->d[6], 0x11B); F_W(0x11B);
    c->d[7] = set_w(c->d[7], 0x4200); F_W(0x4200);
    push32(0xCDAC); break_spawn(); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    push32(0xCDB2); break_spawn(); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    push32(0xCDB8); break_spawn(); c->a[7] += 4;
done:
    { uint16_t v = (uint16_t)(rd16(0xFFFF9994u) & 0xFFF0); wr16(0xFFFF9994u, v); F_W(v); }
}

/* index 6. $CDDE as it loads: with room flag 0 ($9993 bit 0) set, the wall at ($114, $4200)
 * already gone (4 x 1 empty blocks) */
static void room6_setup(void)
{
    f_bit(rd8(0xFFFF9993u) & 1);
    if (!(rd8(0xFFFF9993u) & 1)) return;
    mem_or8(SCRIPT_VARS, 1);
    c->d[0] = 4; F_L(4);
    c->d[1] = 1; F_L(1);
    c->d[2] = 0; F_L(0);
    c->d[6] = set_w(c->d[6], 0x114); F_W(0x114);
    c->d[7] = set_w(c->d[7], 0x4200); F_W(0x4200);
    call(SET_MAP_BLOCKS);
}
/* $CE28, $CE24: one block (two) crumbled at d6 with look d5, d6 moving on */
static void rubble_one(void)
{
    push32(0xCE2C); break_spawn(); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
}
static void rubble_two(void) { push32(0xCE28); rubble_one(); c->a[7] += 4; rubble_one(); }
/* $CE00, each frame: once the flag is set, the wall crumbles, four blocks (look 3) */
static void room6_frame(void)
{
    f_bit(rd8(SCRIPT_VARS) & 1);
    if (rd8(SCRIPT_VARS) & 1) return;
    f_bit(rd8(0xFFFF9993u) & 1);
    if (!(rd8(0xFFFF9993u) & 1)) return;
    mem_or8(SCRIPT_VARS, 1);
    c->d[5] = 3; F_L(3);
    c->d[6] = set_w(c->d[6], 0x114); F_W(0x114);
    c->d[7] = set_w(c->d[7], 0x4200); F_W(0x4200);
    push32(0xCE24); rubble_two(); c->a[7] += 4;          /* bsr to the next instruction: twice */
    rubble_two();
}

/* index 7. $CE30, each frame: $9995 bit 0 lets the effects at $CE68 out, once; bit 1 those at
 * $CE94, every frame it is set (the original never marks them done) */
static void room7_frame(void)
{
    f_bit(rd8(SCRIPT_VARS) & 1);
    if (!(rd8(SCRIPT_VARS) & 1)) {
        f_bit(rd8(0xFFFF9995u) & 1);
        if (rd8(0xFFFF9995u) & 1) {
            mem_or8(SCRIPT_VARS, 1);
            c->a[0] = 0xCE68;
            call(SPAWN_EFFECTS);
        }
    }
    f_bit(rd8(SCRIPT_VARS) >> 1 & 1);
    if (rd8(SCRIPT_VARS) & 2) return;
    f_bit(rd8(0xFFFF9995u) >> 1 & 1);
    if (!(rd8(0xFFFF9995u) & 2)) return;
    c->a[0] = 0xCE94;
    call(SPAWN_EFFECTS);
}
/* index 8. $CEC0, each frame: $9995 bit 0 lets the effects at $CEE0 out, once */
static void room8_frame(void)
{
    f_bit(rd8(SCRIPT_VARS) & 1);
    if (rd8(SCRIPT_VARS) & 1) return;
    f_bit(rd8(0xFFFF9995u) & 1);
    if (!(rd8(0xFFFF9995u) & 1)) return;
    mem_or8(SCRIPT_VARS, 1);
    c->a[0] = 0xCEE0;
    call(SPAWN_EFFECTS);
}

/* index 9. $CFD6: the next strike in 16 to 143 frames ($99DE) */
static void room9_timer(void)
{
    call(RANDOM);
    and_w(0, 0x7F);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x10, c->d[0]));
    put16m(SCRIPT_VARS, c->d[0]);
}
/* $CF48 as it loads: the picture's list ($CF0C, 60 bytes) copied to $9A0A, the first strike timed */
static void room9_setup(void)
{
    c->a[0] = 0xCF0C;
    c->a[1] = 0xFFFF9A0Au;
    c->d[0] = 0x3B; F_L(0x3B);
    do { put8(c->a[1], rd8(c->a[0])); c->a[0]++; c->a[1]++; } while (dbra(0));
    room9_timer();
}
/* $CF5A, each frame: when the time comes, the picture put at a random place (in 32 pixels)
 * across the back layer's view, row $4180: the cells it covers first read into its list's
 * second half ($9A29: a cell, then its place; to a negative word), sound 64 */
static void room9_frame(void)
{
    if (mem_sub_w(SCRIPT_VARS, 1)) return;
    put8(SCRIPT_VARS, 0);
    call(RANDOM);
    and_w(0, 0xE0);
    c->a[3] = LAYER_B;
    c->d[6] = 0xFFFFFFE0u; F_L(c->d[6]);
    c->d[6] = set_w(c->d[6], c->d[6] & rd16(c->a[3] + 4)); F_W(c->d[6]);
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, c->d[0], c->d[6]));
    c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, c->d[6], 4, 16));
    c->d[7] = set_w(c->d[7], 0x4180); F_W(0x4180);
    put16m(0xFFFF99E0u, c->d[6]);
    put16m(0xFFFF99E2u, c->d[7]);
    c->a[0] = 0xFFFF9A29u;
    c->a[3] = LAYER_B;
    for (;;) {
        move_b(0, next8(0));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
        move_b(0, next8(0));
        F_W(c->d[0]);
        if (c->d[0] & 0x8000) break;
        move_b(6, next8(0));
        c->d[6] = set_w(c->d[6], (uint32_t)(int32_t)(int8_t)(c->d[6] & 0xFF)); F_W(c->d[6]);
        c->d[6] = set_w(c->d[6], add_w(&c->ccr, rd16(0xFFFF99E0u), c->d[6]));
        move_b(7, next8(0));
        c->d[7] = set_w(c->d[7], (uint32_t)(int32_t)(int8_t)(c->d[7] & 0xFF)); F_W(c->d[7]);
        c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 6, 16));
        c->d[7] = set_w(c->d[7], add_w(&c->ccr, rd16(0xFFFF99E2u), c->d[7]));
        c->a[7] -= 2; wr16(c->a[7], c->a[0]);
        call(0x2542);                                   /* the cell at (d6, d7) of layer a3 */
        c->a[0] = sx(rd16(c->a[7])); c->a[7] += 2;
        put8(c->a[0] - 3, c->d[0]);
        c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 8, 16));
        put8(c->a[0] - 4, c->d[0]);
    }
    c->d[6] = set_w(c->d[6], rd16(0xFFFF99E0u)); F_W(c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(0xFFFF99E2u)); F_W(c->d[7]);
    c->a[0] = 0xFFFF9A0Au;
    call(0x2974);                                       /* the picture's list at a0 drawn at (d6, d7) */
    c->d[0] = 0x40; F_L(0x40);
    call(QUEUE_SOUND);
    room9_timer();
}

/* index 10. $CFE8 as it loads: four lifts (room objects of kind 8 at places 6, 4, 2, 0), each
 * linked to the one before ($D302) */
static void room10_setup(void)
{
    c->d[1] = 8; F_L(8);
    c->d[2] = 0xFFFFFF80u; F_L(c->d[2]);
    c->d[3] = 0; F_L(0);
    c->d[4] = 6; F_L(6);
    do {
        call(FIND_EFFECT);
        put8(c->a[4] - 0x4000, c->d[2]);
        put16m(c->a[4] - 0x2D00, c->d[4]);
        put16m(c->a[4] - 0x2CFE, c->d[3]);
        put16m(c->a[4] - 0x2BFE, c->d[1]);
        c->d[3] = set_w(c->d[3], c->a[4]); F_W(c->a[4]);
        c->d[4] = set_w(c->d[4], sub_w(&c->ccr, 2, c->d[4]));
    } while (!(c->ccr & CC_N));
}

ENTRY(Room5Setup, room5_setup)             /* $CCF2 */
ENTRY(Room5Frame, room5_frame)             /* $CD34 */
ENTRY(Room6Setup, room6_setup)             /* $CDDE */
ENTRY(Room6Frame, room6_frame)             /* $CE00 */
ENTRY(RubbleTwo, rubble_two)               /* $CE24 */
ENTRY(RubbleOne, rubble_one)               /* $CE28 */
ENTRY(Room7Frame, room7_frame)             /* $CE30 */
ENTRY(Room8Frame, room8_frame)             /* $CEC0 */
ENTRY(Room9Setup, room9_setup)             /* $CF48 */
ENTRY(Room9Frame, room9_frame)             /* $CF5A */
ENTRY(Room9Timer, room9_timer)             /* $CFD6 */
ENTRY(Room10Setup, room10_setup)           /* $CFE8 */

/* ---- batch T27: the sequences of a conversation, a thing received and a statue ----
 * Entered by the game's task like T19's (from its table at $91F4, or from $C728). They wait for
 * frames on the task's coroutine and leave as the original does. */

#define MESSAGES      0x13BCu       /* the message interpreter, each frame */
#define MESSAGE_ON    0xFFFF8CA4u   /* bit 7: a message running */
#define MESSAGE_AT    0xFFFF8C7Au   /* long: the message to run */
#define TALK_STATE    0xFFFF9BF3u   /* bit 0: ended by a fade; bit 1: then restart */
#define TALK_DONE     0xFFFF9C18u   /* long: called each frame while it runs (negative: none) */

/* $C1E8: a conversation: the others frozen, the message $9C16 run until it ends. Then back to
 * the game; or, ended by a fade ($9BF3 bit 0), the screen faded out and either the game
 * restarted ($4CA8, bit 1) or, after sound 84, 300 frames' sleep (the inn's, $C262: left to the
 * original, which waits on the VBlank flag) */
static void seq_talk(uint32_t at)
{
    switch (at) {
    case 0xC20A: goto rC20A;
    case 0xC20E: goto rC20E;
    case 0xC248: goto rC248;
    }
    push32(0xC1EC); reset_continuations(); c->a[7] += 4;
    mem_or8(0xFFFF9659u, 8);
    put8(TALK_STATE, 0);
    put16m(0xFFFF9962u, 0);
    call(FREEZE);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9C16u)); F_W(c->d[0]);
    call(0x279C);
    for (;;) {
        wait_frame(0xC20A);
rC20A:
        call_back(MESSAGES, 0xC20E);
rC20E:
        c->d[0] = rd32(TALK_DONE); F_L(c->d[0]);
        if (!(c->d[0] & 0x80000000u)) { c->a[0] = c->d[0]; call(c->a[0]); }
        f_bit(rd8(TALK_STATE) & 1);
        if (rd8(TALK_STATE) & 1) break;
        F_B(rd8(MESSAGE_ON));
        if (rd8(MESSAGE_ON) & 0x80) continue;
        wr8(TALK_DONE, 0xFF);
        mem_and8(0xFFFF9659u, 0xF7);
        call(0x225C);
        call(UNFREEZE);
        leave_to(GAME_LOOP);
        return;
    }
    wr8(TALK_DONE, 0xFF);                               /* ended by a fade */
    call(0x5842);
    do {
        wait_frame(0xC248);
rC248:
        f_bit(rd8(0xFFFF8C56u) >> 4 & 1);
    } while (!(rd8(0xFFFF8C56u) & 0x10));
    f_bit(rd8(TALK_STATE) >> 1 & 1);
    if (rd8(TALK_STATE) & 2) { c->d[0] = 0; F_L(0); leave_to(0x4CA8); return; }
    c->d[0] = 0x54; F_L(0x54);
    call(QUEUE_SOUND);
    c->d[0] = set_w(c->d[0], 0x12B); F_W(0x12B);
    leave_to(0xC262);
}

/* $C376: each frame of a thing received: the room objects of kind 6 (its sparkles, $AEF2) run,
 * and the thing ($9968) rises: first set up (its look, raised 8, its name's colours $22A8 from
 * $13AD0), then a sparkle every 3 frames beside it, for 64 frames */
static void receive_step(void)
{
    c->a[4] = 0xE0;
    do {
        F_B(rd8(c->a[4] - 0x4000));
        if (rd8(c->a[4] - 0x4000) & 0x80) {
            f_bit(rd8(c->a[4] - 0x4000) >> 5 & 1);
            if (!(rd8(c->a[4] - 0x4000) & 0x20)) {
                cmp_w(&c->ccr, 6, rd16(c->a[4] - 0x2BFE));
                if (rd16(c->a[4] - 0x2BFE) == 6) { push32(0xC394); call(0xAEF2); c->a[7] += 4; }
            }
        }
        c->a[4] += 4;
        cmp_l(&c->ccr, 0x100, c->a[4]);
    } while (c->ccr & CC_C);
    c->a[4] = sx(rd16(0xFFFF9968u));
    f_bit(rd8(c->a[4] - 0x4000) >> 6 & 1);
    if (!(rd8(c->a[4] - 0x4000) & 0x40)) {
        call(CLEAR_ENTITY);
        put16m(c->a[4] - 0x3DFE, 0x7EC);
        put8(c->a[4] - 0x3CFD, 8);
        put8(c->a[4] - 0x3FFE, 0x80);
        put16m(c->a[4] - 0x3D00, 0xE0C);
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        mem_sub_w(c->a[4] - 0x3700, 8);
        c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x2300)); F_W(c->d[0]);
        c->a[0] = 0x13AD0;
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
        c->d[1] = set_w(c->d[1], rd16(c->a[0] + sx(c->d[0]))); F_W(c->d[1]);
        c->d[0] = set_w(c->d[0], 0xFD80); F_W(0xFD80);
        c->d[2] = 0x20; F_L(0x20);
        call(0x22A8);
        put16m(c->a[4] - 0x35FE, 0xFF00);
        put8(c->a[4] - 0x2800, 0x40);
        put8(c->a[4] - 0x27FF, 1);
        mem_or8(c->a[4] - 0x4000, 0x40);
        return;
    }
    if (!mem_dec8(c->a[4] - 0x27FF)) {
        put8(c->a[4] - 0x27FF, 3);
        c->a[3] = sx(c->a[4]);
        call(FIND_EFFECT);
        if (!(c->ccr & CC_N)) {                         /* a sparkle at random beside it */
            call(CLEAR_ENTITY);
            call(RANDOM);
            and_w(0, 0xF);
            c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 8, c->d[0]));
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[3] - 0x3800), c->d[0]));
            put16m(c->a[4] - 0x3800, c->d[0]);
            c->d[0] = 4; F_L(4);
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[3] - 0x3700), c->d[0]));
            put16m(c->a[4] - 0x3700, c->d[0]);
            put8(c->a[4] - 0x3FFE, 0x80);
            put8(c->a[4] - 0x3CFD, 6);
            put16m(c->a[4] - 0x2BFE, 6);
            put16m(c->a[4] - 0x3D00, 0x1604);
            put8(c->a[4] - 0x4000, 0xC0);
            c->d[0] = 0; F_L(0);
            call(SET_ANIM);
        }
        uint32_t t = c->a[3]; c->a[3] = c->a[4]; c->a[4] = t;
    }
    call(APPLY_VEL_Y);
    if (mem_dec8(c->a[4] - 0x2800)) return;
    put8(c->a[4] - 0x4000, 0);
}
/* $C2DE: the thing's own routine ($245A) */
static void receive_own(void)
{
    c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x2300)); F_W(c->d[0]);
    call(0x245A);
}
/* $C2E8: a heart: the most hearts one more (to 14), the player's own one more too */
static void receive_heart(void)
{
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9F00u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x100, c->d[0]));
    cmp_w(&c->ccr, 0xE00, c->d[0]);
    if ((c->d[0] & 0xFFFF) >= 0xE00) { c->d[0] = set_w(c->d[0], 0xE00); F_W(0xE00); }
    put16m(0xFFFF9F00u, c->d[0]);
    c->a[0] = sx(rd16(PLAYER_ENT));
    c->d[1] = set_w(c->d[1], rd16(c->a[0] - 0x2600)); F_W(c->d[1]);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, 0x100, c->d[1]));
    cmp_w(&c->ccr, c->d[1], c->d[0]);
    if ((c->d[0] & 0xFFFF) < (c->d[1] & 0xFFFF)) { c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]); }
    put16m(c->a[0] - 0x2600, c->d[1]);
    call(0x8AA2);
}
/* $C31A: the thing's own routine, then the back palette stepped through its list ($C36E by
 * $9685; two frames each) */
static void receive_palettes_at(uint32_t ret, uint32_t at)
{
    if (at == 0xC35C) goto rC35C;
    if (at == 0xC360) goto rC360;
    push_cont(ret);
    c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x2300)); F_W(c->d[0]);
    call(0x245A);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(0xFFFF9685u));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 3, 16));
    c->a[0] = 0xC36E + sx(c->d[0]);
    for (;;) {
        move_b(0, next8(0));
        if (!(c->d[0] & 0xFF)) break;
        put32m(0xFFFF9C2Cu, c->a[0]);
        c->a[0] = 0xFFFF966Eu;
        put8(0xFFFF9670u, c->d[0]);
        call(LOAD_PALETTES);
        call(0x55BA);
        wait_frame(0xC35C);
rC35C:
        wait_frame(0xC360);
rC360:
        c->a[0] = rd32(0xFFFF9C2Cu);
    }
    pop_cont();
}
/* $C27E, room change 9: a thing received ($B42A). It rises with sparkles until it is gone,
 * then takes effect: below $20 its own routine; from $20 by the table at $C46A (its own routine,
 * nothing, a heart, or the palettes; $27 and $2F hang, as in the original), and its message */
static void receive_palettes(uint32_t ret) { receive_palettes_at(ret, 0); }
static void seq_receive(uint32_t at)
{
    switch (at) {
    case 0xC298: goto rC298;
    case 0xC2C2: goto rC2C2;
    }
    push32(0xC282); reset_continuations(); c->a[7] += 4;
    put16m(0xFFFF9962u, 0);
    call(FREEZE);
    for (;;) {
        push32(0xC28E); receive_step(); c->a[7] += 4;
        F_B(rd8(c->a[4] - 0x4000));
        if (!(rd8(c->a[4] - 0x4000) & 0x80)) break;
        wait_frame(0xC298);
rC298: ;
    }
    c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x2300)); F_W(c->d[0]);
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 0x20, c->d[0]));
    if (c->ccr & CC_C) {
        push32(0xC2AA); receive_own(); c->a[7] += 4;
    } else {
        c->a[0] = 0xC46A;                                /* by thing: 0 own, 2 nothing, 4 heart, 6 palettes, -1 none */
        move_b(0, rd8(c->a[0] + (c->d[0] & 0xFFFF)));
        if (c->ccr & CC_N) { leave_to(0xC2B4); return; }   /* bmi to itself */
        static const uint16_t routine[4] = { 0xC2DE, 0xC2E6, 0xC2E8, 0xC31A };
        c->a[0] = routine[(c->d[0] & 0xFF) / 2];
        switch (c->d[0] & 0xFF) {
        case 0: push32(0xC2C2); receive_own(); c->a[7] += 4; break;
        case 2: break;
        case 4: push32(0xC2C2); receive_heart(); c->a[7] += 4; break;
        default: receive_palettes(0xC2C2); break;
        }
    }
rC2C2:
    c->a[0] = sx(rd16(0xFFFF9968u));
    c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x2300)); F_W(c->d[0]);
    call(0x1FD0);
    call(UNFREEZE);
    leave_to(GAME_LOOP);
}

/* $C48C: a statue ($A0DE): its window ($C56E) and message; with the ocarina, the statue's tune
 * played ($2AAE): a right one sets its room flag, and the message says how it went */
static void seq_statue(uint32_t at)
{
    switch (at) {
    case 0xC4BE: goto rC4BE;
    case 0xC4C2: goto rC4C2;
    case 0xC4E4: goto rC4E4;
    case 0xC4E8: goto rC4E8;
    case 0xC4FA: goto rC4FA;
    case 0xC53E: goto rC53E;
    case 0xC542: goto rC542;
    }
    push32(0xC490); reset_continuations(); c->a[7] += 4;
    c->d[0] = 0xFFFFFFFCu; F_L(c->d[0]);
    call(QUEUE_SOUND);
    put16m(0xFFFF9962u, 0);
    call(FREEZE);
    F_W(rd16(0xFFFFA0DEu));
    c->a[1] = 0xC56E;
    if (rd16(0xFFFFA0DEu) & 0x8000) {                   /* no statue: a message only */
        call(0x27B0);
        put32m(MESSAGE_AT, 0x2B56);
        put8(MESSAGE_ON, 0x80);
        do { wait_frame(0xC4BE); rC4BE: call_back(MESSAGES, 0xC4C2); rC4C2: F_B(rd8(MESSAGE_ON)); } while (rd8(MESSAGE_ON) & 0x80);
    } else {
        call(0x27B0);
        put32m(MESSAGE_AT, 0x2B8F);
        put8(MESSAGE_ON, 0x80);
        do { wait_frame(0xC4E4); rC4E4: call_back(MESSAGES, 0xC4E8); rC4E8: F_B(rd8(MESSAGE_ON)); } while (rd8(MESSAGE_ON) & 0x80);
        c->a[0] = sx(rd16(0xFFFFA0DEu));
        move_b(0, rd8(c->a[0] - 0x31FE));
        call_back(0x2AAE, 0xC4FA);
rC4FA:
        wr8(0xFFFF9ED4u, 0xFF);
        F_B(c->d[0]);
        if (!(c->d[0] & 0xFF)) {                        /* the right tune */
            c->a[0] = sx(rd16(0xFFFFA0DEu));
            move_b(0, rd8(c->a[0] - 0x31FE));
            push32(0xC50E); room_flag_bit(); c->a[7] += 4;
            { uint32_t a = c->a[0] + sx(c->d[1]); uint8_t v = rd8(a); f_bit(v >> (c->d[0] & 7) & 1); wr8(a, (uint8_t)(v | 1 << (c->d[0] & 7))); }
            put32m(MESSAGE_AT, 0x2BA9);
        } else {
            wr8(0xFFFF9ED4u, 0);
            put32m(MESSAGE_AT, c->d[0] & 0x80 ? 0x2BE5 : 0x2BC4);
        }
        put8(MESSAGE_ON, 0xC0);
        do { wait_frame(0xC53E); rC53E: call_back(MESSAGES, 0xC542); rC542: F_B(rd8(MESSAGE_ON)); } while (rd8(MESSAGE_ON) & 0x80);
    }
    call(0x225C);
    call(UNFREEZE);
    c->d[0] = 0xFFFFFFFDu; F_L(c->d[0]);
    call(QUEUE_SOUND);
    c->a[4] = sx(rd16(PLAYER_ENT));
    c->d[0] = 0; F_L(0);
    put16m(c->a[4] - 0x3D00, rd16(0xFFFF9EECu));
    put16m(c->a[4] - 0x3DFE, c->d[0]);
    call(SET_ANIM);
    leave_to(GAME_LOOP);
}

ENTRY(ReceiveStep, receive_step)           /* $C376 */
ENTRY(ReceiveOwn, receive_own)             /* $C2DE */
ENTRY(ReceiveHeart, receive_heart)         /* $C2E8 */
ENTRY(ReceiveNone, nothing)                /* $C2E6 */
/* the sequences leave by port_continue_at, so ret is theirs to set */
void SeqTalk(Cpu *cpu)          { c = cpu; NEEDS_PASS(0xC1E8); seq_talk(0); }                  /* $C1E8 */
void SeqReceive(Cpu *cpu)       { c = cpu; NEEDS_PASS(0xC27E); seq_receive(0); }               /* $C27E */
void SeqStatue(Cpu *cpu)        { c = cpu; NEEDS_PASS(0xC48C); seq_statue(0); }                /* $C48C */
void ReceivePalettes(Cpu *cpu)  { c = cpu; NEEDS_PASS(0xC31A); cont_68k(receive_palettes); }  /* $C31A */

/* ---- batch T28: the rooms' scripts 11 and 12 ----
 * Index 11 (area 11's room 6): a game of chance with three figures (room objects of kind 4 at
 * $99E0) and the message $5B. Index 12 (area 4's room 1): until game flag $39, its graphics
 * streamed in; after it, the way at ($108, $4100) shut. */

/* $D176: the three figures' animations from the bytes at a1 */
static void room11_looks(void)
{
    c->a[0] = 0xFFFF99E0u;
    c->d[3] = 2; F_L(2);
    do {
        c->a[4] = sx(next16(0));
        move_b(0, next8(1));
        call(SET_ANIM);
    } while (dbra(3));
}
/* $D00C as it loads: the three figures (looks and heights from $D05C), $9659 bit 7 */
static void room11_setup(void)
{
    c->a[0] = 0xD05C;
    c->a[1] = 0xFFFF99E0u;
    c->d[3] = 2; F_L(2);
    do {
        call(FIND_EFFECT);
        put16(1, c->a[4]);
        call(CLEAR_ENTITY);
        put16m(c->a[4] - 0x2BFE, 4);
        put16m(c->a[4] - 0x3DFE, 0x400);
        c->d[0] = set_w(c->d[0], 0x1600); F_W(0x1600);
        move_b(0, next8(0));
        put16m(c->a[4] - 0x3D00, c->d[0]);
        put8(c->a[4] - 0x3CFD, next8(0));
        put16m(c->a[4] - 0x3800, 0x10B0);
        put16m(c->a[4] - 0x3700, next16(0));
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        put8(c->a[4] - 0x4000, 0xC0);
    } while (dbra(3));
    { uint8_t v = rd8(0xFFFF9659u); f_bit(v >> 7 & 1); wr8(0xFFFF9659u, v | 0x80); }
}
/* $D068, each frame until it is played ($99DE): the message runs, its choices ($8CC2) turning
 * the figures for 30 frames and the middle one ($99E2) moving at random. The result ($8CD3):
 * 0 the gold and the player's hearts ($DA00) gone, 1 the hearts, 2-3 to room $28 or $2F, others
 * to room $5E with game flag $9688 set. 1 when it returns through the continuation, 0 when it went to the next room */
static int room11_frame_at(uint32_t ret, uint32_t at)
{
    static const uint8_t moves[8] = { 0, 2, 4, 2, 4, 0, 2, 4 };   /* $D0E0 */
    if (!at) push_cont(ret);
    switch (at) {
    case 0xD08A: goto rD08A;
    case 0xD08E: goto rD08E;
    }
    move_b(0, rd8(SCRIPT_VARS));
    if (c->d[0] & 0xFF) { pop_cont(); return 1; }
    mem_or8(0xFFFF9659u, 8);
    c->d[0] = 0x5B; F_L(0x5B);
    call(0x279C);
    do {
        wait_frame(0xD08A);
rD08A:
        call_back(MESSAGES, 0xD08E);
rD08E:
        F_B(rd8(0xFFFF99DFu));
        if (rd8(0xFFFF99DFu)) {
            if (mem_dec8(0xFFFF99DFu)) continue;
            c->a[1] = 0xD18A;
            push32(0xD0C4); room11_looks(); c->a[7] += 4;
            continue;
        }
        if (mem_bclr(0xFFFF8CC2u, 7)) c->a[1] = 0xD18D;
        else if (mem_bclr(0xFFFF8CC2u, 6)) c->a[1] = 0xD190;
        else {
            c->a[4] = sx(rd16(0xFFFF99E2u));
            if (mem_bclr(0xFFFF8CC2u, 0)) {
                call(RANDOM);
                and_w(0, 7);
                move_b(0, moves[c->d[0] & 0xFFFF]);
            } else {
                F_B(rd8(c->a[4] - 0x3A00));
                if (rd8(c->a[4] - 0x3A00)) continue;
                c->d[0] = 0; F_L(0);
            }
            call(SET_ANIM);
            continue;
        }
        put8(0xFFFF99DFu, 0x1E);
        push32(0xD0C4); room11_looks(); c->a[7] += 4;
    } while ((F_B(rd8(MESSAGE_ON)), rd8(MESSAGE_ON) & 0x80));
    c->a[1] = 0xD18A;
    push32(0xD100); room11_looks(); c->a[7] += 4;
    mem_and8(0xFFFF9659u, 0xF7);
    call(0x225C);
    mem_bclr(0xFFFF9659u, 7);
    move_b(0, rd8(0xFFFF8CD3u));
    if (c->d[0] & 0xFF) {
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
        if (c->d[0] & 0xFF) {                           /* another room */
            c->d[1] = 0x28; F_L(0x28);
            c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
            if (c->d[0] & 0xFF) {
                c->d[1] = 0x2F; F_L(0x2F);
                c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
                if (c->d[0] & 0xFF) {
                    move_b(0, rd8(0xFFFF9688u));
                    push32(0xD150); flag_set(); c->a[7] += 4;
                    c->d[1] = 0x5E; F_L(0x5E);
                }
            }
            put16m(0xFFFF9676u, c->d[1]);
            put8(0xFFFF966Au, 3);
            put8(0xFFFF9678u, 4);
            leave_to(NEXT_ROOM);
            return 0;
        }
    } else put32m(0xFFFF962Cu, 0);
    c->a[4] = sx(rd16(PLAYER_ENT));                     /* 0 and 1: the player's hearts gone */
    put16m(c->a[4] - 0x2600, 0);
    call(0x8AA2);
    c->a[4] = sx(rd16(PLAYER_ENT));
    put8(c->a[4] - 0x2BFF, 0);
    mem_or8(c->a[4] - 0x3200, 0x80);
    wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
    pop_cont();
    return 1;
}

/* index 12. $D1EC: the way at ($108, $4100) shut: 4 x 3 blocks of 0 */
static void room12_blocks(void)
{
    c->d[0] = 4; F_L(4);
    c->d[1] = 3; F_L(3);
    c->d[2] = 0; F_L(0);
    c->d[6] = set_w(c->d[6], 0x108); F_W(0x108);
    c->d[7] = set_w(c->d[7], 0x4100); F_W(0x4100);
    call(SET_MAP_BLOCKS);
}
/* $D194 as it loads: until game flag $39 the graphics at $D1C0 streamed in ($99E2 where the
 * list ended); after it the palette $5000 and the way shut ($99DE set) */
static void room12_setup(void)
{
    c->d[0] = 0x39; F_L(0x39);
    push32(0xD19A); flag_test(); c->a[7] += 4;
    if (c->ccr & CC_Z) {
        c->a[0] = 0xD1C0;
        call(0x23A6);
        put32m(0xFFFF99E2u, c->a[1]);
        return;
    }
    c->d[0] = set_w(c->d[0], 0x5000); F_W(0x5000);
    c->d[1] = 0; F_L(0);
    c->d[2] = 0x20; F_L(0x20);
    call(0x22A8);
    push32(0xD1BA); room12_blocks(); c->a[7] += 4;
    wr8(SCRIPT_VARS, 0xFF);
}

ENTRY(Room11Setup, room11_setup)           /* $D00C */
ENTRY(Room11Looks, room11_looks)           /* $D176 */
ENTRY(Room12Setup, room12_setup)           /* $D194 */
ENTRY(Room12Blocks, room12_blocks)         /* $D1EC */
/* waits, and may leave for the next room instead of returning */
void Room11Frame(Cpu *cpu)
{
    c = cpu; NEEDS_PASS(0xD068);
    uint32_t ret = rd32(c->a[7]); c->a[7] += 4;
    if (room11_frame_at(ret, 0)) port_continue_at(c, c->a[0], 0);
}                                                                                         /* $D068 */

/* ---- batch T29: the room script 12's frames, 13's setup ----
 * Index 12 (area 4's room 1), each frame once room flag $11 is set: the player is walked in,
 * the way behind shut as the view scrolls, a message, then a figure walked to the door, which
 * opens and lets it out. Index 13 (area 16's room 4, area 18's room 6): three moving platforms
 * and three figures. */

/* $D36A: the entity of kind $454 among the 16 from $A11A (a4; none: past the last); $D36E kind d1 */
static void find_kind_d1(void)
{
    c->a[4] = sx(rd16(0xFFFFA11Au));
    c->d[0] = 0xF; F_L(0xF);
    do {
        F_B(rd8(c->a[4] - 0x4000));
        if (rd8(c->a[4] - 0x4000) & 0x80) {
            cmp_w(&c->ccr, rd16(c->a[4] - 0x2BFE), c->d[1]);
            if ((c->d[1] & 0xFFFF) == rd16(c->a[4] - 0x2BFE)) return;
        }
        c->a[4] += 4;
    } while (dbra(0));
}
static void find_kind(void) { c->d[1] = set_w(c->d[1], 0x454); F_W(0x454); find_kind_d1(); }
/* $D352: the player's buttons for its frame routine: left */
static void room12_walk(void) { put8(0xFFFF9F0Bu, 4); }
/* $D1FE, each frame: by step ($99DE) */
static void room12_frame_at(uint32_t ret, uint32_t at)
{
    if (!at) push_cont(ret);
    switch (at) {
    case 0xD31C: goto rD31C;
    case 0xD324: goto rD324;
    }
    move_b(0, rd8(SCRIPT_VARS));
    switch (rd8(SCRIPT_VARS)) {
    case 0:                                             /* room flag $11: game flag $39, the player walked left */
        c->d[0] = 0x11; F_L(0x11);
        push32(0xD214); room_flag_bit(); c->a[7] += 4;
        f_bit(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1);
        if (!(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1)) break;
        c->d[0] = 0x39; F_L(0x39);
        push32(0xD222); flag_set(); c->a[7] += 4;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        put8(0xFFFF9F05u, 6);
        put32m(0xFFFF9F22u, 0xD352);
        break;
    case 1:                                             /* at $10E0: the player still, the figure walking */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
        c->a[4] = sx(rd16(PLAYER_ENT));
        cmp_w(&c->ccr, 0x10E0, rd16(c->a[4] - 0x3800));
        if (rd16(c->a[4] - 0x3800) > 0x10E0) break;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        mem_or8(c->a[4] - 0x3FFE, 8);
        put32m(0xFFFF9F22u, 0xDB36);
        push32(0xD260); find_kind(); c->a[7] += 4;
        put32m(c->a[4] - 0x2700, 0xD35A);
        break;
    case 2:                                             /* the view scrolled 96 left: the way shut */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 2, c->d[0]));
        mem_sub_w(0xFFFF9734u, 1);
        cmp_w(&c->ccr, 0xFFA0, rd16(0xFFFF9734u));
        if ((int16_t)rd16(0xFFFF9734u) > -0x60) break;
        put8(0xFFFF99DFu, 0x1E);
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        push32(0xD28C); room12_blocks(); c->a[7] += 4;
        break;
    case 3:                                             /* 30 frames, then the streamed list's flag */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 3, c->d[0]));
        if (mem_dec8(0xFFFF99DFu)) break;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        c->a[0] = rd32(0xFFFF99E2u);
        mem_or8(c->a[0], 1);
        break;
    case 4:                                             /* once streamed: message $55 (room change 8) */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 4, c->d[0]));
        c->a[0] = rd32(0xFFFF99E2u);
        F_B(rd8(c->a[0]));
        if (rd8(c->a[0]) & 0x80) break;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        put16m(0xFFFF9C16u, 0x55);
        put16m(0xFFFF9962u, 8);
        break;
    case 5:                                             /* the figure walks on */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 5, c->d[0]));
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        push32(0xD2D8); find_kind(); c->a[7] += 4;
        put32m(c->a[4] - 0x2700, 0xD362);
        break;
    case 6:                                             /* at $1030: the door at ($1020, $1050) opens and it goes */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 6, c->d[0]));
        push32(0xD2EA); find_kind(); c->a[7] += 4;
        cmp_w(&c->ccr, 0x1030, rd16(c->a[4] - 0x3800));
        if (rd16(c->a[4] - 0x3800) > 0x1030) break;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        put16m(DOOR, 0x1020);
        put16m(DOOR + 2, 0x1050);
        c->d[0] = 2; F_L(2);
        put16m(DOOR + 4, c->d[0]);
        put16m(DOOR + 6, c->d[0]);
        c->d[0] = 0; F_L(0);
        push32(0xD312); door_frame(); c->a[7] += 4;
        put8(c->a[4] - 0x3CFD, 0x1E);
        wait_frame(0xD31C);
rD31C:
        push32(0xD320); door_blank(); c->a[7] += 4;
        door_animation(0xD324, 0);
rD324:
        put8(0xFFFFC008u, 0);
        push32(0xD32C); door_restore(); c->a[7] += 4;
        push32(0xD32E); find_kind(); c->a[7] += 4;
        put8(c->a[4] - 0x4000, 0);
        break;
    case 7:                                             /* the view back: done */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 7, c->d[0]));
        if (mem_add_w(0xFFFF9734u, 1)) break;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        put8(0xFFFF9F05u, 0);
        break;
    default:
        for (int k = 0; k < 7; k++) c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));   /* one at a time, as the chain of subq.b does */
        break;
    }
    pop_cont();
}
static void room12_frame(uint32_t ret) { room12_frame_at(ret, 0); }

/* index 13. $D3C8: moving platform a1 (of $11CE8) from the 8 bytes at a0 */
static void room13_platform(void)
{
    put8(c->a[1] - 0x6072, 0x80);
    put8(c->a[1] - 0x6071, rd8(c->a[0]));
    c->a[0] += 2;
    put16m(c->a[1] - 0x6070, 0xFFFF);
    put32m(c->a[1] - 0x601E, next32(0));
    c->d[0] = 0; F_L(0);
    put32m(c->a[1] - 0x5FCA, c->d[0]);
    move_b(0, next8(0));
    put16m(c->a[1] - 0x5F76, c->d[0]);
    move_b(0, next8(0));
    put16m(c->a[1] - 0x5F74, c->d[0]);
}
/* $D3F2: figure a4 (a room object of kind 4) from the 6 bytes at a0 */
static void room13_figure(void)
{
    call(0x0A1A);
    put16m(c->a[4] - 0x3DFE, 0x360);
    put16m(c->a[4] - 0x3800, next16(0));
    put16m(c->a[4] - 0x3700, next16(0));
    put16m(c->a[4] - 0x3D00, 0x160C);
    move_b(0, next8(0));
    put8(c->a[4] - 0x3CFD, next8(0));
    call(SET_ANIM);
    put16m(c->a[4] - 0x2BFE, 4);
    put8(c->a[4] - 0x4000, 0xC0);
}
/* $D388 as it loads: the three platforms and figures from $D422; $99DE the player's form */
static void room13_setup(void)
{
    call(0x11CE8);
    c->a[0] = 0xD422;
    put16m(0xFFFF99E0u, c->a[1]);
    push32(0xD396); room13_platform(); c->a[7] += 4;
    call(0x11CE8);
    push32(0xD39C); room13_platform(); c->a[7] += 4;
    call(0x11CE8);
    push32(0xD3A2); room13_platform(); c->a[7] += 4;
    call(FIND_EFFECT);
    push32(0xD3A8); room13_figure(); c->a[7] += 4;
    call(FIND_EFFECT);
    put16m(0xFFFF99E2u, c->a[4]);
    push32(0xD3B2); room13_figure(); c->a[7] += 4;
    call(FIND_EFFECT);
    put16m(0xFFFF99E4u, c->a[4]);
    push32(0xD3BC); room13_figure(); c->a[7] += 4;
    f_bit(rd8(0xFFFF9EF1u) >> 4 & 1);
    wr8(SCRIPT_VARS, rd8(0xFFFF9EF1u) & 0x10 ? 0xFF : 0);
}

ENTRY(Room12Walk, room12_walk)             /* $D352 */
ENTRY(FindKind, find_kind)                 /* $D36A */
ENTRY(FindKindD1, find_kind_d1)            /* $D36E */
ENTRY(Room13Setup, room13_setup)           /* $D388 */
ENTRY(Room13Platform, room13_platform)     /* $D3C8 */
ENTRY(Room13Figure, room13_figure)         /* $D3F2 */
void Room12Frame(Cpu *cpu) { c = cpu; NEEDS_PASS(0xD1FE); cont_68k(room12_frame); }     /* $D1FE */

/* ---- batch T30: the room script 13's ride, 14's and 15's setups ----
 * Index 13 (area 16's room 4, area 18's room 6): standing on the first platform (its bit 0 set
 * by the platform) the player rides: the figures carry it up, the screen fades, the room's
 * graphics change and figures rain down, then all goes back. */

/* $DAE6: d0 frames waited */
static void script_wait_at(uint32_t ret, uint32_t at)
{
    if (!at) push_cont(ret);
    if (at) goto rDAF8;
    put8(0xFFFF99DFu, c->d[0]);
    do { wait_frame(0xDAF8); rDAF8: ; } while (mem_dec8(0xFFFF99DFu));
    pop_cont();
}
static void script_wait(uint32_t ret) { script_wait_at(ret, 0); }
/* $D61E: the palette $8B98 faded by $99E6 / 4 (signed) into $8C18 ($594A) */
static void room13_fade(void)
{
    move_b(1, rd8(0xFFFF99E6u));
    c->d[1] = set_w(c->d[1], (uint32_t)(int32_t)(int8_t)(c->d[1] & 0xFF)); F_W(c->d[1]);
    c->d[1] = set_w(c->d[1], asr_n(&c->ccr, c->d[1], 2, 16));
    c->d[2] = set_w(c->d[2], c->d[1]); F_W(c->d[1]);
    c->d[2] = set_w(c->d[2], asl_n(&c->ccr, c->d[2], 4, 16));
    c->d[3] = set_w(c->d[3], c->d[2]); F_W(c->d[2]);
    c->d[3] = set_w(c->d[3], asl_n(&c->ccr, c->d[3], 4, 16));
    c->a[0] = 0xFFFF8B98u;
    c->a[1] = 0xFFFF8C18u;
    c->d[0] = 0xE; F_L(0xE);
    do call(0x594A); while (dbra(0));
    mem_and8(0xFFFF8C56u, 0x3F);
    mem_or8(0xFFFF8C56u, 0x81);
}
/* $D64E: a falling figure in the first free of the 40 entities from $40: at a random place
 * across, height $FF0, a random speed down ($800 on) */
static void room13_drop(void)
{
    c->a[4] = 0x40;
    c->d[0] = 0x27; F_L(0x27);
    for (;;) {
        F_B(rd8(c->a[4] - 0x4000));
        if (!(rd8(c->a[4] - 0x4000) & 0x80)) break;
        c->a[4] += 4;
        if (!dbra(0)) return;
    }
    call(0x0A1A);
    put16m(c->a[4] - 0x3DFE, 0x360);
    mem_or8(c->a[4] - 0x3FFE, 0x80);
    put16m(c->a[4] - 0x3D00, 0x160C);
    c->d[0] = 6; F_L(6);
    call(SET_ANIM);
    put8(c->a[4] - 0x4000, 0xC0);
    put8(c->a[4] - 0x3CFD, 8);
    put16m(c->a[4] - 0x3700, 0xFF0);
    call(RANDOM);
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
    and_w(0, 0xFF);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    put16m(c->a[4] - 0x3800, c->d[0]);
    c->d[0] = set_w(c->d[0], 0x800); F_W(0x800);
    and_w(1, 0xFF00);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 5, 16));
    for (int k = 0; k < 3; k++) c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
    put16m(c->a[4] - 0x35FE, c->d[0]);
    cmp_w(&c->ccr, 0x1400, c->d[0]);
    if ((c->d[0] & 0xFFFF) < 0x1400) put8(c->a[4] - 0x3CFD, 0x12);
}
/* $D6C4: the falling figures moved, gone below $10B0; $99DF set while any is left */
static void room13_drops(void)
{
    c->a[4] = 0x40;
    c->d[7] = 0x27; F_L(0x27);
    put8(0xFFFF99DFu, 0);
    do {
        F_B(rd8(c->a[4] - 0x4000));
        if (rd8(c->a[4] - 0x4000) & 0x80) {
            wr8(0xFFFF99DFu, 0xFF);
            call(APPLY_VEL_Y);
            cmp_w(&c->ccr, 0x10B0, rd16(c->a[4] - 0x3700));
            if (rd16(c->a[4] - 0x3700) >= 0x10B0) put8(c->a[4] - 0x4000, 0);
        }
        c->a[4] += 4;
    } while (dbra(7));
}
/* the two figures ($99E2, $99E4, or only the second) moved for 64 frames at speed v */
static void room13_move_at(int both, uint32_t v, uint32_t resume, int resumed)
{
    if (resumed) goto moved;
    if (both) { c->a[4] = sx(rd16(0xFFFF99E2u)); put16m(c->a[4] - 0x35FE, v); }
    c->a[4] = sx(rd16(0xFFFF99E4u)); put16m(c->a[4] - 0x35FE, v);
    put8(0xFFFF99DFu, 0x40);
    do {
        wait_frame(resume);
moved:
        if (both) { c->a[4] = sx(rd16(0xFFFF99E2u)); call(APPLY_VEL_Y); }
        c->a[4] = sx(rd16(0xFFFF99E4u)); call(APPLY_VEL_Y);
    } while (mem_dec8(0xFFFF99DFu));
}
static void room13_move(int both, uint32_t v, uint32_t resume) { room13_move_at(both, v, resume, 0); }
/* $D48C: the ride */
static void room13_ride_at(uint32_t ret, uint32_t at)
{
    if (!at) push_cont(ret);
    switch (at) {
    case 0xD4BA: room13_move_at(1, 0, 0xD4BA, 1); goto rD4BA;
    case 0xD4D6: goto rD4D6;
    case 0xD4EA: room13_move_at(0, 0, 0xD4EA, 1); goto rD4EA;
    case 0xD4FC: goto rD4FC;
    case 0xD548: goto rD548;
    case 0xD582: goto rD582;
    case 0xD598: goto rD598;
    case 0xD5BA: room13_move_at(0, 0, 0xD5BA, 1); goto rD5BA;
    case 0xD5CE: goto rD5CE;
    case 0xD5F2: room13_move_at(1, 0, 0xD5F2, 1); goto rD5F2;
    }
    c->d[0] = 0x51; F_L(0x51);
    call(QUEUE_SOUND);
    room13_move(1, 0x100, 0xD4BA);                      /* both down */
rD4BA:
    c->d[0] = 0x1E; F_L(0x1E);
    script_wait(0xD4D6);
rD4D6:
    room13_move(0, 0xFF00, 0xD4EA);                     /* the second up */
rD4EA:
    do {                                                /* faded out */
        wait_frame(0xD4FC);
rD4FC:
        mem_dec8(0xFFFF99E6u);
        push32(0xD504); room13_fade(); c->a[7] += 4;
        cmp_byte(0xC8, rd8(0xFFFF99E6u));
    } while (rd8(0xFFFF99E6u) > 0xC8);
    c->a[0] = 0xD6F0;                                   /* the graphics and palette by area */
    cmp_byte(0x10, rd8(AREA));
    if (rd8(AREA) != 0x10) c->a[0] = 0xD6F9;
    put8(0xFFFF9670u, next8(0));
    call(SET_GFX_SLOTS);
    call(STREAM_GFX);
    c->a[0] = 0xFFFF966Eu;
    call(LOAD_PALETTES);
    c->d[0] = 0x15; F_L(0x15);
    call(QUEUE_SOUND);
    c->a[0] = 0xD702;                                   /* the rain: counts and gaps */
    put32m(0xFFFF99EAu, c->a[0]);
    put16m(0xFFFF99E8u, 0x101);
    for (;;) {
        wait_frame(0xD548);
rD548:
        push32(0xD54C); room13_drops(); c->a[7] += 4;
        if (mem_dec8(0xFFFF99E8u)) continue;
        for (;;) {
            put8(0xFFFF99E8u, rd8(0xFFFF99E7u));
            if (mem_dec8(0xFFFF99E9u)) { push32(0xD562); room13_drop(); c->a[7] += 4; break; }
            c->a[0] = rd32(0xFFFF99EAu);
            put8(0xFFFF99E9u, next8(0));
            if (!rd8(0xFFFF99E9u)) goto rained;
            put8(0xFFFF99E7u, next8(0));
            put32m(0xFFFF99EAu, c->a[0]);
        }
    }
rained:
    c->d[0] = 0xFFFFFFFEu; F_L(c->d[0]);
    call(QUEUE_SOUND);
    do {
        wait_frame(0xD582);
rD582:
        push32(0xD586); room13_drops(); c->a[7] += 4;
        F_B(rd8(0xFFFF99DFu));
    } while (rd8(0xFFFF99DFu));
    move_b(0, rd8(0xFFFF9660u));                        /* the room's music again */
    call(QUEUE_SOUND);
    do {                                                /* faded in */
        wait_frame(0xD598);
rD598:
        wr8(0xFFFF99E6u, (uint8_t)add_b(&c->ccr, 1, rd8(0xFFFF99E6u)));
        push32(0xD5A0); room13_fade(); c->a[7] += 4;
        F_B(rd8(0xFFFF99E6u));
    } while (rd8(0xFFFF99E6u));
    room13_move(0, 0x100, 0xD5BA);                      /* the second down */
rD5BA:
    c->d[0] = 0x1E; F_L(0x1E);
    script_wait(0xD5CE);
rD5CE:
    c->d[0] = 0x51; F_L(0x51);
    call(QUEUE_SOUND);
    room13_move(1, 0xFF00, 0xD5F2);                     /* both up */
rD5F2:
    c->a[1] = sx(rd16(0xFFFF99E0u));
    put8(c->a[1] - 0x6071, 1);
    pop_cont();
}
static void room13_ride(uint32_t ret) { room13_ride_at(ret, 0); }
/* $D44C, each frame: the first platform stood on ($9F8E bit 0, not in a conversation): the ride,
 * once, and room flag $10 */
static void room13_frame_at(uint32_t ret, uint32_t at)
{
    if (!at) push_cont(ret);
    if (at == 0xD476) goto rD476;
    move_b(0, rd8(SCRIPT_VARS));
    if (!(c->d[0] & 0xFF)) {
        c->a[1] = sx(rd16(0xFFFF99E0u));
        if (mem_bclr(c->a[1] - 0x6072, 0)) {
            f_bit(rd8(0xFFFF9659u) >> 2 & 1);
            if (!(rd8(0xFFFF9659u) & 4)) {
                wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
                room13_ride(0xD476);
rD476:
                c->d[0] = 0x10; F_L(0x10);
                push32(0xD47C); room_flag_bit(); c->a[7] += 4;
                { uint32_t a = c->a[0] + sx(c->d[1]); uint8_t v = rd8(a); f_bit(v >> (c->d[0] & 7) & 1); wr8(a, (uint8_t)(v | 1 << (c->d[0] & 7))); }
            }
        }
    }
    pop_cont();
}
static void room13_frame(uint32_t ret) { room13_frame_at(ret, 0); }

/* index 14 (area 16's room 3). $D71A: a lift (x $1030, floors $1080 and $15C0) */
static void room14_setup(void) { c->a[0] = 0xD722; lift_setup(); }
/* index 15 (area 14's room 6). $D72A as it loads: before game flag $43, with item $3A the
 * player's speed at least $100 and $9F05 8 (else $99DE set); after it, $99DE set and the
 * effects at $D886 */
static void room15_setup(void)
{
    c->d[0] = 0x43; F_L(0x43);
    push32(0xD730); flag_test(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) {
        wr8(SCRIPT_VARS, 0xFF);
        c->a[0] = 0xD886;
        call(0x2852);
        return;
    }
    c->d[0] = 0x3A; F_L(0x3A);
    call(OWNS);
    if (c->ccr & CC_Z) { wr8(SCRIPT_VARS, 0xFF); return; }
    c->a[0] = sx(rd16(PLAYER_ENT));
    c->d[0] = set_w(c->d[0], 0x100); F_W(0x100);
    cmp_w(&c->ccr, rd16(c->a[0] - 0x3600), c->d[0]);
    if (!((int16_t)0x100 <= (int16_t)rd16(c->a[0] - 0x3600))) put16m(c->a[0] - 0x3600, c->d[0]);
    put8(0xFFFF9F05u, 8);
}

ENTRY(Room13Fade, room13_fade)             /* $D61E */
ENTRY(Room13Drop, room13_drop)             /* $D64E */
ENTRY(Room13Drops, room13_drops)           /* $D6C4 */
ENTRY(Room14Setup, room14_setup)           /* $D71A */
ENTRY(Room15Setup, room15_setup)           /* $D72A */
void Room13Frame(Cpu *cpu) { c = cpu; NEEDS_PASS(0xD44C); cont_68k(room13_frame); }     /* $D44C */
void Room13Ride(Cpu *cpu)  { c = cpu; NEEDS_PASS(0xD48C); cont_68k(room13_ride); }      /* $D48C */
void ScriptWait(Cpu *cpu)  { c = cpu; NEEDS_PASS(0xDAE6); cont_68k(script_wait); }      /* $DAE6 */

/* ---- batch T31: the rooms' scripts 15 and 19-23, the scripts' helpers ----
 * Index 15 (area 14's room 6): the player walks in and gives up item $3A; 19 (area 9's room 8):
 * walked on past a ledge, a picture and message $58; 20 (area 6's rooms 6 and 8): the music's
 * tempo by the player's place; 21 and 22 (area 4's room 3, area 2's room 1): a spot to stand on
 * for a message; 23 (area 12's room 2): its graphics by game flags $4D and $4E. */

/* $DB0A: message d0 run to its end */
static void script_message_at(uint32_t ret, uint32_t at)
{
    if (!at) push_cont(ret);
    if (at == 0xDB1C) goto rDB1C;
    if (at == 0xDB20) goto rDB20;
    call(0x279C);
    do { wait_frame(0xDB1C); rDB1C: call_back(MESSAGES, 0xDB20); rDB20: F_B(rd8(MESSAGE_ON)); } while (rd8(MESSAGE_ON) & 0x80);
    call(0x225C);
    pop_cont();
}
static void script_message(uint32_t ret) { script_message_at(ret, 0); }
/* $DB36, $D87E: the player's buttons for its frame routine: none, right */
static void walk_stop(void) { put8(0xFFFF9F0Bu, 0); }
static void room15_walk(void) { put8(0xFFFF9F0Bu, 8); }

/* index 15. $D766, each frame by step: after 30 frames the player walks right; at $1020 it
 * stops, message $60; then sound 55 and an effect (kind $E) every 6 frames, 32 times (32 frames
 * apart at the last); then a figure at ($1058, $1070) with the effects at $D886 until its
 * animation ends, message $61, item $3A taken and game flag $43 set */
static void room15_frame_at(uint32_t ret, uint32_t at)
{
    if (!at) push_cont(ret);
    switch (at) {
    case 0xD780: goto rD780;
    case 0xD7BC: goto rD7BC;
    case 0xD7C2: goto rD7C2;
    case 0xD83E: goto rD83E;
    case 0xD84A: goto rD84A;
    case 0xD85E: goto rD85E;
    }
    move_b(0, rd8(SCRIPT_VARS));
    switch (rd8(SCRIPT_VARS)) {
    case 0:
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        c->d[0] = 0x1E; F_L(0x1E);
        script_wait(0xD780);
rD780:
        put8(0xFFFF9F05u, 6);
        put32m(0xFFFF9F22u, 0xD87E);
        break;
    case 1:
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
        c->a[4] = sx(rd16(PLAYER_ENT));
        cmp_w(&c->ccr, 0x1020, rd16(c->a[4] - 0x3800));
        if (rd16(c->a[4] - 0x3800) < 0x1020) break;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        put8(0xFFFF9F05u, 4);
        break;
    case 2:
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 2, c->d[0]));
        c->d[0] = 0x1E; F_L(0x1E);
        script_wait(0xD7BC);
rD7BC:
        c->d[0] = 0x60; F_L(0x60);
        script_message(0xD7C2);
rD7C2:
        put16m(0xFFFF99E0u, 0x2010);
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        break;
    case 3:
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 3, c->d[0]));
        if (mem_dec8(0xFFFF99E0u)) break;
        F_B(rd8(0xFFFF99E1u));
        if (rd8(0xFFFF99E1u)) {
            c->d[0] = 0x37; F_L(0x37);
            call(QUEUE_SOUND);
            call(FIND_EFFECT);
            put8(c->a[4] - 0x4000, 0x80);
            put16m(c->a[4] - 0x2BFE, 0xE);
            c->d[0] = 6; F_L(6);
            if (!mem_dec8(0xFFFF99E1u)) { c->d[0] = 0x20; F_L(0x20); }
            put8(0xFFFF99E0u, c->d[0]);
            break;
        }
        call(FIND_EFFECT);
        put16m(0xFFFF99E0u, c->a[4]);
        call(CLEAR_ENTITY);
        put16m(c->a[4] - 0x3800, 0x1058);
        put16m(c->a[4] - 0x3700, 0x1070);
        put16m(c->a[4] - 0x3D00, 0x1614);
        put8(c->a[4] - 0x3CFD, 0x1E);
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        put8(c->a[4] - 0x4000, 0xC0);
        wait_frame(0xD83E);
rD83E:
        c->a[0] = 0xD886;
        call(0x2852);
        do { wait_frame(0xD84A); rD84A: c->a[4] = sx(rd16(0xFFFF99E0u)); F_B(rd8(c->a[4] - 0x3A00)); } while (rd8(c->a[4] - 0x3A00));
        put8(c->a[4] - 0x4000, 0);
        c->d[0] = 0x61; F_L(0x61);
        script_message(0xD85E);
rD85E:
        put8(0xFFFF9F05u, 0);
        c->d[0] = 0x3A; F_L(0x3A);
        call(0x24DC);
        c->d[0] = 0x43; F_L(0x43);
        push32(0xD86E); flag_set(); c->a[7] += 4;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        break;
    default:
        for (int k = 0; k < 3; k++) c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));   /* one at a time, as the chain of subq.b does */
        break;
    }
    pop_cont();
}
static void room15_frame(uint32_t ret) { room15_frame_at(ret, 0); }

/* index 19. $D942: the player's buttons on its walk: right while slow (under $180), and a jump
 * ($20, 8 frames apart) when it is below $1068 */
static void room19_walk(void)
{
    c->d[0] = 8; F_L(8);
    c->a[0] = sx(rd16(PLAYER_ENT));
    cmp_w(&c->ccr, 0x180, rd16(c->a[0] - 0x3600));
    if (!((int16_t)rd16(c->a[0] - 0x3600) < 0x180)) { c->d[0] = 0; F_L(0); }
    F_B(rd8(0xFFFF99DFu));
    if (rd8(0xFFFF99DFu)) mem_dec8(0xFFFF99DFu);
    else {
        cmp_w(&c->ccr, 0x1068, rd16(c->a[0] - 0x3700));
        if (rd16(c->a[0] - 0x3700) >= 0x1068) {
            put8(0xFFFF99DFu, 8);
            or_b(0, 0x20);
        }
    }
    put8(0xFFFF9F0Bu, c->d[0]);
}
/* $D894, each frame by step: past $1140 the player is walked on ($D942); past $1270, unless game
 * flag $3E, the picture at $D92A put at ($12C, $4140); once it has streamed, message $58 (room
 * change 8); then the player's own control back */
static void room19_frame(void)
{
    c->a[4] = sx(rd16(PLAYER_ENT));
    move_b(0, rd8(SCRIPT_VARS));
    switch (rd8(SCRIPT_VARS)) {
    case 0:
        cmp_w(&c->ccr, 0x1140, rd16(c->a[4] - 0x3800));
        if (rd16(c->a[4] - 0x3800) < 0x1140) return;
        put16m(SCRIPT_VARS, 0x100);
        put8(0xFFFF9F05u, 6);
        put32m(0xFFFF9F22u, 0xD942);
        return;
    case 1:
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
        cmp_w(&c->ccr, 0x1270, rd16(c->a[4] - 0x3800));
        if (rd16(c->a[4] - 0x3800) < 0x1270) return;
        c->d[0] = 0x3E; F_L(0x3E);
        push32(0xD8D0); flag_test(); c->a[7] += 4;
        if (!(c->ccr & CC_Z)) { wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 2, rd8(SCRIPT_VARS))); return; }
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        c->a[0] = 0xD92A;
        c->d[6] = set_w(c->d[6], 0x12C); F_W(0x12C);
        c->d[7] = set_w(c->d[7], 0x4140); F_W(0x4140);
        call(0x2974);
        put16m(0xFFFF99E0u, c->a[1]);
        return;
    case 2:
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 2, c->d[0]));
        c->a[1] = sx(rd16(0xFFFF99E0u));
        F_B(rd8(c->a[1] - 0x63CC));
        if (rd8(c->a[1] - 0x63CC) & 0x80) return;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        return;
    case 3:
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 3, c->d[0]));
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        put16m(0xFFFF9C16u, 0x58);
        put16m(0xFFFF9962u, 8);
        return;
    case 4:
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 4, c->d[0]));
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        put8(0xFFFF9F05u, 0);
        return;
    default:
        for (int k = 0; k < 4; k++) c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));   /* one at a time, as the chain of subq.b does */
        return;
    }
}

/* index 20. $D9A2, each frame (unless $99DE): the music's tempo, 0-5 by the player's place
 * across ($1000 on, in 128s; 6 more outside area 6's room 6), written to the Z80's RAM ($1C2B,
 * with $1C2A from the ROM's byte 1) when it changes */
static void room20_frame(void)
{
    F_B(rd8(SCRIPT_VARS));
    if (rd8(SCRIPT_VARS)) return;
    c->a[2] = sx(rd16(PLAYER_ENT));
    c->d[0] = set_w(c->d[0], rd16(c->a[2] - 0x3800)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x1000, c->d[0]));
    if (c->ccr & CC_C) { c->d[0] = 0; F_L(0); }
    else {
        c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 7, 16));
        cmp_w(&c->ccr, 6, c->d[0]);
        if ((c->d[0] & 0xFFFF) >= 6) { c->d[0] = 5; F_L(5); }
    }
    cmp_w(&c->ccr, 0x606, rd16(AREA));
    if (rd16(AREA) != 0x606) c->d[0] = set_w(c->d[0], add_w(&c->ccr, 6, c->d[0]));
    cmp_w(&c->ccr, rd16(0xFFFF99E0u), c->d[0]);
    if ((c->d[0] & 0xFFFF) == rd16(0xFFFF99E0u)) return;
    put16m(0xFFFF99E0u, c->d[0]);
    c->a[0] = 0x00A00000u;
    c->a[1] = 0x00A11100u;
    put16m(c->a[1], 0x100);                             /* the Z80's bus requested */
    do f_bit(rd8(c->a[1]) & 1); while (rd8(c->a[1]) & 1);
    put8(c->a[0] + 0x1C2A, rd8(0x000001));
    put8(c->a[0] + 0x1C2B, c->d[0]);
    put16m(c->a[1], 0);
}
/* $D976 as it loads: unless game flag $38 ($99DE then set), the tempo set and the room's music
 * started if it is not playing, then sound -5 */
static void room20_setup(void)
{
    c->d[0] = 0x38; F_L(0x38);
    push32(0xD97C); flag_test(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) { wr8(SCRIPT_VARS, 0xFF); return; }
    wr8(0xFFFF99E0u, 0xFF);
    push32(0xD98A); room20_frame(); c->a[7] += 4;
    move_b(0, rd8(0xFFFF9660u));
    cmp_byte(rd8(0xFFFF9661u), c->d[0]);
    if ((c->d[0] & 0xFF) != rd8(0xFFFF9661u)) {
        put8(0xFFFF9661u, c->d[0]);
        call(QUEUE_SOUND);
    }
    c->d[0] = 0xFFFFFFFBu; F_L(c->d[0]);
    call(QUEUE_SOUND);
}

/* indexes 21 and 22. $DA00, $DA90 as they load: the spot ($99E2, its x) and message ($99E4):
 * $1190 and $5E unless game flag $4E ($99DE then set), $11A0 and $39 */
static void room21_setup(void)
{
    c->d[0] = 0x4E; F_L(0x4E);
    push32(0xDA06); flag_test(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) { wr8(SCRIPT_VARS, 0xFF); return; }
    put32m(0xFFFF99E2u, 0x1190005E);
}
static void room22_setup(void) { put32m(0xFFFF99E2u, 0x11A00039); }
/* $DA18, each frame: the player's front at the spot, pressing up ($9F02 bit 6) and on the
 * ground: held 8 frames, then the message (room change 8), the player set up again ($DE6A);
 * then until it walks 16 past */
static void room21_frame(void)
{
    c->a[4] = sx(rd16(PLAYER_ENT));
    c->d[1] = 0; F_L(0);
    move_b(1, rd8(c->a[4] - 0x34FE));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[1]));
    move_b(0, rd8(SCRIPT_VARS));
    if (c->d[0] & 0x80) return;
    if (!(c->d[0] & 0xFF)) {
        f_bit(rd8(0xFFFF9F02u) >> 6 & 1);
        if (!(rd8(0xFFFF9F02u) & 0x40)) return;
        cmp_w(&c->ccr, rd16(0xFFFF99E2u), c->d[1]);
        if ((c->d[1] & 0xFFFF) != rd16(0xFFFF99E2u)) return;
        f_bit(rd8(c->a[4] - 0x3FFE) >> 3 & 1);
        if (rd8(c->a[4] - 0x3FFE) & 8) return;
        put16m(SCRIPT_VARS, 0x108);
        put8(0xFFFF9F05u, 8);
        return;
    }
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
    if (!(c->d[0] & 0xFF)) {
        if (mem_dec8(0xFFFF99DFu)) return;
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        put16m(0xFFFF9C16u, rd16(0xFFFF99E4u));
        put16m(0xFFFF9962u, 8);
        return;
    }
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
    if (!(c->d[0] & 0xFF)) {
        put8(0xFFFF9F05u, 0);
        call(0xDE6A);
        wr8(SCRIPT_VARS, (uint8_t)add_b(&c->ccr, 1, rd8(SCRIPT_VARS)));
        return;
    }
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, 0x10, c->d[1]));
    cmp_w(&c->ccr, rd16(0xFFFF99E2u), c->d[1]);
    if ((c->d[1] & 0xFFFF) >= rd16(0xFFFF99E2u)) return;
    put8(SCRIPT_VARS, 0);
}

/* index 23. $DAC8: game flag $4E, or else $4D (ne: either set) */
static void room23_flags(void)
{
    c->d[0] = 0x4E; F_L(0x4E);
    push32(0xDACE); flag_test(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) { c->d[0] = 0; F_L(0); return; }
    c->d[0] = 0x4D; F_L(0x4D);
    flag_test();
}
/* $DAAC: the graphics at $DAB8 streamed in, once ($99DE) */
static void room23_stream(void)
{
    wr8(SCRIPT_VARS, 0xFF);
    c->a[0] = 0xDAB8;
    call(0x23A6);
}
/* $DA9A as it loads: either flag set, the graphics; neither, the palette $5200 */
static void room23_setup(void)
{
    push32(0xDA9C); room23_flags(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) { room23_stream(); return; }
    c->d[0] = set_w(c->d[0], 0x5200); F_W(0x5200);
    c->d[1] = set_w(c->d[1], 0xC12); F_W(0xC12);
    c->d[2] = 0x20; F_L(0x20);
    call(0x22A8);
}
/* $DADA, each frame: the graphics once either flag is set */
static void room23_frame(void)
{
    F_B(rd8(SCRIPT_VARS));
    if (rd8(SCRIPT_VARS)) return;
    push32(0xDAE2); room23_flags(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) room23_stream();
}

ENTRY(WalkStop, walk_stop)                 /* $DB36 */
ENTRY(Room15Walk, room15_walk)             /* $D87E */
ENTRY(Room19Frame, room19_frame)           /* $D894 */
ENTRY(Room19Walk, room19_walk)             /* $D942 */
ENTRY(Room20Setup, room20_setup)           /* $D976 */
ENTRY(Room20Frame, room20_frame)           /* $D9A2 */
ENTRY(Room21Setup, room21_setup)           /* $DA00 */
ENTRY(Room21Frame, room21_frame)           /* $DA18 */
ENTRY(Room22Setup, room22_setup)           /* $DA90 */
ENTRY(Room23Setup, room23_setup)           /* $DA9A */
ENTRY(Room23Flags, room23_flags)           /* $DAC8 */
ENTRY(Room23Frame, room23_frame)           /* $DADA */
void Room15Frame(Cpu *cpu)   { c = cpu; NEEDS_PASS(0xD766); cont_68k(room15_frame); }   /* $D766 */
void ScriptMessage(Cpu *cpu) { c = cpu; NEEDS_PASS(0xDB0A); cont_68k(script_message); } /* $DB0A */

/* ---- batch T32: the room's event list's commands ----
 * A room's event list ($996C, run by $ABBE when it loads) has events (bytes under $C0) and
 * commands ($E0-$FF, the table at $A490, called with a2 at their bytes): the zone's origin and
 * size for the events, things to make, effects, blocks and pictures, flags to set, and jumps on
 * flags, items, the player's form or place. A jump's two bytes are an offset from themselves. */

/* the jump: a2 moved by the word at a2, from it ($ABAC, command $FE) */
static void list_skip(void)
{
    move_b(0, next8(2));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
    move_b(0, rd8(c->a[2]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 1, c->d[0]));
    c->a[2] += sx(c->d[0]);
}
/* the offset after a flag's byte, kept in d2 */
static void list_offset(void)
{
    move_b(2, next8(2));
    c->d[2] = set_w(c->d[2], lsl_n(&c->ccr, c->d[2], 8, 16));
    move_b(2, next8(2));
}
static void list_jump_d2(void) { c->a[2] = c->a[2] - 2 + sx(c->d[2]); }   /* lea -2(a2,d2.w),a2 */
/* $E0 ($A9DA): the zone's origin: x from the byte's high nibble ($F00 on), y from its low ($1000 on, in 12s) */
static void list_origin(void)
{
    move_b(1, next8(2));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 4, 16));
    c->d[0] = 0; F_L(0);
    move_b(0, (uint8_t)c->d[1]);
    and_w(1, 0xF00);
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, 0xF00, c->d[1]));
    put16m(ZONE_X, c->d[1]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x10, c->d[0]));
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
    c->d[0] = set_w(c->d[0], asl_n(&c->ccr, c->d[0], 2, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    put16m(ZONE_Y, c->d[0]);
}
/* $E2, $E3 ($AA04, $AA10): the zone's width, height (in 16s) */
static void list_width(void) { c->d[0] = 0; F_L(0); move_b(0, next8(2)); c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16)); put16m(ZONE_W, c->d[0]); }
static void list_height(void) { c->d[0] = 0; F_L(0); move_b(0, next8(2)); c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16)); put16m(ZONE_H, c->d[0]); }
/* $E8 ($AA1C): a room object of kind 4 from 9 bytes (its look, animation, rate, flags, place);
 * none free: skipped */
static void list_object(void)
{
    c->a[7] -= 2; wr16(c->a[7], c->a[4]);
    call(FIND_EFFECT);
    if (c->ccr & CC_N) c->a[2] += 9;
    else {
        call(CLEAR_ENTITY);
        put8(c->a[4] - 0x3D00, next8(2));
        put8(c->a[4] - 0x3CFF, next8(2));
        move_b(0, next8(2));
        put8(c->a[4] - 0x3CFD, next8(2));
        put8(c->a[4] - 0x3FFE, next8(2));
        put8(c->a[4] - 0x3800, next8(2));
        put8(c->a[4] - 0x37FF, next8(2));
        put8(c->a[4] - 0x3700, next8(2));
        put8(c->a[4] - 0x36FF, next8(2));
        put16m(c->a[4] - 0x2BFE, 4);
        call(SET_ANIM);
        put8(c->a[4] - 0x4000, 0xC0);
    }
    c->a[4] = sx(rd16(c->a[7])); c->a[7] += 2;
}
/* $E9 ($AA66): the effects listed at the offset */
static void list_effects(void)
{
    move_b(0, next8(2));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
    move_b(0, next8(2));
    c->a[0] = c->a[2] - 2 + sx(c->d[0]);
    c->a[7] -= 4; wr32(c->a[7], c->a[2]); F_L(c->a[2]);
    call(SPAWN_EFFECTS);
    c->a[2] = pop32();
}
/* $EA ($AA7A): a thing ($11BF0): its kind, word, place (in 16s, $F00 on) and $A0E0 */
static void list_thing(void)
{
    c->d[5] = 0; F_L(0);
    move_b(5, next8(2));
    move_b(4, next8(2));
    c->d[4] = set_w(c->d[4], lsl_n(&c->ccr, c->d[4], 8, 16));
    move_b(4, next8(2));
    c->d[6] = 0; F_L(0);
    move_b(6, next8(2));
    c->d[6] = set_w(c->d[6], lsl_n(&c->ccr, c->d[6], 4, 16));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 0xF00, c->d[6]));
    c->d[7] = 0; F_L(0);
    move_b(7, next8(2));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 4, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0xF00, c->d[7]));
    put8(0xFFFFA0E0u, next8(2));
    c->a[7] -= 4; wr32(c->a[7], c->a[2]); F_L(c->a[2]);
    call(0x11BF0);
    c->a[2] = pop32();
}
/* the cell from two bytes: ($100 + x, ($100 + y) * $40) */
static void list_cell(void)
{
    c->d[6] = set_w(c->d[6], 0x100); F_W(0x100);
    c->d[7] = set_w(c->d[7], c->d[6]); F_W(c->d[6]);
    move_b(6, next8(2));
    move_b(7, next8(2));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 6, 16));
}
/* $EB ($AAA6): d0 x d1 blocks of the word d2 at the cell */
static void list_blocks(void)
{
    list_cell();
    c->d[0] = 0; F_L(0);
    move_b(0, next8(2));
    c->d[1] = 0; F_L(0);
    move_b(1, next8(2));
    move_b(2, next8(2));
    c->d[2] = set_w(c->d[2], lsl_n(&c->ccr, c->d[2], 8, 16));
    move_b(2, next8(2));
    c->a[7] -= 4; wr32(c->a[7], c->a[2]); F_L(c->a[2]);
    call(SET_MAP_BLOCKS);
    c->a[2] = pop32();
}
/* $EC ($AACA): the picture at the offset put at the cell ($2856) */
static void list_picture(void)
{
    list_cell();
    move_b(0, next8(2));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
    move_b(0, next8(2));
    c->a[7] -= 4; wr32(c->a[7], c->a[2]); F_L(c->a[2]);
    c->a[0] = c->a[2] - 2 + sx(c->d[0]);
    call(0x2856);
    c->a[2] = pop32();
}
/* $F6 ($AAEA): jump if the byte's low 3 bits equal the equipment byte its bits 3-4 pick ($95CE-$95D1) */
static void list_if_equipped(void)
{
    static const uint16_t at[4] = { 0x95CE, 0x95CF, 0x95D0, 0x95D1 };   /* $AB04 */
    move_b(0, next8(2));
    c->d[1] = 0x18; F_L(0x18);
    c->d[1] = set_w(c->d[1], c->d[1] & c->d[0]); F_W(c->d[1]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 16));
    c->a[0] = sx(at[(c->d[1] & 0xFFFF) / 2]);
    and_w(0, 7);
    cmp_byte(rd8(c->a[0]), c->d[0]);
    if ((c->d[0] & 0xFF) == rd8(c->a[0])) { list_skip(); return; }
    c->a[2] += 2;
}
/* $F7 ($AB0C): jump if the player's form bit ($9EF1 bit 4) */
static void list_if_form(void)
{
    f_bit(rd8(0xFFFF9EF1u) >> 4 & 1);
    if (rd8(0xFFFF9EF1u) & 0x10) { list_skip(); return; }
    c->a[2] += 2;
}
/* $F8 ($AB1A): jump if the room flag is set */
static void list_if_room_flag(void)
{
    move_b(0, next8(2));
    push32(0xAB20); room_flag_bit(); c->a[7] += 4;
    list_offset();
    f_bit(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1);
    if (rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1) list_jump_d2();
}
/* $F9 ($AB32): jump if the game flag is set */
static void list_if_flag(void)
{
    move_b(0, next8(2));
    list_offset();
    push32(0xAB3E); flag_test(); c->a[7] += 4;
    if (!(c->ccr & CC_Z)) list_jump_d2();
}
/* $FA, $FB ($AB46, $AB52): a room flag, a game flag set */
static void list_room_flag(void)
{
    move_b(0, next8(2));
    push32(0xAB4C); room_flag_bit(); c->a[7] += 4;
    uint32_t a = c->a[0] + sx(c->d[1]); uint8_t v = rd8(a);
    f_bit(v >> (c->d[0] & 7) & 1); wr8(a, (uint8_t)(v | 1 << (c->d[0] & 7)));
}
static void list_flag(void) { move_b(0, next8(2)); flag_set(); }
/* $FC ($AB58): jump if the player has the item */
static void list_if_owns(void)
{
    move_b(0, next8(2));
    list_offset();
    c->a[7] -= 8; wr32(c->a[7], c->d[2]); wr32(c->a[7] + 4, c->a[2]);
    call(OWNS);
    c->d[2] = rd32(c->a[7]); c->a[2] = rd32(c->a[7] + 4); c->a[7] += 8;
    if (!(c->ccr & CC_Z)) list_jump_d2();
}
/* $FD ($AB74): the zone 16 x 16 at the byte; the player outside it: jump; inside: jump if the
 * flag of the next bytes (0: a room flag, from 1: a game flag; negative: never) is set */
static void list_if_zone(void)
{
    push32(0xAB78); in_zone(); c->a[7] += 4;
    c->d[1] = set_b(c->d[1], c->ccr & CC_C ? 0xFF : 0);
    c->d[0] = 0x10; F_L(0x10);
    put16m(ZONE_W, c->d[0]);
    put16m(ZONE_H, c->d[0]);
    F_B(c->d[1]);
    if (!(c->d[1] & 0xFF)) { c->a[2] += 3; list_skip(); return; }
    c->a[2] += 1;
    uint8_t kind = next8(2); F_B(kind);
    if (!kind) {
        move_b(0, next8(2));
        push32(0xAB98); room_flag_bit(); c->a[7] += 4;
        f_bit(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1);
    } else {
        if (kind & 0x80) { c->a[2] += 2; return; }
        move_b(0, next8(2));
        push32(0xABA6); flag_test(); c->a[7] += 4;
    }
    if (!(c->ccr & CC_Z)) { list_skip(); return; }
    c->a[2] += 2;
}
/* $FF ($ABB8): the list's end: the interpreter's loop left (its return address dropped) */
static void list_end(void) { c->a[7] += 4; c->d[0] = 0; F_L(0); }

/* $A990: whether the player (a4) stands at event d7's place: within 4 of its middle across and
 * in its height (32, or 16 with byte 7) above its foot (d0 0, eq; else -1) */
static void event_near(void)
{
    c->d[0] = set_w(c->d[0], c->d[7]); F_W(c->d[7]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->a[0] = rd32(0x1CC08);
    uint32_t r = c->a[0] + sx(c->d[0]);
    c->d[3] = 0x20; F_L(0x20);
    F_B(rd8(r + 7));
    if (rd8(r + 7)) { c->d[3] = 0x10; F_L(0x10); }
    c->d[1] = set_w(c->d[1], c->d[3]); F_W(c->d[3]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 1, 16));
    c->d[1] = set_b(c->d[1], add_b(&c->ccr, rd8(r + 4), c->d[1]));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(r), c->d[1]));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[1]));
    if (c->ccr & CC_N) c->d[1] = set_w(c->d[1], neg_w(&c->ccr, c->d[1]));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 4, c->d[1]));
    if (!(c->ccr & CC_C)) goto no;
    c->d[1] = 0; F_L(0);
    move_b(1, rd8(r + 5));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(r + 2), c->d[1]));
    c->d[2] = set_w(c->d[2], rd16(c->a[4] - 0x3700)); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[1], c->d[2]));
    if (c->ccr & CC_C) goto no;
    cmp_w(&c->ccr, c->d[3], c->d[2]);
    if (!(c->ccr & CC_C)) goto no;
    c->d[0] = 0; F_L(0);
    return;
no:
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
}

ENTRY(EventNear, event_near)               /* $A990 */
ENTRY(ListOrigin, list_origin)             /* $A9DA */
ENTRY(ListWidth, list_width)               /* $AA04 */
ENTRY(ListHeight, list_height)             /* $AA10 */
ENTRY(ListObject, list_object)             /* $AA1C */
ENTRY(ListEffects, list_effects)           /* $AA66 */
ENTRY(ListThing, list_thing)               /* $AA7A */
ENTRY(ListBlocks, list_blocks)             /* $AAA6 */
ENTRY(ListPicture, list_picture)           /* $AACA */
ENTRY(ListIfEquipped, list_if_equipped)    /* $AAEA */
ENTRY(ListIfForm, list_if_form)            /* $AB0C */
ENTRY(ListIfRoomFlag, list_if_room_flag)   /* $AB1A */
ENTRY(ListIfFlag, list_if_flag)            /* $AB32 */
ENTRY(ListRoomFlag, list_room_flag)        /* $AB46 */
ENTRY(ListFlag, list_flag)                 /* $AB52 */
ENTRY(ListIfOwns, list_if_owns)            /* $AB58 */
ENTRY(ListIfZone, list_if_zone)            /* $AB74 */
ENTRY(ListSkip, list_skip)                 /* $ABAC */
ENTRY(ListEnd, list_end)                   /* $ABB8 */

/* ---- batch T33: the room's event list ----
 * $ABBE runs the room's event list ($996C) as it loads. Bytes $C0-$CF set the zone's width,
 * $D0-$DF its height (in 16s, then back to 16 after each event); $E0-$FF are T32's commands;
 * under $C0 an event, its bytes' count from $A480: 7 a door's picture (open or shut by its flag),
 * 8 a switch, 12 a flagged picture, 13 a gate's place; the others are read by the triggers. The
 * events' handlers jump back into the loop ($AC36), so each event's entry here runs it and then
 * the rest of the list, to its $FF. */

static void (*const list_commands[32])(void) = {   /* $A490: $E0-$FF */
    list_origin, nothing, list_width, list_height, nothing, nothing, nothing, nothing,
    list_object, list_effects, list_thing, list_blocks, list_picture, nothing, nothing, nothing,
    nothing, nothing, nothing, nothing, nothing, nothing, list_if_equipped, list_if_form,
    list_if_room_flag, list_if_flag, list_room_flag, list_flag, list_if_owns, list_if_zone, list_skip, list_end,
};
static const uint16_t list_command_at[32] = {
    0xA9DA, 0xA47E, 0xAA04, 0xAA10, 0xA47E, 0xA47E, 0xA47E, 0xA47E, 0xAA1C, 0xAA66, 0xAA7A, 0xAAA6, 0xAACA, 0xA47E, 0xA47E, 0xA47E,
    0xA47E, 0xA47E, 0xA47E, 0xA47E, 0xA47E, 0xA47E, 0xAAEA, 0xAB0C, 0xAB1A, 0xAB32, 0xAB46, 0xAB52, 0xAB58, 0xAB74, 0xABAC, 0xABB8,
};
/* $AC36: past the event's bytes (then the zone's size back to 16, on with the list) */
static void events_skip(void)
{
    static const uint8_t sizes[16] = { 5, 5, 5, 7, 2, 3, 4, 3, 3, 4, 2, 0, 7, 6, 1, 0 };   /* $A480 */
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(c->a[2] - 1));
    c->a[0] = 0xA480;
    move_b(0, sizes[c->d[0] & 0xF]);
    c->a[2] += sx(c->d[0]);
}
/* event 7 ($AC68): a door's picture, 2 x 2 cells ($160-$165) at the zone's place, open with its flag */
static void event_door(void)
{
    push32(0xAC6C); zone_place(); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], c->d[0]); F_W(c->d[0]);
    c->d[7] = set_w(c->d[7], c->d[1]); F_W(c->d[1]);
    c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, c->d[6], 4, 16));
    c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    c->d[7] = set_w(c->d[7], sub_w(&c->ccr, 0x20, c->d[7]));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 2, 16));
    c->a[3] = LAYER_A;
    move_b(0, rd8(c->a[2] + 1));
    c->a[7] -= 4; wr32(c->a[7], c->a[2]); F_L(c->a[2]);
    push32(0xAC88); flag2_test(); c->a[7] += 4;
    int open = !(c->ccr & CC_Z);
    c->d[0] = set_w(c->d[0], open ? 0x164 : 0x162); F_W(c->d[0]);
    push32(open ? 0xACA0 : 0xAC92); cell_set(); c->a[7] += 4;
    c->d[0] = set_w(c->d[0], open ? 0x165 : 0x163); F_W(c->d[0]);
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 1, c->d[6]));
    push32(0xACAA); cell_set(); c->a[7] += 4;
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x40, c->d[7]));
    c->d[0] = set_w(c->d[0], 0x161); F_W(0x161);
    push32(0xACB6); cell_set(); c->a[7] += 4;
    c->d[6] = set_w(c->d[6], sub_w(&c->ccr, 1, c->d[6]));
    c->d[0] = set_w(c->d[0], 0x160); F_W(0x160);
    push32(0xACC0); cell_set(); c->a[7] += 4;
    c->a[2] = pop32();
}
/* event 8 ($ACC6): a switch ($2CF0 makes it) at the zone's place, its flag at $CE02, its look by the flag */
static void event_switch(void)
{
    push32(0xACCA); zone_place(); c->a[7] += 4;
    move_b(5, rd8(c->a[2] + 2));
    c->d[6] = set_w(c->d[6], c->d[0]); F_W(c->d[0]);
    c->d[7] = 0xFFFFFFF0u; F_L(c->d[7]);
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, c->d[1], c->d[7]));
    call(0x2CF0);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(c->a[2] + 1));
    put16m(c->a[4] - 0x31FE, c->d[0]);
    push32(0xACE6); flag2_test(); c->a[7] += 4;
    c->d[0] = set_b(c->d[0], c->ccr & CC_Z ? 0 : 0xFF);
    c->d[0] = set_b(c->d[0], neg_b(c->d[0]));
    c->d[0] = set_b(c->d[0], add_b(&c->ccr, c->d[0], c->d[0]));
    call(SET_ANIM);
    put8(c->a[4] - 0x4000, 0xC0);
}
/* event 12 ($ADEC): unless its flag (a room flag, or a game flag with bit 8) is set, the picture
 * at its offset put in the rectangle from its bytes ($2A6C) */
static void event_picture(void)
{
    move_b(3, rd8(c->a[2]));
    c->d[3] = set_w(c->d[3], lsl_n(&c->ccr, c->d[3], 8, 16));
    move_b(3, rd8(c->a[2] + 1));
    move_b(0, (uint8_t)c->d[3]);
    f_bit(c->d[3] >> 8 & 1);
    if (!(c->d[3] & 0x100)) {
        move_b(0, (uint8_t)c->d[3]);
        push32(0xAE02); room_flag_bit(); c->a[7] += 4;
        f_bit(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1);
    } else {
        push32(0xAE0E); flag_test(); c->a[7] += 4;
    }
    if (c->ccr & CC_Z) {
        c->d[4] = 0; F_L(0);
        move_b(4, rd8(c->a[2] + 2));
        c->d[4] = set_w(c->d[4], lsl_n(&c->ccr, c->d[4], 4, 16));
        c->d[4] = set_w(c->d[4], add_w(&c->ccr, 0xF00, c->d[4]));
        c->d[5] = 0; F_L(0);
        move_b(5, rd8(c->a[2] + 3));
        c->d[5] = set_w(c->d[5], lsl_n(&c->ccr, c->d[5], 4, 16));
        c->d[5] = set_w(c->d[5], add_w(&c->ccr, 0xF00, c->d[5]));
        move_b(7, rd8(c->a[2] + 4));
        move_b(6, (uint8_t)c->d[7]);
        and_w(6, 0xF);
        c->d[6] = set_w(c->d[6], lsl_n(&c->ccr, c->d[6], 4, 16));
        and_w(7, 0xF0);
        move_b(0, rd8(c->a[2] + 5));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
        move_b(0, rd8(c->a[2] + 6));
        c->a[0] = c->a[2] + 5 + sx(c->d[0]);
        call(0x2A6C);
    }
}
/* event 13 ($AE4E): a gate's places ($9A8B its kind, $9A8C/E one end); unless its game flag is
 * set, the flag ($9A8A) and the other end ($9A90/2); set, $2C08 */
static void event_gate(void)
{
    put8(0xFFFF9A8Bu, rd8(c->a[2] + 1));
    c->d[0] = set_w(c->d[0], 0x100); F_W(0x100);
    move_b(0, rd8(c->a[2] + 2));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    put16m(0xFFFF9A8Cu, c->d[0]);
    c->d[0] = set_w(c->d[0], 0x100); F_W(0x100);
    move_b(0, rd8(c->a[2] + 3));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    put16m(0xFFFF9A8Eu, c->d[0]);
    move_b(0, rd8(c->a[2]));
    push32(0xAE76); flag_test(); c->a[7] += 4;
    if (c->ccr & CC_Z) {
        put8(0xFFFF9A8Au, rd8(c->a[2]));
        c->d[0] = set_w(c->d[0], 0x100); F_W(0x100);
        move_b(0, rd8(c->a[2] + 4));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
        put16m(0xFFFF9A90u, c->d[0]);
        c->d[0] = set_w(c->d[0], 0x100); F_W(0x100);
        move_b(0, rd8(c->a[2] + 5));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
        put16m(0xFFFF9A92u, c->d[0]);
    } else call(0x2C08);
}
/* $ABDC: the list's bytes, to its $FF (reset: the zone's size back to 16 first) */
static void events_loop(int reset)
{
    for (;;) {
        if (reset) {
            c->d[0] = 0x10; F_L(0x10);
            put16m(ZONE_W, c->d[0]);
            put16m(ZONE_H, c->d[0]);
        }
        reset = 0;
        c->d[0] = 0; F_L(0);
        move_b(0, next8(2));
        cmp_byte(0xC0, c->d[0]);
        if ((c->d[0] & 0xFF) < 0xC0) {                  /* an event */
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
            switch ((c->d[0] & 0xFFFF) / 2) {
            case 7: c->a[0] = 0xAC68; event_door(); break;
            case 8: c->a[0] = 0xACC6; event_switch(); break;
            case 12: c->a[0] = 0xADEC; event_picture(); break;
            case 13: c->a[0] = 0xAE4E; event_gate(); break;
            default: c->a[0] = 0xAC36; break;
            }
            events_skip();
            reset = 1;
            continue;
        }
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
        and_w(0, 0x1F);                                 /* a command */
        uint32_t k = c->d[0] & 0x1F;
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
        c->a[0] = list_command_at[k];
        push32(0xAC28);
        if (k == 0x1F) { list_end(); return; }          /* its return address dropped: the list's caller's */
        list_commands[k]();
        c->a[7] += 4;
    }
}
/* $ABBE: the room's event list, if it has one */
static void room_events(void)
{
    put8(0xFFFF9A8Au, 0);
    put16m(0xFFFF9A94u, 0);
    c->d[0] = rd32(0xFFFF996Cu); F_L(c->d[0]);
    if (!c->d[0]) return;
    c->a[2] = c->d[0];
    put16m(ZONE_X, 0x1000);
    put16m(ZONE_Y, 0x1000);
    events_loop(1);
}
/* the events' entries: the event, then the rest of the list */
static void events_door(void) { event_door(); events_skip(); events_loop(1); }
static void events_switch(void) { event_switch(); events_skip(); events_loop(1); }
static void events_picture(void) { event_picture(); events_skip(); events_loop(1); }
static void events_gate(void) { event_gate(); events_skip(); events_loop(1); }

ENTRY(RoomEvents, room_events)             /* $ABBE */
ENTRY(EventDoor, events_door)              /* $AC68 */
ENTRY(EventSwitch, events_switch)          /* $ACC6 */
ENTRY(EventPicture, events_picture)        /* $ADEC */
ENTRY(EventGate, events_gate)              /* $AE4E */

/* ---- batch T34: the room objects' runner and kinds 0, 6, 10 and 16 ----
 * $AEA2 runs the eight room objects each frame by kind (the table at $AECC): 0 a block falling
 * into place (from $C9B4's list), 2 T23's broken block, 4 nothing, 6 a sparkle, 8 T23's lift, 10
 * a mover with a timer, 12 $1A1C6 (still the original's), 14 T23's drifting thing, 16 a chest. */

#define ROOM_OBJECT_COUNT 0xFFFF99FEu

/* $AEF2, kind 6: gone when its animation ends */
static void room_obj_sparkle(void)
{
    f_bit(rd8(e(0xFFFFC000u)) >> 6 & 1);
    if (!(rd8(e(0xFFFFC000u)) & 0x40)) return;
    move_b(0, rd8(e(0xFFFFC600u)));
    if (c->d[0] & 0xFF) return;
    put8(e(0xFFFFC000u), c->d[0]);
}
/* $AF06, kind 10: moved by its speeds, gone when its timer ($DA00) runs out */
static void room_obj_mover(void)
{
    call(APPLY_VELOCITY);
    if (mem_sub_w(e(0xFFFFDA00u), 1)) return;
    put8(e(0xFFFFC000u), 0);
}
/* $AFA0: the falling block's column and floor from its look ($DA02, 3 bytes each at $AFC8):
 * d0 its x, d1 its top, d2 its floor */
static void block_place(void)
{
    c->d[1] = 0; F_L(0);
    move_b(1, rd8(e(0xFFFFDA02u)));
    uint32_t r = 0xAFC8u + (c->d[1] & 0xFFFF);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(r));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x1000, c->d[0]));
    c->d[2] = 0; F_L(0);
    move_b(2, rd8(r + 2));
    c->d[2] = set_w(c->d[2], lsl_n(&c->ccr, c->d[2], 4, 16));
    move_b(1, rd8(r + 1));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 4, 16));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, 0x1000, c->d[1]));
    c->d[2] = set_w(c->d[2], add_w(&c->ccr, c->d[1], c->d[2]));
}
/* $AF16, kind 0: a block that empties its cell and falls to its floor, where it becomes a cell
 * ($300) again */
static void room_obj_block(void)
{
    f_bit(rd8(e(0xFFFFC000u)) >> 6 & 1);
    if (!(rd8(e(0xFFFFC000u)) & 0x40)) {
        call(CLEAR_ENTITY);
        put16m(e(0xFFFFC300u), 0x604);
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        put8(e(0xFFFFC303u), 0x14);
        put16m(e(0xFFFFCC02u), 0x50);
        e_or(0xFFFFD100u, 2);
        put16m(e(0xFFFFCD02u), 0x800);
        push32(0xAF48); block_place(); c->a[7] += 4;
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
        put16m(e(0xFFFFC800u), c->d[0]);
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, 8, c->d[1]));
        put16m(e(0xFFFFC900u), c->d[1]);
        e_or(0xFFFFC000u, 0x40);
        return;
    }
    F_B(rd8(e(0xFFFFD401u)));
    if (!rd8(e(0xFFFFD401u))) {                         /* the first frame: its cell emptied */
        e_inc(0xFFFFD401u, 1);
        push32(0xAF68); block_place(); c->a[7] += 4;
        c->d[6] = set_w(c->d[6], c->d[0]); F_W(c->d[0]);
        c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, c->d[6], 4, 16));
        c->d[7] = set_w(c->d[7], c->d[1]); F_W(c->d[1]);
        c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 2, 16));
        c->d[0] = 0; F_L(0);
        push32(0xAF76); cell_set_a(); c->a[7] += 4;
    }
    call(ACCEL_Y);
    call(APPLY_VEL_Y);
    push32(0xAF80); block_place(); c->a[7] += 4;
    c->d[2] = set_w(c->d[2], add_w(&c->ccr, 8, c->d[2]));
    cmp_w(&c->ccr, rd16(e(0xFFFFC900u)), c->d[2]);
    if ((c->d[2] & 0xFFFF) > rd16(e(0xFFFFC900u))) return;
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 8, c->d[2]));
    c->d[6] = set_w(c->d[6], c->d[0]); F_W(c->d[0]);
    c->d[6] = set_w(c->d[6], lsr_n(&c->ccr, c->d[6], 4, 16));
    c->d[7] = set_w(c->d[7], c->d[2]); F_W(c->d[2]);
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 2, 16));
    c->d[0] = set_w(c->d[0], 0x300); F_W(0x300);
    push32(0xAF9A); cell_set_a(); c->a[7] += 4;
    put8(e(0xFFFFC000u), 0);
}
/* $ADAC (through $998A, as the player opens it): the chest's flag set (unless negative), its
 * drops ($A666, its list $D801), its open look, $9994 bit 15; a negative flag's chest goes
 * after 64 frames */
static void chest_opened(void)
{
    c->a[7] -= 2; wr16(c->a[7], c->a[4]);
    c->a[4] = sx(rd16(0xFFFF9982u));
    c->d[0] = set_w(c->d[0], rd16(e(0xFFFFCE02u))); F_W(c->d[0]);
    if (!(c->d[0] & 0x8000)) { push32(0xADBC); flag2_set(); c->a[7] += 4; }
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(e(0xFFFFD801u)));
    c->d[6] = set_w(c->d[6], rd16(e(0xFFFFC800u))); F_W(c->d[6]);
    c->d[7] = 0x10; F_L(0x10);
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, rd16(e(0xFFFFC900u)), c->d[7]));
    call(0xA666);
    c->d[0] = 2; F_L(2);
    call(SET_ANIM);
    { uint16_t v = (uint16_t)(rd16(0xFFFF9994u) | 0x8000); wr16(0xFFFF9994u, v); F_W(v); }
    F_W(rd16(e(0xFFFFCE02u)));
    if (rd16(e(0xFFFFCE02u)) & 0x8000) put8(e(0xFFFFD800u), 0x40);
    c->a[4] = sx(rd16(c->a[7])); c->a[7] += 2;
}
/* $ACFA, kind 16: a chest. Dropped ($CE00 set): it falls to its place ($DF02) and lands
 * ($2EDE, $4362). Shut: the player standing at it (within 8 across, on its top) opens it by
 * $998A ($ADAC). Opened: after its time ($D800) it becomes kind 6 and vanishes */
static void room_obj_chest(void)
{
    F_B(rd8(e(0xFFFFCE00u)));
    if (rd8(e(0xFFFFCE00u))) {
        put8(e(0xFFFFD100u), 3);
        put16m(e(0xFFFFCB02u), 0x1010);
        put16m(e(0xFFFFCC02u), 0x40);
        put16m(e(0xFFFFCD02u), 0x600);
        call(ACCEL_Y);
        F_W(rd16(e(0xFFFFCA02u)));
        if (!(rd16(e(0xFFFFCA02u)) & 0x8000)) {
            c->d[0] = set_w(c->d[0], rd16(e(0xFFFFDF02u))); F_W(c->d[0]);
            c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 8, c->d[0]));
            cmp_w(&c->ccr, rd16(e(0xFFFFC900u)), c->d[0]);
            if ((c->d[0] & 0xFFFF) < rd16(e(0xFFFFC900u))) {   /* below its place: landed */
                call(0x2EDE);
                call(0x4362);
                if (!(c->ccr & CC_Z)) return;
                put8(e(0xFFFFCE00u), c->d[0]);
                return;
            }
        }
        call(APPLY_VELOCITY);
        return;
    }
    F_B(rd8(e(0xFFFFC302u)));
    if (rd8(e(0xFFFFC302u))) {                          /* open */
        F_B(rd8(e(0xFFFFD800u)));
        if (!rd8(e(0xFFFFD800u)) || e_dec(0xFFFFD800u)) return;
        put16m(e(0xFFFFD402u), 6);
        put16m(e(0xFFFFC300u), 0x120A);
        c->d[0] = 0; F_L(0);
        call(0x07FC);
        return;
    }
    F_B(rd8(DROPS));
    if (rd8(DROPS) & 0x80) return;
    c->a[2] = sx(rd16(PLAYER_ENT));
    c->d[0] = set_w(c->d[0], rd16(c->a[2] - 0x3800)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(e(0xFFFFC800u)), c->d[0]));
    if (c->ccr & CC_N) c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 8, c->d[0]));
    if (!(c->ccr & CC_C)) return;
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(c->a[2] - 0x34FD));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[2] - 0x3700), c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x10, c->d[0]));
    cmp_w(&c->ccr, rd16(e(0xFFFFC900u)), c->d[0]);
    if ((c->d[0] & 0xFFFF) != rd16(e(0xFFFFC900u))) return;
    put16m(0xFFFF9982u, c->a[4]);
    put32m(0xFFFF998Au, 0xADAC);
    wr8(0xFFFF9980u, 0xFF);
    mem_or8(0xFFFF9966u, 1);
}
/* $AEA2: the eight room objects ($E0-$FC) in use, by kind */
static void room_objects(void)
{
    static const uint32_t kinds[9] = { 0xAF16, 0xB00A, 0xAEF0, 0xAEF2, 0xB0DA, 0xAF06, 0x1A1C6, 0xB236, 0xACFA };   /* $AECC */
    c->a[4] = 0xE0;
    put16m(ROOM_OBJECT_COUNT, 0);
    do {
        F_B(rd8(e(0xFFFFC000u)));
        if (rd8(e(0xFFFFC000u)) & 0x80) {
            c->d[0] = set_w(c->d[0], rd16(e(0xFFFFD402u))); F_W(c->d[0]);
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
            c->a[0] = kinds[(c->d[0] & 0xFFFF) / 4];
            call(c->a[0]);
        }
        c->a[4] += 4;
        mem_add_w(ROOM_OBJECT_COUNT, 1);
        cmp_w(&c->ccr, 8, rd16(ROOM_OBJECT_COUNT));
    } while (rd16(ROOM_OBJECT_COUNT) < 8);
}

ENTRY(RoomObjects, room_objects)           /* $AEA2 */
ENTRY(RoomObjNone, nothing)                /* $AEF0 */
ENTRY(RoomObjSparkle, room_obj_sparkle)    /* $AEF2 */
ENTRY(RoomObjMover, room_obj_mover)        /* $AF06 */
ENTRY(RoomObjBlock, room_obj_block)        /* $AF16 */
ENTRY(BlockPlace, block_place)             /* $AFA0 */
ENTRY(RoomObjChest, room_obj_chest)        /* $ACFA */
ENTRY(ChestOpened, chest_opened)           /* $ADAC */

/* ---- batch T35: the game task's room entry, next room, game over and the room's message ----
 * Parts of the game's task ($8EB4-$92A2) that the room changes leave to. The task's loop itself
 * ($8FFC-$9100, $91EA) stays the original's for now: it calls $C7E6 (the room scripts, some of
 * them still the original's code that waits) and $DB3C (which waits), and C cannot call such
 * code. */

/* $8F98: the start point by $965E (into $9676), a0 past it (at its message) */
static void start_entry(void)
{
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(0xFFFF965Eu));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    c->a[0] = 0x8FAA + sx(c->d[0]);
    put16m(0xFFFF9676u, next16(0));
}
/* widescreen: walking in through an exit (arrival 2) at the edge of a room widened by its painted
 * sides, the player starts as far out in them as its exits are moved (trig_exit); asked once the
 * layers are set up ($9808), which the sides' check reads */
static void wide_arrival(void)
{
    int w = port_wide_sides();
    if (!w || rd8(0xFFFF9678u) != 2) return;
    uint32_t e = sx(rd16(0xFFFF9EEEu)) - 0x3800;
    int x = rd16(e), right = 0x1000 + (rd16(LAYER_A + 0x44) + 1) * 16;
    if (x >= right - 16) wr16(e, (uint16_t)(x + w));
    else if (x < 0x1000 + 16) wr16(e, (uint16_t)(x - w));
}
/* $8EE6: a room entered: loaded ($9330) with its layers, events, script, gates and things, its
 * palettes, its music (unless a message $9686 comes first), then the arrival by $9678 */
static void room_enter(void)
{
    { uint8_t v = rd8(0xFFFF9659u); f_bit(v & 1); wr8(0xFFFF9659u, v | 1); }
    mem_bclr(0xFFFF9659u, 4);
    put16m(0xFFFF965Au, 0);
    call(0x5188);
    call(0x9330);
    call(0xDBBE);
    call(0x9720);
    call(0x9808);
    wide_arrival();
    call(0xABBE);
    call(0xC75C);
    call(0x97F0);
    call(0xB50C);
    call(0x133B2);
    put16m(0xFFFF965Cu, 0);
    c->a[0] = 0xFFFF966Eu;
    call(LOAD_PALETTES);
    put16m(0xFFFF8B56u, rd16(0xFFFF966Cu));
    F_W(rd16(0xFFFF9686u));
    if (rd16(0xFFFF9686u) & 0x8000) {
        move_b(0, rd8(0xFFFF9660u));
        cmp_byte(rd8(0xFFFF9661u), c->d[0]);
        if ((c->d[0] & 0xFF) != rd8(0xFFFF9661u)) { put8(0xFFFF9661u, c->d[0]); call(QUEUE_SOUND); }
    }
    move_b(0, rd8(0xFFFF9658u));
    if (!(c->d[0] & 0x80)) {
        wr8(0xFFFF9658u, 0xFF);
        F_B(c->d[0]);
        if (c->d[0] & 0xFF) { uint16_t v = (uint16_t)(rd16(0xFFFF9994u) | 1); wr16(0xFFFF9994u, v); F_W(v); }
    }
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(0xFFFF9678u));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] = rd32(0x8F6C + (c->d[0] & 0xFFFF));       /* the arrivals' table (T20) */
    leave_to(c->a[0]);
}
/* $8EB4: the game (re)started at its start point ($965E): its message, the state reset ($8E56),
 * the room's music, then the room entered */
static void game_restart(void)
{
    call(0x1C254);
    push32(0x8EBE); start_entry(); c->a[7] += 4;
    put16m(0xFFFF9C16u, next16(0));
    put8(0xFFFF966Au, 0);
    call(0x8E56);
    put8(0xFFFF9659u, 0);
    put16m(0xFFFF9992u, 0);
    wr8(0xFFFF9661u, 0xFF);
    put8(0xFFFF965Fu, 0);
    call(0xA37E);
    call(0x92D4);
    put16m(0xFFFF9F30u, 4);
    room_enter();
}
/* $8FEA, arrivals 0 and 1: the screen faded in, then the game */
static void arrive_plain(uint32_t at)
{
    if (at == 0x8FF2) goto r8FF2;
    call(0x581A);
    do {
        wait_frame(0x8FF2);
r8FF2:
        f_bit(rd8(0xFFFF8C56u) >> 4 & 1);
    } while (!(rd8(0xFFFF8C56u) & 0x10));
    leave_to(GAME_LOOP);
}
/* $9100: the next room: after a restart's room change ($965F bit 1) only bit 2; else the area's
 * data again if it changed ($966A bit 1), the music faded if it differs, the screen faded out,
 * the room entered */
static void next_room(uint32_t at)
{
    if (at == 0x9138) goto r9138;
    if (mem_bclr(0xFFFF965Fu, 1)) { mem_or8(0xFFFF965Fu, 4); leave_to(GAME_LOOP); return; }
    if (mem_bclr(0xFFFF966Au, 1)) call(0xA37E);
    call(0x92D4);
    cmp_byte(rd8(0xFFFF9661u), c->d[0]);
    if ((c->d[0] & 0xFF) != rd8(0xFFFF9661u)) { c->d[0] = 0xFFFFFFFEu; F_L(c->d[0]); call(QUEUE_SOUND); }
    call(0x5842);
    do {
        wait_frame(0x9138);
r9138:
        f_bit(rd8(0xFFFF8C56u) >> 4 & 1);
    } while (!(rd8(0xFFFF8C56u) & 0x10));
    room_enter();
}
/* $9144: the game over: faded out, its picture ($91BE, $91C2) and palette faded in, held 240
 * frames or until a button, faded out, then the game restarted ($4CA8, 0) */
static void game_over(uint32_t at)
{
    switch (at) { case 0x914C: goto r914C; case 0x9182: goto r9182; case 0x9194: goto r9194; case 0x91AA: goto r91AA; }
    call(0x5842);
    do { wait_frame(0x914C); r914C: f_bit(rd8(0xFFFF8C56u) >> 4 & 1); } while (!(rd8(0xFFFF8C56u) & 0x10));
    call(0x5188);
    c->a[0] = 0x91BE;
    call(SET_GFX_SLOTS);
    call(STREAM_GFX);
    c->a[2] = 0x91C2;
    put8(0xFFFF8C76u, 2);
    call(0x69AE);
    c->a[0] = 0x91BA;
    call(LOAD_PALETTES);
    call(0x581A);
    do { wait_frame(0x9182); r9182: f_bit(rd8(0xFFFF8C56u) >> 4 & 1); } while (!(rd8(0xFFFF8C56u) & 0x10));
    put16m(c->a[5] + 0x40, 0xF0);
    for (;;) {
        wait_frame(0x9194);
r9194:
        c->d[0] = 0xFFFFFFF0u; F_L(c->d[0]);
        c->d[0] = set_b(c->d[0], c->d[0] & rd8(0xFFFF8A7Cu)); F_B(c->d[0]);
        if (c->d[0] & 0xFF) break;
        if (!mem_sub_w(c->a[5] + 0x40, 1)) break;
    }
    call(0x5842);
    do { wait_frame(0x91AA); r91AA: f_bit(rd8(0xFFFF8C56u) >> 4 & 1); } while (!(rd8(0xFFFF8C56u) & 0x10));
    c->d[0] = 0; F_L(0);
    leave_to(0x4CA8);
}
/* $9228 (from the loop: a message $9686 waiting and $9667 clear): sound 56, the player kept
 * ($9F05 8) while the screen fades through 56 steps, the palette $9689, back over 56, then the
 * message $9686 run, and the room's music */
static void room_message(uint32_t at)
{
    switch (at) { case 0x923C: goto r923C; case 0x9240: goto r9240; case 0x9264: goto r9264; case 0x927C: goto r927C; case 0x9280: goto r9280; }
    c->d[0] = 0x38; F_L(0x38);
    call(QUEUE_SOUND);
    put8(0xFFFF9F05u, 8);
    put8(0xFFFF9666u, 0);
    do {
        wait_frame(0x923C);
r923C:
        call_back(0xDD08, 0x9240);
r9240:
        push32(0x9242); fade_step(); c->a[7] += 4;
        wr8(0xFFFF9666u, (uint8_t)add_b(&c->ccr, 1, rd8(0xFFFF9666u)));
        cmp_byte(0x38, rd8(0xFFFF9666u));
    } while (rd8(0xFFFF9666u) < 0x38);
    put8(0xFFFF9F05u, 0);
    put8(0xFFFF9671u, rd8(0xFFFF9689u));
    c->a[0] = 0xFFFF966Eu;
    call(LOAD_PALETTES);
    do {
        wait_frame(0x9264);
r9264:
        mem_dec8(0xFFFF9666u);
        push32(0x926A); fade_step(); c->a[7] += 4;
        F_B(rd8(0xFFFF9666u));
    } while (rd8(0xFFFF9666u));
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9686u)); F_W(c->d[0]);
    call(0x279C);
    do { wait_frame(0x927C); r927C: call_back(MESSAGES, 0x9280); r9280: F_B(rd8(MESSAGE_ON)); } while (rd8(MESSAGE_ON) & 0x80);
    call(0x225C);
    wr8(0xFFFF9686u, 0xFF);
    move_b(0, rd8(0xFFFF9660u));
    cmp_byte(rd8(0xFFFF9661u), c->d[0]);
    if ((c->d[0] & 0xFF) != rd8(0xFFFF9661u)) { put8(0xFFFF9661u, c->d[0]); call(QUEUE_SOUND); }
    leave_to(GAME_LOOP);
}

ENTRY(StartEntry, start_entry)             /* $8F98 */
/* these leave by port_continue_at, so ret is theirs to set */
void GameRestart(Cpu *cpu) { c = cpu; game_restart(); }                                  /* $8EB4 */
void RoomEnter(Cpu *cpu)   { c = cpu; room_enter(); }                                    /* $8EE6 */
void ArrivePlain(Cpu *cpu) { c = cpu; NEEDS_PASS(0x8FEA); arrive_plain(0); }              /* $8FEA */
void NextRoom(Cpu *cpu)    { c = cpu; NEEDS_PASS(0x9100); next_room(0); }                 /* $9100 */
void GameOver(Cpu *cpu)    { c = cpu; NEEDS_PASS(0x9144); game_over(0); }                 /* $9144 */
void RoomMessage(Cpu *cpu) { c = cpu; NEEDS_PASS(0x9228); room_message(0); }              /* $9228 */

/* ---- batch T36: the HUD's panel and the status screen's labels ----
 * Small pieces of the status screen's region still in the original, which the C calls. */

/* $8596: the gold's label at (5, $16) ($84E7), then the gold ($85A8) */
static void gold_label(void)
{
    c->d[3] = 6; F_L(6);
    c->d[7] = set_w(c->d[7], 0x1605); F_W(0x1605);
    call(0x83B0);
    c->a[1] = 0x84E7;
    call(0x786A);
    call(0x85A8);
}
/* $7544: the HUD's panel drawn whole (a room's start, $97F0): the slots' looks unset, the frame
 * ($7F54), the boss bar cleared ($89B6), the hearts ($8AA2), the HUD ($76C4), palette step 8,
 * the gold */
static void hud_panel(void)
{
    c->d[0] = 0xFFFFFF80u; F_L(c->d[0]);
    put8(0xFFFF95DCu, c->d[0]);
    put8(0xFFFF95DDu, c->d[0]);
    put32m(0xFFFF9628u, 0);
    put32m(0xFFFF9602u, 0);
    put16m(0xFFFF95F2u, 0);
    call(0x7F54);
    call(0x89B6);
    call(0x8AA2);
    call(0x76C4);
    put8(0xFFFF8A77u, 8);
    mem_or8(0xFFFF8006u, 4);
    gold_label();
}
/* $84C4: three labels ($84DE) at (9, 2), (9, 4), (9, 6) */
static void three_labels(void)
{
    c->d[7] = set_w(c->d[7], 0x209); F_W(0x209);
    c->a[1] = 0x84DE;
    c->d[3] = 2; F_L(2);
    do {
        call(0x83B0);
        call(0x786A);
        c->d[7] = set_b(c->d[7], add_b(&c->ccr, 2, c->d[7]));
    } while (dbra(3));
}
/* $850C: d3 digits from a0 backwards to the VDP's data port (tiles $8030 on) */
static void digits_out(void)
{
    c->d[3] = set_w(c->d[3], sub_w(&c->ccr, 1, c->d[3]));
    do {
        c->d[0] = 0; F_L(0);
        c->a[0] -= 1;
        move_b(0, rd8(c->a[0]));
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x8030, c->d[0]));
        wr16(0xC00000, c->d[0]); F_W(c->d[0]);
    } while (dbra(3));
}

ENTRY(HudPanel, hud_panel)                 /* $7544 */
ENTRY(GoldLabel, gold_label)               /* $8596 */
ENTRY(ThreeLabels, three_labels)           /* $84C4 */
ENTRY(DigitsOut, digits_out)               /* $850C */

/* ---- batch T37: the player's looks and the change of equipment ----
 * The player's graphics follow the equipment: $F5FC loads the body's (by the armour, the form,
 * under water), the sword's and the shield's, and $9F32's extra block. When the status screen
 * closes, $DB3C sees whether the equipment changed: if so the rest of the frame is dropped, the
 * player strikes the change pose, and once its animation flags it the looks are loaded again. */

/* $F670: the block at $F680 by the offset in d0 */
static void looks_block(void)
{
    c->a[0] = 0xF680;
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    call(SET_GFX_SLOTS);
    call(STREAM_GFX);
}
/* $F668: $9F32's block, unless it is negative */
static void looks_extra(void)
{
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9F32u)); F_W(c->d[0]);
    if (c->d[0] & 0x8000) return;
    looks_block();
}
/* $F698: the body: transformed ($9EF1 bit 4) $F6F0; else $F6E0, or $F6E8 under water ($966B
 * bit 0) with the gear for it ($9EF1 bit 5); then $F6D8 or $F6DC by $966B bit 2 */
static void looks_body(void)
{
    c->a[0] = 0xF6F0;
    f_bit(rd8(0xFFFF9EF1u) >> 4 & 1);
    if (!(rd8(0xFFFF9EF1u) & 0x10)) {
        c->a[0] = 0xF6E0;
        f_bit(rd8(0xFFFF966Bu) & 1);
        if (rd8(0xFFFF966Bu) & 1) {
            f_bit(rd8(0xFFFF9EF1u) >> 5 & 1);
            if (rd8(0xFFFF9EF1u) & 0x20) c->a[0] = 0xF6E8;
        }
    }
    call(SET_GFX_SLOTS);
    c->a[0] = 0xF6D8;
    f_bit(rd8(0xFFFF966Bu) >> 2 & 1);
    if (!(rd8(0xFFFF966Bu) & 4)) c->a[0] = 0xF6DC;
    call(SET_GFX_SLOTS);
    call(STREAM_GFX);
}
/* $F6F8: the sword's block ($95CE) from the table at $F710 */
static void looks_sword(void)
{
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(0xFFFF95CEu));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] = 0xF710;
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    call(SET_GFX_SLOTS);
    call(STREAM_GFX);
}
/* $F742: the shield's tiles ($95D0, none if negative) copied to $B80 */
static void looks_shield(void)
{
    c->d[1] = 0; F_L(0);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(0xFFFF95D0u));
    if (!(c->d[0] & 0x80)) {
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
        c->d[1] = set_w(c->d[1], rd16(0xF75E + sx(c->d[0]))); F_W(c->d[1]);
    }
    c->d[0] = set_w(c->d[0], 0xB80); F_W(0xB80);
    c->d[2] = set_w(c->d[2], 0x20); F_W(0x20);
    call(0x22A8);
}
/* $F5FC: the player's looks: transformed, only the body (palette 4, $9F12 4); else the body, the
 * sword, the shield, palette by the sword ($F660); then $9F32's block */
static void looks(void)
{
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    c->d[0] = 0; F_L(0);
    put8(0xFFFF9F04u, 0);
    put16m(c->a[4] - 0x3D00, 0);
    put8(0xFFFF9F12u, 0xA);
    f_bit(rd8(0xFFFF9EF1u) >> 4 & 1);
    if (rd8(0xFFFF9EF1u) & 0x10) {
        put8(0xFFFF9F12u, 4);
        put8(0xFFFF9F04u, 2);
        put8(c->a[4] - 0x3CFF, 4);
        looks_body();
    } else {
        f_bit(rd8(0xFFFF966Bu) & 1);
        if (rd8(0xFFFF966Bu) & 1) {
            f_bit(rd8(0xFFFF9EF1u) >> 5 & 1);
            if (rd8(0xFFFF9EF1u) & 0x20) put8(0xFFFF9F04u, 1);
        }
        move_b(0, rd8(0xFFFF95CEu));
        put8(c->a[4] - 0x3CFF, rd8(0xF660 + sx(c->d[0])));
        looks_body();
        looks_sword();
        looks_shield();
    }
    looks_extra();
}
/* $DB3C (after the status screen): the player's $95F4 and $95F6/16 kept; if not transformed and
 * the equipment ($95CE) differs from the last ($95D2): the caller's frame is dropped (its return
 * kept in $9F1E, the stack back to the game loop's), the change pose ($1A, or $1C with palette 2),
 * a frame; once the animation flags it (bit 0 of $C002) the looks, a frame, and unless the
 * player's animation is still on, back to the caller with $9F02 bit 4. Returns 1 if it left
 * elsewhere. */
static int equip_change(uint32_t at)
{
    if (at == 0xDB8C) goto rDB8C;
    if (at == 0xDBA2) goto rDBA2;
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    put16m(c->a[4] - 0x2700, rd16(0xFFFF95F4u));
    c->d[0] = set_w(c->d[0], rd16(0xFFFF95F6u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 4, 16));
    put16m(c->a[4] - 0x26FE, c->d[0]);
    f_bit(rd8(0xFFFF9EF1u) >> 4 & 1);
    if (rd8(0xFFFF9EF1u) & 0x10) return 0;
    c->d[0] = rd32(0xFFFF95CEu); F_L(c->d[0]);
    cmp_l(&c->ccr, rd32(0xFFFF95D2u), c->d[0]);
    if (c->d[0] == rd32(0xFFFF95D2u)) return 0;
    uint32_t ret = rd32(c->a[7]); c->a[7] += 4;
    put32m(0xFFFF9F1Eu, ret);
    c->a[7] = rd32(0xFFFF9662u);
    c->d[0] = 0x1A; F_L(0x1A);
    cmp_byte(2, rd8(c->a[4] - 0x3CFF));
    if (rd8(c->a[4] - 0x3CFF) == 2) { c->d[0] = 0x1C; F_L(0x1C); }
    mem_and8(c->a[4] - 0x4000, 0xDF);
    mem_and8(c->a[4] - 0x3FFE, 0xFC);
    call(SET_ANIM);
    wait_frame(0xDB8C);
rDB8C:
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    if (!mem_bclr(c->a[4] - 0x3FFE, 0)) return 0;   /* rts, at the game loop's level */
    looks();
    wait_frame(0xDBA2);
rDBA2:
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    F_B(rd8(c->a[4] - 0x3A00));
    if (rd8(c->a[4] - 0x3A00)) return 0;
    put16m(0xFFFF9980u, 0);
    mem_or8(0xFFFF9F02u, 0x10);
    c->a[0] = rd32(0xFFFF9F1Eu);
    leave_to(c->a[0]);
    return 1;
}

ENTRY(Looks, looks)                        /* $F5FC */
ENTRY(LooksExtra, looks_extra)             /* $F668 */
ENTRY(LooksBlock, looks_block)             /* $F670 */
ENTRY(LooksBody, looks_body)               /* $F698 */
ENTRY(LooksSword, looks_sword)             /* $F6F8 */
ENTRY(LooksShield, looks_shield)           /* $F742 */
void EquipChange(Cpu *cpu) { c = cpu; NEEDS_PASS(0xDB3C); if (!equip_change(0)) c->ret = 0; }   /* $DB3C */
/* batch N3f: resumed after its frames (a loaded state): its rts then ends the pass, as the
 * original's at the game loop's level does */
void EquipChangeAtDB8C(Cpu *cpu) { c = cpu; NEEDS_PASS(0xDB8C); if (!equip_change(0xDB8C)) c->ret = 0; }
void EquipChangeAtDBA2(Cpu *cpu) { c = cpu; NEEDS_PASS(0xDBA2); if (!equip_change(0xDBA2)) c->ret = 0; }

/* ---- batch T38: the player placed in a room, and the companion set up ----
 * As a room is entered ($8EE6), $DBBE sets the companion up, resets the player and places it by
 * the arrival ($9678). The companion ($9F30 its slot, $9F32 its kind 0-6 or negative for none)
 * follows only in the areas its mask allows ($FA08); a room's command ($95E8) or a message's
 * ($1A24) changes it. */

#define COMPANION     0xFFFF9F30u   /* word: its slot */
#define COMPANION_K   0xFFFF9F32u   /* word: its kind (0, 2, 4, 6), negative if none */

static uint32_t p4(int32_t off) { return c->a[4] + (uint32_t)off; }   /* -$xxxx(a4) */
static uint32_t p2(int32_t off) { return c->a[2] + (uint32_t)off; }   /* -$xxxx(a2) */
static uint32_t p0(int32_t off) { return c->a[0] + (uint32_t)off; }   /* -$xxxx(a0) */

/* $FA20 (kinds 0 and 4): in use, cleared ($A36), its kind byte, $A148 kept, palette 8, drawn */
static void companion_new_a(void)
{
    put8(p4(-0x4000), 0x80);
    call(0xA36);
    put8(p4(-0x2BFF), c->d[0]);
    put16m(p4(-0x2600), rd16(0xFFFFA148u));
    put8(p4(-0x3CFD), 8);
    put8(p4(-0x3FFE), 0x80);
}
/* $FA42 (kinds 2 and 6): its area kept, cleared, its kind and state bytes, speed $40, palette
 * 8, in use, $9F76 set */
static void companion_new_b(void)
{
    put16m(p4(-0x2600), rd16(0xFFFF9668u));
    call(0xA36);
    put8(p4(-0x2BFF), c->d[0]);
    put8(p4(-0x3100), c->d[0]);
    put16m(p4(-0x33FE), 0x40);
    put8(p4(-0x3CFD), 8);
    put8(p4(-0x4000), 0x80);
    wr8(0xFFFF9F76u, 0xFF);
}
/* $F9C8: the companion d0 made, unless it is the boss's room ($966B bit 7), the area forbids it
 * ($966B bit 5), under water (bit 0), or its mask ($FA08) has the area's bit: then none */
static void companion_make(void)
{
    move_b(1, rd8(0xFFFF966Bu));
    if (c->d[1] & 0x80) return;
    f_bit(c->d[1] >> 5 & 1);
    if (c->d[1] & 0x20) return;
    f_bit(c->d[1] & 1);
    if (!(c->d[1] & 1)) {
        c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[1]);
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, c->d[1], c->d[1]));
        c->d[1] = rd32(0xFA08 + sx(c->d[1])); F_L(c->d[1]);
        move_b(2, rd8(0xFFFF9668u));
        f_bit(c->d[1] >> (c->d[2] & 31) & 1);
        if (!(c->d[1] >> (c->d[2] & 31) & 1)) {
            c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->a[4]);
            c->a[4] = sx(rd16(COMPANION));
            put16m(p4(-0x2BFE), c->d[0]);
            c->a[0] = 0xFA18 + sx(rd16(0xFA18 + sx(c->d[0])));
            if (c->a[0] == 0xFA20) companion_new_a();
            else if (c->a[0] == 0xFA42) companion_new_b();
            else call(c->a[0]);                     /* past the table: as the original jumps */
            c->a[4] = sx(rd16(c->a[7])); c->a[7] += 2;
            return;
        }
    }
    wr8(COMPANION_K, 0xFF);                         /* st.b: none */
}
/* $F9C0: the companion made again, if there is one */
static void companion_again(void)
{
    c->d[0] = set_w(c->d[0], rd16(COMPANION_K)); F_W(c->d[0]);
    if (c->d[0] & 0x8000) return;
    companion_make();
}
/* $F9B2: palette slot $200 on from the companion's kind */
static void companion_palette(void)
{
    c->d[0] = set_w(c->d[0], rd16(p4(-0x2BFE))); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0x200, c->d[0]));
    put16m(p4(-0x3D00), c->d[0]);
}
/* $F99C: the companion hidden (not drawn, its state cleared), if there is one */
static void companion_hide(void)
{
    F_W(rd16(COMPANION_K));
    if (rd16(COMPANION_K) & 0x8000) return;
    c->a[0] = sx(rd16(COMPANION));
    mem_and8(p0(-0x4000), 0xBF);
    put8(p0(-0x2BFF), 0);
}
/* $F8E0: the companion's speeds, and x d0 and y d1 for it and its two followers' fields */
static void companion_at(void)
{
    put16m(p2(-0x33FE), 0x40);
    put16m(p2(-0x32FE), 0x400);
    put16m(p2(-0x3800), c->d[0]);
    put16m(p2(-0x2E00), c->d[0]);
    put16m(p2(-0x2100), c->d[0]);
    put16m(p2(-0x3700), c->d[1]);
    put16m(p2(-0x2DFE), c->d[1]);
    put16m(p2(-0x20FE), c->d[1]);
}
/* $F8A8: the companion's graphics block d0 ($F670), a2 kept */
static void companion_gfx(void)
{
    c->a[7] -= 4; wr32(c->a[7], c->a[2]);
    looks_block();
    c->a[2] = rd32(c->a[7]); c->a[7] += 4;
}
/* $F8B2 (the room's companion 2): if game flag $38 is set, its graphics, made, state $10, at
 * ($12D4, $1090) */
static void companion_room_2(void)
{
    c->d[0] = 0x38; F_L(0x38);
    call(0x274C);
    if (c->ccr & CC_Z) return;
    c->a[7] -= 4; wr32(c->a[7], c->a[2]);
    c->d[0] = 2; F_L(2);
    looks_block();
    c->d[0] = 2; F_L(2);
    companion_make();
    c->a[2] = sx(rd16(COMPANION));
    put8(p2(-0x2BFF), 0x10);
    c->d[0] = set_w(c->d[0], 0x12D4); F_W(0x12D4);
    c->d[1] = set_w(c->d[1], 0x1090); F_W(0x1090);
    companion_at();
    c->a[2] = rd32(c->a[7]); c->a[7] += 4;
}
/* $F880 (a room's command, $95E8, with a companion d0 other than the one there is): none, its
 * slot freed, then the room's: graphics 0, 4 or 6, or companion 2's ($F8B2) */
static void companion_room(void)
{
    wr8(COMPANION_K, 0xFF);
    c->a[0] = sx(rd16(COMPANION));
    put16m(p0(-0x4000), 0);
    switch (c->d[0] & 0xFFFF) {                     /* jmp $F896(pc, d0.w) */
    case 0: c->d[0] = 0; F_L(0); companion_gfx(); break;
    case 2: companion_room_2(); break;
    case 4: c->d[0] = 4; F_L(4); companion_gfx(); break;
    case 6: c->d[0] = 6; F_L(6); companion_gfx(); break;
    default: c->a[0] = 0xF896; c->a[0] += sx(rd16(c->a[0] + sx(c->d[0]))); call(c->a[0]); break;   /* past the table: as the original jumps */
    }
}
/* $F906 (a message's command, $1A24): companion d0 joins */
static void companion_join(void)
{
    switch (c->d[0] & 0xFFFF) {                     /* jmp $F910(pc, d0.w) */
    case 0:                                         /* $F942: made, state $A, the pose $402 found ($D36E), animation 2 */
        c->d[0] = 0; F_L(0);
        put16m(COMPANION_K, 0);
        companion_again();
        c->a[0] = sx(rd16(COMPANION));
        put8(p0(-0x2BFF), 0xA);
        c->d[1] = set_w(c->d[1], 0x402); F_W(0x402);
        call(0xD36E);
        c->d[0] = 2; F_L(2);
        call(SET_ANIM);
        break;
    case 2:                                         /* $F966: with game flag $38, speed $40, state $C */
        c->d[0] = 0x38; F_L(0x38);
        call(0x274C);
        if (c->ccr & CC_Z) break;
        c->a[0] = sx(rd16(COMPANION));
        put16m(p0(-0x33FE), 0x40);
        put8(p0(-0x2BFF), 0xC);
        put16m(COMPANION_K, 2);
        break;
    case 4:                                         /* $F986: made, state $A */
        c->d[0] = 4; F_L(4);
        put16m(COMPANION_K, 4);
        companion_again();
        c->a[0] = sx(rd16(COMPANION));
        put8(p0(-0x2BFF), 0xA);
        break;
    case 6:                                         /* $F918: made, speed $40, state $C, at ($1210, $1090) */
        c->d[0] = 6; F_L(6);
        put16m(COMPANION_K, 6);
        companion_again();
        c->a[7] -= 4; wr32(c->a[7], c->a[2]);
        c->a[2] = sx(rd16(COMPANION));
        put16m(p2(-0x33FE), 0x40);
        put8(p2(-0x2BFF), 0xC);
        c->d[0] = set_w(c->d[0], 0x1210); F_W(0x1210);
        c->d[1] = set_w(c->d[1], 0x1090); F_W(0x1090);
        companion_at();
        c->a[2] = rd32(c->a[7]); c->a[7] += 4;
        break;
    default: c->a[0] = 0xF910; c->a[0] += sx(rd16(c->a[0] + sx(c->d[0]))); call(c->a[0]); break;   /* past the table: as the original jumps */
    }
}

/* $DCCC: the player reset: in use only, the pickups and $9F16 cleared, $9F03 and its sprite bits
 * 5-6 off, the screen set up ($8BB6), $95F4 and $95F6/16 kept, the view's limits ($86BC) */
static void player_reset(void)
{
    put8(p4(-0x4000), 0x80);
    put16m(0xFFFF9F7Au, 0);
    put8(0xFFFF9F78u, 0);
    put16m(0xFFFF9F16u, 0);
    mem_and8(0xFFFF9F03u, 0x9F);
    mem_and8(p4(-0x3FFE), 0x9F);
    call(0x8BB6);
    put16m(p4(-0x2700), rd16(0xFFFF95F4u));
    c->d[0] = set_w(c->d[0], rd16(0xFFFF95F6u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 4, 16));
    put16m(p4(-0x26FE), c->d[0]);
    call(0x86BC);
}
/* $DBF0: the player placed at the start point ($967A, $967C) standing on its feet: its motion,
 * state and the frame's flags cleared, animation 0 at once */
static void player_place(void)
{
    c->d[0] = 0; F_L(0);
    put16m(p4(-0x30FE), 0); put8(p4(-0x3100), 0); put16m(p4(-0x3200), 0); put8(p4(-0x3D00), 0);
    put16m(p4(-0x37FE), 0); put16m(p4(-0x36FE), 0); put32m(p4(-0x3600), 0); put32m(p4(-0x3400), 0);
    put8(p4(-0x2AFD), 0); put32m(p4(-0x2D00), 0); put8(0xFFFF9F05u, 0); put16m(p4(-0x3FFE), 0);
    put32m(p4(-0x3E00), 0); put32m(p4(-0x2800), 0); put16m(0xFFFF9F02u, 0);
    put8(0xFFFF9F06u, 0); put8(0xFFFF9F07u, 0); put8(0xFFFF9F09u, 0); put8(0xFFFF9F0Au, 0); put8(0xFFFF9F08u, 0);
    put8(p4(-0x2F00), 3);
    mem_and8(p4(-0x3FFD), 0xBF);
    mem_or8(p4(-0x3FFE), 4);
    put8(p4(-0x3CFD), 0x10);
    put16m(p4(-0x39FE), 0x100);
    put16m(p4(-0x3800), rd16(0xFFFF967Au));
    put16m(p4(-0x3700), rd16(0xFFFF967Cu));
    c->d[0] = 1; F_L(1);
    put8(p4(-0x31FF), 1);
    c->d[0] = 0; F_L(0);
    call(0x7FC);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(p4(-0x34FD)));
    wr16(p4(-0x3700), (uint16_t)sub_w(&c->ccr, c->d[0] & 0xFFFF, rd16(p4(-0x3700))));
    mem_or8(p4(-0x4000), 0x40);
}
/* $DC8A (arrival 2): the player moved from the start point by ($967E, $9680), drawn; under
 * water its fall speed halved */
static void player_moved(void)
{
    c->d[0] = set_w(c->d[0], rd16(0xFFFF967Au)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF967Eu), c->d[0]));
    put16m(p4(-0x3800), c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF967Cu)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFF9680u), c->d[0]));
    put16m(p4(-0x3700), c->d[0]);
    mem_or8(p4(-0x4000), 0x40);
    f_bit(rd8(0xFFFF966Bu) & 1);
    if (!(rd8(0xFFFF966Bu) & 1)) return;
    wr16(p4(-0x35FE), (uint16_t)asr_w1(&c->ccr, rd16(p4(-0x35FE))));
}
/* $DCC0 (arrival 16): placed, and kept still ($C003 bit 4) */
static void player_place_still(void)
{
    player_place();
    mem_or8(p4(-0x3FFD), 0x10);
}
/* $DCB6 (arrival 18): placed, kept still after a restart's room change ($965F bit 0) */
static void player_place_restart(void)
{
    f_bit(rd8(0xFFFF965Fu) & 1);
    if (rd8(0xFFFF965Fu) & 1) player_place_still(); else player_place();
}
/* $DBBE: a room entered: the companion made again, the player reset and placed by the arrival */
static void room_player(void)
{
    companion_again();
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    player_reset();
    c->d[2] = 0; F_L(0);
    move_b(2, rd8(0xFFFF9678u));
    c->a[0] = 0xDBDA + sx(rd16(0xDBDA + sx(c->d[2])));
    switch (c->a[0]) {                              /* jmp (a0) */
    case 0xDBF0: player_place(); break;
    case 0xDC8A: player_moved(); break;
    case 0xDCC0: player_place_still(); break;
    case 0xDCB6: player_place_restart(); break;
    default: call(c->a[0]); break;                  /* past the table: as the original jumps */
    }
}

ENTRY(RoomPlayer, room_player)             /* $DBBE */
ENTRY(PlayerPlace, player_place)           /* $DBF0 */
ENTRY(PlayerMoved, player_moved)           /* $DC8A */
ENTRY(PlayerPlaceRestart, player_place_restart)   /* $DCB6 */
ENTRY(PlayerPlaceStill, player_place_still)       /* $DCC0 */
ENTRY(PlayerReset, player_reset)           /* $DCCC */
ENTRY(CompanionRoom, companion_room)       /* $F880 */
static void companion_gfx_0(void) { c->d[0] = 0; F_L(0); companion_gfx(); }   /* $F89E */
static void companion_gfx_6(void) { c->d[0] = 6; F_L(6); companion_gfx(); }   /* $F8A2 */
static void companion_gfx_4(void) { c->d[0] = 4; F_L(4); companion_gfx(); }   /* $F8A6 */
ENTRY(CompanionGfx0, companion_gfx_0)      /* $F89E */
ENTRY(CompanionGfx6, companion_gfx_6)      /* $F8A2 */
ENTRY(CompanionGfx4, companion_gfx_4)      /* $F8A6 */
ENTRY(CompanionGfx, companion_gfx)         /* $F8A8 */
ENTRY(CompanionRoom2, companion_room_2)    /* $F8B2 */
ENTRY(CompanionAt, companion_at)           /* $F8E0 */
ENTRY(CompanionJoin, companion_join)       /* $F906 */
ENTRY(CompanionHide, companion_hide)       /* $F99C */
ENTRY(CompanionPalette, companion_palette) /* $F9B2 */
ENTRY(CompanionAgain, companion_again)     /* $F9C0 */
ENTRY(CompanionMake, companion_make)       /* $F9C8 */
ENTRY(CompanionNewA, companion_new_a)      /* $FA20 */
ENTRY(CompanionNewB, companion_new_b)      /* $FA42 */

/* ---- batch T39: the companion each frame (kinds 0 and 4) ----
 * Each frame ($9068) $FA6C runs the companion's state ($D401: bit 7 set once it has begun; the
 * state in the rest) for its kind. Kinds 0 and 4 share resting on the player ($FC30), following
 * ($FC86) and catching up ($FCF4). Kind 4 also shoots at an enemy ($FAAE) and appears ($FB60);
 * kind 0 drops a thing ($FDAC), rides an enemy ($FDF4) and appears ($FC02). Kinds 2 and 6 are
 * batch T41's. In the states a4 is the companion, a2 the player. */

static int mem_bset(uint32_t a, int bit) { uint8_t v = rd8(a); f_bit(v >> bit & 1); wr8(a, (uint8_t)(v | 1 << bit)); return v >> bit & 1; }
static int begun(void) { return mem_bset(p4(-0x2BFF), 7); }   /* bset #7,-$2BFF(a4) */
static int cc_ls(void) { return (c->ccr & (CC_C | CC_Z)) != 0; }

/* $108A2: every 8 frames, if far off the screen ($BEE: negative), the companion's state is reset
 * and its caller returns at once (addq #4,a7: the caller's frame dropped). Returns 1 if so. */
static int companion_gone(void)
{
    c->d[0] = 7; F_L(7);
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF965Au)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) return 0;
    call(0xBEE);
    if (!(c->ccr & CC_N)) return 0;
    put8(p4(-0x2BFF), 0);
    return 1;
}

/* -$3600 and -$35FE (the speeds) lose a sixteenth, by register r */
static void companion_drag(int r)
{
    c->d[r] = set_w(c->d[r], rd16(p4(-0x3600))); F_W(c->d[r]);
    c->d[r] = set_w(c->d[r], asr_n(&c->ccr, c->d[r], 4, 16));
    wr16(p4(-0x3600), (uint16_t)sub_w(&c->ccr, c->d[r] & 0xFFFF, rd16(p4(-0x3600))));
    c->d[r] = set_w(c->d[r], rd16(p4(-0x35FE))); F_W(c->d[r]);
    c->d[r] = set_w(c->d[r], asr_n(&c->ccr, c->d[r], 4, 16));
    wr16(p4(-0x35FE), (uint16_t)sub_w(&c->ccr, c->d[r] & 0xFFFF, rd16(p4(-0x35FE))));
}
/* $FFC6: is the player (a2, $18 up) within reach ($108DC: higher if not) */
static void companion_near(void)
{
    c->d[0] = set_w(c->d[0], rd16(p2(-0x3800))); F_W(c->d[0]);
    c->d[1] = 0; F_L(0);
    move_b(1, rd8(p2(-0x34FE)));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, 0x10, c->d[1]));
    c->d[2] = set_w(c->d[2], rd16(p2(-0x3700))); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 0x18, c->d[2]));
    c->d[3] = 0; F_L(0);
    move_b(3, rd8(p4(-0x34FD)));
    call(0x108DC);
}
/* $FF96: on screen and 8 or more columns from its area ($A148): 1 in 32 to go there (state 6) */
static void companion_chance_area(void)
{
    f_bit(rd8(p4(-0x4000)) & 1);
    if (!(rd8(p4(-0x4000)) & 1)) return;
    c->d[0] = set_w(c->d[0], rd16(0xFFFFA148u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(p4(-0x2600)), c->d[0]));
    if (c->d[0] & 0x8000) c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 8, c->d[0]));
    if (c->d[0] & 0x8000) return;
    call(0x5D8);
    and_w(0, 0x1F);
    if (c->d[0] & 0xFFFF) return;
    put16m(p4(-0x2600), rd16(0xFFFFA148u));
    put8(p4(-0x2BFF), 6);
}
/* $FF56: every 16 frames, 1 in 4, one of the 16 enemies at random: if in use, drawn, on screen
 * and free, the companion goes for it (state 8) */
static void companion_chance_enemy(void)
{
    c->d[0] = 0xF; F_L(0xF);
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF965Au)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) return;
    call(0x5D8);
    c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[1]);
    and_b(1, 0x30);
    if (c->d[1] & 0xFF) return;
    and_w(0, 0xF);
    c->d[0] = set_w(c->d[0], asl_n(&c->ccr, c->d[0], 2, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(0xFFFFA11Au), c->d[0]));
    c->a[0] = sx(c->d[0]);
    move_b(1, rd8(p0(-0x4000)));
    and_b(1, 0xC1);
    cmp_byte(0xC1, c->d[1]);
    if ((c->d[1] & 0xFF) != 0xC1) return;
    F_B(rd8(p0(-0x2BFE)));
    if (rd8(p0(-0x2BFE))) return;
    put16m(p4(-0x27FE), c->d[0]);
    put8(p4(-0x2BFF), 8);
}
/* $FF32: every 8 frames, 1 in 128, if $11B4A finds a target: state 6 */
static void companion_chance_away(void)
{
    c->d[0] = 7; F_L(7);
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF965Au)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) return;
    call(0x5D8);
    and_w(0, 0x7F);
    if (c->d[0] & 0xFFFF) return;
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->a[2]);
    call(0x11B4A);
    c->a[2] = sx(rd16(c->a[7])); c->a[7] += 2;
    if (c->ccr & CC_N) return;
    put8(p4(-0x2BFF), 6);
}
/* the chances while following: with a kind ($D402) $FF32, else $FF56 and $FF96 */
static void companion_chances(void)
{
    F_W(rd16(p4(-0x2BFE)));
    if (rd16(p4(-0x2BFE))) companion_chance_away();
    else { companion_chance_enemy(); companion_chance_area(); }
}
/* $FC6A: once its animation is over: following (state 2), its palette, animation 0 */
static void companion_shown(void)
{
    F_B(rd8(p4(-0x3A00)));
    if (rd8(p4(-0x3A00))) return;
    put8(p4(-0x2BFF), 2);
    put16m(p4(-0x3DFE), 0);
    companion_palette();
    c->d[0] = 0; F_L(0);
    call(SET_ANIM);
}
/* $FC52: on the player's head; then as $FC6A */
static void companion_over_player(void)
{
    put16m(p4(-0x3800), rd16(p2(-0x3800)));
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(p2(-0x34FD)));
    c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(p2(-0x3700)), c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 4, c->d[0]));
    put16m(p4(-0x3700), c->d[0]);
    companion_shown();
}
/* $FC30 (state 0): begun, still, its look $1208, drawn on the player's head */
static void companion_rest(void)
{
    if (begun()) { companion_shown(); return; }
    put32m(p4(-0x3600), 0);
    put32m(p4(-0x3400), 0);
    put16m(p4(-0x3D00), 0x1208);
    c->d[0] = 0; F_L(0);
    call(0x7FC);
    mem_or8(p4(-0x4000), 0x40);
    companion_over_player();
}
/* $FC02 (kind 0, state 10): begun, sound $43, the look $402's ($4910), drawn on the player's head */
static void companion_appear_0(void)
{
    if (begun()) { companion_shown(); return; }
    c->d[0] = 0x43; F_L(0x43);
    call(QUEUE_SOUND);
    c->d[1] = set_w(c->d[1], 0x402); F_W(0x402);
    call(0x4910);
    put16m(p4(-0x3DFE), rd16(c->a[0]));
    put16m(p4(-0x3D00), 0x1002);
    c->d[0] = 4; F_L(4);
    call(0x7FC);
    mem_or8(p4(-0x4000), 0x40);
    companion_over_player();
}
/* $FC86 (state 2): following the player, $18 above it; out of reach: catching up (state 4) */
static void companion_follow(void)
{
    if (!begun()) {
        companion_palette();
        put16m(p4(-0x32FE), 0x40);
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        put8(p4(-0x2F00), 3);
    }
    companion_chances();
    put16m(p4(-0x3300), rd16(p2(-0x3300)));
    c->d[0] = 7; F_L(7);
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF965Au)); F_W(c->d[0]);
    if (!(c->d[0] & 0xFFFF)) call(0x44DA);
    c->d[0] = set_w(c->d[0], rd16(p2(-0x3700))); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x18, c->d[0]));
    c->d[1] = 2; F_L(2);
    c->d[2] = 0x40; F_L(0x40);
    call(0x4274);
    call(0xB44);
    companion_near();
    if (!cc_ls()) put8(p4(-0x2BFF), 4);
    c->d[0] = 0x20; F_L(0x20);
    call(0xB92);
    call(0xAB4);
}
/* $FCF4 (state 4): flying to the player, aimed every other frame; within reach, following */
static void companion_return(void)
{
    if (!begun()) {
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        put16m(p4(-0x3300), rd16(p2(-0x3300)));
        put16m(p4(-0x32FE), 0x200);
        put8(p4(-0x2800), 8);
        put8(p4(-0x27FF), 4);
        mem_and8(p4(-0x2F00), 0xFC);
    }
    put32m(p4(-0x3400), 0);
    call(0x44DA);
    if (!mem_dec8(p4(-0x2800))) {
        put8(p4(-0x2800), 1);
        companion_near();
        if (cc_ls()) { put8(p4(-0x2BFF), 2); return; }
    }
    if (companion_gone()) return;
    companion_chances();
    if (!mem_dec8(p4(-0x27FF))) {
        put8(p4(-0x27FF), 2);
        c->d[0] = set_w(c->d[0], rd16(p2(-0x3800))); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(p4(-0x3800)), c->d[0]));
        c->d[1] = 0; F_L(0);
        c->d[1] = set_w(c->d[1], rd16(p2(-0x3700))); F_W(c->d[1]);
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 0x18, c->d[1]));
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(p4(-0x3700)), c->d[1]));
        call(0x109C);
        c->d[4] = 0x40; F_L(0x40);
        call(0xFC0);
        put16m(p4(-0x3400), c->d[0]);
        put16m(p4(-0x33FE), c->d[5]);
    }
    companion_drag(0);
    call(0xB42);
    call(0xAB4);
}
/* $FAAE (kind 4, states 6 and 8): begun, 2-5 shots; every 8 frames on screen, at an enemy
 * ($11B0E) a shot (item $214) aimed at it, animation 2; it hovers at its height meanwhile, and
 * after the last shot catches up (state 4) */
static void companion_shoot(void)
{
    if (!begun()) {
        put16m(p4(-0x20FE), rd16(p4(-0x3700)));
        put16m(p4(-0x32FE), 0x40);
        put8(p4(-0x2F00), 3);
        put8(p4(-0x2800), 0x1E);
        call(0x5D8);
        and_b(0, 3);
        c->d[0] = set_b(c->d[0], add_b(&c->ccr, 2, c->d[0]));
        put8(p4(-0x27FF), c->d[0]);
        put16m(p4(-0x3600), 0);
        put8(p4(-0x25FD), 0);
    }
    F_B(rd8(p4(-0x2800)));
    if (rd8(p4(-0x2800)) && !mem_dec8(p4(-0x2800))) {
        f_bit(rd8(p4(-0x4000)) & 1);
        if (!(rd8(p4(-0x4000)) & 1)) goto done;
        call(0x11B0E);
        if (c->ccr & CC_N) goto done;
        c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->d[2]);
        c->d[0] = set_w(c->d[0], 0x214); F_W(0x214);
        c->d[1] = set_w(c->d[1], rd16(p4(-0x3800))); F_W(c->d[1]);
        c->d[2] = set_w(c->d[2], rd16(p4(-0x3700))); F_W(c->d[2]);
        call(0x1090C);
        if (!(c->ccr & CC_N)) {
            c->d[2] = set_w(c->d[2], rd16(c->a[7])); c->a[7] += 2; F_W(c->d[2]);
            put16m(p0(-0x2CFE), c->d[2]);
            c->a[2] = sx(c->d[2]);
            call(0x44DA);
            c->d[0] = 2; F_L(2);
            call(SET_ANIM);
        } else { c->a[7] += 2; goto done; }
    }
    c->d[0] = set_w(c->d[0], rd16(p4(-0x20FE))); F_W(c->d[0]);
    c->d[1] = 8; F_L(8);
    c->d[2] = 0x10; F_L(0x10);
    call(0x4274);
    call(0xB44);
    call(0xAD6);
    F_B(rd8(p4(-0x25FD)));
    if (!(rd8(p4(-0x25FD)) & 0x80)) return;
    c->d[0] = 0; F_L(0);
    call(SET_ANIM);
    put8(p4(-0x2800), 8);
    put8(p4(-0x25FD), 0);
    if (mem_dec8(p4(-0x27FF))) return;
done:
    put8(p4(-0x2BFF), 4);
}
/* $FBC8: once its animation is over, following (state 2) */
static void companion_appeared_4(void)
{
    F_B(rd8(p4(-0x3A00)));
    if (rd8(p4(-0x3A00))) return;
    put16m(p4(-0x3DFE), 0);
    put8(p4(-0x2BFF), 2);
    companion_palette();
    c->d[0] = 0; F_L(0);
    call(SET_ANIM);
}
/* $FB60 (kind 4, state 10): begun, the look $462's ($4910), facing as the player, beside it */
static void companion_appear_4(void)
{
    if (begun()) { companion_appeared_4(); return; }
    c->d[1] = set_w(c->d[1], 0x462); F_W(0x462);
    call(0x4910);
    put16m(p4(-0x3DFE), rd16(c->a[0]));
    put16m(p4(-0x3D00), 0x1616);
    c->d[0] = 0; F_L(0);
    call(0x7FC);
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    mem_or8(p4(-0x3FFE), 8);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(p4(-0x34FE)));
    f_bit(rd8(p2(-0x3FFE)) >> 3 & 1);
    if (rd8(p2(-0x3FFE)) & 8) {
        c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
        mem_and8(p4(-0x3FFE), 0xF7);
    }
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(p2(-0x3800)), c->d[0]));
    put16m(p4(-0x3800), c->d[0]);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(p2(-0x34FD)));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(p2(-0x3700)), c->d[0]));
    c->d[1] = 0; F_L(0);
    move_b(1, rd8(p4(-0x34FD)));
    c->d[1] = set_w(c->d[1], neg_w(&c->ccr, c->d[1]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
    put16m(p4(-0x3700), c->d[0]);
    mem_or8(p4(-0x4000), 0x40);
}
/* $FDAC (kind 0, state 6): begun, sound $49, animation 2 at once; turning; once its animation is
 * over, following, and thing $18 dropped $C in front ($10910) */
static void companion_drop(void)
{
    if (!begun()) {
        c->d[0] = 0x49; F_L(0x49);
        call(QUEUE_SOUND);
        c->d[0] = 2; F_L(2);
        call(0x7FC);
    }
    c->d[0] = 0x10; F_L(0x10);
    call(0xB92);
    c->d[0] = 0x10; F_L(0x10);
    call(0xBAC);
    F_B(rd8(p4(-0x3A00)));
    if (rd8(p4(-0x3A00))) return;
    put8(p4(-0x2BFF), 2);
    c->d[1] = 0xC; F_L(0xC);
    f_bit(rd8(p4(-0x3FFE)) >> 3 & 1);
    if (rd8(p4(-0x3FFE)) & 8) c->d[1] = set_w(c->d[1], neg_w(&c->ccr, c->d[1]));
    c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(p4(-0x3800)), c->d[1]));
    c->d[2] = set_w(c->d[2], rd16(p4(-0x3700))); F_W(c->d[2]);
    c->d[0] = 0x18; F_L(0x18);
    call(0x10910);
}
/* $FE2A: flying to the enemy (a2), aimed every 4 frames; once there, riding it */
static void companion_chase(void)
{
    if (!mem_bset(p4(-0x31FF), 7)) put8(p4(-0x27FF), 4);
    F_B(rd8(p2(-0x4000)));
    if (!(rd8(p2(-0x4000)) & 0x80)) { put8(p4(-0x2BFF), 4); return; }
    call(0x44DA);
    c->d[0] = set_w(c->d[0], rd16(p2(-0x3800))); F_W(c->d[0]);
    c->d[1] = 0; F_L(0);
    move_b(1, rd8(p2(-0x34FE)));
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 1, 16));
    c->d[3] = 6; F_L(6);
    c->d[2] = 0; F_L(0);
    move_b(2, rd8(p2(-0x34FD)));
    c->d[2] = set_b(c->d[2], add_b(&c->ccr, rd8(p4(-0x34FD)), c->d[2]));
    c->d[2] = set_w(c->d[2], neg_w(&c->ccr, c->d[2]));
    c->d[2] = set_w(c->d[2], add_w(&c->ccr, rd16(p2(-0x3700)), c->d[2]));
    call(0x108DC);
    if (cc_ls()) { put8(p4(-0x31FF), 2); return; }
    if (!mem_dec8(p4(-0x27FF))) {
        put8(p4(-0x27FF), 4);
        c->d[0] = set_w(c->d[0], rd16(p2(-0x3800))); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(p4(-0x3800)), c->d[0]));
        c->d[1] = 0; F_L(0);
        move_b(1, rd8(p2(-0x34FD)));
        c->d[1] = set_b(c->d[1], add_b(&c->ccr, rd8(p4(-0x34FD)), c->d[1]));
        c->d[1] = set_w(c->d[1], neg_w(&c->ccr, c->d[1]));
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, rd16(p2(-0x3700)), c->d[1]));
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(p4(-0x3700)), c->d[1]));
        call(0x109C);
        c->d[4] = set_w(c->d[4], 0x80); F_W(0x80);
        call(0xFC0);
        put16m(p4(-0x3400), c->d[0]);
        put16m(p4(-0x33FE), c->d[5]);
        call(0xB42);
    }
    companion_drag(1);
    call(0xAB4);
}
/* $FED0: riding the enemy (a2) for 180 frames while it is in use, free and alive; then catching
 * up (state 4) */
static void companion_ride(void)
{
    if (!mem_bset(p4(-0x31FF), 7)) {
        c->d[0] = 0; F_L(0);
        put32m(p4(-0x3400), 0);
        put32m(p4(-0x3600), 0);
        put16m(p4(-0x32FE), 0x40);
        put8(p4(-0x2800), 0xB4);
        c->d[0] = 4; F_L(4);
        call(SET_ANIM);
    }
    F_B(rd8(p2(-0x4000)));
    if (rd8(p2(-0x4000)) & 0x80) {
        F_B(rd8(p2(-0x2BFE)));
        if (!rd8(p2(-0x2BFE))) {
            F_W(rd16(p2(-0x2600)));
            if (rd16(p2(-0x2600))) {
                call(0x44DA);
                put16m(p4(-0x3800), rd16(p2(-0x3800)));
                c->d[0] = 0; F_L(0);
                move_b(0, rd8(p2(-0x34FD)));
                c->d[0] = set_b(c->d[0], add_b(&c->ccr, rd8(p4(-0x34FD)), c->d[0]));
                c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(p2(-0x3700)), c->d[0]));
                put16m(p4(-0x3700), c->d[0]);
                if (mem_dec8(p4(-0x2800))) return;
            }
        }
    }
    put8(p4(-0x2BFF), 4);
}
/* $FDF4 (kind 0, state 8): begun, step 0, fast; the enemy it goes for (-$27FE) in a2: flying to
 * it ($FE2A) or riding it ($FED0) */
static void companion_grab(void)
{
    if (!begun()) {
        put8(p4(-0x31FF), 0);
        put16m(p4(-0x3300), 0x300);
        put16m(p4(-0x32FE), 0x300);
    }
    if (companion_gone()) return;
    c->a[2] = sx(rd16(p4(-0x27FE)));
    c->d[0] = 0; F_L(0);
    c->d[0] = 0x7F; F_L(0x7F);
    c->d[0] = set_b(c->d[0], c->d[0] & rd8(p4(-0x31FF))); F_B(c->d[0]);
    c->a[0] = 0xFE26; c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    if (c->a[0] == 0xFE2A) companion_chase();
    else if (c->a[0] == 0xFED0) companion_ride();
    else call(c->a[0]);                             /* past the table: as the original jumps */
}
/* $FA8E: kind 4's state (the table at $FAA2) */
static void companion_kind_4(void)
{
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    c->d[0] = 0x7F; F_L(0x7F);
    c->d[0] = set_b(c->d[0], c->d[0] & rd8(p4(-0x2BFF))); F_B(c->d[0]);
    c->a[0] = 0xFAA2; c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    switch (c->a[0]) {
    case 0xFC30: companion_rest(); break;
    case 0xFC86: companion_follow(); break;
    case 0xFCF4: companion_return(); break;
    case 0xFAAE: companion_shoot(); break;
    case 0xFB60: companion_appear_4(); break;
    default: call(c->a[0]); break;                  /* past the table: as the original jumps */
    }
}
/* $FBE2: kind 0's state (the table at $FBF6) */
static void companion_kind_0(void)
{
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    c->d[0] = 0x7F; F_L(0x7F);
    c->d[0] = set_b(c->d[0], c->d[0] & rd8(p4(-0x2BFF))); F_B(c->d[0]);
    c->a[0] = 0xFBF6; c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    switch (c->a[0]) {
    case 0xFC30: companion_rest(); break;
    case 0xFC86: companion_follow(); break;
    case 0xFCF4: companion_return(); break;
    case 0xFDAC: companion_drop(); break;
    case 0xFDF4: companion_grab(); break;
    case 0xFC02: companion_appear_0(); break;
    default: call(c->a[0]); break;                  /* past the table: as the original jumps */
    }
}
static void companion_kind_2(void), companion_kind_6(void);
/* $FA6C (each frame): the companion, if in use, by its kind (the table at $FA86) */
static void companion_frame(void)
{
    c->a[4] = sx(rd16(COMPANION));
    F_B(rd8(p4(-0x4000)));
    if (!(rd8(p4(-0x4000)) & 0x80)) return;
    c->d[0] = set_w(c->d[0], rd16(p4(-0x2BFE))); F_W(c->d[0]);
    c->a[0] = 0xFA86; c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    switch (c->a[0]) {
    case 0xFBE2: companion_kind_0(); break;
    case 0xFA8E: companion_kind_4(); break;
    case 0x100F2: companion_kind_2(); break;
    case 0xFFE6: companion_kind_6(); break;
    default: call(c->a[0]); break;                  /* past the table: as the original jumps */
    }
}

ENTRY(CompanionFrame, companion_frame)             /* $FA6C */
ENTRY(CompanionKind4, companion_kind_4)            /* $FA8E */
ENTRY(CompanionShoot, companion_shoot)             /* $FAAE */
ENTRY(CompanionAppear4, companion_appear_4)        /* $FB60 */
ENTRY(CompanionAppeared4, companion_appeared_4)    /* $FBC8 */
ENTRY(CompanionKind0, companion_kind_0)            /* $FBE2 */
ENTRY(CompanionAppear0, companion_appear_0)        /* $FC02 */
ENTRY(CompanionRest, companion_rest)               /* $FC30 */
ENTRY(CompanionOverPlayer, companion_over_player)  /* $FC52 */
ENTRY(CompanionShown, companion_shown)             /* $FC6A */
ENTRY(CompanionFollow, companion_follow)           /* $FC86 */
ENTRY(CompanionReturn, companion_return)           /* $FCF4 */
ENTRY(CompanionDrop, companion_drop)               /* $FDAC */
ENTRY(CompanionGrab, companion_grab)               /* $FDF4 */
ENTRY(CompanionChase, companion_chase)             /* $FE2A */
ENTRY(CompanionRide, companion_ride)               /* $FED0 */
ENTRY(CompanionChanceAway, companion_chance_away)  /* $FF32 */
ENTRY(CompanionChanceEnemy, companion_chance_enemy)   /* $FF56 */
ENTRY(CompanionChanceArea, companion_chance_area)  /* $FF96 */
ENTRY(CompanionNear, companion_near)               /* $FFC6 */
void CompanionGone(Cpu *cpu) { c = cpu; if (companion_gone()) c->a[7] += 4; c->ret = 0; }   /* $108A2: returning from its caller */

/* ---- batch T40: the door poses, leaving by an exit and coming out of one ----
 * An exit ($F76C, from the game's loop and the room changes' sequences) shows the exit's
 * graphics, sinks the player into it and goes to the next room; coming out ($F7EA, from the
 * arrivals) raises the player out of it and goes back to the game's loop. Both wait for frames. */

/* $DF80: the player kept still at a door (its frame flag 3), animation $C */
static void pose_keep(void)
{
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    mem_or8(0xFFFF9F06u, 8);
    c->d[0] = 0xC; F_L(0xC);
    call(SET_ANIM);
}
/* $DF90: the same, animation $16 */
static void pose_free(void)
{
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    mem_or8(0xFFFF9F06u, 8);
    c->d[0] = 0x16; F_L(0x16);
    call(SET_ANIM);
}
/* $F76C: the exit's graphics ($50), sound $3C, animation $16 for 3 frames; then the pose for
 * its palette ($F7E6) until it ends; then rising (-$2000) for 8 frames; the next room */
static void exit_room(uint32_t at)
{
    switch (at) { case 0xF790: goto rF790; case 0xF7BA: goto rF7BA; case 0xF7D4: goto rF7D4; }
    put16m(0xFFFF8E76u, 0x50);
    call(STREAM_GFX);
    c->d[0] = 0x3C; F_L(0x3C);
    call(QUEUE_SOUND);
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    put8(p4(-0x2800), 3);
    c->d[0] = 0x16; F_L(0x16);
    call(SET_ANIM);
    do {
        wait_frame(0xF790);
rF790:
        c->a[4] = sx(rd16(0xFFFF9EEEu));
    } while (mem_dec8(p4(-0x2800)));
    mem_and8(p4(-0x3FFD), 0xF7);
    put16m(p4(-0x3DFE), 0x6E0);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(p4(-0x3CFF)));
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 1, 16));
    move_b(0, rd8(0xF7E6 + sx(c->d[0])));
    call(SET_ANIM);
    do {
        wait_frame(0xF7BA);
rF7BA:
        c->a[4] = sx(rd16(0xFFFF9EEEu));
        F_B(rd8(p4(-0x3A00)));
    } while (rd8(p4(-0x3A00)));
    put16m(p4(-0x35FE), 0xE000);
    put8(p4(-0x2800), 8);
    do {
        wait_frame(0xF7D4);
rF7D4:
        c->a[4] = sx(rd16(0xFFFF9EEEu));
        call(0xAD6);
    } while (mem_dec8(p4(-0x2800)));
    leave_to(NEXT_ROOM);
}
/* $F7EA: the exit's graphics, the player $100 lower, the pose for its palette ($F87C), rising
 * ($2000) for 8 frames, still for 3, animation $16 for 3; the game's loop */
static void exit_arrive(uint32_t at)
{
    switch (at) { case 0xF830: goto rF830; case 0xF850: goto rF850; case 0xF86E: goto rF86E; }
    put16m(0xFFFF8E76u, 0x50);
    call(STREAM_GFX);
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    mem_and8(p4(-0x3FFD), 0xEF);
    wr16(p4(-0x3700), (uint16_t)sub_w(&c->ccr, 0x100, rd16(p4(-0x3700))));
    mem_and8(p4(-0x3FFD), 0xF7);
    put16m(p4(-0x3DFE), 0x6E0);
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(p4(-0x3CFF)));
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 1, 16));
    move_b(0, rd8(0xF87C + sx(c->d[0])));
    call(SET_ANIM);
    put16m(p4(-0x35FE), 0x2000);
    put8(p4(-0x2800), 8);
    do {
        wait_frame(0xF830);
rF830:
        c->a[4] = sx(rd16(0xFFFF9EEEu));
        call(0xAD6);
    } while (mem_dec8(p4(-0x2800)));
    put16m(p4(-0x35FE), 0);
    call(0x7F4);
    put8(p4(-0x2800), 3);
    do {
        wait_frame(0xF850);
rF850:
        c->a[4] = sx(rd16(0xFFFF9EEEu));
    } while (mem_dec8(p4(-0x2800)));
    put16m(p4(-0x3DFE), 0);
    c->d[0] = 0x16; F_L(0x16);
    call(SET_ANIM);
    put8(p4(-0x2800), 3);
    do {
        wait_frame(0xF86E);
rF86E:
        c->a[4] = sx(rd16(0xFFFF9EEEu));
    } while (mem_dec8(p4(-0x2800)));
    leave_to(GAME_LOOP);
}

ENTRY(PoseKeep, pose_keep)                 /* $DF80 */
ENTRY(PoseFree, pose_free)                 /* $DF90 */
/* these are jumped to and leave by port_continue_at */
void ExitRoom(Cpu *cpu)   { c = cpu; NEEDS_PASS(0xF76C); exit_room(0); }    /* $F76C */
void ExitArrive(Cpu *cpu) { c = cpu; NEEDS_PASS(0xF7EA); exit_arrive(0); }  /* $F7EA */
/* batch N3f: resumed where they waited (a loaded state) */
#define EXIT_AT(name, fn, at) void name(Cpu *cpu) { c = cpu; NEEDS_PASS(at); fn(at); }
EXIT_AT(ExitRoomAtF790, exit_room, 0xF790)
EXIT_AT(ExitRoomAtF7BA, exit_room, 0xF7BA)
EXIT_AT(ExitRoomAtF7D4, exit_room, 0xF7D4)
EXIT_AT(ExitArriveAtF830, exit_arrive, 0xF830)
EXIT_AT(ExitArriveAtF850, exit_arrive, 0xF850)
EXIT_AT(ExitArriveAtF86E, exit_arrive, 0xF86E)

/* ---- batch T41: the companion each frame, kinds 2 and 6 ----
 * Kind 2 walks after the player along a trail of its cells ($9F34: up to 15 longs, $9F74 the
 * bytes used; $9F76 negative while it is not kept). It walks ($101F2), runs ($1023C), jumps to
 * the trail's next point ($10296), waits for a trail ($10424), sits and drops things ($10366),
 * hops on the spot ($10444) and dives ($103F6). Kind 6 shares five of its states; its own two
 * ($1000C, $1005C) are not traced code and stay the original's. $10550, $10572 and $108B8
 * return from their callers when they act (addq #4,a7), as $108A2 does. The trail's path
 * ($1061E) stays the original's. */

#define TRAIL         0xFFFF9F34u   /* longs: cells (x, y) */
#define TRAIL_LEN     0xFFFF9F74u   /* word: bytes used */
#define TRAIL_OFF     0xFFFF9F76u   /* byte: negative while not kept */

static int is_kind_2(void) { cmp_w(&c->ccr, 2, rd16(p4(-0x2BFE))); return rd16(p4(-0x2BFE)) == 2; }

/* the player able to leave a trail: on the ground ($CF00 & $8A == 2) and not in states 9, 13, 15
 * ($9F1C); else -1 (N set). Shared by $107FC and $10844 */
static int trail_ok(void)
{
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    move_b(0, rd8(p2(-0x3100)));
    and_b(0, 0x8A);
    c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 2, c->d[0]));
    if (c->d[0] & 0xFF) return 0;
    static const uint8_t no[] = { 9, 0xD, 0xF };
    for (int k = 0; k < 3; k++) {
        cmp_byte(no[k], rd8(0xFFFF9F1Cu));
        if (rd8(0xFFFF9F1Cu) == no[k]) return 0;
    }
    return 1;
}
/* $107FC: a trail started at the player's cell ($9F18), the rest filled with d0; kept */
static void trail_start(void)
{
    if (!trail_ok()) { c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]); return; }
    c->a[0] = TRAIL;
    put32m(c->a[0], rd32(0xFFFF9F18u)); c->a[0] += 4;
    c->d[1] = 0xF; F_L(0xF);
    do {
        put32m(c->a[0], c->d[0]); c->a[0] += 4;
        c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 1, c->d[1]));
    } while (c->d[1] & 0xFFFF);
    put16m(TRAIL_LEN, 4);
    put8(TRAIL_OFF, 0);
}
/* $10844 (each frame, kinds 2 and 6): while the trail is kept and the player can leave one, its
 * cell goes after the same cell found in the trail (or at its end), up to 15 */
static void trail_record(void)
{
    F_W(rd16(TRAIL_OFF));
    if (rd16(TRAIL_OFF) & 0x8000) return;
    if (!trail_ok()) return;
    c->d[2] = rd32(0xFFFF9F18u); F_L(c->d[2]);
    c->a[0] = TRAIL;
    c->d[0] = set_w(c->d[0], rd16(TRAIL_LEN)); F_W(c->d[0]);
    c->d[1] = 0; F_L(0);
    for (;;) {
        c->d[1] = set_w(c->d[1], add_w(&c->ccr, 4, c->d[1]));
        cmp_w(&c->ccr, c->d[0], c->d[1]);
        if ((c->d[1] & 0xFFFF) == (c->d[0] & 0xFFFF)) break;
        uint32_t v = rd32(c->a[0] + sx(c->d[1]));
        cmp_l(&c->ccr, v, c->d[2]);
        if (v == c->d[2]) { c->d[0] = set_w(c->d[0], c->d[1]); F_W(c->d[0]); break; }
    }
    put32m(c->a[0] + sx(c->d[0]), c->d[2]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 4, c->d[0]));
    cmp_w(&c->ccr, 0x3C, c->d[0]);
    if ((c->d[0] & 0xFFFF) > 0x3C) { c->d[0] = 0x3C; F_L(0x3C); }
    put16m(TRAIL_LEN, c->d[0]);
}
/* $10550: unless -$27FF is set, every 32 frames 1 in 128: sitting (state 8). 1: the caller
 * returns */
static int companion_chance_sit(void)
{
    F_B(rd8(p4(-0x27FF)));
    if (rd8(p4(-0x27FF))) return 0;
    c->d[0] = 0x1F; F_L(0x1F);
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF965Au)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) return 0;
    call(0x5D8);
    and_w(0, 0x7F);
    if (c->d[0] & 0xFFFF) return 0;
    put8(p4(-0x2BFF), 8);
    return 1;
}
/* $10572: every 4 frames, the first enemy it can go for within $18 high and $30 wide: a shot
 * (item $20E) 8 ahead, facing it, and state 8. 1: the caller returns */
static int companion_spit(void)
{
    c->d[0] = 3; F_L(3);
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF965Au)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) return 0;
    c->d[1] = set_w(c->d[1], rd16(p4(-0x3800))); F_W(c->d[1]);
    c->d[2] = set_w(c->d[2], rd16(p4(-0x3700))); F_W(c->d[2]);
    c->d[3] = 0xF; F_L(0xF);
    c->a[2] = 0x40;
    do {
        move_b(0, rd8(p2(-0x4000)));
        and_b(0, 0xC1);
        cmp_byte(0xC1, c->d[0]);
        if ((c->d[0] & 0xFF) != 0xC1) goto next;
        F_B(rd8(p2(-0x2BFE)));
        if (rd8(p2(-0x2BFE))) goto next;
        F_W(rd16(p2(-0x2600)));
        if (!rd16(p2(-0x2600))) goto next;
        c->d[0] = set_w(c->d[0], rd16(p2(-0x3700))); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[2], c->d[0]));
        if (c->d[0] & 0x8000) c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
        cmp_w(&c->ccr, 0x18, c->d[0]);
        if ((c->d[0] & 0xFFFF) > 0x18) goto next;
        c->d[4] = 0; F_L(0);
        c->d[0] = set_w(c->d[0], rd16(p2(-0x3800))); F_W(c->d[0]);
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
        if (c->d[0] & 0x8000) {
            c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
            c->d[4] = 8; F_L(8);
        }
        put8(p4(-0x25FD), c->d[4]);
        cmp_w(&c->ccr, 0x30, c->d[0]);
        if ((c->d[0] & 0xFFFF) < 0x30) goto found;
    next:
        c->a[2] += 4;
    } while (dbra(3));
    return 0;
found:
    c->d[0] = set_w(c->d[0], 0x20E); F_W(0x20E);
    c->d[1] = set_w(c->d[1], rd16(p4(-0x3800))); F_W(c->d[1]);
    c->d[2] = set_w(c->d[2], rd16(p4(-0x3700))); F_W(c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, 2, c->d[2]));
    call(0x1090C);
    if (c->ccr & CC_N) return 0;
    mem_and8(p4(-0x3FFE), 0xF7);
    move_b(0, rd8(p4(-0x25FD)));
    mem_or8(p4(-0x3FFE), (uint8_t)c->d[0]);
    put8(p0(-0x3CFD), 6);
    c->d[0] = 8; F_L(8);
    f_bit(rd8(p4(-0x3FFE)) >> 3 & 1);
    if (rd8(p4(-0x3FFE)) & 8) {
        c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
        mem_or8(p0(-0x3FFE), 8);
    }
    wr16(p0(-0x3800), (uint16_t)add_w(&c->ccr, c->d[0] & 0xFFFF, rd16(p0(-0x3800))));
    put8(p4(-0x2BFF), 8);
    put8(p4(-0x25FD), 0);
    return 1;
}
/* $108B8: on the ground ($BC6: Z) and $E0 or more below the view's top: the state reset and the
 * caller returns (1) */
static int companion_fell(void)
{
    call(0xBC6);
    if (!(c->ccr & CC_Z)) return 0;
    c->d[0] = set_w(c->d[0], rd16(p4(-0x3700))); F_W(c->d[0]);
    c->d[2] = 0; F_L(0);
    move_b(2, rd8(p4(-0x34FD)));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(0xFFFF975Au), c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[2], c->d[0]));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0xE0, c->d[0]));
    if (c->d[0] & 0x8000) return 0;
    put8(p4(-0x2BFF), 0);
    return 1;
}
/* the chances while walking or running: kind 2 sits, kind 6 shoots. 1: the state returns */
static int companion_walk_chances(void)
{
    if (is_kind_2()) return companion_chance_sit();
    return companion_spit();
}
/* the trail's next state ($1061E): if not negative and not this one, it is taken (1) */
static int companion_path(void)
{
    call(0x1061E);
    if (c->ccr & CC_N) return 0;
    move_b(1, rd8(p4(-0x2BFF)));
    and_b(1, 0x7F);
    cmp_byte(c->d[0], c->d[1]);
    if ((c->d[1] & 0xFF) == (c->d[0] & 0xFF)) return 0;
    put8(p4(-0x2BFF), c->d[0]);
    return 1;
}
/* $1011E (state 0): begun: once its animation is over, walking (state 2). Not begun: hidden,
 * and if a trail can start, set up at its start facing as the player, its look $1208 */
static void companion_enter(void)
{
    F_B(rd8(p4(-0x2BFF)));
    if (rd8(p4(-0x2BFF)) & 0x80) {
        F_B(rd8(p4(-0x3A00)));
        if (rd8(p4(-0x3A00))) return;
        companion_palette();
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
        put8(p4(-0x2BFF), 2);
        return;
    }
    mem_and8(p4(-0x4000), 0xBF);
    trail_start();
    if (c->ccr & CC_N) return;
    move_b(0, rd8(p2(-0x2F00)));
    and_b(0, 0x80);
    or_b(0, 3);
    put8(p4(-0x2F00), c->d[0]);
    put32m(p4(-0x3600), 0);
    put16m(p4(-0x3300), 0x400);
    put16m(p4(-0x33FE), 0x40);
    companion_palette();
    c->d[0] = 0; F_L(0);
    call(0x7FC);
    c->d[0] = set_w(c->d[0], rd16(TRAIL)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    put16m(p4(-0x3800), c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(TRAIL + 2)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 2, 16));
    c->d[1] = 0; F_L(0);
    move_b(1, rd8(p4(-0x34FD)));
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
    put16m(p4(-0x3700), c->d[0]);
    mem_and8(p4(-0x3FFE), 0xF7);
    f_bit(rd8(p2(-0x3FFE)) >> 3 & 1);
    if (rd8(p2(-0x3FFE)) & 8) mem_or8(p4(-0x3FFE), 8);
    put16m(p4(-0x3D00), 0x1208);
    c->d[0] = set_w(c->d[0], rd16(p4(-0x2600))); F_W(c->d[0]);
    cmp_w(&c->ccr, rd16(0xFFFF9668u), c->d[0]);
    if ((c->d[0] & 0xFFFF) != rd16(0xFFFF9668u)) put8(p4(-0x27FF), c->d[0]);
    c->d[0] = 0; F_L(0);
    put8(p4(-0x2800), 0);
    put8(p4(-0x3100), 0);
    put8(p4(-0x2EFF), 0);
    put16m(p4(-0x37FE), 0);
    put16m(p4(-0x36FE), 0);
    put32m(p4(-0x3600), 0);
    put16m(p4(-0x2E00), rd16(p4(-0x3800)));
    put16m(p4(-0x2DFE), rd16(p4(-0x3700)));
    put8(p4(-0x3100), rd8(p2(-0x3100)));
    mem_or8(p4(-0x4000), 0x40);
    mem_or8(p4(-0x2BFF), 0x80);
    call(SET_ANIM);
}
/* $1021A: the trail's next state, or on along it */
static void companion_step(void)
{
    if (companion_path()) return;
    call(0xB44);
    call(0x2ED4);
}
/* $101F2 (state 2): walking */
static void companion_walk(void)
{
    if (!begun()) {
        put16m(p4(-0x3600), 0);
        c->d[0] = 0; F_L(0);
        call(SET_ANIM);
    }
    if (companion_gone()) return;
    if (companion_walk_chances()) return;
    companion_step();
}
/* $1023C (state 4): running at the player's speed less $40, the way it faces */
static void companion_run(void)
{
    if (!begun()) { c->d[0] = 2; F_L(2); call(SET_ANIM); }
    if (companion_gone()) return;
    if (companion_walk_chances()) return;
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9EF6u)); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x40, c->d[0]));
    f_bit(rd8(p4(-0x3FFE)) >> 3 & 1);
    if (rd8(p4(-0x3FFE)) & 8) c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
    put16m(p4(-0x3600), c->d[0]);
    call(0xB44);
    call(0x2ED4);
    companion_path();
}
/* $10328: in the air toward the trail's point; once there ($D802 frames), standing on it */
static void companion_jumping(void)
{
    if (companion_fell()) return;
    call(0xB44);
    if (mem_sub_w(p4(-0x27FE), 1)) { call(0xAB4); return; }
    put16m(p4(-0x3800), rd16(p4(-0x2700)));
    put16m(p4(-0x3700), rd16(p4(-0x26FE)));
    put16m(p4(-0x37FE), 0);
    put16m(p4(-0x36FE), 0);
    put32m(p4(-0x3600), 0);
    put8(p4(-0x3100), 2);
    mem_or8(p4(-0x2F00), 3);
    put8(p4(-0x2BFF), 2);
}
/* $10296 (state 6): a jump to the trail's first point (on its slope, if any: $403C), its speeds
 * and frames from the angle maths ($12B6), animation 4 */
static void companion_jump(void)
{
    if (begun()) { companion_jumping(); return; }
    mem_and8(p4(-0x2F00), 0x80);
    mem_and8(p4(-0x3100), 0x75);
    put16m(p4(-0x37FE), 0);
    put16m(p4(-0x36FE), 0);
    c->d[0] = set_w(c->d[0], rd16(TRAIL)); F_W(c->d[0]);
    c->d[6] = set_w(c->d[6], c->d[0]); F_W(c->d[6]);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 8, c->d[0]));
    put16m(p4(-0x2700), c->d[0]);
    c->d[1] = set_w(c->d[1], rd16(TRAIL + 2)); F_W(c->d[1]);
    c->d[7] = set_w(c->d[7], c->d[1]); F_W(c->d[7]);
    c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 16));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 8, c->d[1]));
    put16m(p4(-0x26FE), c->d[1]);
    c->a[3] = 0xFFFF9754u;
    call(0x30DA);
    and_w(2, 0xF0);
    if (c->d[2] & 0xFFFF) {
        c->a[0] = 0x403C;
        c->a[0] += sx(c->d[2]);
        c->d[0] = 0xF; F_L(0xF);
        c->d[0] = set_w(c->d[0], c->d[0] & rd16(p4(-0x2700))); F_W(c->d[0]);
        move_b(0, rd8(c->a[0] + sx(c->d[0])));
        c->d[0] = ext_w(c->d[0]); F_W(c->d[0]);
        wr16(p4(-0x26FE), (uint16_t)add_w(&c->ccr, c->d[0] & 0xFFFF, rd16(p4(-0x26FE))));
    }
    c->d[0] = set_w(c->d[0], rd16(p4(-0x2700))); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, rd16(p4(-0x3800)), c->d[0]));
    c->d[1] = set_w(c->d[1], rd16(p4(-0x26FE))); F_W(c->d[1]);
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, rd16(p4(-0x3700)), c->d[1]));
    call(0x12B6);
    put16m(p4(-0x3600), c->d[2]);
    put16m(p4(-0x35FE), c->d[3]);
    put16m(p4(-0x27FE), c->d[5]);
    c->d[6] = lsl_n(&c->ccr, c->d[6], 8, 32);
    put16m(p4(-0x36FE), c->d[6]);
    c->d[6] = swap_l(c->d[6]);
    c->d[6] = ext_w(c->d[6]); F_W(c->d[6]);
    wr16(p4(-0x3700), (uint16_t)add_w(&c->ccr, c->d[6] & 0xFFFF, rd16(p4(-0x3700))));
    c->d[0] = 4; F_L(4);
    call(SET_ANIM);
}
/* $10366 (kind 2, state 8): sitting, 1-8 drops (thing 0, every 32 frames); then waiting for a
 * trail (state 12), the trail not kept */
static void companion_sit(void)
{
    if (!begun()) {
        wr8(p4(-0x27FF), 0xFF);
        call(0x5D8);
        and_w(0, 7);
        c->d[0] = set_b(c->d[0], add_b(&c->ccr, 1, c->d[0]));
        put8(p4(-0x2800), c->d[0]);
        put32m(p4(-0x3600), 0);
        c->d[0] = 6; F_L(6);
        call(SET_ANIM);
        return;
    }
    if (companion_gone()) return;
    c->d[0] = 0x1F; F_L(0x1F);
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF965Au)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) return;
    c->d[0] = 0; F_L(0);
    c->d[1] = set_w(c->d[1], rd16(p4(-0x3800))); F_W(c->d[1]);
    c->d[2] = set_w(c->d[2], rd16(p4(-0x3700))); F_W(c->d[2]);
    call(0x1090C);
    if (c->ccr & CC_N) return;
    if (mem_dec8(p4(-0x2800))) return;
    put8(p4(-0x2BFF), 0xC);
    wr8(TRAIL_OFF, 0xFF);
}
/* $103B8 (kind 2, state 10): animation 2; walking ($AB8) to the edge at $9A8C, the way it faces;
 * there, diving (state 18) */
static void companion_edge(void)
{
    if (!begun()) {
        c->d[0] = 2; F_L(2);
        cmp_byte(rd8(p4(-0x3CFE)), c->d[0]);
        if (rd8(p4(-0x3CFE)) != 2) call(SET_ANIM);
    }
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9A8Cu)); F_W(c->d[0]);
    f_bit(rd8(p4(-0x3FFE)) >> 3 & 1);
    cmp_w(&c->ccr, rd16(p4(-0x3800)), c->d[0]);
    if (!(rd8(p4(-0x3FFE)) & 8) ? (c->d[0] & 0xFFFF) > rd16(p4(-0x3800)) : (c->d[0] & 0xFFFF) < rd16(p4(-0x3800))) {
        call(0xAB8);
        return;
    }
    put16m(p4(-0x3800), c->d[0]);
    put8(p4(-0x2BFF), 0x12);
}
/* $103F6 (kind 2, state 18): animation 8; at its splash ($C002 bit 1) $2C4E; once its
 * animation is over, waiting for a trail (state 12) */
static void companion_dive(void)
{
    if (!begun()) {
        mem_and8(p4(-0x3FFE), 0xFD);
        c->d[0] = 8; F_L(8);
        call(SET_ANIM);
        return;
    }
    if (mem_bclr(p4(-0x3FFE), 1)) { call(0x2C4E); return; }
    F_B(rd8(p4(-0x3A00)));
    if (rd8(p4(-0x3A00))) return;
    put8(p4(-0x2BFF), 0xC);
}
/* $10424 (state 12): waiting until a trail can start; then walking */
static void companion_wait(void)
{
    if (!begun()) { c->d[0] = 0; F_L(0); call(SET_ANIM); }
    if (companion_gone()) return;
    trail_start();
    if (c->ccr & CC_N) return;
    put8(p4(-0x2BFF), 2);
}
/* $10444 (kind 2, state 14): hopping at ($1074, $1034), turning every 4 frames, until -$2B00's
 * thing ($A15C bit 4) is set: then a trail from $104FA, walking */
static void companion_hop(void)
{
    if (!begun()) {
        put16m(p4(-0x2800), 0x8FF);
        companion_palette();
        put16m(p4(-0x3800), 0x1074);
        put16m(p4(-0x3700), 0x1034);
        put32m(p4(-0x3600), 0);
        put32m(p4(-0x3400), 0x40);
        put32m(p4(-0x3300), 0x2000400);
        put8(p4(-0x2F00), 3);
        put16m(p4(-0x2100), rd16(p4(-0x3800)));
        c->d[0] = 0; F_L(0);
        call(0x7FC);
        c->d[0] = 2; F_L(2);
        call(SET_ANIM);
        mem_or8(p4(-0x4000), 0x40);
    }
    c->a[3] = sx(rd16(p4(-0x2B00)));
    f_bit(rd8(c->a[3] - 0x5EA4) >> 4 & 1);
    if (rd8(c->a[3] - 0x5EA4) & 0x10) {
        put16m(TRAIL_LEN, 0x12);
        c->d[0] = 4; F_L(4);
        c->a[0] = TRAIL;
        c->a[1] = 0x104FA;
        do {
            uint32_t v = rd32(c->a[1]); c->a[1] += 4;
            put32m(c->a[0], v); c->a[0] += 4;
        } while (dbra(0));
        put8(TRAIL_OFF, c->d[0]);
        put8(p4(-0x2BFF), 2);
        return;
    }
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(p4(-0x27FF)));
    c->d[0] = set_b(c->d[0], add_b(&c->ccr, 1, c->d[0]));
    cmp_byte(0xC, c->d[0]);
    if ((c->d[0] & 0xFF) >= 0xC) { c->d[0] = 0; F_L(0); }
    put8(p4(-0x27FF), c->d[0]);
    move_b(0, rd8(0x1050E + sx(c->d[0])));
    c->d[0] = ext_w(c->d[0]); F_W(c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(p4(-0x2100)), c->d[0]));
    put16m(p4(-0x3800), c->d[0]);
    if (mem_dec8(p4(-0x2800))) return;
    put8(p4(-0x2800), 4);
    { uint8_t v = rd8(p4(-0x3FFE)); f_bit(v >> 3 & 1); wr8(p4(-0x3FFE), (uint8_t)(v ^ 8)); }   /* bchg */
}
/* $1051A (kind 2, state 16): begun once: its palette, animation 0 at once, on its spot (-$2100,
 * -$20FE), drawn */
static void companion_place(void)
{
    if (begun()) return;
    companion_palette();
    c->d[0] = 0; F_L(0);
    call(0x7FC);
    put16m(p4(-0x3800), rd16(p4(-0x2100)));
    c->d[0] = 0; F_L(0);
    move_b(0, rd8(p4(-0x34FD)));
    c->d[0] = set_w(c->d[0], neg_w(&c->ccr, c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(p4(-0x20FE)), c->d[0]));
    put16m(p4(-0x3700), c->d[0]);
    put8(p4(-0x2F00), 3);
    mem_or8(p4(-0x4000), 0x40);
}
/* $100F2: kind 2: the trail kept, then its state (the table at $1010A) */
static void companion_kind_2(void)
{
    trail_record();
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    c->d[0] = 0x7F; F_L(0x7F);
    c->d[0] = set_b(c->d[0], c->d[0] & rd8(p4(-0x2BFF))); F_B(c->d[0]);
    c->a[0] = 0x1010A; c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    switch (c->a[0]) {
    case 0x1011E: companion_enter(); break;
    case 0x101F2: companion_walk(); break;
    case 0x1023C: companion_run(); break;
    case 0x10296: companion_jump(); break;
    case 0x10366: companion_sit(); break;
    case 0x103B8: companion_edge(); break;
    case 0x10424: companion_wait(); break;
    case 0x10444: companion_hop(); break;
    case 0x1051A: companion_place(); break;
    case 0x103F6: companion_dive(); break;
    default: call(c->a[0]); break;                  /* past the table: as the original jumps */
    }
}
/* $FFE6: kind 6: the trail kept, then its state (the table at $FFFE) */
static void companion_kind_6(void)
{
    trail_record();
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    c->d[0] = 0x7F; F_L(0x7F);
    c->d[0] = set_b(c->d[0], c->d[0] & rd8(p4(-0x2BFF))); F_B(c->d[0]);
    c->a[0] = 0xFFFE; c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    switch (c->a[0]) {
    case 0x1011E: companion_enter(); break;
    case 0x101F2: companion_walk(); break;
    case 0x1023C: companion_run(); break;
    case 0x10296: companion_jump(); break;
    case 0x10424: companion_wait(); break;
    default: call(c->a[0]); break;                  /* $1000C, $1005C: the original's */
    }
}

ENTRY(CompanionKind2, companion_kind_2)            /* $100F2 */
ENTRY(CompanionKind6, companion_kind_6)            /* $FFE6 */
ENTRY(CompanionEnter, companion_enter)             /* $1011E */
ENTRY(CompanionWalk, companion_walk)               /* $101F2 */
ENTRY(CompanionStep, companion_step)               /* $1021A */
ENTRY(CompanionRun, companion_run)                 /* $1023C */
ENTRY(CompanionJump, companion_jump)               /* $10296 */
ENTRY(CompanionJumping, companion_jumping)         /* $10328 */
ENTRY(CompanionSit, companion_sit)                 /* $10366 */
ENTRY(CompanionEdge, companion_edge)               /* $103B8 */
ENTRY(CompanionDive, companion_dive)               /* $103F6 */
ENTRY(CompanionWait, companion_wait)               /* $10424 */
ENTRY(CompanionHop, companion_hop)                 /* $10444 */
ENTRY(CompanionPlace, companion_place)             /* $1051A */
ENTRY(CompanionTrailStart, trail_start)            /* $107FC */
ENTRY(CompanionTrail, trail_record)                /* $10844 */
/* these return from their callers when they act */
void CompanionChanceSit(Cpu *cpu) { c = cpu; if (companion_chance_sit()) c->a[7] += 4; c->ret = 0; }   /* $10550 */
void CompanionSpit(Cpu *cpu)      { c = cpu; if (companion_spit()) c->a[7] += 4; c->ret = 0; }         /* $10572 */
void CompanionFell(Cpu *cpu)      { c = cpu; if (companion_fell()) c->a[7] += 4; c->ret = 0; }         /* $108B8 */

/* ---- batch T73: the room scripts each frame, the game task's loop ----
 * Now that every room script's part that waits is C, the scripts' driver ($C7E6) and the game
 * task's loop ($8FFC-$9100, $91EA) are C too: each frame the loop yields ($400: here
 * task_wait_frame at its resume address $901C), then runs the frame's work, and leaves as the
 * original does for game over, an exit, a room change ($9962, by the table at $91F4) or the
 * status screen. */

/* $91EA: room change d0 ($9962): by the table at $91F4 */
static void room_change_go(void)
{
    c->d[0] = set_w(c->d[0], c->d[0] - 1);
    c->d[0] = set_w(c->d[0], c->d[0] << 2);
    c->a[0] = rd32(0x91F4 + (c->d[0] & 0xFFFF));
    leave_to(c->a[0]);
}
/* $C7E6, each frame: the doors ($B90A), the room's script's part ($C838, by $9682), the room
 * objects, the walls, the drops, the animated cells, the triggers; $9A94 counting down to the
 * lift's cells. On the continuations' stack, as the scripts that wait need it */
static int scripts_run_at(uint32_t ret, uint32_t at)
{
    switch (at) {
    case 0xC808: goto rC808;
    case 0xC814: goto rC814;
    }
    call(0xB90A);
    push_cont(ret);
    c->d[0] = set_w(c->d[0], rd16(ROOM_SCRIPT)); F_W(c->d[0]);
    if (!(c->d[0] & 0x8000)) {
        c->d[0] = set_w(c->d[0], c->d[0] << 2);
        c->a[0] = rd32(0xC838 + (c->d[0] & 0xFFFF));
        if (!call_ret(c->a[0], 0xC808)) return 0;
    }
rC808:
    call(0xAEA2);
    call(0xB32E);
    if (!call_ret(0xC89A, 0xC814)) return 0;
rC814:
    call(0x28AC);
    call(0x29B4);
    if (rd16(0xFFFF9A94u)) {
        uint16_t t = (uint16_t)(rd16(0xFFFF9A94u) - 1);
        wr16(0xFFFF9A94u, t);
        if (!t) call(0x2C08);
    }
    pop_cont();
    return 1;
}
static int scripts_run(uint32_t ret) { return scripts_run_at(ret, 0); }
/* $901C on: the frame's work after the yield. 1: back to the loop's top ($8FFC); 0: it left.
 * The routines that keep their return addresses get the original's ($9038, $908A, $90E4-$90EE) */
static int game_frame(uint32_t at)
{
    switch (at) {                                       /* resumed (batch N3b): after the player, */
    case 0x9038: goto r9038;                            /* the scripts, the status screen, the */
    case 0x908A: goto r908A;                            /* equipment or the pause */
    case 0x90E4: goto r90E4;
    case 0x90E8: case 0x90EE: goto r90EE;
    }
    if (!(rd8(0xFFFF9659u) & 0x80)) {                 /* the world not frozen */
        call(0x11C9E);                                  /* the room's things */
        if (!(rd8(0xFFFF965Fu) & 4) && !call_ret(0xDD08, 0x9038)) return 0;   /* the player */
r9038:
        if (rd8(0xFFFF9F03u) & 0x80) { leave_to(0x9144); return 0; }        /* game over */
        if (rd8(0xFFFF965Fu) & 8) { call(0x5188); c->d[0] = 1; leave_to(0x4CA8); return 0; }
        if (rd8(0xFFFF965Fu) & 2) { leave_to(0xF76C); return 0; }           /* an exit */
        c->d[0] = set_w(c->d[0], rd16(ROOM_CHANGE)); F_W(c->d[0]);
        if (c->d[0] & 0xFFFF) { room_change_go(); return 0; }
        call(0xFA6C);                                   /* the companion */
        call(0x133B2);                                  /* the enemies */
        call(0x885A);
        call(0x10A1E);                                  /* items and effects */
        call(0xB5AC);
        call(0xB864);
        call(0x2D1C);                                   /* the item rain */
    }
    if (!scripts_run(0x908A)) return 0;                 /* $C7E6: the room's scripts and objects */
r908A:
    call(0x9CD2);                                       /* the scroll */
    if (rd16(0xFFFF965Cu)) {
        uint16_t t = (uint16_t)(rd16(0xFFFF965Cu) - 1);
        wr16(0xFFFF965Cu, t);
        if (!t) call(0x7F00);
    }
    c->a[4] = sx(rd16(PLAYER_ENT));
    call(0xA3D8);                                       /* the room's triggers */
    c->d[0] = set_w(c->d[0], rd16(ROOM_CHANGE)); F_W(c->d[0]);
    if (c->d[0] & 0xFFFF) { room_change_go(); return 0; }
    if (rd8(0xFFFF9F05u)) return 1;
    if (!(rd8(0xFFFF9F0Du) & 0x80)) return 1;           /* start pressed: the status screen */
    c->d[0] = 0xFFFFFFFCu; call(0x366);
    wr8(0xFFFF950Au, 0xFF);
    c->d[0] = 0x56; call(0x366);
    wr16(0xFFFF965Cu, 0);
    call(0xA66);                                        /* the world frozen */
    if (!(rd8(0xFFFF9659u) & 4)) {
        if (!call_ret(0x7582, 0x90E4)) return 0;        /* the status screen */
r90E4:
        if (!call_ret(0xDB3C, 0x90E8)) return 0;        /* the equipment */
    } else if (!call_ret(0x7F0E, 0x90EE)) return 0;     /* paused */
r90EE:
    c->d[0] = 0xFFFFFFFDu; call(0x366);
    wr8(0xFFFF950Au, 0);
    call(0xA8E);
    return 1;
}
/* $8FFC: the game task's loop: unless a message is due (to $9228), a frame: the counter, the
 * task's stack kept ($9662, for what resets it), the yield; then the frame's work */
static void game_loop(void)
{
    for (;;) {
        if (!(rd16(0xFFFF9686u) & 0x8000) && !rd8(0xFFFF9667u)) { leave_to(0x9228); return; }
        wr8(0xFFFF9659u, rd8(0xFFFF9659u) & 0xFE);
        wr16(0xFFFF965Au, (uint16_t)(rd16(0xFFFF965Au) + 1));
        wr32(0xFFFF9662u, c->a[7]);
        wait_frame(0x901C);
        if (!game_frame(0)) return;
    }
}
void ScriptsRun(Cpu *cpu)                                                              /* $C7E6 */
{
    c = cpu; NEEDS_PASS(0xC7E6);
    if (scripts_run(pop32())) port_continue_at(c, c->a[0], 0);
}
void GameLoop(Cpu *cpu)     { c = cpu; NEEDS_PASS(0x8FFC); game_loop(); }              /* $8FFC */
void GameFrame(Cpu *cpu)    { c = cpu; NEEDS_PASS(0x901C); if (game_frame(0)) game_loop(); }  /* $901C: a pass resumed there */
/* batch N3b: the game task's code started where it waited (its resume and continuation
 * addresses): a loaded state finds it there, with no coroutine to carry on in */
#define FRAME_AT(name, at) void name(Cpu *cpu) { c = cpu; NEEDS_PASS(at); if (game_frame(at)) game_loop(); }
FRAME_AT(GameFrameAt9038, 0x9038)
FRAME_AT(GameFrameAt908A, 0x908A)
FRAME_AT(GameFrameAt90E4, 0x90E4)
FRAME_AT(GameFrameAt90E8, 0x90E8)
FRAME_AT(GameFrameAt90EE, 0x90EE)
#define TASK_AT(name, fn, at) void name(Cpu *cpu) { c = cpu; NEEDS_PASS(at); fn(at); }
TASK_AT(ArrivePlainAt8FF2, arrive_plain, 0x8FF2)
TASK_AT(NextRoomAt9138, next_room, 0x9138)
TASK_AT(GameOverAt914C, game_over, 0x914C)
TASK_AT(GameOverAt9182, game_over, 0x9182)
TASK_AT(GameOverAt9194, game_over, 0x9194)
TASK_AT(GameOverAt91AA, game_over, 0x91AA)
TASK_AT(RoomMessageAt923C, room_message, 0x923C)
TASK_AT(RoomMessageAt9240, room_message, 0x9240)
TASK_AT(RoomMessageAt9264, room_message, 0x9264)
TASK_AT(RoomMessageAt927C, room_message, 0x927C)
TASK_AT(RoomMessageAt9280, room_message, 0x9280)
/* batch N3c: the sequences started where they waited or where a helper goes on (a loaded state) */
TASK_AT(SeqDoorAtBE44, door_then_next, 0xBE44)
TASK_AT(SeqDoorAtBE4C, door_then_next, 0xBE4C)
TASK_AT(SeqWarpAtBE62, seq_warp, 0xBE62)
TASK_AT(SeqInsideAtBE9C, seq_inside, 0xBE9C)
TASK_AT(SeqInsideAtBEBC, seq_inside, 0xBEBC)
TASK_AT(SeqInsideAtBED0, seq_inside, 0xBED0)
TASK_AT(SeqInsideAtBED4, seq_inside, 0xBED4)
TASK_AT(SeqInsideAtBED8, seq_inside, 0xBED8)
TASK_AT(SeqInsideAtBEE6, seq_inside, 0xBEE6)
TASK_AT(SeqInsideAtBEEA, seq_inside, 0xBEEA)
TASK_AT(SeqOutsideAtBF42, seq_outside, 0xBF42)
TASK_AT(SeqOutsideAtBF46, seq_outside, 0xBF46)
TASK_AT(SeqOutsideAtBF4A, seq_outside, 0xBF4A)
TASK_AT(SeqOutsideAtBF5C, seq_outside, 0xBF5C)
TASK_AT(SeqOutsideAtBF6A, seq_outside, 0xBF6A)
TASK_AT(SeqHouseAtBFCA, seq_house, 0xBFCA)
TASK_AT(SeqHouseAtBFD2, seq_house, 0xBFD2)
TASK_AT(SeqHouseAtBFE0, seq_house, 0xBFE0)
TASK_AT(SeqHouseAtC056, seq_house, 0xC056)
TASK_AT(SeqHouseAtC09C, seq_house, 0xC09C)
TASK_AT(SeqHouseAtC0B2, seq_house, 0xC0B2)
TASK_AT(SeqWalkInAtC1A4, seq_walk_in, 0xC1A4)
TASK_AT(SeqWalkInAtC1AC, seq_walk_in, 0xC1AC)
TASK_AT(SeqWalkInAtC1B0, seq_walk_in, 0xC1B0)
TASK_AT(SeqWalkInAtC1B4, seq_walk_in, 0xC1B4)
TASK_AT(SeqWalkInAtC1CC, seq_walk_in, 0xC1CC)
/* batch N3d: the arrivals, the conversation, a thing received, the statue and the room scripts'
 * driver started where they waited or where a helper goes on (a loaded state) */
#define CONT_AT(name, fn, at) void name(Cpu *cpu) { c = cpu; NEEDS_PASS(at); fn(0, at); port_continue_at(c, c->a[0], 0); }
CONT_AT(FadeInWaitAtC5E4, fade_in_wait_at, 0xC5E4)
CONT_AT(ReceivePalettesAtC35C, receive_palettes_at, 0xC35C)
CONT_AT(ReceivePalettesAtC360, receive_palettes_at, 0xC360)
/* batch N3e: the room scripts' frames, the ride's moves, the scripts' waits and messages started
 * where they waited or where a helper goes on (a loaded state) */
CONT_AT(Room0ReleaseAtCA2E, room0_release_at, 0xCA2E)
CONT_AT(Room0FrameAtC958, room0_frame_at, 0xC958)
CONT_AT(Room1FrameAtCAC0, room1_frame_at, 0xCAC0)
CONT_AT(Room1FrameAtCAE2, room1_frame_at, 0xCAE2)
CONT_AT(Room1FrameAtCB06, room1_frame_at, 0xCB06)
CONT_AT(Room12FrameAtD31C, room12_frame_at, 0xD31C)
CONT_AT(Room12FrameAtD324, room12_frame_at, 0xD324)
CONT_AT(Room13FrameAtD476, room13_frame_at, 0xD476)
CONT_AT(Room13RideAtD4BA, room13_ride_at, 0xD4BA)
CONT_AT(Room13RideAtD4D6, room13_ride_at, 0xD4D6)
CONT_AT(Room13RideAtD4EA, room13_ride_at, 0xD4EA)
CONT_AT(Room13RideAtD4FC, room13_ride_at, 0xD4FC)
CONT_AT(Room13RideAtD548, room13_ride_at, 0xD548)
CONT_AT(Room13RideAtD582, room13_ride_at, 0xD582)
CONT_AT(Room13RideAtD598, room13_ride_at, 0xD598)
CONT_AT(Room13RideAtD5BA, room13_ride_at, 0xD5BA)
CONT_AT(Room13RideAtD5CE, room13_ride_at, 0xD5CE)
CONT_AT(Room13RideAtD5F2, room13_ride_at, 0xD5F2)
CONT_AT(Room15FrameAtD780, room15_frame_at, 0xD780)
CONT_AT(Room15FrameAtD7BC, room15_frame_at, 0xD7BC)
CONT_AT(Room15FrameAtD7C2, room15_frame_at, 0xD7C2)
CONT_AT(Room15FrameAtD83E, room15_frame_at, 0xD83E)
CONT_AT(Room15FrameAtD84A, room15_frame_at, 0xD84A)
CONT_AT(Room15FrameAtD85E, room15_frame_at, 0xD85E)
CONT_AT(ScriptWaitAtDAF8, script_wait_at, 0xDAF8)
CONT_AT(ScriptMessageAtDB1C, script_message_at, 0xDB1C)
CONT_AT(ScriptMessageAtDB20, script_message_at, 0xDB20)
void Room11FrameAtD08A(Cpu *cpu) { c = cpu; NEEDS_PASS(0xD08A); if (room11_frame_at(0, 0xD08A)) port_continue_at(c, c->a[0], 0); }
void Room11FrameAtD08E(Cpu *cpu) { c = cpu; NEEDS_PASS(0xD08E); if (room11_frame_at(0, 0xD08E)) port_continue_at(c, c->a[0], 0); }
void ScriptsRunAtC808(Cpu *cpu) { c = cpu; NEEDS_PASS(0xC808); if (scripts_run_at(0, 0xC808)) port_continue_at(c, c->a[0], 0); }
void ScriptsRunAtC814(Cpu *cpu) { c = cpu; NEEDS_PASS(0xC814); if (scripts_run_at(0, 0xC814)) port_continue_at(c, c->a[0], 0); }
TASK_AT(ArriveDoorAtC600, arrive_door, 0xC600)
TASK_AT(ArriveDoorAtC604, arrive_door, 0xC604)
TASK_AT(ArriveDoorAtC608, arrive_door, 0xC608)
TASK_AT(ArriveStillAtC620, arrive_still, 0xC620)
TASK_AT(ArriveStillAtC62A, arrive_still, 0xC62A)
TASK_AT(ArriveWalkOutAtC6AA, arrive_walk_out, 0xC6AA)
TASK_AT(ArriveWalkOutAtC6B6, arrive_walk_out, 0xC6B6)
TASK_AT(ArriveWalkOutAtC6BE, arrive_walk_out, 0xC6BE)
TASK_AT(ArriveWalkOutAtC6C2, arrive_walk_out, 0xC6C2)
TASK_AT(ArriveWalkOutAtC6C6, arrive_walk_out, 0xC6C6)
TASK_AT(ArriveWalkOutAtC6DE, arrive_walk_out, 0xC6DE)
TASK_AT(ArriveInsideDoorAtC6FE, arrive_inside_door, 0xC6FE)
TASK_AT(ArriveInsideDoorAtC702, arrive_inside_door, 0xC702)
TASK_AT(ArriveInsideDoorAtC706, arrive_inside_door, 0xC706)
TASK_AT(ArriveInsideDoorAtC70A, arrive_inside_door, 0xC70A)
TASK_AT(ArriveFadeAtC724, arrive_fade, 0xC724)
TASK_AT(ArriveTalkAtC730, arrive_talk, 0xC730)
TASK_AT(ArriveTalkAtC73E, arrive_talk, 0xC73E)
TASK_AT(ArriveTalkAtC752, arrive_talk, 0xC752)
TASK_AT(SeqTalkAtC20A, seq_talk, 0xC20A)
TASK_AT(SeqTalkAtC20E, seq_talk, 0xC20E)
TASK_AT(SeqTalkAtC248, seq_talk, 0xC248)
TASK_AT(SeqReceiveAtC298, seq_receive, 0xC298)
TASK_AT(SeqReceiveAtC2C2, seq_receive, 0xC2C2)
TASK_AT(SeqStatueAtC4BE, seq_statue, 0xC4BE)
TASK_AT(SeqStatueAtC4C2, seq_statue, 0xC4C2)
TASK_AT(SeqStatueAtC4E4, seq_statue, 0xC4E4)
TASK_AT(SeqStatueAtC4E8, seq_statue, 0xC4E8)
TASK_AT(SeqStatueAtC4FA, seq_statue, 0xC4FA)
TASK_AT(SeqStatueAtC53E, seq_statue, 0xC53E)
TASK_AT(SeqStatueAtC542, seq_statue, 0xC542)
void RoomChangeGo(Cpu *cpu) { c = cpu; NEEDS_PASS(0x91EA); room_change_go(); }        /* $91EA */
