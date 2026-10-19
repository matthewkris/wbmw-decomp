/* Enemy graphics and spawning ($12000-$14000): the kinds' graphics loaded into VRAM for a room,
 * an enemy set up from its kind's records, the free slots found, the room's spawn list read.
 * Batch T53 on.
 *
 * Plain C (as from batch T42): each promises the stack pointer and what it writes, plus the
 * registers and flags its callers use (the slot found in a2 or a3 and its mi; a2/a4 swapped by
 * the spawns). A kind is a word: its group (high byte: 0 monsters, 2 objects, 4 people, 8 more)
 * and its number (low byte, even). */
#include "world.h"
#include "task.h"

static Cpu *c;
static void call(uint32_t addr) { call_68k(c, addr); }

static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }
static uint32_t e(int32_t off) { return c->a[4] + (uint32_t)off; }     /* -$xxxx(a4) */
static void swap_a2a4(void) { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }

#define KIND_RECORDS  0x1BC0Eu      /* per group: offsets to its kinds' records */
#define KINDS         0xFFFFA122u   /* the room's kinds: (kind, VRAM tile) pairs; $A142 how many */
#define KIND_COUNT    0xFFFFA142u
#define GFX_SLOTS     0xFFFF8E08u   /* the VRAM tiles in use, by graphics block */

/* $125A6: a0 kind d5's graphics record (3 bytes each) in its group's table at $1BC0E */
static void kind_record(void)
{
    c->a[2] = KIND_RECORDS;
    c->d[0] = set_w(c->d[0], c->d[5]);
    c->a[0] = c->a[2];
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 8);
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    c->d[0] = set_b(c->d[0], c->d[5]);
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 1);
    c->d[1] = set_w(c->d[1], c->d[1] * 2);
    c->d[0] = set_w(c->d[0], c->d[0] + c->d[1]);
    c->a[0] += sx(c->d[0]);
}
/* $12632: a1 the slots' entry for tile d1 counted from $30 down */
static void gfx_slot_at(void)
{
    c->a[1] = GFX_SLOTS;
    c->d[0] = 0x30;
    c->d[0] = set_w(c->d[0], c->d[0] - c->d[1]);
    c->d[0] = set_w(c->d[0], c->d[0] * 2);
    c->a[1] += sx(c->d[0]);
}
/* $125C4: kind d5's graphics: unless the room has the kind already, it is added to the list
 * (its palette, if any, into $A146) with the VRAM of its blocks: the same blocks already loaded
 * are reused (searched from the top), else they go on after $A144 */
static void load_kind_gfx(void)
{
    kind_record();
    c->d[0] = set_w(c->d[0], c->d[5]);
    c->d[4] = rd8(c->a[0]); c->a[0]++;
    c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
    c->d[0] = set_w(c->d[0], (uint16_t)(c->d[0] << 8));
    c->d[0] = set_b(c->d[0], rd8(c->a[0]));
    c->a[2] += sx(c->d[0]);
    c->a[3] = KINDS;
    c->d[1] = set_w(c->d[1], rd16(KIND_COUNT));
    while (c->d[1] & 0xFFFF) {
        if (rd16(c->a[3]) == (uint16_t)c->d[5]) return;
        c->a[3] += 4;
        c->d[1] = set_w(c->d[1], c->d[1] - 1);
    }
    wr16(KIND_COUNT, (uint16_t)(rd16(KIND_COUNT) + 1));
    wr16(c->a[3], (uint16_t)c->d[5]); c->a[3] += 2;
    if (c->d[4] & 0xFF) wr16(0xFFFFA146u, (uint16_t)c->d[4]);
    c->d[4] = set_w(c->d[4], rd16(c->a[2])); c->a[2] += 2;
    c->d[1] = set_w(c->d[1], rd16(0xFFFFA144u));
    for (;;) {
        if ((uint16_t)c->d[1] < (uint16_t)c->d[4]) {          /* not enough below: new ones */
            c->d[1] = set_w(c->d[1], c->d[4] + rd16(0xFFFFA144u));
            wr16(0xFFFFA144u, (uint16_t)c->d[1]);
            gfx_slot_at();
            do {
                wr16(c->a[1], rd16(c->a[2])); c->a[1] += 2; c->a[2] += 2;
                c->d[4] = set_w(c->d[4], c->d[4] - 1);
            } while (c->d[4] & 0xFFFF);
            break;
        }
        c->a[0] = c->a[2];                                   /* the same blocks at tile d1? */
        gfx_slot_at();
        c->d[0] = set_w(c->d[0], c->d[4] - 1);
        int same;
        do {
            same = rd16(c->a[0]) == rd16(c->a[1]);
            c->a[0] += 2; c->a[1] += 2;
            if (!same) break;
            c->d[0] = set_w(c->d[0], c->d[0] - 1);
        } while ((c->d[0] & 0xFFFF) != 0xFFFF);
        if (same) break;
        c->d[1] = set_w(c->d[1], c->d[1] - 1);
    }
    c->d[0] = 0x30;
    c->d[0] = set_w(c->d[0], (uint16_t)((c->d[0] - c->d[1]) << 5));
    wr16(c->a[3], (uint16_t)c->d[0]);
}
/* $12640: the kinds listed at a0 (to a negative word) loaded */
static void load_gfx_list(void)
{
    for (;;) {
        c->d[5] = set_w(c->d[5], rd16(c->a[0])); c->a[0] += 2;
        if (c->d[5] & 0x8000) { F_W(c->d[5]); return; }
        c->a[7] -= 4; wr32(c->a[7], c->a[0]);
        load_kind_gfx();
        c->a[0] = rd32(c->a[7]); c->a[7] += 4;
    }
}
/* $126AA: a0 kind d5's look (group 2: one byte each; the rest: 3 bytes each, after 3) */
static void kind_look(void)
{
    c->a[2] = KIND_RECORDS;
    c->d[0] = set_w(c->d[0], c->d[5]);
    c->a[0] = c->a[2];
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 8);
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    if ((c->d[0] & 0xFF) == 2) c->d[0] = set_b(c->d[0], c->d[5]);
    else {
        c->a[0] += 3;
        c->d[0] = set_b(c->d[0], c->d[5]);
        c->d[1] = set_w(c->d[1], c->d[0]);
        c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 1);
        c->d[1] = set_w(c->d[1], c->d[1] * 2);
        c->d[0] = set_w(c->d[0], c->d[0] + c->d[1]);
    }
    c->a[0] += sx(c->d[0]);
}
/* $126D4: kind d1's movement record ($1381C): monsters its spawn bits, speeds and accelerations;
 * group 2 speeds and accelerations; people a speed and palette */
static void kind_movement(void)
{
    c->a[0] = 0x1381C;
    c->a[1] = c->a[0];
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[1] >> 8);
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    c->d[0] = set_b(c->d[0], c->d[1]);
    c->d[0] = set_w(c->d[0], rd16(c->a[0] + sx(c->d[0])));
    c->a[1] += sx(c->d[0]);
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[1] >> 8);
    c->a[0] = 0x126FC + sx(rd16(0x126FC + sx(c->d[0])));
    switch (c->a[0]) {
    case 0x12706:
        wr8(e(-0x2AFE), rd8(c->a[1])); c->a[1]++;
        wr8(e(-0x25FD), rd8(c->a[1])); c->a[1]++;
        /* fall through */
    case 0x1270E:
        c->d[0] = (uint32_t)rd8(c->a[1]) << 4; c->a[1]++;
        wr16(e(-0x3300), (uint16_t)c->d[0]);
        c->d[0] = (uint32_t)rd8(c->a[1]) << 4; c->a[1]++;
        wr16(e(-0x32FE), (uint16_t)c->d[0]);
        c->d[0] = set_w(c->d[0], (uint16_t)(int8_t)rd8(c->a[1])); c->a[1]++;
        wr16(e(-0x3400), (uint16_t)c->d[0]);
        c->d[0] = set_w(c->d[0], (uint16_t)(int8_t)rd8(c->a[1])); c->a[1]++;
        wr16(e(-0x33FE), (uint16_t)c->d[0]);
        wr8(e(-0x3CFD), 0xC);
        return;
    case 0x1273A:
        c->d[0] = (uint32_t)rd8(c->a[1]) << 4; c->a[1]++;
        wr16(e(-0x3300), (uint16_t)c->d[0]);
        wr8(e(-0x3CFD), rd8(c->a[1])); c->a[1]++;
        return;
    default: call(c->a[0]); return;                 /* past the table: as the original jumps */
    }
}
/* $1274A: kind d1's fighting record ($1BA6C): monsters health (*32), level, damage and points;
 * the rest a damage word */
static void kind_fighting(void)
{
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[1] >> 8);
    c->a[1] = 0x1BA6C + sx(rd16(0x1BA6C + sx(c->d[0])));
    c->a[0] = 0x12762 + sx(rd16(0x12762 + sx(c->d[0])));
    switch (c->a[0]) {
    case 0x1276C:
        c->d[0] = c->d[1] & 0xFF;
        c->d[2] = set_w(c->d[2], c->d[0]);
        c->d[0] = set_w(c->d[0], c->d[0] * 3);
        c->a[1] += sx(c->d[0]);
        c->d[0] = (uint32_t)rd8(c->a[1]) << 5; c->a[1]++;
        wr16(e(-0x2600), (uint16_t)c->d[0]);
        c->d[0] = rd8(c->a[1]); c->a[1]++;
        wr16(e(-0x26FE), (uint16_t)c->d[0]);
        wr16(e(-0x2700), rd16(c->a[1])); c->a[1] += 2;
        wr16(e(-0x2300), rd16(c->a[1])); c->a[1] += 2;
        return;
    case 0x12794:
        c->d[0] = c->d[1] & 0xFF;
        wr16(e(-0x2700), rd16(c->a[1] + c->d[0]));
        return;
    case 0x12792: return;
    default: call(c->a[0]); return;                 /* past the table: as the original jumps */
    }
}
/* $127A0: the entity's state cleared ($C1C) and set going: in use, frame speed $100 */
static void entity_reset(void)
{
    call(0xC1C);
    wr16(e(-0x4000), 0x8000);
    wr16(e(-0x39FE), 0x100);
    wr8(e(-0x2200), 0xFF);
    c->d[0] = 0;
    wr32(e(-0x3100), 0); wr16(e(-0x3FFE), 0); wr16(e(-0x3E00), 0); wr32(e(-0x3200), 0);
    wr8(e(-0x2BFF), 0); wr32(e(-0x2A00), 0); wr32(e(-0x2900), 0); wr8(e(-0x2AFD), 0);
    wr16(e(-0x21FE), 0);
    F_W(0);
}
/* $12684: enemy a4 of kind d1 set up: its look, movement, fighting and state; a2 and a4 swapped
 * back */
static void enemy_init(void)
{
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->a[2]);
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->d[1]);
    c->d[5] = set_w(c->d[5], c->d[1]);
    kind_look();
    c->d[1] = set_w(c->d[1], rd16(c->a[7])); c->a[7] += 2;
    c->a[2] = sx(rd16(c->a[7])); c->a[7] += 2;
    wr8(e(-0x3D00), rd8(c->a[0])); c->a[0]++;
    wr8(e(-0x3CFF), rd8(c->a[0]));
    kind_movement();
    kind_fighting();
    entity_reset();
    swap_a2a4();
}
/* $12650: enemy slot a2 made kind d1 at its spawn point d0 and (d6, d7), its graphics' tile from
 * the room's list (a kind not in it stops the game: the original loops at $1267C); then set up */
static int spawn_at(void)
{
    swap_a2a4();
    wr16(e(-0x2B00), (uint16_t)c->d[0]);
    wr16(e(-0x2BFE), (uint16_t)c->d[1]);
    wr16(e(-0x3800), (uint16_t)c->d[6]); wr16(e(-0x2100), (uint16_t)c->d[6]);
    wr16(e(-0x3700), (uint16_t)c->d[7]); wr16(e(-0x20FE), (uint16_t)c->d[7]);
    c->a[0] = KINDS;
    c->d[0] = set_w(c->d[0], rd16(KIND_COUNT));
    for (;;) {
        uint16_t k = rd16(c->a[0]); c->a[0] += 2;
        if (k == (uint16_t)c->d[1]) break;
        c->a[0] += 2;
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
        if (!(c->d[0] & 0xFFFF)) { port_continue_at(c, 0x1267C, 0); return 1; }
    }
    wr16(e(-0x3DFE), rd16(c->a[0]));
    enemy_init();
    return 0;
}
/* $127DC: enemy a2's flags from its spawn point a3: drawn behind, its collision kind, spawn bits */
static void spawn_flags(void)
{
    wr8(c->a[2] - 0x2F00, 0);
    c->d[0] = set_b(c->d[0], rd8(c->a[3] - 0x5EA1));
    if (c->d[0] & 0x80) wr8(c->a[2] - 0x2F00, 0x80);
    c->d[0] = set_b(c->d[0], c->d[0] & 0x7F);
    wr8(c->a[2] - 0x3200, (uint8_t)c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[3] - 0x5EA2));
    uint8_t v = rd8(c->a[2] - 0x2AFE) | (uint8_t)c->d[0];
    wr8(c->a[2] - 0x2AFE, v); F_B(v);
}
/* the slots' search (dbpl): from a slot, while in use (negative) and the count lasts, on by
 * step; mi if none is free */
