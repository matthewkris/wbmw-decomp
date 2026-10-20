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
 *   wbmw ROM [--scale N] [--save FILE] [--fullscreen] [--integer-scale] [--fps]   play in a window
 *   wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE] [--wav FILE]   headless, for comparison
 *            [--frame-log FILE] [--input-game FILE] [--dump-at N DIR]          by game frame (harness/framelog.c)
 *
 * The ROM is still needed: the game's data (graphics, maps, text) is read from it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "../harness/md.h"
#include "../port/m68k_rt.h"

#ifndef WBMW_HEADLESS_ONLY
#include <SDL.h>
#endif

/* save states (below), used by the window's keys */
static const char *state_path;
static int save_pending;
static const char *save_to;

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
static int fullscreen, integer_scale, show_fps;
static const char *TITLE = "Wonder Boy in Monster World (native)  (arrows, Z/X/C, Enter; F11 full screen, F3 fps, Esc menu)";

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

/* ---- the menu (Esc): drawn by the host over the frozen game, which never knows ---- */

/* a 5x7 font: A-Z, 0-9 and a little punctuation; each row is 5 bits, the top bit leftmost */
static const char FONT7_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:?>-./";
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
};

static void draw_text(int x, int y, const char *s, uint32_t color)
{
    for (; *s; s++, x += 6) {
        const char *k = strchr(FONT7_CHARS, *s);
        if (*s == ' ' || !k) continue;
        for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++)
            if (FONT7[k - FONT7_CHARS][r] >> (4 - c) & 1 && x + c >= 0 && x + c < 320 && y + r >= 0 && y + r < ACTIVE_LINES)
                frame_rgb[y + r][x + c] = color;
    }
}

static void draw_text_centred(int y, const char *s, uint32_t color) { draw_text(frame_width / 2 - (int)strlen(s) * 3, y, s, color); }

static void present(void)
{
    SDL_UpdateTexture(tex, NULL, &frame_rgb[0][0], 320 * 4);
    SDL_Rect src = {0, 0, frame_width, ACTIVE_LINES};              /* only the width the game uses */
    SDL_RenderSetLogicalSize(ren, frame_width, ACTIVE_LINES);
    SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, &src, NULL); SDL_RenderPresent(ren);
}

enum { MENU_WHITE = 0xFFFFFF, MENU_GREY = 0x8890A0, MENU_GOLD = 0xFFD860, MENU_BOX = 0x101830 };

/* the picture darkened, a box, and either the options or the exit question */
static void draw_menu(int item, int asking, int yes)
{
    for (int y = 0; y < ACTIVE_LINES; y++) for (int x = 0; x < frame_width; x++)
        frame_rgb[y][x] = (frame_rgb[y][x] >> 2) & 0x3F3F3F;
    int w = 150, h = 74, x0 = frame_width / 2 - w / 2, y0 = ACTIVE_LINES / 2 - h / 2;
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++)
        frame_rgb[y][x] = (y == y0 || y == y0 + h - 1 || x == x0 || x == x0 + w - 1) ? MENU_GREY : MENU_BOX;
    if (!asking) {
        draw_text_centred(y0 + 8, "PAUSED", MENU_WHITE);
        static const char *items[2] = {"SHOW FPS", "EXIT GAME"};
        for (int i = 0; i < 2; i++) {
            uint32_t col = i == item ? MENU_GOLD : MENU_WHITE;
            int y = y0 + 26 + i * 14;
            if (i == item) draw_text(x0 + 12, y, ">", col);
            draw_text(x0 + 24, y, items[i], col);
            if (i == 0) draw_text(x0 + w - 12 - 18, y, show_fps ? "ON" : "OFF", col);
        }
        draw_text_centred(y0 + h - 14, "ESC: RESUME", MENU_GREY);
    } else {
        draw_text_centred(y0 + 12, "EXIT THE GAME?", MENU_WHITE);
        int y = y0 + 34, cx = frame_width / 2;
        draw_text(cx - 40, y, yes ? " NO" : ">NO", yes ? MENU_WHITE : MENU_GOLD);
        draw_text(cx + 16, y, yes ? ">YES" : " YES", yes ? MENU_GOLD : MENU_WHITE);
        draw_text_centred(y0 + h - 14, "ESC: BACK", MENU_GREY);
    }
}

/* Run the menu until it closes; the game is frozen meanwhile (no frames, no sound time).
 * asking: start at the exit question (closing the window). Returns 1 to exit the game. */
