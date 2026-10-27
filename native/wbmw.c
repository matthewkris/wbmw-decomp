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
#include <math.h>
#include <time.h>
#include <sys/stat.h>
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

/* the save slots: 4 states, each with a picture of the game at half size. Slot 1 is the --state
 * file (wbmw.state), the others beside it with -2, -3, -4 in their names; each picture has the
 * state's name with .png (wbmw.png, wbmw-2.png). Quick save and load use the current slot, the
 * one last chosen in the menu (kept in wbmw.ini). */
#define SLOTS 4
static int slot = 1;                                /* the current slot */
static int save_slot, load_slot;                    /* the slot being saved or loaded, 0 for another file */
static const char *slot_file(int n, int picture)
{
    static char path[2][SLOTS + 1][1024];
    size_t len = strlen(state_path);
    const char *ext = len >= 6 && !strcmp(state_path + len - 6, ".state") ? ".state" : "";
    char suffix[4] = "";
    if (n > 1) { suffix[0] = '-'; suffix[1] = (char)('0' + n); suffix[2] = 0; }
    snprintf(path[picture][n], sizeof path[0][0], "%.*s%s%s", (int)(len - strlen(ext)), state_path, suffix,
             picture ? ".png" : ext);
    return path[picture][n];
}

/* the picture as shown, without the host's messages over it (the window keeps it each frame);
 * headless runs have only frame_rgb */
static uint32_t clean_rgb[ACTIVE_LINES][FRAME_STRIDE];
static int clean_width;
static void save_picture(const char *path)
{
    static uint32_t half[ACTIVE_LINES / 2][FRAME_STRIDE / 2];
    const uint32_t (*src)[FRAME_STRIDE] = clean_width ? clean_rgb : frame_rgb;
    int w = (clean_width ? clean_width : frame_width) / 2;
    for (int y = 0; y < ACTIVE_LINES / 2; y++)
        for (int x = 0; x < w; x++) {
            uint32_t p[4] = {src[2 * y][2 * x], src[2 * y][2 * x + 1], src[2 * y + 1][2 * x], src[2 * y + 1][2 * x + 1]}, v = 0;
            for (int s = 16; s >= 0; s -= 8)
                v |= (uint32_t)(((p[0] >> s & 255) + (p[1] >> s & 255) + (p[2] >> s & 255) + (p[3] >> s & 255) + 2) / 4) << s;
            half[y][x] = v;
        }
    png_write_rgb(path, &half[0][0], w, ACTIVE_LINES / 2, FRAME_STRIDE / 2);
}

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
static int z80_sound;                                  /* --z80-sound: the original sound driver on the emulated Z80 */
static int enemy_health;                            /* the damage numbers and health bars (Enemy health) */

/* The volumes (the menu's Volume, Music and Effects: 0-100, in tens). The music and the effects
 * come from the same chips, and an effect takes a voice from the music while it plays, so each
 * frame the sound driver's voices are read from the Z80's RAM (docs/SOUND.md): voice v's record
 * at $1C80 + $40 v, bit 7 playing, bit 5 for a song's track ($0DF0). Voices 0-5 are the YM2612's
 * channels 0, 3, 1, 4, 2, 5 ($0446); 6-8 the SN76489's tones, 8 its noise too. A voice not
 * playing keeps its last owner, so a note's release fades at its own volume. All at 100, the
 * sound is the original's, sample for sample. */
