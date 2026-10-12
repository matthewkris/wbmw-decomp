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
void PlayerState(Cpu *), PlayerStartFrame(Cpu *), PlayerInput(Cpu *), PlayerBounce(Cpu *),   /* player.c */
     PlayerAccel(Cpu *), PlayerSplash(Cpu *), PlayerBubbles(Cpu *), PlayerHurtBoxes(Cpu *),
     PlayerAnimate(Cpu *), PlayerSword(Cpu *), PlayerSpecial(Cpu *),
     PlayerDamage(Cpu *), PlayerGold(Cpu *), PlayerItem(Cpu *), PlayerHeart(Cpu *), PlayerUpdate(Cpu *);
void TaskCallSystem(Cpu *), TaskCallUser(Cpu *);
void Cosine(Cpu *), Sine(Cpu *), VelocityAt(Cpu *), VelocityAtCoarse(Cpu *), Direction16To(Cpu *), Direction16(Cpu *), DirectionTo(Cpu *), Direction(Cpu *), SetVelocityAt(Cpu *), VelocityTo(Cpu *), SetVelocityToCoarse(Cpu *), SetVelocityAtCoarse(Cpu *), VelocityToCoarse(Cpu *), Distance(Cpu *);   /* maths.c: exact */
void RunItems(Cpu *);   /* items.c */
void RunSpawnPoints(Cpu *);   /* spawns.c */
void ObjLobbed(Cpu *), ObjThrown(Cpu *), ObjStanding(Cpu *), ObjFlying(Cpu *), ObjWalker(Cpu *),   /* objects.c */
     ObjCrawler(Cpu *), ObjCrawlerStill(Cpu *), ObjAimed(Cpu *), ObjBouncer(Cpu *), ObjHopper(Cpu *),
     ObjFloater(Cpu *), ObjColumn(Cpu *), ObjEdgeCrawler(Cpu *);
void PplStanding(Cpu *), PplWanderer(Cpu *), PplSpeaker(Cpu *), PplTownsperson(Cpu *),   /* people.c */
     PplStandingStill(Cpu *), PplFlagged(Cpu *), PplPlacedBreakable(Cpu *), PplBreakable(Cpu *), PplScene(Cpu *);
void HndAreaMonster(Cpu *), HndHopperOr(Cpu *), HndGuardian(Cpu *), HndVanishing(Cpu *),   /* handlers.c */
     HndFlyer(Cpu *), HndThrownLater(Cpu *), HndCarried(Cpu *), HndPacer(Cpu *),
     HndDarter(Cpu *), HndDiver(Cpu *), HndSeeker(Cpu *), HndGrower(Cpu *);
void Act140F2(Cpu *), Act140FC(Cpu *), Act1410E(Cpu *), Act14120(Cpu *), Act1416E(Cpu *),   /* actions.c */
     Act141A4(Cpu *), Act1429E(Cpu *), Act142D6(Cpu *), Act142E4(Cpu *), Act142F6(Cpu *), Act14308(Cpu *),
     Act1436C(Cpu *), Act14394(Cpu *), Act143BC(Cpu *), Act143F2(Cpu *), Act14420(Cpu *), Act14456(Cpu *),
     Act14518(Cpu *), Act1454C(Cpu *), Act145C4(Cpu *), Act1463C(Cpu *), Act14682(Cpu *), Act146FC(Cpu *), Act1476C(Cpu *),
     Act14822(Cpu *), Act14888(Cpu *), Act1492A(Cpu *), Act14974(Cpu *), Act149D0(Cpu *), Act14A82(Cpu *),
     Act14AEA(Cpu *), Act14AFA(Cpu *), Act14B7E(Cpu *), Act14C1A(Cpu *),
     Act14C50(Cpu *), Act14CA2(Cpu *), Act14CF0(Cpu *), Act14D74(Cpu *), Act14DC8(Cpu *), Act14E1A(Cpu *),
     Act14E6C(Cpu *), Act14EC8(Cpu *), Act14EF4(Cpu *), Act14F28(Cpu *), Act14F54(Cpu *), Act14F80(Cpu *),
     Act14FD2(Cpu *), Act14FFE(Cpu *), Act15010(Cpu *), Act15066(Cpu *), Act150D8(Cpu *), Act15126(Cpu *),
     Act151A0(Cpu *), Act151D6(Cpu *), Act152A0(Cpu *),
     Act15352(Cpu *), Act153BE(Cpu *), Act153F0(Cpu *), Act15464(Cpu *), Act154B2(Cpu *), Act15504(Cpu *),
     Act15576(Cpu *),
     Act155E2(Cpu *), Act15630(Cpu *), Act15704(Cpu *), Act1577A(Cpu *), Act157A6(Cpu *), Act157A8(Cpu *),
     Act157E4(Cpu *), Act15830(Cpu *), Act1589A(Cpu *), Act158E6(Cpu *), Act15994(Cpu *),
     Act15A86(Cpu *), Act15C9E(Cpu *), Act15DD2(Cpu *), Act15E54(Cpu *), Act15EE6(Cpu *), Act15FB0(Cpu *),
     Act16D70(Cpu *), Act16ED0(Cpu *), Act16F50(Cpu *), Act16FF0(Cpu *);
