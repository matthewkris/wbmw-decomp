/* Coroutines: functions that run on their own stack and can stop part-way (coro_yield) to be
 * carried on later (coro_resume). Used to run the game's task passes, so that ordinary C can wait
 * for the next frame from inside a call chain (port/task.c). Windows fibers, or ucontext. */
#ifndef CORO_H
#define CORO_H
#include <stddef.h>

typedef struct Coro Coro;

/* a coroutine that will run fn(arg) on a stack of `stack` bytes; it starts at the first resume */
Coro *coro_new(void (*fn)(void *), void *arg, size_t stack);
/* run it until it yields or fn returns; returns 1 if it yielded, 0 if fn has returned */
int coro_resume(Coro *co);
/* from inside a coroutine: back to whoever resumed it */
void coro_yield(void);
/* the coroutine running now, or NULL on the main stack */
Coro *coro_current(void);
void coro_free(Coro *co);

#endif
