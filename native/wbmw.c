/* The native build: Wonder Boy in Monster World as C, without a 68000 emulator.
 *
 * The game's code is the hand-written C of port/ (the hand ports) and machine.c (the machine
 * core: reset, frame loop, scheduler, frame wait, interrupt, hangs); none of the original's code
 * is translated or emulated. This file is the host: it plays the part of the console around
 * that code. The memory map, the save EEPROM, the controller and the VDP (video) are the
 * harness's own (harness/bus.c, harness/vdp.c), and so is the sound (harness/sound.c: the Z80
 * running the game's sound driver, the YM2612 and the SN76489), one frame of it at each poll point.
 *
 * Time: the code takes no time of its own. It runs until it waits for the VBlank interrupt
 * (machine.c's frame wait calls rt_poll); there the host draws the frame, shows it, waits for
 * the next 60 Hz tick, reads the controller and runs the game's interrupt handler (machine.c's
 * $4B5C), as the hardware would.
 *
 *   wbmw [ROM] [--windowed] [--scale N] [--save FILE] [--integer-scale] [--fps]   play (full screen)
 *   wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE] [--wav FILE]   headless, for comparison
 *            [--frame-log FILE] [--input-game FILE] [--dump-at N DIR]          by game frame (harness/framelog.c)
 *
 * The ROM is still needed: the game's data (graphics, maps, text) is read from it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../harness/md.h"
#include "../port/m68k_rt.h"

#ifndef WBMW_HEADLESS_ONLY
#include <SDL.h>
#endif

/* save states (below), used by the window's keys */
static const char *state_path;
static int save_pending;
static const char *save_to;
static const char *toast; static int toast_frames;   /* a short message over the picture (window only) */

static void request_load(const char *path);
static void maybe_save(Cpu *c, uint32_t pc);

/* ---- the machine around the code ---- */
uint8_t rd8(uint32_t a) { return (uint8_t)bus_read8(a); }
uint16_t rd16(uint32_t a) { return (uint16_t)bus_read16(a); }
void wr8(uint32_t a, uint8_t v) { bus_write8(a, v); }
void wr16(uint32_t a, uint16_t v) { bus_write16(a, v); }

/* ported code waiting on the Z80 (QueueSound): let sound time pass, as the hardware would */
void port_idle(void) { sound_idle(); }
int port_frames_in_c(void) { return 1; }   /* native/machine.c has it */

/* save states: the harness's own file format (harness/core.c), so states move between play.exe
 * and wbmw.exe. The modules write their parts through state_io. */
static int state_failed;
void state_io(FILE *f, void *p, size_t n, int saving)
{
    if (!f) return;
    size_t done = saving ? fwrite(p, 1, n, f) : fread(p, 1, n, f);
    if (done != n) state_failed = 1;
}

/* code reached an address that neither the hand ports (native/machine.c's rt_dispatch) nor the
 * translated code have: control goes back up with c->ret set to it, as in the harness (a hand
 * port going on at its caller's return address ends here too); machine.c reports it if it
 * reaches the frame loop */
void rt_leave(Cpu *c, uint32_t from, uint32_t target)
{
    (void)from;
    c->ret = target;
}

void task_reset(void);                          /* port/task.c */
int machine_run(Cpu *c, uint32_t pc);             /* native/machine.c: the machine core */
void machine_isr(Cpu *c);


/* ---- frames: drawing, showing, input, the interrupt ---- */
static int frame, frames_max, png_every, headless, scale;            /* scale 0: fit the screen */
static const char *out_dir = ".", *save_path = "wbmw.sav", *wav_path;
static int mute;

/* widescreen: the picture 400 wide (16:9 at 224 lines), 72 pixels past each side of the game's
 * 256. Only the rooms below show the game there (port/rooms.c draws their plane A wider and
 * keeps the view inside them); elsewhere those sides are black. docs/WIDESCREEN.md. */
#define WIDE_PX 72
extern int port_wide_px;                        /* port/rooms.c (port/world.h) */
extern int (*port_wide_room)(int area, int room);
static int widescreen;
static int wide_room(int area, int room)
{
    if (area == 1) return room <= 4;                /* after the opening: the side-scrolling rooms */
    return area == 2 && room <= 2;                  /* the first village (0), the forest path east of it (2), the field (1) */
}
static void wide_apply(void)
{
    port_wide_px = vdp_pad = widescreen ? WIDE_PX : 0;
    port_wide_room = wide_room;
}
static struct { int frame; uint8_t buttons; } *script; static int script_n;

static void load_input(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[256]; int cap = 0;
    while (fgets(line, sizeof line, f)) {
        int fr; char b[64] = "";
        if (line[0] == '#' || sscanf(line, "%d %63s", &fr, b) < 1) continue;
        uint8_t m = 0;
        for (char *p = b; *p; p++) { const char *k = strchr("UDLRBCAS", *p); if (k) m |= 1 << (k - "UDLRBCAS"); }
        if (script_n == cap) script = realloc(script, (cap = cap ? cap * 2 : 16) * sizeof *script);
        script[script_n].frame = fr; script[script_n].buttons = m; script_n++;
    }
    fclose(f);
}

static uint8_t buttons_at(int fr)
{
    uint8_t m = 0;
    for (int i = 0; i < script_n; i++) if (script[i].frame <= fr) m = script[i].buttons;
    return m;
}

#ifndef WBMW_HEADLESS_ONLY
static SDL_Window *win; static SDL_Renderer *ren; static SDL_Texture *tex; static SDL_GameController *gc;
static Uint64 next_tick;
static int fullscreen = 1, integer_scale, show_fps;   /* full screen unless --windowed */
static const char *TITLE = "Wonder Boy in Monster World (native)  (Esc: menu, F11: full screen)";

/* frames shown per second, measured over each second */
static Uint64 fps_start; static int fps_frames; static double fps;

/* a 3x5 pixel font for the fps overlay: digits, '.', 'f', 'p', 's'; each row is 3 bits */
static const uint8_t FONT[][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
    {0,0,0,0,2}, {3,4,6,4,4}, {6,5,6,4,4}, {3,4,2,1,6},
};
static int glyph(char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch == '.' ? 10 : ch == 'f' ? 11 : ch == 'p' ? 12 : ch == 's' ? 13 : -1; }

/* draw the fps in the top right corner of the picture, white on a black box */
static void draw_fps(void)
{
    char text[16]; snprintf(text, sizeof text, "%.1ffps", fps);
    int n = (int)strlen(text), w = n * 4 + 1, x0 = frame_width - w - 2, y0 = 2;
    for (int y = y0 - 1; y < y0 + 6; y++) for (int x = x0 - 1; x < x0 + w; x++) frame_rgb[y][x] = 0x000000;
    for (int i = 0; i < n; i++) {
        int g = glyph(text[i]); if (g < 0) continue;
        for (int r = 0; r < 5; r++) for (int col = 0; col < 3; col++)
            if (FONT[g][r] >> (2 - col) & 1) frame_rgb[y0 + r][x0 + i * 4 + col] = 0xFFFFFF;
    }
}

static void set_fullscreen(int on)
{
    fullscreen = on;
    SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);    /* borderless, at the desktop's size */
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

/* ---- controls and settings: the bindings, wbmw.ini ---- */

