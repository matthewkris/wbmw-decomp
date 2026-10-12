/* The townspeople and their things, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The
 * enemies").
 *
 * Nine of the enemy handlers ($163AA-$168E7), run by RunEnemies (enemies.c) through the handler
 * table at $13422: group 4 (people and props in town) and group 6's one kind:
 *
 *   handler  kinds         what it does
 *    9 $163AA  $402        stands, facing as its spawn x says (bit 8)
 *   13 $163C8  $438        wanders to and fro between x $11B8 and $11F8, bobbing about a height
 *   15 $1646E  $600        stands; talks when the player comes up to it and presses up
 *   11 $1652A  $406-$468   the same, or walks a script (-$2700) when NO_COUNT is set
 *   12 $166CA  $412        stands
 *   14 $166DA  $43A        shows one of two poses by game flag 56, and turns when it is set
 *   26 $16712  $46A        a breakable thing placed by y's low bits, as $404 below
 *   10 $1676C  $404        breaks when the sword hits it, and sets its game flag (VAR)
 *    8 $167DC  $400        a scene: the player walks past x $10E0, the screen dims ($9734)
 *                          and it speaks, with $168E8 to call when the text is done
 *
 * Talking ($164B8): no text running ($9962), the player level with it and within its reach
 * (-$3500), standing on the ground, and up pressed alone; then the player is stopped and given
 * animation 12, $8CB0 names the speaker, and $9C16 the message (TIMER's high byte).
 *
 * The routines this calls keep running on the caller's registers (`c` below), as the original
 * does: what one enemy's code leaves in a register is what the next one's sees. */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

#define PLAYER_W      0xFFFF9EEEu
#define TEXT_BUSY     0xFFFF9962u   /* word: a message is running */
#define MESSAGE       0xFFFF9C16u   /* word: the message to show */
#define MESSAGE_DONE  0xFFFF9C18u   /* long: called when it is done */
#define SPEAKER       0xFFFF8CB0u   /* word: who is talking */
#define SCENE_ENT     0xFFFFA38Cu   /* word: the scene's entity, for $168E8 */
#define DIM           0xFFFF9734u   /* word: the screen's dimming, 0-$50 (long cleared) */
#define PLAYER_LOCK   0xFFFF9F05u   /* byte: 2 holds the player during the scene */
#define FRAME         0xFFFF965Au
#define AREA          0xFFFF9668u
#define AREA15_FLAGS  0xFFFF9994u   /* word: area 15 keeps its flags here */
#define ATTACKER_W    0xFFFF9F30u
#define ATTACK_STATE  0xFFFF9F32u
#define PLACES        0x1675Cu      /* handler 26: x, y offsets, attribute, animation, by y & 3 */
#define SCENE_DONE    0x168E8u

#define ALIVE(e)      ((e) - 0x4000)   /* bit 7 in use; 6 drawn; 0 on screen. Word: with its neighbour */
#define ATTR(e)       ((e) - 0x3FFE)   /* bit 3 facing left; 7 */
#define WORD_3DFE(e)  ((e) - 0x3DFE)
#define ANIM_SET(e)   ((e) - 0x3D00)   /* word: set, group */
#define ANIM_RATE(e)  ((e) - 0x3CFD)
#define ANIM_POS(e)   ((e) - 0x39FD)
#define ANIM_BUSY(e)  ((e) - 0x3A00)
#define X(e)          ((e) - 0x3800)
#define Y(e)          ((e) - 0x3700)
#define VEL_X(e)      ((e) - 0x3600)
#define REACH(e)      ((e) - 0x3500)   /* byte: how far sideways it can be talked to */
#define HALF_H(e)     ((e) - 0x34FD)
#define ACCEL_X(e)    ((e) - 0x3400)
#define SPEED(e)      ((e) - 0x3300)   /* long: x, y speeds */
#define SPEED_X(e)    ((e) - 0x3300)
#define SCRIPTED(e)   ((e) - 0x3200)   /* byte: nonzero: it walks its script */
#define COLL(e)       ((e) - 0x3100)
#define MOVE_FLAGS(e) ((e) - 0x2F00)
#define GOAL(e)       ((e) - 0x2D00)   /* word: where it walks to, or the height it bobs at; a wait's frames (high byte) */
#define STEP(e)       ((e) - 0x2BFF)   /* bit 7 once begun */
#define TOUCH(e)      ((e) - 0x2AFD)
#define SCRIPT(e)     ((e) - 0x2700)   /* long */
#define TIMER(e)      ((e) - 0x2800)   /* its message (high byte), or a word from its script */
#define VAR(e)        ((e) - 0x27FE)   /* word: a game flag; the scene's message (byte) */
#define HOME_X(e)     ((e) - 0x2100)

