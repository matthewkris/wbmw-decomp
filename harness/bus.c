/* Memory map, I/O ports, controller and the serial EEPROM. */
#include <stdio.h>
#include <string.h>
#include "md.h"

uint8_t *rom; size_t rom_size;
uint8_t ram[0x10000];
uint8_t zram[0x2000];
uint8_t pad_buttons;

/* ---- controller 1, 3-button pad: TH selects which half of the buttons is visible ---- */
static uint8_t pad_data = 0x40, pad_ctrl;

static unsigned pad_read(void)
{
    unsigned th = pad_data & 0x40, v;
    unsigned b = pad_buttons;                     /* bit 0 up, 1 down, 2 left, 3 right, 4 B, 5 C, 6 A, 7 start */
    if (th) v = 0x40 | (~b & 0x3F);               /* ?1CBRLDU, buttons active low */
    else v = (~b & 0x03) | ((~b >> 2) & 0x30);    /* ?0SA00DU */
    return (v & ~pad_ctrl) | (pad_data & pad_ctrl) | 0x80 * 0;
}

/* ---- serial EEPROM at $200001: X24C01 (bit 0 SDA, bit 1 SCL), as the game drives it at
 * $1C4AC-$1C5D8: start, a byte of (7-bit word address << 1 | read), acknowledge, then data
 * bytes, page writes of 4 bytes. The game hangs ($1C552) if a byte is not acknowledged. */
static uint8_t eeprom[128];
static int e_sda = 1, e_scl = 1, e_out = 1, e_drive;
static int e_state, e_bit, e_byte, e_addr;     /* e_bit counts rising clock edges in the byte */
enum { E_IDLE, E_ADDR, E_WRITE, E_READ };

static void eeprom_write(unsigned v)
{
    int sda = v & 1, scl = (v >> 1) & 1;
    if (e_scl && scl && e_sda != sda) {           /* start or stop: SDA changes while SCL high */
        if (!sda) { e_state = E_ADDR; e_bit = 0; e_byte = 0; }
        else e_state = E_IDLE;
        e_drive = 0;
    } else if (!e_scl && scl) {                   /* rising clock: a bit is transferred */
        if ((e_state == E_ADDR || e_state == E_WRITE) && e_bit < 8) e_byte = e_byte << 1 | sda;
        if (e_state != E_IDLE) e_bit++;
    } else if (e_scl && !scl) {                   /* falling clock: set up the next bit */
        if (e_state == E_ADDR || e_state == E_WRITE) {
            if (e_bit == 8) { e_drive = 1; e_out = 0; }          /* acknowledge in the 9th clock */
            else if (e_bit == 9) {
                e_drive = 0; e_bit = 0;
                if (e_state == E_ADDR) {
                    e_addr = (e_byte >> 1) & 0x7F;
                    if (e_byte & 1) { e_state = E_READ; e_drive = 1; e_out = eeprom[e_addr] >> 7 & 1; }
                    else e_state = E_WRITE;
                } else {
                    eeprom[e_addr] = (uint8_t)e_byte;
                    e_addr = (e_addr & ~3) | ((e_addr + 1) & 3);   /* 4-byte page */
                }
                e_byte = 0;
            }
        } else if (e_state == E_READ) {
            if (e_bit < 8) { e_drive = 1; e_out = eeprom[e_addr] >> (7 - e_bit) & 1; }
            else if (e_bit == 8) e_drive = 0;                     /* the master acknowledges */
            else { e_bit = 0; e_addr = (e_addr + 1) & 0x7F; e_drive = 1; e_out = eeprom[e_addr] >> 7 & 1; }
        }
    }
    e_sda = sda; e_scl = scl;
}

static unsigned eeprom_read(void) { return (e_drive ? e_out : e_sda) | e_scl << 1; }

/* the 128 bytes of save data, kept in a file between sessions of the live window */
int eeprom_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(eeprom, 1, sizeof eeprom, f);
    fclose(f);
    return n == sizeof eeprom ? 0 : -1;
}

int eeprom_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(eeprom, 1, sizeof eeprom, f);
    fclose(f);
    return n == sizeof eeprom ? 0 : -1;
}

void bus_state_io(FILE *f, int saving)
{
    state_io(f, ram, sizeof ram, saving); state_io(f, zram, sizeof zram, saving);
    state_io(f, eeprom, sizeof eeprom, saving);
    int v[9] = {e_sda, e_scl, e_out, e_drive, e_state, e_bit, e_byte, e_addr, pad_data << 8 | pad_ctrl};
    state_io(f, v, sizeof v, saving);
    if (!saving) {
        e_sda = v[0]; e_scl = v[1]; e_out = v[2]; e_drive = v[3]; e_state = v[4]; e_bit = v[5];
        e_byte = v[6]; e_addr = v[7]; pad_data = (uint8_t)(v[8] >> 8); pad_ctrl = (uint8_t)v[8];
    }
}

