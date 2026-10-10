/* Machine core shared by the headless harness and the live window: ROM loading, power-on,
 * interrupts and the NTSC frame loop. */
#include <stdio.h>
#include <stdlib.h>
#include "md.h"
#include "../third_party/musashi/m68k.h"
extern unsigned int m68k_read_memory_32(unsigned int a);

int md_trace;                         /* when set, sample the PC through the frame to stderr */
static int vint_pending, hint_pending;

static void update_irq(void)
{
    m68k_set_irq(vint_pending ? 6 : hint_pending ? 4 : 0);
}

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
