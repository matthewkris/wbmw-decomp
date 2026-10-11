/* The native build: Wonder Boy in Monster World as C, without a 68000 emulator.
 *
 * All of the game's code is the C in work/gen_native (tools/m68k2c.py with analysis/native.json)
 * and port/ (the hand ports). This file is the host: it plays the part of the console around
 * that code. The memory map, the save EEPROM, the controller and the VDP (video) are the
 * harness's own (harness/bus.c, harness/vdp.c); there is no sound yet.
 *
 * Time: the translated code takes no time of its own. It runs until it waits for the VBlank
 * interrupt (the poll points in analysis/native.json); there the host draws the frame, shows it,
 * waits for the next 60 Hz tick, reads the controller and runs the game's interrupt handler
 * ($4B5C), as the hardware would. Restart points ($4BF8: reset the stack and re-enter the frame
 * loop) unwind the C stack to the host, which calls them again from the top.
 *
 *   wbmw ROM [--scale N] [--save FILE]                       play in a window
 *   wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE]   headless, for comparison
 *
 * The ROM is still needed: the game's data (graphics, maps, text) is read from it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/md.h"
#include "../port/m68k_rt.h"

#ifndef WBMW_HEADLESS_ONLY
#include <SDL.h>
#endif

/* ---- the machine around the code ---- */
uint8_t rd8(uint32_t a) { return (uint8_t)bus_read8(a); }
uint16_t rd16(uint32_t a) { return (uint16_t)bus_read16(a); }
void wr8(uint32_t a, uint8_t v) { bus_write8(a, v); }
void wr16(uint32_t a, uint16_t v) { bus_write16(a, v); }

/* save states are the harness's; the native build has none yet */
void state_io(FILE *f, void *p, size_t n, int saving) { (void)f; (void)p; (void)n; (void)saving; }

/* generated code reached an address with no translation: everything is translated here, so
 * this is a bug in the translation (a computed jump the translator did not find) */
void rt_leave(Cpu *c, uint32_t from, uint32_t target)
{
    (void)c;
    fprintf(stderr, "wbmw: no translation for %06X (jumped to from %06X)\n", (unsigned)target, (unsigned)from);
    exit(4);
}

/* the watchdog of the generated code: in the native build a call only ends at a poll point,
 * so it is reset every frame; it fires if a frame never comes */
uint32_t rt_ring[16]; unsigned rt_ring_i; unsigned long rt_steps, rt_limit = 100000000;
void rt_stuck(void)
{
    fprintf(stderr, "wbmw: STUCK: the game looped %lu times without waiting for a frame; recent branch targets:", rt_steps);
    for (unsigned i = 0; i < 16; i++) fprintf(stderr, " %06X", (unsigned)rt_ring[(rt_ring_i + i) & 15]);
    fprintf(stderr, "\n");
    exit(3);
}

extern const PortRoutine gen_routines[];
extern const int gen_routine_count;
static const PortRoutine *entry(uint32_t a)
{
    for (int i = 0; i < gen_routine_count; i++) if (gen_routines[i].addr == a) return &gen_routines[i];
    return NULL;
}

/* ---- frames: drawing, showing, input, the interrupt ---- */
static int frame, frames_max, png_every, headless, scale;            /* scale 0: fit the screen */
static const char *out_dir = ".", *save_path = "wbmw.sav";
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
    win = SDL_CreateWindow("Wonder Boy in Monster World (native)  (arrows, Z/X/C, Enter; Esc quits)",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 256 * scale, 224 * scale, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
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
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) quit(0);
        if (e.type == SDL_CONTROLLERDEVICEADDED && !gc) gc = SDL_GameControllerOpen(e.cdevice.which);
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    b |= k[SDL_SCANCODE_UP] ? 1 : 0;    b |= k[SDL_SCANCODE_DOWN] ? 2 : 0;
    b |= k[SDL_SCANCODE_LEFT] ? 4 : 0;  b |= k[SDL_SCANCODE_RIGHT] ? 8 : 0;
    b |= k[SDL_SCANCODE_X] ? 16 : 0;    b |= k[SDL_SCANCODE_C] ? 32 : 0;
    b |= k[SDL_SCANCODE_Z] ? 64 : 0;    b |= k[SDL_SCANCODE_RETURN] ? 128 : 0;
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
    pad_buttons = b;
    SDL_UpdateTexture(tex, NULL, &frame_rgb[0][0], 320 * 4);
    SDL_Rect src = {0, 0, frame_width, ACTIVE_LINES};              /* only the width the game uses */
    SDL_RenderSetLogicalSize(ren, frame_width, ACTIVE_LINES);
    SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, &src, NULL); SDL_RenderPresent(ren);
    Uint64 freq = SDL_GetPerformanceFrequency();
    next_tick += (Uint64)(freq / 59.922);
    Uint64 now = SDL_GetPerformanceCounter();
    if (now < next_tick) SDL_Delay((Uint32)((next_tick - now) * 1000 / freq));
    else if (now - next_tick > freq / 4) next_tick = now;      /* fell far behind: do not race */
}
#endif