/* the game's buttons, in the bit order of pad_buttons, then the host's own */
enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_B, ACT_C, ACT_A, ACT_START, ACT_SAVE, ACT_LOAD, ACTIONS };
static const char *const ACT_NAME[ACTIONS] = {"UP", "DOWN", "LEFT", "RIGHT", "B", "C", "A", "START", "QUICK SAVE", "QUICK LOAD"};
static const char *const ACT_INI[ACTIONS] = {"up", "down", "left", "right", "b", "c", "a", "start", "save", "load"};

/* each action has a key and a controller input: a button (SDL's number), a side of an axis
 * (PAD_AXIS + axis * 2, + 1 for its negative side: a stick's direction, a trigger) or none */
#define PAD_NONE (-1)
#define PAD_AXIS 64
#define AXIS_ON 16000                               /* how far a stick or trigger counts as pressed */
static SDL_Scancode key_bind[ACTIONS];
static int pad_bind[ACTIONS], stick_moves;          /* stick_moves: the left stick works as the D-pad too */
static const SDL_Scancode KEY_DEFAULT[ACTIONS] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_Z, SDL_SCANCODE_RETURN, SDL_SCANCODE_F2, SDL_SCANCODE_F4};
static const int PAD_DEFAULT[ACTIONS] = {       /* quick load is unbound, so that no stray press loses progress */
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_START, PAD_NONE, PAD_NONE};

static void controls_default(void)
{
    memcpy(key_bind, KEY_DEFAULT, sizeof key_bind);
    memcpy(pad_bind, PAD_DEFAULT, sizeof pad_bind);
    stick_moves = 1;
}

/* a key or input already used by another action goes to it in exchange */
static void bind_key(int a, SDL_Scancode k)
{
    for (int o = 0; o < ACTIONS; o++) if (o != a && key_bind[o] == k) key_bind[o] = key_bind[a];
    key_bind[a] = k;
}

static void bind_pad(int a, int p)
{
    for (int o = 0; o < ACTIONS; o++) if (o != a && pad_bind[o] == p) pad_bind[o] = pad_bind[a];
    pad_bind[a] = p;
}

static int key_down(const Uint8 *k, SDL_Scancode s)
{
    if (s == SDL_SCANCODE_UNKNOWN || !k[s]) return 0;
    return !(s == SDL_SCANCODE_RETURN && (SDL_GetModState() & KMOD_ALT));    /* Alt+Enter is full screen */
}

static int pad_down(int p)
{
    if (!gc || p == PAD_NONE) return 0;
    if (p < PAD_AXIS) return SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)p);
    int v = SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis)((p - PAD_AXIS) / 2));
    return (p - PAD_AXIS) & 1 ? v < -AXIS_ON : v > AXIS_ON;
}

/* names for the menu, at most 13 characters: keys by SDL's names, controller buttons by where
 * they are (SDL's standard layout), as each make of pad labels them its own way */
static const char *key_label(SDL_Scancode s)
{
    static char out[24];
    const char *n = s == SDL_SCANCODE_UNKNOWN ? "" : SDL_GetScancodeName(s);
    if (!*n) return "-";
    if (!strncmp(n, "Keypad ", 7)) snprintf(out, sizeof out, "KP %s", n + 7);
    else snprintf(out, sizeof out, "%s", n);
    for (char *c = out; *c; c++) *c = (char)toupper((unsigned char)*c);
    out[11] = 0;                                    /* the column's width */
    return out;
}

static const char *pad_label(int p)
{
    static const char *const BUTTON[] = {"FACE DOWN", "FACE RIGHT", "FACE LEFT", "FACE UP", "SELECT", "HOME", "START",
        "L STICK BTN", "R STICK BTN", "L SHOULDER", "R SHOULDER", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT"};
    static const char *const AXIS[6][2] = {{"L STICK RIGHT", "L STICK LEFT"}, {"L STICK DOWN", "L STICK UP"},
        {"R STICK RIGHT", "R STICK LEFT"}, {"R STICK DOWN", "R STICK UP"}, {"L TRIGGER", "L TRIGGER"}, {"R TRIGGER", "R TRIGGER"}};
    static char out[16];
    if (p == PAD_NONE) return "-";
    if (p >= PAD_AXIS) return (p - PAD_AXIS) / 2 < 6 ? AXIS[(p - PAD_AXIS) / 2][(p - PAD_AXIS) & 1] : "AXIS";
    if (p < (int)(sizeof BUTTON / sizeof *BUTTON)) return BUTTON[p];
    snprintf(out, sizeof out, "BUTTON %d", p);
    return out;
}

/* wbmw.ini: what the player chose in the menu (and with F11, F3). The command line can override
 * a setting for one run without changing the file: cfg is what the file holds. */
static struct { int fullscreen, show_fps, integer_scale, mute, widescreen; } cfg = {1, 0, 0, 0, 0};
static char ini_path[4096];
static int ini_dirty;

static void toggle_fullscreen(void) { set_fullscreen(!fullscreen); cfg.fullscreen = fullscreen; ini_dirty = 1; }
static void toggle_fps(void) { show_fps = cfg.show_fps = !show_fps; ini_dirty = 1; }

/* controller inputs in the file by SDL's names, an axis with its side: "a", "dpup", "-lefty", "none" */
static void pad_name(int p, char *out, size_t n)
{
    const char *s = p == PAD_NONE ? NULL : p >= PAD_AXIS ? SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)((p - PAD_AXIS) / 2))
                                                         : SDL_GameControllerGetStringForButton((SDL_GameControllerButton)p);
    if (!s) snprintf(out, n, "none");
    else if (p >= PAD_AXIS) snprintf(out, n, "%c%s", (p - PAD_AXIS) & 1 ? '-' : '+', s);
    else snprintf(out, n, "%s", s);
}

static int pad_parse(const char *s)
{
    if (*s == '+' || *s == '-') {
        int ax = SDL_GameControllerGetAxisFromString(s + 1);
        return ax < 0 ? PAD_NONE : PAD_AXIS + ax * 2 + (*s == '-');
    }
    int b = SDL_GameControllerGetButtonFromString(s);
    return b < 0 ? PAD_NONE : b;
}

static void settings_save(void)
{
    ini_dirty = 0;
    if (!ini_path[0]) return;
    FILE *f = fopen(ini_path, "w");
    if (!f) { fprintf(stderr, "wbmw: cannot write the settings to %s\n", ini_path); return; }
    fprintf(f, "# Wonder Boy in Monster World (native): settings, written by the game's menu (Esc).\n"
               "# Keys by SDL's names; controller inputs by SDL's, an axis with + or - before it; none: unbound.\n");
    fprintf(f, "fullscreen=%d\nshow_fps=%d\ninteger_scale=%d\nmute=%d\nwidescreen=%d\nstick_moves=%d\n",
            cfg.fullscreen, cfg.show_fps, cfg.integer_scale, cfg.mute, cfg.widescreen, stick_moves);
    for (int a = 0; a < ACTIONS; a++) {
        char pad[64]; pad_name(pad_bind[a], pad, sizeof pad);
        const char *key = key_bind[a] == SDL_SCANCODE_UNKNOWN ? "" : SDL_GetScancodeName(key_bind[a]);
        fprintf(f, "key.%s=%s\npad.%s=%s\n", ACT_INI[a], *key ? key : "none", ACT_INI[a], pad);
    }
    fclose(f);
}

