/* Comparing runs by game frame, for the harness (the original 68000 code) and the native build.
 *
 * The native build never drops frames and the original does, so the two drift apart in real
 * frames while playing the same game. This samples both at the same point of every game frame:
 * when the frame's logic is done and the game waits for VBlank (`$3DC`, in $3B8, just after the
 * game's frame counter `$FF8002` went up). Each sample is a line of the frame log:
 *     COUNTER  hash(work RAM)  hash(VRAM)  hash(CRAM, VSRAM)
 * and input can be keyed to the same counter (--input-game), so both builds see each button on
 * the same game frame. --dump-at N DIR writes RAM and VDP memory at game frame N, to find what a
 * differing hash means. The frame-wait address is this game's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md.h"

#define FRAME_WAIT 0x3DC
static FILE *flog;
static uint32_t last = 0xFFFFFFFF;
static long dump_at = -1; static const char *dump_dir;
static struct { uint32_t frame; uint8_t buttons; } *gin; static int gin_n;
static int gin_on;

static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h)
{
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* RAM that is expected to differ between the builds, and is left out of the hash:
 * the stack ($FF0000-$FF0BFF: where the VBlank interrupt pushed its frame depends on where the
 * 68000 happened to be), and the VBlank counter $FF8A49 (lag frames add to it). */
static uint32_t ram_hash(void)
{
    uint32_t h = fnv(ram + 0x0C00, 0x8A49 - 0x0C00, 2166136261u);
    return fnv(ram + 0x8A4A, 0x10000 - 0x8A4A, h);
}

int framelog_open(const char *path)
{
    flog = fopen(path, "w");
    if (!flog) { perror(path); return -1; }
    return 0;
}

void framelog_dump_at(long n, const char *dir) { dump_at = n; dump_dir = dir; }

int framelog_input(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    char line[256]; int cap = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned fr; char b[64] = "";
        if (line[0] == '#' || sscanf(line, "%u %63s", &fr, b) < 1) continue;
        uint8_t m = 0;
        for (char *p = b; *p; p++) { const char *k = strchr("UDLRBCAS", *p); if (k) m |= 1 << (k - "UDLRBCAS"); }
        if (gin_n == cap) gin = realloc(gin, (cap = cap ? cap * 2 : 16) * sizeof *gin);
        gin[gin_n].frame = fr; gin[gin_n].buttons = m; gin_n++;
    }
    fclose(f);
    gin_on = 1;
    return 0;
}

static void dump(const char *name, const void *p, size_t n)
{
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dump_dir, name);
    FILE *f = fopen(path, "wb"); if (f) { fwrite(p, 1, n, f); fclose(f); }
}

/* call with the PC about to run (harness) or at a poll point (native) */
void framelog_at(uint32_t pc)
{
    if (pc != FRAME_WAIT || (!flog && !gin_on && dump_at < 0)) return;
    uint32_t n = (uint32_t)ram[0x8002] << 24 | ram[0x8003] << 16 | ram[0x8004] << 8 | ram[0x8005];
    if (n == last) return;                          /* still waiting in the same frame */
    last = n;
    if (flog) {
        uint32_t hv = fnv(vram, sizeof vram, 2166136261u);
        uint32_t hc = fnv((const uint8_t *)cram, sizeof cram, fnv((const uint8_t *)vsram, sizeof vsram, 2166136261u));
        fprintf(flog, "%u %08X %08X %08X\n", n, ram_hash(), hv, hc);
    }
    if ((long)n == dump_at) {
        dump("ram.bin", ram, sizeof ram); dump("vram.bin", vram, sizeof vram); dump("cram.bin", cram, sizeof cram);
        dump("vdpreg.bin", vdp_reg, sizeof vdp_reg); dump("vsram.bin", vsram, sizeof vsram);
    }
    if (gin_on) {                                   /* the buttons for the next game frame */
        uint8_t m = 0;
        for (int i = 0; i < gin_n; i++) if (gin[i].frame <= n + 1) m = gin[i].buttons;
        pad_buttons = m;
    }
}

void framelog_close(void) { if (flog) fclose(flog); flog = NULL; }
int framelog_input_active(void) { return gin_on; }