static int volume = 100, music_volume = 100, effects_volume = 100;
static int play_sound = -1;                         /* --play-sound N: a song or effect queued after the first frame */
void QueueSound(Cpu *c);                            /* port/kernel.c, $366 */
static int volume_clamp(int v) { return v < 0 ? 0 : v > 100 ? 100 : v / 10 * 10; }
static void volume_frame(void)
{
    static const int FM_CHANNEL[6] = {0, 3, 1, 4, 2, 5};
    static int music[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    for (int v = 0; v < 9; v++) {
        uint8_t flags = zram[0x1C80 + 0x40 * v];
        if (flags & 0x80) music[v] = (flags & 0x20) != 0;
        int g = (music[v] ? music_volume : effects_volume) * 256 / 100;
        if (v < 6) sound_gain[FM_CHANNEL[v]] = g;
        else sound_gain[v] = g;
        if (v == 8) sound_gain[9] = g;
    }
    sound_master = volume * 256 / 100;
}

/* widescreen: the picture 400 wide (16:9 at 224 lines), 72 pixels past each side of the game's
 * 256. Only the rooms wide_room() allows show the game there (port/rooms.c draws their layers
 * wider and keeps the view inside them); elsewhere those sides are black. docs/WIDESCREEN.md. */
#define WIDE_PX 72
extern int port_wide_px;                        /* port/rooms.c (port/world.h) */
extern int (*port_wide_room)(int area, int room);
extern int port_found_gold, port_found_thing;   /* port/message.c (port/world.h) */
static int widescreen;
/* Whether a room's layers allow it, from its script in the ROM (the area blocks at the data
 * directory's slots 8-26; the commands as $93C4 reads them): plane A following the camera across
 * and drawn as screens (its kind 0 or 2: $94CC) and at least two screens wide; plane B repeated
 * (kinds 3, 4, 5, 7: one screen, drawn whole), fixed (kind 1: the map lookup wraps its columns,
 * so the sides show it repeated: area 5's castle wall behind its bridge carries on seamlessly)
 * or, drawn as screens, two screens wide too. A narrower plane A would show what is beyond its
 * map at the sides. */
static int layers_wide_why(int area, int room, int *narrow);
static int layers_wide(int area, int room) { int narrow; return layers_wide_why(area, room, &narrow); }

/* A narrow layer drawn as screens can have its sides painted (docs/MODDING.md, "Widescreen
 * sides"): a screen beyond its width in every band of its map, which the camera never reaches;
 * port/map.c reads the wide view's sides from it. Whether the loaded room's layer (plane A's
 * record at $FF9754, B's at $FF97D4) has one */
static int layer_margin(int plane)
{
    uint32_t rec = plane ? 0xFF97D4 : 0xFF9754;
    int cols = rd16(rec + 0x44) + 1, rows = rd16(rec + 0x46) / 64 + 1;
    uint32_t map = 0xFFFF0000u | rd16(rec + 0x22);
    if (cols & 15 || cols / 16 >= 8) return 0;
    for (int band = 0; band * 12 < rows && band < 8; band++)
        if (rd8(map + (uint32_t)(band * 8 + cols / 16)) & 0x80) return 0;
    return 1;
}

static int layers_wide_why(int area, int room, int *narrow)
{
    *narrow = 0;                                    /* bit 0 plane A, bit 1 plane B: narrow, drawn as screens */
    static const signed char args[0x15] = {   /* the commands' argument bytes; -1 a pointer (a flag byte, then a long or a word) */
        7, 7, -1, 3, -1, -1, 1, 5, -1, 1, 1, -3, 1, 1, 1, 3, 9, 9, 1, 9, 9};
    uint32_t a1 = rd32(0x1CC00 + 4 * (8 + (uint32_t)area)) & 0xFFFFFF;
    if (rd16(a1) / 2 <= (unsigned)room) return 0;
    uint32_t p = a1 + (uint32_t)(int16_t)rd16(a1 + 2 * (uint32_t)room);
    int a_ok = 0, b_ok = 1;
    for (int guard = 0; guard < 64; guard++) {
        uint8_t c = rd8(p++);
        if (c == 0xFF) break;
        if (c > 0x14) return 0;
        if (c <= 1 || c == 0x10 || c == 0x11) {     /* a layer: kind, width, height (screens), word, its data */
            uint8_t kind = rd8(p), w = rd8(p + 1);
            if (c & 1) {
                b_ok = kind == 1 || kind == 3 || kind == 4 || kind == 5 || kind == 7 || ((kind == 0 || kind == 2) && w >= 2);
                if (kind == 0 || kind == 2) *narrow = (*narrow & 1) | (w < 2) << 1;
            } else {
                a_ok = (kind == 0 || kind == 2) && w >= 2;
                *narrow = (*narrow & 2) | ((kind <= 2 && w < 2) || kind == 1);   /* (fixed: one screen that stays) */
            }
        }
        int n = args[c];
        if (n == -1) n = rd8(p) ? 5 : 3;            /* a pointer */
        else if (n == -3) n = (rd8(p) ? 5 : 3) + 2; /* command 11: a pointer and two bytes */
        p += (uint32_t)n;
    }
    if (!(a_ok || (*narrow & 1)) || !(b_ok || (*narrow & 2))) *narrow = 0;   /* narrow is not all that stops it */
    if (a_ok) *narrow &= ~1;
    if (b_ok) *narrow &= ~2;
    return a_ok && b_ok;
}

/* the rooms shown wide (1, or 2 for a narrow room with its sides painted): every room whose layers allow it, but not area 15 (its moving floor is
 * placed from the screen's edge, $18C8C). Inside a house or shop ($9659 bit 4, set by $BE6C and
 * $C634) the room stays wide: the house's picture is drawn over the room, which shows around it
 * as in the original (at first bit 4 was taken for a boss's room, and the sides went black at
 * once as the player went through a door) */
static int wide_room(int area, int room)
{
    static signed char known[19][64];               /* 0 not yet, 1 yes, -1 no, else 2 + its narrow planes */
    if (area < 0 || area >= 19 || room < 0 || room >= 64 || area == 15) return 0;
    if (!known[area][room]) {
        int narrow, wide = layers_wide_why(area, room, &narrow);
        known[area][room] = (signed char)(wide ? 1 : narrow ? 2 + narrow : -1);
    }
    if (known[area][room] < 2) return known[area][room] > 0;
    if (area == 13 && room == 7) return 0;          /* its plane A has places beyond its width of its own (3 x 2 for 1 x 1) */
    /* narrow, but its sides may be painted: asked of the loaded room's maps, each time (they are
     * what the room has once it is loaded; before, RAM holds the last room's or nothing) */
    static int last_area = -1, last_room, last_ok;  /* the loaded room's answer, kept for its fade out: the game sets */
    if (ram[0x9668] != area || ram[0x9669] != room) /* the next room's number as the player walks out, its maps loaded later */
        return area == last_area && room == last_room && last_ok ? 2 : 0;
    last_area = area; last_room = room; last_ok = 1;
    for (int k = 0; k < 2; k++)
        if ((known[area][room] - 2) >> k & 1 && !layer_margin(k)) last_ok = 0;
    return last_ok ? 2 : 0;                         /* wide by its sides: its exits move out to them (port/rooms.c) */
}
/* each frame: whether the room is shown wide, and how much of the HUD's band beside it shows the
 * map. In a wide room port/rooms.c draws each layer's rows up to 64 lines above the play field, as
 * far as its map's top; above the lowest of those tops (of the layers drawn as screens: $C, the
 * first row drawn, in 64s of 16 lines; a repeated layer, $33 = 2, fills the plane) the band's
 * sides are black, as the HUD covers the scene there in the original. */
static void gold_found(int amount);
/* what the HUD's band shows beside it where the map draws nothing (vdp_band_colour): black, as
 * rooms leave gaps in their maps' top rows that the 64-line HUD always covered (area 12's cave
 * has its purple backdrop there); or, where a room's sky is its backdrop, that (the tops of
 * clouds drawn in its map's first rows, over black, looked cut out) */
static const struct { uint8_t area, room; int8_t colour; } band_colours[] = {
    {1, 0, 64},                                     /* the meadow past the village: the sky ($E60, entry 0) */
};
static int band_colour(int area, int room)
{
    for (size_t k = 0; k < sizeof band_colours / sizeof band_colours[0]; k++)
        if (band_colours[k].area == area && band_colours[k].room == room) return band_colours[k].colour;
    return -1;
}
static void wide_frame(void)
{
    /* The room shown: the game sets the next room's number as the player walks out, while the
     * old room is still on the screen for the fade out (the castle's tower drawn wide for those
     * frames, past what it draws: sky with its walls cut at the 256's edges). So the room number
     * is taken when the screen is dark (the palette all black, or the display off), where the
     * next room is drawn; or once it has stayed the same for a second (a change without a fade),
     * or after a state is loaded (the frame jumps). */
    static int shown_area = -1, shown_room, last_area = -1, last_room, same, last_frame;
    int area = ram[0x9668], room = ram[0x9669], dark = !(vdp_reg[1] & 0x40);
    if (area != last_area || room != last_room) { last_area = area; last_room = room; same = 0; }
    else same++;
    if (!dark) {
        dark = 1;
        for (int k = 0; k < 64; k++) if (cram[k]) { dark = 0; break; }
    }
    if (shown_area < 0 || dark || same >= 60 || frame != last_frame + 1) { shown_area = area; shown_room = room; }
    last_frame = frame;
    vdp_pad_fill = widescreen && wide_room(shown_area, shown_room);
    vdp_band_colour = band_colour(shown_area, shown_room);
    int top = 0;
    for (int k = 0; k < 2 && vdp_pad_fill; k++) {
        const uint8_t *l = &ram[k ? 0x97D4 : 0x9754];
        if (l[0x33] == 2) continue;
        int y = l[6] << 8 | l[7], first = (l[0xC] << 8 | l[0xD]) >> 2, t = 64 - (y - first);
        if (t > top) top = t;
    }
    /* widescreen: the "Found 5 GOLD." box hidden (its amount rises from Wonder Boy instead) until
     * the game closes it, or another found thing's box opens */
    static int msg_seen, msg_wait;
    if (port_found_thing) { port_found_thing = 0; vdp_hide_msg = 0; }
    if (port_found_gold) {
        if (vdp_hud_wide) { gold_found(port_found_gold); vdp_hide_msg = 1; msg_seen = msg_wait = 0; }
        port_found_gold = 0;
    }
    if (vdp_hide_msg) {
        if ((vdp_reg[18] & 0x1F) > 8 && !(vdp_reg[18] & 0x80)) msg_seen = 1;
        else if (msg_seen || ++msg_wait > 30) vdp_hide_msg = 0;
    }
    /* the scroll the picture shows is the frame before's (the game sends it in VBlank), so the
     * band is the higher of this frame's and that one's: coming back down from a jump near a
     * room's top, this frame's alone let lines above the map show beside the HUD, the plane's
     * wrapped rows (area 12's floor) */
    static int last_top;
    int shown_top = top > last_top ? top : last_top;
    last_top = top;
    vdp_band_top = !vdp_pad_fill ? 64 : shown_top < 0 ? 0 : shown_top > 64 ? 64 : shown_top;   /* a room not wide: black (the wide HUD leaves its bottom row's lines to it) */
}
static void wide_apply(void)
{
    port_wide_px = vdp_pad = widescreen ? WIDE_PX : 0;
    vdp_hud_wide = widescreen;                      /* the HUD's top row only, the gold beside the items (harness/vdp.c) */    port_wide_room = wide_room;
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
static int talk_zoom;                                  /* the zoom during conversations (Talk zoom) */
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
/* the added features (widescreen, talk zoom, enemy health) on unless turned off; headless runs
 * never read the file, so they stay the original's unless their flags say otherwise */
static struct { int fullscreen, show_fps, integer_scale, widescreen, talk_zoom, enemy_health, volume, music_volume, effects_volume; }
    cfg = {1, 0, 0, 1, 1, 1, 100, 100, 100};
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
    fprintf(f, "fullscreen=%d\nshow_fps=%d\ninteger_scale=%d\nwidescreen=%d\ntalk_zoom=%d\nenemy_health=%d\nstick_moves=%d\n",
            cfg.fullscreen, cfg.show_fps, cfg.integer_scale, cfg.widescreen, cfg.talk_zoom, cfg.enemy_health, stick_moves);
    fprintf(f, "volume=%d\nmusic_volume=%d\neffects_volume=%d\n", cfg.volume, cfg.music_volume, cfg.effects_volume);
    fprintf(f, "slot=%d\n", slot);
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
    int old_mute = 0;                               /* the menu's old Sound off: Volume 0 now, written back */
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char *v = strchr(line, '=');
            if (line[0] == '#' || !v) continue;
            *v++ = 0; v[strcspn(v, "\r\n")] = 0;
            if (!strcmp(line, "fullscreen")) cfg.fullscreen = atoi(v);
            else if (!strcmp(line, "show_fps")) cfg.show_fps = atoi(v);
            else if (!strcmp(line, "integer_scale")) cfg.integer_scale = atoi(v);
            else if (!strcmp(line, "mute")) old_mute = atoi(v);
            else if (!strcmp(line, "widescreen")) cfg.widescreen = atoi(v);
            else if (!strcmp(line, "talk_zoom")) cfg.talk_zoom = atoi(v);
            else if (!strcmp(line, "enemy_health")) cfg.enemy_health = atoi(v);
            else if (!strcmp(line, "stick_moves")) stick_moves = atoi(v);
            else if (!strcmp(line, "volume")) cfg.volume = volume_clamp(atoi(v));
            else if (!strcmp(line, "music_volume")) cfg.music_volume = volume_clamp(atoi(v));
            else if (!strcmp(line, "effects_volume")) cfg.effects_volume = volume_clamp(atoi(v));
            else if (!strcmp(line, "slot")) slot = atoi(v) >= 1 && atoi(v) <= SLOTS ? atoi(v) : 1;
            else for (int a = 0; a < ACTIONS; a++) {
                if (!strncmp(line, "key.", 4) && !strcmp(line + 4, ACT_INI[a]))
                    key_bind[a] = strcmp(v, "none") ? SDL_GetScancodeFromName(v) : SDL_SCANCODE_UNKNOWN;
                if (!strncmp(line, "pad.", 4) && !strcmp(line + 4, ACT_INI[a])) pad_bind[a] = pad_parse(v);
            }
        }
        fclose(f);
    }
    if (old_mute) { cfg.volume = 0; ini_dirty = 1; }
    fullscreen = cfg.fullscreen; show_fps = cfg.show_fps; integer_scale = cfg.integer_scale;
    widescreen = cfg.widescreen; talk_zoom = cfg.talk_zoom; enemy_health = cfg.enemy_health;
    volume = cfg.volume; music_volume = cfg.music_volume; effects_volume = cfg.effects_volume;
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

/* ---- the zoom during conversations (Talk zoom): done here, as the picture goes to the
 * window, so the game never knows (docs/TALKZOOM.md) ----
 * A conversation ($C1E8) sets $9659 bit 3 while it runs, and its message is open while $8CA4
 * bit 7 is set. The scene is then drawn larger about an anchor between Wonder Boy and the one
 * speaking, at their feet. The message's box and its pointer (the two tiles over the speaker in
 * the row above the box, from the message's data) stay at their own size, the pointer moved
 * over the speaker as the zoom moves him; the HUD slides up out of the picture, and the box with
 * it, by as much as the zoomed play field needs (zoom_layout), so the two fit under the box.
 * The box is drawn into plane A, so the zoomed scene is a second picture rendered with plane A's
 * map as it was before the box opened (kept each frame until then): no box in it. */
