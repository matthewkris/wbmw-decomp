/* Movement with collision, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * Unlike the rest of port/, this is not an instruction-by-instruction transcription: it is
 * written to be read, with named fields and plain control flow, and it does not reproduce the
 * 68000's leftover registers. Each routine has a contract (port.h, registry.c): what the game
 * actually uses after it returns (here the stack pointer, and for the player the map cell under
 * its feet); the harness checks
 * memory exactly and watches that every register left different is dead.
 *
 * All of movement with collision: the seven entry points ($2E38-$3039), what they are built from
 * (walls, slopes, floors and ceilings against the map, $303A-$3AFB; the moving platforms,
 * $3AFC-$3E1F), the conveyors ($3F08) and the water currents ($3E4C). The map lookups they use
 * ($2542, $30D2-$3140) are shared with the rest of the game and stay in map.c. */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

/* ---- entities: fields are arrays of 256 bytes, indexed by the entity's offset (0-$FC). As in the
 * original, a field is the entity (a4) plus a negative displacement, in 32 bits; the bus then
 * keeps 24 (so $FFFFC800 + e is work RAM). Platforms are addressed the same way from a2. */

#define X(e)          ((e) - 0x3800)   /* position, whole pixels */
#define X_FRAC(e)     ((e) - 0x37FE)   /* its fraction (the high byte; written as a word) */
#define Y(e)          ((e) - 0x3700)
#define Y_FRAC(e)     ((e) - 0x36FE)
#define VEL_X(e)      ((e) - 0x3600)   /* 8.8 */
#define VEL_Y(e)      ((e) - 0x35FE)
#define HALF_W(e)     ((e) - 0x34FE)   /* byte */
#define HALF_H(e)     ((e) - 0x34FD)   /* byte */
#define COLL(e)       ((e) - 0x3100)   /* collision result of this move (byte, bits below) */
#define COLL_ATTR(e)  ((e) - 0x30FF)   /* map attribute of the tile it stopped on (byte) */
#define COLL_TYPE(e)  ((e) - 0x30FE)   /* collision type of what it stands on (word) */
#define PUSH_X(e)     ((e) - 0x3000)   /* correction to apply to X after the sideways checks */
#define PUSH_Y(e)     ((e) - 0x2FFE)   /* ... and to Y */
#define MODE(e)       ((e) - 0x2F00)   /* physics mode; bit 6: no collision with platforms */
#define PREV_COLL(e)  ((e) - 0x2EFF)   /* last move's collision result */
#define PREV_COLL_TYPE(e) ((e) - 0x2EFE)
#define PREV_X(e)     ((e) - 0x2E00)
#define PREV_Y(e)     ((e) - 0x2DFE)
#define PLATFORM(e)   ((e) - 0x2D00)   /* the platform it stands on (its record offset) */

/* collision result bits. ON_FLOOR means "stopped vertically": PUSH_Y applies; with CEILING it
 * was a ceiling. WALL means PUSH_X applies; with WALL_ON_LEFT the wall was on the left. */
enum { WALL = 0x01, ON_FLOOR = 0x02, WALL_ON_LEFT = 0x04, CEILING = 0x08, CARRIED = 0x20,
       SLOPE_STEP = 0x40, BY_PLATFORM = 0x80 };

/* collision type bits (tile_type): which sides of the tile are solid */
enum { T_BLOCKS_RIGHT = 0x01,   /* stops something moving right (its left face) */
       T_BLOCKS_LEFT = 0x02, T_CEILING = 0x04, T_FLOOR = 0x08,
       T_SLOPE = 0x100 };       /* bits 4-7: which slope */

/* ---- the six moving platforms: records at offsets 0, 4 ... 20 ---- */
#define P_FLAGS(p)    ((uint32_t)(int32_t)(p) - 0x6072)   /* bit 7 active; bits 0/1: carrying the player / another */
#define P_ATTR(p)     ((uint32_t)(int32_t)(p) - 0x6071)   /* collision attribute, as a map block's */
#define P_X(p)        ((uint32_t)(int32_t)(p) - 0x601E)   /* centre */
#define P_Y(p)        ((uint32_t)(int32_t)(p) - 0x601C)
#define P_DX(p)       ((uint32_t)(int32_t)(p) - 0x5FCA)   /* movement this frame */
#define P_DY(p)       ((uint32_t)(int32_t)(p) - 0x5FC8)
#define P_HALF_W(p)   ((uint32_t)(int32_t)(p) - 0x5F76)
#define P_HALF_H(p)   ((uint32_t)(int32_t)(p) - 0x5F74)
#define PLATFORMS 6

#define PLAYER        0xFFFF9EEEu           /* the player's entity offset */
#define AREA_FLAGS    0xFFFF966Bu           /* bit 0 water currents, bit 1 slopes, bit 4 see CARRIED */
#define LAYER_A       0xFFFF9754u           /* the map layer collision uses (map.c) */
#define TILE_BELOW    0xFFFF9EDEu           /* the player's probes (map.c): type under the feet */
#define TILE_HERE     0xFFFF9EE2u           /* ... and at the position */
#define CONVEYOR_1    0xFFFF9EDAu           /* speeds of the two kinds of conveyor (signed bytes) */
#define CONVEYOR_2    0xFFFF9EDBu
#define FLOOR_THEN    0xFFFF9EE8u           /* long: $38E4 if the slope floor check goes on to the span check */
#define PLAYER_FLAGS2 0xFFFF9F0Bu           /* bit 0: slope checks in the other order */
#define RIDE_OVERLAP  0xFFFF9EE4u           /* how far the entity overlaps the platform it rides, -1 none */
#define LAND_OVERLAP  0xFFFF9EE6u           /* ... the platform being landed on */

