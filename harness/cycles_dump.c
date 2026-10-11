/* Writes Musashi's 68000 cycle table: for every opcode word, the cycles Musashi charges after
 * running the instruction (work/cycles.bin, 65,536 bytes). tools/m68k2c.py reads it for the
 * native build's optional timing, so its counts match the harness's by construction; the
 * dynamic extras (branches not taken, DBcc, Scc, MOVEM, shifts, MULU/MULS) follow Musashi's
 * rules in the translator. */
#include <stdio.h>
#include "m68k.h"
#include "m68kops.h"

/* Musashi's callbacks and memory, unused here */
int md_int_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }
int md_instr_hook(unsigned int pc) { (void)pc; return 0; }
unsigned int m68k_read_memory_8(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_memory_16(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_memory_32(unsigned int a) { (void)a; return 0; }
void m68k_write_memory_8(unsigned int a, unsigned int v) { (void)a; (void)v; }
void m68k_write_memory_16(unsigned int a, unsigned int v) { (void)a; (void)v; }
void m68k_write_memory_32(unsigned int a, unsigned int v) { (void)a; (void)v; }
unsigned int m68k_read_disassembler_8(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_disassembler_16(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_disassembler_32(unsigned int a) { (void)a; return 0; }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: cycles_dump OUT\n"); return 2; }
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); return 1; }
    fwrite(m68ki_cycles[0], 1, 0x10000, f);
    fclose(f);
    return 0;
}