static struct {
    float z, ax, ay;                                /* the zoom now, its anchor (picture pixels) */
    int on;                                         /* a conversation, and where its box is */
    SDL_Rect box;
    int pointer_x, pointer_w;                       /* the pointer's place over the box, or w 0 */
    float e, zt;                                    /* the way in (0 none, 1 all: zoom_update), to the zoom zt */
} zoom = {1, 0, 0, 0, {0, 0, 0, 0}, 0, 0, 0, 1};
#define ZOOM_IN_FRAMES 60                           /* the way in, about a second, and out */
#define ZOOM_OUT_FRAMES 45
#define ZOOM_MAX 1.5f
#define ZOOM_HUD 64                                 /* the HUD's lines (the message boxes' rows count from them) */
/* the HUD's lines as shown, never zoomed: the wide HUD's one row (32), and under it the lines that
 * show the map (from vdp_band_top); else 64 */
static int zoom_hud(void)
{
    if (!vdp_hud_wide || (vdp_reg[12] & 1)) return ZOOM_HUD;
    return vdp_band_top > 32 ? vdp_band_top : 32;
}
#define ZOOM_BODY 28                                /* a character's height above its feet, about */
#define ZOOM_TALLEST 48                            /* a speaker taller than this is a boss: no zoom */
#define ZOOM_PERSON 24                             /* the smallest speaker found without a pointer */
static uint8_t zoom_plane[0x2000];                  /* plane A's map before the box, and whether kept */
static int zoom_plane_ok, zoom_scene;               /* zoom_scene: zoom_rgb holds the scene without the box */
static uint32_t zoom_rgb[ACTIVE_LINES][FRAME_STRIDE];
static SDL_Texture *tex_zoom;

static unsigned plane_a(unsigned *size)
{
    static const int dim[4] = {32, 64, 32, 128};
    *size = (unsigned)(dim[vdp_reg[16] & 3] * dim[(vdp_reg[16] >> 4) & 3] * 2);
    if (*size > sizeof zoom_plane) *size = sizeof zoom_plane;
    return (vdp_reg[2] & 0x38) << 10;
}

/* the scene again, with plane A's map from before the box, and without the window (the HUD's
 * band): zoomed, its lines come into the picture, and they are the scenery's */
static void zoom_render_scene(void)
{
    static uint8_t keep[0x2000];
    static uint32_t saved[ACTIVE_LINES][FRAME_STRIDE];
    unsigned size, base = plane_a(&size);
    if (base + size > sizeof vram) { zoom_scene = 0; return; }
    uint8_t r17 = vdp_reg[17], r18 = vdp_reg[18];
    memcpy(keep, vram + base, size); memcpy(vram + base, zoom_plane, size);
    vdp_reg[17] = vdp_reg[18] = 0;                  /* no window */
    memcpy(saved, frame_rgb, sizeof saved);
    vdp_scene = 1;                                  /* the backdrop beside the HUD too, as in the middle (area 12's sky) */
    for (int l = 0; l < ACTIVE_LINES; l++) vdp_render_line(l);
    vdp_scene = 0;
    memcpy(zoom_rgb, frame_rgb, sizeof zoom_rgb);
    memcpy(frame_rgb, saved, sizeof saved); memcpy(vram + base, keep, size);
    vdp_reg[17] = r17; vdp_reg[18] = r18;
    zoom_scene = 1;
}

/* The box and its pointer by themselves, from their tiles in plane A, with their transparent
 * pixels transparent: the frame's corners and the pointer have some, and a copy of the picture
 * there would show the scene unzoomed around them */
static uint32_t box_rgb[ACTIVE_LINES][FRAME_STRIDE];
static SDL_Texture *tex_box;

static void box_tile(int x0, int y0, uint16_t name)
{
    unsigned tile = name & 0x7FF, pal = (name >> 13) & 3;
    for (int r = 0; r < 8; r++) {
        int row = name & 0x1000 ? 7 - r : r;
        const uint8_t *p = &vram[(tile * 32 + row * 4) & 0xFFFF];
        for (int x = 0; x < 8; x++) {
            int sx = name & 0x800 ? 7 - x : x, X = x0 + x, Y = y0 + r;
            unsigned ci = (p[sx >> 1] >> (sx & 1 ? 0 : 4)) & 15;
            if (X < 0 || X >= FRAME_STRIDE || Y < 0 || Y >= ACTIVE_LINES) continue;
            if (!ci) { box_rgb[Y][X] = 0; continue; }
            uint16_t c = cram[pal * 16 + ci];
            box_rgb[Y][X] = 0xFF000000u | ((c >> 1) & 7) * 255 / 7 << 16 | ((c >> 5) & 7) * 255 / 7 << 8 | ((c >> 9) & 7) * 255 / 7;
        }
    }
}

/* whether the message drew plane A's tile at na: it differs from the map kept from before the box
 * (the row above the box holds the scenery's tiles there, the top of a building, where there is
 * no pointer) */
static int box_drew(unsigned na)
{
    unsigned size, base = plane_a(&size);
    if (na < base || na + 1 >= base + size) return 0;
    return vram[na] != zoom_plane[na - base] || vram[na + 1] != zoom_plane[na + 1 - base];
}

static void box_build(int px, int py, int w, int h)
{
    static const int dim[4] = {32, 64, 32, 128};
    int pw = dim[vdp_reg[16] & 3], ph = dim[(vdp_reg[16] >> 4) & 3];
    unsigned base = (vdp_reg[2] & 0x38) << 10;
    for (int j = -1; j < h; j++)                    /* the row above: the pointer */
        for (int i = 0; i < w; i++) {
            unsigned na = (base + ((unsigned)((py + j) & (ph - 1)) * pw + ((px + i) & (pw - 1))) * 2) & 0xFFFF;
            int x0 = zoom.box.x + i * 8, y0 = zoom.box.y + j * 8;
            if (j < 0 && !box_drew(na)) {           /* the scenery, not the pointer: transparent */
                for (int y = y0; y < y0 + 8; y++)
                    for (int x = x0; x < x0 + 8; x++)
                        if (x >= 0 && x < FRAME_STRIDE && y >= 0 && y < ACTIVE_LINES) box_rgb[y][x] = 0;
                continue;
            }
            box_tile(x0, y0, (uint16_t)(vram[na] << 8 | vram[na + 1]));
        }
}

/* For zoom z about the anchor's line ay: how far the HUD and the box move up, and where the
 * zoomed scene's top goes (dy), so that only the play field's lines (64 down) are ever shown,
 * never those the HUD covers (nothing of the scene is drawn there: plane B, and in widescreen the
 * band's sides). The HUD moves up as far as the zoomed field reaches, at most its 64 lines; past
 * that the scene keeps its anchor as near as the field allows. */
static void zoom_layout(float z, float ay, float *up, float *dy)
{
    int hud = zoom_hud();
    *up = (ACTIVE_LINES - hud) * (z - 1);           /* the box's rise: up to 64 lines, the HUD's up to its own */
    if (*up > ZOOM_HUD) *up = ZOOM_HUD;
    float hud_up = *up < hud ? *up : hud;
    float hi = hud - hud_up - hud * z;              /* the field's first line at the top shown, or above */
    float lo = ACTIVE_LINES - ACTIVE_LINES * z;     /* its last line at the bottom, or below */
    float d = ay * (1 - z);
    *dy = d > hi ? hi : d < lo ? lo : d;
}