static void find_free(uint32_t *reg, uint32_t first, int32_t field, uint32_t step, uint32_t count)
{
    *reg = first;
    c->d[2] = count;
    for (;;) {
        uint8_t v = rd8(*reg + (uint32_t)field);
        F_B(v);
        if (!(v & 0x80)) return;
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
        if ((c->d[2] & 0xFFFF) == 0xFFFF) return;
        *reg += step;
    }
}
static void find_free_a0e2(void) { find_free(&c->a[3], 0xFFFFA0E2u, 0, 0x10, 1); }                 /* $127FE */
static void find_free_enemy(void) { find_free(&c->a[2], sx(rd16(0xFFFFA11Au)), -0x4000, 4, 0xF); }  /* $12812 */
/* $12826: a free spawn point ($A15C): 16 of them, or 4 from $40 in a boss's room ($9659 bit 4) */
static void find_free_spawn(void)
{
    if (rd8(0xFFFF9659u) & 0x10) find_free(&c->a[3], 0x40, -0x5EA4, 4, 3);
    else find_free(&c->a[3], 0, -0x5EA4, 4, 0xF);
}

#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(KindRecord, kind_record)             /* $125A6 */
ENTRY(LoadKindGfx, load_kind_gfx)          /* $125C4 */
ENTRY(GfxSlotAt, gfx_slot_at)              /* $12632 */
ENTRY(LoadGfxList, load_gfx_list)          /* $12640 */
void SpawnAt(Cpu *cpu) { c = cpu; if (!spawn_at()) c->ret = 0; }   /* $12650 */
ENTRY(EnemyInit, enemy_init)               /* $12684 */
ENTRY(KindLook, kind_look)                 /* $126AA */
ENTRY(KindMovement, kind_movement)         /* $126D4 */
ENTRY(KindFighting, kind_fighting)         /* $1274A */
ENTRY(EntityReset, entity_reset)           /* $127A0 */
ENTRY(SpawnFlags, spawn_flags)             /* $127DC */
ENTRY(FindFreeA0E2, find_free_a0e2)        /* $127FE */
ENTRY(FindFreeEnemy, find_free_enemy)      /* $12812 */
ENTRY(FindFreeSpawn, find_free_spawn)      /* $12826 */

/* ---- batch T54: the room's spawn list ----
 * $A11E: the room's kinds to load (to a negative word), then its spawn points: each a kind byte
 * (0-15) and its fields, into the next free spawn point ($A15C on, 4 bytes apart); $F8-$FE are
 * commands (tests of the game's flags that jump, a spawn point's timer and so on), $FF the end. */
static uint32_t s3(int32_t off) { return c->a[3] + (uint32_t)off; }   /* -$xxxx(a3) */
static void sp_b(int32_t off) { wr8(s3(off), rd8(c->a[1])); c->a[1]++; }
static void sp_w(int32_t off) { wr16(s3(off), rd16(c->a[1])); c->a[1] += 2; }
static void list_jump(int taken) { if (taken) c->a[1] += sx(rd16(c->a[1])); else c->a[1] += 2; }
/* $12A56: the spawn point's counters cleared and it set in use */
static void spawn_point_ready(void)
{
    c->d[0] = 0;
    wr8(s3(-0x5DB2), 0); wr8(s3(-0x5DB3), 0); wr8(s3(-0x5DB1), 0); wr8(s3(-0x5EA2), 0);
    wr8(s3(-0x5EA4), 0x80);
}
/* $12A4A: its kind of enemy and its place (x, y), then ready */
static void spawn_point_place(void) { sp_w(-0x5E52); sp_w(-0x5E04); sp_w(-0x5E02); spawn_point_ready(); }
/* the commands ($1289E) */
static void list_command(void)
{
    switch (c->a[0]) {
    case 0x128CC:                                   /* $F8: jump if game flag n is set */
        c->d[0] = set_b(c->d[0], rd8(c->a[1])); c->a[1]++;
        c->d[1] = c->d[0] & 0xFF;                    /* $2758: its byte and bit */
        c->d[0] = set_w(c->d[0], c->d[0] & 7);
        c->a[0] = 0xFFFF9992u;
        c->d[1] = set_w(c->d[1], ((uint16_t)c->d[1] >> 3) ^ 1);
        list_jump(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1);
        return;
    case 0x128E0:                                   /* $F9: jump if game flag n is set ($274C) */
        c->d[0] = set_b(c->d[0], rd8(c->a[1])); c->a[1]++;
        call(0x274C);
        list_jump(!(c->ccr & CC_Z));
        return;
    case 0x128F0:                                   /* $FA: the spawn point's timer n */
        c->d[0] = rd8(c->a[1]); c->a[1]++;
        wr16(s3(-0x5CC2), (uint16_t)c->d[0]);
        return;
    case 0x128FA:                                   /* $FB: its timer $100 + n */
        c->d[0] = set_w(c->d[0], 0x100 | rd8(c->a[1])); c->a[1]++;
        wr16(s3(-0x5CC2), (uint16_t)c->d[0]);
        return;
    case 0x12906:                                   /* $FC: jump if the player has item n ($23E4) */
        c->d[0] = set_b(c->d[0], rd8(c->a[1])); c->a[1]++;
        c->a[7] -= 4; wr32(c->a[7], c->a[1]);
        call(0x23E4);
        c->a[1] = rd32(c->a[7]); c->a[7] += 4;
        list_jump(!(c->ccr & CC_Z));
        return;
    case 0x1291A:                                   /* $FD: its other timer n */
        c->d[0] = rd8(c->a[1]); c->a[1]++;
        wr16(s3(-0x5CC4), (uint16_t)c->d[0]);
        return;
    case 0x12924:                                   /* $FE: jump */
        c->a[1]++;
        list_jump(1);
        return;
    default: call(c->a[0]); return;                 /* $FF ends the list before its entry */
    }
}
/* the spawn points' kinds ($128AC): their fields from the list */
static void spawn_point_kind(void)
{
    switch (c->a[0]) {
    case 0x1292A:                                   /* 0: one enemy */
        sp_b(-0x5EA1);
        wr8(s3(-0x5DB4), 1);
        spawn_point_place();
        return;
    case 0x12938:                                   /* 1, 2: so many, so often */
        sp_b(-0x5EA1); sp_b(-0x5D64); sp_b(-0x5DB4);
        wr8(s3(-0x5D63), 0);
        spawn_point_place();
        return;
    case 0x1294C: {                                 /* 3: a row, a step apart */
        sp_b(-0x5EA1);
        c->d[0] = set_b(c->d[0], rd8(c->a[1])); c->a[1]++;
        c->d[0] = set_w(c->d[0], (uint16_t)(int8_t)c->d[0]);
        wr16(s3(-0x5D14), (uint16_t)c->d[0]);
        c->d[0] = set_b(c->d[0], rd8(c->a[1])); c->a[1]++;
        wr8(s3(-0x5D61), (uint8_t)c->d[0]);
        sp_b(-0x5D64);
        c->d[1] = set_b(c->d[1], rd8(c->a[1])); c->a[1]++;
        wr8(s3(-0x5DB4), (uint8_t)c->d[1]);
        c->d[0] = set_w(c->d[0], (uint16_t)(int8_t)c->d[0]);
        c->d[2] = 0;
        int odd = c->d[1] & 1;
        c->d[1] = set_b(c->d[1], (uint8_t)c->d[1] >> 1);
        if (!odd) { c->d[2] = set_w(c->d[2], c->d[0]); c->d[2] = set_w(c->d[2], (uint16_t)((int16_t)c->d[2] >> 1)); }
        do {
            c->d[2] = set_w(c->d[2], c->d[2] + c->d[0]);
            c->d[1] = set_b(c->d[1], c->d[1] - 1);
        } while (c->d[1] & 0xFF);
        wr16(s3(-0x5D12), (uint16_t)c->d[2]);
        spawn_point_place();
        return;
    }
    case 0x12982:                                   /* 4 */
        sp_b(-0x5D63); sp_w(-0x5D14);
        spawn_point_place();
        return;
    case 0x1298E:                                   /* 5: a path, if any */
        sp_b(-0x5D63);
        wr32(s3(-0x5D14), 0);
        wr16(s3(-0x5E54), 0);
        c->d[0] = set_w(c->d[0], rd16(c->a[1])); c->a[1] += 2;
        if (c->d[0] & 0xFFFF) { c->a[0] = c->a[1] - 2 + sx(c->d[0]); wr32(s3(-0x5D14), c->a[0]); }
        spawn_point_place();
        return;
    case 0x129AA:                                   /* 6: n records of 6 bytes, kept where they are */
        c->d[0] = rd8(c->a[1]); c->a[1]++;
        wr8(s3(-0x5D63), (uint8_t)c->d[0]);
        wr32(s3(-0x5D14), c->a[1]);
        c->d[0] = set_w(c->d[0], c->d[0] * 2);
        c->d[1] = set_w(c->d[1], c->d[0]);
        c->d[0] = set_w(c->d[0], c->d[0] * 2);
        c->d[0] = set_w(c->d[0], c->d[0] + c->d[1]);
        c->a[1] += sx(c->d[0]);
        wr8(s3(-0x5EA4), 0x80);
        return;
    case 0x129C8: case 0x129CE:                     /* 7, 8 */
        sp_b(-0x5D63);
        spawn_point_place();
        return;
    case 0x129D4:                                   /* 9 */
        c->a[1]++;
        spawn_point_place();
        return;
    case 0x129D8:                                   /* 10: a word of $A14C's table, no spawn point */
        c->d[0] = rd8(c->a[1]); c->a[1]++;
        c->a[0] = 0xFFFFA14Cu;
        c->d[0] = set_w(c->d[0], c->d[0] << 2);
        wr16(c->a[0] + 2 + sx(c->d[0]), rd16(c->a[1])); c->a[1] += 2;
        return;
    case 0x129E8:                                   /* 11 */
        sp_b(-0x5EA1); sp_b(-0x5D64); sp_b(-0x5DB4);
        wr32(s3(-0x5D14), rd32(c->a[1])); c->a[1] += 4;
        wr8(s3(-0x5D63), 0);
        spawn_point_place();
        return;
    case 0x129FE:                                   /* 12 */
        c->a[1]++;
        wr8(s3(-0x5D61), 0);
        spawn_point_place();
        return;
    case 0x12A06:                                   /* 13: n words kept where they are */
        sp_b(-0x5EA1);
        c->d[0] = rd8(c->a[1]); c->a[1]++;
        wr8(s3(-0x5DB4), (uint8_t)c->d[0]);
        c->a[1]++;
        wr32(s3(-0x5D14), c->a[1]);
        c->d[0] = set_w(c->d[0], c->d[0] * 2);
        c->a[1] += sx(c->d[0]);
        spawn_point_place();
        return;
    case 0x12A1E:                                   /* 14: its data elsewhere */
        c->a[1]++;
        c->a[0] = c->a[1] + sx(rd16(c->a[1])); c->a[1] += 2;
        sp_w(-0x5E52);
        wr8(s3(-0x5D64), rd8(c->a[0])); c->a[0]++;
        wr32(s3(-0x5D14), c->a[0]);
        spawn_point_ready();
        return;
    case 0x12A32:                                   /* 15 */
        c->a[1]++;
        sp_w(-0x5D14); sp_w(-0x5D12); sp_b(-0x5E54); sp_b(-0x5E53);
        wr8(s3(-0x5DB4), 1);
        spawn_point_place();
        return;
    default: call(c->a[0]); return;
    }
}
/* $12848: the room's kinds' graphics loaded (not in a boss's room: there the list is all spawn
 * points), then its spawn points made, as long as there are free ones */
static void spawn_list(void)
{
    c->d[0] = rd32(0xFFFFA11Eu);
    if (!c->d[0]) { F_L(0); return; }
    c->a[0] = c->d[0];
    if (!(rd8(0xFFFF9659u) & 0x10)) load_gfx_list();
    c->a[1] = c->a[0];
    for (;;) {
        find_free_spawn();
        if (c->ccr & CC_N) return;
        c->d[0] = 0xFFFFFFFFu;
        wr32(s3(-0x5CC4), c->d[0]);
        for (;;) {
            c->d[1] = rd8(c->a[1]); c->a[1]++;
            if (c->d[1] == 0xFF) { c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); return; }
            if (c->d[1] < 0xF8) break;
            c->d[1] = set_w(c->d[1], (c->d[1] & 7) * 2);
            c->a[0] = 0x1289E + sx(rd16(0x1289E + c->d[1]));
            list_command();
        }
        wr8(s3(-0x5EA3), (uint8_t)c->d[1]);
        c->d[1] = set_w(c->d[1], c->d[1] * 2);
        c->a[0] = 0x128AC + sx(rd16(0x128AC + c->d[1]));
        spawn_point_kind();
    }
}
ENTRY(SpawnList, spawn_list)               /* $12848 */

/* ---- batch T55: the room's things ----
 * The things ($11C9E runs slots $10-$3C) by kind (-$31FF, table $11CCA): platforms that swing,
 * rise or shuttle, switches, springs and so on. Each is entered with a4 its slot and goes on to
 * $11CFC (thing_riders: what stands on it, its link -$2D00 an index into the riders' table
 * $9F8E, bit 0 set while something stands there), or returns. */
