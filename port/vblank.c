/* The kernel's frame work ($4FEE-$5608): what runs once a frame after the vertical blank ($5262,
 * from the wait at $3B8), the joypads, the palette buffers, the screen reset and the demo input.
 * Batch T42 on; T44 adds the planes cleared, the layouts drawn and the kernel's two tasks
 * (the animated tiles, the Sega screen).
 *
 * Written as plain C: these promise the stack pointer and what they write (RAM, VRAM, the VDP's
 * registers). Registers are left as the original leaves them where that is cheap; flags only
 * where a caller branches on them. A register or flag the original's callers read and this code
 * leaves different shows up as a watched difference in the shadow runs. */
#include "world.h"
#include "task.h"

static Cpu *c;
static void call(uint32_t addr) { call_68k(c, addr); }

#define VDP_DATA      0xC00000u
#define VDP_CTRL      0xC00004u
#define Z80_BUSREQ    0xA11100u
#define FRAME_FLAGS   0xFFFF8006u   /* bit 1: VDP register $8A5C; bit 2: $8A74/$8A76; bit 3: scroll */
#define PAD           0xFFFF8A7Au   /* the buttons held this frame */
#define PAD_LAST      0xFFFF8A7Bu
#define PAD_PRESSED   0xFFFF8A7Cu
#define PAD_WHICH     0xFFFF8A7Du   /* bit 7: chosen; bit 0: pad 2 */
#define PALETTE       0xFFFF8B56u   /* 32 longs: the palette being built */
#define PALETTE_OUT   0xFFFF8BD6u   /* 32 longs: the palette uploaded */
#define PAL_FLAGS     0xFFFF8C56u   /* bit 7: upload the palette */
#define DEMO_COUNT    0xFFFF8A4Eu   /* byte: frames left of this input; $8A4F: the input */

static uint32_t sx16(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }
static int bclr8(uint32_t a, int bit) { uint8_t v = rd8(a); wr8(a, (uint8_t)(v & ~(1 << bit))); return v >> bit & 1; }

/* $5398: every entity's slot unused */
static void clear_entities(void)
{
    for (uint32_t e = 0; e < 0x100; e += 4) wr8(0xFFFFC000u + e, 0);
    c->d[0] = 0; c->a[4] = 0x100; c->d[1] = 0xFFFF;
}
/* $5124: seven longs at $9996 cleared */
static void clear_9996(void)
{
    for (int k = 0; k < 7; k++) wr32(0xFFFF9996u + 4 * k, 0);
    c->d[0] = set_w(0, 0xFFFF); c->a[0] = 0xFFFF9996u + 28;
}
/* $5132: the sound stopped ($364), then sound $25 */
static void sound_stop_25(void)
{
    call(0x364);
    c->d[0] = 0x25;
    call(0x366);
}
/* $513C: which pad plays. Chosen (bit 7): Z set if its Start ($8A81 or $8A84, bit 7) is up.
 * Not yet: the first pad whose Start is down is chosen ($80 or $81); Z set if neither. */
static void pad_which(void)
{
    uint8_t w = rd8(PAD_WHICH);
    c->d[0] = set_b(c->d[0], w);
    if (w & 0x80) {
        c->a[0] = (w & 0x7F) ? 0xFFFF8A84u : 0xFFFF8A81u;
        c->d[0] = set_b(c->d[0], w & 0x7F);
        c->ccr = (uint16_t)((c->ccr & CC_X) | ((w & 0x7F) ? 0 : CC_Z));   /* andi.b #$7f,d0 */
        c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (rd8(c->a[0]) & 0x80 ? 0 : CC_Z));
        return;
    }
    c->d[0] = 0xFFFFFF80u;
    if (rd8(0xFFFF8A81u) & 0x80) ;
    else if (rd8(0xFFFF8A84u) & 0x80) c->d[0] = 0xFFFFFF81u;
    else { c->ccr = (uint16_t)((c->ccr & CC_X) | CC_N | CC_Z); return; }   /* moveq #$80; btst: Z */
    wr8(PAD_WHICH, (uint8_t)c->d[0]);
    c->ccr = (uint16_t)((c->ccr & CC_X) | CC_N);
}
/* $5216: one pad (port a0, record a1): the last buttons kept, the buttons read in the two halves
 * the port gives (Start and A with TH low, the rest with TH high), and the newly pressed */
static void read_pad(void)
{
    uint32_t p = c->a[0], r = c->a[1];
    wr8(r + 1, rd8(r));
    wr8(p, 0);
    uint8_t lo = (uint8_t)~rd8(p);
    wr8(r, (uint8_t)(lo << 2 & 0xC0));
    wr8(p, 0x40);
    uint8_t hi = (uint8_t)(~rd8(p) & 0x3F);
    wr8(r, rd8(r) | hi);
    uint8_t now = rd8(r);
    c->d[1] = set_b(c->d[1], now);
    c->d[0] = set_b(c->d[0], (rd8(r + 1) ^ now) & now);
    wr8(r + 2, (uint8_t)c->d[0]);
}
/* $51C6: both pads read with the Z80 off the bus; the playing pad's buttons into PAD, unless the
 * input is a demo's ($8A50 bit 7) */
static void read_pads(void)
{
    wr16(Z80_BUSREQ, 0x100);
    while (rd8(Z80_BUSREQ) & 1) {}
    c->a[0] = 0xA10003; c->a[1] = 0xFFFF8A7Fu; read_pad();
    c->a[0] = 0xA10005; c->a[1] = 0xFFFF8A82u; read_pad();
    wr16(Z80_BUSREQ, 0);
    if (!(rd8(PAD_WHICH) & 1)) c->a[1] = 0xFFFF8A7Fu;
    if (rd8(0xFFFF8A50u) & 0x80) return;
    wr8(PAD_LAST, rd8(PAD));
    wr8(PAD, rd8(c->a[1]));
}
/* $5250: the buttons newly pressed */
static void presses(void)
{
    uint8_t now = rd8(PAD);
    c->d[1] = set_b(c->d[1], now);
    c->d[0] = set_b(c->d[0], (rd8(PAD_LAST) ^ now) & now);
    wr8(PAD_PRESSED, (uint8_t)c->d[0]);
}
/* $5314: d2+1 pairs of words to the VDP's data port, from a0 and a1 at d0 and d1, both wrapping
 * at d3. (The original tests d0 for both wraps; kept.) */
static void scroll_pairs(void)
{
    c->a[2] = VDP_DATA;
    do {
        wr16(VDP_DATA, rd16(c->a[0] + sx16(c->d[0])));
        wr16(VDP_DATA, rd16(c->a[1] + sx16(c->d[1])));
        c->d[0] = set_w(c->d[0], c->d[0] + 2);
        if ((uint16_t)c->d[0] >= (uint16_t)c->d[3]) c->d[0] = 0;
        c->d[1] = set_w(c->d[1], c->d[1] + 2);
        if ((uint16_t)c->d[0] >= (uint16_t)c->d[3]) c->d[1] = 0;
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
    } while ((uint16_t)c->d[2] != 0xFFFF);
}
/* $52DA: the horizontal scroll table: 256 pairs from $FF2400/$FF2600 by $8AE0/$8AE2 when the
 * mode ($8A5F & 3) scrolls by line, else one pair */
static void hscroll_upload(void)
{
    c->d[0] = set_w(c->d[0], rd16(0xFFFF8A6Au));
    call(0x5A0);
    c->d[0] = 0; c->d[1] = 0; c->d[2] = 0;
    c->d[3] = set_w(c->d[3], 0x200);
    c->a[0] = 0xFF2400; c->a[1] = 0xFF2600;
    c->d[4] = 3 & rd8(0xFFFF8A5Fu);
    if (c->d[4]) {
        c->d[2] = set_w(c->d[2], 0xFF);
        c->d[0] = set_w(c->d[0], rd16(0xFFFF8AE0u) & 0xFF);
        c->d[1] = set_w(c->d[1], rd16(0xFFFF8AE2u) & 0xFF);
        c->d[0] = set_w(c->d[0], c->d[0] * 2);
        c->d[1] = set_w(c->d[1], c->d[1] * 2);
    }
    scroll_pairs();
}
/* $529E: when asked ($8006 bit 3): the horizontal scroll, then the vertical (VSRAM): 20 pairs
 * from $8A8C/$8AB4 by $8ADC/$8ADE when the mode ($8A5F bit 2) scrolls by column, else one */