static void zoom_update(void)
{
    int pad = (frame_width - 256) / 2;              /* the game's 256 in the picture (widescreen) */
    int talking = talk_zoom && (ram[0x9659] & 8) && (ram[0x8CA4] & 0x80) && !(vdp_reg[12] & 1);
    float zt = 1;
    static int box_px, box_py, box_w, box_h;
    /* A conversation ended by a fade ($9BF3 bit 0: the inn's "Good night") fades out with its box
     * still on the screen, the message flag already clear: the zoom is held as it is, the box
     * redrawn each frame as the palette fades, until the screen is dark, and then put back while
     * nothing shows (at first it eased out during the fade, the box zoomed with the scene). */
    int hold = !talking && zoom.on && zoom.z > 1 && (ram[0x9BF3] & 1);
    zoom.on = 0;
    if (hold) {
        int dark = !(vdp_reg[1] & 0x40);
        if (!dark) {
            dark = 1;
            for (int k = 0; k < 64; k++) if (cram[k]) { dark = 0; break; }
        }
        zoom_scene = 0;
        if (dark) { zoom.z = 1; zoom.e = 0; return; }
        zoom.on = 1;
        box_build(box_px, box_py, box_w, box_h);
        if (zoom_plane_ok) zoom_render_scene();
        return;
    }
    if (!talking) {                                 /* plane A's map while there is no box */
        unsigned size, base = plane_a(&size);
        zoom_plane_ok = base + size <= sizeof vram;
        if (zoom_plane_ok) memcpy(zoom_plane, vram + base, size);
    }
    if (talking && zoom_plane_ok) {                 /* (no map from before the box: a state loaded mid-conversation) */
        int col = ram[0x8D92] << 8 | ram[0x8D93], row = ram[0x8D94] << 8 | ram[0x8D95];
        int w = ram[0x8D96] << 8 | ram[0x8D97], h = ram[0x8D98] << 8 | ram[0x8D99];
        int px = ram[0x8D9A] << 8 | ram[0x8D9B], py = ram[0x8D9C] << 8 | ram[0x8D9D];
        zoom.box = (SDL_Rect){pad + col * 8, ZOOM_HUD + row * 8, w * 8, h * 8};
        /* the pointer: the tiles the message drew in plane A's row above the box */
        unsigned base = (vdp_reg[2] & 0x38) << 10;
        int first = -1, last = -1;
        for (int i = 0; i < w && w < 64; i++) {
            unsigned na = (base + (((py - 1) & 31) * 64 + ((px + i) & 63)) * 2) & 0xFFFF;
            if ((vram[na] | vram[na + 1]) && box_drew(na)) { if (first < 0) first = i; last = i; }
        }
        zoom.pointer_w = first < 0 ? 0 : (last - first + 1) * 8;
        zoom.pointer_x = first < 0 ? 0 : zoom.box.x + first * 8;
        box_build(px, py, w, h);
        box_px = px; box_py = py; box_w = w; box_h = h;
        /* Wonder Boy ($9EEE) and the speaker, under the pointer: the anchor between them, at
         * Wonder Boy's feet; the zoom as large as leaves the two below the box */
        uint32_t e = 0xFFFF0000u + (ram[0x9EEE] << 8 | ram[0x9EEF]);
        int cam_x = ram[0x8B34] << 8 | ram[0x8B35], cam_y = ram[0x8B36] << 8 | ram[0x8B37];
        int wx = pad + (int16_t)(rd16(e - 0x3800) - cam_x - 128), wy = (int16_t)(rd16(e - 0x3700) - cam_y - 128);
        /* the speaker: the character nearest under the pointer, or with no pointer (a box may
         * have none) the person nearest Wonder Boy: an entity in use, not him, placed by the
         * camera, and without a pointer at least person-sized (so not Priscilla, nor a fire).
         * Only ordinary characters are zoomed: with none there (the Sphinx is drawn in the
         * background, a message may have no speaker) or a big one (a boss, taller than
         * ZOOM_TALLEST by its size, -$34FD being half its height), not */
        int sx = 0, speaker = 0;
        int centre = zoom.pointer_w ? zoom.pointer_x + zoom.pointer_w / 2 : wx, best = zoom.pointer_w ? 24 : 64;
        for (uint32_t k = 0; k < 0x100; k += 4) {
            uint32_t o = 0xFFFF0000u + k;
            if (o == e || !(rd8(o - 0x4000) & 0x80) || rd16(o - 0x3E00)) continue;
            int half = rd8(o - 0x34FD);
            int ox = pad + (int16_t)(rd16(o - 0x3800) - cam_x - 128), d = abs(ox - centre);
            /* with no pointer, one the player could have talked to (within its reach, -$3500,
             * and level with him, as $164B8 has it) is the speaker whatever its size (one
             * 22 lines tall in area 6, standing where Wonder Boy stands) */
            int dx = (int16_t)(rd16(o - 0x3800) - rd16(e - 0x3800)), dy = (int16_t)(rd16(o - 0x3700) - rd16(e - 0x3700));
            int reach = rd8(o - 0x3500), talkable = !zoom.pointer_w && reach && abs(dx) <= reach + 2 && abs(dy) <= half + rd8(e - 0x34FD);
            if (!zoom.pointer_w && half < ZOOM_PERSON / 2 && !talkable) continue;
            if (talkable) d = -1;
            if (d < best) { best = d; sx = ox; speaker = 2 * half <= ZOOM_TALLEST ? 1 : -1; }
        }
        if (speaker > 0) {
            float ax = (wx + sx) / 2.0f, ay = (float)wy;
            for (zt = ZOOM_MAX; zt > 1; zt -= 0.02f) {  /* the largest zoom with the heads below the box */
                float up, dy;
                zoom_layout(zt, ay, &up, &dy);
                if (ay * zt + dy - ZOOM_BODY * zt >= zoom.box.y + zoom.box.h - up + 2) break;
            }
            if (zt < 1) zt = 1;
            if (zoom.z < 1.01f) { zoom.ax = ax; zoom.ay = ay; }        /* starting: from where they are */
            zoom.ax += (ax - zoom.ax) * 0.1f; zoom.ay += (ay - zoom.ay) * 0.1f;
            zoom.on = 1;
        }
    }
    /* The way in and out is one smooth curve (slow, faster, slow) over a fixed time, and the zoom,
     * the scene's place, the box and the HUD all follow it (present_frame), so they move together
     * and arrive together (at first the zoom eased exponentially: a jolt at the start, a long
     * crawl at the end, the scene's rise kinked where zoom_layout's limits took over, and the box
     * stopped dead at its highest) */
    if (zt > 1) zoom.zt = zt;
    if (zt > 1) { zoom.e += 1.0f / ZOOM_IN_FRAMES; if (zoom.e > 1) zoom.e = 1; }
    else { zoom.e -= 1.0f / ZOOM_OUT_FRAMES; if (zoom.e < 0) zoom.e = 0; }
    float sm = zoom.e * zoom.e * (3 - 2 * zoom.e);
    zoom.z = 1 + (zoom.zt - 1) * sm;
    zoom_scene = 0;
    if (zoom.z > 1 && zoom.on && zoom_plane_ok) zoom_render_scene();   /* after the box closes, the frame is the scene */
}

static void present_frame(int zoomed)
{
    SDL_UpdateTexture(tex, NULL, &frame_rgb[0][0], FRAME_STRIDE * 4);
    SDL_Rect src = {0, 0, frame_width, ACTIVE_LINES};              /* only the width the game uses */
    SDL_RenderSetLogicalSize(ren, frame_width, ACTIVE_LINES);
    SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, &src, NULL);
    if (zoomed && zoom.z > 1) {
        /* the way there: the layout at the zoom aimed at, in proportion (zoom_layout's limits are
         * met all the way: a straight line from no zoom to a layout that meets them) */
        float z = zoom.z, up, dy, k = zoom.zt > 1 ? (z - 1) / (zoom.zt - 1) : 0;
        zoom_layout(zoom.zt, zoom.ay, &up, &dy);
        up *= k; dy *= k;
        int hud_h = zoom_hud(), top = (int)(hud_h - (up < hud_h ? up : hud_h));
        /* and on the way, zoom_layout's limit at this zoom: the scene's first line shown (hud_h)
         * at the field's top or above. The box can rise further than the HUD's lines (64 against
         * 48 under area 12's band), and in proportion alone the lines above the band showed, their
         * black sides, while it eased in */
        if (dy > top - hud_h * z) dy = top - hud_h * z;
        SDL_Rect field = {0, top, frame_width, ACTIVE_LINES - top};
        SDL_FRect dst = {zoom.ax * (1 - z), dy, frame_width * z, ACTIVE_LINES * z};
        if (zoom_scene) SDL_UpdateTexture(tex_zoom, NULL, &zoom_rgb[0][0], FRAME_STRIDE * 4);
        SDL_RenderSetClipRect(ren, &field);
        SDL_RenderCopyF(ren, zoom_scene ? tex_zoom : tex, &src, &dst);
        SDL_RenderSetClipRect(ren, NULL);
        SDL_Rect hud = {0, 0, frame_width, hud_h};                       /* the HUD, slid up */
        SDL_FRect hud_to = {0, -up, (float)frame_width, (float)hud_h};
        SDL_RenderCopyF(ren, tex, &hud, &hud_to);
        if (zoom.on) {                              /* the box and its pointer at their own size, slid up */
            SDL_FRect box_to = {(float)zoom.box.x, zoom.box.y - up, (float)zoom.box.w, (float)zoom.box.h};
            SDL_UpdateTexture(tex_box, NULL, &box_rgb[0][0], FRAME_STRIDE * 4);
            SDL_RenderCopyF(ren, tex_box, &zoom.box, &box_to);
            if (zoom.pointer_w) {
                SDL_Rect p = {zoom.pointer_x, zoom.box.y - 8, zoom.pointer_w, 8};
                float centre = zoom.ax + (p.x + p.w / 2.0f - zoom.ax) * z;   /* over the speaker, zoomed */
                SDL_FRect q = {centre - p.w / 2.0f, p.y - up, (float)p.w, 8};
                if (q.x < zoom.box.x + 8) q.x = (float)zoom.box.x + 8;
                if (q.x > zoom.box.x + zoom.box.w - 8 - p.w) q.x = (float)(zoom.box.x + zoom.box.w - 8 - p.w);
                SDL_RenderCopyF(ren, tex_box, &p, &q);
            }
        }
    }
    SDL_RenderPresent(ren);
    const char *dir = getenv("WBMW_SHOT_EVERY_DIR"); const char *every = getenv("WBMW_SHOT_EVERY");
    if (dir && every && atoi(every) > 0 && zoomed && frame % atoi(every) == 0) {   /* testing: the window's own picture */
        int w, h; SDL_GetRendererOutputSize(ren, &w, &h);
        uint32_t *px = malloc((size_t)w * h * 4);
        if (px && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0) {
            char path[1024]; snprintf(path, sizeof path, "%s/window_%05d.png", dir, frame);
            png_write_rgb(path, px, w, h, w);
        }
        free(px);
    }
}
static void present(void) { present_frame(0); }

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

/* ---- Enemy health: the damage numbers and the health bars, drawn over the finished picture
 * (docs/ENEMYHEALTH.md) ----
 * Each frame the host compares each enemy's health (-$2600) with the frame before: a drop is
 * damage, shown as a number rising from above it and fading, and its health is shown as a bar
 * over it for two seconds. Enemies are the entities of category 0 (-$2BFE, as the game's own
 * $4948 has it), not Wonder Boy; their maximum is their kind's (-$2BFD) in the table $4928 reads.
 * The game never knows. */
#define HIT_SLOTS 64
#define POPUPS 24
static struct { int hp, until; } hit_track[HIT_SLOTS];
static struct { int x, y, value, age, killing; } popup[POPUPS];   /* x, y: in the world (plane A's pixels) */
static int hits_started;