/* "is it the player?" is asked two ways in the original: cmp.w compares the low word of a4,
 * cmpa.w the whole register with the sign-extended player offset */
static int is_player(Ent e) { return (uint16_t)rdw(PLAYER) == (uint16_t)e; }
static int is_player_a(Ent e) { return (uint32_t)(int32_t)rdw(PLAYER) == e; }
static int active(int p) { return rd8(P_FLAGS(p)) & 0x80; }

/* the collision type of an attribute byte (docs/NOTES.md, Collision): a two-level table at
 * $3FC2; for the player, types $5xxx and $6xxx become solid unless $FF9EF1 says otherwise */
static uint16_t tile_type(uint8_t attr, Ent e)
{
    uint32_t table = 0x3FC2 + (uint32_t)(int32_t)rdw(0x3FC2 + (attr >> 4) * 2);
    uint16_t type = rd16(table + (attr & 15) * 2);
    if (is_player_a(e)) {
        uint8_t equip = rd8(0xFFFF9EF1u);
        if ((type & 0xF000) == 0x5000 && !(equip & 0x20)) type = 8;
        if ((type & 0xF000) == 0x6000 && !(equip & 0x10)) type = 15;
    }
    return type;
}

/* ---- sideways ($3AFC, $3B74) ---- */

/* one platform against an entity moving sideways by `moved`; `right` and `left` are its edges */
static void platform_side(Ent e, int p, int16_t moved, int16_t right, int16_t left)
{
    uint16_t dist = (uint16_t)(rdw(Y(e)) - rdw(P_Y(p)));
    if ((uint16_t)rdw(Y(e)) < (uint16_t)rdw(P_Y(p))) dist = (uint16_t)(rdw(P_Y(p)) - rdw(Y(e)));
    uint16_t reach = (uint16_t)(rd8(HALF_H(e)) - 1 + rdw(P_HALF_H(p)));
    if (dist > reach) return;                       /* not level with it */
    int16_t closing = (int16_t)(rdw(P_DX(p)) - moved);   /* the platform's motion, relative */
    if (closing == 0) return;
    uint8_t coll = rd8(COLL(e));
    int16_t push = rdw(PUSH_X(e));
    if (closing < 0) {                              /* coming at the entity's right side */
        if ((coll & 0x85) == 0x85) return;
        if (!(tile_type(rd8(P_ATTR(p)), e) & 1)) return;
        int16_t depth = (int16_t)(rdw(P_X(p)) - rdw(P_HALF_W(p)) - right);
        if (depth >= 0 || (uint16_t)depth < (uint16_t)closing || depth >= push) return;
        wr8(COLL(e), coll | 0x81);
        wrw(PUSH_X(e), depth);
    } else {                                        /* coming at its left side */
        if ((coll & 0x85) == 0x81) return;
        if (!(tile_type(rd8(P_ATTR(p)), e) & 2)) return;
        int16_t depth = (int16_t)(rdw(P_X(p)) + rdw(P_HALF_W(p)) - left);
        if (depth < 0 || (uint16_t)depth > (uint16_t)closing || depth <= push) return;
        wr8(COLL(e), coll | 0x85);
        wrw(PUSH_X(e), depth);
    }
}

/* $3AFC: carried sideways by the platform it rides, then pushed out of any platform it ran into */
void platforms_sideways(Ent e)
{
    if (rd8(MODE(e)) & 0x40) return;
    uint8_t prev = rd8(PREV_COLL(e));
    int riding = (prev & 0x8A) == (BY_PLATFORM | ON_FLOOR);
    if ((rd8(AREA_FLAGS) & 0x10) && is_player(e) && (prev & CARRIED)) riding = 1;
    if (riding) {
        int p = rdw(PLATFORM(e));
        if (rd16(P_FLAGS(p)) & 0x8000) wrw(X(e), rdw(X(e)) + rdw(P_DX(p)));   /* tst.w: a word read, unlike $3C22 */
    }
    wrw(PUSH_X(e), 0);
    int16_t x = rdw(X(e)), half = rd8(HALF_W(e));
    int16_t moved = (int16_t)(x - rdw(PREV_X(e)));
    for (int p = 0; p < PLATFORMS * 4; p += 4)
        if (active(p)) platform_side(e, p, moved, (int16_t)(x + half), (int16_t)(x - half));
    wrw(X(e), rdw(X(e)) + rdw(PUSH_X(e)));
}

/* ---- vertically ($3C22, $3D06, $3E00) ---- */

/* $3E00: how far the entity and the platform overlap sideways, or -1 if they do not */
static int overlap_x(Ent e, int p)
{
    uint16_t a = (uint16_t)rdw(X(e)), b = (uint16_t)rdw(P_X(p));
    uint16_t dist = a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
    uint16_t reach = (uint16_t)((uint8_t)(rd8(HALF_W(e)) - 1) + rdw(P_HALF_W(p)));
    return dist > reach ? -1 : (uint16_t)(reach - dist);
}

/* stand on platform p: remember it, and stop falling */
static void stand_on(Ent e, int p, int16_t depth)
{
    wrw(PLATFORM(e), p);
    wrw(PUSH_Y(e), depth);
    wrw(Y_FRAC(e), 0);
}