/* read before the command line; the file is next to the program unless --config names one */
static void settings_open(const char *path)
{
    controls_default();
    if (path) snprintf(ini_path, sizeof ini_path, "%s", path);
    else { char *base = SDL_GetBasePath(); snprintf(ini_path, sizeof ini_path, "%swbmw.ini", base ? base : ""); SDL_free(base); }
    FILE *f = fopen(ini_path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char *v = strchr(line, '=');
            if (line[0] == '#' || !v) continue;
            *v++ = 0; v[strcspn(v, "\r\n")] = 0;
            if (!strcmp(line, "fullscreen")) cfg.fullscreen = atoi(v);
            else if (!strcmp(line, "show_fps")) cfg.show_fps = atoi(v);
            else if (!strcmp(line, "integer_scale")) cfg.integer_scale = atoi(v);
            else if (!strcmp(line, "mute")) cfg.mute = atoi(v);
            else if (!strcmp(line, "widescreen")) cfg.widescreen = atoi(v);
            else if (!strcmp(line, "stick_moves")) stick_moves = atoi(v);
            else for (int a = 0; a < ACTIONS; a++) {
                if (!strncmp(line, "key.", 4) && !strcmp(line + 4, ACT_INI[a]))
                    key_bind[a] = strcmp(v, "none") ? SDL_GetScancodeFromName(v) : SDL_SCANCODE_UNKNOWN;
                if (!strncmp(line, "pad.", 4) && !strcmp(line + 4, ACT_INI[a])) pad_bind[a] = pad_parse(v);
            }
        }
        fclose(f);
    }
    fullscreen = cfg.fullscreen; show_fps = cfg.show_fps; integer_scale = cfg.integer_scale; mute = cfg.mute;
    widescreen = cfg.widescreen;
}

/* a controller plugged in (or there at the start) or pulled out: the first one plays */
static void controller_event(const SDL_Event *e)
{
    if (e->type == SDL_CONTROLLERDEVICEADDED && !gc) gc = SDL_GameControllerOpen(e->cdevice.which);
    if (e->type == SDL_CONTROLLERDEVICEREMOVED && gc && e->cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc))) {
        SDL_GameControllerClose(gc); gc = NULL;
        for (int i = 0; i < SDL_NumJoysticks() && !gc; i++) if (SDL_IsGameController(i)) gc = SDL_GameControllerOpen(i);
    }
}

/* ---- the menu (Esc): drawn by the host over the frozen game, which never knows ---- */

/* a 5x7 font: A-Z, 0-9 and a little punctuation; each row is 5 bits, the top bit leftmost */
static const char FONT7_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:?>-./[];,'=+\\`()";
static const uint8_t FONT7[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},      {7,2,2,2,2,18,12},      {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,17,25,21,19,17,17}, {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30},   {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},  {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,17,10,4,4,4},    {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},      {14,17,1,2,4,8,31},     {31,2,4,2,1,17,14},
    {2,6,10,18,31,2,2},     {31,16,30,1,1,17,14},   {6,8,16,30,17,17,14},   {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12},
    {0,12,12,0,12,12,0},    {14,17,1,2,4,0,4},      {8,4,2,1,2,4,8},        {0,0,0,31,0,0,0},
    {0,0,0,0,0,12,12},      {1,1,2,4,8,16,16},
    {14,8,8,8,8,8,14},      {14,2,2,2,2,2,14},      {0,12,12,0,12,4,8},     {0,0,0,0,12,4,8},
    {4,4,8,0,0,0,0},        {0,0,31,0,31,0,0},      {0,4,4,31,4,4,0},       {16,16,8,4,2,1,1},
    {8,4,2,0,0,0,0},        {2,4,8,8,8,4,2},        {8,4,2,2,2,4,8},
};

static void draw_text(int x, int y, const char *s, uint32_t color)
{
    for (; *s; s++, x += 6) {
        const char *k = strchr(FONT7_CHARS, *s);
        if (*s == ' ' || !k) continue;
        for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++)
            if (FONT7[k - FONT7_CHARS][r] >> (4 - c) & 1 && x + c >= 0 && x + c < FRAME_STRIDE && y + r >= 0 && y + r < ACTIVE_LINES)
                frame_rgb[y + r][x + c] = color;
    }
}

static void draw_text_centred(int y, const char *s, uint32_t color) { draw_text(frame_width / 2 - (int)strlen(s) * 3, y, s, color); }

static void present(void)
{
    SDL_UpdateTexture(tex, NULL, &frame_rgb[0][0], FRAME_STRIDE * 4);
    SDL_Rect src = {0, 0, frame_width, ACTIVE_LINES};              /* only the width the game uses */
    SDL_RenderSetLogicalSize(ren, frame_width, ACTIVE_LINES);
    SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, &src, NULL); SDL_RenderPresent(ren);
}

enum { MENU_WHITE = 0xFFFFFF, MENU_GREY = 0x8890A0, MENU_GOLD = 0xFFD860, MENU_BOX = 0x101830 };

static void draw_box(int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++)
        frame_rgb[y][x] = (y == y0 || y == y0 + h - 1 || x == x0 || x == x0 + w - 1) ? MENU_GREY : MENU_BOX;
}

/* the message at the bottom of the picture ("STATE SAVED") */
static void draw_toast(void)
{
    int w = (int)strlen(toast) * 6 + 9, h = 15, x0 = frame_width / 2 - w / 2, y0 = ACTIVE_LINES - h - 8;
    draw_box(x0, y0, w, h);
    draw_text(x0 + 5, y0 + 4, toast, MENU_WHITE);
}

/* the menu's screens: the main one, the exit question, the controls, and the controls waiting
 * for a press (capture) */
enum { SCR_MAIN, SCR_ASK, SCR_CONTROLS, SCR_CAPTURE };
enum { MI_FPS, MI_FULL, MI_PIXELS, MI_WIDE, MI_SOUND, MI_CONTROLS, MI_EXIT, MAIN_N };
static const char *const MAIN_ITEM[MAIN_N] = {"SHOW FPS", "FULL SCREEN", "WHOLE PIXELS", "WIDESCREEN", "SOUND", "CONTROLS", "EXIT GAME"};
static const int CTRL_ROW[ACTIONS] = {ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_A, ACT_B, ACT_C, ACT_START, ACT_SAVE, ACT_LOAD};
enum { CR_STICK = ACTIONS, CR_RESET, CR_BACK, CTRL_N };   /* the controls' rows after the actions */
enum { COL_KEY = 80, COL_PAD = 152 };              /* the controls' columns, from the box's left */
#define CAPTURE_WAIT 330                            /* menu passes (15 ms) to wait for a press: 5 s */
typedef struct { int screen, item, yes, row, col, wait; } Menu;

