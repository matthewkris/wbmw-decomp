/* Coroutines (coro.h): Windows fibers, or POSIX ucontext elsewhere. One level only: a coroutine
 * yields back to the code that resumed it, which is on the main stack. */
#include <stdlib.h>
#include "coro.h"

#ifdef _WIN32
#include <windows.h>

struct Coro { void *fiber, *caller; void (*fn)(void *); void *arg; int done; };
static Coro *running;

static void WINAPI fiber_main(void *p)
{
    Coro *co = p;
    co->fn(co->arg);
    co->done = 1;
    for (;;) SwitchToFiber(co->caller);             /* a fiber must never return */
}

Coro *coro_new(void (*fn)(void *), void *arg, size_t stack)
{
    Coro *co = calloc(1, sizeof *co);
    co->fn = fn; co->arg = arg;
    co->fiber = CreateFiber(stack, fiber_main, co);
    if (!co->fiber) { free(co); return NULL; }
    return co;
}

int coro_resume(Coro *co)
{
    static void *main_fiber;
    if (!main_fiber) main_fiber = ConvertThreadToFiber(NULL);   /* the first time */
    void *self = running ? running->fiber : main_fiber;
    Coro *outer = running;
    co->caller = self; running = co;
    SwitchToFiber(co->fiber);
    running = outer;
    return !co->done;
}

void coro_yield(void) { SwitchToFiber(running->caller); }

void coro_free(Coro *co)
{
    if (!co) return;
    DeleteFiber(co->fiber);
    free(co);
}

#else
#include <stdint.h>
#include <ucontext.h>

struct Coro { ucontext_t ctx, caller; void (*fn)(void *); void *arg; int done; void *stack; };
static Coro *running;

static void ctx_main(unsigned hi, unsigned lo)
{
    Coro *co = (Coro *)(((uintptr_t)hi << 32) | lo);
    co->fn(co->arg);
    co->done = 1;
    swapcontext(&co->ctx, &co->caller);
}

Coro *coro_new(void (*fn)(void *), void *arg, size_t stack)
{
    Coro *co = calloc(1, sizeof *co);
    co->fn = fn; co->arg = arg; co->stack = malloc(stack);
    getcontext(&co->ctx);
    co->ctx.uc_stack.ss_sp = co->stack; co->ctx.uc_stack.ss_size = stack; co->ctx.uc_link = NULL;
    uintptr_t p = (uintptr_t)co;
    makecontext(&co->ctx, (void (*)(void))ctx_main, 2, (unsigned)(p >> 32), (unsigned)p);
    return co;
}

int coro_resume(Coro *co)
{
    Coro *outer = running;
    running = co;
    swapcontext(&co->caller, &co->ctx);
    running = outer;
    return !co->done;
}

void coro_yield(void) { swapcontext(&running->ctx, &running->caller); }

void coro_free(Coro *co)
{
    if (!co) return;
    free(co->stack);
    free(co);
}
#endif

Coro *coro_current(void) { return running; }
