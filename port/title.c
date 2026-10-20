/* The title screen's tasks, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The title").
 *
 * The title is drawn by tasks, each started by name from the scheduler's tables: Kttl ($7042,
 * the opening: the two halves of the logo slide together), Ktt2 ($715C, the title with its
 * music, then the demo), a second entry to it with the title already shown ($713A, named K_mn)
 * and Kmnu ($71C6, the menu after Start: a new game, or the saved one). A task's pass ends when
 * it calls Yield ($400: its resume address kept in the slot) or returns (the same address kept);
 * the next pass carries on there with the scheduler's registers. Here each task is a function
 * running on its slot's coroutine (port/task.c) that waits with task_wait_frame at the original's
 * resume addresses; a pass that just returns is a wait at the address kept. The tasks hand over to
 * the next scene through $4CA8 (the task ended, d0 the scene).
 *
 * The routines under them that do not wait are ported register for register, with every
 * register and flag in the contract. */
#include "world.h"
#include "task.h"

#define LOAD_PALETTES  0x135Cu
#define QUEUE_SOUND    0x0366u
#define INIT_ENTITY    0x0A1Au
#define SET_ANIM       0x07E8u
#define SET_GFX_SLOTS  0x234Cu
#define STREAM_GFX     0x22F0u
#define FADE_START     0x5842u
#define SCREEN_CLEAR   0x5188u
#define DRAW_LAYOUT    0x69AEu   /* a2: a layout of tiles, $8C76 the plane */
#define FADE_IN        0x581Au
#define PALETTES_NOW   0x55BAu
#define START_PRESSED  0x513Cu   /* ne: Start pressed (either pad) */
#define SPRITE_INIT    0x8E22u
#define SAVE_GOOD      0x1C430u  /* eq: the saved game can be continued */
#define SCENE_END      0x4CA8u   /* the task ended; d0 the next scene */
#define FADE_STATE     0xFFFF8C56u   /* bit 4: the fade is done */
#define PAD_MODE       0xFFFF8A7Du   /* byte: bit 7 one pad only (the low bits which) */
#define PAD1           0xFFFF8A81u
#define PAD2           0xFFFF8A84u

static Cpu *c;

static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static void f_bit(int on) { c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (on ? 0 : CC_Z)); }   /* btst */
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }

static void call(uint32_t addr) { call_68k(c, addr); }
static void sound(int32_t n) { c->d[0] = (uint32_t)n; F_L(c->d[0]); call(QUEUE_SOUND); }
static void palettes(uint32_t list) { c->a[0] = list; call(LOAD_PALETTES); }
/* Yield, at the original's resume address: the next pass carries on with the scheduler's
 * registers (another routine of this file may have run meanwhile, hence c kept) */
static void wait(uint32_t resume) { Cpu *me = c; task_wait_frame(me, resume); c = me; }
static int fade_done(void) { int on = rd8(FADE_STATE) >> 4 & 1; f_bit(on); return on; }
static uint16_t count_down(uint32_t a)       /* subq.w #1 */
{
    uint16_t v = (uint16_t)sub_w(&c->ccr, 1, rd16(a)); wr16(a, v); return v;
}
static void scene_end(void) { port_continue_at(c, SCENE_END, 0); }

/* $711C: entity a4 placed: its picture d0, x d1, y d2; started with animation 0 */
static void title_place(void)
{
    wr16(c->a[4] - 0x3D00, (uint16_t)c->d[0]);
    wr16(c->a[4] - 0x3800, (uint16_t)c->d[1]);
    wr16(c->a[4] - 0x3700, (uint16_t)c->d[2]); F_W(c->d[2]);
    call(INIT_ENTITY);
    c->d[0] = 0; F_L(0);
    wr8(c->a[4] - 0x3CFD, 0);
    call(SET_ANIM);
}

/* $70D6: the title's screen: cleared, its graphics, the two layouts (planes 2 and 3), the logo's
 * halves (entities 0 and 4) */
static void title_setup(void)
{
    call(SCREEN_CLEAR);
    c->a[0] = 0x72F8u; call(SET_GFX_SLOTS);
    call(STREAM_GFX);
    c->a[2] = 0x7300u; wr8(0xFFFF8C76u, 2); F_B(2); call(DRAW_LAYOUT);
    c->a[2] = 0x73CCu; wr8(0xFFFF8C76u, 3); F_B(3); call(DRAW_LAYOUT);
    c->a[4] = 0;
    c->d[0] = set_w(c->d[0], 0x800);
    c->d[1] = 0;
    c->d[2] = 48; F_L(48);
    title_place();
    c->a[4] += 4;
    c->d[0] = set_w(c->d[0], 0x802);
    c->d[1] = 0;
    c->d[2] = set_w(c->d[2], 0xB8); F_W(0xB8);
    title_place();
}