static void scroll_upload(void)
{
    if (!bclr8(FRAME_FLAGS, 3)) return;
    hscroll_upload();
    wr32(VDP_CTRL, 0x40000010);
    c->d[0] = 0; c->d[1] = 0; c->d[2] = 0; c->d[3] = 0x28;
    c->a[0] = 0xFFFF8A8Cu; c->a[1] = 0xFFFF8AB4u;
    if (rd8(0xFFFF8A5Fu) & 4) {
        c->d[0] = set_w(c->d[0], rd16(0xFFFF8ADCu));
        c->d[1] = set_w(c->d[1], rd16(0xFFFF8ADEu));
        c->d[2] = 0x13;
        c->d[0] = set_w(c->d[0], c->d[0] * 2);
        c->d[1] = set_w(c->d[1], c->d[1] * 2);
    }
    scroll_pairs();
}
/* $5338: when asked ($8006 bit 2): the VDP register words $8A74 and $8A76, and $8AE4 from the
 * second's low five bits (the screen's height: (n*8 + $60), $20 more for bit 7) */
static void vdp_regs(void)
{
    if (!bclr8(FRAME_FLAGS, 2)) return;
    wr16(VDP_CTRL, rd16(0xFFFF8A74u));
    wr16(0xFFFF8AE4u, 0);
    c->d[0] = set_w(c->d[0], rd16(0xFFFF8A76u));
    wr16(VDP_CTRL, (uint16_t)c->d[0]);
    c->d[1] = 0x1F & c->d[0];
    if (!c->d[1]) return;
    c->d[1] = (c->d[1] << 3) + 0x60;
    if (c->d[0] & 0x80) c->d[1] += 0x20;
    wr16(0xFFFF8AE4u, (uint16_t)c->d[1]);
}
/* $5370: when asked ($8C56 bit 7): the palette to CRAM */
static void palette_upload(void)
{
    if (!bclr8(PAL_FLAGS, 7)) return;
    wr32(VDP_CTRL, 0xC0000000u);
    for (int k = 0; k < 32; k++) wr32(VDP_DATA, rd32(PALETTE_OUT + 4 * k));
    c->a[0] = PALETTE_OUT + 128; c->a[1] = VDP_DATA; c->d[0] = set_w(c->d[0], 0xFFFF);
}
/* $557E: the sprite table ($FF2800, 80 sprites) to VRAM $D000 */
static void sprites_upload(void)
{
    c->a[1] = VDP_DATA;
    wr32(VDP_CTRL, 0x50000003);
    for (int k = 0; k < 160; k++) wr32(VDP_DATA, rd32(0xFF2800 + 4 * k));
    c->a[0] = 0xFF2800 + 640; c->d[0] = set_w(c->d[0], 0xFFFF);
}
/* $5262 (each frame, after the vertical blank): scroll, VDP registers, sprites, pads, presses, a
 * pause for slow-motion ($8A8B), the palette, and VDP register $8A5C when asked (kept in $8A7E) */
static void frame_work(void)
{
    scroll_upload();
    vdp_regs();
    sprites_upload();
    read_pads();
    presses();
    if (rd8(0xFFFF8A8Bu)) c->d[0] = set_w(c->d[0], 0xFFFF);   /* a busy loop of 1000 */
    palette_upload();
    if (!bclr8(FRAME_FLAGS, 1)) return;
    c->d[0] = set_w(c->d[0], rd16(0xFFFF8A5Cu));
    wr16(VDP_CTRL, (uint16_t)c->d[0]);
    wr8(0xFFFF8A7Eu, (uint8_t)c->d[0]);
}
/* $559E: both palettes black, uploaded */
static void palette_clear(void)
{
    c->d[1] = 0;
    for (int k = 0; k < 32; k++) { wr32(PALETTE + 4 * k, 0); wr32(PALETTE_OUT + 4 * k, 0); }
    wr8(PAL_FLAGS, rd8(PAL_FLAGS) | 0x80);
    c->a[0] = PALETTE + 128; c->a[1] = PALETTE_OUT + 128; c->d[0] = 0xFFFF;
}
/* $55BA: the built palette uploaded as it is ($8C56: upload, nothing fading) */
static void palette_copy(void)
{
    for (int k = 0; k < 32; k++) wr32(PALETTE_OUT + 4 * k, rd32(PALETTE + 4 * k));
    wr8(PAL_FLAGS, (uint8_t)((rd8(PAL_FLAGS) & 0x3F) | 0x81));
    c->a[0] = PALETTE + 128; c->a[1] = PALETTE_OUT + 128; c->d[0] = set_w(c->d[0], 0xFFFF);
}
/* $55DA: the fades reset: $8C56, $8C58, $8C68 and $8C57 cleared */
static void fade_reset(void)
{
    c->d[0] = 0;
    wr8(PAL_FLAGS, 0);
    wr16(0xFFFF8C58u, 0);
    wr16(0xFFFF8C68u, 0);
    wr8(0xFFFF8C57u, 0);
    c->a[0] = 0xFFFF8C68u + 6; c->d[1] = set_w(0, 0xFFFF);
}
/* $5170: the VDP set up ($6F6), VRAM, VSRAM and the scroll cleared ($6910-$696C), the palettes
 * black */
static void screen_reset(void)
{
    call(0x6F6);
    call(0x6910);
    call(0x6916);
    call(0x693E);
    call(0x696C);
    palette_clear();
}
/* $5188: a new screen: the display off ($6BE), reset, the four $8B34 words $80, no entities, the
 * tasks $6EA8 and $5608 started ($4A2), the base graphics ($51C0) */
static void screen_new(void)
{
    call(0x6BE);
    screen_reset();
    c->d[0] = 0xFFFFFF80u;
    for (int k = 0; k < 4; k++) wr16(0xFFFF8B34u + 2 * k, 0xFF80);
    clear_entities();
    c->a[0] = 0x6EA8; call(0x4A2);
    c->a[0] = 0x5608; call(0x4A2);
    c->a[0] = 0x51C0; call(0x234C);
}
/* $5106: tasks from the list at a1 (ended by 0) into the slots from $80C8: each in use, its
 * name (the descriptor's first long) and its code after it */
static void tasks_start(void)
{
    c->a[0] = 0xFFFF80C8u;
    for (;;) {
        c->d[0] = rd32(c->a[1]); c->a[1] += 4;
        if (!c->d[0]) { c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); return; }
        wr16(c->a[0], 0x8000);
        c->a[2] = c->d[0];
        wr32(c->a[0] + 4, rd32(c->a[2])); c->a[2] += 4;
        wr32(c->a[0] + 12, c->a[2]);
        c->a[0] += 0x80;
    }
}
/* $501C: a demo's input begins: its first count from a0, the rest kept in the task ($44(a5)) */
static void demo_start(void)
{
    wr16(DEMO_COUNT, rd16(c->a[0])); c->a[0] += 2;
    wr32(c->a[5] + 0x44, c->a[0]);
    wr32(0xFFFF8A52u, 0);
    wr8(PAD, 0); wr8(PAD_LAST, 0); wr8(PAD_PRESSED, 0);
}
/* $505C: a demo's input for this frame ($8A4F), the next (count, input) when the count runs out */
static void demo_step(void)
{
    wr8(PAD_LAST, rd8(PAD));
    wr8(PAD, rd8(0xFFFF8A4Fu));
    uint8_t n = (uint8_t)(rd8(DEMO_COUNT) - 1);
    wr8(DEMO_COUNT, n);
    if (n) return;
    c->a[0] = rd32(c->a[5] + 0x44);
    wr16(DEMO_COUNT, rd16(c->a[0])); c->a[0] += 2;
    wr32(c->a[5] + 0x44, c->a[0]);
}
/* $4FEE (unreferenced): a demo's recording begins at $FF1400 (4K cleared), no input */
static void record_start(void)
{
    c->a[0] = 0xFF1400;
    wr32(c->a[5] + 0x44, c->a[0]);
    for (int k = 0; k < 0x400; k++) wr32(0xFF1400 + 4 * k, 0);
    c->a[0] = 0xFF2400; c->d[0] = 0; c->d[1] = set_w(c->d[1], 0xFFFF);
    wr8(PAD, 0); wr8(PAD_LAST, 0); wr8(PAD_PRESSED, 0);
    wr32(0xFFFF8A52u, 0);
}
/* $5036 (unreferenced): a frame of a demo's recording: (count, input) pairs, a new pair when the
 * input changes or the count reaches $FF */
