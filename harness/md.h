/* Reference harness: the original 68000 code on Musashi, against a reimplemented Mega Drive.
 * Headless: it runs frames, writes PNG frames and RAM dumps, and takes scripted input.
 * Only what this game uses is modelled; see harness/README.md for what is and is not. */
#ifndef MD_H
#define MD_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* timing (NTSC) */
#define LINES_PER_FRAME 262
#define CYCLES_PER_LINE 488
#define ACTIVE_LINES 224

/* bus.c */
extern uint8_t *rom; extern size_t rom_size;
extern uint8_t *bus_rom_reads;          /* bus.c: when set, each ROM byte read is marked (1) */
extern uint8_t ram[0x10000];
extern uint8_t zram[0x2000];
extern uint8_t pad_buttons;           /* active-high: up down left right B C A start */
void bus_reset(void);
unsigned int bus_read8(unsigned int a);
unsigned int bus_read16(unsigned int a);
enum { TIMING_OFF, TIMING_RECORD, TIMING_REPLAY };
void bus_timing(int mode);              /* record a port's VDP status/HV reads, replay them to the original */
void bus_write8(unsigned int a, unsigned int v);
void bus_write16(unsigned int a, unsigned int v);

/* sound.c (with ym2612.cpp): the Z80 and its sound driver, the YM2612 and the SN76489 */
void sound_reset(void);
void sound_set_busreq(int on);
void sound_set_reset(int on);
int sound_bus_granted(void);
void sound_psg_write(uint8_t d);
void sound_hold(int on);                /* hold the Z80 and the chips still (shadow checks) */
void sound_run_line(int vint);          /* one scanline; vint: raise the Z80's VBlank interrupt */
void sound_idle(void);                  /* let some sound time pass, even while held */
int sound_take(int16_t *dst, int max);  /* take up to max stereo samples (53,267 Hz) */
#define SOUND_RATE 53267
void sound_state_io(FILE *f, int saving);

/* vdp.c */
extern int vdp_line, vdp_line_cycle;   /* current scanline and cycle within it */
#define FRAME_STRIDE 400                /* pixels a row of frame_rgb holds: 256 + 2 * 72 for widescreen */
extern uint32_t frame_rgb[240][FRAME_STRIDE];
extern int frame_width;
/* widescreen (the native build; 0 in the harness): vdp_pad pixels shown at each side of the H32
 * picture, which stays 256 + 2 * vdp_pad wide in H40 too (black sides there); vdp_pad_fill: the
 * game fills them (the room allows it), else they are black; vdp_band_top: in a filled picture,
 * the first of the HUD's lines whose sides show the scene (the map is drawn there), the lines
 * above it black at the sides (64, the default: the whole band); vdp_hud_wide: the HUD laid out
 * for the wide picture (its top row only, the gold beside the items; vdp.c); vdp_hide_msg: with it,
 * the window's rows under the HUD not drawn (the host shows the gold found its own way);
 * vdp_scene: the scene alone, as the host's talk zoom shows it (its pixels with nothing drawn the
 * backdrop beside the HUD too, not black: the zoom brings those lines into the picture) */
extern int vdp_pad, vdp_pad_fill, vdp_band_top, vdp_hud_wide, vdp_hide_msg, vdp_scene;
void vdp_reset(void);
void vdp_write_ctrl(uint16_t w);
void vdp_write_data(uint16_t w);
uint16_t vdp_read_status(void);
uint16_t vdp_read_data(void);
uint16_t vdp_read_hv(void);
void vdp_render_line(int line);
int vdp_vint_enabled(void), vdp_hint_enabled(void), vdp_hint_counter_reload(void);
void vdp_set_vblank(int on);
void vdp_set_vint_pending(int on);
extern uint8_t vdp_reg[32];
extern uint8_t vram[0x10000];
extern uint16_t cram[64], vsram[40];
void vdp_latch(int v[4]);

/* save states (state.c): every module writes or reads its own part */
#include <stdio.h>
void state_io(FILE *f, void *p, size_t n, int saving);
void bus_state_io(FILE *f, int saving);
void vdp_state_io(FILE *f, int saving);

/* core.c */
extern int md_trace;
int md_load_rom(const char *path);
void md_power_on(void);
void md_run_frame(void);
int eeprom_load(const char *path);
int eeprom_save(const char *path);
void eeprom_capture(uint8_t out[136]);   /* its 128 bytes and its serial state, for the shadow check */
int md_save_state(const char *path, int frame);
int md_load_state(const char *path, int *frame);
const uint8_t *md_snapshot(size_t *n);          /* points into a buffer reused by the next call */
void md_restore(const uint8_t *buf, size_t n);
void md_irq_hold(int on);

/* hybrid.c: ported routines running in place of the 68000 originals */
enum { PORT_OFF, PORT_REPLACE, PORT_SHADOW };
void hybrid_init(int mode);
void hybrid_report(void);
void yield_survey_report(void);         /* WBMW_YIELD_SURVEY=1: where Yield returns */
void hybrid_coverage(const char *path);
int hybrid_fuzz(const char *name, long count);
int hybrid_call(const char *name);      /* run one ported routine once, registers kept */
int md_instr_hook(unsigned int pc);

/* audio.c (windows only): the sound through SDL */
void audio_open(void);
void audio_frame(int mute);

/* framelog.c: comparing runs by game frame (harness and native build) */
int framelog_open(const char *path);
int framelog_input(const char *path);    /* input keyed by game frame */
int framelog_input_active(void);
void framelog_dump_at(long n, const char *dir);
void framelog_skip(uint32_t lo, uint32_t hi);   /* RAM offsets lo-hi left out of the hash too */
void framelog_at(uint32_t pc);
void framelog_close(void);

/* wav.c */
int wav_open(const char *path, uint32_t rate);
void wav_write(const int16_t *stereo, int n);
void wav_close(void);

/* png.c */
int png_write_rgb(const char *path, const uint32_t *px, int w, int h, int stride);
#endif
