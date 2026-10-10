/* Reference harness main loop.
 *
 *   harness ROM [--frames N] [--out DIR] [--png-every K] [--ram-every K] [--vdp-every K] [--input FILE]
 *               [--save-state-at F FILE] [--load-state FILE] [--pc-trace F] [--port replace|shadow] [--quiet]
 *
 * Runs N NTSC frames (262 lines of 488 68000 cycles). Every K frames it writes DIR/frame_NNNNN.png
 * and/or DIR/ram_NNNNN.bin (the 64 KB of work RAM) and/or DIR/vdp_NNNNN.bin (VRAM 64 KB, CRAM 128 bytes,
 * VSRAM 80 bytes, registers 32 bytes; words big-endian). The input file holds lines
 * "FRAME BUTTONS": from FRAME on, hold BUTTONS (any of U D L R A B C S, or - for none).
 * --save-state-at writes a save state after frame F. --load-state starts from a save state
 * instead of power-on; frame numbers (for --frames, the dumps and the input file) continue from
 * the frame it was saved at. --port runs the routines in port/ in place of the originals
 * (replace) or checks every call of them against the originals (shadow); see hybrid.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md.h"
#include "../third_party/musashi/m68k.h"

static int trace_frame = -1;                  /* --pc-trace: sample the PC through one frame */

/* scripted input */
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
        for (char *p = b; *p; p++) {
            const char *k = strchr("UDLRBCAS", *p);
            if (k) m |= 1 << (k - "UDLRBCAS");
        }
        if (script_n == cap) script = realloc(script, (cap = cap ? cap * 2 : 16) * sizeof *script);
        script[script_n].frame = fr; script[script_n].buttons = m; script_n++;
    }
    fclose(f);
}

static uint8_t buttons_at(int frame)
{
    uint8_t m = 0;
    for (int i = 0; i < script_n; i++) if (script[i].frame <= frame) m = script[i].buttons;
    return m;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *out = ".", *input = NULL, *save_path = NULL, *load_path = NULL;
    int save_at = -1, first = 0, port = PORT_OFF;
    int frames = 600, png_every = 0, ram_every = 0, vdp_every = 0, quiet = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--png-every") && i + 1 < argc) png_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ram-every") && i + 1 < argc) ram_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--vdp-every") && i + 1 < argc) vdp_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--pc-trace") && i + 1 < argc) trace_frame = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save-state-at") && i + 2 < argc) { save_at = atoi(argv[++i]); save_path = argv[++i]; }
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_path = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            i++; port = !strcmp(argv[i], "replace") ? PORT_REPLACE : !strcmp(argv[i], "shadow") ? PORT_SHADOW : PORT_OFF;
        }
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else rom_path = argv[i];
    }
    if (!rom_path) { fprintf(stderr, "usage: harness ROM [--frames N] [--out DIR] [--png-every K] [--ram-every K] [--input FILE]\n"); return 2; }
    if (md_load_rom(rom_path)) return 1;
    if (input) load_input(input);
    md_power_on();
    if (load_path && md_load_state(load_path, &first)) return 1;
    hybrid_init(port);

    char path[1024];
    for (int fr = first + 1; fr <= frames; fr++) {
        pad_buttons = buttons_at(fr); md_trace = fr == trace_frame;
        md_run_frame();
        if (fr == save_at && md_save_state(save_path, fr)) return 1;
        if (png_every && fr % png_every == 0) {
            snprintf(path, sizeof path, "%s/frame_%05d.png", out, fr);
            png_write_rgb(path, &frame_rgb[0][0], frame_width, ACTIVE_LINES, 320);
        }
        if (ram_every && fr % ram_every == 0) {
            snprintf(path, sizeof path, "%s/ram_%05d.bin", out, fr);
            FILE *r = fopen(path, "wb"); if (r) { fwrite(ram, 1, sizeof ram, r); fclose(r); }
        }
        if (vdp_every && fr % vdp_every == 0) {
            snprintf(path, sizeof path, "%s/vdp_%05d.bin", out, fr);
            FILE *v = fopen(path, "wb");
            if (v) {
                uint8_t w[2];
                fwrite(vram, 1, sizeof vram, v);
                for (int i = 0; i < 64; i++) { w[0] = cram[i] >> 8; w[1] = cram[i]; fwrite(w, 1, 2, v); }
                for (int i = 0; i < 40; i++) { w[0] = vsram[i] >> 8; w[1] = vsram[i]; fwrite(w, 1, 2, v); }
                fwrite(vdp_reg, 1, 32, v); fclose(v);
            }
        }
        if (!quiet && fr % 60 == 0)
            fprintf(stderr, "frame %d  pc %06X  frame counter %08X\n", fr, m68k_get_reg(NULL, M68K_REG_PC),
                    ram[0x8002] << 24 | ram[0x8003] << 16 | ram[0x8004] << 8 | ram[0x8005]);
    }
    hybrid_report();
    return 0;
}
