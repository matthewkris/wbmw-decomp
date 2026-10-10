/* Machine core shared by the headless harness and the live window: ROM loading, power-on,
 * interrupts and the NTSC frame loop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md.h"
#include "../third_party/musashi/m68k.h"
extern unsigned int m68k_read_memory_32(unsigned int a);

int md_trace;                         /* when set, sample the PC through the frame to stderr */
static int vint_pending, hint_pending;

static int irq_hold;                  /* set while the shadow check runs an original routine */

static void update_irq(void)
{
    m68k_set_irq(irq_hold ? 0 : vint_pending ? 6 : hint_pending ? 4 : 0);
}

void md_irq_hold(int on) { irq_hold = on; update_irq(); }

int md_int_ack(int level)
{
    if (level == 6) { vint_pending = 0; vdp_set_vint_pending(0); }
    else if (level == 4) hint_pending = 0;
    update_irq();
    return M68K_INT_ACK_AUTOVECTOR;
}

int md_load_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END); rom_size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(rom_size);
    if (fread(rom, 1, rom_size, f) != rom_size) { fprintf(stderr, "%s: short read\n", path); fclose(f); return -1; }
    fclose(f);
    return 0;
}

void md_power_on(void)
{
    bus_reset(); vdp_reset();
    vint_pending = hint_pending = 0;
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_pulse_reset();
}

/* ---- save states, taken between frames ----
 * File: "WBMWST01", the frame number, the 68000 registers, pending interrupts, then the bus
 * (RAM, Z80 RAM, EEPROM and its protocol state, pad latch) and the VDP. Integers are in host
 * byte order: a state belongs to the machine that wrote it. */
static const char STATE_MAGIC[8] = "WBMWST01";
static int state_failed;
static FILE *st_file;                 /* a state goes to a file or, for snapshots, to memory */
static uint8_t *st_mem; static size_t st_pos, st_cap;

void state_io(FILE *f, void *p, size_t n, int saving)
{
    (void)f;
    if (st_file) {
        size_t done = saving ? fwrite(p, 1, n, st_file) : fread(p, 1, n, st_file);
        if (done != n) state_failed = 1;
        return;
    }
    if (saving) {
        if (st_pos + n > st_cap) {
            st_cap = (st_pos + n) * 2;
            st_mem = realloc(st_mem, st_cap);
        }
        memcpy(st_mem + st_pos, p, n);
    } else {
        if (st_pos + n > st_cap) { state_failed = 1; return; }
        memcpy(p, st_mem + st_pos, n);
    }
    st_pos += n;
}

static const int cpu_regs[] = {
    M68K_REG_SR, M68K_REG_USP, M68K_REG_ISP,     /* SR first: it selects which stack A7 is */
    M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6,
    M68K_REG_PC,
};
#define NREGS (int)(sizeof cpu_regs / sizeof cpu_regs[0])

/* the machine, through state_io: CPU registers, pending interrupts, bus, VDP */
static void machine_io(int saving)
{
    unsigned regs[NREGS];
    if (saving) for (int i = 0; i < NREGS; i++) regs[i] = m68k_get_reg(NULL, cpu_regs[i]);
    state_io(NULL, regs, sizeof regs, saving);
    int irq[2] = {vint_pending, hint_pending};
    state_io(NULL, irq, sizeof irq, saving);
    bus_state_io(NULL, saving);
    vdp_state_io(NULL, saving);
    if (!saving && !state_failed) {
        for (int i = 0; i < NREGS; i++) m68k_set_reg(cpu_regs[i], regs[i]);
        vint_pending = irq[0]; hint_pending = irq[1];
        update_irq();
    }
}

static int state_file(const char *path, int *frame, int saving)
{
    FILE *f = fopen(path, saving ? "wb" : "rb");
    if (!f) { perror(path); return -1; }
    st_file = f; state_failed = 0;
    char magic[8]; memcpy(magic, STATE_MAGIC, 8);
    state_io(f, magic, 8, saving);
    if (!saving && memcmp(magic, STATE_MAGIC, 8)) { fprintf(stderr, "%s: not a save state\n", path); fclose(f); st_file = NULL; return -1; }
    state_io(f, frame, sizeof *frame, saving);
    machine_io(saving);
    fclose(f); st_file = NULL;
    if (state_failed) { fprintf(stderr, "%s: %s failed\n", path, saving ? "writing" : "reading"); return -1; }
    return 0;
}

/* in-memory snapshots, for the shadow check: the whole machine, restored exactly */
const uint8_t *md_snapshot(size_t *n)
{
    st_file = NULL; st_pos = 0; state_failed = 0;
    machine_io(1);
    *n = st_pos;
    return st_mem;
}

void md_restore(const uint8_t *buf, size_t n)
{
    uint8_t *keep = st_mem; size_t keep_cap = st_cap;
    st_mem = (uint8_t *)buf; st_cap = n; st_pos = 0; state_failed = 0; st_file = NULL;
    machine_io(0);
    st_mem = keep; st_cap = keep_cap;
}

int md_save_state(const char *path, int frame) { return state_file(path, &frame, 1); }
int md_load_state(const char *path, int *frame) { return state_file(path, frame, 0); }

void md_run_frame(void)
{
    int hcount = vdp_hint_counter_reload();
    for (int line = 0; line < LINES_PER_FRAME; line++) {
        vdp_line = line;
        if (line == ACTIVE_LINES) {
            vdp_set_vblank(1); vdp_set_vint_pending(1);
            if (vdp_vint_enabled()) { vint_pending = 1; update_irq(); }
        }
        if (line == 0) vdp_set_vblank(0);
        /* run the line in slices so the HV counter and HBlank flag move */
        for (int c = 0; c < CYCLES_PER_LINE; c += 61) {
            vdp_line_cycle = c;
            m68k_execute(61);
            if (md_trace)
                fprintf(stderr, "line %3d  pc %06X  sp %06X  ret %06X\n", line, m68k_get_reg(NULL, M68K_REG_PC),
                        m68k_get_reg(NULL, M68K_REG_SP), m68k_read_memory_32(m68k_get_reg(NULL, M68K_REG_SP)));
        }
        if (line < ACTIVE_LINES) {
            vdp_render_line(line);
            if (--hcount < 0) {
                hcount = vdp_hint_counter_reload();
                if (vdp_hint_enabled()) { hint_pending = 1; update_irq(); }
            }
        } else hcount = vdp_hint_counter_reload();
    }
}