static int kind_max(int kind)
{
    uint32_t t = 0x1BA6C + (uint32_t)(int16_t)(rom[0x1BA6C] << 8 | rom[0x1BA6D]);
    return t + 3 * (uint32_t)kind < rom_size ? rom[t + 3 * kind] * 32 : 0;
}

/* what the bars and numbers go under: the window's lines (the HUD, and a "Found ..." box under
 * it) and an open message's box with its pointer's row */
static int hit_window_lines;
static SDL_Rect hit_box;

/* a pixel blended over the picture by a (0-256) */
static void blend_px(int x, int y, uint32_t c, int a)
{
    if (x < 0 || x >= frame_width || y < hit_window_lines || y >= ACTIVE_LINES) return;
    if (x >= hit_box.x && x < hit_box.x + hit_box.w && y >= hit_box.y && y < hit_box.y + hit_box.h) return;
    uint32_t b = frame_rgb[y][x];
    uint32_t r = ((b >> 16 & 255) * (256 - a) + (c >> 16 & 255) * a) >> 8;
    uint32_t g = ((b >> 8 & 255) * (256 - a) + (c >> 8 & 255) * a) >> 8;
    uint32_t l = ((b & 255) * (256 - a) + (c & 255) * a) >> 8;
    frame_rgb[y][x] = r << 16 | g << 8 | l;
}

/* text in the 5x7 font with a black outline, centred on x, faded by a */
static void draw_text_faded(int cx, int y, const char *t, uint32_t color, int a)
{
    int x0 = cx - (int)strlen(t) * 3;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; t[i]; i++) {
            const char *k = strchr(FONT7_CHARS, t[i]);
            if (!k) continue;
            for (int r = 0; r < 7; r++) for (int col = 0; col < 5; col++) {
                if (!(FONT7[k - FONT7_CHARS][r] >> (4 - col) & 1)) continue;
                int X = x0 + i * 6 + col, Y = y + r;
                if (pass == 0) { for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) blend_px(X + dx, Y + dy, 0, a); }
                else blend_px(X, Y, color, a);
            }
        }
}
static void draw_number(int cx, int y, int value, uint32_t color, int a)
{
    char t[12]; snprintf(t, sizeof t, "%d", value);
    draw_text_faded(cx, y, t, color, a);
}

/* what the numbers and bars go under, this frame */
static void hit_limits(int pad)
{
    hit_window_lines = vdp_reg[18] & 0x80 ? 0 : (vdp_reg[18] & 0x1F) * 8;   /* a window from the top */
    if (vdp_hud_wide && !(vdp_reg[12] & 1)) {       /* the wide HUD: its top row, and no hidden message's box */
        if (hit_window_lines == 64 || vdp_hide_msg) hit_window_lines = 32;
    }
    hit_box = (SDL_Rect){0, 0, 0, 0};
    if (ram[0x8CA4] & 0x80) {
        int col = ram[0x8D92] << 8 | ram[0x8D93], row = ram[0x8D94] << 8 | ram[0x8D95];
        int w = ram[0x8D96] << 8 | ram[0x8D97], h = ram[0x8D98] << 8 | ram[0x8D99];
        hit_box = (SDL_Rect){pad + col * 8, 64 + row * 8 - 8, w * 8, h * 8 + 8};
    }
}

/* Widescreen: gold found is shown as its amount rising in gold from Wonder Boy, like the damage
 * numbers, instead of the "Found 5 GOLD." box under the HUD (hidden: wide_frame, vdp_hide_msg) */
#define GOLD_POPS 8
static struct { int x, y, value, age; } gold_pop[GOLD_POPS];
static void gold_found(int amount)
{
    uint32_t p = 0xFFFF0000u + (ram[0x9EEE] << 8 | ram[0x9EEF]);
    for (int i = 0; i < GOLD_POPS; i++) if (!gold_pop[i].age) {
        gold_pop[i].x = rd16(p - 0x3800); gold_pop[i].y = rd16(p - 0x3700) - 2 * rd8(p - 0x34FD) - 10;
        gold_pop[i].value = amount; gold_pop[i].age = 1;
        return;
    }
}
/* a state loaded: the health bars, numbers and the zoom belonged to the run before it (its
 * enemies' bars stayed up over the loaded scene) */
static void overlays_reset(void)
{
    memset(hit_track, 0, sizeof hit_track); memset(popup, 0, sizeof popup); memset(gold_pop, 0, sizeof gold_pop);
    hits_started = 0;
    vdp_hide_msg = 0;
    zoom.z = 1; zoom.e = 0; zoom.on = 0;
}
static void draw_text_faded(int cx, int y, const char *t, uint32_t color, int a);
static void gold_frame(void)
{
    if ((vdp_reg[12] & 1) || !(vdp_reg[1] & 0x40)) return;
    int pad = (frame_width - 256) / 2;
    int cam_x = ram[0x8B34] << 8 | ram[0x8B35], cam_y = ram[0x8B36] << 8 | ram[0x8B37];
    hit_limits(pad);
    for (int i = 0; i < GOLD_POPS; i++) {           /* up 20 pixels in 60 frames, the last 20 fading */
        if (!gold_pop[i].age) continue;
        char t[16]; snprintf(t, sizeof t, "+%d", gold_pop[i].value);
        int sx = pad + (int16_t)(gold_pop[i].x - cam_x - 128);
        int sy = (int16_t)(gold_pop[i].y - cam_y - 128) - gold_pop[i].age / 3;
        int a = gold_pop[i].age > 40 ? (60 - gold_pop[i].age) * 256 / 20 : 256;
        draw_text_faded(sx, sy, t, 0xFFD860, a);
        if (++gold_pop[i].age > 60) gold_pop[i].age = 0;
    }
}

static void hits_frame(void)
{
    if (!enemy_health || (vdp_reg[12] & 1) || !(vdp_reg[1] & 0x40)) { hits_started = 0; return; }
    uint32_t player = 0xFFFF0000u + (ram[0x9EEE] << 8 | ram[0x9EEF]);
    int pad = (frame_width - 256) / 2;
    int cam_x = ram[0x8B34] << 8 | ram[0x8B35], cam_y = ram[0x8B36] << 8 | ram[0x8B37];
    hit_limits(pad);
    for (int k = 0; k < HIT_SLOTS; k++) {
        uint32_t o = 0xFFFF0000u + 4 * (uint32_t)k;
        int hp = rd16(o - 0x2600), max = kind_max(rd8(o - 0x2BFD));
        if (!(rd8(o - 0x4000) & 0x80) || o == player || rd8(o - 0x2BFE) || !max || hp > max) { hit_track[k].hp = -1; continue; }
        if (hits_started && hit_track[k].hp > hp) {   /* hurt: its number, and its bar for 2 seconds */
            for (int i = 0; i < POPUPS; i++) if (!popup[i].age) {
                popup[i].x = rd16(o - 0x3800); popup[i].y = rd16(o - 0x3700) - 2 * rd8(o - 0x34FD) - 10;
                popup[i].value = hit_track[k].hp - hp; popup[i].age = 1; popup[i].killing = !hp;
                break;
            }
            hit_track[k].until = frame + 120;
        }
        hit_track[k].hp = hp;
        if (hp && hit_track[k].until > frame && !(rd8(o - 0x4000) & 4)) {   /* the bar, unless hidden */
            int sx = pad + (int16_t)(rd16(o - 0x3800) - cam_x - 128);
            int sy = (int16_t)(rd16(o - 0x3700) - cam_y - 128) - 2 * rd8(o - 0x34FD) - 6;
            int w = 20, fill = (hp * w + max - 1) / max;
            uint32_t col = hp * 3 > max * 2 ? 0x40E040 : hp * 3 > max ? 0xF0D020 : 0xF04030;
            int a = hit_track[k].until - frame < 20 ? (hit_track[k].until - frame) * 256 / 20 : 256;
            for (int y = -1; y <= 3; y++) for (int x = -1; x <= w; x++) {
                int edge = y < 0 || y > 2 || x < 0 || x >= w;
                blend_px(sx - w / 2 + x, sy + y, edge ? 0x000000 : x < fill ? col : 0x303030, a);
            }
        }
    }
    hits_started = 1;
    for (int i = 0; i < POPUPS; i++) {             /* the numbers: up 20 pixels in 60 frames, the last 20 fading */
        if (!popup[i].age) continue;
        int sx = pad + (int16_t)(popup[i].x - cam_x - 128);
        int sy = (int16_t)(popup[i].y - cam_y - 128) - popup[i].age / 3;
        int a = popup[i].age > 40 ? (60 - popup[i].age) * 256 / 20 : 256;
        draw_number(sx, sy, popup[i].value, popup[i].killing ? 0xFFD860 : 0xFFFFFF, a);
        if (++popup[i].age > 60) popup[i].age = 0;
    }
}

/* ---- the save slots in the menu ---- */

/* a slot saved at the next frame wait, or loaded (quick save and load, the menu) */
static void slot_save(int n) { save_pending = frame; save_to = slot_file(n, 0); save_slot = n; }
static void slot_load(int n) { load_slot = n; request_load(slot_file(n, 0)); }

/* what the menu shows of each slot: used, when (the state's time), its picture */
static struct { int used, w, h; char when[24]; uint32_t px[(ACTIVE_LINES / 2) * (FRAME_STRIDE / 2)]; } slot_info[SLOTS + 1];
static void slots_scan(void)
{
    for (int n = 1; n <= SLOTS; n++) {
        struct stat st;
        slot_info[n].used = stat(slot_file(n, 0), &st) == 0;
        slot_info[n].w = 0;
        if (!slot_info[n].used) continue;
        strftime(slot_info[n].when, sizeof slot_info[n].when, "%b %d %H:%M", localtime(&st.st_mtime));
        for (char *p = slot_info[n].when; *p; p++) *p = (char)toupper((unsigned char)*p);
        if (png_read_rgb(slot_file(n, 1), slot_info[n].px, (int)(sizeof slot_info[n].px / 4), &slot_info[n].w, &slot_info[n].h))
            slot_info[n].w = 0;                     /* a state without a picture (saved before the slots) */
    }
}