/* Called by the generated code where the game waits for VBlank. If the interrupt has not come
 * yet, this is the end of the frame: draw it, show it, and deliver the interrupt. */
void rt_poll(Cpu *c, uint32_t at)
{
    if (ram[0x8006] & 1) return;                    /* the interrupt already came this frame */
    rt_steps = 0;
    for (int line = 0; line < ACTIVE_LINES; line++) { vdp_line = line; vdp_render_line(line); }
    vdp_line = 0;                                   /* the game's own time runs at line 0 */
    frame++;
    if (headless) {
        if (png_every && frame % png_every == 0) {
            char path[1024]; snprintf(path, sizeof path, "%s/frame_%05d.png", out_dir, frame);
            png_write_rgb(path, &frame_rgb[0][0], frame_width, ACTIVE_LINES, 320);
        }
        if (frame >= frames_max) exit(0);
        pad_buttons = buttons_at(frame + 1);
    }
#ifndef WBMW_HEADLESS_ONLY
    else window_frame();
#endif
    if (!vdp_vint_enabled()) return;                /* the game turned the interrupt off */
    c->a[7] -= 6;                                   /* the 68000's interrupt frame: SR, then PC */
    wr16(c->a[7], (uint16_t)(0x2000 | c->ccr)); wr32(c->a[7] + 2, at);
    static const PortRoutine *isr;
    if (!isr && !(isr = entry(0x4B5C))) { fprintf(stderr, "wbmw: no VBlank handler\n"); exit(4); }
    isr->fn(c);                                     /* ends in rte: SR and PC popped */
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *input = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) { frames_max = atoi(argv[++i]); headless = 1; }
        else if (!strcmp(argv[i], "--png-every") && i + 1 < argc) png_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save_path = argv[++i];
        else rom_path = argv[i];
    }
    if (!rom_path) {
        fprintf(stderr, "usage: wbmw ROM [--scale N] [--save FILE]\n"
                        "       wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE]\n");
        return 2;
    }
    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); return 1; }
    fseek(f, 0, SEEK_END); rom_size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(rom_size);
    if (fread(rom, 1, rom_size, f) != rom_size) { fprintf(stderr, "%s: short read\n", rom_path); return 1; }
    fclose(f);
    bus_reset(); vdp_reset();
    if (input) load_input(input);
    if (!headless) {
        if (eeprom_load(save_path) == 0) fprintf(stderr, "loaded save data from %s\n", save_path);
#ifndef WBMW_HEADLESS_ONLY
        window_open();
#endif
    }
    pad_buttons = buttons_at(1);

    /* reset: the stack pointer and the start address from the vector table */
    Cpu c; memset(&c, 0, sizeof c);
    c.a[7] = rd32(0); c.ccr = 0;
    uint32_t pc = rd32(4) & 0xFFFFFF;
    for (;;) {                                       /* restart points come back here */
        const PortRoutine *r = entry(pc);
        if (!r) { fprintf(stderr, "wbmw: cannot start at %06X\n", (unsigned)pc); return 4; }
        r->fn(&c);
        pc = c.ret & 0xFFFFFF;
        if (pc == 0x4CB4) { fprintf(stderr, "wbmw: the game stopped (no free task slot, $4CB4)\n"); return 5; }
    }
}
