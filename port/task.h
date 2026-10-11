/* Task passes on coroutines (port/task.c). */
#ifndef TASK_H
#define TASK_H
#include "port.h"

/* From ordinary C running on a task pass (c: the pass's own registers, as the generated code
 * passes them down): wait for the next frame, as the original does by calling Yield from here.
 * `resume` is the address the original stores as the slot's resume address at that point; if
 * the next pass of the slot starts anywhere else, the wait is abandoned and the original's code
 * at that address runs instead. */
void task_wait_frame(Cpu *c, uint32_t resume);

/* End this pass here and abandon the call chain, as the original does when it yields after
 * resetting the stack: the scheduler carries on, and the slot's next pass starts afresh at
 * `resume` (stored in the slot first), or at its resume address as it is if 0. Does not return. */
void task_end_pass(Cpu *c, uint32_t resume);

/* the machine was reset or a state loaded: forget every waiting pass */
void task_reset(void);

#endif