/* one platform against an entity moving vertically by `moved`; `bottom` and `top` are its edges */
static void platform_vertical(Ent e, int p, int16_t moved, int16_t bottom, int16_t top)
{
    int over = overlap_x(e, p);
    if (over < 0) return;
    wrw(LAND_OVERLAP, over);
    int16_t closing = (int16_t)(rdw(P_DY(p)) - moved);
    uint8_t coll = rd8(COLL(e));
    if (closing <= 0 && !(closing == 0 && rdw(RIDE_OVERLAP) < 0 && (rd8(PREV_COLL(e)) & 0x0A) != ON_FLOOR)) {
        /* landing on top (or staying there) */
        if ((coll & 0x8A) == 0x8A) return;
        if (!(tile_type(rd8(P_ATTR(p)), e) & 8)) return;
        int16_t p_top = (int16_t)(rdw(P_Y(p)) - rdw(P_HALF_H(p)));
        int16_t depth = (int16_t)(p_top - bottom);
        if ((uint16_t)p_top > (uint16_t)bottom) return;          /* above it */
        if (rdw(RIDE_OVERLAP) >= 0) {
            int ride = rdw(PLATFORM(e));
            if (p_top == (int16_t)(rdw(P_Y(ride)) - rdw(P_HALF_H(ride)))) {
                /* level with the platform it rides: take this one only if it overlaps more */
                if ((uint16_t)rdw(LAND_OVERLAP) <= (uint16_t)rd8(HALF_W(e))) return;
                if (depth > rdw(PUSH_Y(e))) return;
                goto land;
            }
        }
        if (depth < closing || depth > rdw(PUSH_Y(e))) return;
    land:
        wr8(COLL(e), coll | 0x82);
        wrw(VEL_Y(e), 0);
        wrw(Y_FRAC(e), 0);
        stand_on(e, p, depth);
    } else if (closing > 0) {
        /* hitting it from below */
        if (!(tile_type(rd8(P_ATTR(p)), e) & 4)) return;
        int16_t depth = (int16_t)(rdw(P_Y(p)) + rdw(P_HALF_H(p)) - top);
        if (depth < 0 || (uint16_t)depth > (uint16_t)closing) return;
        if ((coll & 0x8A) != (BY_PLATFORM | ON_FLOOR) && depth < rdw(PUSH_Y(e))) return;
        wr8(COLL(e), coll | 0x8A);
        if (rdw(VEL_Y(e)) < 0) wrw(VEL_Y(e), 0);
        stand_on(e, p, depth);
    }
}

/* $3C22: carried vertically by the platform it rides, then landing on or bumping into platforms */
void platforms_vertical(Ent e)
{
    if (rd8(MODE(e)) & 0x40) return;
    wrw(RIDE_OVERLAP, -1);
    uint8_t prev = rd8(PREV_COLL(e));
    int p = rdw(PLATFORM(e)), carried = 0;
    if ((rd8(AREA_FLAGS) & 0x10) && is_player(e) && (prev & CARRIED)) {
        carried = active(p);
    } else if ((prev & 0x8A) == (BY_PLATFORM | ON_FLOOR) && active(p)) {
        int over = overlap_x(e, p);
        if (over >= 0) {
            wrw(RIDE_OVERLAP, over);
            wr8(COLL(e), rd8(COLL(e)) | 0x82);
            carried = 1;
        }
    }
    if (carried) {
        wrw(Y(e), rdw(Y(e)) + rdw(P_DY(p)));
        wrw(COLL_TYPE(e), tile_type(rd8(P_ATTR(p)), e));
        wrw(VEL_Y(e), 0);
        wrw(Y_FRAC(e), 0);
    }
    wrw(PUSH_Y(e), 0);
    int16_t y = rdw(Y(e)), half = rd8(HALF_H(e));
    int16_t moved = (int16_t)(y - rdw(PREV_Y(e)));
    for (int q = 0; q < PLATFORMS * 4; q += 4) {
        if (!active(q)) continue;
        if ((rd8(PREV_COLL(e)) & 0x8A) == (BY_PLATFORM | ON_FLOOR) && rdw(PLATFORM(e)) == q) continue;
        platform_vertical(e, q, moved, (int16_t)(y + half), (int16_t)(y - half));
    }
    wrw(Y(e), rdw(Y(e)) + rdw(PUSH_Y(e)));
    if ((rd8(COLL(e)) & 0x8A) == (BY_PLATFORM | ON_FLOOR)) {
        int ride = rdw(PLATFORM(e));
        wr8(P_FLAGS(ride), rd8(P_FLAGS(ride)) | (is_player_a(e) ? 1 : 2));
    }
}

/* ---- the map ---- */

void MapBlockAt(Cpu *);                     /* map.c */

/* The last map cell looked at, as the original leaves it in d6 (column) and d7 (row * 64): the
 * player's caller keeps it as the last cell stood on. Scans that step past a cell move it on. */
static struct { uint16_t col, row; uint8_t attr; } cell;

static uint16_t col_of(int16_t x) { return (uint16_t)x >> 4; }
static uint16_t row_of(int16_t y) { return (uint16_t)((y & 0xFFF0) << 2); }

/* the collision type of a map cell ($30DA), remembering the cell and its attribute byte */
static uint16_t type_at(Ent e, uint16_t col, uint16_t row)
{
    Cpu t = {{0}};
    t.a[3] = LAYER_A; t.d[6] = col; t.d[7] = row;
    MapBlockAt(&t);
    uint16_t block = (uint16_t)t.d[0];
    uint32_t table = rd32(LAYER_A + 0x10 + ((block & 0x300) >> 6));   /* one of four block tables */
    cell.col = col; cell.row = row;
    cell.attr = rd8(table + 0x800 + (block & 0xFF));
    return tile_type(cell.attr, e);
}

static uint16_t type_at_pixel(Ent e, int16_t x, int16_t y) { return type_at(e, col_of(x), row_of(y)); }

