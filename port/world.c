/* Shared by the idiomatic rewrite's files (world.h). */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

int rt_dispatch(Cpu *c, uint32_t target);       /* the generated code's (work/gen) */

#define RETURN_MARK 0x00FFFFFEu                 /* a return address no code has */

void call_68k(Cpu *t, uint32_t addr)
{
    t->a[7] -= 4; wr32(t->a[7], RETURN_MARK);
    if (!rt_dispatch(t, addr) || t->ret != RETURN_MARK) {
        fprintf(stderr, "call_68k: $%06X did not return normally (control went to $%06X)\n", (unsigned)addr, (unsigned)t->ret);
        exit(3);
    }
}

void rt_leave(Cpu *c, uint32_t from, uint32_t target);

/* the same for a routine that keeps its return address in RAM and goes on there later (the
 * message interpreter at $8CD6, say): the original's return address pushed, so that RAM is the
 * same, and the routine's C version run directly; it must come back there */
void call_68k_ret(Cpu *t, uint32_t addr, uint32_t ret)
{
    const PortRoutine *p = 0;
    for (int i = 0; i < port_routine_count && !p; i++) if (port_routines[i].addr == addr) p = &port_routines[i];
    t->a[7] -= 4; wr32(t->a[7], ret);
    t->ret = 0;
    if (p) {
        p->fn(t);
        if (!t->ret) { t->ret = rd32(t->a[7]); t->a[7] += 4; }
        else if (t->ret != ret) {
            uint32_t to = t->ret;
            if (to & RT_TO_68K) rt_leave(t, 0, to & 0xFFFFFF);
            else if (!rt_dispatch(t, to)) rt_leave(t, 0, to);
        }
    } else if (!rt_dispatch(t, addr)) rt_leave(t, 0, addr);
    if (t->ret != ret) {
        fprintf(stderr, "call_68k_ret: $%06X did not come back to $%06X (control went to $%06X)\n", (unsigned)addr, (unsigned)ret, (unsigned)t->ret);
        exit(3);
    }
}