/* slot n's picture fitted in the rectangle, centred; or a word where there is none */
static void draw_slot_picture(int n, int bx, int by, int bw, int bh)
{
    int w = slot_info[n].w, h = slot_info[n].h;
    if (!w) {
        const char *t = slot_info[n].used ? "NO PICTURE" : "EMPTY";
        draw_text(bx + bw / 2 - (int)strlen(t) * 3, by + bh / 2 - 3, t, MENU_GREY);
        return;
    }
    int dw = bw, dh = h * bw / w;
    if (dh > bh) { dh = bh; dw = w * bh / h; }
    int ox = bx + (bw - dw) / 2, oy = by + (bh - dh) / 2;
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) frame_rgb[oy + y][ox + x] = slot_info[n].px[(y * h / dh) * w + x * w / dw];
}

/* a frame around a rectangle, and black inside */
static void draw_frame(int x0, int y0, int w, int h, uint32_t edge)
{
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++)
        frame_rgb[y][x] = (y == y0 || y == y0 + h - 1 || x == x0 || x == x0 + w - 1) ? edge : 0;
}

/* the menu's screens: the main one, the exit question, the controls, the controls waiting for a
 * press (capture), the slots (to save or to load: col), and the question before a save over one */
enum { SCR_MAIN, SCR_ASK, SCR_CONTROLS, SCR_CAPTURE, SCR_SLOTS, SCR_OVERWRITE };
enum { MI_SAVE, MI_LOAD, MI_FPS, MI_FULL, MI_PIXELS, MI_WIDE, MI_ZOOM, MI_HEALTH, MI_VOLUME, MI_MUSIC, MI_EFFECTS, MI_CONTROLS, MI_EXIT, MAIN_N };
static const char *const MAIN_ITEM[MAIN_N] = {"SAVE STATE", "LOAD STATE", "SHOW FPS", "FULL SCREEN", "WHOLE PIXELS", "WIDESCREEN",
                                              "TALK ZOOM", "ENEMY HEALTH", "VOLUME", "  MUSIC", "  EFFECTS", "CONTROLS", "EXIT GAME"};
/* a volume item's value (0-100), or NULL for the others */
static int *menu_volume(int item)
{
    return item == MI_VOLUME ? &volume : item == MI_MUSIC ? &music_volume : item == MI_EFFECTS ? &effects_volume : NULL;
}
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
        int w = 150, h = 40 + MAIN_N * 13, x0 = frame_width / 2 - w / 2, y0 = ACTIVE_LINES / 2 - h / 2;
        draw_box(x0, y0, w, h);
        draw_text_centred(y0 + 7, "PAUSED", MENU_WHITE);
        for (int i = 0; i < MAIN_N; i++) {
            uint32_t col = i == m->item ? MENU_GOLD : MENU_WHITE;
            int y = y0 + 22 + i * 13;
            int on = i == MI_FPS ? show_fps : i == MI_FULL ? fullscreen : i == MI_PIXELS ? integer_scale : i == MI_WIDE ? widescreen : i == MI_ZOOM ? talk_zoom : i == MI_HEALTH ? enemy_health : -1;
            if (i == m->item) draw_text(x0 + 12, y, ">", col);
            draw_text(x0 + 24, y, MAIN_ITEM[i], col);
            if (on >= 0) draw_text(x0 + w - 12 - 18, y, on ? "ON" : "OFF", col);
            int *vol = menu_volume(i);
            if (vol) {                              /* a bar of ten, then the percent (the font has no %) */
                char pc[8]; snprintf(pc, sizeof pc, "%d", *vol);
                for (int k = 0; k < 10; k++)
                    for (int yy = y + 1; yy < y + 6; yy++) for (int xx = 0; xx < 2; xx++)
                        frame_rgb[yy][x0 + 82 + k * 3 + xx] = k < *vol / 10 ? col : MENU_GREY;
                draw_text(x0 + w - 12 - (int)strlen(pc) * 6, y, pc, col);
            }
        }
        draw_text_centred(y0 + h - 13, gc ? "ESC/SELECT: RESUME" : "ESC: RESUME", MENU_GREY);
    } else if (m->screen == SCR_SLOTS) {             /* the four slots, 2 by 2: picture, number, time */
        int wide = frame_width >= 400, cw = wide ? 176 : 116, gap = wide ? 8 : 6, th = 76;
        int w = 2 * cw + 3 * gap, h = 220, x0 = frame_width / 2 - w / 2, y0 = ACTIVE_LINES / 2 - h / 2;
        draw_box(x0, y0, w, h);
        draw_text_centred(y0 + 5, m->col ? "LOAD STATE" : "SAVE STATE", MENU_WHITE);
        for (int k = 0; k < SLOTS; k++) {
            int n = k + 1, sel = k == m->row, cx = x0 + gap + (k & 1) * (cw + gap), cy = y0 + 16 + (k >> 1) * (th + 19);
            draw_frame(cx, cy, cw, th, sel ? MENU_GOLD : MENU_GREY);
            draw_slot_picture(n, cx + 2, cy + 2, cw - 4, th - 4);
            char label[40]; snprintf(label, sizeof label, "%d  %s", n, slot_info[n].used ? slot_info[n].when : "EMPTY");
            draw_text(cx + cw / 2 - (int)strlen(label) * 3, cy + th + 3, label, sel ? MENU_GOLD : MENU_WHITE);
        }
        const char *keys = m->col ? (gc ? "ENTER/A: LOAD   ESC/B: BACK" : "ENTER: LOAD   ESC: BACK")
                                  : (gc ? "ENTER/A: SAVE   ESC/B: BACK" : "ENTER: SAVE   ESC: BACK");
        draw_text_centred(y0 + h - 12, keys, MENU_GREY);
    } else if (m->screen == SCR_OVERWRITE) {         /* the slot's picture, and the question */
        int n = m->row + 1, pw = slot_info[n].w ? slot_info[n].w : 128, ph = ACTIVE_LINES / 2;
        int w = pw + 16 > 170 ? pw + 16 : 170, h = ph + 66, x0 = frame_width / 2 - w / 2, y0 = ACTIVE_LINES / 2 - h / 2;
        draw_box(x0, y0, w, h);
        char q[32]; snprintf(q, sizeof q, "OVERWRITE SLOT %d?", n);
        draw_text_centred(y0 + 6, q, MENU_WHITE);
        draw_frame(frame_width / 2 - pw / 2 - 1, y0 + 17, pw + 2, ph + 2, MENU_GREY);
        draw_slot_picture(n, frame_width / 2 - pw / 2, y0 + 18, pw, ph);
        int y = y0 + ph + 26, cx = frame_width / 2;
        draw_text(cx - 40, y, m->yes ? " NO" : ">NO", m->yes ? MENU_WHITE : MENU_GOLD);
        draw_text(cx + 16, y, m->yes ? ">YES" : " YES", m->yes ? MENU_GOLD : MENU_WHITE);
        draw_text_centred(y0 + h - 12, gc ? "ESC/B: BACK" : "ESC: BACK", MENU_GREY);
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
            case MI_SAVE: case MI_LOAD:
                if (ok) { slots_scan(); m->screen = SCR_SLOTS; m->col = m->item == MI_LOAD; m->row = slot - 1; }
                break;
            case MI_FPS: toggle_fps(); break;
            case MI_FULL: toggle_fullscreen(); break;
            case MI_PIXELS:
                integer_scale = cfg.integer_scale = !integer_scale; ini_dirty = 1;
                SDL_RenderSetIntegerScale(ren, integer_scale ? SDL_TRUE : SDL_FALSE);
                break;
            case MI_WIDE: widescreen = cfg.widescreen = !widescreen; wide_apply(); ini_dirty = 1; break;
            case MI_ZOOM: talk_zoom = cfg.talk_zoom = !talk_zoom; ini_dirty = 1; break;
            case MI_HEALTH: enemy_health = cfg.enemy_health = !enemy_health; ini_dirty = 1; break;
            case MI_VOLUME: case MI_MUSIC: case MI_EFFECTS: {
                int *vol = menu_volume(m->item);
                *vol = left ? volume_clamp(*vol - 10) : right ? volume_clamp(*vol + 10) : *vol >= 100 ? 0 : *vol + 10;
                cfg.volume = volume; cfg.music_volume = music_volume; cfg.effects_volume = effects_volume;
                ini_dirty = 1;
                break;
            }
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
    case SCR_SLOTS:                                 /* the grid: left and right, up and down */
        if (left || right) m->row ^= 1;
        if (up || down) m->row ^= 2;
        if (ok) {
            int n = m->row + 1;
            if (m->col) { if (slot_info[n].used) { slot = n; ini_dirty = 1; slot_load(n); return 0; } }
            else if (slot_info[n].used) { m->screen = SCR_OVERWRITE; m->yes = 0; }
            else { slot = n; ini_dirty = 1; slot_save(n); return 0; }
        }
        if (back) m->screen = SCR_MAIN;
        break;
    case SCR_OVERWRITE:
        if (up || down || left || right) m->yes ^= 1;
        if (ok && m->yes) { slot = m->row + 1; ini_dirty = 1; slot_save(slot); return 0; }
        if ((ok && !m->yes) || back) m->screen = SCR_SLOTS;
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
            unsigned state = (unsigned)(m.screen | m.item << 3 | m.yes << 7 | m.row << 8 | m.col << 12 | show_fps << 13
                                        | fullscreen << 14 | integer_scale << 15 | stick_moves << 16);
            for (int a = 0; a < ACTIONS; a++) state = state * 31 + (unsigned)key_bind[a] * 7 + (unsigned)(pad_bind[a] + 1);
            state = state * 31 + (unsigned)(volume + 101 * music_volume + 10201 * effects_volume);
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
    tex_zoom = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, FRAME_STRIDE, 224);
    tex_box = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, FRAME_STRIDE, 224);
    SDL_SetTextureBlendMode(tex_box, SDL_BLENDMODE_BLEND);   /* the box's transparent pixels */
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
/* quick save, at the next frame wait; over an existing state only when pressed again within two
 * seconds (a save was too easily overwritten by accident) */
