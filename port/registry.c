/* Every ported routine, by the address range of the 68000 routine it replaces (end exclusive:
 * the routine's own code, for coverage). The harness traps
 * these addresses (harness/hybrid.c); names match analysis/layout.json. */
#include "port.h"

void Random(Cpu *), SetVRAMWrite(Cpu *), UnpackPalette(Cpu *), LoadPalettes(Cpu *);
void ApplyVelX(Cpu *), ApplyVelY(Cpu *), ApplyVelocity(Cpu *), FrictionX(Cpu *), FrictionY(Cpu *);
void AccelX(Cpu *), AccelY(Cpu *), Accel(Cpu *), QueueSound(Cpu *), QueueSoundFF(Cpu *), SetGfxSlots(Cpu *);
void SetAnim(Cpu *), ResetAnimTimer(Cpu *), SetAnimNow(Cpu *), UpdateAnim(Cpu *);
void DecodeGfxBlock(Cpu *), StreamGfxBlocks(Cpu *), CopyRawTiles(Cpu *);
void BuildSprites(Cpu *), AddEntitySprites(Cpu *);
void HitboxOverlapSwap(Cpu *), HitboxOverlap(Cpu *), BoxPairOverlap(Cpu *), BodyVsHurtBoxes(Cpu *);
void BodyVsAttackBoxesSwap(Cpu *), BodyVsAttackBoxes(Cpu *), BoxVsBody(Cpu *), ResolveContact(Cpu *);
void ContactSide(Cpu *), CheckBodyContact(Cpu *), BodyOverlap(Cpu *);
void PlatformsSideways(Cpu *), PlatformsVertical(Cpu *);   /* movement.c: ordinary C, with contracts */
void MovePlayer(Cpu *), MoveFlat(Cpu *), MoveOnSlopesNoConveyor(Cpu *), MoveOnSlopes(Cpu *),
     MoveOnSlopesWithCeiling(Cpu *), MoveWithPlatforms(Cpu *), MoveWithPlatformsAndSlopes(Cpu *),
     EndSideways(Cpu *), EndVertical(Cpu *), WallsBothSides(Cpu *), WallsAhead(Cpu *),
     WallsAtCentreSlopes(Cpu *), WallsAtCentre(Cpu *), SlopeWalk(Cpu *),
     FloorSlopesOrCentre(Cpu *), FloorWithSlopes(Cpu *), FloorSpanOrCeiling(Cpu *),
     FloorAtCentre(Cpu *), CeilingSpanRising(Cpu *), CeilingSpan(Cpu *), CeilingAtCentre(Cpu *),
     Currents(Cpu *), Conveyors(Cpu *);