/* how many 16-pixel cells from a to b, less one (the scans run this + 1 times) */
static int cells_between(int16_t a, int16_t b) { return (uint16_t)((b & 0xFFF0) - (a & 0xFFF0)) >> 4; }

/* ---- the entity's box ---- */
static int16_t half_w(Ent e) { return rd8(HALF_W(e)); }
static int16_t half_h(Ent e) { return rd8(HALF_H(e)); }
static int16_t feet(Ent e)   { return (int16_t)(rdw(Y(e)) + half_h(e)); }
static int16_t head(Ent e)   { return (int16_t)(rdw(Y(e)) - half_h(e)); }
static uint8_t coll(Ent e)   { return rd8(COLL(e)); }
static void set_coll(Ent e, uint8_t keep, uint8_t add) { wr8(COLL(e), (uint8_t)((coll(e) & keep) | add)); }
static int was_on_floor(Ent e) { return (rd8(PREV_COLL(e)) & 0x8A) == ON_FLOOR; }   /* on the map, not a platform */

/* one axis of the velocity ($AB8, $AD6): the 8.8 velocity added to the 16.8 position */
static void apply_velocity(Ent e, uint32_t vel, uint32_t pos, uint32_t prev)
{
    unsigned frac = rd8(pos + 2) + rd8(vel + 1);
    int16_t old = rdw(pos);
    wr8(pos + 2, (uint8_t)frac);
    wrw(pos, old + (int8_t)rd8(vel) + (frac > 0xFF));
    wrw(prev, old);
}

/* ---- slopes ---- */

/* The original jumps through a table by slope number to see whether the entity moves into the
 * slope ($3860). Slope 0 jumps to an endless loop ($3888), and numbers past 10 read past the
 * table; no tile the game uses gets there. */
static void original_hangs(uint16_t type)
{
    fprintf(stderr, "movement: tile type %04X makes the original loop forever ($3860)\n", type);
    exit(3);
}

/* $3860: the entity's velocity measured along the slope's normal; negative: moving away from it */
static int16_t into_slope(Ent e, uint16_t type)
{
    int16_t vx = rdw(VEL_X(e)), vy = rdw(VEL_Y(e));
    switch ((type & 0xF0) >> 4) {
    case 1: case 2:  return (int16_t)(2 * vy + vx);
    case 3: case 4:  return (int16_t)(2 * vy - vx);
    case 5:          return (int16_t)(vy + vx);
    case 6:          return (int16_t)(vy - vx);
    case 7: case 8:  return (int16_t)(vy + 2 * vx);
    case 9: case 10: return (int16_t)(vy - 2 * vx);
    default: original_hangs(type); return 0;
    }
}

/* $383C: where the slope's surface is in the entity's column, relative to the row of y */
static int16_t slope_floor(Ent e, uint16_t type, int16_t y)
{
    int8_t surface = (int8_t)rd8(0x403C + (type & 0xF0) + (rdw(X(e)) & 15));
    return (int16_t)(surface - (y & 15));
}

/* $3538: where the slope's face is in row y, relative to the column of x; 0 if it has none there */
static int slope_face(uint16_t type, int16_t x, int16_t y, int16_t *dx)
{
    int8_t face = (int8_t)rd8(0x40DC + (type & 0xF0) + (y & 15));
    *dx = (int16_t)(face - (x & 15));
    return face != -128;
}

/* $3750: which way a slope leaves a cell on the given side (0 left, 1 right): a code 0-7 */
static uint8_t slope_exit(uint16_t type, int side)
{
    return rd8(0x418C + side + ((type & 0xF0) >> 3));
}

/* $341C: walking off a ledge onto a slope, or into the side of one, while in the air: step onto
 * its surface. "back" is how far it moved, reversed. */
static int fits(int16_t step, int16_t back)
{
    if (back >= 0) return step >= 0 && (int16_t)(back - step) >= 0;
    return step == 0 || (step < 0 && (uint16_t)step >= (uint16_t)back);
}

static void step_onto_slope(Ent e, int16_t step, uint16_t type)
{
    wrw(X(e), rdw(X(e)) + step);
    set_coll(e, 0x77, SLOPE_STEP | ON_FLOOR);
    wrw(COLL_TYPE(e), type);
    wrw(Y_FRAC(e), 0);
    wrw(VEL_Y(e), 0);
}

static void slope_walk(Ent e)
{
    int16_t x = rdw(X(e)), px = rdw(PREV_X(e)), back = (int16_t)(px - x);
    if (back == 0) return;
    uint16_t need = T_SLOPE | T_FLOOR | (back < 0 ? T_BLOCKS_RIGHT : T_BLOCKS_LEFT);
    int16_t crossed = (int16_t)((x & 0xFFF0) - (px & 0xFFF0));
    if (rd8(PREV_COLL(e)) & ON_FLOOR) return;
    int16_t bottom = (int16_t)(feet(e) - 1), step;
    if (crossed) {                                  /* the cell it came from */
        uint16_t t = type_at_pixel(e, px, bottom);
        if ((t & need) == need && into_slope(e, t) >= 0 && slope_face(t, px, bottom, &step)) {
            step = (int16_t)(step + back);
            if (fits(step, back)) { step_onto_slope(e, step, t); return; }
        }
    }
    uint16_t t = type_at_pixel(e, x, bottom);       /* the cell it is in */
    if ((t & need) != need || into_slope(e, t) < 0 || !slope_face(t, x, bottom, &step)) return;
    if (fits(step, back)) step_onto_slope(e, step, t);
}

/* ---- sideways against the map ---- */