static void thing_riders(void);
static void thing_tail(void) { thing_riders(); }          /* bra $11CFC */
/* the link: d0 its word; when it is one, a0 the rider's entry and its bit 0 */
static int ridden(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x2D00)));
    if (c->d[0] & 0x8000) return 0;
    c->a[0] = sx(c->d[0]);
    return rd8(c->a[0] - 0x6072) & 1;
}
/* $2758 inline: game flag d0's byte (a0 + d1) and bit (d0) */
static void flag_at(void)
{
    c->d[1] = c->d[0] & 0xFF;
    c->d[0] = set_w(c->d[0], c->d[0] & 7);
    c->a[0] = 0xFFFF9992u;
    c->d[1] = set_w(c->d[1], ((uint16_t)c->d[1] >> 3) ^ 1);
}
static int flag_set(void)    /* bset: the old bit */
{
    uint32_t a = c->a[0] + sx(c->d[1]);
    uint8_t v = rd8(a), m = (uint8_t)(1u << (c->d[0] & 7));
    wr8(a, v | m);
    return (v & m) != 0;
}
static void thing_sound(uint32_t n) { c->d[0] = n; call(0x366); }
static void inc8(int32_t off) { wr8(e(off), (uint8_t)(rd8(e(off)) + 1)); }

/* $11F80 (x) and $11F86 (y): swings: speeding up to 2 one way, then (bit 0 clear) coasting for
 * -$31FE frames, then the other way (bit 7) */
static void thing_swing(int32_t off)
{
    c->a[0] = e(off);
    uint8_t f = rd8(e(-0x3200));
    if (f & 1) {
        int16_t v;
        if (!(f & 0x80)) { v = (int16_t)(rd16(c->a[0]) + 0x20); wr16(c->a[0], (uint16_t)v); if (v < 0x200) goto move; }
        else { v = (int16_t)(rd16(c->a[0]) - 0x20); wr16(c->a[0], (uint16_t)v); if (v > -0x200) goto move; }
        wr8(e(-0x31FD), rd8(e(-0x31FE)));
        wr8(e(-0x3200), rd8(e(-0x3200)) ^ 1);
    } else {
        uint8_t t = (uint8_t)(rd8(e(-0x31FD)) - 1);
        wr8(e(-0x31FD), t);
        if (!t) wr8(e(-0x3200), rd8(e(-0x3200)) ^ 0x81);
    }
move:
    call(0xAB4);
    thing_tail();
}
static void thing_swing_x(void) { thing_swing(-0x3600); }
static void thing_swing_y(void) { thing_swing(-0x35FE); }
/* $11FD2: a lift that starts when the player stands on it near its middle (bit 0; also $9994),
 * goes up when $9995 bit 2 says so, and stops after -$31FE frames (bit 1) */
static void thing_lift_up(void)
{
    uint8_t f = rd8(e(-0x3200));
    if (f & 2) goto move;
    if (f & 1) {
        uint8_t m = rd8(0xFFFF9995u);
        wr8(0xFFFF9995u, m & ~4);
        if (m & 4) { wr16(e(-0x35FE), rd16(e(-0x27FE))); thing_sound(0x41); }
        call(0xAB4);
        if (!rd16(e(-0x35FE))) goto tail;
        uint16_t t = (uint16_t)(rd16(e(-0x31FE)) - 1);
        wr16(e(-0x31FE), t);
        if (t) goto tail;
        c->d[0] = 2;
        wr8(e(-0x3200), rd8(e(-0x3200)) | 2);
        wr16(0xFFFF9994u, rd16(0xFFFF9994u) | 2);
        wr16(e(-0x35FE), 0);
        wr8(0xFFFF9F05u, 0);
        thing_sound(0x42);
        goto tail;
    }
    if (!ridden()) goto move;
    c->a[0] = sx(rd16(0xFFFF9EEEu));
    c->d[0] = set_w(c->d[0], rd16(e(-0x3800)) - rd16(c->a[0] - 0x3800));
    if (c->d[0] & 0x8000) c->d[0] = set_w(c->d[0], -c->d[0]);
    {
        int near = (uint16_t)c->d[0] < 8;
        c->d[0] = set_w(c->d[0], c->d[0] - 8);
        if (!near) goto move;
    }
    c->d[0] = 1;
    wr8(e(-0x3200), rd8(e(-0x3200)) | 1);
    wr16(0xFFFF9994u, rd16(0xFFFF9994u) | 1);
move:
    call(0xAB4);
tail:
    thing_tail();
}
/* $1211E: mi (d0 -1) when the thing is out of the room's map */
static int thing_out(void)
{
    c->d[1] = rd8(e(-0x34FE));
    c->d[0] = set_w(c->d[0], rd16(e(-0x3800)) + c->d[1]);
    c->d[0] = set_w(c->d[0], c->d[0] - 0xFE0);
    if (c->d[0] & 0x8000) { c->ccr = (uint16_t)((c->ccr & CC_X) | CC_N); return 1; }
    c->d[0] = set_w(c->d[0], c->d[0] + 0x50 - c->d[1] - c->d[1]);
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 4);
    if ((uint16_t)c->d[0] >= rd16(0xFFFF9798u)) { c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]); return 1; }
    c->d[1] = rd8(e(-0x34FD));
    c->d[0] = set_w(c->d[0], rd16(e(-0x3700)) - 0x1030 - c->d[1]);
    c->d[0] = set_w(c->d[0], c->d[0] << 2);
    if ((int16_t)c->d[0] >= (int16_t)rd16(0xFFFF979Au)) { c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]); return 1; }
    c->d[0] = 0; F_L(0);
    return 0;
}
/* $12058: a block pushed off by the player standing on it: it falls (into water: a splash) and
 * is gone off the map */
static void thing_block(void)
{
    if (!(rd8(e(-0x3200)) & 1)) {
        if (!ridden()) { call(0xAB4); thing_tail(); return; }
        c->d[0] = 2;
        call(0x7E8);
        wr8(e(-0x3200), rd8(e(-0x3200)) | 1);
        wr16(e(-0x35FE), 0xFF00);
        c->d[0] = set_w(c->d[0], 0x400);
        if (rd8(e(-0x31FE)) & 0x80) c->d[0] = set_w(c->d[0], -c->d[0]);
        wr16(e(-0x3600), (uint16_t)c->d[0]);
    }
    call(0x2F8E);
    if (rd8(e(-0x3100)) & 1) wr16(e(-0x3600), (uint16_t)-rd16(e(-0x3600)));
    call(0x4362);
    if (!(c->ccr & CC_Z)) call(0xB44);
    else {
        call(0x3F54);
        c->d[2] = set_w(c->d[2], c->d[2] & 0xF000);
        if ((uint16_t)c->d[2] == 0x9000) {
            wr16(e(-0x35FE), (uint16_t)(rd16(e(-0x35FE)) - 0x600));
            c->d[2] = rd8(e(-0x34FD));
            c->d[2] = set_w(c->d[2], c->d[2] + rd16(e(-0x3700)));
            c->d[1] = 0xFFFFFFF0u;
            c->d[2] = set_w(c->d[2], c->d[2] & c->d[1]);
            c->d[1] = set_w(c->d[1], c->d[1] & rd16(e(-0x3800)));
            c->d[1] = set_w(c->d[1], c->d[1] + 8);
            c->d[0] = set_w(c->d[0], 0x200);
            call(0x10910);
        } else if (!(rd8(e(-0x2EFF)) & 2)) {
            c->d[0] = set_w(c->d[0], -rd16(e(-0x35FE)));
            c->d[0] = set_w(c->d[0], (uint16_t)((int16_t)c->d[0] >> 2));
            if ((int16_t)c->d[0] >= -0x80) c->d[0] = set_w(c->d[0], 0);
            wr16(e(-0x35FE), (uint16_t)c->d[0]);
        }
    }
    if (!thing_out()) { thing_tail(); return; }
    wr8(e(-0x4000), 0);
    c->d[0] = set_w(c->d[0], rd16(e(-0x2D00)));
    if (!(c->d[0] & 0x8000)) { c->a[0] = sx(c->d[0]); wr8(c->a[0] - 0x6072, 0); }
}
/* $12160: a step that sinks under the player to its floor -$27FE + 16 (the first time, game flag
 * -$31FE set and a sound), and comes back up to -$27FE (unless bits 2 and 0: it stays down) */
static void thing_step(void)
{
    if (ridden()) {
        c->d[0] = 0x10;
        c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x35FE)));
        c->d[1] = set_w(c->d[1], 0x200);
        if ((int16_t)c->d[0] > 0x200) c->d[0] = set_w(c->d[0], c->d[1]);
        wr16(e(-0x35FE), (uint16_t)c->d[0]);
        call(0xAB4);
        c->d[0] = 0x10;
        c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x27FE)));
        if ((uint16_t)c->d[0] > rd16(e(-0x3700))) goto tail;
        wr16(e(-0x3700), (uint16_t)c->d[0]);
        c->d[0] = 0;
        wr16(e(-0x35FE), 0);
        if (rd8(e(-0x3200)) & 1) goto tail;
        wr8(e(-0x3200), rd8(e(-0x3200)) | 1);
        c->d[0] = set_b(c->d[0], rd8(e(-0x31FE)));
        flag_at(); flag_set();
        thing_sound(0x2A);
        goto tail;
    }
    {
        uint8_t f = rd8(e(-0x3200));
        if ((f & 4) && (f & 1)) { wr16(e(-0x2DFE), rd16(e(-0x3700))); goto tail; }
    }
    c->d[0] = 0xFFFFFFE0u;
    c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x35FE)));
    c->d[1] = set_w(c->d[1], 0xFE00);
    if ((int16_t)c->d[0] < -0x200) c->d[0] = set_w(c->d[0], c->d[1]);
    wr16(e(-0x35FE), (uint16_t)c->d[0]);
    call(0xAB4);
    c->d[0] = set_w(c->d[0], rd16(e(-0x27FE)));
    if ((uint16_t)c->d[0] >= rd16(e(-0x3700))) {
        wr16(e(-0x3700), (uint16_t)c->d[0]);
        wr16(e(-0x35FE), 0);
        wr8(e(-0x3200), rd8(e(-0x3200)) & ~1);
        goto tail;
    }
    c->d[0] = set_w(c->d[0], c->d[0] + 0x10);
    if ((uint16_t)c->d[0] <= rd16(e(-0x3700))) wr16(e(-0x3700), (uint16_t)c->d[0]);
tail:
    thing_tail();
}
/* $1221C: a thing that shakes when game flag -$31FE is set ($70 frames, x by $12290), then
 * stays put (state 2) */
static void thing_shaker(void)
{
    wr16(e(-0x2DFE), rd16(e(-0x3700)));
    c->d[0] = set_b(c->d[0], rd8(e(-0x2BFF)));
    if (!(c->d[0] & 0xFF)) {
        c->d[0] = set_b(c->d[0], rd8(e(-0x31FE)));
        flag_at();
        if (!(rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1)) goto tail;
        thing_sound(0x32);
        inc8(-0x2BFF);
        wr8(e(-0x2800), 0x70);
        wr8(e(-0x27FF), 0);
        wr16(e(-0x35FE), 0xFF80);
        goto tail;
    }
    c->d[0] = set_b(c->d[0], c->d[0] - 1);
    if (c->d[0] & 0xFF) goto tail;
    call(0xAD6);
    {
        uint8_t t = (uint8_t)(rd8(e(-0x2800)) - 1);
        wr8(e(-0x2800), t);
        if (!t) { inc8(-0x2BFF); wr16(e(-0x3800), rd16(e(-0x23FE))); goto tail; }
    }
    c->d[1] = set_w(c->d[1], rd16(e(-0x23FE)));
    c->d[0] = rd8(e(-0x27FF));
    c->d[1] = set_w(c->d[1], c->d[1] + rd16(0x12290 + c->d[0]));
    wr16(e(-0x3800), (uint16_t)c->d[1]);
    c->d[0] = set_w(c->d[0], c->d[0] + 2);
    if ((uint16_t)c->d[0] >= 8) c->d[0] = set_w(c->d[0], 0);
    wr8(e(-0x27FF), (uint8_t)c->d[0]);
tail:
    thing_tail();
}
/* $12298: a spring: stood on, it is pressed down to -$2600 less 16 per -$31FE, then throws the
 * player up ($EC08) and comes back; the player's fall speed kept in -$2300 */
static void thing_spring(void)
{
    wr16(e(-0x2DFE), rd16(e(-0x3700)));
    c->d[0] = set_b(c->d[0], rd8(e(-0x2BFF)));
    if (!(c->d[0] & 0xFF)) {
        if (!ridden()) goto end;
        inc8(-0x2BFF);
        wr16(e(-0x35FE), rd16(e(-0x2300)));
        wr16(e(-0x33FE), 0xFFA0);
        wr16(e(-0x32FE), 0x800);
        wr8(e(-0x2F00), 2);
        goto end;
    }
    c->d[0] = set_b(c->d[0], c->d[0] - 1);
    if (c->d[0] & 0xFF) {
        call(0xAD6);
        c->d[0] = set_w(c->d[0], rd16(e(-0x3700)));
        if ((uint16_t)c->d[0] < rd16(e(-0x2600))) goto end;
        wr16(e(-0x35FE), 0);
        wr8(e(-0x2BFF), 0);
        goto end;
    }
    call(0xB44);
    call(0xAD6);
    c->d[1] = set_w(c->d[1], rd16(e(-0x2600)));
    c->d[0] = rd8(e(-0x31FE));
    c->d[0] = set_w(c->d[0], c->d[0] << 4);
    c->d[1] = set_w(c->d[1], c->d[1] - c->d[0]);
    if ((uint16_t)c->d[1] <= rd16(e(-0x3700))) goto end;
    inc8(-0x2BFF);
    c->d[0] = set_w(c->d[0], rd16(e(-0x35FE)));
    wr16(e(-0x35FE), 0x100);
    c->d[1] = set_w(c->d[1], rd16(e(-0x2D00)));
    if (c->d[1] & 0x8000) goto end;
    c->a[0] = sx(c->d[1]);
    if (!(rd8(c->a[0] - 0x6072) & 1)) goto end;
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->a[4]);
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    call(0xEC08);
    c->a[4] = sx(rd16(c->a[7])); c->a[7] += 2;
    thing_sound(0x33);