static void quick_save(void)
{
    static Uint32 confirm_until;
    static char said[48];
    FILE *f = fopen(slot_file(slot, 0), "rb");
    int exists = f != NULL;
    if (f) fclose(f);
    int confirming = confirm_until && !SDL_TICKS_PASSED(SDL_GetTicks(), confirm_until);
    if (exists && !confirming) {
        confirm_until = SDL_GetTicks() + 2000;
        snprintf(said, sizeof said, "SLOT %d: PRESS SAVE AGAIN TO OVERWRITE", slot);
        toast = said; toast_frames = 120;
        return;
    }
    confirm_until = 0;
    slot_save(slot);
}

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
    if (load) slot_load(slot);
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
    if (script_n) b |= buttons_at(frame + 1);       /* testing: --input works in a window too */
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
    memcpy(clean_rgb, frame_rgb, sizeof clean_rgb); clean_width = frame_width;   /* for a slot's picture */
    if (show_fps) draw_fps();
    if (toast_frames > 0) { toast_frames--; draw_toast(); }
    zoom_update();
    present_frame(1);
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
#ifndef WBMW_HEADLESS_ONLY
    hits_frame();                                   /* Enemy health, drawn over the picture */
    gold_frame();                                   /* widescreen: the gold found */
#endif
    volume_frame();                                 /* the volumes for the next frame's sound */
    static int play_sound_wait = 10;
    if (play_sound >= 0 && --play_sound_wait <= 0) {   /* --play-sound N: sound N queued, as the game does, once, 10 frames in */
        Cpu t; memset(&t, 0, sizeof t); t.d[0] = (uint32_t)play_sound; play_sound = -1;
        QueueSound(&t);
    }
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
    if (line == 0) { vdp_set_vblank(0); hcount = vdp_hint_counter_reload(); wide_frame(); }
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
#ifndef WBMW_HEADLESS_ONLY
    zoom_plane_ok = 0;                              /* the talk zoom's map from before a box is another scene's */
    overlays_reset();
#endif
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
        static char said[32];
        if (load_state(path, the_cpu, &pc) == 0) {
            if (load_slot) { snprintf(said, sizeof said, "LOADED SLOT %d", load_slot); toast = said; }
            load_slot = 0;
            resume_pc = pc; __builtin_longjmp(restart, 1);
        }
        toast = "NO STATE TO LOAD"; toast_frames = 90;
        load_slot = 0;
    }
    if (save_pending < 0 || frame < save_pending || !resumable(c, pc)) return;
    if (save_state(save_to, c, pc) == 0 && save_slot) {   /* a slot: its picture too */
        static char said[32];
        save_picture(slot_file(save_slot, 1));
        snprintf(said, sizeof said, "SAVED TO SLOT %d", save_slot); toast = said;
    }
    save_pending = -1; save_slot = 0;
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
    wide_frame();
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
/* the first of tries (each with %s the program's folder) that exists */
static const char *find_file(const char *argv0, const char *const *tries, int n)
{
    static char path[4096];
    char dir[4096] = ".";
    const char *slash = strrchr(argv0, '/'), *bs = strrchr(argv0, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
    if (slash && (size_t)(slash - argv0) < sizeof dir) { memcpy(dir, argv0, (size_t)(slash - argv0)); dir[slash - argv0] = 0; }
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof path, tries[i], dir);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return path; }
    }
    return NULL;
}
static const char *find_rom(const char *argv0)
{
    static const char *const tries[] = { "rom/" ROM_NAME, ROM_NAME, "%s/rom/" ROM_NAME, "%s/" ROM_NAME, "%s/../../rom/" ROM_NAME };
    return find_file(argv0, tries, 5);
}
/* the pack tools/makepack.sh makes (the build runs it): played from when no ROM is named */
static const char *find_pack(const char *argv0)
{
    static const char *const tries[] = { "assets/game.pak", "game.pak", "%s/assets/game.pak", "%s/game.pak", "%s/../../assets/game.pak" };
    return find_file(argv0, tries, 5);
}

/* --rom-reads FILE: which of the ROM's bytes the game reads (a byte each, 1 read), added to what
 * FILE holds, so that runs from many states add up; tools/rebuild.py --reads FILE compares them
 * with what the extracted files cover (docs/EXTRACT.md) */
static const char *rom_reads_path;
static void rom_reads_save(void)
{
    FILE *f = fopen(rom_reads_path, "wb");
    if (f) { fwrite(bus_rom_reads, 1, rom_size, f); fclose(f); }
}

/* --pack FILE: the game's data from the files tools/extract.py wrote, packed by tools/pack.py
 * (docs/EXTRACT.md, "The game reads the files"), instead of from the ROM. The pack's data image
 * takes the ROM's place in the address space; the decoders take the compressed data's decoded
 * form from it (port_pack_find). The mask says which bytes the files hold: a read of any other is
 * counted (bus_rom_reads) and reported at exit, since the game should never make one. */