static void menu_test_keys(void);

static int menu(int asking)
{
    static uint32_t saved[ACTIVE_LINES][320];
    memcpy(saved, frame_rgb, sizeof saved);
    int item = 0, yes = 0, only_asking = asking;
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            int up = 0, down = 0, side = 0, ok = 0, back = 0;
            if (e.type == SDL_QUIT) { asking = 1; yes = 0; }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
                if (key == SDLK_F11 || (key == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    if (!e.key.repeat) set_fullscreen(!fullscreen);
                    continue;
                }
                if (key == SDLK_F3 && !e.key.repeat) show_fps = !show_fps;
                up = key == SDLK_UP; down = key == SDLK_DOWN; side = key == SDLK_LEFT || key == SDLK_RIGHT;
                ok = !e.key.repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_z);
                back = !e.key.repeat && (key == SDLK_ESCAPE || key == SDLK_x);
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                int b = e.cbutton.button;
                up = b == SDL_CONTROLLER_BUTTON_DPAD_UP; down = b == SDL_CONTROLLER_BUTTON_DPAD_DOWN;
                side = b == SDL_CONTROLLER_BUTTON_DPAD_LEFT || b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                ok = b == SDL_CONTROLLER_BUTTON_A;
                back = b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_START;
            }
            if (e.type == SDL_CONTROLLERDEVICEADDED && !gc) gc = SDL_GameControllerOpen(e.cdevice.which);
            if (!asking) {
                if (up || down) item ^= 1;
                if (ok && item == 0) show_fps = !show_fps;
                if (ok && item == 1) { asking = 1; yes = 0; }
                if (back) { memcpy(frame_rgb, saved, sizeof saved); return 0; }
            } else {
                if (up || down || side) yes ^= 1;
                if (ok && yes) return 1;
                if ((ok && !yes) || back) {
                    if (only_asking) { memcpy(frame_rgb, saved, sizeof saved); return 0; }
                    asking = 0;
                }
            }
        }
        memcpy(frame_rgb, saved, sizeof saved);
        draw_menu(item, asking, yes);
        if (show_fps) draw_fps();
        present();
        const char *shots = getenv("WBMW_MENU_SHOTS");   /* testing: each screen of the menu as a PNG */
        if (shots) {
            static int n, last = -1;
            int state = item | asking << 1 | yes << 2 | show_fps << 3;
            if (state != last) {
                char path[1024]; snprintf(path, sizeof path, "%s/menu_%02d.png", shots, n++);
                png_write_rgb(path, &frame_rgb[0][0], frame_width, ACTIVE_LINES, 320);
                last = state;
            }
        }
        menu_test_keys();
        SDL_Delay(15);
    }
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
    e.type = SDL_KEYDOWN; e.key.keysym.sym = SDL_GetKeyFromName(name);
    SDL_PushEvent(&e);
}

/* buttons held when the menu closed: kept from the game until released */
static uint8_t held_over;

static void window_open(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); exit(1); }
    /* the game is 256 pixels wide almost everywhere (a few screens are 320): size the window for
     * 256, at the largest whole scale up to 3 that fits the screen */
    if (scale <= 0) {
        SDL_Rect r; scale = 3;
        if (SDL_GetDisplayUsableBounds(0, &r) == 0)
            while (scale > 1 && (224 * scale > r.h * 9 / 10 || 256 * scale > r.w * 9 / 10)) scale--;
    }
    win = SDL_CreateWindow(TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 256 * scale, 224 * scale, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetIntegerScale(ren, integer_scale ? SDL_TRUE : SDL_FALSE);   /* whole multiples only: even pixels */
    if (fullscreen) set_fullscreen(1);
    fps_start = SDL_GetPerformanceCounter();
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 224);
    next_tick = SDL_GetPerformanceCounter();
}

static void quit(int code)
{
    if (eeprom_save(save_path) == 0) fprintf(stderr, "saved save data to %s\n", save_path);
    SDL_Quit();
    exit(code);
}