end:
    c->a[0] = sx(rd16(0xFFFF9EEEu));
    wr16(e(-0x2300), rd16(c->a[0] - 0x35FE));
    thing_tail();
}
/* $1234C: a weight: stood on it sinks, else it rises; held at most $18 below the thing -$2CFE
 * (reaching it sets game flag -$31FE, with a sound the first time); the gap kept in -$2400 */
static void thing_weight(void)
{
    if (ridden()) {
        if (rd16(e(-0x35FE)) & 0x8000) wr16(e(-0x35FE), 0);
        call(0xB44);
    } else wr16(e(-0x35FE), 0xFF00);
    call(0xAD6);
    c->a[1] = sx(rd16(e(-0x2CFE)));
    c->d[2] = set_w(c->d[2], rd16(e(-0x3700)));
    c->d[3] = set_w(c->d[3], rd16(c->a[1] - 0x3700));
    int above = (uint16_t)c->d[2] < (uint16_t)c->d[3];
    c->d[2] = set_w(c->d[2], c->d[2] - c->d[3]);
    if (above) { wr16(e(-0x3700), (uint16_t)c->d[3]); c->d[2] = 0; }
    else if ((uint16_t)c->d[2] >= 0x18) {
        c->d[0] = set_b(c->d[0], rd8(e(-0x31FE)));
        flag_at();
        if (!flag_set()) thing_sound(0x2A);
        c->d[2] = 0x18;
        c->d[3] = set_w(c->d[3], c->d[3] + c->d[2]);
        wr16(e(-0x3700), (uint16_t)c->d[3]);
    }
    wr16(e(-0x2400), (uint16_t)c->d[2]);
    call(0x11EEC);
    thing_tail();
}
/* $123BC: stood on, it is the thing $A0DE */
static void thing_marker(void)
{
    if (ridden()) wr16(0xFFFFA0DEu, (uint16_t)c->a[4]);
    thing_tail();
}
/* $123D4 (x) and $123DA (y): shuttles: while stood on, at 2 one way for -$31FE frames, then a
 * pause of 16 and back (-$31FD frames) */
static void thing_shuttle(int32_t off)
{
    c->a[1] = e(off);
    wr16(c->a[1], 0);
    c->d[0] = set_w(c->d[0], rd16(e(-0x2D00)));
    if (c->d[0] & 0x8000) goto move;
    c->a[0] = sx(c->d[0]);
    if (!(rd8(c->a[0] - 0x6072) & 1)) { wr8(e(-0x2800), 0); goto move; }
    if (rd8(e(-0x2800))) { wr8(e(-0x2800), (uint8_t)(rd8(e(-0x2800)) - 1)); goto move; }
    c->d[0] = set_w(c->d[0], 0x200);
    if (rd8(e(-0x3200)) & 0x80) c->d[0] = set_w(c->d[0], -c->d[0]);
    wr16(c->a[1], (uint16_t)c->d[0]);
    {
        uint8_t t = (uint8_t)(rd8(e(-0x31FE)) - 1);
        wr8(e(-0x31FE), t);
        if (t) goto move;
    }
    wr8(e(-0x2800), 0x10);
    wr8(e(-0x31FE), rd8(e(-0x31FD)));
    wr8(e(-0x3200), (uint8_t)~rd8(e(-0x3200)));
move:
    call(0xAB4);
    thing_tail();
}
static void thing_shuttle_x(void) { thing_shuttle(-0x3600); }
static void thing_shuttle_y(void) { thing_shuttle(-0x35FE); }
/* $1242E: a platform on a path (-$2A00: bytes of a step each, $FF its end): stood on, it goes to
 * its next point ($127A: at 4 a frame); when it gets there and nobody is on it, it waits and
 * goes back home ($C2E) */
static void thing_path(void)
{
    if (!rd8(e(-0x3200))) {
        if (rd8(e(-0x2800))) {
            uint8_t t = (uint8_t)(rd8(e(-0x2800)) - 1);
            wr8(e(-0x2800), t);
            if (t) { call(0xAB4); return; }
            wr16(e(-0x3800), rd16(e(-0x2100)));
            wr16(e(-0x3700), rd16(e(-0x20FE)));
            wr8(e(-0x3FFD), rd8(e(-0x3FFD)) & 0xBF);
            call(0xC2E);
            return;
        }
        if (ridden()) inc8(-0x3200);
        call(0xAB4);
        thing_tail();
        return;
    }
    c->d[0] = set_w(c->d[0], rd16(e(-0x2D00)));
    if (!(c->d[0] & 0x8000)) {
        c->a[0] = sx(c->d[0]);
        if (rd8(c->a[0] - 0x6072) & 1) { call(0xB44); call(0xAB4); thing_tail(); return; }
        wr8(c->a[0] - 0x6072, 0);
        wr8(e(-0x2D00), 0xFF);
    }
    wr8(e(-0x3200), 0);
    c->d[0] = rd32(e(-0x2A00));
    if (c->d[0]) {
        c->a[0] = c->d[0];
        c->d[1] = set_b(c->d[1], rd8(c->a[0])); c->a[0]++;
        if ((c->d[1] & 0xFF) == 0xFF) wr32(e(-0x2A00), 0);
        else {
            wr32(e(-0x2A00), c->a[0]);
            c->d[0] = 0xF & c->d[1];
            c->d[0] = set_w(c->d[0], c->d[0] - 8);
            c->d[0] = set_w(c->d[0], (uint16_t)(int8_t)c->d[0]);
            c->d[0] = set_w(c->d[0], c->d[0] << 4);
            wr16(e(-0x2100), (uint16_t)(rd16(e(-0x2100)) + c->d[0]));
            c->d[1] = set_w(c->d[1], c->d[1] & 0xF0);
            c->d[1] = set_w(c->d[1], c->d[1] - 0x80);
            wr16(e(-0x20FE), (uint16_t)(rd16(e(-0x20FE)) + c->d[1]));
        }
    }
    call(0xC22);
    c->d[0] = set_w(c->d[0], rd16(e(-0x2100)) - rd16(e(-0x3800)));
    c->d[1] = set_w(c->d[1], rd16(e(-0x20FE)) - rd16(e(-0x3700)));
    c->d[2] = set_w(c->d[2], 0x400);
    call(0x127A);
    wr16(e(-0x3600), (uint16_t)c->d[4]);
    wr16(e(-0x35FE), (uint16_t)c->d[5]);
    c->d[2] = set_w(c->d[2], (uint16_t)c->d[2] >> 2);
    if (!(c->d[2] & 0xFFFF)) c->d[2] = 1;
    wr8(e(-0x2800), (uint8_t)c->d[2]);
    wr8(e(-0x3FFD), rd8(e(-0x3FFD)) | 0x40);
    wr16(e(-0x3900), 1);
}

#define ENTRY_TAIL ENTRY
ENTRY_TAIL(ThingSwingX, thing_swing_x)     /* $11F80 */
ENTRY_TAIL(ThingSwingY, thing_swing_y)     /* $11F86 */
ENTRY_TAIL(ThingLiftUp, thing_lift_up)     /* $11FD2 */
ENTRY_TAIL(ThingBlock, thing_block)        /* $12058 */
void ThingOut(Cpu *cpu) { c = cpu; thing_out(); c->ret = 0; }   /* $1211E */
ENTRY_TAIL(ThingStep, thing_step)          /* $12160 */
ENTRY_TAIL(ThingShaker, thing_shaker)      /* $1221C */
ENTRY_TAIL(ThingSpring, thing_spring)      /* $12298 */
ENTRY_TAIL(ThingWeight, thing_weight)      /* $1234C */
ENTRY_TAIL(ThingMarker, thing_marker)      /* $123BC */
ENTRY_TAIL(ThingShuttleX, thing_shuttle_x) /* $123D4 */
ENTRY_TAIL(ThingShuttleY, thing_shuttle_y) /* $123DA */
ENTRY_TAIL(ThingPath, thing_path)          /* $1242E */

/* ---- batch T56: the enemies and spawn points cleared and set up with the room ---- */
/* $137A8: the tables at $A102 (12 words) and $A0E2's two slots cleared */
static void spawn_tables_clear(void)
{
    c->d[0] = 0;
    c->a[3] = 0xFFFFA102u;
    for (int k = 0; k < 12; k++) { wr16(c->a[3], 0); c->a[3] += 2; }
    c->a[3] = 0xFFFFA0E2u;
    for (int k = 0; k < 2; k++) { wr8(c->a[3], 0); c->a[3] += 0x10; }
    c->d[1] = 0xFFFF;
}
/* $136A6: leaving the room: each enemy (none past kind group 4) gone, its spawn point told: a
 * group-4 one of a kind-5 spawn point leaves it where it stood (its offset, facing, path, state
 * and link), the others count as one gone (-$5DB1); then each such count taken from the points'
 * totals; the tables cleared */
static void enemies_leave(void)
{
    c->a[0] = sx(rd16(0xFFFFA11Au));
    for (int k = 0; k < 16; k++, c->a[0] += 4) {
        if (!(rd8(c->a[0] - 0x4000) & 0x80)) continue;
        uint8_t g = rd8(c->a[0] - 0x2BFE);
        if (g < 4) {
            c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x2B00));
            if (!(c->d[0] & 0x8000)) { c->a[1] = sx(c->d[0]); wr8(c->a[1] - 0x5DB1, (uint8_t)(rd8(c->a[1] - 0x5DB1) + 1)); }
        } else if (g == 4) {
            c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x2B00));
            if (!(c->d[0] & 0x8000)) {
                c->a[1] = sx(c->d[0]);
                c->d[0] = 0x7F & rd8(c->a[1] - 0x5EA3);
                if ((c->d[0] & 0xFF) == 5) {
                    wr8(c->a[1] - 0x5EA3, (uint8_t)c->d[0]);
                    c->d[0] = set_w(c->d[0], rd16(c->a[0] - 0x3800) - rd16(c->a[0] - 0x2100));
                    wr8(c->a[1] - 0x5E54, (uint8_t)c->d[0]);
                    c->d[0] = (8 & rd8(c->a[0] - 0x3FFE)) | 0x80;
                    wr8(c->a[1] - 0x5E53, (uint8_t)c->d[0]);
                    wr32(c->a[1] - 0x5D14, rd32(c->a[0] - 0x2700));
                    wr8(c->a[1] - 0x5D61, rd8(c->a[0] - 0x2BFF));
                    wr16(c->a[1] - 0x5DB4, rd16(c->a[0] - 0x2D00));
                }
            }
        }
        wr8(c->a[0] - 0x4000, 0);
    }
    c->a[0] = 0;
    for (int k = 0; k < 16; k++, c->a[0] += 4) {
        if (!(rd8(c->a[0] - 0x5EA4) & 0x80)) continue;
        if (rd8(c->a[0] - 0x5DB4) & 0x80) continue;
        c->d[0] = set_b(c->d[0], rd8(c->a[0] - 0x5DB1));
        wr8(c->a[0] - 0x5DB3, (uint8_t)(rd8(c->a[0] - 0x5DB3) - (uint8_t)c->d[0]));
        wr8(c->a[0] - 0x5DB1, 0);
    }
    spawn_tables_clear();
}
/* $13748: the boss's four spawn points ($40 on) cleared, and its enemies (all but group 2 and
 * those of the room's first 16 points) gone */
static void boss_spawns_clear(void)
{
    c->a[0] = 0x40;
    c->d[0] = 0;
    for (int k = 0; k < 4; k++) { wr8(c->a[0] - 0x5EA4, 0); c->a[0] += 4; }
    c->d[1] = 0xFFFF;
    c->a[0] = sx(rd16(0xFFFFA11Au));
    for (int k = 0; k < 16; k++, c->a[0] += 4) {
        if (!(rd8(c->a[0] - 0x4000) & 0x80)) continue;
        if (rd8(c->a[0] - 0x2BFE) != 2) {
            c->d[2] = set_w(c->d[2], rd16(c->a[0] - 0x2B00));
            if (c->d[2] & 0x8000) continue;
            if ((uint16_t)c->d[2] < 0x40) continue;
        }
        wr8(c->a[0] - 0x4000, 0);
    }
}
/* $13786: every spawn point (20) and enemy (17) cleared */
static void spawns_all_clear(void)
{
    c->d[0] = 0;
    c->a[0] = 0;
    for (int k = 0; k < 20; k++) { wr16(c->a[0] - 0x5EA4, 0); c->a[0] += 4; }
    c->a[0] = sx(rd16(0xFFFFA11Au));
    for (int k = 0; k < 17; k++) { wr16(c->a[0] - 0x4000, 0); c->a[0] += 4; }
    c->d[1] = 0xFFFF;
}
/* $137C8: the room's enemies set up: tables and spawn points cleared, the enemies' slots from $40,
 * $A14C-$A15B and the kinds' list seeded with the frame counter, the spawn list read ($12848);
 * a kind's palette ($A146), if one came, to $9671 */