/* $3142: both sides of the box, every row it covers */
static void walls_both_sides(Ent e)
{
    if (rd8(AREA_FLAGS) & 2) slope_walk(e);
    int16_t x = rdw(X(e)), right = (int16_t)(x + half_w(e) - 1), left = (int16_t)(x - half_w(e));
    int16_t top = head(e);
    int rows = cells_between(top, (int16_t)(feet(e) - 1));
    uint16_t row = row_of(top);
    for (int n = rows; n >= 0; n--, row += 0x40) {
        if ((type_at(e, col_of(left), row) & (T_SLOPE | T_BLOCKS_LEFT)) == T_BLOCKS_LEFT) {
            wrw(PUSH_X(e), -left & 15);
            set_coll(e, 0x7F, WALL_ON_LEFT | WALL);
            break;
        }
    }
    row = row_of(top);
    for (int n = rows; n >= 0; n--, row += 0x40) {
        if ((type_at(e, col_of(right), row) & (T_SLOPE | T_BLOCKS_RIGHT)) == T_BLOCKS_RIGHT) {
            if ((coll(e) & 0x85) == (WALL_ON_LEFT | WALL)) {
                set_coll(e, (uint8_t)~WALL, 0);     /* walls on both sides: neither pushes */
            } else {
                wrw(PUSH_X(e), -((right & 15) + 1));
                set_coll(e, 0x7F, WALL);
            }
            break;
        }
    }
}

/* $322E: the side it is moving towards, every row. On a hit it clears the collision type: the
 * original stores the register it has just tested the wall's type with (andi, subq), which is 0
 * by then. That also undoes the type a slope step set (SlopeWalk). */
static void walls_ahead(Ent e)
{
    if (rdw(VEL_X(e)) == 0) return;
    if (rd8(AREA_FLAGS) & 2) slope_walk(e);
    int16_t top = head(e);
    int rows = cells_between(top, (int16_t)(feet(e) - 1));
    uint16_t row = row_of(top);
    if (rdw(VEL_X(e)) < 0) {
        int16_t left = (int16_t)(rdw(X(e)) - half_w(e));
        for (int n = rows; n >= 0; n--, row += 0x40) {
            uint16_t t = type_at(e, col_of(left), row);
            if ((t & (T_SLOPE | T_BLOCKS_LEFT)) == T_BLOCKS_LEFT) {
                wrw(COLL_TYPE(e), 0);                 /* see above */
                set_coll(e, 0x7F, WALL_ON_LEFT | WALL);
                wrw(PUSH_X(e), -left & 15);
                return;
            }
        }
    } else {
        int16_t right = (int16_t)(rdw(X(e)) + half_w(e));
        for (int n = rows; n >= 0; n--, row += 0x40) {
            uint16_t t = type_at(e, col_of(right), row);
            if ((t & (T_SLOPE | T_BLOCKS_RIGHT)) == T_BLOCKS_RIGHT) {
                wrw(COLL_TYPE(e), 0);
                set_coll(e, 0x7F, WALL);
                wrw(PUSH_X(e), -((right & 15) + 1));
                return;
            }
        }
    }
}

/* $3310, and $32FE which ignores slopes: the column of its centre, every row */
static void walls_at_centre(Ent e, uint16_t ignore)
{
    if (rdw(VEL_X(e)) == 0) return;
    int left = rdw(VEL_X(e)) < 0;
    int16_t top = head(e);
    uint16_t row = row_of(top), col = col_of(rdw(X(e)));
    for (int n = cells_between(top, (int16_t)(feet(e) - 1)); n >= 0; n--, row += 0x40) {
        uint16_t t = type_at(e, col, row);
        if (t & ignore) continue;
        if (!(t >> left & 1)) continue;             /* T_BLOCKS_RIGHT, or T_BLOCKS_LEFT going left */
        int16_t x = rdw(X(e));
        set_coll(e, 0x7F, 0);
        if (!left) { set_coll(e, 0xFF, WALL); wrw(PUSH_X(e), -((x & 15) + 1)); }
        else       { set_coll(e, 0xFF, WALL_ON_LEFT | WALL); wrw(PUSH_X(e), -x & 15); }
        return;
    }
}

static void walls_at_centre_slopes(Ent e)
{
    if (rd8(AREA_FLAGS) & 2) slope_walk(e);
    walls_at_centre(e, T_SLOPE);
}

/* $303A: apply the sideways push, and stop (or bounce off) a wall it is moving into */
static void end_sideways(Ent e)
{
    uint8_t c = coll(e);
    if (!(c & WALL)) return;
    if (!(c & BY_PLATFORM)) wrw(X(e), rdw(X(e)) + rdw(PUSH_X(e)));
    wrw(X_FRAC(e), c & WALL_ON_LEFT ? 0 : 0xFFFF);  /* flush against the wall */
    int16_t v = rdw(VEL_X(e));
    if (v == 0 || (c & WALL_ON_LEFT ? v > 0 : v < 0)) return;
    uint8_t mode = rd8(MODE(e));
    if (!(mode & 0x10)) wrw(VEL_X(e), 0);
    else if (!(mode & 0x04)) wrw(VEL_X(e), -v);     /* bounces */
}

/* ---- vertically against the map ---- */

/* landed on (or hit the ceiling of) the last cell looked at, `push` pixels away */
static void land(Ent e, int16_t push, uint16_t type)
{
    wrw(PUSH_Y(e), push);
    wr8(COLL_ATTR(e), cell.attr);
    wrw(COLL_TYPE(e), type);
    set_coll(e, 0x77, ON_FLOOR);
}

static void hit_ceiling(Ent e, int16_t top)
{
    wrw(PUSH_Y(e), -top & 15);
    set_coll(e, 0x5F, CEILING | ON_FLOOR);
}