/* $7272: the title's "press Start" (entity 8), shown on the window plane */
static void title_cursor(void)
{
    c->a[4] = 8;
    c->d[0] = set_w(c->d[0], 0x808);
    c->d[1] = set_w(c->d[1], 0x80);
    c->d[2] = set_w(c->d[2], 0x80); F_W(0x80);
    title_place();
    wr16(c->a[4] - 0x3900, 0xFF01);
    uint8_t v = rd8(c->a[4] - 0x3FFD) | 0x40; wr8(c->a[4] - 0x3FFD, v); F_B(v);
}

/* $7294: the menu's entities: its lines (entity 8, and $C "continue" if the saved game can be,
 * $42(a5) then 1) and the cursor ($10) on the first */
static void menu_setup(void)
{
    wr16(c->a[5] + 0x42, 0);
    call(SAVE_GOOD);
    if (!(c->ccr & CC_Z)) {
        c->d[2] = set_w(c->d[2], 0x80); F_W(0x80);
    } else {
        wr16(c->a[5] + 0x42, 1);
        c->a[4] = 0xC;
        c->d[0] = set_w(c->d[0], 0x806);
        c->d[1] = set_w(c->d[1], 0x80);
        c->d[2] = set_w(c->d[2], 0x88); F_W(0x88);
        title_place();
        c->d[2] = set_w(c->d[2], 0x78); F_W(0x78);
    }
    c->a[4] = 8;
    c->d[0] = set_w(c->d[0], 0x804);
    c->d[1] = set_w(c->d[1], 0x80); F_W(0x80);
    title_place();
    c->a[4] = 0x10;
    c->d[0] = set_w(c->d[0], 0x80A);
    c->d[1] = 80; F_L(80);
    title_place();
    wr16(c->a[4] - 0x3900, 0xFF01);
    uint8_t v = rd8(c->a[4] - 0x3FFD) | 0x40; wr8(c->a[4] - 0x3FFD, v); F_B(v);
}

/* $724E: button d0 (0 up, 1 down) pressed: on the one pad in use, or on either (ne: pressed) */
static void title_pad(void)
{
    c->d[1] = set_b(c->d[1], rd8(PAD_MODE)); F_B(c->d[1]);
    if (c->d[1] & 0x80) {
        c->a[0] = PAD1;
        c->d[1] = set_b(c->d[1], c->d[1] & 0x7F); F_B(c->d[1]);
        if (c->d[1] & 0xFF) c->a[0] = PAD2;
        f_bit(rd8(c->a[0]) >> (c->d[0] & 7) & 1);
        return;
    }
    f_bit(rd8(PAD1) >> (c->d[0] & 7) & 1);
    if (c->ccr & CC_Z) f_bit(rd8(PAD2) >> (c->d[0] & 7) & 1);
}

/* $7042, Kttl: the opening. The logo's halves come in from either side, $20 a frame, until they
 * meet at x $80; a second later the next scene (d0 as the scheduler left it) */
static void title_opening(uint32_t at)
{
    switch (at) { case 0x7062: goto r7062; case 0x7076: goto r7076; case 0x7082: goto r7082; case 0x70CC: goto r70CC; }
    title_setup();
    wr16(0xFFFFC800u, 0x180);
    wr16(0xFFFFC804u, 0xFF80);
    palettes(0x72F0u);
    call(FADE_IN);
    do { wait(0x7062); r7062: ; } while (!fade_done());
    wr16(c->a[5] + 0x40, 0x3C);
    do { wait(0x7076); r7076: ; } while (count_down(c->a[5] + 0x40));
    for (;;) {
        wait(0x7082);
r7082:
        c->a[4] = 0;
        c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x3800));
        c->d[0] = set_w(c->d[0], sub_w(&c->ccr, 0x20, c->d[0]));
        cmp_w(&c->ccr, 0x80, c->d[0]);
        if ((int16_t)c->d[0] < 0x80) { c->d[0] = set_w(c->d[0], 0x80); F_W(0x80); }
        wr16(c->a[4] - 0x3800, (uint16_t)c->d[0]); F_W(c->d[0]);
        c->a[4] += 4;
        c->d[0] = 32;
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, rd16(c->a[4] - 0x3800), c->d[0]));
        cmp_w(&c->ccr, 0x80, c->d[0]);
        if ((int16_t)c->d[0] > 0x80) break;
        wr16(c->a[4] - 0x3800, (uint16_t)c->d[0]); F_W(c->d[0]);   /* the pass ends */
    }
    wr16(c->a[4] - 0x3800, 0x80);
    palettes(0x72F4u);
    wr16(c->a[5] + 0x40, 0x3C);
    call(PALETTES_NOW);
    do { wait(0x70CC); r70CC: ; } while (count_down(c->a[5] + 0x40));
    scene_end();
}

/* $718A: the title's music for $314 frames (or until the scene is left), then a fade to the
 * demo (scene -1) */
static void title_music(uint32_t at)
{
    if (at == 0x719A) goto r719A;
    if (at == 0x71B0) goto r71B0;
    sound(20);
    wr16(c->a[5] + 0x40, 0x314);
    do { wait(0x719A); r719A: ; } while (count_down(c->a[5] + 0x40));
    sound(-2);
    call(FADE_START);
    do { wait(0x71B0); r71B0: ; } while (!fade_done());
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
    scene_end();
}