static void room_spawns(void)
{
    wr16(0xFFFFA11Cu, rd16(0xFFFF9EEEu));
    spawn_tables_clear();
    wr8(0xFFFFA14Au, (uint8_t)c->d[0]);
    c->a[3] = 0;
    for (int k = 0; k < 20; k++) { wr8(c->a[3] - 0x5EA4, (uint8_t)c->d[0]); c->a[3] += 4; }
    c->d[1] = set_w(c->d[1], 0xFFFF);
    c->a[4] = 0x40;
    wr16(0xFFFFA11Au, 0x40);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF965Au));
    c->d[0] = c->d[0] << 16 | c->d[0] >> 16;
    wr32(0xFFFFA14Cu, c->d[0]); wr32(0xFFFFA150u, c->d[0]); wr32(0xFFFFA154u, c->d[0]); wr32(0xFFFFA158u, c->d[0]);
    wr32(0xFFFFA142u, c->d[0]);
    wr8(0xFFFFA146u, 0xFF);
    spawn_list();
    c->d[0] = set_w(c->d[0], rd16(0xFFFFA146u));
    if (!(c->d[0] & 0x8000)) wr8(0xFFFF9671u, (uint8_t)c->d[0]);
}
ENTRY(EnemiesLeave, enemies_leave)         /* $136A6 */
ENTRY(BossSpawnsClear, boss_spawns_clear)  /* $13748 */
ENTRY(SpawnsAllClear, spawns_all_clear)    /* $13786 */
ENTRY(SpawnTablesClear, spawn_tables_clear)   /* $137A8 */
ENTRY(RoomSpawns, room_spawns)             /* $137C8 */

/* ---- batch T57: the room's things run, made and set up ---- */
/* $11BA4: the magic's eight slots ($C0 on) and $9F7A cleared */
static void magic_slots_clear(void)
{
    c->d[0] = 0;
    wr16(0xFFFF9F7Au, 0);
    c->a[0] = 0xC0;
    for (int k = 0; k < 8; k++) { wr16(c->a[0] - 0x4000, 0); c->a[0] += 4; }
    c->d[1] = 0xFFFF;
}
/* $11BBC: the riders' six entries ($9F8E) cleared */
static void riders_clear(void)
{
    c->d[0] = 0;
    c->a[0] = 0;
    for (int k = 0; k < 6; k++) { wr8(c->a[0] - 0x6072, 0); c->a[0] += 4; }
    c->d[1] = 0xFFFF;
}
/* $11BCE: a4 a free thing's slot ($10-$3C); mi if none */
static void find_free_thing(void)
{
    c->a[4] = 0x10;
    c->d[0] = 0xB;
    for (;;) {
        uint8_t v = rd8(c->a[4] - 0x4000);
        F_B(v);
        if (!(v & 0x80)) return;
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
        if ((c->d[0] & 0xFFFF) == 0xFFFF) return;
        c->a[4] += 4;
    }
}
/* $11BE2: where the thing was, for its riders */
static void thing_was(void) { wr16(e(-0x2E00), rd16(e(-0x3800))); wr16(e(-0x2DFE), rd16(e(-0x3700))); }
/* $11EEC: a weight's look and height by its gap (-$2400) */
static void weight_look(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x2400)));
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[1] = set_w(c->d[1], (uint16_t)c->d[1] >> 1);
    c->d[1] = set_w(c->d[1], c->d[1] + 0xC);
    wr8(e(-0x34FD), (uint8_t)c->d[1]);
    c->d[0] = set_w(c->d[0], c->d[0] + 7);
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 3);
    c->d[0] = set_w(c->d[0], c->d[0] * 2);
    c->d[0] = set_b(c->d[0], c->d[0] + 2);
    if ((uint8_t)c->d[0] != rd8(e(-0x3CFE))) call(0x7E8);
}
/* $11DB4: a swing's speed (2, by -$31FE bit 7), its run (-$31FE * 8) and coasting ((* 4 + 4) & $F8);
 * d1 the offset to start from (the speed * $80 / $10000) */
static void swing_setup(void)
{
    c->d[1] = set_b(c->d[1], rd8(e(-0x31FE)));
    c->d[2] = set_b(c->d[2], c->d[1]);
    c->d[0] = set_w(c->d[0], 0x200);
    c->d[2] = set_b(c->d[2], c->d[2] & 0x80);
    if (c->d[2] & 0xFF) c->d[0] = set_w(c->d[0], -c->d[0]);
    wr8(e(-0x3200), (uint8_t)c->d[2]);
    c->d[1] = set_w(c->d[1], c->d[1] << 2);
    c->d[2] = set_b(c->d[2], c->d[1]);
    c->d[1] = set_b(c->d[1], c->d[1] * 2);
    wr8(e(-0x31FE), (uint8_t)c->d[1]);
    c->d[2] = set_w(c->d[2], c->d[2] + 4);
    c->d[2] = set_w(c->d[2], c->d[2] & 0xF8);
    wr8(e(-0x31FD), (uint8_t)c->d[2]);
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[1] = (uint32_t)(int32_t)(int16_t)c->d[1];
    c->d[1] <<= 7;
    c->d[1] = c->d[1] << 16 | c->d[1] >> 16;
}
/* the kinds' set-ups ($11C82) */
static void thing_setup(uint32_t at)
{
    switch (at) {
    case 0x11D9C:                                   /* 0, 1: swings */
        swing_setup();
        wr16(e(-0x3600), (uint16_t)c->d[0]);
        wr16(e(-0x3800), (uint16_t)(rd16(e(-0x3800)) + c->d[1]));
        return;
    case 0x11DA8:
        swing_setup();
        wr16(e(-0x35FE), (uint16_t)c->d[0]);
        wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) + c->d[1]));
        return;
    case 0x11DEA:                                   /* 2, 4 (the steps): its floor */
        wr16(e(-0x27FE), rd16(e(-0x3700)));
        return;
    case 0x11DF2:                                   /* 3 (the lift): its run and speed */
        c->d[0] = 0;
        wr8(e(-0x3200), 0);
        c->d[0] = set_b(c->d[0], rd8(e(-0x31FE)));
        c->d[1] = set_w(c->d[1], 0x200);
        {
            int up = c->d[0] & 0x80;
            c->d[0] = set_b(c->d[0], c->d[0] * 2);
            if (up) c->d[1] = set_w(c->d[1], -c->d[1]);
        }
        c->d[0] = set_w(c->d[0], c->d[0] << 2);
        wr16(e(-0x31FE), (uint16_t)c->d[0]);
        wr16(e(-0x27FE), (uint16_t)c->d[1]);
        return;
    case 0x11E12:                                   /* 4: down already once game flag 2 is set */
        wr8(e(-0x3200), 4);
        wr16(e(-0x27FE), rd16(e(-0x3700)));
        c->d[0] = 2;
        call(0x274C);
        if (!(c->ccr & CC_Z)) { wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) + 0x10)); inc8(-0x3200); }
        return;
    case 0x11E32:                                   /* 5 (the block) */
        wr8(e(-0x3200), 0);
        wr8(e(-0x2F00), 0x3E);
        wr16(e(-0x33FE), 0x40);
        wr16(e(-0x32FE), 0x800);
        return;
    case 0x11E4A:                                   /* 6 (the shaker): moved already when its flag is set */
        c->d[0] = set_b(c->d[0], rd8(e(-0x31FE)));
        flag_at();
        if (rd8(c->a[0] + sx(c->d[1])) >> (c->d[0] & 7) & 1) {
            wr8(e(-0x2BFF), 2);
            wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) - 0x38));
        }
        /* fall through */
    case 0x11E64:                                   /* 7 (the spring): where it rests */
        wr16(e(-0x23FE), rd16(e(-0x3800)));
        wr16(e(-0x2600), rd16(e(-0x3700)));
        wr16(e(-0x2300), 0);
        return;
    case 0x11E76:                                   /* 8 (the weight): its top, another thing, made too */
        c->a[3] = sx(c->a[4]);
        find_free_thing();
        if (c->ccr & CC_N) {
            uint32_t t = c->a[3]; c->a[3] = c->a[4]; c->a[4] = t;
            wr8(e(-0x4000), 0);
            return;
        }
        call(0xA36);
        wr16(e(-0x2CFE), (uint16_t)c->a[3]);
        wr8(e(-0x31FF), 0x12);
        wr16(e(-0x2BFE), 0x22);
        wr16(e(-0x3800), rd16(c->a[3] - 0x3800));
        wr16(e(-0x3700), rd16(c->a[3] - 0x3700));
        thing_was();
        wr8(e(-0x31FE), rd8(c->a[3] - 0x31FE));
        wr8(e(-0x2F00), 2);
        wr16(e(-0x33FE), 0x20);
        wr16(e(-0x32FE), 0x400);
        wr16(e(-0x2400), 0);
        wr8(e(-0x3CFE), 0xFF);
        wr8(e(-0x2D00), 0xFF);
        wr8(c->a[3] - 0x3CFD, 0x1A);
        wr8(e(-0x3CFD), 0x1C);
        wr16(e(-0x3D00), rd16(c->a[3] - 0x3D00));
        weight_look();
        wr8(e(-0x4000), 0xC0);
        { uint32_t t = c->a[3]; c->a[3] = c->a[4]; c->a[4] = t; }
        return;
    case 0x11CE6: return;                           /* 9, 10 */
    case 0x11F10:                                   /* 11, 12 (shuttles): the way (bit 7) and run */
        c->d[0] = set_b(c->d[0], rd8(e(-0x31FE)));
        {
            int back = c->d[0] & 0x80;
            c->d[0] = set_b(c->d[0], c->d[0] & 0x7F);
            wr8(e(-0x3200), back ? 0xFF : 0);
        }
        c->d[0] = set_b(c->d[0], c->d[0] << 3);
        wr8(e(-0x31FE), (uint8_t)c->d[0]);
        wr8(e(-0x31FD), (uint8_t)c->d[0]);
        return;
    case 0x11F28:                                   /* 13: its path ($11F48) */
        c->d[0] = rd8(e(-0x31FE));
        wr32(e(-0x2A00), rd32(0x11F48 + c->d[0]));
        wr16(e(-0x32FE), 0x600);
        wr8(e(-0x2F00), 3);
        wr16(e(-0x33FE), 4);
        return;
    default: call(at); return;
    }
}
/* $11BF0: a thing of kind d4 (low byte; the high its setting -$31FE) and size d5 ($1250A) at
 * (d6, d7), in a free slot (none: mi): its look (palette $A0E0), placed by its size's offset, then
 * set up by kind */
static void thing_make(void)
{
    find_free_thing();
    if (c->ccr & CC_N) return;
    wr16(e(-0x4000), 0x8000);
    call(0xA36);
    wr16(e(-0x3800), (uint16_t)c->d[6]);
    wr16(e(-0x3700), (uint16_t)c->d[7]);
    c->d[0] = set_w(c->d[0], 0x600);
    c->d[0] = set_b(c->d[0], rd8(0xFFFFA0E0u));
    wr16(e(-0x3D00), (uint16_t)c->d[0]);
    wr8(e(-0x3CFD), 0x14);
    wr8(e(-0x2D00), 0xFF);
    wr8(e(-0x31FF), (uint8_t)c->d[4]);
    c->d[4] = set_w(c->d[4], (uint16_t)c->d[4] >> 8);
    wr8(e(-0x31FE), (uint8_t)c->d[4]);
    c->a[0] = 0x1250A;
    c->d[4] = set_b(c->d[4], c->d[5]);
    wr16(e(-0x2BFE), (uint16_t)c->d[4]);
    c->d[5] = set_w(c->d[5], c->d[4]);
    c->d[5] = set_w(c->d[5], c->d[5] * 2);
    c->d[4] = set_w(c->d[4], c->d[4] + c->d[5]);
    c->a[0] += sx(c->d[4]);
    c->d[0] = set_b(c->d[0], rd8(c->a[0]));
    call(0x7FC);
    wr8(e(-0x4000), rd8(e(-0x4000)) | 0x40);
    c->d[0] = 0;
    wr8(e(-0x2BFF), 0);
    c->d[0] = set_b(c->d[0], rd8(e(-0x34FE)));
    c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x3800)));
    wr16(e(-0x3800), (uint16_t)c->d[0]); wr16(e(-0x2100), (uint16_t)c->d[0]);
    c->d[0] = 0;
    c->d[0] = set_b(c->d[0], rd8(e(-0x34FD)));
    c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x3700)));
    wr16(e(-0x3700), (uint16_t)c->d[0]); wr16(e(-0x20FE), (uint16_t)c->d[0]);
    thing_was();
    c->d[0] = rd8(e(-0x31FF));
    c->a[0] = 0x11C82 + sx(rd16(0x11C82 + c->d[0]));
    thing_setup(c->a[0]);
}
/* $11C9E: each thing in use run by kind ($11CCA); $A0DE (the thing stood on) cleared first */
static void things_run(void)
{
    wr8(0xFFFFA0DEu, 0xFF);
    c->a[4] = 0x10;
    c->d[7] = 0xB;
    for (;;) {
        if (rd8(e(-0x4000)) & 0x80) {
            c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->d[7]);
            c->d[0] = rd8(e(-0x31FF));
            c->a[0] = 0x11CCA + sx(rd16(0x11CCA + c->d[0]));
            if (c->a[0] != 0x11CE6) call(c->a[0]);      /* kinds 9 and 10: an rts */
            c->d[7] = set_w(c->d[7], rd16(c->a[7])); c->a[7] += 2;
        }
        c->a[4] += 4;
        c->d[7] = set_w(c->d[7], c->d[7] - 1);
        if ((c->d[7] & 0xFFFF) == 0xFFFF) break;
    }
}
/* $11CE8: a1 a free rider's entry ($9F8E, six); mi if none */
static void find_free_rider(void)
{
    c->a[1] = 0;
    c->d[1] = 5;
    for (;;) {
        uint8_t v = rd8(c->a[1] - 0x6072);
        F_B(v);
        if (!(v & 0x80)) return;
        c->d[1] = set_w(c->d[1], c->d[1] - 1);
        if ((c->d[1] & 0xFFFF) == 0xFFFF) return;
        c->a[1] += 4;
    }
}
/* $11CFC: the thing's riders' entry: taken when something is on it ($BEE), let go when not; kept
 * up to date: the thing, its box by size ($1250B), its place and how far it moved */
