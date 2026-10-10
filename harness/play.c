/* Live window for the reference harness: the same machine as `harness`, shown in an SDL2 window
 * at 60 frames a second, with keyboard and game controller input. No sound (the Z80 and the
 * sound chips are not emulated).
 *
 *   play ROM [--scale N] [--save FILE]
 *
 * Keys: arrows = D-pad, Z = A, X = B, C = C, Enter = Start.
 *       Esc quits, P pauses, Tab (held) runs fast, F12 writes a screenshot, F5 resets.
 * Controller: D-pad or left stick, X = A, A = B, B = C, Start = Start.
 * The save EEPROM is loaded from and written back to FILE (default play.sav). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>
#include "md.h"

enum { B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_B = 16, B_C = 32, B_A = 64, B_START = 128 };

static uint8_t keys_down(const Uint8 *k)
{
    uint8_t m = 0;
    if (k[SDL_SCANCODE_UP]) m |= B_UP;
    if (k[SDL_SCANCODE_DOWN]) m |= B_DOWN;
    if (k[SDL_SCANCODE_LEFT]) m |= B_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) m |= B_RIGHT;
    if (k[SDL_SCANCODE_Z]) m |= B_A;
    if (k[SDL_SCANCODE_X]) m |= B_B;
    if (k[SDL_SCANCODE_C]) m |= B_C;
    if (k[SDL_SCANCODE_RETURN]) m |= B_START;
    return m;
}

static uint8_t pad_down(SDL_GameController *gc)
{
    if (!gc) return 0;
    uint8_t m = 0;
    int ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    int ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -16000) m |= B_UP;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 16000) m |= B_DOWN;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -16000) m |= B_LEFT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 16000) m |= B_RIGHT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) m |= B_A;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) m |= B_B;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) m |= B_C;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) m |= B_START;
    return m;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *save = "play.sav";
    int scale = 3;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
        else rom_path = argv[i];
    }
    if (!rom_path) { fprintf(stderr, "usage: play ROM [--scale N] [--save FILE]\n"); return 2; }
    if (md_load_rom(rom_path)) return 1;
    md_power_on();
    if (eeprom_load(save) == 0) fprintf(stderr, "loaded save data from %s\n", save);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("Wonder Boy in Monster World - harness  (arrows, Z/X/C, Enter; P pause, Tab fast, F12 shot)",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 320 * scale, 224 * scale,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!win || !ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 224);
    SDL_GameController *gc = NULL;

    const double frame_s = 1.0 / 59.922;                     /* NTSC */
    Uint64 freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
    int running = 1, paused = 0, shots = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_CONTROLLERDEVICEADDED && !gc) gc = SDL_GameControllerOpen(e.cdevice.which);
            else if (e.type == SDL_CONTROLLERDEVICEREMOVED && gc) { SDL_GameControllerClose(gc); gc = NULL; }
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_p: paused = !paused; break;
                case SDLK_F5: md_power_on(); eeprom_load(save); break;
                case SDLK_F12: {
                    char path[64];
                    snprintf(path, sizeof path, "shot_%ld_%d.png", (long)time(NULL), shots++);
                    png_write_rgb(path, &frame_rgb[0][0], frame_width, ACTIVE_LINES, 320);
                    fprintf(stderr, "wrote %s\n", path);
                    break;
                }
                }
            }
        }
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        int fast = k[SDL_SCANCODE_TAB];
        if (!paused) {
            pad_buttons = keys_down(k) | pad_down(gc);
            md_run_frame();
        }
        /* the game switches between 256 and 320 pixels wide; show the active width */
        SDL_UpdateTexture(tex, NULL, &frame_rgb[0][0], 320 * sizeof(uint32_t));
        SDL_Rect src = {0, 0, frame_width, ACTIVE_LINES};
        SDL_RenderSetLogicalSize(ren, frame_width, ACTIVE_LINES);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, &src, NULL);
        SDL_RenderPresent(ren);

        next += (Uint64)(frame_s * freq);
        Uint64 now = SDL_GetPerformanceCounter();
        if (fast) next = now;
        else if (now < next) SDL_Delay((Uint32)((next - now) * 1000 / freq));
        else if (now - next > freq / 4) next = now;            /* fell far behind: do not race */
    }
    if (eeprom_save(save) == 0) fprintf(stderr, "saved save data to %s\n", save);
    SDL_Quit();
    return 0;
}