/* show the frame, keep 60 Hz, read the keyboard and controller */
static void window_frame(void)
{
    SDL_Event e;
    menu_test_keys();
    int open_menu = -1;                             /* 0: the menu, 1: straight to the exit question */
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) open_menu = 1;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !e.key.repeat) open_menu = 0;
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode key = e.key.keysym.sym;
            if (key == SDLK_F11 || (key == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) set_fullscreen(!fullscreen);
            if (key == SDLK_F3) show_fps = !show_fps;
            if (key == SDLK_F2) { save_pending = frame; save_to = state_path; }   /* at the next frame wait */
            if (key == SDLK_F4) request_load(state_path);
        }
        if (e.type == SDL_CONTROLLERDEVICEADDED && !gc) gc = SDL_GameControllerOpen(e.cdevice.which);
    }
    if (open_menu >= 0) {
        if (menu(open_menu)) quit(0);
        Uint64 now = SDL_GetPerformanceCounter();   /* carry on as if no time had passed */
        next_tick = now; fps_start = now; fps_frames = 0;
        held_over = 0xFF;                           /* whatever is still held, until released */
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    b |= k[SDL_SCANCODE_UP] ? 1 : 0;    b |= k[SDL_SCANCODE_DOWN] ? 2 : 0;
    b |= k[SDL_SCANCODE_LEFT] ? 4 : 0;  b |= k[SDL_SCANCODE_RIGHT] ? 8 : 0;
    b |= k[SDL_SCANCODE_X] ? 16 : 0;    b |= k[SDL_SCANCODE_C] ? 32 : 0;
    b |= k[SDL_SCANCODE_Z] ? 64 : 0;
    b |= k[SDL_SCANCODE_RETURN] && !(SDL_GetModState() & KMOD_ALT) ? 128 : 0;   /* Alt+Enter is full screen */
    if (gc) {
        b |= SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP) ? 1 : 0;
        b |= SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? 2 : 0;
        b |= SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? 4 : 0;
        b |= SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? 8 : 0;
        b |= SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A) ? 16 : 0;
        b |= SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) ? 32 : 0;
        b |= SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X) ? 64 : 0;
        b |= SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START) ? 128 : 0;
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
            png_write_rgb(path, &frame_rgb[0][0], frame_width, ACTIVE_LINES, 320);
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

static jmp_buf restart;
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
    return 0;
}

/* a frame boundary with the game at `pc` (the next instruction): save if asked and possible */
static void maybe_save(Cpu *c, uint32_t pc)
{
    if (save_pending < 0 || frame < save_pending || !resumable(c, pc)) return;
    save_state(save_to, c, pc);
    save_pending = -1;
}

/* load: abandon the C call stack and start the code at the state's PC (main) */
static void request_load(const char *path)
{
    uint32_t pc;
    if (load_state(path, the_cpu, &pc) == 0) { resume_pc = pc; longjmp(restart, 1); }
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

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *input = NULL, *load_path = NULL;
    const char *state_path_ptr = "\x01";            /* --load: the --state file, whatever it ends up being */
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
        else if (!strcmp(argv[i], "--timing")) fprintf(stderr, "wbmw: --timing counts the translated code's cycles; the hand-written code has none, so it is off\n");
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) state_path = argv[++i];
        else if (!strcmp(argv[i], "--load")) load_path = state_path_ptr;
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_path = argv[++i];
        else if (!strcmp(argv[i], "--save-state-at") && i + 2 < argc) { save_pending = atoi(argv[++i]); save_to = argv[++i]; }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save_path = argv[++i];
#ifndef WBMW_HEADLESS_ONLY
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--integer-scale")) integer_scale = 1;
        else if (!strcmp(argv[i], "--fps")) show_fps = 1;
#endif
        else rom_path = argv[i];
    }
    if (!rom_path) {
        fprintf(stderr, "usage: wbmw ROM [--scale N] [--save FILE] [--fullscreen] [--integer-scale] [--fps] [--mute] [--timing]\n"
                        "       wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE] [--wav FILE] [--timing]\n"
                        "                [--frame-log FILE] [--input-game FILE] [--dump-at N DIR]\n"
                        "       states: [--state FILE] [--load] [--load-state FILE] [--save-state-at N FILE]; F2 saves, F4 loads\n");
        return 2;
    }
    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); return 1; }
    fseek(f, 0, SEEK_END); rom_size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(rom_size);
    if (fread(rom, 1, rom_size, f) != rom_size) { fprintf(stderr, "%s: short read\n", rom_path); return 1; }
    fclose(f);
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
    if (setjmp(restart)) pc = resume_pc;             /* a state was loaded (F4) */
    return machine_run(&c, pc);                      /* from reset, or the state's frame wait */
}