/* $3A54: the ceiling, every column of the box */
static void ceiling_span(Ent e)
{
    int16_t top = head(e), left = (int16_t)(rdw(X(e)) - half_w(e));
    int n = cells_between(left, (int16_t)(rdw(X(e)) + half_w(e) - 1));
    uint16_t col = col_of(left & 0xFFF0), row = row_of(top);
    for (;;) {
        if ((type_at(e, col, row) & (T_SLOPE | T_CEILING)) == T_CEILING) { hit_ceiling(e, top); return; }
        cell.col = ++col;
        if (--n < 0) return;
    }
}

/* $3A4C, $3AB0: the ceiling while rising; every column, or the centre only */
static void ceiling_span_rising(Ent e) { if (rdw(VEL_Y(e)) < 0) ceiling_span(e); }

static void ceiling_at_centre(Ent e)
{
    if (rdw(VEL_Y(e)) >= 0) return;
    int16_t top = head(e);
    if (type_at_pixel(e, rdw(X(e)), top) & T_CEILING) hit_ceiling(e, top);
}

/* $39FA: the floor under the centre, while falling */
static void floor_at_centre(Ent e)
{
    if (rdw(VEL_Y(e)) < 0) return;
    int16_t f = feet(e);
    uint16_t t = type_at_pixel(e, rdw(X(e)), f);
    if (!(t & T_FLOOR)) return;
    int16_t push = (int16_t)-(f & 15);
    if ((int16_t)(rdw(Y(e)) - rdw(PREV_Y(e)) + push) < 0) return;   /* was already below its top */
    set_coll(e, 0x77, ON_FLOOR);
    wrw(PUSH_Y(e), push);
    wr8(COLL_ATTR(e), cell.attr);
    wrw(COLL_TYPE(e), t);
}

/* $3948 (and $38E4 without slopes): the floor under every column of the box. `fell` is how far
 * the feet moved down; the floor counts if it was standing, or if the feet came from above it. */
static void floor_span(Ent e, int16_t f, int16_t fell, uint16_t mask)
{
    int16_t left = (int16_t)(rdw(X(e)) - half_w(e));
    int n = cells_between(left, (int16_t)(rdw(X(e)) + half_w(e) - 1));
    uint16_t col = col_of(left & 0xFFF0), row = row_of(f);
    for (;;) {
        uint16_t t = type_at(e, col, row);
        if ((t & mask) == T_FLOOR) {
            int16_t push = (int16_t)-(f & 15);
            if (was_on_floor(e) || (int16_t)(push + fell) >= 0) { land(e, push, t); return; }
        }
        cell.col = ++col;
        if (--n < 0) return;
    }
}

static void floor_span_or_ceiling(Ent e)
{
    if ((coll(e) & 0x8A) != (BY_PLATFORM | ON_FLOOR) && rdw(VEL_Y(e)) < 0) { ceiling_span(e); return; }
    floor_span(e, feet(e), (int16_t)(rdw(Y(e)) - rdw(PREV_Y(e))), T_FLOOR);
}

/* $358C, the floor where there are slopes. Standing on the map, it follows the surface into the
 * next cell: the cell below, level or above, as the slope it stood on leads (codes at $418C and
 * $3772). In the air, it lands on a slope's surface or a flat top it fell through this frame.
 * Returns 1 when it has decided; 0 to go on to the plain floor check. */
static int floor_slopes(Ent e)
{
    int16_t f = feet(e), prev_f = (int16_t)(rdw(PREV_Y(e)) + half_h(e)), fell = (int16_t)(f - prev_f);
    if (!(rd8(AREA_FLAGS) & 2)) return 0;
    uint16_t col = col_of(rdw(X(e))), row = row_of(f);
    if (was_on_floor(e)) {
        static const uint8_t which[8] = {2, 3, 1, 3, 2, 6, 4, 6};   /* bit 0 below, 1 level, 2 above */
        int16_t crossed = (int16_t)((rdw(X(e)) & 0xFFF0) - (rdw(PREV_X(e)) & 0xFFF0));
        uint8_t checks = 2, below = 0, level = 0;
        int side = 0, reverse = 0;
        if (crossed) {
            uint8_t code = slope_exit(rdw(PREV_COLL_TYPE(e)), crossed > 0);
            below = (code + 4) & 7;                 /* what the next cell's slope must start with */
            level = (4 - code) & 7;
            checks = which[code & 7];
            side = crossed < 0;                     /* the side it enters the next cell from */
            reverse = is_player_a(e) && (rd8(PLAYER_FLAGS2) & 1);
        }
        for (int i = 0; i < 3; i++) {
            int k = reverse ? 2 - i : i;
            if (!(checks >> k & 1)) continue;
            uint16_t t;
            int16_t push;
            if (k == 0) {                           /* the cell below */
                t = type_at(e, col, (uint16_t)(row + 0x40));
                if (!(t & T_FLOOR) || slope_exit(t, side) != below) continue;
                push = (int16_t)((t & T_SLOPE ? slope_floor(e, t, f) : -(f & 15)) + 16);
            } else if (k == 1) {                    /* level */
                t = type_at(e, col, row);
                if (!(t & T_FLOOR) || (crossed && slope_exit(t, side) != level)) continue;
                push = t & T_SLOPE ? slope_floor(e, t, f) : (int16_t)-(f & 15);
            } else {                                /* the cell above: only a slope */
                t = type_at(e, col, (uint16_t)(row - 0x40));
                if ((t & (T_SLOPE | T_FLOOR)) != (T_SLOPE | T_FLOOR) || slope_exit(t, side) != below) continue;
                push = (int16_t)(slope_floor(e, t, f) - 16);
            }
            land(e, push, t);
            return 1;
        }
        return 0;
    }
    if ((int16_t)((prev_f & 0xFFF0) - (f & 0xFFF0)) < 0) {
        /* the feet moved into a lower row: first the row they came from */
        uint16_t t = type_at_pixel(e, rdw(X(e)), prev_f);
        if (t & T_FLOOR) {
            int ok = 1;
            int16_t top = 0;
            if (!(t & T_SLOPE)) top = (int16_t)-(prev_f & 15);
            else if (into_slope(e, t) < 0) ok = 0;
            else top = slope_floor(e, t, prev_f);
            if (ok) {
                int16_t push = (int16_t)(top - fell);
                if (push <= 0 && (int16_t)(push + fell) >= 0) { land(e, push, t); return 1; }
            }
        }
    }
    uint16_t t = type_at_pixel(e, rdw(X(e)), f);
    if (!(t & T_FLOOR)) return 0;
    if (!(t & T_SLOPE)) {
        int16_t push = (int16_t)-(f & 15);
        if ((int16_t)(push + fell) < 0) return 0;
        land(e, push, t);
        return 1;
    }
    if (into_slope(e, t) < 0) return 1;             /* leaving the slope: no floor this frame */
    int16_t push = slope_floor(e, t, f);
    if (push > 0 || (int16_t)(push + fell) < 0) return 1;
    land(e, push, t);
    return 1;
}