/* routines still called as they are */
#define QUEUE_SOUND     0x366u
#define RANDOM          0x5D8u
#define SET_ANIM        0x7E8u
#define SET_ANIM_NOW    0x7FCu
#define APPLY_VEL       0xAB4u
#define APPLY_VEL_X     0xAB8u
#define ACCEL_XY        0xB42u
#define BODY_VS_ATTACK  0xD76u
#define TEST_GAME_FLAG  0x274Cu
#define GAME_FLAG_BIT   0x2758u
#define BOB_TOWARD      0x4274u   /* ACCEL_Y toward the height d0, give or take d1, by d2 */

static Cpu *c;                        /* the running registers */

static void call(uint32_t addr) { call_68k(c, addr); }
static void call_d0(uint32_t addr, uint32_t d0) { c->d[0] = d0; call_68k(c, addr); }   /* moveq #d0 */
static int  zero(void) { return (c->ccr & CC_Z) != 0; }
static void set_d0w(int v) { c->d[0] = set_w(c->d[0], (uint32_t)v); }
static Ent  player(void) { return (Ent)(int32_t)rdw(PLAYER_W); }

static int begins(Ent e) { uint8_t s = rd8(STEP(e)); wr8(STEP(e), s | 0x80); return !(s & 0x80); }
static int shows(Ent e)  { uint8_t a = rd8(ALIVE(e)); wr8(ALIVE(e), a | 0x40); return !(a & 0x40); }
static void face_by_x(Ent e) { if (rd8(X(e)) & 1) set8(ATTR(e), 8); }   /* btst #0 of x's high byte: bit 8 */

static void stop(const char *what, Ent e)
{
    fprintf(stderr, "people.c: %s (entity $%04X, step $%02X, pc of the script $%06X)\n", what,
            (unsigned)(e & 0xFFFF), rd8(STEP(e)), (unsigned)rd32(SCRIPT(e)));
    exit(3);
}

/* $164B8: may the player talk to it now? (0: yes, and the player is stopped; d0 as the original) */
static int can_talk(Ent e)
{
    if (rdw(TEXT_BUSY)) { c->d[0] = 0xFFFFFFFFu; return 0; }
    Ent p = player();
    c->a[2] = p;
    uint16_t dy = (uint16_t)(rdw(Y(p)) - rdw(Y(e)));
    if (dy & 0x8000) dy = (uint16_t)-dy;
    set_d0w(dy);
    c->d[1] = rd8(HALF_H(e)); c->d[2] = rd8(HALF_H(p));
    c->d[1] = set_w(c->d[1], (c->d[1] + c->d[2]) & 0xFFFF);
    if (dy > (uint16_t)c->d[1]) { c->d[0] = 0xFFFFFFFFu; return 0; }
    uint16_t dx = (uint16_t)(rdw(X(p)) - rdw(X(e)));
    if (dx & 0x8000) dx = (uint16_t)-dx;
    set_d0w(dx);
    c->d[1] = rd8(REACH(e));
    if (dx > (uint16_t)c->d[1]) { c->d[0] = 0xFFFFFFFFu; return 0; }
    if (!(rd8(0xFFFF9F02u) & 0x40)) { c->d[0] = 0xFFFFFFFFu; return 0; }
    c->d[0] = (uint8_t)((rd8(COLL(p)) & 10) - 2);                     /* on the ground */
    if (c->d[0]) { c->d[0] = 0xFFFFFFFFu; return 0; }
    c->d[0] = rd8(0xFFFF9F0Bu) & 14;
    if (c->d[0]) { c->d[0] = 0xFFFFFFFFu; return 0; }
    c->d[0] = rd8(0xFFFF9F0Du) & 15;                                  /* up, alone */
    if (c->d[0] != 1) { c->d[0] = 0xFFFFFFFFu; return 0; }
    c->d[0] = 0;
    wrw(VEL_X(p), 0);
    return 1;
}

