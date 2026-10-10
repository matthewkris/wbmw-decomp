/* Every ported routine, by the address of the 68000 routine it replaces. The harness traps
 * these addresses (harness/hybrid.c); names match analysis/layout.json. */
#include "port.h"

void Random(Cpu *), SetVRAMWrite(Cpu *), UnpackPalette(Cpu *), LoadPalettes(Cpu *);
void ApplyVelX(Cpu *), ApplyVelY(Cpu *), ApplyVelocity(Cpu *), FrictionX(Cpu *), FrictionY(Cpu *);

const PortRoutine port_routines[] = {
    {0x0005D8, "Random", Random},
    {0x0005A0, "SetVRAMWrite", SetVRAMWrite},
    {0x001370, "UnpackPalette", UnpackPalette},
    {0x00135C, "LoadPalettes", LoadPalettes},
    {0x000AB4, "ApplyVelocity", ApplyVelocity},
    {0x000AB8, "ApplyVelX", ApplyVelX},
    {0x000AD6, "ApplyVelY", ApplyVelY},
    {0x000B92, "FrictionX", FrictionX},
    {0x000BAC, "FrictionY", FrictionY},
};
const int port_routine_count = sizeof port_routines / sizeof port_routines[0];