/* $715C, Ktt2: the title fading in */
static void title_screen(uint32_t at)
{
    if (at == 0x7180) goto r7180;
    title_setup();
    palettes(0x72F4u);
    wr16(0xFFFFC800u, 0x80);
    wr16(0xFFFFC804u, 0x80);
    title_cursor();
    call(FADE_IN);
    do { wait(0x7180); r7180: ; } while (!fade_done());
    title_music(0);
}

/* $713A, K_mn: the title at once (after the opening) */
static void title_shown(void)
{
    palettes(0x72F4u);
    call(PALETTES_NOW);
    wr16(0xFFFFC800u, 0x80);
    wr16(0xFFFFC804u, 0x80);
    title_cursor();
    title_music(0);
}

/* $71C6, Kmnu: the menu. Without a saved game Start begins a new one; with one, up and down move
 * the cursor (y $78 a new game, $88 continue) and Start chooses. A new game resets the save's
 * buffer ($8E22). Then a fade, and scene 0 */
static void title_menu(uint32_t at)
{
    int fresh = 0;
    if (at == 0x71DA) goto r71DA;
    if (at == 0x723C) goto r723C;
    menu_setup();
    palettes(0x72F4u);
    call(PALETTES_NOW);
    for (;;) {
        wait(0x71DA);
r71DA:
        F_W(rd16(c->a[5] + 0x42));
        if (!rd16(c->a[5] + 0x42)) {
            call(START_PRESSED);
            if (!(c->ccr & CC_Z)) { fresh = 1; break; }
            continue;
        }
        c->a[4] = 0x10;
        call(START_PRESSED);
        if (!(c->ccr & CC_Z)) {
            cmp_w(&c->ccr, 0x78, rd16(c->a[4] - 0x3700));
            fresh = rd16(c->a[4] - 0x3700) == 0x78;
            break;
        }
        c->d[0] = 0; F_L(0);
        title_pad();
        if (!(c->ccr & CC_Z)) c->d[0] = set_w(c->d[0], 0x78);
        else {
            c->d[0] = 1; F_L(1);
            title_pad();
            if (c->ccr & CC_Z) continue;
            c->d[0] = set_w(c->d[0], 0x88);
        }
        F_W(c->d[0]);
        cmp_w(&c->ccr, rd16(c->a[4] - 0x3700), c->d[0]);
        if ((c->d[0] & 0xFFFF) == rd16(c->a[4] - 0x3700)) continue;
        wr16(c->a[4] - 0x3700, (uint16_t)c->d[0]);
        sound(37);
    }
    if (fresh) call(SPRITE_INIT);
    sound(-2);
    sound(38);
    call(FADE_START);
    do { wait(0x723C); r723C: ; } while (!fade_done());
    c->d[0] = 0; F_L(0);
    scene_end();
}

/* the tasks leave by port_continue_at ($4CA8), so ret is theirs to set */
/* a routine that waits for frames runs only on a task pass; reached outside one (a pass the harness
 * finished on the 68000), it goes on in the original at its own address (port/task.h, CT_WAITS) */
#define NEEDS_PASS(self) do { if (!task_in_pass()) { port_leave_to_68k(c, (self)); return; } } while (0)
void TitleOpening(Cpu *cpu) { c = cpu; NEEDS_PASS(0x7042); title_opening(0); }  /* $7042 */
void TitleShown(Cpu *cpu)   { c = cpu; NEEDS_PASS(0x713A); title_shown(); }     /* $713A */
void TitleScreen(Cpu *cpu)  { c = cpu; NEEDS_PASS(0x715C); title_screen(0); }   /* $715C */
void TitleMenu(Cpu *cpu)    { c = cpu; NEEDS_PASS(0x71C6); title_menu(0); }     /* $71C6 */
/* batch N3h: the title's tasks started where they waited (a loaded state) */
#define TITLE_AT(name, fn, at) void name(Cpu *cpu) { c = cpu; NEEDS_PASS(at); fn(at); }
TITLE_AT(TitleOpeningAt7062, title_opening, 0x7062)
TITLE_AT(TitleOpeningAt7076, title_opening, 0x7076)
TITLE_AT(TitleOpeningAt7082, title_opening, 0x7082)
TITLE_AT(TitleOpeningAt70CC, title_opening, 0x70CC)
TITLE_AT(TitleScreenAt7180, title_screen, 0x7180)
TITLE_AT(TitleMusicAt719A, title_music, 0x719A)
TITLE_AT(TitleMusicAt71B0, title_music, 0x71B0)
TITLE_AT(TitleMenuAt71DA, title_menu, 0x71DA)
TITLE_AT(TitleMenuAt723C, title_menu, 0x723C)

/* call_68k leaves its mark in ret: cleared, as none of these goes on elsewhere */
#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(TitleSetup, title_setup)         /* $70D6 */
ENTRY(TitlePlace, title_place)         /* $711C */
ENTRY(TitleCursor, title_cursor)       /* $7272 */
ENTRY(TitleMenuSetup, menu_setup)      /* $7294 */
ENTRY(TitlePad, title_pad)             /* $724E */
