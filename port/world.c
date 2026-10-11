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
        fprintf(stderr, "call_68k: $%06X did not return normally\n", (unsigned)addr);
        exit(3);
    }
}