void bus_reset(void)
{
    memset(ram, 0, sizeof ram); memset(zram, 0, sizeof zram);
    memset(eeprom, 0xFF, sizeof eeprom);
    pad_data = 0x40; pad_ctrl = 0;
}

/* ---- the map ---- */
unsigned int bus_read8(unsigned int a)
{
    a &= 0xFFFFFF;
    if (a == 0x200001) return eeprom_read();
    if (a < 0x400000) return a < rom_size ? rom[a] : 0xFF;
    if (a >= 0xE00000) return ram[a & 0xFFFF];
    if (a >= 0xA00000 && a < 0xA10000) return (a & 0x7FFF) < 0x2000 ? zram[a & 0x1FFF] : 0;
    switch (a) {
    case 0xA10001: return 0xA0;                   /* overseas, NTSC, no expansion unit */
    case 0xA10003: return pad_read();
    case 0xA10005: return 0x7F;                   /* port 2: nothing pressed */
    case 0xA10009: return pad_ctrl;
    case 0xA11100: return 0x00;                   /* Z80 bus always granted (the Z80 is not run) */
    }
    if (a >= 0xC00000 && a < 0xC00020) {
        uint16_t w = bus_read16(a & ~1);
        return a & 1 ? w & 0xFF : w >> 8;
    }
    return 0;
}

/* Timing inputs. Ported code runs in no emulated time, so a VDP status or HV counter read in it
 * sees an earlier raster position than the original's read does. For a shadow check the harness
 * records what the port read and replays the same values, in order, to the original (the reads
 * still happen, for their side effects). Past the end of the record, live values. */
static int timing_mode; static uint16_t timing_log[4096]; static int timing_n, timing_pos;
void bus_timing(int mode) { timing_mode = mode; if (mode == TIMING_RECORD) timing_n = 0; timing_pos = 0; }
static uint16_t timing(uint16_t live)
{
    if (timing_mode == TIMING_RECORD && timing_n < 4096) timing_log[timing_n++] = live;
    else if (timing_mode == TIMING_REPLAY && timing_pos < timing_n) return timing_log[timing_pos++];
    return live;
}

unsigned int bus_read16(unsigned int a)
{
    a &= 0xFFFFFE;
    if (a < 0x400000 && a != 0x200000) return a + 1 < rom_size ? rom[a] << 8 | rom[a + 1] : 0xFFFF;
    if (a >= 0xE00000) return ram[a & 0xFFFF] << 8 | ram[(a + 1) & 0xFFFF];
    if (a >= 0xC00000 && a < 0xC00020) {
        switch (a & 0x1C) {
        case 0x00: return vdp_read_data();
        case 0x04: return timing(vdp_read_status());
        case 0x08: case 0x0C: return timing(vdp_read_hv());
        }
        return 0;
    }
    return bus_read8(a) << 8 | bus_read8(a + 1);
}

void bus_write8(unsigned int a, unsigned int v)
{
    a &= 0xFFFFFF; v &= 0xFF;
    if (a >= 0xE00000) { ram[a & 0xFFFF] = v; return; }
    if (a == 0x200001) { eeprom_write(v); return; }
    if (a >= 0xA00000 && a < 0xA10000) { if ((a & 0x7FFF) < 0x2000) zram[a & 0x1FFF] = v; return; }
    if (a == 0xA10003) { pad_data = v; return; }
    if (a == 0xA10009) { pad_ctrl = v; return; }
    if (a >= 0xC00000 && a < 0xC00020) {
        if ((a & 0x1C) == 0x00) vdp_write_data(v << 8 | v);
        else if ((a & 0x1C) == 0x04) vdp_write_ctrl(v << 8 | v);
        return;                                   /* PSG at $C00011 is not modelled */
    }
}

void bus_write16(unsigned int a, unsigned int v)
{
    a &= 0xFFFFFE; v &= 0xFFFF;
    if (a >= 0xE00000) { ram[a & 0xFFFF] = v >> 8; ram[(a + 1) & 0xFFFF] = v; return; }
    if (a >= 0xC00000 && a < 0xC00020) {
        if ((a & 0x1C) == 0x00) vdp_write_data(v);
        else if ((a & 0x1C) == 0x04) vdp_write_ctrl(v);
        return;
    }
    bus_write8(a, v >> 8); bus_write8(a + 1, v);
}

/* Musashi's memory interface */
unsigned int m68k_read_memory_8(unsigned int a) { return bus_read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return bus_read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return bus_read16(a) << 16 | bus_read16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { bus_write8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { bus_write16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { bus_write16(a, v >> 16); bus_write16(a + 2, v); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return bus_read8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return bus_read16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
