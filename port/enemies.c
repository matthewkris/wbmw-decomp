/* The enemies' framework, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * Each frame the gameplay loop runs $133B2: first the spawn points ($12A70), then each of the
 * sixteen enemy slots (from $FFA11A, usually $40) by its kind. A kind is a word: the high byte
 * picks a group (0, 2 .. 8; with bit 7 the slot is skipped), the low byte the kind in the group;
 * the tables at $13418 and $13422 give each kind a handler. Group 0's handler for most kinds is
 * the monsters' state machine (`EnemyDispatch`, $13582): a monster's state (STEP, 0-8) and its
 * kind pick an action routine from the tables at $13B70 and $13DA4.
 *
 * The handlers and actions are still the original's code: they run on the caller's registers,
 * as in the original (what one enemy's code leaves in a register is what the next one's sees). */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

#define FIRST_ENEMY   0xFFFFA11Au   /* word: the first enemy slot */
#define TASK_FLAGS    0xFFFF9659u   /* bit 3: only kinds of groups above 2 run */
#define GROUPS        0x13418u      /* per group: the offset of its kinds' handler numbers */
#define HANDLERS      0x13422u      /* handler addresses (longs) */
#define KIND_STATES   0x13B70u      /* per monster kind: the offset of its action per state */
#define STATE_ACTIONS 0x13DA4u      /* per state: the offset of its action addresses (longs) */
#define NEXT_STATES   0x13FD2u      /* per variant, 9 states: the state that follows */

#define ALIVE(e)      ((e) - 0x4000)
#define STEP(e)       ((e) - 0x2BFF)   /* the monster's state; bit 7 once begun */
#define TYPE(e)       ((e) - 0x2BFE)   /* word: group, kind */
#define TOUCH(e)      ((e) - 0x2AFD)
#define NEXT_STATE(e) ((e) - 0x2200)   /* byte: a state to change to, or $FF */
#define VARIANT(e)    ((e) - 0x25FD)
#define MAGIC_HIT(e)  ((e) - 0x21FF)
#define MAGIC_DMG(e)  ((e) - 0x21FE)

/* $13582: the action for this monster's kind and state. Leaves the registers as the original
 * does on its jmp (a0): d0 = action number * 4, d1 = state * 2, a0 = the action. */
static uint32_t monster_action(Cpu *c)
{
    Ent e = c->a[4];
    uint8_t kind = rd8(TYPE(e) + 1), state = rd8(STEP(e)) & 0x7F;
    uint32_t row = KIND_STATES + (uint32_t)(int32_t)rdw(KIND_STATES + kind);
    uint8_t action = rd8(row + state);
    uint32_t list = STATE_ACTIONS + (uint32_t)(int32_t)rdw(STATE_ACTIONS + 2u * state);
    uint16_t cc = c->ccr;
    c->d[0] = lsl_n(&cc, action, 2, 16);              /* d0's upper word was cleared */
    c->d[1] = set_w(c->d[1], 2u * state);
    c->ccr = cc;
    c->a[0] = rd32(list + 4u * action);
    return c->a[0];
}

void EnemyDispatch(Cpu *c) { port_continue_at(c, monster_action(c), 0); }

/* $1357E: a new state (d0), then its action */
void EnemySetState(Cpu *c)
{
    wr8(STEP(c->a[4]), (uint8_t)c->d[0]);
    EnemyDispatch(c);
}

/* $13542: the state that follows: a pending one (NEXT_STATE), else the next of this state for
 * the monster's variant (none if negative); NEXT_STATE is then cleared */
void EnemyNextState(Cpu *c)
{
    Ent e = c->a[4];
    uint16_t cc = c->ccr;
    uint8_t next = rd8(NEXT_STATE(e));
    c->d[0] = set_b(c->d[0], next);
    cmp_b(&cc, 0xFF, next);
    if (next == 0xFF) {
        c->a[0] = NEXT_STATES;
        uint8_t v = rd8(VARIANT(e));
        c->d[0] = v; cc = tst_b(cc, v);
        if (v & 0x80) { c->ccr = cc; return; }
        uint16_t row = (uint16_t)(v * 9);
        c->d[1] = set_w(c->d[1], row);
        uint16_t at = (uint16_t)(row + (rd8(STEP(e)) & 0x7F));
        next = rd8(NEXT_STATES + at);
        c->d[0] = (uint32_t)(at & 0xFF00) | next;        /* move.b into the index's low byte */
        cc = (uint16_t)(cc & ~CC_X);                       /* the add.w's carry: none */
        cc = tst_b(cc, next);
        if (next & 0x80) { c->ccr = cc; return; }
    }
    wr8(STEP(e), next);
    wr8(NEXT_STATE(e), 0xFF);
    c->ccr = tst_b(cc, next);
}

/* $133B2: the spawn points, then every enemy */
void RunEnemies(Cpu *c)
{
    call_68k(c, 0x12A70);                           /* the spawn points */
    c->a[4] = (Ent)(int32_t)rdw(FIRST_ENEMY);
    for (int n = 0; n < 16; n++, c->a[4] += 4) {
        Ent e = c->a[4];
        if (!(rd8(ALIVE(e)) & 0x80)) continue;
        uint8_t group = rd8(TYPE(e));
        c->d[0] = group;
        if (group & 0x80) continue;
        if ((rd8(TASK_FLAGS) & 8) && group <= 2) continue;
        uint32_t kinds = GROUPS + (uint32_t)(int32_t)rdw(GROUPS + group);
        uint8_t handler = rd8(kinds + (rd8(TYPE(e) + 1) >> 1));
        c->d[0] = (uint32_t)handler << 2;
        c->a[0] = rd32(HANDLERS + c->d[0]);
        call_68k(c, c->a[0]);
        e = c->a[4];                                /* as the original: whatever a4 is now */
        wr8(TOUCH(e), 0);
        wr8(MAGIC_HIT(e), 0);
        wrw(MAGIC_DMG(e), 0);
    }
    c->ret = 0;                                     /* call_68k on these registers left its mark */
}
