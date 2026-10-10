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

/* word arithmetic with the 68000's condition codes (M68000 PRM, section 3). Each returns the
 * 16-bit result and updates *ccr exactly as the instruction would. */
static inline uint16_t cc_nz_w(uint32_t r) { return (uint16_t)((r & 0x8000 ? CC_N : 0) | ((r & 0xFFFF) ? 0 : CC_Z)); }

static inline uint32_t add_w(uint16_t *ccr, uint32_t src, uint32_t dst)     /* add.w src,dst */
{
    uint32_t s = src & 0xFFFF, d = dst & 0xFFFF, r = (s + d) & 0xFFFF;
    int c = s + d > 0xFFFF;
    int v = ((s ^ r) & (d ^ r) & 0x8000) != 0;
    *ccr = (uint16_t)(cc_nz_w(r) | (v ? CC_V : 0) | (c ? CC_C | CC_X : 0));
    return r;
}
static inline uint32_t sub_w(uint16_t *ccr, uint32_t src, uint32_t dst)     /* sub.w src,dst */
{
    uint32_t s = src & 0xFFFF, d = dst & 0xFFFF, r = (d - s) & 0xFFFF;
    int c = s > d;
    int v = ((s ^ d) & (r ^ d) & 0x8000) != 0;
    *ccr = (uint16_t)(cc_nz_w(r) | (v ? CC_V : 0) | (c ? CC_C | CC_X : 0));
    return r;
}
static inline void cmp_w(uint16_t *ccr, uint32_t src, uint32_t dst)         /* cmp.w src,dst: X kept */
{
    uint16_t x = *ccr & CC_X;
    sub_w(ccr, src, dst);
    *ccr = (uint16_t)((*ccr & ~CC_X) | x);
}
static inline uint32_t neg_w(uint16_t *ccr, uint32_t dst)                   /* neg.w dst */
{
    return sub_w(ccr, dst, 0);
}
static inline uint32_t asr_w1(uint16_t *ccr, uint32_t dst)                  /* asr.w #1,dst */
{
    uint32_t d = dst & 0xFFFF, r = ((d >> 1) | (d & 0x8000)) & 0xFFFF;
    *ccr = (uint16_t)(cc_nz_w(r) | (d & 1 ? CC_C | CC_X : 0));
    return r;
}
static inline uint16_t tst_w(uint16_t ccr, uint32_t v) { return (uint16_t)((ccr & CC_X) | cc_nz_w(v)); }
/* byte versions where routines need them */
static inline uint32_t add_b(uint16_t *ccr, uint32_t src, uint32_t dst)     /* add.b / addq.b */
{
    uint32_t s = src & 0xFF, d = dst & 0xFF, r = (s + d) & 0xFF;
    int c = s + d > 0xFF;
    int v = ((s ^ r) & (d ^ r) & 0x80) != 0;
    *ccr = (uint16_t)((r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v ? CC_V : 0) | (c ? CC_C | CC_X : 0));
    return r;
}
static inline uint32_t sub_b(uint16_t *ccr, uint32_t src, uint32_t dst)     /* sub.b / subq.b */
{
    uint32_t s = src & 0xFF, d = dst & 0xFF, r = (d - s) & 0xFF;
    int v = ((s ^ d) & (r ^ d) & 0x80) != 0;
    *ccr = (uint16_t)((r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v ? CC_V : 0) | (s > d ? CC_C | CC_X : 0));
    return r;
}
static inline uint16_t tst_b(uint16_t ccr, uint32_t v)
{
    return (uint16_t)((ccr & CC_X) | (v & 0x80 ? CC_N : 0) | ((v & 0xFF) ? 0 : CC_Z));
}
static inline uint32_t ext_w(uint32_t r) { return set_w(r, (uint32_t)(int32_t)(int8_t)(r & 0xFF)); }

/* logical shifts of a byte, word or long (bits = 8, 16, 32) by n, with the 68000's flags:
 * C and X get the last bit shifted out; a count of 0 clears C and leaves X; V is cleared */