/* the picture darkened, a box, and the screen */
static void draw_menu(const Menu *m)
{
    for (int y = 0; y < ACTIVE_LINES; y++) for (int x = 0; x < frame_width; x++)
        frame_rgb[y][x] = (frame_rgb[y][x] >> 2) & 0x3F3F3F;
    if (m->screen == SCR_MAIN) {
        int w = 150, h = 44 + MAIN_N * 14, x0 = frame_width / 2 - w / 2, y0 = ACTIVE_LINES / 2 - h / 2;
        draw_box(x0, y0, w, h);
        draw_text_centred(y0 + 8, "PAUSED", MENU_WHITE);
        for (int i = 0; i < MAIN_N; i++) {
            uint32_t col = i == m->item ? MENU_GOLD : MENU_WHITE;
            int y = y0 + 24 + i * 14;
            int on = i == MI_FPS ? show_fps : i == MI_FULL ? fullscreen : i == MI_PIXELS ? integer_scale : i == MI_WIDE ? widescreen : i == MI_SOUND ? !mute : -1;
            if (i == m->item) draw_text(x0 + 12, y, ">", col);
            draw_text(x0 + 24, y, MAIN_ITEM[i], col);
            if (on >= 0) draw_text(x0 + w - 12 - 18, y, on ? "ON" : "OFF", col);
        }
        draw_text_centred(y0 + h - 14, gc ? "ESC/SELECT: RESUME" : "ESC: RESUME", MENU_GREY);
    } else if (m->screen == SCR_ASK) {
        int w = 150, h = 74, x0 = frame_width / 2 - w / 2, y0 = ACTIVE_LINES / 2 - h / 2;
        draw_box(x0, y0, w, h);
        draw_text_centred(y0 + 12, "EXIT THE GAME?", MENU_WHITE);
        int y = y0 + 34, cx = frame_width / 2;
        draw_text(cx - 40, y, m->yes ? " NO" : ">NO", m->yes ? MENU_WHITE : MENU_GOLD);
        draw_text(cx + 16, y, m->yes ? ">YES" : " YES", m->yes ? MENU_GOLD : MENU_WHITE);
        draw_text_centred(y0 + h - 14, gc ? "ESC/SELECT: BACK" : "ESC: BACK", MENU_GREY);
    } else {
        int w = 236, h = 56 + CTRL_N * 11, x0 = frame_width / 2 - w / 2, y0 = ACTIVE_LINES / 2 - h / 2;
        draw_box(x0, y0, w, h);
        draw_text_centred(y0 + 6, "CONTROLS", MENU_WHITE);
        draw_text(x0 + COL_KEY, y0 + 18, "KEYBOARD", MENU_GREY);
        draw_text(x0 + COL_PAD, y0 + 18, "CONTROLLER", MENU_GREY);
        for (int r = 0; r < CTRL_N; r++) {
            int y = y0 + 30 + r * 11, sel = r == m->row;
            uint32_t col = sel ? MENU_GOLD : MENU_WHITE;
            if (sel) draw_text(x0 + 6, y, ">", col);
            if (r < ACTIONS) {
                int a = CTRL_ROW[r];
                draw_text(x0 + 14, y, ACT_NAME[a], col);
                for (int c = 0; c < 2; c++) {
                    int here = sel && m->col == c;
                    const char *t = here && m->screen == SCR_CAPTURE ? "PRESS..." : c ? pad_label(pad_bind[a]) : key_label(key_bind[a]);
                    draw_text(x0 + (c ? COL_PAD : COL_KEY), y, t, here ? MENU_GOLD : MENU_WHITE);
                }
            } else if (r == CR_STICK) {
                draw_text(x0 + 14, y, "LEFT STICK AS D-PAD", col);
                draw_text(x0 + COL_PAD, y, stick_moves ? "ON" : "OFF", col);
            } else draw_text(x0 + 14, y, r == CR_RESET ? "RESET TO DEFAULTS" : "BACK", col);
        }
        if (m->screen == SCR_CAPTURE) {
            draw_text_centred(y0 + h - 24, m->col ? "PRESS A BUTTON, OR MOVE A STICK" : "PRESS A KEY", MENU_GOLD);
            draw_text_centred(y0 + h - 13, gc ? "ESC/SELECT: CANCEL" : "ESC: CANCEL", MENU_GREY);
        } else {
            draw_text_centred(y0 + h - 24, gc ? "ENTER/A: CHANGE   DEL/Y: CLEAR" : "ENTER: CHANGE   DEL: CLEAR", MENU_GREY);
            draw_text_centred(y0 + h - 13, gc ? "ESC/B: BACK" : "ESC: BACK", MENU_GREY);
        }
    }
}

/* the left stick past AXIS_ON: up, down, left, right; the menu moves when it gets there */
static int stick_was[4];

static void stick_edges(int axis, int v, int *lo_edge, int *hi_edge)
{
    if (axis != SDL_CONTROLLER_AXIS_LEFTX && axis != SDL_CONTROLLER_AXIS_LEFTY) return;
    int *was = &stick_was[axis == SDL_CONTROLLER_AXIS_LEFTY ? 0 : 2], lo = v < -AXIS_ON, hi = v > AXIS_ON;
    if (lo_edge) *lo_edge = lo && !was[0];
    if (hi_edge) *hi_edge = hi && !was[1];
    was[0] = lo; was[1] = hi;
}

/* the controls waiting for a press for the chosen cell: a key for the keyboard's column; a
 * button, a stick's direction or a trigger for the controller's. Esc and Back/Select cancel;
 * the host's own keys (F11, F3, Alt+Enter) and the Home button are not taken. */
static void capture(Menu *m, const SDL_Event *e)
{
    int a = CTRL_ROW[m->row];
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        SDL_Keycode key = e->key.keysym.sym; SDL_Scancode sc = e->key.keysym.scancode;
        if (key == SDLK_ESCAPE) m->screen = SCR_CONTROLS;
        else if (m->col == 0 && sc != SDL_SCANCODE_UNKNOWN && key != SDLK_F11 && key != SDLK_F3
                 && !(key == SDLK_RETURN && (e->key.keysym.mod & KMOD_ALT))) {
            bind_key(a, sc); ini_dirty = 1; m->screen = SCR_CONTROLS;
        }
    }
    if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        int b = e->cbutton.button;
        if (b == SDL_CONTROLLER_BUTTON_BACK) m->screen = SCR_CONTROLS;
        else if (m->col == 1 && b != SDL_CONTROLLER_BUTTON_GUIDE) { bind_pad(a, b); ini_dirty = 1; m->screen = SCR_CONTROLS; }
    }
    if (e->type == SDL_CONTROLLERAXISMOTION) {
        int ax = e->caxis.axis, v = e->caxis.value;
        stick_edges(ax, v, NULL, NULL);             /* so that the stick held does not move the menu after */
        if (m->col == 1 && (v > 24000 || v < -24000)) { bind_pad(a, PAD_AXIS + ax * 2 + (v < 0)); ini_dirty = 1; m->screen = SCR_CONTROLS; }
    }
}

/* one input on a screen of the menu: 0 to resume, 1 to exit the game, -1 to stay */
static int menu_step(Menu *m, int only_asking, int up, int down, int left, int right, int ok, int back, int clear)
{
    switch (m->screen) {
    case SCR_MAIN:
        if (up) m->item = (m->item + MAIN_N - 1) % MAIN_N;
        if (down) m->item = (m->item + 1) % MAIN_N;
        if (ok || left || right) switch (m->item) {
            case MI_FPS: toggle_fps(); break;
            case MI_FULL: toggle_fullscreen(); break;
            case MI_PIXELS:
                integer_scale = cfg.integer_scale = !integer_scale; ini_dirty = 1;
                SDL_RenderSetIntegerScale(ren, integer_scale ? SDL_TRUE : SDL_FALSE);
                break;
            case MI_WIDE: widescreen = cfg.widescreen = !widescreen; wide_apply(); ini_dirty = 1; break;
            case MI_SOUND: mute = cfg.mute = !mute; ini_dirty = 1; break;
            case MI_CONTROLS: if (ok) { m->screen = SCR_CONTROLS; m->row = 0; m->col = 0; } break;
            case MI_EXIT: if (ok) { m->screen = SCR_ASK; m->yes = 0; } break;
        }
        if (back) return 0;
        break;
    case SCR_ASK:
        if (up || down || left || right) m->yes ^= 1;
        if (ok && m->yes) return 1;
        if ((ok && !m->yes) || back) {
            if (only_asking) return 0;
            m->screen = SCR_MAIN;
        }
        break;
    case SCR_CONTROLS:
        if (up) m->row = (m->row + CTRL_N - 1) % CTRL_N;
        if (down) m->row = (m->row + 1) % CTRL_N;
        if (m->row < ACTIONS) {
            int a = CTRL_ROW[m->row];
            if (left || right) m->col ^= 1;
            if (ok) { m->screen = SCR_CAPTURE; m->wait = CAPTURE_WAIT; }
            if (clear) { if (m->col) pad_bind[a] = PAD_NONE; else key_bind[a] = SDL_SCANCODE_UNKNOWN; ini_dirty = 1; }
        } else if (m->row == CR_STICK && (ok || left || right)) { stick_moves = !stick_moves; ini_dirty = 1; }
        else if (m->row == CR_RESET && ok) { controls_default(); ini_dirty = 1; }
        else if (m->row == CR_BACK && ok) m->screen = SCR_MAIN;
        if (back) m->screen = SCR_MAIN;
        break;
    }
    return -1;
}

