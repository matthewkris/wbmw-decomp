/* The port: game routines rewritten in C.
 *
 * A ported routine is a C function that does exactly what one 68000 routine does: same
 * registers out, same condition codes, same memory writes (including what it leaves on the
 * stack), so it can replace the original one routine at a time while the rest of the game still
 * runs as 68000 code in the harness. It sees the machine only through this header: the register
 * file below and the memory accessors, which the host provides (the harness today, a native
 * machine later). Nothing in port/ knows about Musashi or the harness.
 *
 * Exactness is checked by the harness's shadow mode (harness/hybrid.c), which runs both versions
 * of every call and compares the results. */
#ifndef PORT_H
#define PORT_H
#include <stdint.h>

typedef struct {
    uint32_t d[8], a[8];      /* a[7] is the active stack pointer */
    uint16_t ccr;             /* condition codes: X N Z V C in bits 4..0 */
} Cpu;

enum { CC_C = 1, CC_V = 2, CC_Z = 4, CC_N = 8, CC_X = 16 };

/* memory, provided by the host; addresses are 68000 addresses (24 bits used) */
uint8_t rd8(uint32_t a);
uint16_t rd16(uint32_t a);
void wr8(uint32_t a, uint8_t v);
void wr16(uint32_t a, uint16_t v);
static inline uint32_t rd32(uint32_t a) { return (uint32_t)rd16(a) << 16 | rd16(a + 2); }
static inline void wr32(uint32_t a, uint32_t v) { wr16(a, (uint16_t)(v >> 16)); wr16(a + 2, (uint16_t)v); }

/* d(An): a 16-bit signed displacement, as the 68000 adds it */
static inline uint32_t ea(uint32_t base, int16_t disp) { return base + (uint32_t)(int32_t)disp; }

/* replace the low byte / word of a register, keeping the rest (move.b / move.w into Dn) */
static inline uint32_t set_b(uint32_t r, uint32_t v) { return (r & 0xFFFFFF00u) | (v & 0xFF); }
static inline uint32_t set_w(uint32_t r, uint32_t v) { return (r & 0xFFFF0000u) | (v & 0xFFFF); }

/* condition codes after a move or logic op: N and Z from the value, V and C clear, X kept */
static inline uint16_t cc_move_w(uint16_t ccr, uint32_t v)
{
    v &= 0xFFFF;
    return (uint16_t)((ccr & CC_X) | (v & 0x8000 ? CC_N : 0) | (v ? 0 : CC_Z));
}
static inline uint16_t cc_move_l(uint16_t ccr, uint32_t v)
{
    return (uint16_t)((ccr & CC_X) | (v & 0x80000000u ? CC_N : 0) | (v ? 0 : CC_Z));
}

/* the 68000 pushes a return address when one routine calls another; a port that inlines the
 * call must leave the same long below the stack pointer for memory to match */
static inline void stack_residue(Cpu *c, uint32_t ret) { wr32(c->a[7] - 4, ret); }

/* the registry of ported routines */
typedef void (*PortFn)(Cpu *);
typedef struct { uint32_t addr; const char *name; PortFn fn; } PortRoutine;
extern const PortRoutine port_routines[];
extern const int port_routine_count;

#endif