static inline uint32_t lsl_n(uint16_t *ccr, uint32_t v, unsigned n, unsigned bits)
{
    uint64_t mask = (1ull << bits) - 1, x = v & mask;
    uint32_t r = n >= bits ? 0 : (uint32_t)((x << n) & mask);
    uint16_t c = n == 0 ? 0 : n > bits ? 0 : (uint16_t)(x >> (bits - n) & 1);
    uint16_t xf = n == 0 ? (*ccr & CC_X) : (c ? CC_X : 0);
    *ccr = (uint16_t)(xf | (c ? CC_C : 0) | (r >> (bits - 1) & 1 ? CC_N : 0) | (r ? 0 : CC_Z));
    return r;
}
static inline uint32_t lsr_n(uint16_t *ccr, uint32_t v, unsigned n, unsigned bits)
{
    uint64_t mask = (1ull << bits) - 1, x = v & mask;
    uint32_t r = n >= bits ? 0 : (uint32_t)(x >> n);
    uint16_t c = n == 0 ? 0 : n > bits ? 0 : (uint16_t)(x >> (n - 1) & 1);
    uint16_t xf = n == 0 ? (*ccr & CC_X) : (c ? CC_X : 0);
    *ccr = (uint16_t)(xf | (c ? CC_C : 0) | (r >> (bits - 1) & 1 ? CC_N : 0) | (r ? 0 : CC_Z));
    return r;
}
/* arithmetic shift left: like lsl, but V is set if the sign bit changes at any step */
static inline uint32_t asl_n(uint16_t *ccr, uint32_t v, unsigned n, unsigned bits)
{
    uint64_t mask = (1ull << bits) - 1, x = v & mask;
    int vflag;                                      /* the top n+1 bits are not all equal */
    if (n == 0) vflag = 0;
    else if (n >= bits) vflag = x != 0;
    else { uint64_t top = x >> (bits - 1 - n), all = (1ull << (n + 1)) - 1; vflag = top != 0 && top != all; }
    uint32_t r = lsl_n(ccr, v, n, bits);
    *ccr = (uint16_t)((*ccr & ~CC_V) | (vflag ? CC_V : 0));
    return r;
}
static inline uint32_t add_l(uint16_t *ccr, uint32_t s, uint32_t d)          /* add.l */
{
    uint32_t r = s + d;
    int c = r < s;
    int v = ((s ^ r) & (d ^ r) & 0x80000000u) != 0;
    *ccr = (uint16_t)((r & 0x80000000u ? CC_N : 0) | (r ? 0 : CC_Z) | (v ? CC_V : 0) | (c ? CC_C | CC_X : 0));
    return r;
}
static inline uint16_t tst_l(uint16_t ccr, uint32_t v) { return cc_move_l(ccr, v); }
static inline void cmp_l(uint16_t *ccr, uint32_t s, uint32_t d)              /* cmp.l / cmpa: X kept */
{
    uint32_t r = d - s;
    int v = ((s ^ d) & (r ^ d) & 0x80000000u) != 0;
    *ccr = (uint16_t)((*ccr & CC_X) | (r & 0x80000000u ? CC_N : 0) | (r ? 0 : CC_Z) | (v ? CC_V : 0) | (s > d ? CC_C : 0));
}
static inline void cmp_b(uint16_t *ccr, uint32_t src, uint32_t dst)         /* cmp.b / cmpi.b: X kept */
{
    uint32_t s = src & 0xFF, d = dst & 0xFF, r = (d - s) & 0xFF;
    int v = ((s ^ d) & (r ^ d) & 0x80) != 0;
    *ccr = (uint16_t)((*ccr & CC_X) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v ? CC_V : 0) | (s > d ? CC_C : 0));
}

/* the 68000 pushes a return address when one routine calls another; a port that inlines the
 * call must leave the same long below the stack pointer for memory to match */
static inline void stack_residue(Cpu *c, uint32_t ret) { wr32(c->a[7] - 4, ret); }

/* the registry of ported routines */
typedef void (*PortFn)(Cpu *);
typedef struct { uint32_t addr, end; const char *name; PortFn fn; } PortRoutine;
extern const PortRoutine port_routines[];
extern const int port_routine_count;

#endif