static void thing_riders(void)
{
    c->d[0] = set_w(c->d[0], rd16(e(-0x2D00)));
    if (c->d[0] & 0x8000) {
        call(0xBEE);
        if (!(c->ccr & CC_Z)) return;
        find_free_rider();
        if (c->ccr & CC_N) return;
        wr16(e(-0x2D00), (uint16_t)c->a[1]);
    } else {
        c->a[1] = sx(c->d[0]);
        c->d[0] = 0xF & rd8(c->a[1] - 0x6072);
        if (!(c->d[0] & 0xFF)) {
            call(0xBEE);
            if (!(c->ccr & CC_Z)) { wr8(c->a[1] - 0x6072, 0); wr8(e(-0x2D00), 0xFF); return; }
        }
    }
    wr16(c->a[1] - 0x6070, (uint16_t)c->a[4]);
    c->d[0] = set_w(c->d[0], rd16(e(-0x2BFE)));
    c->d[0] = set_w(c->d[0], c->d[0] * 2);
    c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x2BFE)));
    c->a[0] = 0x1250B + sx(c->d[0]);
    wr8(c->a[1] - 0x6071, rd8(c->a[0])); c->a[0]++;
    c->d[2] = set_b(c->d[2], rd8(c->a[0])); c->a[0]++;
    c->d[2] = set_w(c->d[2], (uint16_t)(int8_t)c->d[2]);
    c->d[0] = set_w(c->d[0], rd16(e(-0x3800)));
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[1] - rd16(e(-0x2E00)));
    wr16(c->a[1] - 0x5FCA, (uint16_t)c->d[1]);
    c->d[0] = set_w(c->d[0], c->d[0] + c->d[2]);
    wr16(c->a[1] - 0x601E, (uint16_t)c->d[0]);
    c->d[2] = set_b(c->d[2], rd8(c->a[0])); c->a[0]++;
    c->d[2] = set_w(c->d[2], (uint16_t)(int8_t)c->d[2]);
    c->d[0] = set_w(c->d[0], rd16(e(-0x3700)));
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[1] - rd16(e(-0x2DFE)));
    wr16(c->a[1] - 0x5FC8, (uint16_t)c->d[1]);
    c->d[0] = set_w(c->d[0], c->d[0] + c->d[2]);
    wr16(c->a[1] - 0x601C, (uint16_t)c->d[0]);
    c->d[0] = rd8(c->a[0]); c->a[0]++;
    wr16(c->a[1] - 0x5F76, (uint16_t)c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
    if (rd8(c->a[1] - 0x6071) == 5) { wr8(c->a[1] - 0x6071, 3); c->d[0] = set_b(c->d[0], rd8(e(-0x34FD))); }
    wr16(c->a[1] - 0x5F74, (uint16_t)c->d[0]);
    wr8(c->a[1] - 0x6072, 0x80);
}
ENTRY(MagicSlotsClear, magic_slots_clear)  /* $11BA4 */
ENTRY(RidersClear, riders_clear)           /* $11BBC */
ENTRY(FindFreeThing, find_free_thing)      /* $11BCE */
ENTRY(ThingWas, thing_was)                 /* $11BE2 */
ENTRY(ThingMake, thing_make)               /* $11BF0 */
ENTRY(ThingsRun, things_run)               /* $11C9E */
ENTRY(FindFreeRider, find_free_rider)      /* $11CE8 */
ENTRY(ThingRiders, thing_riders)           /* $11CFC */
ENTRY(SwingSetup, swing_setup)             /* $11DB4 */
ENTRY(WeightLook, weight_look)             /* $11EEC */
/* the kinds' set-ups, each entered by the jump at $11C80 */
#define SETUP(name, at) void name(Cpu *cpu) { c = cpu; thing_setup(at); c->ret = 0; }
SETUP(ThingSetSwingX, 0x11D9C)
SETUP(ThingSetSwingY, 0x11DA8)
SETUP(ThingSetFloor, 0x11DEA)
SETUP(ThingSetLift, 0x11DF2)
SETUP(ThingSetStep, 0x11E12)
SETUP(ThingSetBlock, 0x11E32)
SETUP(ThingSetShaker, 0x11E4A)
SETUP(ThingSetSpring, 0x11E64)
SETUP(ThingSetWeight, 0x11E76)
SETUP(ThingSetShuttle, 0x11F10)
SETUP(ThingSetPath, 0x11F28)

/* ---- batch T59: the companion's route and small helpers ($1061E-$1115E) ---- */
/* $107E2: d0 the route table's byte ($418C) for the tile under (d6, d7) ($30DA) and side d1 */
static void route_byte(void)
{
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->d[1]);
    call(0x30DA);
    c->d[1] = set_w(c->d[1], rd16(c->a[7])); c->a[7] += 2;
    c->d[2] = set_w(c->d[2], c->d[2] & 0xF0);
    c->d[2] = set_w(c->d[2], (uint16_t)c->d[2] >> 3);
    c->a[0] = 0x418C;
    c->d[2] = set_w(c->d[2], c->d[2] + c->d[1]);
    c->d[0] = set_b(c->d[0], rd8(c->a[0] + sx(c->d[2])));
}
/* $1070A: the trail's first point dropped ($9F34, $9F74 its length in bytes); mi if it was the
 * last */
static void trail_shift(void)
{
    c->a[0] = c->a[1];
    c->d[0] = set_w(c->d[0], rd16(0xFFFF9F74u));
    c->d[0] = set_w(c->d[0], c->d[0] - 4);
    if (!(c->d[0] & 0xFFFF)) { c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]); return; }
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 2);
    do {
        wr32(c->a[0], rd32(c->a[0] + 4)); c->a[0] += 4;
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
    } while (c->d[0] & 0xFFFF);
    F_W(0);
}
/* $10726: from (d6, d7), the probes of route d0 ($107A2: a count, then (dx, dy, wanted) each):
 * eq when one finds its wanted byte in table d1 ($418C), mi when none does or a wall (tile $F) */
static void route_probe(void)
{
    uint16_t x = (uint16_t)c->d[6], y = (uint16_t)c->d[7], tab = (uint16_t)c->d[1];
    uint8_t want;
    c->d[0] = set_w(c->d[0], c->d[0] & 0xFF);
    c->d[0] = set_w(c->d[0], c->d[0] * 2);
    c->a[0] = 0x107A2 + sx(rd16(0x107A2 + (c->d[0] & 0xFFFF)));
    int8_t n = (int8_t)rd8(c->a[0]); c->a[0]++;
    uint32_t at = c->a[0];
    for (;;) {
        c->a[0] = at;
        c->d[6] = set_w(c->d[6], (uint16_t)(int8_t)rd8(c->a[0]) + x); c->a[0]++;
        c->d[7] = set_w(c->d[7], (uint16_t)(int8_t)rd8(c->a[0]) + y); c->a[0]++;
        want = rd8(c->a[0]); c->a[0]++;
        at = c->a[0];
        call(0x30DA);
        if (c->d[2] & 8) {
            c->d[0] = set_w(c->d[0], c->d[2] & 0xF0);
            c->d[0] = set_w(c->d[0], (uint16_t)c->d[0] >> 3);
            c->d[0] = set_w(c->d[0], c->d[0] + 0x418C);
            c->a[0] = sx(tab);
            c->d[0] = set_b(c->d[0], rd8(c->a[0] + sx(c->d[0])));
            if ((uint8_t)c->d[0] == want) { F_B(0); return; }
        }
        n--;
        if (n < 0) { c->ccr = (uint16_t)(CC_N | CC_C | CC_X); return; }
        c->d[2] = set_b(c->d[2], c->d[2] & 0xF);
        if ((c->d[2] & 0xFF) == 0xF) break;
    }
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
}
/* $1061E: the companion's next state on its trail: -1 off the ground; $A on the lift ($9A8A:
 * facing and moving off it); else following the trail's points ($9F34; reached ones dropped,
 * 2 when none is left): 4 when the way to the next is clear, 6 (on the ground) or 2 when not */
static void companion_route(void)
{
    c->d[0] = set_b(c->d[0], rd8(e(-0x3100)) & 0xA);
    c->d[0] = set_b(c->d[0], c->d[0] - 2);
    if (c->d[0] & 0xFF) { c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]); return; }
    c->d[6] = set_w(c->d[6], rd16(e(-0x3800)));
    c->d[7] = rd8(e(-0x34FD));
    c->d[7] = set_w(c->d[7], c->d[7] + rd16(e(-0x3700)));
    if (rd16(e(-0x2BFE)) == 2 && rd8(0xFFFF9A8Au) && (uint16_t)c->d[7] == rd16(0xFFFF9A8Eu)
        && (uint16_t)c->d[6] >= rd16(0xFFFF9A90u) && (uint16_t)c->d[6] <= rd16(0xFFFF9A92u)) {
        c->d[0] = 0;
        c->d[1] = set_w(c->d[1], rd16(0xFFFF9EF6u) - 0x40);
        if ((uint16_t)c->d[6] >= rd16(0xFFFF9A8Cu)) { c->d[0] = 8; c->d[1] = set_w(c->d[1], -c->d[1]); }
        wr8(e(-0x3FFE), (uint8_t)((rd8(e(-0x3FFE)) & 0xF7) | (uint8_t)c->d[0]));
        wr16(e(-0x3600), (uint16_t)c->d[1]);
        c->d[0] = 0xA; F_L(c->d[0]);
        return;
    }
    c->d[6] = set_w(c->d[6], (uint16_t)c->d[6] >> 4);
    c->d[7] = set_w(c->d[7], c->d[7] & 0xFFF0);
    c->d[7] = set_w(c->d[7], c->d[7] << 2);
    c->d[5] = set_w(c->d[5], c->d[6]);
    c->d[5] = c->d[5] << 16 | c->d[5] >> 16;
    c->d[5] = set_w(c->d[5], c->d[7]);
    c->a[3] = 0xFFFF9754u;
    c->a[1] = 0xFFFF9F34u;
    while (rd32(c->a[1]) == c->d[5]) {
        trail_shift();
        if (c->ccr & CC_N) { c->d[0] = 2; F_L(2); return; }
        wr16(0xFFFF9F74u, (uint16_t)c->d[1]);
    }
    c->d[0] = 0;
    if ((uint16_t)c->d[6] != rd16(c->a[1])) {
        if ((uint16_t)c->d[6] >= rd16(c->a[1])) c->d[0] = 8;
        wr8(e(-0x3FFE), (uint8_t)((rd8(e(-0x3FFE)) & 0xF7) | (uint8_t)c->d[0]));
    }
    c->d[0] = set_w(c->d[0], c->d[7] - rd16(c->a[1] + 2));
    if (c->d[0] & 0x8000) c->d[0] = set_w(c->d[0], -c->d[0]);
    if (((uint16_t)c->d[6] == rd16(c->a[1]) && (int16_t)c->d[0] >= 0x40) || (int16_t)c->d[0] >= 0x80) goto blocked;
    c->d[1] = (rd8(e(-0x3FFE)) & 8) ? 0 : 1;
    route_byte();
    c->d[1] = set_w(c->d[1], c->d[1] ^ 1);
    route_probe();
    if (c->ccr & CC_N) goto blocked;
    c->d[0] = 4; F_L(4);
    return;
blocked:
    c->d[0] = 0xA & rd8(e(-0x3100));
    c->d[0] = set_b(c->d[0], c->d[0] - 2);
    if (c->d[0] & 0xFF) { c->d[0] = 2; F_L(2); return; }
    c->d[0] = 6; F_L(6);
}
/* $108EE: ls when the y distance (d2, from the entity) is at most d3 */
static void near_y(void)
{
    c->d[2] = set_w(c->d[2], c->d[2] - rd16(e(-0x3700)));
    if (c->d[2] & 0x8000) c->d[2] = set_w(c->d[2], -c->d[2]);
    c->d[2] = set_w(c->d[2], sub_w(&c->ccr, c->d[3], c->d[2]));
}
/* $108E2: the same for x (d0, d1) */
static void near_x(void)
{
    c->d[0] = set_w(c->d[0], c->d[0] - rd16(e(-0x3800)));
    if (c->d[0] & 0x8000) c->d[0] = set_w(c->d[0], -c->d[0]);
    c->d[0] = set_w(c->d[0], sub_w(&c->ccr, c->d[1], c->d[0]));
}
/* $108DC: ls when (d0, d2) is within (d1, d3) of the entity */
static void near_box(void)
{
    near_x();
    if (!(c->ccr & (CC_C | CC_Z))) return;
    near_y();
}
/* $108FA and $1115E: the 16 slots from $80 cleared */
static void slots80_clear(void)
{
    c->a[0] = 0x80;
    c->d[0] = 0xF;
    for (int k = 0; k < 16; k++) { wr16(c->a[0] - 0x4000, 0); c->a[0] += 4; }
    c->d[0] = 0xFFFF;
}
ENTRY(CompanionRoute, companion_route)     /* $1061E */
ENTRY(TrailShift, trail_shift)             /* $1070A */
ENTRY(RouteProbe, route_probe)             /* $10726 */
ENTRY(RouteByte, route_byte)               /* $107E2 */
ENTRY(BoxNear, near_box)                   /* $108DC */
ENTRY(BoxNearX, near_x)                       /* $108E2 */
ENTRY(BoxNearY, near_y)                       /* $108EE */
ENTRY(Slots80Clear, slots80_clear)         /* $108FA */
ENTRY(Slots80Clear2, slots80_clear)        /* $1115E */