static void record_step(void)
{
    c->a[0] = rd32(c->a[5] + 0x44);
    uint8_t in = rd8(PAD);
    c->d[0] = set_b(c->d[0], in);
    if (rd8(c->a[0])) {
        if (rd8(c->a[0] + 1) == in && rd8(c->a[0]) != 0xFF) { wr8(c->a[0], (uint8_t)(rd8(c->a[0]) + 1)); return; }
        c->a[0] += 2;
        wr32(c->a[5] + 0x44, c->a[0]);
    }
    wr8(c->a[0] + 1, in);
    wr8(c->a[0], (uint8_t)(rd8(c->a[0]) + 1));
}

#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(RecordStart, record_start)           /* $4FEE */
ENTRY(DemoStart, demo_start)               /* $501C */
ENTRY(RecordStep, record_step)             /* $5036 */
ENTRY(DemoStep, demo_step)                 /* $505C */
ENTRY(TasksStart, tasks_start)             /* $5106 */
ENTRY(Clear9996, clear_9996)               /* $5124 */
ENTRY(SoundStop25, sound_stop_25)          /* $5132 */
ENTRY(PadWhich, pad_which)                 /* $513C */
ENTRY(ScreenReset, screen_reset)           /* $5170 */
ENTRY(ScreenNew, screen_new)               /* $5188 */
ENTRY(ReadPads, read_pads)                 /* $51C6 */
ENTRY(ReadPad, read_pad)                   /* $5216 */
ENTRY(Presses, presses)                    /* $5250 */
ENTRY(FrameWork, frame_work)               /* $5262 */
ENTRY(ScrollUpload, scroll_upload)         /* $529E */
ENTRY(HScrollUpload, hscroll_upload)       /* $52DA */
ENTRY(VdpRegs, vdp_regs)                   /* $5338 */
ENTRY(PaletteUpload, palette_upload)       /* $5370 */
ENTRY(ClearEntities, clear_entities)       /* $5398 */
ENTRY(SpritesUpload, sprites_upload)       /* $557E */
ENTRY(PaletteClear, palette_clear)         /* $559E */
ENTRY(PaletteCopy, palette_copy)           /* $55BA */
ENTRY(FadeReset, fade_reset)               /* $55DA */

/* ---- batch T43: the colour task ($5608, "Kcol") ----
 * Each frame (unless held, $8C56 bit 2) it runs the colour script ($8C58: one slot), a fade
 * ($8C56 bit 5: in, bit 3 out; $8C70 the level, $8C74 the step every $8C6F frames), copies the
 * built palette for upload when anything changed, and runs the colour cycle ($8C68: one slot).
 * A colour is $0BGR, three bits a component; $594A adds a level to each component, clamped. */

#define FADE_LEVEL    0xFFFF8C70u   /* bytes: $8C70 the fade; $8C71-$8C73 red, green, blue */

static uint32_t sx8(uint32_t b) { return (uint32_t)(int32_t)(int8_t)b; }

/* $594A: one colour from a0 to a1, each component plus d1 (red), d2 (green, <<4), d3 (blue, <<8),
 * clamped to 0-$E; a negative green or blue is left out */
static void color_adjust(void)
{
    uint16_t col = rd16(c->a[0]); c->a[0] += 2;
    c->d[4] = set_w(c->d[4], col);
    int16_t r = (int16_t)((col & 0xE) + (uint16_t)c->d[1]);
    uint16_t out = r < 0 ? 0 : (uint16_t)r >= 0xE ? 0xE : (uint16_t)r;
    int16_t g = (int16_t)((col & 0xE0) + (uint16_t)c->d[2]);
    if (g >= 0) out |= (uint16_t)g >= 0xE0 ? 0xE0 : (uint16_t)g;
    int16_t b = (int16_t)((col & 0xE00) + (uint16_t)c->d[3]);
    if (b >= 0) out |= (uint16_t)b >= 0xE00 ? 0xE00 : (uint16_t)b;
    c->d[7] = 0xE00;
    c->d[5] = set_w(c->d[5], out);
    wr16(c->a[1], out); c->a[1] += 2;
}
/* $5916: the whole palette faded by $8C71-$8C73 into the upload copy */
static void fade_apply(void)
{
    c->a[0] = PALETTE; c->a[1] = PALETTE_OUT;
    c->d[1] = set_w(c->d[1], sx8(rd8(FADE_LEVEL + 1)));
    c->d[2] = set_w(c->d[2], sx8(rd8(FADE_LEVEL + 2)) << 4);
    c->d[3] = set_w(c->d[3], sx8(rd8(FADE_LEVEL + 3)) << 8);
    for (int k = 0; k < 64; k++) color_adjust();
    c->d[0] = 0xFFFF;
    wr8(PAL_FLAGS, (uint8_t)((rd8(PAL_FLAGS) & 0x3F) | 0x81));
}
static void fade_level_all(uint8_t v) { for (int k = 0; k < 4; k++) wr8(FADE_LEVEL + k, v); }
/* $5892: a fade in's step, every $8C6F frames (the sound driver nudged, $618, unless $8A5D bit
 * 6): the level up by the step; at 0 the fade ends ($8C56: done, bits 1, 4, 6) */
static void fade_in_step(void)
{
    uint8_t n = (uint8_t)(rd8(0xFFFF8C6Eu) - 1);
    wr8(0xFFFF8C6Eu, n);
    if (n) return;
    if (!(rd8(0xFFFF8A5Du) & 0x40)) call(0x618);
    wr8(0xFFFF8C6Eu, rd8(0xFFFF8C6Fu));
    uint8_t v = (uint8_t)(rd8(FADE_LEVEL) + rd8(0xFFFF8C74u));
    c->d[0] = set_b(c->d[0], v);
    if (v & 0x80) { fade_level_all(v); return; }
    wr8(PAL_FLAGS, (uint8_t)((rd8(PAL_FLAGS) & 0xC7) | 0x52));
}
/* $58D6: a fade out's step: the level down by the step; below -14 the fade ends (bit 4) and the
 * display is blanked ($62A, which waits for the frame in place: the original runs it). Returns 1
 * if it went on there */
static int fade_out_step(void)
{
    uint8_t n = (uint8_t)(rd8(0xFFFF8C6Eu) - 1);
    wr8(0xFFFF8C6Eu, n);
    if (n) return 0;
    wr8(0xFFFF8C6Eu, rd8(0xFFFF8C6Fu));
    uint8_t v = (uint8_t)(rd8(FADE_LEVEL) - rd8(0xFFFF8C74u));
    c->d[0] = set_b(c->d[0], v);
    if ((int8_t)v >= -14) { fade_level_all(v); return 0; }
    wr8(PAL_FLAGS, (uint8_t)((rd8(PAL_FLAGS) & 0xC7) | 0x10));
    port_continue_at(c, 0x62A, 0x5902);             /* jsr $62A: the display blanked, then the frame waited for in place */
    return 1;
}
/* $587E: unless the colour task is running ($47A finds it: d0), the fades reset */
static void fade_owner(void)
{
    c->a[1] = 0x5608;
    call(0x47A);
    if (!(c->d[0] & 0xFF)) fade_reset();
}
/* $581A: a fade in from black: level -16, a step of 2 a frame */
static void fade_in(void)
{
    fade_owner();
    wr8(PAL_FLAGS, (uint8_t)((rd8(PAL_FLAGS) & 0xC7) | 0x20));
    wr8(FADE_LEVEL, 0xF0); wr8(0xFFFF8C6Eu, 1); wr8(0xFFFF8C6Fu, 1); wr8(0xFFFF8C74u, 2);
}
/* $5842: a fade out to black: level 0, a step of 2 a frame */
static void fade_out(void)
{
    fade_owner();
    wr8(PAL_FLAGS, (uint8_t)((rd8(PAL_FLAGS) & 0xC7) | 0x28));
    wr8(FADE_LEVEL, 0); wr8(0xFFFF8C6Eu, 1); wr8(0xFFFF8C6Fu, 1); wr8(0xFFFF8C74u, 2);
}
/* $5868 (unreferenced): a slow fade out: a step of 1 every 2 frames */
static void fade_out_slow(void)
{
    fade_out();
    wr8(0xFFFF8C6Eu, 2); wr8(0xFFFF8C6Fu, 2); wr8(0xFFFF8C74u, 1);
}
/* $5986: the upload copy all white, uploaded and held */
static void palette_white(void)
{
    c->d[0] = 0xFFFFFFFFu;
    for (int k = 0; k < 32; k++) wr32(PALETTE_OUT + 4 * k, 0xFFFFFFFFu);
    c->a[0] = PALETTE_OUT + 128; c->d[1] = 0xFFFF;
    wr8(PAL_FLAGS, rd8(PAL_FLAGS) | 0x84);
}
/* $5766: a colour cycle from the 4 bytes at a2 (its kind, step, speed, colour), if its slot is
 * free */