/* $1649C: the player turns to talk (animation 12), and its message starts */
static void talk(Ent e)
{
    uint32_t t = c->a[4]; c->a[4] = c->a[2]; c->a[2] = t;              /* exg a4,a2 */
    call_d0(SET_ANIM, 12);
    t = c->a[4]; c->a[4] = c->a[2]; c->a[2] = t;
    wrw(TEXT_BUSY, 8);
    c->d[0] = rd8(TIMER(e));
    wrw(MESSAGE, (int)c->d[0]);
}

/* ---- the handlers ---- */

/* handlers 9 and 12, $163AA and $166CA: stand (9 facing by its x) */
static void standing(Ent e)
{
    if (!shows(e)) return;
    call_d0(SET_ANIM, 0);
    face_by_x(e);
}

static void standing_still(Ent e)
{
    if (shows(e)) call_d0(SET_ANIM, 0);
}

/* handler 13, $163C8: wanders between x $11B8 and $11F8, bobbing about a height that changes
 * every 64 frames */
static void wanderer(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 0);
        set8(ALIVE(e), 0x40);
        set8(ATTR(e), 0x80);
        wrw(WORD_3DFE(e), 0);
        call(RANDOM);
        set_d0w(((c->d[0] & 0x7F) + 0x1000) & 0xFFFF);
        wrw(GOAL(e), (int)(c->d[0] & 0xFFFF));
        wr32(SPEED(e), 0x00800080u);
        wr8(MOVE_FLAGS(e), 3);
        face_by_x(e);
        return;
    }
    int16_t a = 16;
    uint16_t x = (uint16_t)rdw(X(e));
    int turn;
    c->d[2] = 16;
    if (rd8(ATTR(e)) & 8) { a = -16; turn = x < 0x11B8; }
    else turn = x >= 0x11F8;
    if (turn) { a = (int16_t)-a; wr8(ATTR(e), rd8(ATTR(e)) ^ 8); }
    c->d[2] = set_w(c->d[2], (uint16_t)a);
    wrw(ACCEL_X(e), a);
    c->d[0] = (uint32_t)rdw(FRAME) & 63;
    if (!c->d[0]) {
        call(RANDOM);
        c->d[1] = set_w(c->d[1], c->d[0]);
        set_d0w(((c->d[0] & 0x1F) + 0x1020) & 0xFFFF);
        wrw(GOAL(e), (int)(c->d[0] & 0xFFFF));
    }
    set_d0w(rdw(GOAL(e)));
    c->d[1] = 8; c->d[2] = 16;
    call(BOB_TOWARD);
    call(ACCEL_XY);
    call(APPLY_VEL);
}

/* handler 15, $1646E: a speaker */
static void speaker(Ent e)
{
    if (begins(e)) {
        wr8(ANIM_RATE(e), 0x18);
        wrw(ANIM_SET(e), 0x0E0C);
        call_d0(SET_ANIM, 0);
        set8(ALIVE(e), 0x40);
        return;
    }
    if (!can_talk(e)) return;
    wrw(SPEAKER, (int)(c->a[4] & 0xFFFF));
    talk(e);
}

/* handler 11's script ($16598): each command sets the next step (and what it needs); 0 if it
 * has to wait (text is running) */
