/* Task passes on coroutines, so that ordinary C can wait for the next frame from anywhere in a
 * call chain (docs/PORT.md, "Coroutines").
 *
 * The original's scheduler runs each task slot's pass with `jsr (a0)` to the slot's resume
 * address (slot + $C): $4BD6 for the four system slots at $FF8048, $4C88 for the sixteen user
 * slots at $FF8248, $80 bytes apart. A pass ends when the task returns, usually through Yield
 * ($400), which stores the address after its call as the slot's resume address. Tasks are
 * stackless: all they keep between passes is that address and RAM.
 *
 * Here both `jsr (a0)` are ported: each slot's pass runs on that slot's coroutine. Ordinary C
 * that wants to wait for the next frame part-way through (task_wait_frame) stores its resume
 * address in the slot, as Yield would, keeps the part of the 68000 stack its call chain uses,
 * and suspends the coroutine; the scheduler carries on as if the pass had ended. When the
 * scheduler next runs the slot at that same address, the coroutine carries on where it stopped.
 * Anything else (another address, a loaded state, a reset) discards it and starts the pass
 * afresh at the slot's address, which is exactly what the original does: so the original's own
 * state in RAM always decides. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "coro.h"
#include "task.h"

int rt_dispatch(Cpu *c, uint32_t target);
void rt_leave(Cpu *c, uint32_t from, uint32_t target);

#define SLOTS 20
#define STACK_KEEP 0x400                        /* bytes of 68000 stack a suspended chain may use */

typedef struct {
    Coro *co;
    Cpu cpu;                                    /* the pass's registers: the generated code's */
    uint32_t target, cont, base;                /* where the pass starts, returns to; its stack */
    int suspended;
    uint32_t wait_at, wait_sp;                  /* suspended: its resume address and stack pointer */
    uint32_t yield_a0;                          /* a0 as Yield leaves it: the resume address stored */
    uint8_t stack[STACK_KEEP];
} Slot;

static Slot slots[SLOTS];
static Slot *current;
static unsigned generation, slot_generation[SLOTS];

static int slot_index(uint32_t a5)
{
    uint16_t a = (uint16_t)a5;
    if (a >= 0x8048 && a < 0x8048 + 4 * 0x80 && !((a - 0x8048) & 0x7F)) return (a - 0x8048) / 0x80;
    if (a >= 0x8248 && a < 0x8248 + 16 * 0x80 && !((a - 0x8248) & 0x7F)) return 4 + (a - 0x8248) / 0x80;
    return -1;
}

static void pass_body(void *arg)
{
    Slot *s = arg;
    for (;;) {
        if (!rt_dispatch(&s->cpu, s->target)) rt_leave(&s->cpu, s->cont, s->target);
        coro_yield();                           /* the pass is over */
    }
}

static void run_pass(Cpu *c, uint32_t cont)
{
    int i = slot_index(c->a[5]);
    uint32_t target = c->a[0];
    if (i < 0) {                                /* not a slot: run it as the original would */
        c->a[7] -= 4; wr32(c->a[7], cont);
        port_continue_at(c, target, 0);
        return;
    }
    Slot *s = &slots[i];
    if (slot_generation[i] != generation) {     /* a state was loaded, or the machine reset */
        coro_free(s->co); s->co = NULL; s->suspended = 0;
        slot_generation[i] = generation;
    }
    int resume = s->suspended && s->wait_at == target && c->a[7] == s->base;
    if (s->suspended && !resume) {              /* moved on without it: start afresh */
        coro_free(s->co); s->co = NULL; s->suspended = 0;
    }
    if (resume) {
        for (uint32_t k = 0; k < s->base - s->wait_sp; k++) wr8(s->wait_sp + k, s->stack[k]);
        s->cpu.a[7] = s->wait_sp;
        s->suspended = 0;
    } else {
        if (!s->co) s->co = coro_new(pass_body, s, 1 << 20);
        s->cpu = *c;
        s->target = target; s->cont = cont; s->base = c->a[7];
        s->cpu.a[7] -= 4; wr32(s->cpu.a[7], cont);   /* jsr (a0) */
    }
    Slot *outer = current;
    current = s;
    coro_resume(s->co);
    current = outer;
    if (s->suspended) {                         /* waiting for the next frame: Yield's return */
        c->a[0] = s->yield_a0;
        c->a[7] = s->base;
        port_continue_at(c, cont, 0);
        return;
    }
    *c = s->cpu;                                /* the pass is over */
    port_continue_at(c, c->ret ? c->ret : cont, 0);
}

/* $4BD6 and $4C88: the scheduler's `jsr (a0)` for a system and a user slot */
void TaskCallSystem(Cpu *c) { run_pass(c, 0x4BD8); }
void TaskCallUser(Cpu *c)   { run_pass(c, 0x4C8A); }

void task_wait_frame(Cpu *c, uint32_t resume)
{
    Slot *s = current;
    if (!s || c != &s->cpu || s->base - c->a[7] > STACK_KEEP) {
        fprintf(stderr, "task_wait_frame: not on a task pass's own registers, or too deep a stack\n");
        exit(3);
    }
    wr32((uint32_t)(int32_t)(int16_t)rd16(0xFFFF8A4Cu) + 0xC, resume);   /* the slot's resume address */
    s->wait_at = resume; s->wait_sp = c->a[7]; s->yield_a0 = resume;
    for (uint32_t k = 0; k < s->base - s->wait_sp; k++) s->stack[k] = rd8(s->wait_sp + k);
    s->suspended = 1;
    coro_yield();
}

void task_end_pass(Cpu *c, uint32_t resume)
{
    Slot *s = current;
    if (!s || c != &s->cpu) {
        fprintf(stderr, "task_end_pass: not on a task pass's own registers\n");
        exit(3);
    }
    if (resume) wr32((uint32_t)(int32_t)(int16_t)rd16(0xFFFF8A4Cu) + 0xC, resume);
    s->wait_at = 0;                             /* nothing to come back to: the next pass starts afresh */
    s->yield_a0 = resume ? resume : c->a[0];
    s->wait_sp = s->base;
    s->suspended = 1;
    coro_yield();
    fprintf(stderr, "task_end_pass: resumed\n");   /* never: run_pass discards it */
    exit(3);
}

int task_on_pass(const Cpu *c) { return current && c == &current->cpu; }

void task_reset(void) { generation++; }