static void cycle_start(void)
{
    c->a[3] = 0xFFFF8C68u;
    c->d[2] = 0;
    if (rd8(c->a[3]) & 0x80) { c->d[2] = 0xFFFF; return; }
    wr8(0xFFFF8C57u, (uint8_t)(rd8(0xFFFF8C57u) + 1));
    wr8(c->a[3] + 2, 1);
    wr8(c->a[3] + 3, 0);
    wr8(c->a[3], rd8(c->a[2])); c->a[2]++;
    wr8(c->a[3] + 4, rd8(c->a[2])); c->a[2]++;
    wr8(c->a[3] + 1, rd8(c->a[2])); c->a[2]++;
    wr8(c->a[3] + 5, rd8(c->a[2])); c->a[2]++;
    wr8(c->a[3], rd8(c->a[3]) | 0xA0);
}
/* $579A: the colour cycle's frame: every (speed) frames its level swings between 0 and -14 by
 * its step; its colour gets the level on the components its kind does not leave out */
static void cycles_run(void)
{
    c->a[2] = 0xFFFF8C68u;
    c->d[0] = 0;
    uint8_t kind = rd8(c->a[2]);
    c->d[4] = set_b(c->d[4], kind);
    if (kind & 0x80) {
        uint8_t lv = rd8(c->a[2] + 3);
        uint8_t n = (uint8_t)(rd8(c->a[2] + 2) - 1);
        wr8(c->a[2] + 2, n);
        if (!n) {
            wr8(c->a[2] + 2, rd8(c->a[2] + 1));
            for (;;) {
                if (!(rd8(c->a[2]) & 0x20)) { lv = (uint8_t)(lv + rd8(c->a[2] + 4)); if (lv & 0x80) break; }
                else { lv = (uint8_t)(lv - rd8(c->a[2] + 4)); if ((int8_t)lv >= -14) break; }
                wr8(c->a[2], rd8(c->a[2]) ^ 0x20);
            }
        }
        c->d[5] = set_b(c->d[5], lv);
        wr8(c->a[2] + 3, lv);
        c->d[1] = (kind & 1) ? 0 : set_w(0, sx8(lv));
        c->d[2] = (kind & 2) ? 0 : set_w(0, sx8(lv) << 4);
        c->d[3] = (kind & 4) ? 0 : set_w(0, sx8(lv) << 8);
        c->d[4] = (uint32_t)rd8(c->a[2] + 5) * 2;
        c->a[0] = PALETTE_OUT + c->d[4];
        c->a[1] = c->a[0];
        color_adjust();
    }
    c->a[2] += 6;
    c->d[0] = 0xFFFF;
}
/* $56BE: the colour script's frame. When its wait runs out, its commands run: a byte with bit 7
 * ends it; 0 then n is command n (from $5756); else a wait, then a count of (offset, colour)
 * words written into the built palette (marked changed, $8C56 bit 6) */
static void color_scripts(void)
{
    c->d[0] = 0;
    c->a[0] = 0xFFFF8C58u;
    if (!(rd8(c->a[0]) & 0x80)) goto next;
    {
        uint8_t n = (uint8_t)(rd8(c->a[0] + 1) - 1);
        wr8(c->a[0] + 1, n);
        if (n) goto next;
    }
    c->a[1] = rd32(c->a[0] + 2);
    c->d[1] = set_w(c->d[1], rd16(c->a[0] + 6));
    for (;;) {
        uint8_t op = rd8(c->a[1] + sx16(c->d[1]));
        c->d[2] = set_b(c->d[2], op);
        if (op & 0x80) { wr16(c->a[0], 0); goto next; }
        if (op) break;
        c->d[2] = (uint32_t)rd8(c->a[1] + sx16(c->d[1]) + 1) << 2;
        c->d[1] = set_w(c->d[1], c->d[1] + 2);
        c->a[2] = rd32(0x5756 + c->d[2]);
        switch (c->a[2]) {
        case 0x5734:                                /* the loop's start here, its count */
            c->a[1] += sx16(c->d[1]);
            wr16(c->a[0] + 8, rd16(c->a[1])); c->a[1] += 2;
            c->d[1] = 0;
            break;
        case 0x573E: {                              /* the loop's end: back to its start until done */
            uint8_t k = (uint8_t)(rd8(c->a[0] + 8) - 1);
            wr8(c->a[0] + 8, k);
            if (!k) c->d[1] = 0;
            break;
        }
        case 0x5746:                                /* a jump by the word here */
            c->a[1] += sx16(c->d[1]);
            c->a[1] += sx16(rd16(c->a[1]));
            c->d[1] = 0;
            break;
        case 0x574E:                                /* a colour cycle from the 4 bytes here */
            c->a[2] = c->a[1] + sx16(c->d[1]);
            c->d[1] = set_w(c->d[1], c->d[1] + 4);
            cycle_start();
            break;
        default: call(c->a[2]); break;              /* past the table: as the original jumps */
        }
    }
    wr8(c->a[0] + 1, (uint8_t)c->d[2]);
    c->d[2] = set_b(c->d[2], rd8(c->a[1] + sx16(c->d[1]) + 1));
    c->d[1] = set_w(c->d[1], c->d[1] + 2);
    c->a[2] = PALETTE;
    do {
        c->d[3] = set_w(c->d[3], rd16(c->a[1] + sx16(c->d[1])));
        c->d[1] = set_w(c->d[1], c->d[1] + 2);
        wr16(c->a[2] + sx16(c->d[3]), rd16(c->a[1] + sx16(c->d[1])));
        c->d[1] = set_w(c->d[1], c->d[1] + 2);
        c->d[2] = set_b(c->d[2], c->d[2] - 1);
    } while (c->d[2] & 0xFF);
    wr32(c->a[0] + 2, c->a[1]);
    wr16(c->a[0] + 6, (uint16_t)c->d[1]);
    wr8(PAL_FLAGS, rd8(PAL_FLAGS) | 0x40);
next:
    c->a[0] += 16;
    c->d[0] = 0xFFFF;
}
/* $5658: colour script d0 (the table at $643C) started, the colour task told to free it ($569C)
 * when it ends; with its slot busy, the game stops ($4CB4, 3). 1: it left elsewhere */
static int color_script_start(void)
{
    c->a[2] = 0xFFFF8C58u;
    c->d[1] = 0;
    if (rd8(c->a[2]) & 0x80) {
        c->a[2] += 16; c->d[1] = 0xFFFF;
        c->d[0] = 3;
        port_continue_at(c, 0x4CB4, 0);
        return 1;
    }
    wr32(c->a[2] + 2, rd32(0x643C + sx16(c->d[0])));
    c->a[1] = 0x5608;
    call(0x47A);
    wr32(c->a[0] + 8, 0x569C);
    wr16(c->a[2] + 6, 0);
    wr16(c->a[2], 0x8001);
    return 0;
}
/* $569C (the colour task's end): its script and cycle slots freed */
static void color_end(void)
{
    wr8(0xFFFF8C58u, 0);
    wr8(0xFFFF8C68u, 0);
    c->a[0] = 0xFFFF8C68u + 6; c->d[0] = 0xFFFF;
}
/* $560C (the colour task, each frame) */
static void colour_frame(void)
{
    if (rd8(PAL_FLAGS) & 4) return;
    color_scripts();
    if (rd8(PAL_FLAGS) & 0x20) {
        if (rd8(PAL_FLAGS) & 8) fade_out_step(); else fade_in_step();
        if (!bclr8(PAL_FLAGS, 1)) fade_apply();
    }
    if (rd8(0xFFFF8C57u) || (rd8(PAL_FLAGS) & 0x41) != 1) palette_copy();
    cycles_run();
}