static uint8_t *pack, *pack_mask;
static const uint8_t *pack_gfx;
typedef struct { uint32_t key; const uint8_t *at; } PackEntry;
static PackEntry *pack_tables, *pack_layers;
static int pack_ntables, pack_nlayers;
static int pack_loud;                               /* --pack named: say at exit that all was well too */
static uint32_t be32p(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static const uint8_t *pack_lookup(PackEntry *e, int n, uint32_t key)
{
    int lo = 0, hi = n - 1;                         /* sorted by key (pack.py) */
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (e[m].key == key) return e[m].at;
        if (e[m].key < key) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

static const uint8_t *pack_find(int kind, uint32_t key)
{
    if (kind == 'G') return key < 768 ? pack_gfx + 1024 * key : NULL;
    if (kind == 'B') return pack_lookup(pack_tables, pack_ntables, key & 0xFFFFFF);
    if (kind == 'L') return pack_lookup(pack_layers, pack_nlayers, key & 0xFFFFFF);
    return NULL;
}

static void pack_report(void)
{
    size_t n = 0, first = 0;
    for (size_t i = 0; i < rom_size; i++)
        if (bus_rom_reads[i] && !pack_mask[i]) { if (!n) first = i; n++; }
    if (n) fprintf(stderr, "wbmw: the game read %lu bytes the files do not hold (the first at $%06lX)\n",
                   (unsigned long)n, (unsigned long)first);
    else if (pack_loud) fprintf(stderr, "wbmw: every byte the game read is held by the files\n");
}

static int pack_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { char m[4200]; snprintf(m, sizeof m, "Cannot open the pack: %s", path); fail(m); return 1; }
    fseek(f, 0, SEEK_END); size_t size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    pack = malloc(size);
    size_t got = fread(pack, 1, size, f);
    fclose(f);
    if (got != size || size < 12 || memcmp(pack, "WBMWPAK1", 8)) {
        char m[4200]; snprintf(m, sizeof m, "%s is not a pack made by tools/pack.py", path); fail(m); return 1;
    }
    const uint8_t *p = pack + 8;
    rom_size = be32p(p); p += 4;
    rom = (uint8_t *)p; p += rom_size;
    pack_mask = (uint8_t *)p; p += rom_size;
    pack_gfx = p; p += 768 * 1024;
    pack_ntables = (int)be32p(p); p += 4;
    pack_tables = calloc((size_t)pack_ntables, sizeof *pack_tables);
    for (int i = 0; i < pack_ntables; i++) {
        pack_tables[i].key = be32p(p); pack_tables[i].at = p + 4;
        p += 6 + 10 * (p[4] << 8 | p[5]);
    }
    pack_nlayers = (int)be32p(p); p += 4;
    pack_layers = calloc((size_t)pack_nlayers, sizeof *pack_layers);
    for (int i = 0; i < pack_nlayers; i++) {
        pack_layers[i].key = be32p(p); pack_layers[i].at = p + 4;
        p += 9 + 385 * p[8];
    }
    if (p != pack + size) { char m[4200]; snprintf(m, sizeof m, "%s is damaged (its parts do not add up)", path); fail(m); return 1; }
    port_pack_find = pack_find;
    return 0;
}

/* --verify-graphics DIR: the 768 graphics blocks decoded by the port's DecodeGfxBlock ($6BC4,
 * verified against the original's in the harness) compared with the files tools/extract.py wrote
 * (DIR/graphics/block_NNN.bin): its Python decoder checked by an independent one (docs/EXTRACT.md) */
void DecodeGfxBlock(Cpu *c);
static int verify_graphics_blocks(const char *dir)
{
    int bad = 0, missing = 0;
    for (int i = 0; i < 768; i++) {
        char path[4200]; snprintf(path, sizeof path, "%s/graphics/block_%03d.bin", dir, i);
        uint8_t file[1024];
        FILE *f = fopen(path, "rb");
        if (!f || fread(file, 1, sizeof file, f) != sizeof file) { missing++; if (f) fclose(f); continue; }
        fclose(f);
        Cpu c; memset(&c, 0, sizeof c);
        c.d[0] = (uint32_t)i; c.a[1] = 0xFFFF2D00u; c.a[7] = 0xFFFF0C00u;
        wr32(c.a[7] - 4, 0); c.a[7] -= 4;           /* a return address, as a jsr leaves */
        DecodeGfxBlock(&c);
        if (memcmp(&ram[0x2D00], file, sizeof file)) { if (bad < 5) fprintf(stderr, "block %d differs\n", i); bad++; }
    }
    printf("graphics: %d blocks decoded by the port, %d differ from the files, %d files missing\n", 768 - missing, bad, missing);
    return bad || missing ? 1 : 0;
}

/* --unpack-list REQ OUT: the rooms' block tables and layer maps unpacked by the port's own
 * unpackers (UnpackBlocks $9F6A, UnpackLayerMap $A068, verified against the original's in the
 * harness), for tools/rebuild.py to compare with tools/extract.py's (docs/EXTRACT.md). REQ has a
 * line a table, "B address" (a block table) or "L address" (a layer's data); OUT gets a record
 * each: the letter, the address (4 bytes), then for B the count (2 bytes) and 8 bytes of tile
 * words and the attribute byte a block, for L the 64 bytes of the layer's map and the 16 chunks'
 * 192 cells (words). The buffers are filled with $EE first, so a block left empty shows.
 * OUT.reads marks the ROM bytes the unpackers read (a byte each, 1 read), for the coverage. */
void UnpackBlocks(Cpu *c), UnpackLayerMap(Cpu *c);
#define UNPACK_TABLE 0xFFFF7000u                   /* a block table: 8 bytes a block, the attributes $800 on */
#define UNPACK_RECORD 0xFFFF7C00u                  /* a layer's record ($22 its map, $1C its table, $34 its data) */
#define UNPACK_MAP 0xFFFFA000u                     /* (sign-extended from a word: at $8000 up to be RAM) */
static int unpack_list(const char *req, const char *out)
{
    FILE *in = fopen(req, "r"), *o = fopen(out, "wb");
    if (!in || !o) { perror(in ? out : req); return 1; }
    bus_rom_reads = calloc(rom_size, 1);            /* the ROM bytes the unpackers read: OUT.reads */
    char kind; unsigned addr; int n = 0;
    while (fscanf(in, " %c %x", &kind, &addr) == 2) {
        Cpu c; memset(&c, 0, sizeof c);
        c.a[7] = 0xFFFF0C00u - 4; wr32(c.a[7], 0);  /* a return address, as a jsr leaves */
        uint8_t head[5] = {(uint8_t)kind, (uint8_t)(addr >> 24), (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
        fwrite(head, 1, 5, o);
        memset(&ram[UNPACK_TABLE & 0xFFFF], 0xEE, 0x1000);
        if (kind == 'B') {
            c.a[3] = addr; c.a[1] = UNPACK_TABLE;
            UnpackBlocks(&c);
            unsigned count = rd16(addr);
            uint8_t cnt[2] = {(uint8_t)(count >> 8), (uint8_t)count};
            fwrite(cnt, 1, 2, o);
            for (unsigned i = 0; i < count && i < 0x800; i++) {
                fwrite(&ram[(UNPACK_TABLE & 0xFFFF) + 8 * i], 1, 8, o);
                fwrite(&ram[(UNPACK_TABLE & 0xFFFF) + 0x800 + i], 1, 1, o);
            }
        } else {
            memset(&ram[0x5000], 0xEE, 16 * 0x180);
            memset(&ram[UNPACK_RECORD & 0xFFFF], 0, 0x80);
            wr16(UNPACK_RECORD + 0x22, (uint16_t)UNPACK_MAP);     /* its map: a word, sign-extended */
            wr32(UNPACK_RECORD + 0x1C, UNPACK_TABLE);
            wr32(UNPACK_RECORD + 0x34, addr);
            c.a[3] = UNPACK_RECORD;
            UnpackLayerMap(&c);
            fwrite(&ram[UNPACK_MAP & 0xFFFF], 1, 64, o);
            fwrite(&ram[0x5000], 1, 16 * 0x180, o);
        }
        n++;
    }
    fclose(in); fclose(o);
    char rpath[4200]; snprintf(rpath, sizeof rpath, "%s.reads", out);
    FILE *r = fopen(rpath, "wb");
    if (r) { fwrite(bus_rom_reads, 1, rom_size, r); fclose(r); }
    printf("unpacked %d tables with the port's unpackers\n", n);
    return 0;
}

int main(int argc, char **argv)
{
    const char *pack_path = NULL;
    const char *rom_path = NULL, *input = NULL, *load_path = NULL, *verify_graphics = NULL, *unpack_req = NULL, *unpack_out = NULL;
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
        else if (!strcmp(argv[i], "--frame-log-skip") && i + 1 < argc) {   /* LO-HI: RAM addresses (hex) left out */
            unsigned lo, hi;
            if (sscanf(argv[++i], "%x-%x", &lo, &hi) == 2) framelog_skip(lo & 0xFFFF, hi & 0xFFFF);
        }
        else if (!strcmp(argv[i], "--input-game") && i + 1 < argc) { if (framelog_input(argv[++i])) return 1; }
        else if (!strcmp(argv[i], "--dump-at") && i + 2 < argc) { long n = atol(argv[++i]); framelog_dump_at(n, argv[++i]); }
        else if (!strcmp(argv[i], "--mute")) mute = 1;
        else if (!strcmp(argv[i], "--z80-sound")) z80_sound = 1;    /* the original driver on the emulated Z80 */
        else if (!strcmp(argv[i], "--verify-graphics") && i + 1 < argc) { verify_graphics = argv[++i]; headless = 1; }
        else if (!strcmp(argv[i], "--rom-reads") && i + 1 < argc) rom_reads_path = argv[++i];
        else if (!strcmp(argv[i], "--pack") && i + 1 < argc) pack_path = argv[++i];
        else if (!strcmp(argv[i], "--unpack-list") && i + 2 < argc) { unpack_req = argv[++i]; unpack_out = argv[++i]; headless = 1; }
        else if (!strcmp(argv[i], "--widescreen")) widescreen = 1;
        else if (!strcmp(argv[i], "--no-widescreen")) widescreen = 0;
        else if (!strcmp(argv[i], "--enemy-health")) enemy_health = 1;
        else if (!strcmp(argv[i], "--no-enemy-health")) enemy_health = 0;
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
        else if (!strcmp(argv[i], "--talk-zoom")) talk_zoom = 1;
        else if (!strcmp(argv[i], "--no-talk-zoom")) talk_zoom = 0;
        else if (!strcmp(argv[i], "--play-sound") && i + 1 < argc) play_sound = (int)strtol(argv[++i], NULL, 16) & 0xFF;   /* hex, as the sound table numbers them */
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) volume = volume_clamp(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--music-volume") && i + 1 < argc) music_volume = volume_clamp(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--effects-volume") && i + 1 < argc) effects_volume = volume_clamp(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--integer-scale")) integer_scale = 1;
        else if (!strcmp(argv[i], "--fps")) show_fps = 1;
#endif
        else rom_path = argv[i];
    }
    int pack_named = pack_loud = pack_path != NULL;
    if (!pack_path && !rom_path && !verify_graphics && !unpack_req) pack_path = find_pack(argv[0]);
    if (pack_path) { if (pack_load(pack_path)) return 1; if (!pack_named) fprintf(stderr, "playing from %s\n", pack_path); }
    else if (!rom_path) rom_path = find_rom(argv[0]);
    if (!pack_path && !rom_path) {
        fprintf(stderr, "usage: wbmw [ROM] [--windowed] [--widescreen] [--scale N] [--save FILE] [--integer-scale] [--fps] [--mute] [--talk-zoom] [--enemy-health] [--config FILE]\n"
                        "       wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE] [--wav FILE]\n"
                        "                [--frame-log FILE] [--input-game FILE] [--dump-at N DIR]\n"
                        "       states: [--state FILE] [--load] [--load-state FILE] [--save-state-at N FILE]; F2 saves, F4 loads\n");
        fail("No ROM found. Put your own dump of the game, named\n\n    " ROM_NAME "\n\n"
             "in a folder called rom (in the repository, or next to wbmw), or give its path:\n\n    wbmw path/to/your.rom");
        return 2;
    }
    FILE *f = pack_path ? NULL : fopen(rom_path, "rb");
    if (!pack_path && !f) { char m[4200]; snprintf(m, sizeof m, "Cannot open the ROM: %s", rom_path); fail(m); return 1; }
    if (f) {
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
    }
    wide_apply();
    if (rom_reads_path) {                           /* the ROM's bytes this run reads, added to the file's */
        bus_rom_reads = calloc(rom_size, 1);
        FILE *rf = fopen(rom_reads_path, "rb");
        if (rf) { if (fread(bus_rom_reads, 1, rom_size, rf) != rom_size) memset(bus_rom_reads, 0, rom_size); fclose(rf); }
        atexit(rom_reads_save);
    }
    if (pack) {                                     /* the reads the files should hold, checked at exit */
        if (!bus_rom_reads) bus_rom_reads = calloc(rom_size, 1);
        atexit(pack_report);
    }
    bus_reset(); vdp_reset(); sound_reset();
    sound_c_driver = !z80_sound;                    /* the sound driver in C (port/sounddrv.c) */
    if (verify_graphics) return verify_graphics_blocks(verify_graphics);
    if (unpack_req) return unpack_list(unpack_req, unpack_out);
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
