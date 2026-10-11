/* Movement with collision, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * Unlike the rest of port/, this is not an instruction-by-instruction transcription: it is
 * written to be read, with named fields and plain control flow, and it does not reproduce the
 * 68000's leftover registers. Each routine has a contract (port.h, registry.c): what the game
 * actually uses after it returns (here, nothing but the stack pointer); the harness checks
 * memory exactly and watches that every register left different is dead.
 *
 * So far: collision with the moving platforms ($3AFC-$3E1E). The rest of the movement routines
 * ($2E38-$3AFB) are still the translator's, and call these. */
#include "port.h"

/* ---- entities: fields are arrays of 256 bytes, indexed by the entity's offset (0-$FC). As in the
 * original, a field is the entity (a4) plus a negative displacement, in 32 bits; the bus then
 * keeps 24 (so $FFFFC800 + e is work RAM). Platforms are addressed the same way from a2. */
typedef uint32_t Ent;
static int16_t  rdw(uint32_t a) { return (int16_t)rd16(a); }
static void     wrw(uint32_t a, int v) { wr16(a, (uint16_t)v); }

#define X(e)          ((e) - 0x3800)   /* position, whole pixels */
#define Y(e)          ((e) - 0x3700)
#define Y_FRAC(e)     ((e) - 0x36FE)
#define VEL_Y(e)      ((e) - 0x35FE)   /* 8.8 */
#define HALF_W(e)     ((e) - 0x34FE)   /* byte */
#define HALF_H(e)     ((e) - 0x34FD)   /* byte */
#define COLL(e)       ((e) - 0x3100)   /* collision result of this move (byte, bits below) */
#define COLL_TYPE(e)  ((e) - 0x30FE)   /* collision type of what it stands on (word) */
#define PUSH_X(e)     ((e) - 0x3000)   /* correction to apply to X after the sideways checks */
#define PUSH_Y(e)     ((e) - 0x2FFE)   /* ... and to Y */
#define MODE(e)       ((e) - 0x2F00)   /* physics mode; bit 6: no collision with platforms */
#define PREV_COLL(e)  ((e) - 0x2EFF)   /* last move's collision result */
#define PREV_X(e)     ((e) - 0x2E00)
#define PREV_Y(e)     ((e) - 0x2DFE)
#define PLATFORM(e)   ((e) - 0x2D00)   /* the platform it stands on (its record offset) */

/* collision result bits */
enum { HIT_LEFT = 0x01, ON_FLOOR = 0x02, HIT_RIGHT_SIDE = 0x04, HIT_CEILING = 0x08, CARRIED = 0x20,
       BY_PLATFORM = 0x80 };

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
#define AREA_FLAGS    0xFFFF966Bu
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

/* ---- the 68000 calls: the entity is in a4 ---- */
void PlatformsSideways(Cpu *c) { platforms_sideways(c->a[4]); }
void PlatformsVertical(Cpu *c) { platforms_vertical(c->a[4]); }