/* the fade out ends this frame: its end blanks the display and waits for the frame in place ($62A
 * to $3B8), which C cannot do; the original runs that frame */
static int fade_out_ends(void)
{
    return rd8(0xFFFF8C6Eu) == 1 && (int8_t)(rd8(FADE_LEVEL) - rd8(0xFFFF8C74u)) < -14;
}
void ColourFrame(Cpu *cpu)                 /* $560C */
{
    c = cpu;
    if ((rd8(PAL_FLAGS) & 0x2C) == 0x28 && fade_out_ends()) { port_leave_to_68k(c, 0x560C); return; }
    colour_frame(); c->ret = 0;
}
void FadeOutStep(Cpu *cpu)                 /* $58D6 */
{
    c = cpu;
    if (!fade_out_step()) c->ret = 0;
}
void ColorScriptStart(Cpu *cpu) { c = cpu; if (!color_script_start()) c->ret = 0; }   /* $5658 */
ENTRY(ColorEnd, color_end)                 /* $569C */
ENTRY(ColorScripts, color_scripts)         /* $56BE */
ENTRY(CycleStart, cycle_start)             /* $5766 */
ENTRY(CyclesRun, cycles_run)               /* $579A */
ENTRY(FadeIn, fade_in)                     /* $581A */
ENTRY(FadeOut, fade_out)                   /* $5842 */
ENTRY(FadeOutSlow, fade_out_slow)          /* $5868 */
ENTRY(FadeOwner, fade_owner)               /* $587E */
ENTRY(FadeInStep, fade_in_step)            /* $5892 */
ENTRY(FadeApply, fade_apply)               /* $5916 */
ENTRY(ColorAdjust, color_adjust)           /* $594A */
ENTRY(PaletteWhite, palette_white)         /* $5986 */

/* ---- batch T44: clearing the planes, drawing a layout, the animated tiles, the Sega screen ---- */

#define PLANE_W       0xFFFF8A70u   /* word: cells across a plane */
#define PLANE_H       0xFFFF8A72u   /* word: cells down */
#define LAYOUT_PLANE  0xFFFF8C76u   /* bit 7 the window; else bit 0 plane B; bit 1 by the scroll */
#define TILE_ANIMS    0xFFFF950Cu   /* 8 slots of 16 bytes: the animated tiles */

/* $6910 (plane A, $8A62) and $6916 (plane B, $8A64): the plane cleared */
static void plane_clear_at(uint32_t base)
{
    c->d[0] = set_w(c->d[0], rd16(base));
    call(0x5A0);
    c->d[0] = 0;
    c->a[0] = VDP_DATA;
    c->d[2] = set_w(c->d[2], rd16(PLANE_H));
    c->d[3] = set_w(c->d[3], rd16(PLANE_W) >> 1);
    do {
        c->d[1] = set_w(c->d[1], c->d[3]);
        do { wr32(VDP_DATA, 0); c->d[1] = set_w(c->d[1], c->d[1] - 1); } while (c->d[1] & 0xFFFF);
        c->d[2] = set_w(c->d[2], c->d[2] - 1);
    } while (c->d[2] & 0xFFFF);
}
static void plane_a_clear(void) { plane_clear_at(0xFFFF8A62u); }
static void plane_b_clear(void) { plane_clear_at(0xFFFF8A64u); }
/* $693E: the window plane ($8A66, 2K) cleared, the window registers off, $8AE4 0, asked for */
static void window_clear(void)
{
    c->d[0] = set_w(c->d[0], rd16(0xFFFF8A66u));
    call(0x5A0);
    c->d[0] = 0;
    c->a[0] = VDP_DATA;
    for (int k = 0; k < 0x200; k++) wr32(VDP_DATA, 0);
    c->d[1] = set_w(c->d[1], 0xFFFF);
    wr8(0xFFFF8A75u, 0); wr8(0xFFFF8A77u, 0); wr16(0xFFFF8AE4u, 0);
    wr8(FRAME_FLAGS, rd8(FRAME_FLAGS) | 4);
}
/* $699E: d1+1 longs of d0 at a0 and at a1; the scroll asked for */
static void scroll_clear_pair(void)
{
    do {
        wr32(c->a[0], c->d[0]); c->a[0] += 4;
        wr32(c->a[1], c->d[0]); c->a[1] += 4;
        c->d[1] = set_w(c->d[1], c->d[1] - 1);
    } while ((c->d[1] & 0xFFFF) != 0xFFFF);
    wr8(FRAME_FLAGS, rd8(FRAME_FLAGS) | 8);
}
/* $696C: the scroll's positions and tables cleared */
static void scroll_clear(void)
{
    c->d[0] = 0;
    wr16(0xFFFF8ADCu, 0); wr16(0xFFFF8ADEu, 0); wr16(0xFFFF8AE0u, 0); wr16(0xFFFF8AE2u, 0);
    c->a[0] = 0xFF2400; c->a[1] = 0xFF2600;
    c->d[1] = set_w(c->d[1], 0x7F);
    scroll_clear_pair();
    c->a[0] = 0xFFFF8A8Cu; c->a[1] = 0xFFFF8AB4u;
    c->d[1] = 9;
    scroll_clear_pair();
}
/* $6BAE: the animated tiles' slots freed (the Mxcg task's end) */
static void tile_anims_clear(void)
{
    c->a[0] = TILE_ANIMS; c->d[0] = 7; c->d[1] = 0;
    for (int k = 0; k < 8; k++) { wr16(c->a[0], 0); c->a[0] += 16; }
    c->d[0] = 0xFFFF;
}
/* $6BA0: $8E08 (64 longs) all -1, the animated tiles' slots freed */
static void gfx_tables_reset(void)
{
    c->a[0] = 0xFFFF8E08u; c->d[1] = 0xFFFFFFFFu;
    for (int k = 0; k < 64; k++) { wr32(c->a[0], 0xFFFFFFFFu); c->a[0] += 4; }
    tile_anims_clear();
}

/* $69AE: a layout of tiles from a2 drawn on the plane $8C76 says. Bytes above $C are tiles (the
 * high byte set by command 2); 0-$C are commands (the table at $69D8): 0 the end, 1 a position,
 * 2 the attributes, 3 a tile repeated, 4 a run of tiles, 5 several runs, 6 pairs, 7 one tile, 9
 * the line's length, 10 blanks, 12 another plane; 8 and 11 do nothing. Its state is in a stack
 * frame (link a6) as the original's, read uninitialised as it is when a layout sets no length. */
#define PIC_X         (c->a[6] - 2)     /* words: the cursor, the line's start */
#define PIC_Y         (c->a[6] - 4)
#define PIC_X0        (c->a[6] - 6)
#define PIC_Y0        (c->a[6] - 8)
#define PIC_LEN       (c->a[6] - 0xA)   /* bytes: the line's length, the tiles left on it */
#define PIC_LEFT      (c->a[6] - 0xC)
#define PIC_BASE      (c->a[6] - 0xE)   /* words: the plane's VRAM, the scroll's offsets */
#define PIC_XOFF      (c->a[6] - 0x10)
#define PIC_YOFF      (c->a[6] - 0x12)

