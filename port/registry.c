/* Every ported routine, by the address range of the 68000 routine it replaces (end exclusive:
 * the routine's own code, for coverage). The harness traps
 * these addresses (harness/hybrid.c); names match analysis/layout.json. */
#include "port.h"

void Random(Cpu *), SetVRAMWrite(Cpu *), UnpackPalette(Cpu *), LoadPalettes(Cpu *);
void ApplyVelX(Cpu *), ApplyVelY(Cpu *), ApplyVelocity(Cpu *), FrictionX(Cpu *), FrictionY(Cpu *);
void AccelX(Cpu *), AccelY(Cpu *), Accel(Cpu *), QueueSound(Cpu *), QueueSoundFF(Cpu *), SetGfxSlots(Cpu *);

const PortRoutine port_routines[] = {
    {0x0005D8, 0x0005FE, "Random", Random},
    {0x0005A0, 0x0005B6, "SetVRAMWrite", SetVRAMWrite},
    {0x001370, 0x0013AE, "UnpackPalette", UnpackPalette},
    {0x00135C, 0x001370, "LoadPalettes", LoadPalettes},
    {0x000AB4, 0x000AB8, "ApplyVelocity", ApplyVelocity},
    {0x000AB8, 0x000AD6, "ApplyVelX", ApplyVelX},
    {0x000AD6, 0x000AF4, "ApplyVelY", ApplyVelY},
    {0x000B92, 0x000BAC, "FrictionX", FrictionX},
    {0x000BAC, 0x000BC6, "FrictionY", FrictionY},
    {0x000AF4, 0x000B42, "AccelX", AccelX},
    {0x000B42, 0x000B44, "Accel", Accel},
    {0x000B44, 0x000B92, "AccelY", AccelY},
    {0x000366, 0x0003B8, "QueueSound", QueueSound},
    {0x000364, 0x000366, "QueueSoundFF", QueueSoundFF},
    {0x00234C, 0x0023A6, "SetGfxSlots", SetGfxSlots},
};
const int port_routine_count = sizeof port_routines / sizeof port_routines[0];
