/* What the idiomatic rewrite's files share: entity fields, the map's collision types, and calls
 * into code that is not rewritten yet. Entities are addressed as in the original: a field is the
 * entity's offset (0-$FC) plus a negative displacement, in 32 bits, which the bus keeps to 24. */
#ifndef WORLD_H
#define WORLD_H
#include "port.h"

typedef uint32_t Ent;

static inline int16_t rdw(uint32_t a) { return (int16_t)rd16(a); }
static inline void    wrw(uint32_t a, int v) { wr16(a, (uint16_t)v); }
static inline void    set8(uint32_t a, uint8_t bits) { wr8(a, rd8(a) | bits); }
static inline void    clr8(uint32_t a, uint8_t bits) { wr8(a, rd8(a) & (uint8_t)~bits); }

/* movement.c: the collision type of a map attribute byte ($30F4), and of the map at a pixel of
 * plane A ($30CE) */
uint16_t world_tile_type(uint8_t attr, Ent e);
uint16_t world_type_at_pixel(Ent e, int16_t x, int16_t y);

/* Run a routine that is still 68000 code (generated or ported) from ordinary C: the registers go
 * in and come out through t, whose a7 must be a real stack. Stops the program if the routine does
 * not come back (it never should for the routines called this way). */
void call_68k(Cpu *t, uint32_t addr);

#endif