/* $69F2: the plane's VRAM and (bit 1) the offsets that put the layout where the plane shows */
static void pic_plane(void)
{
    c->d[0] = 0;
    wr16(PIC_XOFF, 0); wr16(PIC_YOFF, 0);
    uint8_t p = rd8(LAYOUT_PLANE);
    if (p & 0x80) { wr16(PIC_BASE, rd16(0xFFFF8A66u)); return; }
    int b = p & 1;
    c->d[0] = set_w(c->d[0], rd16(b ? 0xFFFF8A64u : 0xFFFF8A62u));
    c->d[1] = set_w(c->d[1], rd16(b ? 0xFF2600 : 0xFF2400));
    c->d[2] = set_w(c->d[2], rd16(b ? 0xFFFF8AB4u : 0xFFFF8A8Cu));
    wr16(PIC_BASE, (uint16_t)c->d[0]);
    if (!(p & 2)) return;
    c->d[1] = set_w(c->d[1], (uint16_t)(((uint16_t)(-c->d[1] + 4) & rd16(0xFFFF8A88u)) >> 3));
    c->d[2] = set_w(c->d[2], (uint16_t)(((uint16_t)(c->d[2] + 4) & rd16(0xFFFF8A86u)) >> 3));
    wr16(PIC_XOFF, (uint16_t)c->d[1]);
    wr16(PIC_YOFF, (uint16_t)c->d[2]);
}
/* $5CA: the offset of cell (d0, d1) in a plane ($8A8A: the shift for a row) */
static void pic_cell(void)
{
    c->d[0] = set_w(c->d[0], c->d[0] * 2);
    c->d[1] = set_w(c->d[1], c->d[1] * 2);
    c->d[2] = set_b(c->d[2], rd8(0xFFFF8A8Au));
    c->d[1] = set_w(c->d[1], (c->d[2] & 63) >= 16 ? 0 : (uint32_t)(uint16_t)c->d[1] << (c->d[2] & 63));
    c->d[0] = set_w(c->d[0], c->d[0] + c->d[1]);
}
static uint16_t count_w(uint32_t a) { uint16_t n = (uint16_t)(rd16(a) - 1); wr16(a, n); return n; }
/* $6A9C: the cell (d0, d1)'s VRAM, then the VDP set to write there ($5A0) */
static void pic_seek(void)
{
    pic_cell();
    c->d[0] = set_w(c->d[0], c->d[0] + rd16(PIC_BASE));
    call(0x5A0);
    c->a[1] = VDP_DATA;
}
/* $6B64: tile d7 put; at the line's end the next line from its start, else the next cell (the
 * plane wrapping) */
static void pic_put(void)
{
    wr16(c->a[1], (uint16_t)c->d[7]);                /* move.w d7,(a1): the VDP once a position is set */
    uint8_t left = (uint8_t)(rd8(PIC_LEFT) - 1);
    wr8(PIC_LEFT, left);
    if (left & 0x80) {
        wr8(PIC_LEFT, rd8(PIC_LEN));
        c->d[0] = set_w(c->d[0], rd16(PIC_X0));
        c->d[1] = set_w(c->d[1], rd16(PIC_Y) + 1);
        if ((uint16_t)c->d[1] >= rd16(PLANE_H)) c->d[1] = 0;
        wr16(PIC_Y, (uint16_t)c->d[1]);
        wr16(PIC_X, (uint16_t)c->d[0]);
        pic_seek();
        return;
    }
    c->d[0] = set_w(c->d[0], rd16(PIC_X) + 1);
    if ((uint16_t)c->d[0] < rd16(PLANE_W)) { wr16(PIC_X, (uint16_t)c->d[0]); return; }
    c->d[0] = 0;
    wr16(PIC_X, 0);
    c->d[1] = set_w(c->d[1], rd16(PIC_Y));
    pic_seek();
}
static uint8_t pic_byte(void) { return rd8(c->a[2]++); }
/* $6AE0: d3+1 tiles from d7 up */
static void pic_run(void)
{
    do {
        pic_put();
        c->d[7] = set_b(c->d[7], c->d[7] + 1);
        c->d[3] = set_w(c->d[3], c->d[3] - 1);
    } while (!(c->d[3] & 0x8000));
}
static void pic_command(uint8_t n)
{
    switch (n) {
    case 1:                                         /* $6A5E: the cursor at (x, y) past the offsets */
        c->d[0] = pic_byte();
        c->d[0] = set_w(c->d[0], (c->d[0] + rd16(PIC_XOFF)) & (uint16_t)(rd16(PLANE_W) - 1));
        wr16(PIC_X0, (uint16_t)c->d[0]); wr16(PIC_X, (uint16_t)c->d[0]);
        c->d[1] = pic_byte();
        c->d[1] = set_w(c->d[1], (c->d[1] + rd16(PIC_YOFF)) & (uint16_t)(rd16(PLANE_H) - 1));
        c->d[2] = set_w(c->d[2], rd16(PLANE_H) - 1);
        wr16(PIC_Y0, (uint16_t)c->d[1]); wr16(PIC_Y, (uint16_t)c->d[1]);
        pic_seek();
        break;
    case 2:                                         /* $6AA6: the tiles' high byte */
        c->d[7] = set_w(c->d[7], (uint32_t)pic_byte() << 8);
        break;
    case 3:                                         /* $6AAC: a tile n+1 times */
        c->d[3] = pic_byte();
        c->d[7] = set_b(c->d[7], pic_byte());
        do { pic_put(); c->d[3] = set_w(c->d[3], c->d[3] - 1); } while (!(c->d[3] & 0x8000));
        break;
    case 4:                                         /* $6ABC: a run of n+1 tiles */
        c->d[3] = pic_byte();
        c->d[7] = set_b(c->d[7], pic_byte());
        pic_run();
        break;
    case 5:                                         /* $6AC8: the same run n+1 times */
        c->d[4] = pic_byte();
        do {
            c->d[3] = rd8(c->a[2]);
            c->d[7] = set_b(c->d[7], rd8(c->a[2] + 1));
            pic_run();
            c->d[4] = set_w(c->d[4], c->d[4] - 1);
        } while (!(c->d[4] & 0x8000));
        c->a[2] += 2;
        break;
    case 6:                                         /* $6AEC: a tile and the next, n+1 times */
        c->d[3] = pic_byte();
        do {
            c->d[7] = set_b(c->d[7], rd8(c->a[2]));
            pic_put(); c->d[7] = set_b(c->d[7], c->d[7] + 1);
            pic_put(); c->d[7] = set_b(c->d[7], c->d[7] + 1);
            c->d[3] = set_w(c->d[3], c->d[3] - 1);
        } while (!(c->d[3] & 0x8000));
        c->a[2] += 1;
        break;
    case 7:                                         /* $6B06: one tile, any value */
        c->d[7] = set_b(c->d[7], pic_byte());
        pic_put();
        break;
    case 9:                                         /* $6B10: the line's length */
        c->d[0] = set_b(c->d[0], pic_byte());
        wr8(PIC_LEN, (uint8_t)c->d[0]); wr8(PIC_LEFT, (uint8_t)c->d[0]);
        break;
    case 10:                                        /* $6B1C: n+1 blank cells */
        c->d[3] = pic_byte();
        c->d[7] = c->d[7] << 16;                    /* swap, clr.w */
        do { pic_put(); c->d[3] = set_w(c->d[3], c->d[3] - 1); } while (!(c->d[3] & 0x8000));
        c->d[7] = c->d[7] >> 16;                    /* swap back */
        break;
    case 12:                                        /* $6B32: another plane */
        wr8(LAYOUT_PLANE, pic_byte());
        pic_plane();
        break;
    default:                                        /* 8, 11 */
        break;
    }
}
static void draw_layout(void)
{
    c->a[7] -= 4; wr32(c->a[7], c->a[6]); c->a[6] = c->a[7]; c->a[7] -= 0x12;   /* link a6,#-$12 */
    pic_plane();
    c->d[7] = 0;
    for (;;) {
        c->d[7] = set_b(c->d[7], pic_byte());
        uint8_t b = (uint8_t)c->d[7];
        if (b > 0xC) { pic_put(); continue; }
        c->d[0] = (uint32_t)b * 2;
        c->a[0] = 0x69D8 + rd16(0x69D8 + c->d[0]);
        if (!b) break;
        pic_command(b);
    }
    c->a[7] = c->a[6]; c->a[6] = rd32(c->a[7]); c->a[7] += 4;                    /* unlk a6 */
}

static void wait_frame(uint32_t resume) { Cpu *me = c; task_wait_frame(me, resume); c = me; }
/* $40C (YieldIfLate): with the display on ($8A5D bit 6), late is in the vertical blank or past line
 * $D0 (the H/V counter read until two reads agree), and then it yields */
