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
void call_68k_ret(Cpu *t, uint32_t addr, uint32_t ret);   /* for a routine that keeps its return address */

/* $48C4, DespawnIfOffscreen, for the enemy in a4 (objects.c): the original returns past its
 * caller when it removes the enemy; this answers 1 then, and the caller must return */
int world_despawn_offscreen(Cpu *c);

/* Widescreen (the native build only: the harness leaves these alone, so the game is the
 * original's). port_wide_px: pixels the host shows beyond each side of the 256; port_wide_room:
 * whether the room (area, number: $FF9668, $FF9669) is drawn for it, the host's choice. Where
 * both say so, rooms.c draws the layers' columns for the wider view and keeps the view inside
 * their maps, sprites.c draws what is in it, and kernhelp.c's off-screen tests count it as the
 * screen, so things appear, wake and go away at its edges (docs/WIDESCREEN.md). */
extern int port_wide_px;
extern int (*port_wide_room)(int area, int room);
int port_wide_now(void);            /* rooms.c: port_wide_px where the room allows it, else 0 */

/* Leave a rewritten routine the way the original does when it jumps away instead of returning
 * (into code not rewritten yet, e.g. one that resets the stack and yields): the caller carries on
 * at addr with c's registers and stack (m68k_rt.h, rt_after_hand). A routine that does this must
 * return at once. `pushed`: return addresses the original would have on the stack there, from
 * nested calls the C does not make (outermost first), or 0. */
static inline void port_continue_at(Cpu *c, uint32_t addr, uint32_t pushed)
{
    if (pushed) { c->a[7] -= 4; wr32(c->a[7], pushed); }
    c->ret = addr;
}
/* the same, but on the 68000 even where the address has a C version (the harness only): for a
 * routine that cannot run here, such as one that waits for frames outside a task pass */
#define RT_TO_68K 0x80000000u
static inline void port_leave_to_68k(Cpu *c, uint32_t addr) { c->ret = addr | RT_TO_68K; }

#endif
