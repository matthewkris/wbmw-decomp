/* The kernel's frame work ($4FEE-$5608): what runs once a frame after the vertical blank ($5262,
 * from the wait at $3B8), the joypads, the palette buffers, the screen reset and the demo input.
 * Batch T42 on.
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