static int frame_late(void)
{
    if (!(rd8(0xFFFF8A5Du) & 0x40)) return 0;
    c->d[0] = set_w(c->d[0], rd16(VDP_CTRL) & 8);
    if (c->d[0] & 0xFFFF) return 1;
    c->d[0] = set_w(c->d[0], rd16(0xC00008) & 0xFF00);
    do {
        c->d[2] = set_w(c->d[2], c->d[0]);
        c->d[0] = set_w(c->d[0], rd16(0xC00008) & 0xFF00);
    } while ((uint16_t)c->d[0] != (uint16_t)c->d[2]);
    return (uint16_t)c->d[2] >= 0xD000;
}
static int count8(uint32_t a) { uint8_t n = (uint8_t)(rd8(a) - 1); wr8(a, n); return n; }   /* subq.b #1; ne */

/* $6EAC, the Mxcg task: the animated tiles, each frame. Each slot of $950C (in use: bit 15) counts
 * down ($2) by the frames passed ($8F08); when it runs out its script ($C) goes on: 0 ends the
 * slot, $8000 restarts it ($8), $8001 jumps once more if bit 0 is set, other $80xx jump; else a
 * frame: (tiles << 8 | time), then the tiles' index, copied to the slot's VRAM ($4) by $22A8.
 * Nothing moves while $950A is set; the task ends ($4E6) when no slot is in use. */
static void tile_anims_task(void)
{
    wr32(c->a[5] + 8, 0x6BAE);
    wr16(c->a[5] + 0x40, rd16(0xFFFF8F08u));
    goto slots;
frame:                                              /* $6EBC */
    do wait_frame(0x6EC0); while (rd8(0xFFFF950Au));
    wr16(0xFFFF8F08u, (uint16_t)(rd16(0xFFFF8F08u) + 1));
    c->d[0] = set_w(c->d[0], rd16(0xFFFF8F08u));
    c->d[1] = set_w(c->d[1], c->d[0] - rd16(c->a[5] + 0x40));
    wr16(c->a[5] + 0x40, (uint16_t)c->d[0]);
    c->a[0] = TILE_ANIMS;
    c->d[2] = 0;
    for (int k = 0; k < 8; k++) {
        if (rd16(c->a[0]) & 0x8000) {
            wr16(c->a[0] + 2, (uint16_t)(rd16(c->a[0] + 2) - (uint16_t)c->d[1]));
            c->d[2] = set_b(c->d[2], c->d[2] + 1);
        }
        c->a[0] += 16;
    }
    c->d[0] = 0xFFFF;
    if (!(c->d[2] & 0xFFFF)) {
        wr32(c->a[5] + 8, c->d[2]);
        port_continue_at(c, 0x4E6, 0);
        return;
    }
slots:                                              /* $6EFE */
    c->a[0] = TILE_ANIMS;
    wr8(c->a[5] + 0x42, 8);
next_vblank:                                        /* $6F08 */
    wr16(c->a[5] + 0x44, (uint16_t)c->a[0]);
    if (frame_late()) wait_frame(0x6F10);
    c->a[0] = sx16(rd16(c->a[5] + 0x44));
slot:                                               /* $6F14 */
    {
        int16_t t = (int16_t)rd16(c->a[0] + 2);
        if (!(rd16(c->a[0]) & 0x8000) || t > 0) goto skip;
    }
    c->a[1] = rd32(c->a[0] + 0xC);
    for (;;) {                                      /* $6F30 */
        c->d[0] = set_w(c->d[0], rd16(c->a[1])); c->a[1] += 2;
        uint16_t w = (uint16_t)c->d[0];
        if (!w) { wr16(c->a[0], 0); goto skip; }
        if (!(w & 0x8000)) break;
        c->d[0] = set_w(c->d[0], w * 2);
        if (!(c->d[0] & 0xFFFF)) { c->a[1] = rd32(c->a[0] + 8); continue; }
        c->d[1] = set_w(c->d[1], rd16(c->a[1])); c->a[1] += 2;
        c->d[0] = set_w(c->d[0], c->d[0] - 2);
        if (!(c->d[0] & 0xFFFF)) {
            uint8_t f = rd8(c->a[0]);
            wr8(c->a[0], (uint8_t)(f & ~1));
            if (f & 1) continue;
        }
        c->a[1] = c->a[1] - 2 + sx16(c->d[1]);
    }
    c->d[2] = set_w(c->d[2], (c->d[0] & 0x7F00) >> 5);
    c->d[0] = set_w(c->d[0], c->d[0] & 0xFF);
    wr16(c->a[0] + 2, (uint16_t)(rd16(c->a[0] + 2) + (uint16_t)c->d[0]));
    c->d[1] = set_w(c->d[1], rd16(c->a[1])); c->a[1] += 2;
    wr32(c->a[0] + 0xC, c->a[1]);
    c->a[2] = sx16(c->a[0]);
    c->d[0] = set_w(c->d[0], rd16(c->a[0] + 4));
    call(0x22A8);
    c->a[0] = c->a[2] + 16;
    if (count8(c->a[5] + 0x42)) goto next_vblank;
    goto frame;
skip:                                               /* $6F18 */
    c->a[0] += 16;
    if (count8(c->a[5] + 0x42)) goto slot;
    goto frame;
}

/* $6F8A, the Kseg task: the Sega screen. A new screen, its graphics, palette and layout, colour
 * script $18 (the logo's shine), a fade in, half a second, script $1C, three seconds, a fade out,
 * then the next scene ($4CA8, d0 0) */
static void sega_task(void)
{
    call(0x5188);
    c->a[0] = 0x701C; call(0x234C);
    call(0x22F0);
    c->a[0] = 0x7018; call(0x135C);
    wr16(PALETTE, 0);
    fade_reset();
    c->d[0] = 0x18;
    if (color_script_start()) return;
    wait_frame(0x6FB4);
    wr8(LAYOUT_PLANE, 2);
    c->a[2] = 0x7024; draw_layout();
    fade_in();
    do wait_frame(0x6FCA); while (!(rd8(PAL_FLAGS) & 0x10));
    wr16(c->a[5] + 0x40, 0x1E);
    do wait_frame(0x6FDE); while (count_w(c->a[5] + 0x40));
    wr16(c->a[5] + 0x40, 0xB4);
    c->d[0] = 0x1C;
    if (color_script_start()) return;
    do wait_frame(0x6FF4); while (count_w(c->a[5] + 0x40));
    fade_out();
    do wait_frame(0x7004); while (!(rd8(PAL_FLAGS) & 0x10));
    c->d[0] = set_w(c->d[0], rd16(c->a[5] + 0x40));
    port_continue_at(c, 0x4CA8, 0);
}

ENTRY(PlaneAClear, plane_a_clear)          /* $6910 */
ENTRY(PlaneBClear, plane_b_clear)          /* $6916 */
ENTRY(WindowClear, window_clear)           /* $693E */
ENTRY(ScrollClear, scroll_clear)           /* $696C */
ENTRY(ScrollClearPair, scroll_clear_pair)  /* $699E */
ENTRY(DrawLayout, draw_layout)             /* $69AE */
ENTRY(GfxTablesReset, gfx_tables_reset)    /* $6BA0 */
ENTRY(TileAnimsClear, tile_anims_clear)    /* $6BAE */
ENTRY(CellVram, pic_cell)                  /* $5CA */
/* the tasks wait for frames and leave by port_continue_at */
#define NEEDS_PASS(self) do { if (!task_in_pass()) { port_leave_to_68k(c, (self)); return; } } while (0)
void TileAnimsTask(Cpu *cpu) { c = cpu; NEEDS_PASS(0x6EAC); tile_anims_task(); }   /* $6EAC */
void SegaTask(Cpu *cpu)      { c = cpu; NEEDS_PASS(0x6F8A); sega_task(); }         /* $6F8A */

/* ---- batch T47: the scenes' tasks ----
 * A scene is a task that starts others ($5106) and each frame watches for Start ($513C) or for
 * the first of them ($80C8) to end; then it hands over to the next scene's task through $50D4
 * (the screen faded and blanked first) or $50E2 (at once), both the original's. */