/* ---- batch T60: the enemies of area 6, room 5 ($16908) ----
 * The monsters' handler there (port/handlers.c hands it over): by state (-$2BFF, table $16918),
 * each enemy walks to its place on its spawn point's row ($16A20), drops or jumps at the player,
 * and is told by its spawn point (-$2B00: bit 6 its turn by -$27FE, bit 5 all of them away) when
 * to go. */
static int begun(void)     /* bset #7,-$2BFF: 1 the first time */
{
    uint8_t s = rd8(e(-0x2BFF));
    wr8(e(-0x2BFF), s | 0x80);
    return !(s & 0x80);
}
static void anim(uint32_t n) { c->d[0] = n; call(0x7E8); }
static int zero_after(uint32_t addr) { call(addr); return (c->ccr & CC_Z) != 0; }
static uint32_t point(void) { c->a[3] = sx(rd16(e(-0x2B00))); return c->a[3]; }
/* is it this enemy's turn ($16D00 and the like): bit 6 and -$27FE the point's -$5D14 */
static int my_turn(void)
{
    if (!(rd8(c->a[3] - 0x5EA4) & 0x40)) return 0;
    c->d[0] = rd8(e(-0x27FE));
    return (uint16_t)c->d[0] == rd16(c->a[3] - 0x5D14);
}
/* $16D00: its turn taken */
static void turn_taken(void)
{
    point();
    if (my_turn()) wr8(c->a[3] - 0x5EA4, rd8(c->a[3] - 0x5EA4) & 0xBF);
}
/* $16BBC: all away (bit 5): state $E, and the state returns (1) */
static int all_away(void)
{
    point();
    if (!(rd8(c->a[3] - 0x5EA4) & 0x20)) return 0;
    wr8(e(-0x2BFF), 0xE);
    return 1;
}
/* $16D20: hit: state $10, or $C when it is beaten ($4986) */
static void a605_hit(void)
{
    wr8(e(-0x2BFF), 0x10);
    if (zero_after(0x4986)) wr8(e(-0x2BFF), 0xC);
}
/* $16BB2: touched ($439E mi): as $16D20 */
static void a605_touched(void) { call(0x439E); if (c->ccr & CC_N) a605_hit(); }
static void a605_states(uint32_t at)
{
    switch (at) {
    case 0x1692A:                                   /* 0: set up, facing, on its feet */
        c->d[0] = (rd8(e(-0x3FFE)) & 8) ? 0x1E : 0xC;
        wr8(e(-0x3CFD), (uint8_t)c->d[0]);
        anim(0);
        call(0x80E);
        c->d[0] = rd8(e(-0x34FD));
        wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) - c->d[0]));
        wr8(e(-0x4000), rd8(e(-0x4000)) | 0x40);
        wr8(e(-0x2F00), 0x43);
        wr8(e(-0x2BFF), 2);
        return;
    case 0x16962:                                   /* 2: pacing $28 about its point till its turn */
        if (begun()) anim(8);
        point();
        if (my_turn()) {
            wr8(c->a[3] - 0x5EA4, rd8(c->a[3] - 0x5EA4) & 0xBF);
            wr8(e(-0x2BFF), 4);
            return;
        }
        c->d[0] = set_w(c->d[0], 0x80);
        c->a[0] = sx(rd16(e(-0x2B00)));
        c->d[2] = set_w(c->d[2], rd16(c->a[0] - 0x5E04));
        if (rd8(e(-0x3FFE)) & 8) {
            c->d[0] = set_w(c->d[0], -c->d[0]);
            c->d[2] = set_w(c->d[2], c->d[2] - 0x28);
            if ((uint16_t)c->d[2] < rd16(e(-0x3800))) goto pace;
            c->d[1] = 0xC;
        } else {
            c->d[2] = set_w(c->d[2], c->d[2] + 0x28);
            if ((uint16_t)c->d[2] > rd16(e(-0x3800))) goto pace;
            c->d[1] = 0x1E;
        }
        c->d[0] = set_w(c->d[0], -c->d[0]);
        wr8(e(-0x3CFD), (uint8_t)c->d[1]);
        wr8(e(-0x3FFE), rd8(e(-0x3FFE)) ^ 8);
    pace:
        wr16(e(-0x3600), (uint16_t)c->d[0]);
        call(0xAB8);
        return;
    case 0x169DC:                                   /* 4: a jump to its place in the row */
        wr8(e(-0x3CFD), 0xC);
        wr8(e(-0x2BFF), 6);
        c->d[0] = rd8(e(-0x27FE));
        c->d[0] = set_b(c->d[0], rd8(0x16A20 + (c->d[0] & 0xFFFF)));
        c->d[0] = set_w(c->d[0], (uint16_t)(int8_t)c->d[0]);
        c->d[0] = set_w(c->d[0], c->d[0] + rd16(e(-0x2100)));
        c->d[0] = set_w(c->d[0], c->d[0] - rd16(e(-0x3800)));
        c->d[1] = 0;
        call(0x12B6);
        wr16(e(-0x3600), (uint16_t)c->d[2]);
        wr16(e(-0x35FE), (uint16_t)c->d[3]);
        wr8(e(-0x3100), rd8(e(-0x3100)) & 0x75);
        anim(0xA);
        call(0xB44);
        call(0xAB4);
        return;
    case 0x16A26:                                   /* 6: landing in its place, then waiting there */
        if (!(rd8(e(-0x2BFF)) & 0x80)) {
            call(0xB44);
            if (rd16(e(-0x35FE)) & 0x8000) { call(0xAB4); return; }
            call(0x2FD0);
            call(0x44D6);
            if (!zero_after(0x4362)) return;
            point();
            c->d[0] = set_b(c->d[0], rd8(e(-0x27FE)));
            wr8(c->a[3] - 0x5D64, rd8(c->a[3] - 0x5D64) | (uint8_t)(1u << (c->d[0] & 7)));
            wr8(e(-0x2800), 8);
            wr8(e(-0x2BFF), rd8(e(-0x2BFF)) | 0x80);
            c->d[0] = set_b(c->d[0], 6);
            call(0x7E8);
            return;
        }
        point();
        if (rd8(c->a[3] - 0x5EA4) & 0x20) { wr8(e(-0x2BFF), 0xE); return; }
        if (my_turn()) {
            wr8(c->a[3] - 0x5D64, rd8(c->a[3] - 0x5D64) & (uint8_t)~(1u << (c->d[0] & 7)));
            wr8(c->a[3] - 0x5EA4, rd8(c->a[3] - 0x5EA4) & 0xBF);
            wr8(e(-0x2BFF), 8);
            return;
        }
        call(0x46D6);
        if (!zero_after(0x4362)) return;
        wr16(e(-0x3600), 0);
        if (rd8(e(-0x2800))) { wr8(e(-0x2800), (uint8_t)(rd8(e(-0x2800)) - 1)); return; }
        call(0x5D8);
        c->d[0] = set_b(c->d[0], c->d[0] & 0xF);
        if (c->d[0] & 0xFF) return;
        wr8(e(-0x2800), 8);
        c->d[0] = set_w(c->d[0], 0xFD00);
        call(0x4508);                               /* a hop */
        return;
    case 0x16AD8:                                   /* 8: after the player, jumping at it when near */
        if (begun()) {
            wr16(e(-0x3300), 0x180);
            call(0x44D6);
            call(0x43CC);
            if (zero_after(0x4362)) {
                call(0x455E);
                if ((uint16_t)c->d[1] >= 0x50) goto walk;
                c->d[2] = set_w(c->d[2], c->d[1]);
                call(0x5D8);
                c->d[0] = set_w(c->d[0], c->d[0] & 3);
                if (c->d[0] & 0xFFFF) goto walk;
                c->d[0] = set_w(c->d[0], 0xFC00);
                call(0x4508);
                c->d[0] = set_w(c->d[0], c->d[2]);
                c->d[0] = set_w(c->d[0], c->d[0] << 3);
                call(0x4496);
            }
            c->d[0] = 0xA;
            goto look;
        walk:
            call(0x4492);
            c->d[0] = 8;
        look:
            call(0x7E8);
        }
        turn_taken();
        if (all_away()) return;
        call(0x42C4);
        a605_touched();
        if (rd8(e(-0x2AFD)) & 4) { wr8(e(-0x2BFF), 0xA); return; }
        if (!zero_after(0x4362)) return;
        c->d[0] = 8;
        if (rd8(e(-0x3CFE)) != 8) call(0x7E8);
        c->d[0] = (uint8_t)((rd8(e(-0x3100)) & 0x41) - 1);   /* $4428: at a wall, turned, and the state returns */
        if (!c->d[0]) { call(0x448C); return; }
        if (rd8(e(-0x2800))) {
            wr8(e(-0x2800), (uint8_t)(rd8(e(-0x2800)) - 1));
            if (rd8(e(-0x2800))) return;
        }
        call(0x44D6);
        call(0x4492);
        call(0x43CC);
        return;
    case 0x16B78:                                   /* $A: knocked back, then after the player again */
        if (begun()) {
            c->d[0] = set_w(c->d[0], 0xFC00);
            call(0x4508);
            call(0x4492);
            wr16(e(-0x3600), (uint16_t)-rd16(e(-0x3600)));
            anim(0xA);
        }
        turn_taken();
        if (all_away()) return;
        call(0x42C4);
        a605_touched();
        if (zero_after(0x4362)) wr8(e(-0x2BFF), 8);
        return;
    case 0x16BD2:                                   /* $C: beaten: counted off its point, gone after $30 frames */
        if (begun()) {
            point();
            turn_taken();
            wr8(c->a[3] - 0x5D62, (uint8_t)(rd8(c->a[3] - 0x5D62) - 1));
            wr8(c->a[3] - 0x5DB2, (uint8_t)(rd8(c->a[3] - 0x5DB2) + 1));
            wr8(e(-0x2800), 0x30);
            anim(2);
            return;
        }
        wr8(e(-0x2800), (uint8_t)(rd8(e(-0x2800)) - 1));
        if (!rd8(e(-0x2800))) wr8(e(-0x4000), 0);
        return;
    case 0x16C02:                                   /* $E: all away: to the right, then up to ($10E0, $1030) and off */
        if (!(rd8(e(-0x2BFF)) & 0x80)) {
            wr8(e(-0x3FFE), rd8(e(-0x3FFE)) & 0xF7);
            call(0xB44);
            call(0x2FD0);
            c->d[0] = 0xA & rd8(e(-0x3100));
            c->d[0] = set_b(c->d[0], c->d[0] - 2);
            if (c->d[0] & 0xFF) return;
            c->d[0] = 0x1F & rd16(0xFFFF965Au);
            if (c->d[0] & 0xFFFF) return;
            c->d[0] = set_w(c->d[0], 0x10A0);
            if ((uint16_t)c->d[0] >= rd16(e(-0x3800))) { wr16(e(-0x3600), 0x200); return; }
            wr8(e(-0x2BFF), rd8(e(-0x2BFF)) | 0x80);
            c->d[0] = set_w(c->d[0], 0x10E0 - rd16(e(-0x3800)));
            c->d[1] = set_w(c->d[1], 0x1030 - rd16(e(-0x3700)));
            call(0x12B6);
            wr16(e(-0x3600), (uint16_t)c->d[2]);
            wr16(e(-0x35FE), (uint16_t)c->d[3]);
            wr16(e(-0x2800), (uint16_t)c->d[5]);
            c->d[6] <<= 8;
            wr16(e(-0x36FE), (uint16_t)c->d[6]);
            c->d[6] = c->d[6] << 16 | c->d[6] >> 16;
            c->d[6] = set_w(c->d[6], (uint16_t)(int8_t)c->d[6]);
            wr16(e(-0x3700), (uint16_t)(rd16(e(-0x3700)) + c->d[6]));
            anim(8);
            return;
        }
        if (rd16(e(-0x2800))) {
            uint16_t t = (uint16_t)(rd16(e(-0x2800)) - 1);
            wr16(e(-0x2800), t);
            if (t) { call(0xB44); call(0xAB4); return; }
            wr16(e(-0x3800), 0x10E0);
            wr16(e(-0x3700), 0x1030);
            wr16(e(-0x3600), 0x100);
        }
        call(0xAB8);
        if (!(rd8(e(-0x4000)) & 1)) {
            point();
            wr8(c->a[3] - 0x5DB2, (uint8_t)(rd8(c->a[3] - 0x5DB2) + 1));
            wr8(e(-0x4000), 0);
        }
        return;
    case 0x16CB4:                                   /* $10: hit: thrown back $14 frames, blinking */
        if (begun()) {
            anim(4);
            wr8(e(-0x4000), rd8(e(-0x4000)) | 0x20);
            wr8(e(-0x2800), 0x14);
            call(0x452E);
        }
        if (zero_after(0x4362)) { c->d[0] = 0x10; call(0xB92); }
        call(0xB44);
        call(0x46DA);
        wr8(e(-0x2800), (uint8_t)(rd8(e(-0x2800)) - 1));
        if (!rd8(e(-0x2800))) {
            wr8(e(-0x3FFD), rd8(e(-0x3FFD)) & 0xBF);
            wr8(e(-0x4000), rd8(e(-0x4000)) & 0xDF);
            wr8(e(-0x2BFF), 8);
        }
        return;
    default: call(at); return;
    }
}
/* $16908: by state */
static void area605(void)
{
    c->d[0] = 0x7F & rd8(e(-0x2BFF));
    c->a[0] = 0x16918 + sx(rd16(0x16918 + c->d[0]));
    a605_states(c->a[0]);
}
/* $16D34 (not called): the player's touch and damage for this enemy, with the boss bar */
static void area605_touch(void)
{
    c->a[2] = sx(rd16(0xFFFF9EEEu));
    if (rd8(0xFFFF9F03u) & 0x40) call(0x4742);
    else { call(0xE64); call(0xD6E); }
    call(0xD76);
    call(0xC3E);
    if (rd8(e(-0x2AFD)) & 4) {
        c->d[0] = 0x58;
        call(0x492E);
        wr16(0xFFFF9628u, (uint16_t)c->d[0]);
        wr16(0xFFFF962Au, rd16(e(-0x2600)));
    }
}
ENTRY(Area605, area605)                    /* $16908 */
ENTRY(Area605Turn, turn_taken)             /* $16D00 */
ENTRY(Area605Touch, area605_touch)         /* $16D34 */
void Area605Hit(Cpu *cpu) { c = cpu; a605_hit(); c->ret = 0; }           /* $16D20 */
void Area605Touched(Cpu *cpu) { c = cpu; a605_touched(); c->ret = 0; }   /* $16BB2 */
/* $16BBC returns from its caller when all are away */
void Area605Away(Cpu *cpu) { c = cpu; if (all_away()) c->a[7] += 4; c->ret = 0; }
#define STATE(name, at) void name(Cpu *cpu) { c = cpu; a605_states(at); c->ret = 0; }
STATE(Area605Beaten, 0x16BD2)
STATE(Area605Leave, 0x16C02)
STATE(Area605Thrown, 0x16CB4)