/* Run the menu until it closes; the game is frozen meanwhile (no frames, no sound time). The
 * menu itself always answers to the arrows, Enter and Esc, and on a controller to the D-pad (or
 * left stick), A and B, whatever the game's controls are.
 * asking: start at the exit question (closing the window). Returns 1 to exit the game. */
static void menu_test_keys(void);

static int menu(int asking)
{
    static uint32_t saved[ACTIVE_LINES][FRAME_STRIDE];
    memcpy(saved, frame_rgb, sizeof saved);
    Menu m = {asking ? SCR_ASK : SCR_MAIN, 0, 0, 0, 0, 0};
    int only_asking = asking, result = -1;
    while (result < 0) {
        SDL_Event e;
        while (result < 0 && SDL_PollEvent(&e)) {
            int up = 0, down = 0, left = 0, right = 0, ok = 0, back = 0, clear = 0;
            controller_event(&e);
            if (m.screen == SCR_CAPTURE) { capture(&m, &e); continue; }
            if (e.type == SDL_QUIT) { m.screen = SCR_ASK; m.yes = 0; }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
                if (key == SDLK_F11 || (key == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    if (!e.key.repeat) toggle_fullscreen();
                    continue;
                }
                if (key == SDLK_F3 && !e.key.repeat) toggle_fps();
                up = key == SDLK_UP; down = key == SDLK_DOWN; left = key == SDLK_LEFT; right = key == SDLK_RIGHT;
                ok = !e.key.repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_z);
                back = !e.key.repeat && (key == SDLK_ESCAPE || key == SDLK_x);
                clear = !e.key.repeat && (key == SDLK_DELETE || key == SDLK_BACKSPACE);
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                int b = e.cbutton.button;
                up = b == SDL_CONTROLLER_BUTTON_DPAD_UP; down = b == SDL_CONTROLLER_BUTTON_DPAD_DOWN;
                left = b == SDL_CONTROLLER_BUTTON_DPAD_LEFT; right = b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                ok = b == SDL_CONTROLLER_BUTTON_A;
                back = b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_BACK;
                clear = b == SDL_CONTROLLER_BUTTON_Y;
            }
            if (e.type == SDL_CONTROLLERAXISMOTION)
                stick_edges(e.caxis.axis, e.caxis.value, e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? &up : &left,
                            e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? &down : &right);
            result = menu_step(&m, only_asking, up, down, left, right, ok, back, clear);
        }
        if (result >= 0) break;
        if (m.screen == SCR_CAPTURE && --m.wait <= 0) m.screen = SCR_CONTROLS;   /* nothing pressed */
        memcpy(frame_rgb, saved, sizeof saved);
        draw_menu(&m);
        if (show_fps) draw_fps();
        present();
        const char *shots = getenv("WBMW_MENU_SHOTS");   /* testing: each screen of the menu as a PNG */
        if (shots) {
            static int n; static unsigned last = ~0u;
            unsigned state = (unsigned)(m.screen | m.item << 2 | m.yes << 5 | m.row << 6 | m.col << 10 | show_fps << 11
                                        | fullscreen << 12 | integer_scale << 13 | mute << 14 | stick_moves << 15);
            for (int a = 0; a < ACTIONS; a++) state = state * 31 + (unsigned)key_bind[a] * 7 + (unsigned)(pad_bind[a] + 1);
            if (state != last) {
                char path[1024]; snprintf(path, sizeof path, "%s/menu_%02d.png", shots, n++);
                png_write_rgb(path, &frame_rgb[0][0], frame_width, ACTIVE_LINES, FRAME_STRIDE);
                last = state;
            }
        }
        menu_test_keys();
        SDL_Delay(15);
    }
    memcpy(frame_rgb, saved, sizeof saved);
    if (ini_dirty) settings_save();
    return result;
}

/* testing: WBMW_MENU_KEYS="escape return down ..." (SDL key names) are pressed one per frame
 * (or per menu screen) after the first 120 frames of the session, as if typed */
static void menu_test_keys(void)
{
    static const char *keys; static int started, calls;
    if (!started) { keys = getenv("WBMW_MENU_KEYS"); started = 1; }
    if (!keys || !*keys || ++calls < 120) return;
    char name[32]; int n = 0;
    while (*keys == ' ') keys++;
    while (*keys && *keys != ' ' && n < 31) name[n++] = *keys++;
    name[n] = 0;
    if (!n) return;
    SDL_Event e = {0};
    e.type = SDL_KEYDOWN; e.key.keysym.sym = SDL_GetKeyFromName(name); e.key.keysym.scancode = SDL_GetScancodeFromKey(e.key.keysym.sym);
    SDL_PushEvent(&e);
}

/* buttons held when the menu closed: kept from the game until released */
static uint8_t held_over;

static void window_open(void)
{
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");   /* buttons by position, on every make of pad */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); exit(1); }
    /* the game is 256 pixels wide almost everywhere (a few screens are 320): size the window for
     * 256 (400 with widescreen), at the largest whole scale up to 3 that fits the screen */
    int w = widescreen ? 256 + 2 * WIDE_PX : 256;
    if (scale <= 0) {
        SDL_Rect r; scale = 3;
        if (SDL_GetDisplayUsableBounds(0, &r) == 0)
            while (scale > 1 && (224 * scale > r.h * 9 / 10 || w * scale > r.w * 9 / 10)) scale--;
    }
    win = SDL_CreateWindow(TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * scale, 224 * scale, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetIntegerScale(ren, integer_scale ? SDL_TRUE : SDL_FALSE);   /* whole multiples only: even pixels */
    if (fullscreen) set_fullscreen(1);
    fps_start = SDL_GetPerformanceCounter();
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, FRAME_STRIDE, 224);
    next_tick = SDL_GetPerformanceCounter();
}

static void quit(int code)
{
    if (eeprom_save(save_path) == 0) fprintf(stderr, "saved save data to %s\n", save_path);
    if (ini_dirty) settings_save();
    SDL_Quit();
    exit(code);
}

/* show the frame, keep 60 Hz, read the keyboard and controller (through the bindings) */
static void quick_save(void) { save_pending = frame; save_to = state_path; }   /* at the next frame wait */