static int read_script(Ent e)
{
    uint32_t a1 = rd32(SCRIPT(e));
    for (int n = 0; ; n++) {
        if (n > 1000) stop("a script that only jumps", e);
        c->a[1] = a1;
        uint8_t cmd = rd8(a1++);
        c->d[0] = (uint32_t)cmd * 2;
        if (cmd > 5) stop("a script command past the table", e);
        c->a[0] = 0x165A8u + (uint32_t)(int32_t)rdw(0x165A8u + c->d[0]);
        uint8_t step;
        switch (cmd) {
        case 0:                                     /* wait: frames */
            wr8(GOAL(e), rd8(a1++));
            step = 2; break;
        case 1: {                                   /* walk: a signed distance (its sign: which way) */
            clr8(ATTR(e), 8);
            int8_t d = (int8_t)rd8(a1++);
            c->d[1] = set_b(c->d[1], (uint8_t)d);
            if (d < 0) set8(ATTR(e), 8);
            c->d[1] = ext_w(c->d[1]);
            c->d[1] = set_w(c->d[1], (c->d[1] + (uint16_t)rdw(X(e))) & 0xFFFF);
            wrw(GOAL(e), (int)(c->d[1] & 0xFFFF));
            step = 4; break;
        }
        case 2:                                     /* walk home */
            wrw(GOAL(e), rdw(HOME_X(e)));
            step = 6; break;
        case 3:                                     /* speak: a word for TIMER (its high byte the message) */
            if (rdw(TEXT_BUSY)) { c->a[1] = a1; return 0; }
            c->d[1] = set_b(c->d[1], rd8(a1++));
            c->d[1] = set_w(c->d[1], (c->d[1] << 8) & 0xFFFF);
            c->d[1] = set_b(c->d[1], rd8(a1++));
            wrw(TIMER(e), (int)(c->d[1] & 0xFFFF));
            step = 8; break;
        case 4:
            step = 8; break;
        default: {                                  /* jump: a word from its own place */
            c->d[0] = rd8(a1++);
            c->d[0] = (c->d[0] << 8 | rd8(a1++)) & 0xFFFF;
            a1 = a1 - 2 + (uint32_t)(int32_t)(int16_t)c->d[0];
            continue;
        }
        }
        c->d[0] = step;
        c->a[1] = a1;
        wr8(STEP(e), step);
        wr32(SCRIPT(e), a1);
        return 1;
    }
}

/* handler 11's steps ($16560): run the step, and run again from the top when one ends */
static void scripted(Ent e)
{
    for (int n = 0; ; n++) {
        if (n > 1000) stop("a script that never waits", e);
        if (!rd8(SCRIPTED(e))) return;
        uint8_t step = rd8(STEP(e)) & 0x7F;
        c->d[0] = step;
        if (step > 8 || (step & 1)) stop("a step past the table", e);
        c->a[0] = 0x16578u + (uint32_t)(int32_t)rdw(0x16578u + step);
        switch (step) {
        case 0:                                     /* read the script */
            if (begins(e)) { wrw(VEL_X(e), 0); call_d0(SET_ANIM, 0); }
            if (!read_script(e)) return;            /* waiting to speak: tried again next frame */
            continue;
        case 2:                                     /* wait */
            if (begins(e)) { call_d0(SET_ANIM, 0); return; }
            wr8(GOAL(e), (uint8_t)(rd8(GOAL(e)) - 1));
            if (rd8(GOAL(e))) return;
            wr8(STEP(e), 0);
            continue;
        case 6: {                                   /* walk home: face it, then walk */
            clr8(ATTR(e), 8);
            int16_t d = (int16_t)(rdw(HOME_X(e)) - rdw(X(e)));
            set_d0w(d);
            if (!d) { wr8(STEP(e), 0); continue; }
            if (d < 0) set8(ATTR(e), 8);
            wr8(STEP(e), 4);
        }   /* fall through */
        case 4:                                     /* walk to GOAL */
            if (begins(e)) {
                int v = rdw(SPEED_X(e));
                set_d0w(v);
                if (rd8(ATTR(e)) & 8) { v = -v; set_d0w(v); }
                wrw(VEL_X(e), v);
                call_d0(SET_ANIM, 2);
                return;
            }
            call(APPLY_VEL_X);
            {
                uint16_t goal = (uint16_t)rdw(GOAL(e)), x = (uint16_t)rdw(X(e));
                set_d0w(goal);
                if (rd8(ATTR(e)) & 8 ? goal < x : goal > x) return;
                wrw(X(e), (int16_t)goal);
                wr8(STEP(e), 0);
            }
            continue;
        case 8:                                     /* speak, when no text is running */
            if (rd8(STEP(e)) & 0x80) return;
            set_d0w(rdw(TEXT_BUSY));
            if (c->d[0] & 0xFFFF) return;
            wr8(STEP(e), 0);
            c->d[0] = set_b(c->d[0], rd8(TIMER(e)));
            wrw(MESSAGE, (int)(c->d[0] & 0xFFFF));
            call_d0(SET_ANIM, 0);
            return;
        }
    }
}