void MapBlockAt(Cpu *), MapBlockAtRaw(Cpu *), TileAttrAtPixel(Cpu *), TileAttrAt(Cpu *), TileTypeOf(Cpu *);
void ProbeTileBelow(Cpu *), ProbeTileAbove(Cpu *), ProbeTileFeet(Cpu *), ProbeTileAt(Cpu *), ProbeTileAtA3(Cpu *);

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
    {0x0007E8, 0x0007F4, "SetAnim", SetAnim},
    {0x0007F4, 0x0007FC, "ResetAnimTimer", ResetAnimTimer},
    {0x0007FC, 0x00080E, "SetAnimNow", SetAnimNow},
    {0x00080E, 0x000A1A, "UpdateAnim", UpdateAnim},     /* with its command handlers */
    {0x006BC4, 0x006EA8, "DecodeGfxBlock", DecodeGfxBlock},
    {0x0022F0, 0x00234C, "StreamGfxBlocks", StreamGfxBlocks},
    {0x0022A8, 0x0022F0, "CopyRawTiles", CopyRawTiles},
    {0x0053AA, 0x0054C4, "BuildSprites", BuildSprites},
    {0x0054C4, 0x00557E, "AddEntitySprites", AddEntitySprites},
    {0x000C36, 0x000C3E, "HitboxOverlapSwap", HitboxOverlapSwap},
    {0x000C3E, 0x000CA0, "HitboxOverlap", HitboxOverlap},
    {0x000CA0, 0x000D24, "BoxPairOverlap", BoxPairOverlap},
    {0x000D2C, 0x000D6E, "BodyVsHurtBoxes", BodyVsHurtBoxes},
    {0x000D6E, 0x000D76, "BodyVsAttackBoxesSwap", BodyVsAttackBoxesSwap},
    {0x000D76, 0x000DB6, "BodyVsAttackBoxes", BodyVsAttackBoxes},
    {0x000DB6, 0x000E1A, "BoxVsBody", BoxVsBody},
    {0x000E1A, 0x000E4A, "ResolveContact", ResolveContact},
    {0x000E4A, 0x000E64, "ContactSide", ContactSide},
    {0x000E64, 0x000E9A, "CheckBodyContact", CheckBodyContact},
    {0x000E9A, 0x000EE2, "BodyOverlap", BodyOverlap},
    {0x002542, 0x002570, "MapBlockAt", MapBlockAt},
    {0x002570, 0x0025CA, "MapBlockAtRaw", MapBlockAtRaw},
    {0x0030D2, 0x0030DA, "TileAttrAtPixel", TileAttrAtPixel},
    {0x0030DA, 0x0030F4, "TileAttrAt", TileAttrAt},
    {0x0030F4, 0x003142, "TileTypeOf", TileTypeOf},
    /* the idiomatic rewrite (movement.c): checked on memory and the stack pointer (and for the
     * player d6/d7, the map cell its caller keeps); every other register is watched */
    {0x002E38, 0x002E9E, "MovePlayer", MovePlayer, CT_SP | CT_D0 << 6 | CT_D0 << 7},
    {0x002E9E, 0x002ED4, "MoveFlat", MoveFlat, CT_SP},
    {0x002ED4, 0x002EDE, "MoveOnSlopesNoConveyor", MoveOnSlopesNoConveyor, CT_SP},
    {0x002EDE, 0x002F34, "MoveOnSlopes", MoveOnSlopes, CT_SP},
    {0x002F34, 0x002F8E, "MoveOnSlopesWithCeiling", MoveOnSlopesWithCeiling, CT_SP},
    {0x002F8E, 0x002FD0, "MoveWithPlatforms", MoveWithPlatforms, CT_SP},
    {0x002FD0, 0x00303A, "MoveWithPlatformsAndSlopes", MoveWithPlatformsAndSlopes, CT_SP},
    {0x00303A, 0x003098, "EndSideways", EndSideways, CT_SP},
    {0x003098, 0x0030CE, "EndVertical", EndVertical, CT_SP},
    {0x003142, 0x00322E, "WallsBothSides", WallsBothSides, CT_SP},
    {0x00322E, 0x0032FE, "WallsAhead", WallsAhead, CT_SP},
    {0x0032FE, 0x003310, "WallsAtCentreSlopes", WallsAtCentreSlopes, CT_SP},
    {0x003310, 0x0033B8, "WallsAtCentre", WallsAtCentre, CT_SP},
    {0x00341C, 0x00356E, "SlopeWalk", SlopeWalk, CT_SP},
    {0x00356E, 0x003584, "FloorSlopesOrCentre", FloorSlopesOrCentre, CT_SP},
    {0x003584, 0x003948, "FloorWithSlopes", FloorWithSlopes, CT_SP},
    {0x003948, 0x0039FA, "FloorSpanOrCeiling", FloorSpanOrCeiling, CT_SP},
    {0x0039FA, 0x003A4C, "FloorAtCentre", FloorAtCentre, CT_SP},
    {0x003A4C, 0x003A54, "CeilingSpanRising", CeilingSpanRising, CT_SP},
    {0x003A54, 0x003AB0, "CeilingSpan", CeilingSpan, CT_SP},
    {0x003AB0, 0x003AFC, "CeilingAtCentre", CeilingAtCentre, CT_SP},
    {0x003AFC, 0x003C22, "PlatformsSideways", PlatformsSideways, CT_SP},
    {0x003C22, 0x003E20, "PlatformsVertical", PlatformsVertical, CT_SP},
    {0x003E4C, 0x003EB8, "Currents", Currents, CT_SP},
    {0x003F08, 0x003F54, "Conveyors", Conveyors, CT_SP},
    {0x003F58, 0x003F70, "ProbeTileBelow", ProbeTileBelow},
    {0x003F74, 0x003F8E, "ProbeTileAbove", ProbeTileAbove},
    {0x003F92, 0x003FAC, "ProbeTileFeet", ProbeTileFeet},
    {0x003FAC, 0x003FB0, "ProbeTileAt", ProbeTileAt},
    {0x003FB0, 0x003FC2, "ProbeTileAtA3", ProbeTileAtA3},
};
const int port_routine_count = sizeof port_routines / sizeof port_routines[0];