static void window_frame(void)
{
    SDL_Event e;
    menu_test_keys();
    int open_menu = -1, load = 0;                   /* open_menu 0: the menu, 1: straight to the exit question */
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) open_menu = 1;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !e.key.repeat) open_menu = 0;
        /* on a controller, Back/Select (View, Share/Create, Minus), which the Genesis pad does not
         * have: Start stays the game's */
        if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) open_menu = 0;
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode key = e.key.keysym.sym; SDL_Scancode sc = e.key.keysym.scancode;
            if (key == SDLK_F11 || (key == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) toggle_fullscreen();
            else if (key == SDLK_F3) toggle_fps();
            else if (sc != SDL_SCANCODE_UNKNOWN && sc == key_bind[ACT_SAVE]) quick_save();
            else if (sc != SDL_SCANCODE_UNKNOWN && sc == key_bind[ACT_LOAD]) load = 1;
        }
        controller_event(&e);
    }
    {                                               /* the controller's quick save and load, when pressed */
        static int was_save, was_load;
        int sv = pad_down(pad_bind[ACT_SAVE]), ld = pad_down(pad_bind[ACT_LOAD]);
        if (sv && !was_save) quick_save();
        if (ld && !was_load) load = 1;
        was_save = sv; was_load = ld;
    }
    if (load) request_load(state_path);
    if (open_menu >= 0) {
        if (menu(open_menu)) quit(0);
        Uint64 now = SDL_GetPerformanceCounter();   /* carry on as if no time had passed */
        next_tick = now; fps_start = now; fps_frames = 0;
        held_over = 0xFF;                           /* whatever is still held, until released */
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    for (int a = 0; a < ACT_SAVE; a++) if (key_down(k, key_bind[a]) || pad_down(pad_bind[a])) b |= (uint8_t)(1 << a);
    if (gc && stick_moves) {
        int x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        b |= (uint8_t)((y < -AXIS_ON ? 1 : 0) | (y > AXIS_ON ? 2 : 0) | (x < -AXIS_ON ? 4 : 0) | (x > AXIS_ON ? 8 : 0));
    }
    held_over &= b;                                 /* released: the game sees it again */
    pad_buttons = b & (uint8_t)~held_over;
    Uint64 freq = SDL_GetPerformanceFrequency(), t = SDL_GetPerformanceCounter();
    fps_frames++;
    if (t - fps_start >= freq) {                     /* once a second: the rate, in the title too */
        fps = fps_frames * (double)freq / (double)(t - fps_start);
        fps_frames = 0; fps_start = t;
        char title[256]; snprintf(title, sizeof title, "%s  %.1f fps", TITLE, fps);
        SDL_SetWindowTitle(win, title);
    }
    if (show_fps) draw_fps();
    if (toast_frames > 0) { toast_frames--; draw_toast(); }
    present();
    next_tick += (Uint64)(freq / 59.922);
    Uint64 now = SDL_GetPerformanceCounter();
    if (now < next_tick) SDL_Delay((Uint32)((next_tick - now) * 1000 / freq));
    else if (now - next_tick > freq / 4) next_tick = now;      /* fell far behind: do not race */
}
#endif

/* A frame is complete (drawn): write it out or show it, and keep the frame count. */
static void frame_done(void)
{
    frame++;
    if (headless) {
        if (png_every && frame % png_every == 0) {
            char path[1024]; snprintf(path, sizeof path, "%s/frame_%05d.png", out_dir, frame);
            png_write_rgb(path, &frame_rgb[0][0], frame_width, ACTIVE_LINES, FRAME_STRIDE);
        }
        { static int16_t snd[4096 * 2]; int n = sound_take(snd, 4096); if (wav_path) wav_write(snd, n); }
        if (frame >= frames_max) { wav_close(); framelog_close(); exit(0); }
        if (!framelog_input_active()) pad_buttons = buttons_at(frame + 1);
    }
#ifndef WBMW_HEADLESS_ONLY
    else { window_frame(); audio_frame(mute); }
#endif
}

/* ---- timing (--timing): the 68000's cycles, as the harness runs them on Musashi ----
 * The harness runs each scanline as 8 slices of 61 cycles (core.c). An instruction's cycles are
 * charged after it runs; a slice ends at the first instruction boundary at or past zero, and the
 * overshoot is dropped. Interrupts are taken at the start of a slice, or just after an
 * instruction that changed the status register. The game reads the raster as (line, start of
 * the slice). Lines render and play their sound at their end; VBlank comes at line 224. This
 * reproduces that loop around the translated code, so time, and everything that reads it, runs
 * as in the harness. Without --timing the code takes no time (the poll points end each frame). */
static int timing;
int32_t rt_budget = 0x3FFFFFFF;
int rt_irq_check;
static int line, slice, hcount, sr_mask = 7, vint_pending, hint_pending;
#define SLICE 61

void rt_set_sr(Cpu *c, uint16_t sr)
{
    c->ccr = (uint16_t)(sr & 0x1F);
    sr_mask = (sr >> 8) & 7;
    rt_irq_check = 1;                               /* Musashi checks for an interrupt at once */
}

/* the level of an interrupt that would be taken now, or 0 */
static int irq_level(void)
{
    int level = vint_pending ? 6 : hint_pending ? 4 : 0;
    return level > sr_mask ? level : 0;
}

/* take it: acknowledge, push the 68000's frame (SR, then PC), raise the mask, 44 cycles */
static int take_irq(Cpu *c, uint32_t next)
{
    int level = irq_level();
    if (!level) return 0;
    if (level == 6) { vint_pending = 0; vdp_set_vint_pending(0); } else hint_pending = 0;
    c->a[7] -= 6;
    wr16(c->a[7], (uint16_t)(0x2000 | sr_mask << 8 | c->ccr)); wr32(c->a[7] + 2, next);
    sr_mask = level;
    rt_budget -= 44;
    return level;
}

static void start_line(void)
{
    vdp_line = line;
    if (line == ACTIVE_LINES) {
        vdp_set_vblank(1); vdp_set_vint_pending(1);
        if (vdp_vint_enabled()) vint_pending = 1;
    }
    if (line == 0) { vdp_set_vblank(0); hcount = vdp_hint_counter_reload(); }
}

static void end_line(void)
{
    if (line < ACTIVE_LINES) {
        vdp_render_line(line);
        if (--hcount < 0) { hcount = vdp_hint_counter_reload(); if (vdp_hint_enabled()) hint_pending = 1; }
    } else hcount = vdp_hint_counter_reload();
    sound_run_line(line == ACTIVE_LINES);
}

void rt_slice(Cpu *c, uint32_t next)
{
    if (!timing) { rt_irq_check = 0; rt_budget = 0x3FFFFFFF; return; }
    int level = 0;
    if (rt_irq_check) { rt_irq_check = 0; level = take_irq(c, next); }
    while (rt_budget <= 0) {                        /* the slice is used up: on to the next */
        if (++slice == 8) {
            end_line(); slice = 0;
            if (++line == LINES_PER_FRAME) { line = 0; frame_done(); maybe_save(c, next); }
            start_line();
        }
        vdp_line_cycle = slice * SLICE;
        rt_budget = SLICE;
        if (!level) level = take_irq(c, next);
    }
    if (level) {                                    /* the handler: only the VBlank's is ever enabled */
        if (level != 6) { fprintf(stderr, "wbmw: interrupt level %d, which the game never enables\n", level); exit(4); }
        uint32_t ret = c->ret;
        machine_isr(c);                             /* ends in rte: SR (with the mask) and PC popped */
        c->ret = ret;
    }
}