/* handler 11, $1652A: a townsperson, who may walk a script */
static void townsperson(Ent e)
{
    if (shows(e)) {
        call_d0(SET_ANIM_NOW, 0);
        call(RANDOM);
        c->d[0] = set_b(c->d[0], c->d[0] & 0x3F);
        wr8(ANIM_POS(e), (uint8_t)c->d[0]);         /* not all in step */
        c->d[0] = rd8(HALF_H(e));
        wrw(Y(e), rdw(Y(e)) - (int)c->d[0]);
        return;
    }
    if (can_talk(e) && !rdw(TEXT_BUSY)) { talk(e); return; }
    scripted(e);
}

/* handler 14, $166DA: two poses by game flag 56 (0, or 4 once set); turns (2) when it is set */
static void flagged(Ent e)
{
    if (shows(e)) {
        c->d[2] = 0;
        call_d0(TEST_GAME_FLAG, 56);
        if (!zero()) c->d[2] = 4;
        wr8(STEP(e), (uint8_t)c->d[2]);
        set_d0w((int)(c->d[2] & 0xFFFF));
        call(SET_ANIM);
    }
    if (rd8(STEP(e))) return;
    call_d0(TEST_GAME_FLAG, 56);
    if (zero()) return;
    c->d[0] = 2;
    wr8(STEP(e), 2);
    call(SET_ANIM);
}

/* handlers 10 and 26 after their first frame ($1678A): broken by the sword (sound $2A, the
 * breaking animation), then gone with their game flag set */
static void breakable_frame(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        c->a[2] = player();
        call(BODY_VS_ATTACK);
        if (!(rd8(TOUCH(e)) & 0x80)) return;
        call_d0(QUEUE_SOUND, 42);
        wr8(STEP(e), 0xFF);
        wrw(ANIM_SET(e), 0x120A);
        call_d0(SET_ANIM, 0);
        return;
    }
    if (rd8(ANIM_BUSY(e))) return;
    set_d0w(rdw(VAR(e)));
    if (rd8(AREA) == 0x0F)
        wrw(AREA15_FLAGS, rdw(AREA15_FLAGS) | (int16_t)(c->d[0] & 0xFFFF));
    else {
        call(GAME_FLAG_BIT);                        /* d0: the bit, a0 + d1.w: the byte */
        uint32_t at = c->a[0] + (uint32_t)(int32_t)(int16_t)(c->d[1] & 0xFFFF);
        wr8(at, rd8(at) | (uint8_t)(1u << (c->d[0] & 7)));
    }
    wrw(ALIVE(e), 0);
}

/* handler 26, $16712: placed by y's low two bits (offsets, attribute, animation from $1675C) */
static void placed_breakable(Ent e)
{
    if (!shows(e)) { breakable_frame(e); return; }
    wr8(ANIM_RATE(e), 0x14);
    wrw(ANIM_SET(e), 0x1068);
    c->d[0] = 0;
    wrw(WORD_3DFE(e), 0);
    c->d[1] = (uint32_t)rdw(Y(e)) & 3;
    c->d[1] = set_w(c->d[1], (c->d[1] << 2) & 0xFFFF);
    uint32_t p = PLACES + (uint32_t)(int32_t)(int16_t)(c->d[1] & 0xFFFF);
    c->d[2] = set_b(c->d[2], rd8(p)); c->d[2] = ext_w(c->d[2]);
    wrw(X(e), rdw(X(e)) + (int16_t)(c->d[2] & 0xFFFF));
    c->d[2] = set_b(c->d[2], rd8(p + 1)); c->d[2] = ext_w(c->d[2]);
    wrw(Y(e), rdw(Y(e)) + (int16_t)(c->d[2] & 0xFFFF));
    c->d[0] = set_b(c->d[0], rd8(p + 2));
    set8(ATTR(e), (uint8_t)c->d[0]);
    c->d[0] = set_b(c->d[0], rd8(p + 3));
    c->a[0] = p + 3;
    wrw(Y(e), rdw(Y(e)) & 0xFFFC);
    call(SET_ANIM);
}