void RunEnemies(Cpu *), EnemyDispatch(Cpu *), EnemySetState(Cpu *), EnemyNextState(Cpu *);   /* enemies.c */
void MagicUsed(Cpu *), SlotEmptied(Cpu *), MagicMeter(Cpu *), HudRedraw(Cpu *), GoldDisplay(Cpu *);   /* hud.c */
void ClearEntityAnim(Cpu *), ClearEntity(Cpu *), FreezeEntities(Cpu *), UnfreezeEntities(Cpu *),   /* entity.c */
     FindFreeEffect(Cpu *), CheckGrounded(Cpu *);
void Hearts(Cpu *), SpawnItem(Cpu *), SpawnItemAt(Cpu *), PlayerSwordTip(Cpu *);   /* hud.c, effects.c, player.c */
void MagicStart(Cpu *), MagicFrame(Cpu *), MagicShieldHit(Cpu *),   /* magic.c */
     QuakeShaking(Cpu *), QuakeRocks(Cpu *), QuakeSettling(Cpu *), QuakeEnding(Cpu *), ThunderFlashes(Cpu *);   /* task.c: task passes on coroutines */
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
    {0x000A20, 0x000A36, "ClearEntityAnim", ClearEntityAnim},
    {0x000A36, 0x000A66, "ClearEntity", ClearEntity},
    {0x000A66, 0x000A8E, "FreezeEntities", FreezeEntities},
    {0x000A8E, 0x000AB4, "UnfreezeEntities", UnfreezeEntities},
    {0x0027E6, 0x0027FA, "FindFreeEffect", FindFreeEffect},
    {0x004362, 0x00436C, "CheckGrounded", CheckGrounded},
    {0x000EE2, 0x000EE6, "Cosine", Cosine},
    {0x000EE6, 0x000FA8, "Sine", Sine},
    {0x000FA8, 0x000FC0, "VelocityAt", VelocityAt},
    {0x000FC0, 0x001032, "VelocityAtCoarse", VelocityAtCoarse},
    {0x001032, 0x001042, "Direction16To", Direction16To},
    {0x001042, 0x00108C, "Direction16", Direction16},
    {0x00108C, 0x00109C, "DirectionTo", DirectionTo},
    {0x00109C, 0x001208, "Direction", Direction},
    {0x001208, 0x001216, "SetVelocityAt", SetVelocityAt},
    {0x001216, 0x001220, "VelocityTo", VelocityTo},
    {0x001234, 0x00123A, "SetVelocityToCoarse", SetVelocityToCoarse},
    {0x00123A, 0x001248, "SetVelocityAtCoarse", SetVelocityAtCoarse},
    {0x001248, 0x001252, "VelocityToCoarse", VelocityToCoarse},
    {0x001252, 0x00127A, "Distance", Distance},
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
    {0x00E2C6, 0x00ED5C, "PlayerState", PlayerState, CT_SP},   /* player.c: the state handlers and their checks */
    {0x00E1A6, 0x00E22E, "PlayerStartFrame", PlayerStartFrame, CT_SP},
    {0x00DE6A, 0x00DF2E, "PlayerInput", PlayerInput, CT_SP},
    {0x00DFA0, 0x00DFDE, "PlayerBounce", PlayerBounce, CT_SP},
    {0x00EF4E, 0x00EFE2, "PlayerAccel", PlayerAccel, CT_SP},
    {0x00DD7C, 0x00DDDE, "PlayerSplash", PlayerSplash, CT_SP},
    {0x00DDDE, 0x00DE6A, "PlayerBubbles", PlayerBubbles, CT_SP},
    {0x00EFE2, 0x00F064, "PlayerHurtBoxes", PlayerHurtBoxes, CT_SP},
    {0x00F064, 0x00F1B0, "PlayerAnimate", PlayerAnimate, CT_SP},
    {0x00ED5C, 0x00EE70, "PlayerSword", PlayerSword, CT_SP},
    {0x00EF12, 0x00EF4E, "PlayerSpecial", PlayerSpecial, CT_SP},
    {0x00DFDE, 0x00E1A6, "PlayerDamage", PlayerDamage, CT_SP},   /* may carry on at $E22E or $F288 */
    {0x00DD08, 0x00DD7C, "PlayerUpdate", PlayerUpdate, CT_SP},       /* the player's frame; waits on its task's pass */
    {0x004BD6, 0x004BD8, "TaskCallSystem", TaskCallSystem, CT_SP},   /* the scheduler's jsr (a0) */
    {0x004C88, 0x004C8A, "TaskCallUser", TaskCallUser, CT_SP},
    {0x00F1EC, 0x00F256, "PlayerGold", PlayerGold, CT_SP},       /* pickups: reached from $F1B0's table */
    {0x00F256, 0x00F262, "PlayerItem", PlayerItem, CT_SP},
    {0x00F262, 0x00F282, "PlayerHeart", PlayerHeart, CT_SP},
    {0x008AA2, 0x008B62, "Hearts", Hearts, CT_SP | CT_A0 << 4},
    {0x008778, 0x0087CE, "MagicUsed", MagicUsed, CT_SP},          /* hud.c: the HUD bar */
    {0x0087CE, 0x00882C, "MagicMeter", MagicMeter, CT_SP},
    {0x00882C, 0x00885A, "SlotEmptied", SlotEmptied, CT_SP},      /* also from using up an item ($24E8) */
    {0x0076C4, 0x0076E8, "HudRedraw", HudRedraw, CT_SP},
    {0x0085A8, 0x0085CC, "GoldDisplay", GoldDisplay, CT_SP},   /* hud.c: leaves a4 the player */
    {0x01090C, 0x010910, "SpawnItem", SpawnItem},                 /* effects.c: every register as the original */
    {0x010910, 0x010A1E, "SpawnItemAt", SpawnItemAt},
    {0x010A1E, 0x011094, "RunItems", RunItems, CT_SP},
    {0x012A70, 0x0133B2, "RunSpawnPoints", RunSpawnPoints, CT_SP}, /* spawns.c: with its kinds and helpers */
    {0x0133B2, 0x013418, "RunEnemies", RunEnemies, CT_SP},       /* enemies.c: every enemy, each frame */
    {0x013582, 0x0135AE, "EnemyDispatch", EnemyDispatch},         /* the monsters' state machine: carries on at the action */
    {0x01357E, 0x013582, "EnemySetState", EnemySetState},
    {0x013542, 0x013578, "EnemyNextState", EnemyNextState},           /* items.c: every item and effect, each frame */
    {0x01A464, 0x01A4D2, "ObjLobbed", ObjLobbed, CT_SP},         /* objects.c: thirteen enemy handlers (ranges to the next) */
    {0x01A4D2, 0x01A538, "ObjThrown", ObjThrown, CT_SP},
    {0x01A538, 0x01A55A, "ObjStanding", ObjStanding, CT_SP},
    {0x01A55A, 0x01A5B0, "ObjFlying", ObjFlying, CT_SP},
    {0x01A5B0, 0x01A62C, "ObjWalker", ObjWalker, CT_SP},
    {0x01A62C, 0x01A678, "ObjCrawler", ObjCrawler, CT_SP},
    {0x01A678, 0x01A686, "ObjCrawlerStill", ObjCrawlerStill, CT_SP},
    {0x01A686, 0x01A6EE, "ObjAimed", ObjAimed, CT_SP},
    {0x01A6EE, 0x01A7B8, "ObjBouncer", ObjBouncer, CT_SP},
    {0x01A7B8, 0x01A84E, "ObjHopper", ObjHopper, CT_SP},
    {0x01A84E, 0x01A8BA, "ObjFloater", ObjFloater, CT_SP},
    {0x01A8BA, 0x01A99A, "ObjColumn", ObjColumn, CT_SP},
    {0x01A99A, 0x01AA3C, "ObjEdgeCrawler", ObjEdgeCrawler, CT_SP},
    {0x0163AA, 0x0163C8, "PplStanding", PplStanding, CT_SP},     /* people.c: nine handlers (ranges to the next) */
    {0x0163C8, 0x01646E, "PplWanderer", PplWanderer, CT_SP},
    {0x01646E, 0x01652A, "PplSpeaker", PplSpeaker, CT_SP},       /* with the talk test $164B8 and $1649C */
    {0x01652A, 0x0166CA, "PplTownsperson", PplTownsperson, CT_SP},
    {0x0166CA, 0x0166DA, "PplStandingStill", PplStandingStill, CT_SP},
    {0x0166DA, 0x016712, "PplFlagged", PplFlagged, CT_SP},
    {0x016712, 0x01676C, "PplPlacedBreakable", PplPlacedBreakable, CT_SP},
    {0x01676C, 0x0167DC, "PplBreakable", PplBreakable, CT_SP},   /* with $1678A, the breaking */
    {0x0167DC, 0x0168E8, "PplScene", PplScene, CT_SP},
    {0x0135AE, 0x0135BC, "HndAreaMonster", HndAreaMonster, CT_SP},   /* handlers.c: eight handlers; */
    {0x0135BC, 0x0135CA, "HndHopperOr", HndHopperOr, CT_SP},         /* these two carry on elsewhere */
    {0x0135CA, 0x01369A, "HndGuardian", HndGuardian, CT_SP},
    {0x01369A, 0x0136A6, "HndVanishing", HndVanishing, CT_SP},
    {0x018910, 0x01898C, "HndFlyer", HndFlyer, CT_SP},
    {0x0194AA, 0x019544, "HndThrownLater", HndThrownLater, CT_SP},
    {0x01766C, 0x0176E6, "HndCarried", HndCarried, CT_SP},
    {0x019810, 0x01985A, "HndPacer", HndPacer, CT_SP},
    {0x017EE6, 0x017FEC, "HndDarter", HndDarter, CT_SP},           /* batch 6 */
    {0x019C86, 0x019D82, "HndDiver", HndDiver, CT_SP},             /* with the burst $19D60 */
    {0x01A0B0, 0x01A196, "HndSeeker", HndSeeker, CT_SP},
    {0x019544, 0x019612, "HndGrower", HndGrower, CT_SP},
    {0x0140F2, 0x0140FC, "Act140F2", Act140F2, CT_SP},           /* actions.c: the monsters' actions */
    {0x0140FC, 0x01410E, "Act140FC", Act140FC, CT_SP},           /* (ranges to the next entry of $13DB6) */
    {0x01410E, 0x014120, "Act1410E", Act1410E, CT_SP},
    {0x014120, 0x01416E, "Act14120", Act14120, CT_SP},
    {0x01416E, 0x0141A4, "Act1416E", Act1416E, CT_SP},
    {0x0141A4, 0x01429E, "Act141A4", Act141A4, CT_SP},           /* with the appearing helpers $14202-$1429C */
    {0x01429E, 0x0142D6, "Act1429E", Act1429E, CT_SP},
    {0x0142D6, 0x0142E4, "Act142D6", Act142D6, CT_SP},
    {0x0142E4, 0x0142F6, "Act142E4", Act142E4, CT_SP},
    {0x0142F6, 0x014308, "Act142F6", Act142F6, CT_SP},
    {0x014308, 0x01436C, "Act14308", Act14308, CT_SP},           /* with the fade $1432C */
    {0x01436C, 0x014394, "Act1436C", Act1436C, CT_SP},           /* hurt (batch 8) */
    {0x014394, 0x0143BC, "Act14394", Act14394, CT_SP},
    {0x0143BC, 0x0143F2, "Act143BC", Act143BC, CT_SP},
    {0x0143F2, 0x014420, "Act143F2", Act143F2, CT_SP},
    {0x014420, 0x014456, "Act14420", Act14420, CT_SP},
    {0x014456, 0x014518, "Act14456", Act14456, CT_SP},           /* with the hurt frame $144D4-$14516 */
    {0x014518, 0x01454C, "Act14518", Act14518, CT_SP},           /* walking, hovering, shooting (batch 9) */
    {0x01454C, 0x0145C4, "Act1454C", Act1454C, CT_SP},
    {0x0145C4, 0x01463C, "Act145C4", Act145C4, CT_SP},
    {0x01463C, 0x014682, "Act1463C", Act1463C, CT_SP},
    {0x014682, 0x0146FC, "Act14682", Act14682, CT_SP},
    {0x0146FC, 0x01476C, "Act146FC", Act146FC, CT_SP},
    {0x01476C, 0x014822, "Act1476C", Act1476C, CT_SP},
    {0x014822, 0x014888, "Act14822", Act14822, CT_SP},           /* choosing (batch 10) */
    {0x014888, 0x01492A, "Act14888", Act14888, CT_SP},
    {0x01492A, 0x014974, "Act1492A", Act1492A, CT_SP},
    {0x014974, 0x0149D0, "Act14974", Act14974, CT_SP},
    {0x0149D0, 0x014A82, "Act149D0", Act149D0, CT_SP},
    {0x014A82, 0x014AEA, "Act14A82", Act14A82, CT_SP},
    {0x014AEA, 0x014AFA, "Act14AEA", Act14AEA, CT_SP},
    {0x014AFA, 0x014B7E, "Act14AFA", Act14AFA, CT_SP},
    {0x014B7E, 0x014C1A, "Act14B7E", Act14B7E, CT_SP},
    {0x014C1A, 0x014C50, "Act14C1A", Act14C1A, CT_SP},
    {0x014C50, 0x014CA2, "Act14C50", Act14C50, CT_SP},           /* patrolling and jumping (batch 11) */
    {0x014CA2, 0x014CF0, "Act14CA2", Act14CA2, CT_SP},
    {0x014CF0, 0x014D74, "Act14CF0", Act14CF0, CT_SP},
    {0x014D74, 0x014DC8, "Act14D74", Act14D74, CT_SP},
    {0x014DC8, 0x014E1A, "Act14DC8", Act14DC8, CT_SP},
    {0x014E1A, 0x014E6C, "Act14E1A", Act14E1A, CT_SP},
    {0x014E6C, 0x014EC8, "Act14E6C", Act14E6C, CT_SP},
    {0x014EC8, 0x014EF4, "Act14EC8", Act14EC8, CT_SP},
    {0x014EF4, 0x014F28, "Act14EF4", Act14EF4, CT_SP},
    {0x014F28, 0x014F54, "Act14F28", Act14F28, CT_SP},
    {0x014F54, 0x014F80, "Act14F54", Act14F54, CT_SP},           /* with the steered jump $14F66 */
    {0x014F80, 0x014FD2, "Act14F80", Act14F80, CT_SP},           /* with the jump's frame $14FBC */
    {0x014FD2, 0x014FFE, "Act14FD2", Act14FD2, CT_SP},           /* leaping and attacking (batch 12) */
    {0x014FFE, 0x015010, "Act14FFE", Act14FFE, CT_SP},
    {0x015010, 0x015066, "Act15010", Act15010, CT_SP},           /* with the leap $15020 */
    {0x015066, 0x0150D8, "Act15066", Act15066, CT_SP},
    {0x0150D8, 0x015126, "Act150D8", Act150D8, CT_SP},
    {0x015126, 0x0151A0, "Act15126", Act15126, CT_SP},           /* $15182 (the shot) is not an action: translated */
    {0x0151A0, 0x0151D6, "Act151A0", Act151A0, CT_SP},
    {0x0151D6, 0x0152A0, "Act151D6", Act151D6, CT_SP},
    {0x0152A0, 0x015352, "Act152A0", Act152A0, CT_SP},
    {0x015352, 0x0153BE, "Act15352", Act15352, CT_SP},           /* more attacks, a dive (batch 13) */
    {0x0153BE, 0x0153F0, "Act153BE", Act153BE, CT_SP},
    {0x0153F0, 0x015464, "Act153F0", Act153F0, CT_SP},
    {0x015464, 0x0154B2, "Act15464", Act15464, CT_SP},
    {0x0154B2, 0x015504, "Act154B2", Act154B2, CT_SP},
    {0x015504, 0x015576, "Act15504", Act15504, CT_SP},
    {0x015576, 0x0155E2, "Act15576", Act15576, CT_SP},
    {0x0155E2, 0x015630, "Act155E2", Act155E2, CT_SP},           /* swimming, flying, hopping (batch 14) */
    {0x015630, 0x015704, "Act15630", Act15630, CT_SP},
    {0x015704, 0x01577A, "Act15704", Act15704, CT_SP},
    {0x01577A, 0x0157A6, "Act1577A", Act1577A, CT_SP},
    {0x0157A6, 0x0157A8, "Act157A6", Act157A6, CT_SP},           /* rts */
    {0x0157A8, 0x0157E4, "Act157A8", Act157A8, CT_SP},
    {0x0157E4, 0x015830, "Act157E4", Act157E4, CT_SP},
    {0x015830, 0x01589A, "Act15830", Act15830, CT_SP},
    {0x01589A, 0x0158E6, "Act1589A", Act1589A, CT_SP},
    {0x0158E6, 0x015994, "Act158E6", Act158E6, CT_SP},
    {0x015994, 0x0159FA, "Act15994", Act15994, CT_SP},           /* to its end: $159FA-$15A85 are the shot's helpers */
    {0x015A86, 0x015C9E, "Act15A86", Act15A86, CT_SP},           /* Gragg & Glagg (batch 15) */
    {0x015C9E, 0x015DD2, "Act15C9E", Act15C9E, CT_SP},
    {0x015DD2, 0x015E0E, "Act15DD2", Act15DD2, CT_SP},           /* to its end: $15E10/$15E2E are called */
    {0x015E54, 0x015EE6, "Act15E54", Act15E54, CT_SP},
    {0x015EE6, 0x015F9C, "Act15EE6", Act15EE6, CT_SP},
    {0x015FB0, 0x01602A, "Act15FB0", Act15FB0, CT_SP},           /* $15F9C, $1600C are C; $1602A is called */
    {0x016D70, 0x016E56, "Act16D70", Act16D70, CT_SP},           /* on the water (batch 16) */
    {0x016ED0, 0x016F24, "Act16ED0", Act16ED0, CT_SP},           /* $16E56-$16ECE, $16F24-$16F4F: helpers */
    {0x016F50, 0x016FD8, "Act16F50", Act16F50, CT_SP},
    {0x016FF0, 0x017014, "Act16FF0", Act16FF0, CT_SP},
    {0x00EE70, 0x00EF12, "PlayerSwordTip", PlayerSwordTip, CT_SP},
    {0x011170, 0x01141A, "MagicStart", MagicStart, CT_SP},       /* magic.c; a potion may carry on at $F288 */
    {0x01141A, 0x0114C0, "MagicFrame", MagicFrame, CT_SP},       /* may carry on at $11590 or $11780 off a pass */
    {0x0114C0, 0x011568, "MagicShieldHit", MagicShieldHit, CT_SP},
    {0x0115BC, 0x0115DE, "QuakeShaking", QuakeShaking, CT_SP},   /* resume points: on a task pass only */
    {0x0115DE, 0x011626, "QuakeRocks", QuakeRocks, CT_SP},
    {0x011626, 0x01163E, "QuakeSettling", QuakeSettling, CT_SP},
    {0x01163E, 0x011682, "QuakeEnding", QuakeEnding, CT_SP},
    {0x011792, 0x011822, "ThunderFlashes", ThunderFlashes, CT_SP},
    {0x003F58, 0x003F70, "ProbeTileBelow", ProbeTileBelow},
    {0x003F74, 0x003F8E, "ProbeTileAbove", ProbeTileAbove},
    {0x003F92, 0x003FAC, "ProbeTileFeet", ProbeTileFeet},
    {0x003FAC, 0x003FB0, "ProbeTileAt", ProbeTileAt},
    {0x003FB0, 0x003FC2, "ProbeTileAtA3", ProbeTileAtA3},
};
const int port_routine_count = sizeof port_routines / sizeof port_routines[0];