/* ---- save states ----
 * A state is taken only where the game waits for VBlank in $3B8 ($3D6-$3FE), called from the
 * frame loop (the stack holds just its return address, $4BF6). Every instruction there is a
 * translated entry, so a state resumes by starting the code at its PC: no C call stack needed.
 * That is also where the harness's states stop (between frames), so they load here too. With
 * --timing a loaded state starts at line 0, slice 0, as the harness's next frame would. */
static const char *state_path = "wbmw.state";   /* F2 saves here, F4 and --load load it */
static int save_pending = -1;                       /* the frame from which to save, or -1 */

/* GCC's own setjmp and longjmp: they only restore the registers. MinGW's longjmp unwinds the
 * stack the Windows way (SEH), which fails (STATUS_BAD_STACK) once the thread runs fibers
 * (port/coro.c), as it does from the first task pass on. */
static void *restart[5];
static Cpu *the_cpu;
static uint32_t resume_pc;

static int resumable(Cpu *c, uint32_t pc)
{
    return pc >= 0x3D6 && pc <= 0x3FE && (c->a[7] & 0xFFFFFF) == 0xFF0BFC && rd32(0xFF0BFC) == 0x4BF6;
}

static void state_regs(FILE *f, Cpu *c, uint32_t *pc, int saving)
{
    /* the harness's order: SR, USP, ISP, D0-D7, A0-A6, PC (core.c) */
    unsigned regs[19] = {0};
    if (saving) {
        regs[0] = 0x2000 | (unsigned)sr_mask << 8 | c->ccr; regs[2] = c->a[7];
        for (int i = 0; i < 8; i++) regs[3 + i] = c->d[i];
        for (int i = 0; i < 7; i++) regs[11 + i] = c->a[i];
        regs[18] = *pc;
    }
    state_io(f, regs, sizeof regs, saving);
    if (!saving) {
        c->ccr = (uint16_t)(regs[0] & 0x1F); sr_mask = (int)(regs[0] >> 8) & 7; c->a[7] = regs[2];
        for (int i = 0; i < 8; i++) c->d[i] = regs[3 + i];
        for (int i = 0; i < 7; i++) c->a[i] = regs[11 + i];
        *pc = regs[18] & 0xFFFFFF;
    }
    int irq[2] = {vint_pending, hint_pending};
    state_io(f, irq, sizeof irq, saving);
    if (!saving) { vint_pending = irq[0]; hint_pending = irq[1]; }
}

static int save_state(const char *path, Cpu *c, uint32_t pc)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    state_failed = 0;
    char magic[8]; memcpy(magic, "WBMWST02", 8);
    state_io(f, magic, 8, 1); state_io(f, &frame, sizeof frame, 1);
    state_regs(f, c, &pc, 1);
    bus_state_io(f, 1); vdp_state_io(f, 1); sound_state_io(f, 1);
    fclose(f);
    if (state_failed) { fprintf(stderr, "%s: writing failed\n", path); return -1; }
    fprintf(stderr, "saved state at frame %d to %s\n", frame, path);
    toast = "STATE SAVED"; toast_frames = 90;
    return 0;
}

static int load_state(const char *path, Cpu *c, uint32_t *pc)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    state_failed = 0;
    char magic[8];
    state_io(f, magic, 8, 0);
    int version = !memcmp(magic, "WBMWST02", 8) ? 2 : !memcmp(magic, "WBMWST01", 8) ? 1 : 0;
    if (!version) { fprintf(stderr, "%s: not a save state\n", path); fclose(f); return -1; }
    state_io(f, &frame, sizeof frame, 0);
    state_regs(f, c, pc, 0);
    bus_state_io(f, 0); vdp_state_io(f, 0);
    if (version >= 2) sound_state_io(f, 0); else { sound_reset(); sound_set_reset(0); }
    fclose(f);
    if (state_failed) { fprintf(stderr, "%s: reading failed\n", path); return -1; }
    if (!resumable(c, *pc)) {
        fprintf(stderr, "%s: the game is not waiting for a frame there (PC %06X); save it again a moment later\n", path, (unsigned)*pc);
        return -1;
    }
    task_reset();                                   /* port/task.c: the waiting passes are stale now */
    if (timing) { line = 0; slice = 0; start_line(); vdp_line_cycle = 0; rt_budget = SLICE; rt_irq_check = 1; }
    else { vdp_line = 0; vdp_set_vblank(0); }       /* untimed code runs at line 0 */
    fprintf(stderr, "loaded state from %s (frame %d)\n", path, frame);
    toast = "STATE LOADED"; toast_frames = 90;
    return 0;
}

/* load (F4): at the frame loop's own frame wait, like a save. Only there is the C on the main
 * stack: a wait inside a task pass runs on that pass's coroutine (port/coro.c, a fiber), and
 * jumping from there to main's stack crashes. */
static const char *load_pending;
static void request_load(const char *path) { load_pending = path; }

/* a frame boundary with the game at `pc` (the next instruction): save or load if asked and
 * possible. A load abandons the C call stack and starts the code at the state's PC (main). */
static void maybe_save(Cpu *c, uint32_t pc)
{
    if (load_pending && resumable(c, pc)) {
        const char *path = load_pending;
        load_pending = NULL;
        if (load_state(path, the_cpu, &pc) == 0) { resume_pc = pc; __builtin_longjmp(restart, 1); }
        toast = "NO STATE TO LOAD"; toast_frames = 90;
    }
    if (save_pending < 0 || frame < save_pending || !resumable(c, pc)) return;
    save_state(save_to, c, pc);
    save_pending = -1;
}

/* Called by the generated code at a poll point. Without timing, a wait for VBlank ($3DC, $C262)
 * is the end of the frame: draw it, show it and deliver the interrupt; $38E is QueueSound waiting
 * on the Z80, where sound time is let pass. It must come after $386 gives the Z80 its bus back:
 * the Z80 does not run while the 68000 holds it. With timing, time passes by itself. */
void rt_poll(Cpu *c, uint32_t at)
{
    framelog_at(at);                                /* --frame-log, --input-game, --dump-at */
    if (timing) return;
    if (at == 0x38E) {
        static int reported, last = -1;             /* rare: the owner's shop hang came through here */
        if (reported < 10 && frame != last) { fprintf(stderr, "wbmw: QueueSound waited for the sound driver (frame %d)\n", frame); reported++; last = frame; }
        sound_idle(); return;
    }
    if (ram[0x8006] & 1) return;                    /* the interrupt already came this frame */
    maybe_save(c, at);                              /* the game waits for VBlank: a state can be taken */
    vdp_pad_fill = widescreen && wide_room(ram[0x9668], ram[0x9669]);
    for (int l = 0; l < ACTIVE_LINES; l++) { vdp_line = l; vdp_render_line(l); }
    vdp_line = 0;                                   /* the game's own time runs at line 0 */
    for (int l = 0; l < LINES_PER_FRAME; l++) sound_run_line(l == ACTIVE_LINES);   /* a frame of sound */
    frame_done();
    if (!vdp_vint_enabled()) return;                /* the game turned the interrupt off */
    c->a[7] -= 6;                                   /* the 68000's interrupt frame: SR, then PC */
    wr16(c->a[7], (uint16_t)(0x2000 | sr_mask << 8 | c->ccr)); wr32(c->a[7] + 2, at);
    uint32_t ret = c->ret;
    machine_isr(c);                                 /* $4B5C, ending in rte: SR and PC popped */
    c->ret = ret;
}

/* an error the player should see even when the program was started by a double click (its
 * console closes at once): in a message box too */