/* $3584: slopes, then the plain floor under every column */
static void floor_with_slopes(Ent e)
{
    wr32(FLOOR_THEN, 0x38E4);
    if (floor_slopes(e)) return;
    int16_t f = feet(e);
    floor_span(e, f, (int16_t)(f - (int16_t)(rdw(PREV_Y(e)) + half_h(e))), T_SLOPE | T_FLOOR);
}

/* $356E: slopes only, or without them the floor under the centre */
static void floor_slopes_or_centre(Ent e)
{
    if (!(rd8(AREA_FLAGS) & 2)) { floor_at_centre(e); return; }
    wr32(FLOOR_THEN, 0xFFFFFFFF);
    floor_slopes(e);
}

/* $3098: apply the vertical push, and stop (or bounce) */
static void end_vertical(Ent e)
{
    if ((coll(e) & 0xA2) != ON_FLOOR) return;       /* not carried, not by a platform */
    wrw(Y(e), rdw(Y(e)) + rdw(PUSH_Y(e)));
    wrw(Y_FRAC(e), 0);
    uint8_t mode = rd8(MODE(e));
    if (!(mode & 0x20)) wrw(VEL_Y(e), 0);
    else if (!(mode & 0x08)) wrw(VEL_Y(e), -rdw(VEL_Y(e)));   /* bounces */
}

/* ---- conveyors and currents ---- */

/* $3F08: standing on a conveyor (the tile below, or what it stood on last move) carries it */
static void conveyors(Ent e)
{
    if (rd8(MODE(e)) & 0x40) return;
    if ((rd8(PREV_COLL(e)) & (CEILING | ON_FLOOR)) != ON_FLOOR) return;
    uint16_t below = rd16(TILE_BELOW) & 0xF008, stood = rd16(COLL_TYPE(e)) & 0xF008;
    int8_t speed;
    if (below == 0x1008 || stood == 0x1008) speed = (int8_t)rd8(CONVEYOR_1);
    else if (below == 0x2008 || stood == 0x2008) speed = (int8_t)rd8(CONVEYOR_2);
    else return;
    wrw(X(e), rdw(X(e)) + speed);
}

/* $3E4C: water currents (tiles $Bxx0) push the player, up to a speed of 1.5 pixels a frame */
static void currents(Ent e)
{
    if (!(rd8(AREA_FLAGS) & 1)) return;
    uint16_t here = rd16(TILE_HERE);
    if ((here & 0xF000) != 0xB000) return;
    uint32_t push = 0x3EE0 + ((here & 0xF0) >> 3);  /* 16 directions: vertical, then horizontal */
    int16_t dy = (int16_t)rd16(push), dx = (int16_t)rd16(push + 8);
    int16_t vy = rdw(VEL_Y(e));
    if (dy > 0 && vy < 0x180) wrw(VEL_Y(e), vy + 3 * dy);   /* down three times as strong */
    if (dy < 0 && vy > -0x180) wrw(VEL_Y(e), vy + dy);
    int16_t vx = rdw(VEL_X(e));
    if ((dx > 0 && vx < 0x180) || (dx < 0 && vx > -0x180)) wrw(VEL_X(e), vx + dx);
}

/* ---- the entry points ---- */

/* the start of every move: sideways velocity, conveyors, and a fresh collision result */
static void begin_move(Ent e, int conveyor)
{
    apply_velocity(e, VEL_X(e), X(e), PREV_X(e));
    if (conveyor) conveyors(e);
    wr8(PREV_COLL(e), coll(e));
    wrw(PREV_COLL_TYPE(e), rdw(COLL_TYPE(e)));
    wr8(COLL(e), 0);
    wrw(COLL_TYPE(e), 0);
}

static void move_down(Ent e) { apply_velocity(e, VEL_Y(e), Y(e), PREV_Y(e)); }

/* stepped onto a slope sideways: that is the vertical move; note what is under the feet */
static int stepped_onto_slope(Ent e, int attr)
{
    if (!(coll(e) & SLOPE_STEP)) return 0;
    wrw(COLL_TYPE(e), type_at_pixel(e, rdw(X(e)), feet(e)));
    if (attr) wr8(COLL_ATTR(e), cell.attr);
    return 1;
}

