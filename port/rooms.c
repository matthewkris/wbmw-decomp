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
 * Ported register for register, with every register and flag in the contract. */
#include "world.h"

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