static void fail(const char *msg)
{
    fprintf(stderr, "wbmw: %s\n", msg);
#ifndef WBMW_HEADLESS_ONLY
    if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Wonder Boy in Monster World", msg, NULL);
#endif
}

/* SHA-1 (FIPS 180-1), to tell the player whether their ROM is the dump this port was made from */
static void sha1(const uint8_t *data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t bits = (uint64_t)len * 8;
    size_t total = ((len + 8) / 64 + 1) * 64;
    for (size_t off = 0; off < total; off += 64) {
        uint8_t blk[64];
        for (int i = 0; i < 64; i++) {
            size_t k = off + (size_t)i;
            blk[i] = k < len ? data[k] : k == len ? 0x80 : k >= total - 8 ? (uint8_t)(bits >> (8 * (total - 1 - k))) : 0;
        }
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)blk[4 * i] << 24 | blk[4 * i + 1] << 16 | blk[4 * i + 2] << 8 | blk[4 * i + 3];
        for (int i = 16; i < 80; i++) { uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = x << 1 | x >> 31; }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = (a << 5 | a >> 27) + f + e + k + w[i];
            e = d; d = c; c = b << 30 | b >> 2; b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 20; i++) out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
}

#define ROM_NAME "Wonder Boy in Monster World (USA, Europe).md"
#define ROM_SHA1 "87a968f773c7e807e647c0737132457b06b78276"

/* the ROM when none is named: rom/ here, next to the program, or the repository's rom/ (the
 * program is built into work/native/) */
static const char *find_rom(const char *argv0)
{
    static char path[4096];
    char dir[4096] = ".";
    const char *slash = strrchr(argv0, '/'), *bs = strrchr(argv0, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
    if (slash && (size_t)(slash - argv0) < sizeof dir) { memcpy(dir, argv0, (size_t)(slash - argv0)); dir[slash - argv0] = 0; }
    const char *tries[] = { "rom/" ROM_NAME, ROM_NAME, "%s/rom/" ROM_NAME, "%s/" ROM_NAME, "%s/../../rom/" ROM_NAME };
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof path, tries[i], dir);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return path; }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *input = NULL, *load_path = NULL;
    const char *state_path_ptr = "\x01";            /* --load: the --state file, whatever it ends up being */
#ifndef WBMW_HEADLESS_ONLY
    {                                               /* the settings file first: the command line overrides it */
        const char *config = NULL; int plays = 1;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--frames")) plays = 0;
            if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[i + 1];
        }
        if (plays) settings_open(config);
    }
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) { frames_max = atoi(argv[++i]); headless = 1; }
        else if (!strcmp(argv[i], "--png-every") && i + 1 < argc) png_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--frame-log") && i + 1 < argc) { if (framelog_open(argv[++i])) return 1; }
        else if (!strcmp(argv[i], "--input-game") && i + 1 < argc) { if (framelog_input(argv[++i])) return 1; }
        else if (!strcmp(argv[i], "--dump-at") && i + 2 < argc) { long n = atol(argv[++i]); framelog_dump_at(n, argv[++i]); }
        else if (!strcmp(argv[i], "--mute")) mute = 1;
        else if (!strcmp(argv[i], "--widescreen")) widescreen = 1;
        else if (!strcmp(argv[i], "--no-widescreen")) widescreen = 0;
        else if (!strcmp(argv[i], "--timing")) fprintf(stderr, "wbmw: --timing counts the translated code's cycles; the hand-written code has none, so it is off\n");
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) state_path = argv[++i];
        else if (!strcmp(argv[i], "--load")) load_path = state_path_ptr;
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_path = argv[++i];
        else if (!strcmp(argv[i], "--save-state-at") && i + 2 < argc) { save_pending = atoi(argv[++i]); save_to = argv[++i]; }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save_path = argv[++i];
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) i++;
#ifndef WBMW_HEADLESS_ONLY
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--windowed")) fullscreen = 0;
        else if (!strcmp(argv[i], "--integer-scale")) integer_scale = 1;
        else if (!strcmp(argv[i], "--fps")) show_fps = 1;
#endif
        else rom_path = argv[i];
    }
    if (!rom_path) rom_path = find_rom(argv[0]);
    if (!rom_path) {
        fprintf(stderr, "usage: wbmw [ROM] [--windowed] [--widescreen] [--scale N] [--save FILE] [--integer-scale] [--fps] [--mute] [--config FILE]\n"
                        "       wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE] [--wav FILE]\n"
                        "                [--frame-log FILE] [--input-game FILE] [--dump-at N DIR]\n"
                        "       states: [--state FILE] [--load] [--load-state FILE] [--save-state-at N FILE]; F2 saves, F4 loads\n");
        fail("No ROM found. Put your own dump of the game, named\n\n    " ROM_NAME "\n\n"
             "in a folder called rom (in the repository, or next to wbmw), or give its path:\n\n    wbmw path/to/your.rom");
        return 2;
    }
    FILE *f = fopen(rom_path, "rb");
    if (!f) { char m[4200]; snprintf(m, sizeof m, "Cannot open the ROM: %s", rom_path); fail(m); return 1; }
    fseek(f, 0, SEEK_END); rom_size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(rom_size);
    if (fread(rom, 1, rom_size, f) != rom_size) { char m[4200]; snprintf(m, sizeof m, "Cannot read the ROM: %s", rom_path); fail(m); return 1; }
    fclose(f);
    if (rom_size != 786432) {
        char m[4300];
        snprintf(m, sizeof m, "%s is %lu bytes; the game's ROM is 786,432 bytes (768 KB). Use a plain dump of the cartridge (.md or .bin, no header).",
                 rom_path, (unsigned long)rom_size);
        fail(m);
        return 1;
    }
    {
        uint8_t d[20]; char hex[41];
        sha1(rom, rom_size, d);
        for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
        if (strcmp(hex, ROM_SHA1))
            fprintf(stderr, "wbmw: warning: %s is not the expected dump (SHA-1 %s, expected " ROM_SHA1 "): "
                            "the port reads the game's data from it at fixed places, so another version will not work right\n", rom_path, hex);
    }
    wide_apply();
    bus_reset(); vdp_reset(); sound_reset();
    if (input) load_input(input);
    if (wav_path && wav_open(wav_path, SOUND_RATE)) return 1;
    if (!headless) {
        if (eeprom_load(save_path) == 0) fprintf(stderr, "loaded save data from %s\n", save_path);
#ifndef WBMW_HEADLESS_ONLY
        window_open();
        audio_open();
#endif
    }
    pad_buttons = buttons_at(1);

    /* reset: the stack pointer and the start address from the vector table */
    Cpu c; memset(&c, 0, sizeof c);
    c.a[7] = rd32(0); c.ccr = 0;
    uint32_t pc = rd32(4) & 0xFFFFFF;
    the_cpu = &c;
    if (load_path == state_path_ptr) load_path = state_path;
    if (timing) {                                   /* reset takes 40 of the first slice's 61 cycles */
        line = 0; slice = 0; start_line(); vdp_line_cycle = 0; rt_budget = SLICE - 40;
    }
    if (load_path) {
        if (load_state(load_path, &c, &pc)) return 1;
        if (headless && !framelog_input_active()) pad_buttons = buttons_at(frame + 1);
    }
    if (__builtin_setjmp(restart)) pc = resume_pc;             /* a state was loaded (F4) */
    return machine_run(&c, pc);                      /* from reset, or the state's frame wait */
}
