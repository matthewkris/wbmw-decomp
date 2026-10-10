/* Reference harness: the original 68000 code on Musashi, against a reimplemented Mega Drive.
 * Headless: it runs frames, writes PNG frames and RAM dumps, and takes scripted input.
 * Only what this game uses is modelled; see harness/README.md for what is and is not. */
#ifndef MD_H
#define MD_H
#include <stdint.h>
#include <stddef.h>

/* timing (NTSC) */
#define LINES_PER_FRAME 262
#define CYCLES_PER_LINE 488
#define ACTIVE_LINES 224

/* bus.c */
extern uint8_t *rom; extern size_t rom_size;
extern uint8_t ram[0x10000];
extern uint8_t zram[0x2000];
extern uint8_t pad_buttons;           /* active-high: up down left right B C A start */
void bus_reset(void);
unsigned int bus_read8(unsigned int a);
unsigned int bus_read16(unsigned int a);
void bus_write8(unsigned int a, unsigned int v);
void bus_write16(unsigned int a, unsigned int v);

/* vdp.c */
extern int vdp_line, vdp_line_cycle;   /* current scanline and cycle within it */
extern uint32_t frame_rgb[240][320];
extern int frame_width;
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
int md_save_state(const char *path, int frame);
int md_load_state(const char *path, int *frame);
const uint8_t *md_snapshot(size_t *n);          /* points into a buffer reused by the next call */
void md_restore(const uint8_t *buf, size_t n);
void md_irq_hold(int on);

/* hybrid.c: ported routines running in place of the 68000 originals */
enum { PORT_OFF, PORT_REPLACE, PORT_SHADOW };
void hybrid_init(int mode);
void hybrid_report(void);
void hybrid_coverage(const char *path);
int hybrid_fuzz(const char *name, long count);
int md_instr_hook(unsigned int pc);

/* png.c */
int png_write_rgb(const char *path, const uint32_t *px, int w, int h, int stride);
#endif