/* $2E38: the player. Returns 1 if it stepped onto a slope (the move ends there). */
static int move_player(Ent e)
{
    begin_move(e, 1);
    platforms_sideways(e);
    walls_both_sides(e);
    end_sideways(e);
    if (stepped_onto_slope(e, 1)) return 1;
    move_down(e);
    platforms_vertical(e);
    ceiling_span(e);
    floor_with_slopes(e);
    end_vertical(e);
    return 0;
}

/* $2E9E: no slopes; walls and floor at the centre */
static void move_flat(Ent e)
{
    begin_move(e, 1);
    walls_at_centre(e, 0);
    end_sideways(e);
    move_down(e);
    floor_at_centre(e);
    end_vertical(e);
}

/* $2ED4, $2EDE, $2F34: walls at the centre, slopes; $2F34 also bumps its head */
static void move_on_slopes(Ent e, int conveyor, int ceiling)
{
    begin_move(e, conveyor);
    walls_at_centre_slopes(e);
    end_sideways(e);
    if (stepped_onto_slope(e, 0)) return;
    move_down(e);
    if (ceiling) ceiling_at_centre(e);
    floor_slopes_or_centre(e);
    end_vertical(e);
}

/* $2F8E: platforms, walls ahead, the floor under the whole box */
static void move_with_platforms(Ent e)
{
    begin_move(e, 1);
    platforms_sideways(e);
    walls_ahead(e);
    end_sideways(e);
    move_down(e);
    platforms_vertical(e);
    ceiling_span_rising(e);
    floor_span_or_ceiling(e);
    end_vertical(e);
}

/* $2FD0: platforms and slopes, like the player but without conveyors */
static void move_with_platforms_and_slopes(Ent e)
{
    begin_move(e, 0);
    platforms_sideways(e);
    walls_ahead(e);
    end_sideways(e);
    if (stepped_onto_slope(e, 0)) return;
    move_down(e);
    platforms_vertical(e);
    if ((int16_t)(rdw(Y(e)) - rdw(PREV_Y(e))) < 0) ceiling_span(e);
    else floor_with_slopes(e);
    end_vertical(e);
}

/* ---- the 68000 calls: the entity is in a4; the entry points leave plane A's layer in a3 ---- */
void PlatformsSideways(Cpu *c) { platforms_sideways(c->a[4]); }
void PlatformsVertical(Cpu *c) { platforms_vertical(c->a[4]); }

/* the player's caller keeps the last map cell looked at (d6, d7) as the last cell stood on */
void MovePlayer(Cpu *c)
{
    int stepped = move_player(c->a[4]);
    c->a[3] = LAYER_A;
    c->d[6] = (stepped ? c->d[6] & 0xFFFF0000u : 0) | cell.col;   /* the long scans clear d6 first */
    c->d[7] = cell.row;
}
void MoveFlat(Cpu *c)                  { move_flat(c->a[4]); c->a[3] = LAYER_A; }
void MoveOnSlopes(Cpu *c)              { move_on_slopes(c->a[4], 1, 0); c->a[3] = LAYER_A; }
void MoveOnSlopesNoConveyor(Cpu *c)    { move_on_slopes(c->a[4], 0, 0); c->a[3] = LAYER_A; }
void MoveOnSlopesWithCeiling(Cpu *c)   { move_on_slopes(c->a[4], 1, 1); c->a[3] = LAYER_A; }
void MoveWithPlatforms(Cpu *c)         { move_with_platforms(c->a[4]); c->a[3] = LAYER_A; }
void MoveWithPlatformsAndSlopes(Cpu *c) { move_with_platforms_and_slopes(c->a[4]); c->a[3] = LAYER_A; }
void Currents(Cpu *c)                  { currents(c->a[4]); }

/* The steps, registered too: the game only calls them from the entry points above (so from C
 * now), but the harness can fuzz each one alone and counts its coverage. They expect a3 = plane
 * A's layer, as the entry points set it. */
void EndSideways(Cpu *c)         { end_sideways(c->a[4]); }
void EndVertical(Cpu *c)         { end_vertical(c->a[4]); }
void WallsBothSides(Cpu *c)      { walls_both_sides(c->a[4]); }
void WallsAhead(Cpu *c)          { walls_ahead(c->a[4]); }
void WallsAtCentreSlopes(Cpu *c) { walls_at_centre_slopes(c->a[4]); }
void WallsAtCentre(Cpu *c)       { walls_at_centre(c->a[4], 0); }
void SlopeWalk(Cpu *c)           { slope_walk(c->a[4]); }
void FloorSlopesOrCentre(Cpu *c) { floor_slopes_or_centre(c->a[4]); }
void FloorWithSlopes(Cpu *c)     { floor_with_slopes(c->a[4]); }
void FloorSpanOrCeiling(Cpu *c)  { floor_span_or_ceiling(c->a[4]); }
void FloorAtCentre(Cpu *c)       { floor_at_centre(c->a[4]); }
void CeilingSpanRising(Cpu *c)   { ceiling_span_rising(c->a[4]); }
void CeilingSpan(Cpu *c)         { ceiling_span(c->a[4]); }
void CeilingAtCentre(Cpu *c)     { ceiling_at_centre(c->a[4]); }
void Conveyors(Cpu *c)           { conveyors(c->a[4]); }

/* ---- for the other rewritten files (world.h) ---- */
uint16_t world_tile_type(uint8_t attr, Ent e) { return tile_type(attr, e); }
uint16_t world_type_at_pixel(Ent e, int16_t x, int16_t y) { return type_at_pixel(e, x, y); }