/* ---- batch T61: Gragg & Glagg's helpers and the boss rooms' walkers ($15F9C-$162C8) ----
 * A group's record (-$2D00, at $A0E2 or $A0F2): its flags word (bit 15 in use; bit 0 start,
 * bit 1 called back, bit 2 told to rise), then its head and two legs. */
static uint32_t rec(void) { c->a[3] = sx(rd16(e(-0x2D00))); return c->a[3]; }
/* $15F9C: called back (bit 1, cleared): step 8, and the caller returns (1) */
static int called_back2(void)
{
    rec();
    uint8_t g = rd8(c->a[3]);
    wr8(c->a[3], g & 0xFD);
    if (!(g & 2)) { c->ccr = (uint16_t)((c->ccr & ~CC_Z) | CC_Z); return 0; }
    c->ccr &= (uint16_t)~CC_Z;
    wr8(e(-0x2BFF), 8);
    return 1;
}
/* $1609E: leg a2 at height d1 + $E, its look d0 (shown) */
static void leg_pose(void)
{
    wr16(c->a[2] - 0x3700, (uint16_t)c->d[1]);
    wr16(c->a[2] - 0x3700, (uint16_t)(rd16(c->a[2] - 0x3700) + 0xE));
    if ((rd8(c->a[2] - 0x4000) & 0x40) && (uint8_t)c->d[0] == rd8(c->a[2] - 0x3CFE)) return;
    { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }
    call(0x7E8);
    wr8(e(-0x4000), rd8(e(-0x4000)) | 0x40);
    { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }
}
/* $160CE: the legs hidden */
static void legs_hide(void)
{
    rec();
    c->a[0] = sx(rd16(c->a[3] + 4)); wr8(c->a[0] - 0x4000, rd8(c->a[0] - 0x4000) & 0xBF);
    c->a[0] = sx(rd16(c->a[3] + 6)); wr8(c->a[0] - 0x4000, rd8(c->a[0] - 0x4000) & 0xBF);
}
/* $1605A: the legs under the body by its height over home (-$20FE): stretched by 0-2 ($160C6,
 * $160CA), $18 at most; hidden at home */
static void glide_legs(void)
{
    c->d[2] = 0;
    c->d[1] = set_w(c->d[1], rd16(e(-0x20FE)) - rd16(e(-0x3700)));
    if (!(c->d[1] & 0xFFFF)) { legs_hide(); return; }
    c->d[1] = set_w(c->d[1], (uint16_t)c->d[1] >> 1);
    if ((uint16_t)c->d[1] >= 8) { c->d[2] = 1; if ((uint16_t)c->d[1] >= 0x10) c->d[2] = 2; }
    if ((uint16_t)c->d[1] >= 0x18) c->d[1] = 0x18;
    c->d[1] = set_w(c->d[1], -c->d[1]);
    c->d[1] = set_w(c->d[1], c->d[1] + rd16(e(-0x20FE)));
    rec();
    c->a[2] = sx(rd16(c->a[3] + 4));
    c->d[0] = set_b(c->d[0], rd8(0x160C6 + c->d[2]));
    leg_pose();
    c->a[2] = sx(rd16(c->a[3] + 6));
    c->d[0] = set_b(c->d[0], rd8(0x160CA + c->d[2]));
    leg_pose();
}
/* $1602A: towards its home height (VEL_Y), $30 above it at most; d0 0 (eq) when there */
static void glide_home(void)
{
    call(0xAD6);
    c->d[0] = set_w(c->d[0], rd16(e(-0x20FE)));
    int there;
    if (rd16(e(-0x35FE)) & 0x8000) {
        c->d[0] = set_w(c->d[0], c->d[0] - 0x30);
        there = !((uint16_t)c->d[0] < rd16(e(-0x3700)));
    } else there = !((uint16_t)c->d[0] > rd16(e(-0x3700)));
    if (there) wr16(e(-0x3700), (uint16_t)c->d[0]);
    glide_legs();
    c->d[0] = there ? 0 : 1; F_L(c->d[0]);
}
/* $160E8: the boss rooms' walkers (the hopper's handler when $966B bit 5): by state ($160F8) */
static void walker_states(uint32_t at)
{
    switch (at) {
    case 0x16100:                                   /* 0: with its group's head till the start */
        rec();
        if (!(rd16(c->a[3]) & 0x8000)) { wr8(e(-0x2BFF), 4); return; }
        if (begun()) {
            c->a[2] = sx(rd16(c->a[3] + 2));
            c->d[0] = 8 & rd8(c->a[2] - 0x3FFE);
            wr8(e(-0x3FFE), (uint8_t)c->d[0]);
            wr16(e(-0x3800), rd16(c->a[2] - 0x3800));
            wr16(e(-0x3700), rd16(c->a[2] - 0x3700));
            wr8(e(-0x3CFD), 0x12);
            anim(0);
            wr8(e(-0x4000), rd8(e(-0x4000)) | 0x40);
        }
        {
            uint8_t g = rd8(c->a[3]);
            wr8(c->a[3], g & 0xFE);
            if (g & 1) wr8(e(-0x2BFF), 4);
        }
        return;
    case 0x16152: return;                           /* 2 */
    case 0x16154:                                   /* 4: walking; turning from its partner; calling a group to rise */
        if (begun()) {
            wr8(e(-0x2200), 0);
            wr8(e(-0x2800), 0);
            wr8(e(-0x27FF), 0x10);
            wr16(e(-0x27FE), rd16(e(-0x3400)));
            wr8(e(-0x2F00), (uint8_t)((rd8(e(-0x2F00)) & 0x80) | 0x13));
            anim(0);
            return;
        }
        c->a[2] = sx(rd16(0xFFFF9EEEu));
        call(0xE64);
        if (rd8(e(-0x2200))) wr8(e(-0x2200), (uint8_t)(rd8(e(-0x2200)) - 1));
        else if (rd8(0xFFFFA106u) >= 2 && sx(rd16(0xFFFFA108u)) == c->a[4]) {
            c->d[0] = set_w(c->d[0], rd16(0xFFFFA10Au));
            if (c->d[0] & 0xFFFF) {
                c->a[2] = sx(c->d[0]);
                call(0xE9A);
                if (c->ccr & (CC_C | CC_Z)) {               /* the two touch: both turn */
                    c->d[0] = 0x39; call(0x366);
                    wr8(e(-0x3FFE), rd8(e(-0x3FFE)) ^ 8);
                    wr8(c->a[2] - 0x3FFE, rd8(c->a[2] - 0x3FFE) ^ 8);
                    wr16(e(-0x2800), 1);
                    wr16(c->a[2] - 0x2800, 1);
                    wr16(e(-0x3600), (uint16_t)-rd16(e(-0x3600)));
                    wr16(c->a[2] - 0x3600, (uint16_t)-rd16(c->a[2] - 0x3600));
                    wr8(e(-0x2200), 4);
                }
            }
        }
        c->d[0] = set_w(c->d[0], rd16(e(-0x27FE)));
        call(0x46C6);
        call(0xB42);
        call(0x46DA);
        if (rd8(e(-0x2800)) & 0x80) {
            c->a[2] = sx(rd16(e(-0x31FE)));
            if (rd16(c->a[2]) & 0x8000) { wr8(e(-0x2800), 0); goto wall; }
            c->a[2] = sx(rd16(e(-0x2CFE)));
            c->d[0] = set_w(c->d[0], rd16(c->a[2] - 0x3800) - rd16(e(-0x3800)));
            if (c->d[0] & 0x8000) c->d[0] = set_w(c->d[0], -c->d[0]);
            if ((uint16_t)c->d[0] >= 0x10) goto wall;
            c->a[3] = sx(rd16(c->a[2] - 0x2D00));
            wr8(c->a[3], rd8(c->a[3]) | 4);
            wr8(e(-0x2BFF), 6);
            return;
        }
        {
            uint8_t t = (uint8_t)(rd8(e(-0x27FF)) - 1);
            wr8(e(-0x27FF), t);
            if (t) goto wall;
        }
        wr8(e(-0x27FF), 1);
        c->a[3] = 0xFFFFA102u;
        c->d[2] = 1;
        do {                                        /* a group's head ahead within $70 */
            c->d[0] = set_w(c->d[0], rd16(c->a[3])); c->a[3] += 2;
            if (!(c->d[0] & 0x8000)) {
                c->a[2] = sx(c->d[0]);
                c->d[0] = set_w(c->d[0], rd16(c->a[2] - 0x3800));
                c->d[1] = set_w(c->d[1], rd16(e(-0x3800)));
                if (rd8(e(-0x3FFE)) & 8) { uint32_t t = c->d[0]; c->d[0] = c->d[1]; c->d[1] = t; }
                c->d[0] = set_w(c->d[0], c->d[0] - c->d[1]);
                if (!(c->d[0] & 0x8000) && (uint16_t)c->d[0] < 0x70) {
                    wr16(e(-0x2CFE), (uint16_t)c->a[2]);
                    wr16(e(-0x31FE), rd16(c->a[2] - 0x2CFE));
                    c->a[3] = sx(rd16(c->a[2] - 0x2D00));
                    wr8(c->a[3], rd8(c->a[3]) | 2);
                    wr8(e(-0x2800), 0xFF);
                    goto wall;
                }
            }
            c->d[2] = set_w(c->d[2], c->d[2] - 1);
        } while ((c->d[2] & 0xFFFF) != 0xFFFF);
    wall:
        c->d[0] = 0x41 & rd8(e(-0x3100));
        c->d[0] = set_b(c->d[0], c->d[0] - 1);
        if (c->d[0] & 0xFF) return;
        wr8(e(-0x3FFE), rd8(e(-0x3FFE)) ^ 8);
        c->d[0] = 0x39; call(0x366);
        return;
    case 0x16294:                                   /* 6: gone after $28 frames, off the boss's count */
        if (begun()) wr8(e(-0x2800), 0x28);
        call(0xB42);
        call(0xAB4);
        wr8(e(-0x2800), (uint8_t)(rd8(e(-0x2800)) - 1));
        if (rd8(e(-0x2800))) return;
        c->a[3] = 0xFFFFA102u;
        wr8(c->a[3] + 4, (uint8_t)(rd8(c->a[3] + 4) - 1));
        c->d[0] = rd8(e(-0x25FE));
        wr16(c->a[3] + 6 + sx(c->d[0]), 0);
        wr8(e(-0x4000), 0);
        return;
    default: call(at); return;
    }
}
static void walker(void)
{
    c->d[0] = 0x7F & rd8(e(-0x2BFF));
    c->a[0] = 0x160F8 + sx(rd16(0x160F8 + c->d[0]));
    walker_states(c->a[0]);
}
/* $1600C: touches: outside the boss rooms ($966B bit 5) hit like any monster ($43BC: the caller
 * returns and the action goes to $1357E with d0 2); in them, with the shield while it blinks */
void GuardTouch(Cpu *cpu)
{
    c = cpu;
    if (!(rd8(0xFFFF966Bu) & 0x20)) {
        call(0x4792);
        call(0x439E);
        c->ret = 0;
        if (!(c->ccr & CC_N)) return;
        c->d[0] = 2; F_L(2);
        c->a[7] += 4;
        port_continue_at(c, 0x1357E, 0);
        return;
    }
    call(rd8(e(-0x2800)) ? 0x4778 : 0x4792);
    c->ret = 0;
}
void CalledBack(Cpu *cpu) { c = cpu; if (called_back2()) c->a[7] += 4; c->ret = 0; }   /* $15F9C */
ENTRY(GlideHome, glide_home)               /* $1602A */
ENTRY(GlideLegs, glide_legs)               /* $1605A */
ENTRY(LegPose, leg_pose)                   /* $1609E */
ENTRY(LegsHide, legs_hide)                 /* $160CE */
ENTRY(Walker, walker)                      /* $160E8 */
void WalkerWalk(Cpu *cpu) { c = cpu; walker_states(0x16154); c->ret = 0; }   /* $16154 */
void WalkerGone(Cpu *cpu) { c = cpu; walker_states(0x16294); c->ret = 0; }   /* $16294 */