/* handler 10, $1676C */
static void breakable(Ent e)
{
    if (!shows(e)) { breakable_frame(e); return; }
    wr8(ANIM_RATE(e), 0x14);
    wrw(ANIM_SET(e), 0x1004);
    c->d[0] = 0;
    wrw(WORD_3DFE(e), 0);
    call(SET_ANIM);
}

/* handler 8, $167DC: the scene */
static void scene(Ent e)
{
    uint8_t step = rd8(STEP(e)) & 0x7F;
    c->d[0] = step;
    if (step > 8 || (step & 1)) stop("a scene step past the table", e);
    c->a[0] = 0x167ECu + (uint32_t)(int32_t)rdw(0x167ECu + step);
    switch (step) {
    case 0: {                                       /* wait for the player to come past */
        if (begins(e)) { call_d0(SET_ANIM, 0); set8(ALIVE(e), 0x40); return; }
        Ent p = player();
        c->a[2] = p;
        if ((uint16_t)rdw(X(p)) < 0x10E0) return;
        wr8(PLAYER_LOCK, 2);
        if ((uint16_t)rdw(X(p)) < 0x1140) return;
        wr8(STEP(e), 2);
        return;
    }
    case 2:                                         /* dim the screen */
        wrw(DIM, rdw(DIM) + 1);
        if ((uint16_t)rdw(DIM) < 0x50) return;
        wrw(DIM, 0x50);
        wr8(STEP(e), 4);
        return;
    case 4:                                         /* an animation, then 20 frames */
        if (begins(e)) { call_d0(SET_ANIM_NOW, 2); wr8(TIMER(e), 0x14); }
        if (rd8(ANIM_BUSY(e))) return;
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        wr8(STEP(e), 6);
        return;
    case 6:                                         /* speak, then wait for the player's attack to end */
        if (!(rd8(STEP(e)) & 0x80)) {
            if (rdw(TEXT_BUSY)) return;
            set8(STEP(e), 0x80);
            wrw(TEXT_BUSY, 8);
            c->d[0] = rd8(VAR(e));
            wrw(MESSAGE, (int)c->d[0]);
            wr32(MESSAGE_DONE, SCENE_DONE);
            wrw(SCENE_ENT, (int)(c->a[4] & 0xFFFF));
            return;
        }
        set_d0w(rdw(ATTACK_STATE));
        if (!(c->d[0] & 0x8000)) {
            c->a[2] = (uint32_t)(int32_t)rdw(ATTACKER_W);
            c->d[0] = rd8(STEP(c->a[2])) & 0x7F;
            if (c->d[0] == 0x0C) return;            /* cmpi #$C: not done; ($A would be, never both) */
        }
        wr8(PLAYER_LOCK, 0);
        wr8(STEP(e), 8);
        return;
    case 8:                                         /* the light comes back */
        if (begins(e)) call_d0(SET_ANIM, 6);
        if (rd16(DIM) == 0) wr32(DIM, 0);           /* subq.w: borrow */
        else wrw(DIM, rdw(DIM) - 1);
        return;
    }
}

/* the handlers, registered by their addresses (registry.c); call_68k leaves its mark in ret */
#define HANDLER(name, fn) void name(Cpu *cpu) { c = cpu; fn((Ent)c->a[4]); c->ret = 0; }
HANDLER(PplStanding, standing)
HANDLER(PplWanderer, wanderer)
HANDLER(PplSpeaker, speaker)
HANDLER(PplTownsperson, townsperson)
HANDLER(PplStandingStill, standing_still)
HANDLER(PplFlagged, flagged)
HANDLER(PplPlacedBreakable, placed_breakable)
HANDLER(PplBreakable, breakable)
HANDLER(PplScene, scene)