#define SCENE_FIRST   0xFFFF80C8u   /* the first task the scene started: bit 7 still running */
#define SCENE_NEXT    0xFFFF8068u   /* word: the scene to go to (0 the title) */

static void scene_leave(uint32_t to, uint32_t next) { c->a[1] = next; port_continue_at(c, to, 0); }
static int start_pressed(void) { pad_which(); return !(c->ccr & CC_Z); }
static int first_running(void) { return rd8(SCENE_FIRST) & 0x80; }

/* $4CC8, Ksmt: the Sega screen ($6F86); Start: the title's menu (Ktm2); when it ends: the game's
 * intro (Kedm) or, with no scene to go to, the title (Ktmt) */
static void scene_sega(void)
{
    call(0x364);
    c->a[1] = 0x4D0C; tasks_start();
    for (;;) {
        wait_frame(0x4CD8);
        if (start_pressed()) { sound_stop_25(); scene_leave(0x50D4, 0x4D64); return; }
        if (!first_running()) { scene_leave(0x50D4, rd16(SCENE_NEXT) ? 0x4F54 : 0x4D14); return; }
    }
}
/* $4D18, Ktmt: the title's opening ($703E); Start or its end: the colour task, then Ktm3 */
static void scene_title(void)
{
    call(0x364);
    c->a[1] = 0x4D4A; tasks_start();
    do {
        wait_frame(0x4D28);
        if (start_pressed()) break;
    } while (first_running());
    c->a[0] = 0x5608; call(0x4A2);
    scene_leave(0x50E2, 0x4D52);
}
/* $4D6C: the title's screen (a1's list): Start: sound $25, the menu (Kmnu); its end: the demo
 * (Kdym) */
static void scene_title_screen(void)
{
    tasks_start();
    for (;;) {
        wait_frame(0x4D74);
        if (start_pressed()) { c->d[0] = 0x25; call(0x366); scene_leave(0x50E2, 0x4DA4); return; }
        if (!first_running()) { scene_leave(0x50D4, 0x4DDE); return; }
    }
}
static void scene_title_shown(void) { c->a[1] = 0x4D5C; scene_title_screen(); }   /* $4D56, Ktm3 */
static void scene_title_after(void) { c->a[1] = 0x4D9C; scene_title_screen(); }   /* $4D68, Ktm2 */
/* $4DA8, Kmnu: the menu ($71C2); when it ends: the demo if $20(a5) is negative, else the game
 * (Kgmt) */
static void scene_menu(void)
{
    c->a[1] = 0x4DD6; tasks_start();
    do wait_frame(0x4DB4); while (first_running());
    scene_leave(0x50D4, (rd16(c->a[5] + 0x20) & 0x8000) ? 0x4DDE : 0x507C);
}
/* $4DE2, Kdym: the demo: its input ($4E64), its starting state ($4F14 into $FF3300), the game's
 * task; Start: Ktm2; the input's end or the game's: the Sega screen again */
static void scene_demo(void)
{
    call(0x364);
    clear_9996();
    wr8(0xFFFF8A50u, 0x80);
    c->a[0] = 0x4E64; demo_start();
    c->d[0] = 0xF; c->a[0] = 0xFF3300; c->a[1] = 0x4F14;
    for (int k = 0; k < 16; k++) { wr32(c->a[0], rd32(c->a[1])); c->a[0] += 4; c->a[1] += 4; }
    c->d[0] = 0xFFFF;
    c->a[1] = 0x4E5C; tasks_start();
    wr8(0xFFFF8A51u, rd8(0xFFFF8A51u) | 1);
    for (;;) {
        wait_frame(0x4E20);
        if (start_pressed()) { sound_stop_25(); wr8(0xFFFF8A50u, 0); c->a[1] = 0x4D64; break; }
        demo_step();
        if (rd8(DEMO_COUNT) && first_running()) continue;
        wr8(0xFFFF8A50u, 0); c->a[1] = 0x4CC4; break;
    }
    wr8(0xFFFF8A51u, rd8(0xFFFF8A51u) & 0xFE);
    port_continue_at(c, 0x50D4, 0);
}
/* $4F58, Kedm: the ending ($4FC8 and the ending's code at $1AA3C) */
static void scene_ending(void)
{
    c->a[1] = 0x4FC0; tasks_start();
    port_continue_at(c, 0x1AA3C, 0);
}
/* $4FA6 (put in place by $4FCC): a fade out, then the Sega screen */
static void scene_ending_fade(void)
{
    fade_out();
    do wait_frame(0x4FAE); while (!(rd8(PAL_FLAGS) & 0x10));
    scene_leave(0x50D4, 0x4CC4);
}
/* $4F66 (from the ending, $1B5C6): an hour ($E10 frames) of the ending's frames ($1AD62) or
 * until Start; then a fade out with them, then the Sega screen */
static void scene_ending_wait(void)
{
    wr16(c->a[5] + 0x40, 0xE10);
    for (;;) {
        wait_frame(0x4F70);
        call(0x1AD62);
        uint16_t n = (uint16_t)(rd16(c->a[5] + 0x40) - 1);
        wr16(c->a[5] + 0x40, n);
        if (!n || start_pressed()) break;
    }
    fade_out();
    do { wait_frame(0x4F8C); call(0x1AD62); } while (!(rd8(PAL_FLAGS) & 0x10));
    scene_leave(0x50D4, 0x4CC4);
}
/* $4FCC, Keds: waits for $A3A4 (the ending's end) and then button 7 (Start): the ending's task
 * goes on at $4FA6, and this one ends */
static void scene_ending_end(void)
{
    wr8(0xFFFFA3A4u, 0);
    do wait_frame(0x4FD4); while (!rd8(0xFFFFA3A4u) || !(rd8(PAD_PRESSED) & 0x80));
    wr32(0xFFFF8054u, 0x4FA6);
    wr16(c->a[5], 0);
}
/* $5080, Kgmt: the game ($8EB0); when its task ends: the ending (Kedm) or the Sega screen */
static void scene_game(void)
{
    call(0x364);
    clear_9996();
    wr8(0xFFFF8A50u, 0);
    wr8(0xFFFF9658u, 0);
    c->a[1] = 0x50CC; tasks_start();
    wr8(0xFFFF8A51u, rd8(0xFFFF8A51u) | 1);
    do wait_frame(0x50A2); while (first_running());
    c->a[1] = rd16(SCENE_NEXT) ? 0x4F54 : 0x4CC4;
    wr8(0xFFFF8A51u, rd8(0xFFFF8A51u) & 0xFE);
    port_continue_at(c, 0x50D4, 0);
}

void SceneSega(Cpu *cpu)        { c = cpu; NEEDS_PASS(0x4CC8); scene_sega(); }          /* $4CC8 */
void SceneTitle(Cpu *cpu)       { c = cpu; NEEDS_PASS(0x4D18); scene_title(); }         /* $4D18 */
void SceneTitleShown(Cpu *cpu)  { c = cpu; NEEDS_PASS(0x4D56); scene_title_shown(); }   /* $4D56 */
void SceneTitleAfter(Cpu *cpu)  { c = cpu; NEEDS_PASS(0x4D68); scene_title_after(); }   /* $4D68 */
void SceneMenu(Cpu *cpu)        { c = cpu; NEEDS_PASS(0x4DA8); scene_menu(); }          /* $4DA8 */
void SceneDemo(Cpu *cpu)        { c = cpu; NEEDS_PASS(0x4DE2); scene_demo(); }          /* $4DE2 */
/* the ending's four are not registered yet: no session reaches the ending */
void SceneEnding(Cpu *cpu)      { c = cpu; NEEDS_PASS(0x4F58); scene_ending(); }        /* $4F58 */
void SceneEndingWait(Cpu *cpu)  { c = cpu; NEEDS_PASS(0x4F66); scene_ending_wait(); }   /* $4F66 */
void SceneEndingFade(Cpu *cpu)  { c = cpu; NEEDS_PASS(0x4FA6); scene_ending_fade(); }   /* $4FA6 */
void SceneEndingEnd(Cpu *cpu)   { c = cpu; NEEDS_PASS(0x4FCC); scene_ending_end(); c->ret = 0; }   /* $4FCC */
void SceneGame(Cpu *cpu)        { c = cpu; NEEDS_PASS(0x5080); scene_game(); }          /* $5080 */
