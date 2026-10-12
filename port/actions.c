/* The monsters' action routines, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The
 * enemies").
 *
 * A monster (group 0) runs the state machine in enemies.c: its kind and state (STEP, 0-8) pick
 * one of the 135 actions in the table at $13DB6, and EnemyDispatch jumps there (d0 = the action
 * number * 4, d1 = the state * 2, a0 = the action). An action returns to RunEnemies, or goes on
 * into other code as the original's bra/jmp does (go_on below). Each is named by its address.
 *
 * Batch 7, the first actions ($140F2-$1436B):
 *
 *   appearing (state 0 of most kinds): $140F2 and its variants $140FC (attribute bit 4), $1410E
 *     (remembers the frame), $14120 (in water: half speed, steps 3 or 7), $1416E (makes a
 *     shadow entity first), $141A4 (waits for the player within $60, throws debris as its
 *     animation goes). $14250 is the shared wait: on its first frame it is placed ($14228) and,
 *     if its spawn flags (-$2AFE) say so, shows a puff ($1208 or $120A) until it ends; $14202
 *     then sets it up ($14208) and moves on to the next state (EnemyNextState).
 *   defeated: $1429E (30 frames, then EnemyDefeated), $142D6 and $142E4 (blinking), $142F6 (also
 *     removes its partner, -$2CFE), $14308 (when its animation ends); $1432C fades it out with
 *     the puff when EnemyDefeated left TIMER set.
 *
 * Batch 8, hurt ($1436C-$14517): state 2's actions. The first frame clears the hit records and
 * takes the damage (beaten: state 1), then blinks and is knocked back ($4558); each frame after
 * ($144E8) it can be hit again, and after TIMER frames it goes on. $1436C and $14394 stand,
 * $143BC, $143F2 and $14420 are knocked back and fall, $14456 drifts in water.
 *
 * Batch 9, walking, hovering and shooting ($14518-$14821): $14518 stands a random while,
 * $1454C the same until $168 frames old, then three shots and gone; $145C4 hovers and swoops
 * (state 7); $1463C walks; $14682 stands shooting; $146FC hops, shooting once per hop; $1476C
 * rises $60 and waits for the player below. Most call EnemyCheckHit ($43BC) and
 * DespawnIfOffscreen ($48C4), which leave past the action: hit() and despawned() do their
 * tests in C and the action returns.
 *
 * Batch 10, choosing ($14822-$14C4F): standing until its wait is over ($42AA, which leaves past
 * the action otherwise), then a step from a table by how near the player is ($428C): $14822,
 * $14888 (with the shield), $1492A, $14974, $14AEA/$14AFA (a range of their own), $14B7E (water
 * rows); $149D0 walks home; $14A82 turns to a player it faces; $14C1A waits, then falls.
 *
 * Batch 11, patrolling and jumping ($14C50-$14FD1): walks for a while, turning round at walls
 * ($4428), touches ($4444), the ends of floors and platforms ($462A) and a range from home
 * ($45DC), each of which leaves past the action when it turns: $14C50, $14CA2, $14CF0 (away
 * from a player close by; jumps a wall where it can: state 5, the current one pending), $14D74,
 * $14DC8 (with the shield), $14E1A (runs), $14E6C (charges until something is in the way);
 * jumps until landing: $14EC8 (up), $14EF4, $14F28 (slow and high in water), $14F54, $14F80.
 *
 * Batch 12, leaping and attacking ($14FD2-$15351): $14FD2 floats at the top of a jump; $14FFE
 * and $15010 leap at the player ($15020); $15066 hops three times, then leaps; $150D8, $15126
 * and $151A0 attack (jumping, hopping, standing), firing when the animation marks it (attribute
 * bit 0); $151D6 walks at the player with the shield and attacks close by; $152A0 hops 4-7
 * times.
 *
 * Batch 13, more attacks and a dive ($15352-$155E1): $15352 and $154B2 attack with their kind's
 * hit records ($15A4E); $153BE plays its animation; $153F0 fires a shot it keeps 60 frames (gone
 * with it if hit); $15464 shoots 8 (or 16) times; $15504 hops and turns to attack; $15576 dives to
 * the target's height and flies back up home.
 *
 * Batch 14, swimming, flying and hopping ($155E2-$159F9): $155E2 swims after the target;
 * $15630 swims about it (jumps out of the ground first, bites from just above); $15704 bobs
 * about a moving height; $1577A stands 20 frames; $157A6 does nothing; $157A8 and $157E4 hop
 * three times; $15830 flies back home; $1589A faces the player, then charges; $158E6 climbs up
 * and down its thread; $15994 walks to a wall or a stop, then hops.
 *
 * Batch 15, Gragg & Glagg ($15A86-$16059): $15A86 appears with its companions and opens the
 * fight; $15C9E is defeated; $15DD2 is hurt; $15E54 waits and fires; $15EE6 rises with an object
 * and sinks back; $15FB0 rises and sinks when told. $15F9C (called back) and $1600C (the touch,
 * hit outside the boss rooms) leave past the action; the group's other helpers are called.
 * Where the original waits for ever (no free group record or entity), the C stops with a message.
 *
 * Batch 16, on the water ($16D70-$17013): $16D70 swims along the surface; $16ED0 is hurt;
 * $16F50 swims burning what it touches and leaps at the player; $16FF0 is the leap. $16E8E (the
 * touch, which hurts the player by 20 less its armour, and the hit) leaves past the action.
 *
 * Batch 17, the hopper and the chain ($17014-$173A9): $17014 crouches and springs at the player;
 * the chain of kinds $2E/$34 ($17132: followers trace the link ahead, the head wanders, grows the
 * chain and removes it when it is all off the screen, leaving past the action): $1729A a link
 * defeated (the chain closes up), $17310 hurt, $17362 moving.
 *
 * Batch 18, bouncers, a shooter and a dodger ($173AA-$1765F): $173AA drifts and bounces; $1743C
 * flies at the target and bounces; $174E6 fires three times; $17548 aims a shot and $17588 flies
 * it; $1759A swims burning and dodges the player's sword.
 *
 * Spawn flags (-$2AFE): bit 7 puff on appearing, bit 1 the other puff, bit 2 turned over
 * (attribute bit 7), bit 0 not placed by its animation.
 *
 * The routines this calls keep running on the caller's registers (`c` below), as the original
 * does. */
#include <stdio.h>
#include <stdlib.h>
#include "world.h"

#define FRAME         0xFFFF965Au
#define WORLD_FLAGS   0xFFFF966Bu   /* bit 0: in water */

#define ALIVE(e)      ((e) - 0x4000)   /* bit 7 in use; 6 drawn; 5; 0 on screen. Word: with its neighbour */
#define ATTR(e)       ((e) - 0x3FFE)   /* bit 3 facing left; 4; 7 turned over */
#define BLINK(e)      ((e) - 0x3FFD)   /* bit 6 blinking; bit 3 */
#define ANIM_SET(e)   ((e) - 0x3D00)   /* word: set, group */
#define ANIM_RATE(e)  ((e) - 0x3CFD)
#define ANIM_BUSY(e)  ((e) - 0x3A00)
#define ANIM_STEP(e)  ((e) - 0x39FE)   /* word */
#define X(e)          ((e) - 0x3800)
#define Y(e)          ((e) - 0x3700)
#define HALF_H(e)     ((e) - 0x34FD)
#define SPEED_X(e)    ((e) - 0x3300)
#define SPEED_Y(e)    ((e) - 0x32FE)
#define WORD_31FE(e)  ((e) - 0x31FE)
#define MOVE_FLAGS(e) ((e) - 0x2F00)
#define FRAME_SEEN(e) ((e) - 0x2C00)   /* byte: its animation's frame counter */
#define LINK2(e)      ((e) - 0x2CFE)   /* word: its partner entity; or a frame number */
#define STEP(e)       ((e) - 0x2BFF)   /* the state; bit 7 once begun */
#define TYPE(e)       ((e) - 0x2BFE)   /* word: group, kind */
#define LINK(e)       ((e) - 0x2B00)   /* word: its spawn point */
#define SPAWN(e)      ((e) - 0x2AFE)   /* byte: spawn flags */
#define HIT_A(e)      ((e) - 0x2A00)   /* long: hit records, cleared when it takes damage */
#define HIT_B(e)      ((e) - 0x2900)   /* long */
#define TIMER(e)      ((e) - 0x2800)
#define TIMER2(e)     ((e) - 0x27FF)
#define SAVED(e)      ((e) - 0x27FE)   /* word: the animation set while the puff shows */
#define VAR(e)        ((e) - 0x27FE)   /* byte: a flag of the action's */
#define SHOT(e)       ((e) - 0x27FD)   /* byte: bit 0, shot this hop */
#define WORD_3DFE(e)  ((e) - 0x3DFE)
#define VEL_X(e)      ((e) - 0x3600)
#define VEL(e)        ((e) - 0x3600)   /* long: x, y */
#define VEL_Y(e)      ((e) - 0x35FE)
#define ACCEL_X(e)    ((e) - 0x3400)
#define COLL(e)       ((e) - 0x3100)
#define HOME_Y(e)     ((e) - 0x20FE)
#define HOME_X(e)     ((e) - 0x2100)
#define SPEED(e)      ((e) - 0x3300)   /* long: x, y speeds */
#define ANIM_ID(e)    ((e) - 0x3CFE)
#define GRAVITY(e)    ((e) - 0x33FE)   /* word: its ACCEL_Y */
#define TOUCH(e)      ((e) - 0x2AFD)
#define NEXT_STATE(e) ((e) - 0x2200)   /* byte: a state to go to next, or $FF */
#define STRENGTH(e)   ((e) - 0x2700)   /* word */
#define STRENGTH2(e)  ((e) - 0x2400)   /* word: the strength of its attack */
#define GOAL(e)       ((e) - 0x2D00)   /* word: the guardian's group record */
#define MAKER(e)      ((e) - 0x2CFE)   /* word: a byte to set when it is gone */
#define HALF_W(e)     ((e) - 0x34FE)
#define DELAY(e)      ((e) - 0x25FE)
#define ARMOUR(e)     ((e) - 0x26FE)   /* word: the player's armour */
#define PHASE(e)      ((e) - 0x31FF)   /* byte: an action's own phase; a chain link's number */
#define FLASH(e)      ((e) - 0x3900)   /* word */
#define CHAIN_A(e)    ((e) - 0x30FF)   /* byte: a link set up */
#define CHAIN_B(e)    ((e) - 0x30FE)   /* byte: a link with a leader */
#define ACCEL(e)      ((e) - 0x3400)   /* long: x, y */

/* routines still called as they are */
#define SET_ANIM          0x7E8u
#define SET_ANIM_NOW      0x7FCu
#define TYPE_AT_PIXEL     0x30CEu   /* d2: the collision type at d6, d7 */
#define MAKE_PARTNER      0x41A2u   /* a2 becomes its partner (-$2CFE both ways), cleared */
#define SPAWN_DEBRIS      0x42E8u
#define START_BLINK       0x451Au
#define FACE_PLAYER       0x44D6u
#define DIST_X_TO_PLAYER  0x455Eu   /* d1: the distance */
#define ENEMY_DEFEATED    0x482Cu
#define FIND_FREE_ENTITY  0x12812u  /* a2; mi: none */
#define ENEMY_NEXT_STATE  0x13542u
#define ENEMY_SET_STATE   0x1357Eu  /* d0: the state; runs its action */
#define RANDOM            0x5D8u
#define ACCEL_Y           0xB44u
#define FRICTION_X        0xB92u
#define MOVE_WITH_PLATFORMS 0x2F8Eu
#define BUBBLE            0x4218u   /* an item ($204 or $210) at its top */
#define SLOW_ON_GROUND    0x4356u
#define CHECK_GROUNDED    0x4362u   /* z: on the ground */
#define ENEMY_TAKE_HIT    0x439Eu   /* mi: hit */
#define START_BLINK_FROZEN 0x4514u
#define KNOCK_BACK_AT     0x453Cu   /* d0: the speed */
#define KNOCK_BACK        0x4558u   /* at $400 ($100 on the ground) */
#define ENEMY_MOVE        0x46DAu
#define PLAYER_TOUCH      0x4778u   /* the player (or the shield) against it */
#define DAMAGE_ENEMY      0x4986u   /* z: beaten */
#define APPLY_VEL         0xAB4u
#define BOB_TOWARD        0x4274u   /* ACCEL_Y toward the height d0, give or take d1, by d2 */
#define CONTACT_AND_MOVE  0x42C4u   /* touching the player, then falling and moving */
#define RANDOM_TIMER      0x43CCu   /* TIMER: $40 + up to $7F */
#define RANDOM_TIMER_32   0x43DEu   /* TIMER: $20 + up to $1F; d0 the random */
#define RANDOM_DELAY      0x43F0u   /* TIMER2: $10 + up to $1F */
#define FACE_VELOCITY     0x44A6u
#define JUMP              0x44F4u   /* VEL_Y up by SPEED_Y + $80 */
#define DIST_X_TO_ENTITY  0x4562u   /* d1: to a2 */
#define DIST_Y_TO_TARGET  0x4576u   /* a2 = $FFA11C's entity; d1 = its y - this one's (flags of sub.w) */
#define PLAYER_CONTACT_ALL 0x4792u  /* touching, and the sword */
#define DESPAWN           0x4814u
#define SHOOT             0x15182u
#define APPLY_VEL_Y       0xAD6u
#define MAYBE_BUBBLE      0x41F0u   /* in water, now and then */
#define SHIELD_AND_MOVE   0x42CCu   /* the shield's touch, then falling and moving */
#define BACK_OFF          0x437Eu   /* touched: VEL_X backwards at twice its speed */
#define WALK_FORWARD      0x4492u
#define FACE_X            0x44DEu   /* towards x d0 */
#define DIST_Y_ABS_TARGET 0x4584u   /* a2 = $FFA11C's entity; d1 = |its y - this one's| */
#define DIST_Y_ABS        0x4588u   /* the same to a2 */
#define KIND_STATS        0x15A3Au  /* a1: its kind's record in $1381C */
#define KIND_MOVES        0x15A76u  /* -$2900: its moves, by d0 */
#define ACCEL_X_          0xAF4u
#define RANGE_40          0x4402u   /* WORD_31FE: $40 + up to $3F */
#define RANGE_80          0x4414u   /* WORD_31FE: $80 + up to $7F */
#define FACE_AWAY         0x445Cu   /* faces away from its touch, walks on */
#define TURN              0x448Cu   /* reverses its facing, walks on */
#define SET_VEL_X_FACING  0x4496u   /* VEL_X: d0, by its facing */
#define SET_VEL_Y         0x4508u   /* VEL_Y: d0, off the ground */
#define ROOM_ABOVE        0x45ACu   /* nz: room to jump up the wall ahead */
#define PAST_RANGE        0x45F4u   /* cc: as far from home as d2 */
#define TARGET_FROM_HOME  0x4616u   /* cs: $FFA11C's entity within d2 of its home */
#define EDGE_AHEAD        0x4646u   /* its platform: 0 at its end, mi off one */
#define GROUND_AHEAD      0x4686u   /* 0: no floor ahead */
#define RESET_ANIM_TIMER  0x7F4u
#define TURN_AT_WALL      0x4438u   /* turns at a wall, and returns (no leaving past) */
#define HOP               0x4500u   /* VEL_Y up by half SPEED_Y */
#define ATTACK_FRAME      0x15A4Eu  /* -$2A00: the hit records of its attack d0, by animation frame */
#define QUEUE_SOUND       0x366u
#define ACCEL_XY          0xB42u
#define FRICTION_Y        0xBACu
#define SHIELD_CONTACT    0x46F4u   /* touching the player, or the shield */
#define TARGET_W          0xFFFFA11Cu
#define VEL_TOWARD        0x1234u   /* VEL at speed d4 towards a2 */
#define ACCEL_TOWARD      0x1248u   /* d0, d5: an acceleration d4 towards a2 */
#define ACCEL_TO          0x127Au   /* d4, d5: an acceleration d2 along (d0, d1) */
#define SWIM_LIMIT        0x425Cu   /* its speed held to SPEED (the original's $425C) */
#define FLY_FACING        0x46BEu   /* ACCEL_X by its facing (the original's $46BE) */
#define CLEAR_ENTITY      0xA36u
#define SET_MAP_BLOCKS    0x2812u   /* d0 x d1 blocks from d2 at (d6, d7) */
#define SHOW_TILES        0x2DFEu
#define PUFF_A            0x42D4u   /* a puff ($1208) about it, sound $3F; a2 */
#define PUFF_B            0x42DAu   /* the same, $120A */
#define FALL_AND_MOVE     0x46D6u
#define FREEZE_ENEMIES    0x47D2u
#define ROOM_KIND         0x4910u   /* a0: kind d1's entry in the room's list (waits for ever without) */
#define SPAWN_ITEM_AT     0x10910u
#define GROUP_SLOT        0x127FEu  /* a3: a free group record; mi: none */
#define GROUP_TO_IT       0x15C84u  /* its first companion (a2) moved to it */
#define PARTNER_DOWN      0x15DA4u  /* the entity d0 to step 6 */
#define GROUP_BLINK       0x15E10u
#define GROUP_UNBLINK     0x15E2Eu
#define GLIDE_HOME        0x1602Au  /* z: at its home height */
#define SPAWN_FROM_A394   0x1A438u  /* an enemy from the record at $FFA394; a2, mi: none */
#define SHAKE             0xFFFF9738u
#define G_COUNT6          0xFFFFA106u
#define G_MODE            0xFFFFA107u
#define G_LAST            0xFFFFA10Cu
#define G_FLAGS           0xFFFFA10Fu
#define G_OPEN            0xFFFFA111u
#define G_WORD            0xFFFFA112u
#define TEXT_BUSY         0xFFFF9962u
#define MESSAGE           0xFFFF9C16u
#define AREA              0xFFFF9668u
#define BODY_VS_ATTACK    0xD76u
#define BODY_OVERLAP      0xE9Au    /* ls: touching a2 */
#define TILE_ATTR_AT      0x30DAu
#define PAST_RANGE_80     0x45F0u   /* cc: $80 or more from home */
#define ACCEL_FACING      0x46C6u   /* ACCEL_X d0, by its facing */
#define TO_HEIGHT         0x16E72u  /* ACCEL_Y towards the height d0 */
#define FACE_BY_D0        0x16F24u  /* faces left (and d2 negated) if d0's low byte is negative */
#define BURNING           0x16F3Au  /* its burning look (flashing), animation d0 */
#define TURN_IN_RANGE     0x16FD8u  /* turns at $20 from home; a new wait */
#define PLAYER_HARM       0xFFFF9856u
#define PLAYER_W          0xFFFF9EEEu
#define TO_X              0x16E56u  /* ACCEL_X towards x d0 */
#define HOPPER_START      0x17118u  /* $17014's start: speeds $100, phase 0, animation 6, a wait */
#define SWING_X           0x170F4u
#define SWING_Y           0x170EEu
#define COPY_FIELDS       0x1730Au  /* fields from a4 to a2, by the list of offsets at a0 */
#define STOP              0xC2Eu    /* VEL 0; d0 0 */
#define VEL_AT_COARSE     0xFC0u    /* d0, d5: the velocity at angle d3, speed d4 */
#define SET_VEL_AT        0x1208u   /* VEL at angle d3, speed d4 */
#define FACE_ENTITY       0x44DAu   /* faces a2; d0: the distance */
#define BOUNCE            0x17420u  /* off walls (COLL): VEL reversed */
#define AIM_LEVEL         0x174B0u  /* d3: the angle to a2, kept within an eighth of level */
#define SHOT_AT           0x18256u  /* an enemy d0 at the offsets at a0 (by its facing); a2, mi: none */
#define QUEUE_SOUND_FF    0x364u    /* sound $FF: stop */
#define VELOCITY_TOWARD   0x44BCu   /* VEL_X: (a2's x - its x) * 256 / d1 */
#define SPAWN_KIND        0x12650u  /* a2 made kind d1 at d6, d7, spawn point d0 */
#define BOSS_MARK         0xFFFF9748u   /* word: the room's boss; byte $FF once beaten */
#define SOUND_MODE        0xFFFF9668u   /* byte: $12 keeps the music */
#define BODY_W(e)     ((e) - 0x3500)   /* byte: its body's half width */
#define BODY_H(e)     ((e) - 0x34FF)   /* byte: its body's half height */
#define UPDATE_ANIM       0x80Eu
#define PAIR_ANIM         0x179AAu  /* the pair's animations d0, d0+1 ($179C2), partner first */
#define PAIR_TOUCH        0x179DEu  /* the partner takes its health; the player against it */
#define PAIR_HOVER        0x179F8u  /* the body bobs to its height; the head on it */
#define PAIR_ON_BODY      0x17A26u  /* a0 the partner; its y $2C above the partner's */
#define PAIR_ALIGN_X      0x17A38u  /* the partner's x its own */
#define PAIR_FALL         0x17B76u  /* the partner (a4) falls a frame; z: TIMER run out */
#define PAIR_FALL_HERE    0x17B7Au  /* it falls a frame; z: TIMER run out */
#define FADE_TILES        0x17C84u  /* a strip of its tiles faded, while TIMER2 < $11 */
#define BREAK_UP_START    0x18290u  /* d2 tile rows marked in the scroll table */
#define BREAK_UP          0x182AAu  /* tile d1 of d2 rows cleared in VRAM */
#define MOUNT_ANIM        0x18314u  /* the mount's animation d0 */
#define MOUNT_FALL        0x18322u  /* the mount falls and moves a frame; then MOUNT_PLACE */
#define MOUNT_PLACE       0x18332u  /* the rider on its mount, by the mount's animation ($18372) */
#define MOUNT_TOUCH       0x1839Cu  /* the mount takes its health; the player against it */
#define MOUNT_GROUNDED    0x183BAu  /* a2 the mount; z: on the ground (COLL & 10 is 2) */
#define SHAKE_SCREEN      0x183D4u  /* the screen shaken ($9738), sound 57 */
#define ENEMY_MAX_HEALTH  0x4928u   /* d0: its kind's health */
#define HEALTH(e)     ((e) - 0x2600)   /* word */
#define RIDE_ANIM         0x18A24u  /* the beast's animation d0 */
#define RIDE_FALL         0x18A32u  /* the beast falls and moves a frame; then RIDE_PLACE */
#define BLINK_BOTH        0x18A74u  /* the beast and the rider start blinking */
#define RIDE_TOUCH        0x18A96u  /* the beast takes its health; the player against it */
#define RIDE_GROUNDED     0x18AC2u  /* a2 the beast; z: on the ground (COLL & 10 is 2) */
#define RIDE_BLOCKED      0x18AD2u  /* a2 the beast; nz: blocked (COLL bit 0) */
#define START_EFFECT      0x5658u   /* an effect slot ($8C58) started: d0 */
#define GAME_FLAG_BIT     0x2758u   /* flag d0: bit d0 of byte d1 at a0 ($9992) */
#define BECOME_KIND       0x1274Au  /* made kind d1 (group, kind) */
#define FACE_PLAYER_AND_WALK 0x44D0u
#define MOUNT_BLOCKED     0x183C8u  /* a2 the mount; nz: blocked (COLL bit 0) */
#define ANGLE_OF          0x109Cu   /* d3: the angle of (d0, d1) */
#define VEL_OF_ANGLE      0xFA8u    /* d0, d5: angle d3 at speed d4 */
#define COPY_TO_A2        0x41AAu   /* a2 cleared and given its position, home and looks */
#define RIDE_PLACE        0x18A3Eu  /* on its partner, facing as it does, Y_OFFSET above */
#define Y_OFFSET(e)   ((e) - 0x36FD)   /* byte: its height on its partner */
#define APPLY_VEL_X       0xAB8u

void EnemySetState(Cpu *);           /* enemies.c */

static Cpu *c;                        /* the running registers */
static int went_on;                   /* go_on was called: c->ret holds where */

static void call(uint32_t addr) { call_68k(c, addr); }
static void call_d0(uint32_t addr, uint32_t d0) { c->d[0] = d0; call_68k(c, addr); }   /* moveq #d0 */
static void set_d0w(int v) { c->d[0] = set_w(c->d[0], (uint32_t)v); }
static int  begins(Ent e) { uint8_t s = rd8(STEP(e)); wr8(STEP(e), s | 0x80); return !(s & 0x80); }

/* ---- appearing ---- */

/* $14228: placed: turned over by its spawn flags, and standing on its spawn point */
static void place(Ent e)
{
    if (rd8(SPAWN(e)) & 4) set8(ATTR(e), 0x80);
    if (rd8(SPAWN(e)) & 1) return;
    call_d0(SET_ANIM_NOW, 0);
    c->d[0] = rd8(HALF_H(e));
    wrw(Y(e), rdw(Y(e)) - (int)c->d[0]);
}

/* the puff's animation set by the spawn flags (bit 1) */
static void puff_set(Ent e)
{
    set_d0w(rd8(SPAWN(e)) & 2 ? 0x120A : 0x1208);
    wrw(ANIM_SET(e), (int)(c->d[0] & 0xFFFF));
}

/* $14250: 1 once it has appeared (at once, or when its puff ends) */
static int appeared(Ent e)
{
    if (begins(e)) {
        place(e);
        if (!(rd8(SPAWN(e)) & 0x80)) return 1;
        wrw(SAVED(e), rdw(ANIM_SET(e)));
        puff_set(e);
        set8(BLINK(e), 8);
        call_d0(SET_ANIM_NOW, 0);
        set8(ALIVE(e), 0x40);
    }
    if (rd8(ANIM_BUSY(e))) return 0;
    wrw(ANIM_SET(e), rdw(SAVED(e)));
    c->d[0] = 0;
    return 1;
}

/* $14208: shown, facing the player, moving */
static void set_up(Ent e)
{
    clr8(BLINK(e), 8);
    c->d[0] = 0;
    wrw(WORD_31FE(e), 0);
    call(SET_ANIM_NOW);
    set8(ALIVE(e), 0x40);
    set8(MOVE_FLAGS(e), 3);
    call(FACE_PLAYER);
}

/* $14202: set up, and on to the next state */
static void begin_life(Ent e)
{
    set_up(e);
    call(ENEMY_NEXT_STATE);
}

static void a140F2(Ent e) { if (appeared(e)) begin_life(e); }

static void a140FC(Ent e)
{
    if (!appeared(e)) return;
    set8(ATTR(e), 0x10);
    begin_life(e);
}

static void a1410E(Ent e)
{
    if (!appeared(e)) return;
    wrw(LINK2(e), rdw(FRAME));
    begin_life(e);
}

/* in water: half its speed, up as well as along, and step 3 or 7 by what it stands in */
static void a14120(Ent e)
{
    if (!appeared(e)) return;
    if (!(rd8(WORLD_FLAGS) & 1)) { begin_life(e); return; }
    wrw(SPEED_X(e), rdw(SPEED_X(e)) >> 1);
    set_d0w(rdw(SPEED_X(e)));
    wrw(SPEED_Y(e), rdw(SPEED_X(e)));
    set_up(e);
    set8(MOVE_FLAGS(e), 0x40);
    c->d[6] = set_w(c->d[6], (uint16_t)rdw(X(e)));
    c->d[7] = rd8(HALF_H(e));
    c->d[7] = set_w(c->d[7], (c->d[7] + (uint16_t)rdw(Y(e))) & 0xFFFF);
    call(TYPE_AT_PIXEL);
    c->d[0] = c->d[2] & 8 ? 3 : 7;
    wr8(STEP(e), (uint8_t)c->d[0]);
    set_d0w((int)(c->d[0] * 2));
    call(SET_ANIM);
}

/* with a shadow: a partner entity drawn turned over, a little slower, of no kind */
static void a1416E(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        call(FIND_FREE_ENTITY);
        if (c->ccr & CC_N) return;                  /* none free: tried again next frame */
        call(MAKE_PARTNER);
        Ent s = c->a[2];
        wrw(ANIM_SET(s), 0x162C);
        set8(ATTR(s), 0x80);
        c->d[0] = set_b(c->d[0], (uint8_t)(rd8(ANIM_RATE(e)) + 2));
        wr8(ANIM_RATE(s), (uint8_t)c->d[0]);
        wr8(LINK(s), 0xFF);
        wr8(TYPE(s), 0xFF);
    }
    if (appeared(e)) begin_life(e);
}

/* waits for the player to come within $60, then rises out of the ground throwing debris */
static void a141A4(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        call(DIST_X_TO_PLAYER);
        if ((uint16_t)c->d[1] >= 0x60) return;
        place(e);
        clr8(BLINK(e), 8);
        set8(ALIVE(e), 0x40);
        set8(MOVE_FLAGS(e), 3);
        set8(STEP(e), 0x80);
        wr8(TIMER2(e), 0xFF);
        call(FACE_PLAYER);
        return;
    }
    if (!rd8(ANIM_BUSY(e))) { call(ENEMY_NEXT_STATE); return; }
    c->d[0] = set_b(c->d[0], rd8(FRAME_SEEN(e)));
    if ((uint8_t)c->d[0] == rd8(TIMER2(e))) return;
    wr8(TIMER2(e), (uint8_t)c->d[0]);               /* a new frame: debris */
    c->d[4] = 16; c->d[5] = 1;
    c->d[6] = set_w(c->d[6], 0x1208);
    wrw(Y(e), rdw(Y(e)) + 8);
    call(SPAWN_DEBRIS);
    wrw(Y(e), rdw(Y(e)) - 8);
}

/* ---- defeated ---- */

/* $1432C: faded out with the puff, gone when it ends */
static void fade_out(Ent e)
{
    uint8_t t = rd8(TIMER2(e));
    wr8(TIMER2(e), t | 0x80);
    if (!(t & 0x80)) {
        set8(BLINK(e), 8);
        clr8(ALIVE(e), 0x20);
        clr8(BLINK(e), 0x40);
        puff_set(e);
        call_d0(SET_ANIM, 0);
        return;
    }
    if (!rd8(ANIM_BUSY(e))) wrw(ALIVE(e), 0);
}

/* $142AC: still, not blinking, the defeated animation */
static void lie_still(Ent e)
{
    wrw(ANIM_STEP(e), 0x100);
    clr8(ALIVE(e), 0x20);
    clr8(BLINK(e), 0x40);
    call_d0(SET_ANIM, 2);
}

/* $142C4: TIMER frames, then EnemyDefeated; faded out if it was already 0 */
static void count_down(Ent e)
{
    if (!rd8(TIMER(e))) { fade_out(e); return; }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) call(ENEMY_DEFEATED);
}

static void a1429E(Ent e)
{
    if (!begins(e)) { count_down(e); return; }
    wr8(TIMER(e), 0x1E);
    lie_still(e);
}

static void a142D6(Ent e)
{
    if (begins(e)) call(START_BLINK);
    count_down(e);
}

static void blink_down(Ent e) { call(START_BLINK); call_d0(SET_ANIM, 2); }   /* $142EC */

static void a142E4(Ent e)
{
    if (begins(e)) blink_down(e); else count_down(e);
}

static void a142F6(Ent e)
{
    if (!begins(e)) { count_down(e); return; }
    c->a[2] = (uint32_t)(int32_t)rdw(LINK2(e));     /* its partner goes too */
    wrw(ALIVE(c->a[2]), 0);
    blink_down(e);
}

static void a14308(Ent e)
{
    if (begins(e)) { wr8(TIMER(e), 0); lie_still(e); return; }
    if (rd8(TIMER(e))) { fade_out(e); return; }
    if (rd8(ANIM_BUSY(e))) return;
    wr8(TIMER(e), 0xFF);
    call(ENEMY_DEFEATED);
}

/* ---- hurt (batch 8) ---- */

/* a new state (d0 as the caller left it), as `bra EnemySetState` does: the action of that state
 * runs, and returns to this action's caller. Called from a subroutine (`nested`, reached by bsr
 * in the original), the new action returns into the caller's code instead, which carries on */
static void new_state(int nested)
{
    if (nested) { call(ENEMY_SET_STATE); return; }
    EnemySetState(c);
    went_on = 1;
}

/* the hit records cleared, the damage taken: 0 if that beat it (then state 1, defeated) */
static int take_damage(Ent e)
{
    wr32(HIT_A(e), 0);
    wr32(HIT_B(e), 0);
    call(DAMAGE_ENEMY);
    if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return 0; }
    return 1;
}

/* $144E8: each frame while hurt: touching the player, and hit again (state 1 if that beats it);
 * after TIMER frames it stops blinking and goes on to the next state */
static void hurt_tick(Ent e, int nested)
{
    call(PLAYER_TOUCH);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = set_b(c->d[0], 1); new_state(nested); return; }
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    clr8(BLINK(e), 0x40);
    clr8(ALIVE(e), 0x20);
    call(ENEMY_NEXT_STATE);
}

/* $144E4: moved, then the frame's hurt */
static void hurt_move(Ent e) { call(ENEMY_MOVE); hurt_tick(e, 0); }

/* $144D4: knocked back: slowed on the ground, falling */
static void hurt_knocked(Ent e)
{
    call(CHECK_GROUNDED);
    if (c->ccr & CC_Z) call_d0(FRICTION_X, 64);
    call(ACCEL_Y);
    hurt_move(e);
}

static void a1436C(Ent e)
{
    if (!begins(e)) { hurt_tick(e, 0); return; }
    if (!take_damage(e)) return;
    call(START_BLINK);
    call_d0(SET_ANIM, 4);
}

static void a14394(Ent e)
{
    if (!begins(e)) { hurt_tick(e, 0); return; }
    clr8(BLINK(e), 0x40);
    if (!take_damage(e)) return;
    call(START_BLINK_FROZEN);
}

static void a143BC(Ent e)
{
    if (!begins(e)) { hurt_knocked(e); return; }
    clr8(BLINK(e), 0x40);
    if (!take_damage(e)) return;
    wr8(TIMER(e), 0x1E);
    call(KNOCK_BACK);
    call_d0(SET_ANIM, 4);
}

static void a143F2(Ent e)
{
    if (!begins(e)) { hurt_knocked(e); return; }
    wrw(ANIM_STEP(e), 0x100);
    if (!take_damage(e)) return;
    call(START_BLINK_FROZEN);
    call(KNOCK_BACK);
}

static void a14420(Ent e)
{
    if (!begins(e)) {
        call(SLOW_ON_GROUND);
        call(ACCEL_Y);
        hurt_move(e);
        return;
    }
    if (!take_damage(e)) return;
    call_d0(SET_ANIM, 6);
    call(START_BLINK_FROZEN);
    call(KNOCK_BACK);
}

/* in water: drifts, sometimes dropping a bubble, and comes to rest at step 3 or 7 */
static void a14456(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        if (!take_damage(e)) return;
        call(START_BLINK_FROZEN);
        if (!(rd8(WORLD_FLAGS) & 1)) { call(KNOCK_BACK); return; }
        set_d0w(0x100);
        call(KNOCK_BACK_AT);
        return;
    }
    if (!(rd8(WORLD_FLAGS) & 1)) { hurt_knocked(e); return; }
    c->d[0] = rd8(TIMER(e)) & 15;
    if (!c->d[0]) {
        call(RANDOM);
        c->d[0] = set_w(c->d[0], c->d[0] & 3);
        if (!(c->d[0] & 3)) call(BUBBLE);
    }
    call_d0(FRICTION_X, 32);
    call(ACCEL_Y);
    call(MOVE_WITH_PLATFORMS);
    hurt_tick(e, 1);
    if (rd8(TIMER(e))) return;
    c->d[1] = 3;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) c->d[1] = 7;
    wr8(STEP(e), (uint8_t)c->d[1]);
}

/* ---- walking, hovering, shooting (batch 9) ---- */

/* $43BC, EnemyCheckHit: hit, so state 2 (hurt). The original drops this action's return and
 * jumps to EnemySetState; here it answers 1 and the action returns */
static int hit(void)
{
    call(ENEMY_TAKE_HIT);
    if (!(c->ccr & CC_N)) return 0;
    c->d[0] = 2;
    new_state(0);
    return 1;
}

/* $48C4, DespawnIfOffscreen (objects.c), which also leaves past the action */
static int despawned(void) { return world_despawn_offscreen(c); }

/* $15182: a shot (a2), turned as it is; mi if there was no room for one */
static void shoot(void) { call(SHOOT); }

/* $14518: stands for a random while, then on */
static void stand_frame(Ent e)
{
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (!rd8(TIMER2(e))) call(ENEMY_NEXT_STATE);
}

static void a14518(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
    }
    stand_frame(e);
}

/* $1454C: stands, as $14518, until $168 frames after it appeared (LINK2); then a puff, three
 * shots at once, and gone when the puff ends */
static void a1454C(Ent e)
{
    if (begins(e)) {
        wr8(VAR(e), 0);
        wrw(VEL_X(e), 0);
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
    }
    if (rd8(VAR(e)) & 0x80) {
        if (!rd8(ANIM_BUSY(e))) call(DESPAWN);
        return;
    }
    uint16_t age = (uint16_t)(rdw(FRAME) - rdw(LINK2(e)));
    if (age & 0x8000) age = (uint16_t)-age;
    set_d0w(age);
    if (age < 0x168) { stand_frame(e); return; }
    call(FACE_PLAYER);
    wr8(VAR(e), 0xFF);
    wrw(ANIM_SET(e), 0x1208);
    set8(BLINK(e), 8);
    call_d0(SET_ANIM, 0);
    wr8(TIMER(e), 2);
    do {
        shoot();
        if (c->ccr & CC_N) return;
        Ent s = c->a[2];
        wrw(WORD_3DFE(s), 0);
        wr8(TIMER(s), rd8(TIMER(e)));
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    } while (!(rd8(TIMER(e)) & 0x80));
}

/* $145C4: hovers about its home height, facing the player; when the player is below within $60
 * and $28 to the side, it swoops (state 7) */
static void a145C4(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 14);
        call(RANDOM_DELAY);
        c->d[0] = 0;
        wrw(VEL_X(e), 0);
        wrw(ACCEL_X(e), 0);
        wrw(SPEED_Y(e), 0x100);
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(FACE_PLAYER);
    call_d0(FRICTION_X, 16);
    call(ACCEL_Y);
    call(APPLY_VEL);
    set_d0w(rdw(HOME_Y(e)));
    c->d[1] = 4;
    c->d[2] = set_w(c->d[2], 0x80);
    call(BOB_TOWARD);
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(DIST_Y_TO_TARGET);
    if (c->ccr & CC_C) return;
    if ((uint16_t)c->d[1] >= 0x60) return;
    call(DIST_X_TO_ENTITY);
    if ((uint16_t)c->d[1] > 0x28) return;
    wr8(STEP(e), 7);
}

/* $1463C: walks, facing the player, choosing every TIMER2 frames whether to go on (1 in 4) */
static void a1463C(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
        wr8(TIMER(e), rd8(TIMER2(e)));
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(FACE_PLAYER);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    wr8(TIMER(e), rd8(TIMER2(e)));
    call(RANDOM);
    c->d[0] = set_w(c->d[0], c->d[0] & 3);
    if (!(c->d[0] & 3)) call(ENEMY_NEXT_STATE);
}

/* $14682: stands still shooting, faster up at a player above it, and every other volley
 * faster along */
static void a14682(Ent e)
{
    if (begins(e)) {
        call(RANDOM_TIMER);
        wr8(TIMER2(e), 0);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(FACE_PLAYER);
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    wr8(COLL(e), 2);
    if (rd8(TIMER(e))) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
    }
    if (!(rd8(ALIVE(e)) & 1)) return;
    shoot();
    c->a[0] = (uint32_t)(int32_t)(int16_t)(c->a[2] & 0xFFFF);
    call(DIST_Y_TO_TARGET);
    if ((c->ccr & CC_N) && (int16_t)c->d[1] <= -0x40) {     /* well above it: faster up */
        set_d0w((uint16_t)rdw(SPEED_Y(c->a[0])) >> 1);
        wrw(SPEED_Y(c->a[0]), rdw(SPEED_Y(c->a[0])) + (int16_t)(c->d[0] & 0xFFFF));
    }
    c->a[2] = (uint32_t)(int32_t)(int16_t)(c->a[0] & 0xFFFF);
    c->d[0] = rd8(TIMER2(e)) & 3;
    if (c->d[0]) {
        set_d0w((uint16_t)rdw(SPEED_X(c->a[2])) >> 1);
        wrw(SPEED_X(c->a[2]), rdw(SPEED_X(c->a[2])) + (int16_t)(c->d[0] & 0xFFFF));
    }
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) + 1));
    call(RANDOM_TIMER);
}

/* $146FC: hops (on the ground: a random wait, then a jump), shooting once per hop: on the
 * ground, or (half of them) on the way down */
static void a146FC(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 6);
        wrw(VAR(e), 0);
        call(RANDOM_TIMER_32);
        if (c->d[0] & 4) wr8(VAR(e), 0xFF);
    }
    if (despawned()) return;
    call(FACE_PLAYER);
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (c->ccr & CC_Z) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (!rd8(TIMER(e))) {
            call(RANDOM_TIMER);
            call(JUMP);
            wr8(SHOT(e), 0);
        }
        if (rd8(VAR(e))) return;
    } else {
        if (!rd8(VAR(e))) return;
        if (rdw(VEL_Y(e)) < 0) return;
    }
    if (!(rd8(ALIVE(e)) & 1)) return;
    uint8_t s = rd8(SHOT(e));
    wr8(SHOT(e), s | 1);
    if (!(s & 1)) shoot();
}

/* $1476C: rises $60 above its home, then hangs facing the player until it is below (between
 * $10 and $80 down, $10 to $C0 aside), checked every 8 frames */
static void a1476C(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 0x50);
        c->d[0] = 1;
        if (!rd32(VEL(e))) {
            wr32(VEL(e), 0xFE80u);
            call(FACE_PLAYER);
            c->d[0] = 8;
        }
        wr8(TIMER(e), (uint8_t)c->d[0]);
        set_d0w(rdw(HOME_Y(e)) - 0x60);
        wrw(LINK2(e), (int)(c->d[0] & 0xFFFF));
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 14);
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { wr8(TIMER(e), 1); call(MOVE_WITH_PLATFORMS); }
    if (rdw(VEL_Y(e))) {
        if (rdw(VEL_X(e))) call(FACE_VELOCITY);
        set_d0w(rdw(LINK2(e)));
        if ((uint16_t)rdw(LINK2(e)) < (uint16_t)rdw(Y(e))) return;    /* not up there yet */
    }
    wr32(VEL(e), 0);
    call(FACE_PLAYER);
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    c->d[0] = (uint32_t)rdw(FRAME) & 7;
    if (c->d[0]) return;
    call(DIST_Y_TO_TARGET);
    if (c->ccr & CC_N) return;
    uint16_t dy = (uint16_t)c->d[1];
    if (dy >= 0x80 || dy < 0x10) return;
    call(DIST_X_TO_ENTITY);
    uint16_t dx = (uint16_t)c->d[1];
    if (dx >= 0xC0) return;
    if (dx >= 0x10) call(ENEMY_NEXT_STATE);
}

/* ---- choosing what to do next (batch 10) ---- */

/* $42AA: standing on the ground, facing the player, its wait (TIMER2) over: 0. Otherwise the
 * original drops this action's return (it is done for this frame): 1, and the action returns.
 * The ground test is $4356's, whose flags come from FrictionX when it is on the ground */
static int still_waiting(Ent e)
{
    call(SLOW_ON_GROUND);
    if (!(c->ccr & CC_Z)) return 1;
    call(FACE_PLAYER);
    if (!rd8(TIMER2(e))) return 0;
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    return rd8(TIMER2(e)) != 0;
}

/* $428C: the next step from a table, at d2 (the row: how near the player is) plus a random 0-7:
 * 0 stays, negative goes on to the next state, else that step */
static void pick_step(Ent e, uint32_t table)
{
    c->a[0] = table;
    call(RANDOM);
    c->d[0] = set_w(c->d[0], ((c->d[0] & 7) + c->d[2]) & 0xFFFF);
    uint8_t s = rd8(table + (uint32_t)(int32_t)(int16_t)(c->d[0] & 0xFFFF));
    c->d[0] = set_b(c->d[0], s);
    if (!s) return;
    if (s & 0x80) { call(ENEMY_NEXT_STATE); return; }
    wr8(STEP(e), s);
}

/* the player within $30 up or down and less than `dx` aside: the rows of the tables below */
static int near_target(uint16_t dx)
{
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] >= 0x30) return 0;
    call(DIST_X_TO_ENTITY);
    return (uint16_t)c->d[1] < dx;
}

static void a14822(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    if (!near_target(0x80)) { wr8(STEP(e), 4); return; }
    c->d[2] = (uint16_t)c->d[1] < 0x30 ? 8 : 0;
    pick_step(e, 0x14878);
}

/* with the shield's touch and its own moves (from its kind's stats, $15A3A, $15A76) */
static void a14888(Ent e)
{
    if (begins(e)) {
        wr32(HIT_A(e), 0);
        call(KIND_STATS);
        c->d[0] = set_b(c->d[0], rd8(c->a[1] + 7));
        wr8(TIMER(e), (uint8_t)c->d[0]);
        call(KIND_MOVES);
        call(RANDOM_DELAY);
        call(FACE_PLAYER);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(SHIELD_AND_MOVE);
    if (hit()) return;
    call(BACK_OFF);
    call(SLOW_ON_GROUND);
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    call(FACE_PLAYER);
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] >= 0x30) { wr8(STEP(e), 4); return; }
    call(DIST_X_TO_PLAYER);
    if ((uint16_t)c->d[1] >= 0x80) { wr8(STEP(e), 4); return; }
    c->d[2] = 0;
    if ((uint16_t)c->d[1] < 0x60) c->d[2] = (uint16_t)c->d[1] < 0x28 ? 16 : 8;
    pick_step(e, 0x14912);
}

static void a1492A(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 6);
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    call(DIST_X_TO_PLAYER);
    c->d[2] = (uint16_t)c->d[1] < 0x60 ? 8 : 0;
    pick_step(e, 0x14964);
}

static void a14974(Ent e)
{
    if (begins(e)) {
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    c->d[2] = 0;
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] < 0x30) {
        c->d[2] = 8;
        call(DIST_X_TO_ENTITY);
        if ((uint16_t)c->d[1] >= 0x60) c->d[2] = 16;
    }
    pick_step(e, 0x149B8);
}

/* walks back to its home x, then stands facing the player; near the player (within $20 up or
 * down, $40 aside) it goes to step 4 */
static void a149D0(Ent e)
{
    if (begins(e)) {
        wr8(TIMER(e), 0xFF);
        wrw(ANIM_STEP(e), 0x200);
        call(RANDOM_DELAY);
        set_d0w(rdw(HOME_X(e)));
        if (rdw(HOME_X(e)) == rdw(X(e))) { wrw(VEL_X(e), 0); c->d[0] = 0; }
        else { call(FACE_X); call(WALK_FORWARD); c->d[0] = 8; }
        call(SET_ANIM);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    set_d0w(rdw(HOME_X(e)));
    int16_t v = rdw(VEL_X(e));
    if (v) {
        uint16_t home = (uint16_t)rdw(HOME_X(e)), x = (uint16_t)rdw(X(e));
        set_d0w((uint16_t)(home - x));
        int there = v < 0 ? home >= x : home < x;   /* reached or passed it */
        if (there) {
            wrw(VEL_X(e), 0);
            if (rd8(ANIM_ID(e))) {
                call(FACE_PLAYER);
                call_d0(SET_ANIM, 6);
                wr8(TIMER(e), 0x1E);
            }
        }
    }
    if (!(rd8(TIMER(e)) & 0x80)) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e)) & 0x80) call_d0(SET_ANIM, 0);
    }
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(DIST_Y_ABS_TARGET);
    if ((uint8_t)c->d[1] >= 0x20) return;           /* cmpi.b: the low byte */
    call(DIST_X_TO_ENTITY);
    if ((uint16_t)c->d[1] >= 0x40) return;
    wr8(STEP(e), 4);
}

/* turns to the player (on to the next state) when facing it, close by */
static void a14A82(Ent e)
{
    if (begins(e)) {
        call(RANDOM_DELAY);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] >= 0x30) { wr8(STEP(e), 4); return; }
    call(DIST_X_TO_ENTITY);                         /* d0: 8 if the player is to the left */
    if ((uint16_t)c->d[1] < 0x30) {
        c->d[1] = rd8(ATTR(e)) & 8;
        if ((uint8_t)c->d[0] == (uint8_t)c->d[1]) { call(ENEMY_NEXT_STATE); return; }
    }
    call(RANDOM);
    c->d[0] = set_w(c->d[0], c->d[0] & 7);
    c->d[0] = set_b(c->d[0], rd8(0x14AE2u + (c->d[0] & 7)));
    if ((uint8_t)c->d[0]) wr8(STEP(e), (uint8_t)c->d[0]);
}

/* $14AEA and $14AFA: as $14974, the near row by a distance of their own (VAR: $28 or $40) */
static void near_by_range(Ent e)
{
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (still_waiting(e)) return;
    c->d[2] = 0;
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] < 0x30) {
        c->d[2] = 8;
        call(DIST_X_TO_ENTITY);
        if ((uint16_t)c->d[1] < (uint16_t)rdw(VAR(e))) c->d[2] = 16;
    }
    pick_step(e, 0x14B44);
}

static void ranged_start(Ent e, int range)
{
    wrw(VAR(e), range);
    call_d0(SET_ANIM, 6);
    call(RANDOM_DELAY);
}

static void a14AEA(Ent e) { if (begins(e)) ranged_start(e, 0x28); near_by_range(e); }
static void a14AFA(Ent e) { if (begins(e)) ranged_start(e, 0x40); near_by_range(e); }

/* in water or on land; off the ground it goes to step 7. Its table's rows: near, middle, far,
 * and the same again in water */
static void a14B7E(Ent e)
{
    if (!begins(e)) {                               /* the original's test is this way round */
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
    }
    call(MAYBE_BUBBLE);
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    call(MOVE_WITH_PLATFORMS);
    if (hit()) return;
    call(SLOW_ON_GROUND);
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) { wr8(STEP(e), 7); return; }
    call(DIST_X_TO_PLAYER);
    c->d[2] = 0;
    int check_y = 1;
    if ((uint16_t)c->d[1] >= 0x30) {
        c->d[2] = 8;
        if ((uint16_t)c->d[1] < 0x60) check_y = 0;
    }
    if (check_y) {
        call(DIST_Y_ABS);
        if ((uint16_t)c->d[1] >= 0x28) c->d[2] = 16;
    }
    if (rd8(WORLD_FLAGS) & 1) c->d[2] = set_w(c->d[2], (c->d[2] + 0x18) & 0xFFFF);
    pick_step(e, 0x14BEA);
}

/* waits for the player within $40, then falls for 30 frames */
static void a14C1A(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        call(DIST_X_TO_PLAYER);
        if ((uint16_t)c->d[1] >= 0x40) return;
        set8(STEP(e), 0x80);
        wr8(TIMER(e), 0x1E);
    }
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(ACCEL_Y);
    call(APPLY_VEL_Y);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) call(ENEMY_NEXT_STATE);
}

/* ---- patrolling and jumping (batch 11) ---- */

/* The library's turns: each drops this action's return when it turns the monster round (it is
 * done for this frame); here they answer 1 and the action returns. $448C reverses its facing and
 * walks on; $445C faces it away from what touched it */
static int turned_at_wall(void)                     /* $4428: blocked ahead (COLL & $41 == 1) */
{
    c->d[0] = (uint8_t)((rd8(COLL((Ent)c->a[4])) & 0x41) - 1);
    if (c->d[0]) return 0;
    call(TURN);
    return 1;
}

static int turned_by_touch(void)                    /* $4444: touched (TOUCH bit 2) */
{
    if (!(rd8(TOUCH((Ent)c->a[4])) & 4)) return 0;
    call(FACE_AWAY);
    return 1;
}

static int turned_at_range(Ent e)                   /* $45DC: as far from home as WORD_31FE */
{
    c->d[2] = set_w(c->d[2], (uint16_t)rdw(WORD_31FE(e)));
    call(PAST_RANGE);
    if (c->ccr & CC_C) return 0;
    call(TURN);
    return 1;
}

static int turned_at_edge(void)                     /* $462A: the end of its platform or floor */
{
    call(EDGE_AHEAD);
    if (c->ccr & CC_N) call(GROUND_AHEAD);
    if (!(c->ccr & CC_Z)) return 0;
    call(TURN);
    return 1;
}

/* $140E0: a state to go to now (d0), the one it was in pending (NEXT_STATE) */
static void detour(Ent e)
{
    c->d[1] = set_b(c->d[1], rd8(STEP(e)) & 0x7F);
    wr8(NEXT_STATE(e), (uint8_t)c->d[1]);
    wr8(STEP(e), (uint8_t)c->d[0]);
}

/* the frame's walk: TIMER frames, then the next state (1); else 0 */
static int walk_over(Ent e)
{
    if (despawned()) return 1;
    call(CONTACT_AND_MOVE);
    if (hit()) return 1;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return 1; }
    return 0;
}

/* $14C50, $14CA2: walks (mostly towards the player) for a random while, turning at walls,
 * edges and touches; $14C50 also within a random range of home */
static void patrol_start(Ent e)
{
    call_d0(SET_ANIM, 8);
    call(RANDOM);
    c->d[0] = set_w(c->d[0], c->d[0] & 7);
    if (c->d[0] & 7) call(FACE_PLAYER);
    call(WALK_FORWARD);
    call(RANGE_40);
    call(RANDOM_TIMER);
}

static void a14C50(Ent e)
{
    if (begins(e)) patrol_start(e);
    if (walk_over(e)) return;
    c->d[2] = set_w(c->d[2], (uint16_t)rdw(WORD_31FE(e)));
    if (turned_at_range(e) || turned_at_wall() || turned_at_edge() || turned_by_touch()) return;
}

static void a14CA2(Ent e)
{
    if (begins(e)) patrol_start(e);
    if (walk_over(e)) return;
    c->d[2] = set_w(c->d[2], (uint16_t)rdw(WORD_31FE(e)));
    if (turned_at_wall() || turned_at_edge() || turned_by_touch()) return;
}

/* walks away from a player close by (else towards); at a wall it jumps (state 5, then back)
 * where there is room above, or turns */
static void a14CF0(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 8);
        call(FACE_PLAYER);
        call(DIST_X_TO_ENTITY);
        if ((uint16_t)c->d[1] < 0x30) wr8(ATTR(e), rd8(ATTR(e)) ^ 8);
        call(WALK_FORWARD);
        call(RANGE_80);
        call(RANDOM);
        c->d[0] = set_w(c->d[0], ((c->d[0] & 0x1F) + 0x20) & 0xFFFF);
        wr8(TIMER(e), (uint8_t)c->d[0]);
    }
    call(MAYBE_BUBBLE);
    if (walk_over(e)) return;
    uint8_t coll = rd8(COLL(e));
    c->d[0] = set_b(c->d[0], coll);
    c->d[1] = (uint8_t)((coll & 0x41) - 1);
    if (!c->d[1]) {                                 /* a wall */
        c->d[0] = set_b(c->d[0], (uint8_t)((coll & 0x0A) - 2));
        if (!(uint8_t)c->d[0]) {                    /* on the ground: room to jump? */
            call(ROOM_ABOVE);
            if (!(c->ccr & CC_Z)) { c->d[0] = 5; detour(e); return; }
        }
        call(TURN);
        return;
    }
    if (turned_by_touch()) return;
    if (turned_at_range(e)) return;
}

/* walks towards the player when it is near and within its range of home, else ahead */
static void a14D74(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 8);
        call(RANDOM_TIMER);
        call(RANGE_80);
        call(DIST_Y_ABS_TARGET);
        if ((uint16_t)c->d[1] < 0x30) {
            c->d[2] = set_w(c->d[2], (uint16_t)rdw(WORD_31FE(e)));
            call(TARGET_FROM_HOME);
            if (c->ccr & CC_C) call(FACE_PLAYER);
        }
        call(WALK_FORWARD);
    }
    if (walk_over(e)) return;
    if (turned_at_wall()) return;
    if (turned_at_range(e)) return;
}

/* with the shield, backing off a touch */
static void a14DC8(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 8);
        call(FACE_PLAYER);
        call(WALK_FORWARD);
        call(RANGE_80);
        call(RANDOM_TIMER_32);
    }
    if (despawned()) return;
    call(SHIELD_AND_MOVE);
    if (hit()) return;
    call(BACK_OFF);
    if (!(c->ccr & CC_Z)) call(ENEMY_NEXT_STATE);   /* bsr: and on with this frame */
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    if (turned_at_edge() || turned_at_wall() || turned_by_touch() || turned_at_range(e)) return;
}

/* runs at twice its speed from a player $50 away or more */
static void a14E1A(Ent e)
{
    if (begins(e)) {
        call(RANDOM_TIMER_32);
        call(RANGE_80);
        call(DIST_X_TO_PLAYER);
        set_d0w(rdw(SPEED_X(e)));
        if ((uint16_t)c->d[1] >= 0x50) {
            wrw(ANIM_STEP(e), 0x200);
            set_d0w((int)((c->d[0] * 2) & 0xFFFF));
        }
        call(SET_VEL_X_FACING);
        call_d0(SET_ANIM, 8);
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    if (turned_at_edge() || turned_at_wall() || turned_at_range(e)) return;
}

/* charges at the player; anything in the way (a touch, a wall, an edge) ends it */
static void a14E6C(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x200);
        call(FACE_PLAYER);
        call(WALK_FORWARD);
        call(RANDOM_TIMER_32);
        call_d0(SET_ANIM, 8);
    }
    if (walk_over(e)) return;
    if (rd8(TOUCH(e)) & 4) { call(ENEMY_NEXT_STATE); return; }
    c->d[0] = (uint8_t)((rd8(COLL(e)) & 0x41) - 1);
    if (!c->d[0]) { call(ENEMY_NEXT_STATE); return; }
    call(EDGE_AHEAD);
    if (c->ccr & CC_Z) { call(ENEMY_NEXT_STATE); return; }
    call(GROUND_AHEAD);
    if (c->ccr & CC_Z) call(ENEMY_NEXT_STATE);
}

/* jumps: on until it lands */
static void landed(Ent e)
{
    (void)e;
    call(CHECK_GROUNDED);
    if (c->ccr & CC_Z) call(ENEMY_NEXT_STATE);
}

static void a14EC8(Ent e)                           /* straight up */
{
    if (begins(e)) {
        call_d0(SET_ANIM, 10);
        wrw(VEL_X(e), 0);
        call(JUMP);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    landed(e);
}

static void a14EF4(Ent e)                           /* forward, on platforms */
{
    if (begins(e)) {
        call_d0(SET_ANIM, 10);
        call(WALK_FORWARD);
        call(JUMP);
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    call(ACCEL_Y);
    call(MOVE_WITH_PLATFORMS);
    if (hit()) return;
    landed(e);
}

/* $14FBC (with despawning) and $14FC0: a jump's frame, on until it lands */
static void jump_frame(Ent e, int despawn)
{
    if (despawn && despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    landed(e);
}

/* $14F66: steered forward at a quarter of its speed, then the frame of a jump ($14FBC) */
static void steered_jump(Ent e)
{
    int a = (uint16_t)rdw(SPEED_X(e)) >> 2;
    set_d0w(a);
    if (rd8(ATTR(e)) & 8) { a = -a; set_d0w(a); }
    wrw(ACCEL_X(e), a);
    call(ACCEL_X_);
    jump_frame(e, 1);
}

static void a14F54(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 10);
        call(JUMP);
    }
    steered_jump(e);
}

/* in water: a slow high jump (four times its speed up, a quarter of it in gravity) */
static void a14F28(Ent e)
{
    if (!(rd8(WORLD_FLAGS) & 1)) { a14F54(e); return; }
    if (begins(e)) {
        set_d0w(rdw(SPEED_Y(e)));
        c->d[1] = set_w(c->d[1], (c->d[0] & 0xFFFF) >> 2);
        wrw(GRAVITY(e), (int)(c->d[1] & 0xFFFF));
        set_d0w((int)(-(int)((c->d[0] << 2) & 0xFFFF)) & 0xFFFF);
        call(SET_VEL_Y);
        call_d0(SET_ANIM, 10);
    }
    steered_jump(e);
}

/* with the shield: a jump forward, backing off a touch */
static void a14F80(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        call_d0(SET_ANIM, 10);
        call(JUMP);
        call(WALK_FORWARD);
    }
    if (despawned()) return;
    call(SHIELD_AND_MOVE);
    if (hit()) return;
    call(BACK_OFF);
    if (!(c->ccr & CC_Z)) call(ENEMY_NEXT_STATE);   /* bsr: and on with this frame */
    landed(e);
}

/* ---- leaping and attacking (batch 12) ---- */

/* a jump that floats at the top (a slower animation while rising) */
static void a14FD2(Ent e)
{
    if (begins(e)) {
        set8(ALIVE(e), 0x40);
        wr8(ANIM_RATE(e), 0x12);
        call_d0(SET_ANIM, 10);
        call(JUMP);
    }
    if (rdw(VEL_Y(e)) >= 0) wr8(ANIM_RATE(e), 0x0C);
    jump_frame(e, 1);
}

/* $15020: a leap at the player, farther the farther it is (sometimes twice as far, by the bits of
 * $15064 or $15065 for near), at most $300 */
static void leap(Ent e)
{
    wrw(ANIM_STEP(e), 0x100);
    set_d0w(-0x400);
    call(SET_VEL_Y);
    call(DIST_X_TO_PLAYER);
    c->a[0] = (uint16_t)c->d[1] < 0x40 ? 0x15065u : 0x15064u;
    c->d[2] = set_w(c->d[2], c->d[1]);
    call(RANDOM);
    c->d[0] = set_b(c->d[0], c->d[0] & 7);
    if (rd8(c->a[0]) & (1u << (c->d[0] & 7))) c->d[2] = set_w(c->d[2], (c->d[2] * 2) & 0xFFFF);
    uint16_t v = (uint16_t)((c->d[2] & 0xFFFF) << 3);
    if (v > 0x300) v = 0x300;
    set_d0w(v);
    call(SET_VEL_X_FACING);
    jump_frame(e, 0);
}

static void a14FFE(Ent e) { if (!begins(e)) { jump_frame(e, 0); return; } call_d0(SET_ANIM, 12); leap(e); }
static void a15010(Ent e) { if (!begins(e)) { jump_frame(e, 0); return; } call_d0(SET_ANIM, 10); leap(e); }

/* three hops back and forth on the spot (TIMER), then a leap at the player */
static void a15066(Ent e)
{
    if (begins(e)) wrw(TIMER(e), 0x0301);           /* TIMER 3 hops, TIMER2 1 frame */
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) { call_d0(SET_ANIM, 6); return; }
    call_d0(SET_ANIM, 10);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) {                            /* a hop the other way */
        wr8(TIMER2(e), 4);
        set_d0w(rdw(SPEED_X(e)));
        call(SET_VEL_X_FACING);
        wrw(VEL_X(e), -rdw(VEL_X(e)));
        call(HOP);
        return;
    }
    set_d0w(-0x400);
    call(SET_VEL_Y);
    call(DIST_X_TO_PLAYER);
    uint16_t v = (uint16_t)((c->d[1] & 0xFFFF) << 3);
    if (v > 0x400) v = 0x400;
    set_d0w(v);
    call(SET_VEL_X_FACING);
    call(ENEMY_NEXT_STATE);
}

/* the shot its animation marks (attribute bit 0, set by the animation): fired once */
static int fires(Ent e)
{
    uint8_t a = rd8(ATTR(e));
    wr8(ATTR(e), a & (uint8_t)~1);
    if (!(a & 1)) return 0;
    shoot();
    return 1;
}

/* jumps straight up, and at the top attacks (animation 12), firing when it marks */
static void a150D8(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        wr8(TIMER(e), 0);
        clr8(ATTR(e), 3);
        call(JUMP);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (!rd8(TIMER(e))) {
        if (rdw(VEL_Y(e)) < 0) return;
        wr8(TIMER(e), 0xFF);
        call_d0(SET_ANIM, 12);
        return;
    }
    if (!rd8(ANIM_BUSY(e))) { call(ENEMY_NEXT_STATE); return; }
    fires(e);
}

/* two small hops at the player, then the attack */
static void a15126(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        call(FACE_PLAYER);
        clr8(ATTR(e), 3);
        wr8(TIMER(e), 2);
        call_d0(SET_ANIM, 10);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (rd8(TIMER(e)) & 0x80) {
        if (!rd8(ANIM_BUSY(e))) { call(ENEMY_NEXT_STATE); return; }
        fires(e);
        return;
    }
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!(rd8(TIMER(e)) & 0x80)) { call(HOP); return; }
    call_d0(SET_ANIM, 12);
}

/* attacks where it stands */
static void a151A0(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        clr8(ATTR(e), 3);
        call_d0(SET_ANIM_NOW, 12);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (fires(e)) return;
    if (!rd8(ANIM_BUSY(e))) call(ENEMY_NEXT_STATE);
}

/* with the shield: walks or hops at the player, faster than its speed, turning at walls; close
 * by (within $10 up or down, $28 aside) it attacks, with its kind's hit records frame by frame */
static void a151D6(Ent e)
{
    if (begins(e)) {
        call(CHECK_GROUNDED);
        if (c->ccr & CC_Z) {
            call(RANDOM);
            c->d[0] = set_b(c->d[0], c->d[0] & 3);
            if (c->d[0] & 3) c->d[0] = 8;
            else { call(JUMP); c->d[0] = 10; }
            call(SET_ANIM);
        }
        wr8(VAR(e), 0);
        wrw(STRENGTH2(e), rdw(STRENGTH(e)));
        call(KIND_STATS);
        wr8(TIMER2(e), rd8(c->a[1] + 6));
        set_d0w(rdw(SPEED_X(e)) + 0x80);
        call(SET_VEL_X_FACING);
        call(RANDOM_TIMER);
    }
    if (despawned()) return;
    call(SHIELD_AND_MOVE);
    if (hit()) return;
    call(BACK_OFF);
    if (!(c->ccr & CC_Z)) call(ENEMY_NEXT_STATE);   /* bsr: and on with this frame */
    call(TURN_AT_WALL);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    if (rd8(VAR(e)) & 0x80) {                       /* attacking */
        call(CHECK_GROUNDED);
        if (c->ccr & CC_Z) wrw(VEL_X(e), 0);
        c->d[0] = set_b(c->d[0], rd8(TIMER2(e)));
        call(ATTACK_FRAME);
        if (!rd8(ANIM_BUSY(e))) call(ENEMY_NEXT_STATE);
        return;
    }
    if (rdw(VEL_Y(e)) < 0) return;
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] < 0x10) {
        call(DIST_X_TO_PLAYER);
        if ((uint16_t)c->d[1] < 0x28) {
            wr8(VAR(e), 0xFF);
            call(FACE_PLAYER);
            call_d0(SET_ANIM, 12);
            return;
        }
    }
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    if (rd8(ANIM_ID(e)) != 0x0A) return;
    call_d0(SET_ANIM, 8);                           /* landed: walking again */
}

/* hops 4-7 times, every other frame on the ground, at the player when it was close (VAR) or
 * at random */
static void a152A0(Ent e)
{
    if (begins(e)) {
        call(RANDOM);
        set_d0w((int)(((c->d[0] & 3) + 4) & 0xFFFF));
        wrw(TIMER(e), (int)(c->d[0] & 0xFFFF));     /* TIMER 0, TIMER2 the hops */
        wrw(VAR(e), 1);                             /* VAR 0, SHOT (the frames) 1 */
        call(DIST_X_TO_PLAYER);
        if ((uint16_t)c->d[1] < 0x30) wr8(VAR(e), 0xFF);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    if (!(rd8(TIMER(e)) & 0x80) && rd8(ANIM_ID(e)) == 0x0C) {
        call(RESET_ANIM_TIMER);
        wr8(TIMER(e), 0xFF);
    }
    wr8(SHOT(e), (uint8_t)(rd8(SHOT(e)) - 1));
    if (rd8(SHOT(e))) return;
    wr8(SHOT(e), 2);
    if (rd8(VAR(e))) {
        call(FACE_PLAYER);
        call(DIST_X_TO_ENTITY);
        uint16_t v = (uint16_t)(((c->d[1] & 0xFFFF) * 2) << 3);
        if (v > 0x200) v = 0x200;
        set_d0w(v);
        call(SET_VEL_X_FACING);
    } else {
        call(RANDOM);
        c->d[0] = set_b(c->d[0], c->d[0] & 3);
        if (!(c->d[0] & 3)) wr8(ATTR(e), rd8(ATTR(e)) ^ 8);
        call(WALK_FORWARD);
    }
    set_d0w(-0x500);
    call(SET_VEL_Y);
    call_d0(SET_ANIM, 12);
    wr8(TIMER(e), 0);
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (!rd8(TIMER2(e))) call(ENEMY_NEXT_STATE);
}

/* ---- more attacks, a dive (batch 13) ---- */

/* an attack's set-up: its strength, and its attack number from its kind's stats (TIMER2) */
static void attack_start(Ent e)
{
    wrw(STRENGTH2(e), rdw(STRENGTH(e)));
    call(KIND_STATS);
    wr8(TIMER2(e), rd8(c->a[1] + 6));
}

/* this frame's hit records of the attack */
static void attack_frame(Ent e)
{
    c->d[0] = set_b(c->d[0], rd8(TIMER2(e)));
    call(ATTACK_FRAME);
}

/* an attack drifting to a stop, with the shield; then step 3, or 7 off the ground in water */
static void a15352(Ent e)
{
    if (begins(e)) {
        wrw(HIT_B(e), 0);
        call_d0(SET_ANIM, 12);
        attack_start(e);
    }
    call(MAYBE_BUBBLE);
    if (despawned()) return;
    call_d0(FRICTION_X, 8);
    call_d0(FRICTION_Y, 8);
    call(SHIELD_CONTACT);
    call(MOVE_WITH_PLATFORMS);
    if (hit()) return;
    attack_frame(e);
    if (rd8(ANIM_BUSY(e))) return;
    c->d[1] = 3;
    if (rd8(WORLD_FLAGS) & 1) {
        call(CHECK_GROUNDED);
        if (!(c->ccr & CC_Z)) c->d[1] = 7;
    }
    wr8(STEP(e), (uint8_t)c->d[1]);
    wr32(HIT_A(e), 0);
}

/* an animation (12), slowing on the ground, then on */
static void a153BE(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        call_d0(SET_ANIM_NOW, 12);
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (c->ccr & CC_Z) call_d0(FRICTION_X, 64);
    if (!rd8(ANIM_BUSY(e))) call(ENEMY_NEXT_STATE);
}

/* $15456: the shot it holds (VAR, a word: an entity, or negative for none) removed */
static void drop_shot(Ent e)
{
    set_d0w(rdw(VAR(e)));
    if (rdw(VAR(e)) < 0) return;
    c->a[2] = (uint32_t)(int32_t)rdw(VAR(e));
    wrw(ALIVE(c->a[2]), 0);
}

/* fires a shot it keeps for 60 frames (sound $37); hit meanwhile, the shot goes too */
static void a153F0(Ent e)
{
    if (begins(e)) {
        wr8(VAR(e), 0xFF);
        wr8(TIMER(e), 0);
        wrw(VEL_X(e), 0);
        clr8(ATTR(e), 3);
        call_d0(SET_ANIM_NOW, 12);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) { drop_shot(e); c->d[0] = 2; new_state(0); return; }
    if (!rd8(TIMER(e))) {
        uint8_t a = rd8(ATTR(e));
        wr8(ATTR(e), a & (uint8_t)~1);
        if (!(a & 1)) return;
        shoot();
        wrw(VAR(e), (int)(c->a[2] & 0xFFFF));
        wr8(TIMER(e), 0x3C);
        call_d0(QUEUE_SOUND, 55);
        return;
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    drop_shot(e);
    call(ENEMY_NEXT_STATE);
}

/* stands shooting 8 shots (16 one time in 64), one every 4 frames once its animation ends */
static void a15464(Ent e)
{
    if (begins(e)) {
        wrw(TIMER(e), 0x0801);
        call(RANDOM);
        c->d[0] = set_w(c->d[0], c->d[0] & 0x3F);
        if (!(c->d[0] & 0x3F)) wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) + 8));
        call(FACE_PLAYER);
        call_d0(SET_ANIM, 12);
    }
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    if (rd8(ANIM_BUSY(e))) return;
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    wr8(TIMER2(e), 4);
    shoot();
}

/* an attack while falling and moving, with the shield */
static void a154B2(Ent e)
{
    if (begins(e)) {
        wrw(HIT_B(e), 0);
        call_d0(SET_ANIM_NOW, 12);
        attack_start(e);
    }
    if (despawned()) return;
    call(ACCEL_Y);
    call(SHIELD_CONTACT);
    call(ENEMY_MOVE);
    if (hit()) return;
    call(SLOW_ON_GROUND);
    attack_frame(e);
    if (rd8(ANIM_BUSY(e))) return;
    wr32(HIT_A(e), 0);
    call(ENEMY_NEXT_STATE);
}

/* hops forward; on the way down it turns to attack (12), on until it faces away from the player,
 * its time is up, or it meets a wall */
static void a15504(Ent e)
{
    if (begins(e)) {
        call(FACE_PLAYER);
        wr8(VAR(e), 0);
        call_d0(SET_ANIM, 10);
        call(RANDOM_TIMER_32);
        call(WALK_FORWARD);
        set_d0w(-0x300);
        call(SET_VEL_Y);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    call(ENEMY_MOVE);
    if (hit()) return;
    int check = 1;
    if (!rd8(VAR(e))) {
        if (rdw(VEL_Y(e)) < 0) check = 0;
        else { c->d[0] = 12; wr8(VAR(e), 12); call(SET_ANIM); }
    }
    if (check) {
        call(DIST_X_TO_PLAYER);                     /* d0: 8 if the player is to the left */
        c->d[1] = (uint8_t)((rd8(ATTR(e)) & 8) - (uint8_t)c->d[0]);
        if (c->d[1]) { call(ENEMY_NEXT_STATE); return; }
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    }
    c->d[0] = (uint8_t)((rd8(COLL(e)) & 0x41) - 1);
    if (!c->d[0]) call(ENEMY_NEXT_STATE);
}

/* dives down to $10 above the target's height, then flies back up home, sideways */
static void a15576(Ent e)
{
    if (begins(e)) {
        wrw(GRAVITY(e), 0);
        wrw(VEL_Y(e), 0x300);
        wrw(SPEED_Y(e), 0x400);
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(ACCEL_XY);
    call(APPLY_VEL);
    Ent t = (Ent)(int32_t)rdw(TARGET_W);
    c->a[2] = t;
    if (!rdw(GRAVITY(e))) {                         /* diving */
        uint16_t goal = (uint16_t)(rdw(Y(t)) - 0x10), y = (uint16_t)rdw(Y(e));
        set_d0w((uint16_t)(goal - y));
        if (goal > y) return;                       /* bhi: not there yet */
        wrw(GRAVITY(e), -0x40);
        int a = 0x40;
        if (rd8(ATTR(e)) & 8) a = -a;
        set_d0w(a);
        wrw(ACCEL_X(e), a);
        return;
    }
    uint16_t home = (uint16_t)rdw(HOME_Y(e)), y = (uint16_t)rdw(Y(e));
    set_d0w((uint16_t)(home - y));
    if (home >= y) call(ENEMY_NEXT_STATE);          /* back up home */
}

/* ---- swimming, flying, hopping (batch 14) ---- */

/* swims after the target, at its height or rising, until it reaches it */
static void a155E2(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 0x68);
        c->d[4] = set_w(c->d[4], 0x200);
        c->a[2] = (Ent)(int32_t)rdw(TARGET_W);
        call(VEL_TOWARD);
        call_d0(SET_ANIM, 14);
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    call(MOVE_WITH_PLATFORMS);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) {
        Ent t = (Ent)(int32_t)rdw(TARGET_W);
        c->a[2] = t;
        set_d0w((uint16_t)(rdw(Y(t)) - rdw(Y(e))));
        if ((uint16_t)rdw(Y(t)) >= (uint16_t)rdw(Y(e))) return;
    }
    wrw(VEL_Y(e), -rdw(VEL_Y(e)));
    call(ENEMY_NEXT_STATE);
}

/* swims about after the target, jumping up out of the ground first (VAR); off the ground, close
 * above the player, it bites (step 6); on the ground level with it, step 3 */
static void a15630(Ent e)
{
    if (begins(e)) {
        call(RANDOM_DELAY);
        wr8(TIMER(e), 1);
        wr8(VAR(e), 0);
        if (rd8(ANIM_ID(e)) != 0x0E) {
            wr8(VAR(e), 0xFF);
            call(CHECK_GROUNDED);
            if (!(c->ccr & CC_Z)) {
                set_d0w(rdw(SPEED_Y(e)));
                c->d[1] = set_w(c->d[1], (c->d[0] & 0xFFFF) >> 2);
                wrw(GRAVITY(e), (int)(c->d[1] & 0xFFFF));
                set_d0w((int)(-(int)((c->d[0] << 2) & 0xFFFF)) & 0xFFFF);
                call(SET_VEL_Y);
            }
        }
    }
    call(MAYBE_BUBBLE);
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    call(MOVE_WITH_PLATFORMS);
    if (hit()) return;
    if (rd8(VAR(e))) {
        call(ACCEL_XY);
        if (rdw(VEL_Y(e)) < 0) return;
        wr8(VAR(e), 0);
        call_d0(SET_ANIM, 14);
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) {
        call(RANDOM_TIMER);
        c->a[2] = (Ent)(int32_t)rdw(TARGET_W);
        c->d[4] = 16;
        call(ACCEL_TOWARD);
        wrw(ACCEL_X(e), (int)(c->d[0] & 0xFFFF));
        wrw(GRAVITY(e), (int)(c->d[5] & 0xFFFF));
        call(FACE_PLAYER);
    }
    call(SWIM_LIMIT);
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(RANDOM_DELAY);
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) {                         /* off the ground: a bite at a player just below */
        call(DIST_Y_TO_TARGET);
        if (c->ccr & CC_N) return;
        if ((uint8_t)c->d[1] >= 0x10) return;       /* cmpi.b */
        call(DIST_X_TO_ENTITY);
        if ((uint16_t)c->d[1] >= 0x18) return;
        wr8(STEP(e), 6);
        return;
    }
    call(DIST_Y_ABS_TARGET);
    if ((uint16_t)c->d[1] < 0x10) wr8(STEP(e), 3);
}

/* bobs about a height (LINK2) that moves now and then, facing the player */
static void a15704(Ent e)
{
    if (begins(e)) {
        clr8(ATTR(e), 0x10);
        call(RANDOM_TIMER);
        call(RANDOM_DELAY);
        wrw(LINK2(e), rdw(Y(e)));
        call(FACE_PLAYER);
        call(FLY_FACING);
        call_d0(SET_ANIM, 14);
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    set_d0w(rdw(LINK2(e)));
    c->d[1] = 4; c->d[2] = 64;
    call(BOB_TOWARD);
    call(ACCEL_XY);
    call(APPLY_VEL);
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    call(RANDOM_DELAY);
    call(RANDOM);
    set_d0w((int)(((c->d[0] & 0x3F) + (uint16_t)rdw(HOME_Y(e))) & 0xFFFF));
    wrw(LINK2(e), (int)(c->d[0] & 0xFFFF));
    call(FACE_PLAYER);
    call(FLY_FACING);
}

/* stands 20 frames */
static void a1577A(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 8);
        wr8(TIMER(e), 0x14);
        wrw(VEL_X(e), 0);
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) call(ENEMY_NEXT_STATE);
}

static void a157A6(Ent e) { (void)e; }              /* nothing */

/* three hops on the spot ($157D4: up $200, animation 12) */
static void hop_up(int anim) { set_d0w(-0x200); call(SET_VEL_Y); call_d0(SET_ANIM, (uint32_t)anim); }

static void a157A8(Ent e)
{
    if (begins(e)) {
        wrw(ANIM_STEP(e), 0x100);
        wr8(TIMER(e), 3);
        hop_up(12);
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    hop_up(12);
}

/* the same with animation 10, its timer reset once on the way down */
static void hop_10(Ent e) { wr8(TIMER2(e), 0); hop_up(10); }   /* $1581C */

static void a157E4(Ent e)
{
    if (begins(e)) { wr8(TIMER(e), 3); hop_10(e); }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) {
        if (rdw(VEL_Y(e)) < 0) return;
        uint8_t t = rd8(TIMER2(e));
        wr8(TIMER2(e), t | 1);
        if (!(t & 1)) call(RESET_ANIM_TIMER);
        return;
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    hop_10(e);
}

/* flies back towards home, on when it is within $10 below it */
static void a15830(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 0x43);
        wr32(SPEED(e), 0x02000200u);
        clr8(ATTR(e), 8);
        set_d0w((uint16_t)(rdw(HOME_X(e)) - rdw(X(e))));
        if ((uint16_t)rdw(HOME_X(e)) < (uint16_t)rdw(X(e))) set8(ATTR(e), 8);
        c->d[1] = set_w(c->d[1], (uint16_t)(rdw(HOME_Y(e)) - rdw(Y(e))));
        c->d[2] = set_w(c->d[2], 0x40);
        call(ACCEL_TO);
        wrw(ACCEL_X(e), (int)(c->d[4] & 0xFFFF));
        wrw(GRAVITY(e), (int)(c->d[5] & 0xFFFF));
    }
    if (despawned()) return;
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(ACCEL_XY);
    call(APPLY_VEL);
    uint16_t lim = (uint16_t)(rdw(HOME_Y(e)) + 0x10);
    set_d0w(lim);
    if (lim >= (uint16_t)rdw(Y(e))) call(ENEMY_NEXT_STATE);
}

/* faces the player for a while, then charges at four times its speed; close by, on */
static void a1589A(Ent e)
{
    if (begins(e)) {
        call(RANDOM_TIMER_32);
        call(FACE_PLAYER);
        wrw(ANIM_STEP(e), 0x300);
        call_d0(SET_ANIM, 8);
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (rd8(TIMER(e))) {
        call(FACE_PLAYER);
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        set_d0w((int)((rdw(SPEED_X(e)) << 2) & 0xFFFF));
        call(SET_VEL_X_FACING);
    }
    call(DIST_X_TO_PLAYER);
    if ((uint16_t)c->d[1] < 0x30) call(ENEMY_NEXT_STATE);
}

/* $1593C: TIMER, $18 + up to $3F; d0 the random */
static void random_wait(Ent e)
{
    call(RANDOM);
    set_d0w((int)(((c->d[0] & 0x3F) + 0x18) & 0xFFFF));
    wr8(TIMER(e), (uint8_t)c->d[0]);
}

/* climbs up and down a thread from its partner (LINK2), which it draws stretched to it ($1594E);
 * pausing at each turn */
static void a158E6(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 8);
        wr8(TIMER2(e), 0);
        wrw(VEL_Y(e), rdw(SPEED_Y(e)));
        random_wait(e);
        c->d[0] = set_w(c->d[0], c->d[0] & 7);
        if (c->d[0] & 7) {
            wrw(VEL_Y(e), rdw(VEL_Y(e)) << 1);
            wrw(TIMER(e), (uint16_t)rdw(TIMER(e)) >> 1);
        }
    }
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    if (rd8(TIMER2(e))) { wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1)); return; }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) {                           /* turns, and pauses */
        wrw(VEL_Y(e), -rdw(VEL_Y(e)));
        random_wait(e);
        set_d0w((uint16_t)(c->d[0] & 0xFFFF) >> 1);
        wr8(TIMER2(e), (uint8_t)c->d[0]);
        return;
    }
    call(APPLY_VEL_Y);
    Ent t = (Ent)(int32_t)rdw(LINK2(e));
    c->a[2] = t;
    uint16_t y = (uint16_t)rdw(Y(e)), top = (uint16_t)rdw(Y(t));
    c->d[1] = set_w(c->d[1], top);
    uint16_t d = (uint16_t)(y - top);
    set_d0w(d);
    if (d & 0x8000) {                               /* above the top: held there, thread hidden */
        wrw(Y(e), (int16_t)top);
        clr8(ALIVE(t), 0x40);
        return;
    }
    if (d >= 0x58) {                                /* at the end of the thread */
        c->d[0] = 87;
        c->d[1] = set_w(c->d[1], (uint16_t)(top + 87));
        wrw(Y(e), (int16_t)(top + 87));
    }
    set_d0w((int)((((c->d[0] & 0xFFFF) >> 3) * 2) & 0xFFFF));
    uint8_t a = rd8(ALIVE(t));
    wr8(ALIVE(t), a | 0x40);
    if ((a & 0x40) && (uint8_t)c->d[0] == rd8(ANIM_ID(t))) return;
    uint32_t s = c->a[4]; c->a[4] = c->a[2]; c->a[2] = s;   /* exg: the thread's animation */
    call(SET_ANIM);
    s = c->a[4]; c->a[4] = c->a[2]; c->a[2] = s;
}

/* turns, and walks until a wall or until it stops; then a hop (VAR), on when coming down */
static void a15994(Ent e)
{
    if (begins(e)) {
        wr8(VAR(e), 0);
        wr8(ATTR(e), rd8(ATTR(e)) ^ 8);
        wrw(ANIM_STEP(e), 0x300);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    call(ENEMY_MOVE);
    if (hit()) return;
    if (rd8(VAR(e))) {
        if (rdw(VEL_Y(e)) >= 0) return;
        wrw(ANIM_STEP(e), 0x100);
        call(ENEMY_NEXT_STATE);
        return;
    }
    call_d0(FRICTION_X, 8);
    c->d[0] = (uint8_t)((rd8(COLL(e)) & 0x41) - 1);
    if (c->d[0]) {
        set_d0w(rdw(VEL_X(e)));
        if (rdw(VEL_X(e))) return;
    }
    wrw(VEL_X(e), (int)(c->d[0] & 0xFFFF));
    wr8(VAR(e), 0xFF);
    set_d0w(-0x200);
    call(SET_VEL_Y);
}

/* ---- Gragg & Glagg (batch 15) ---- */

static void stop(const char *what, Ent e)
{
    fprintf(stderr, "actions.c: %s (entity $%04X, step $%02X)\n", what, (unsigned)(e & 0xFFFF), rd8(STEP(e)));
    exit(3);
}

static uint32_t group(Ent e) { return (uint32_t)(int32_t)rdw(GOAL(e)); }   /* -$2D00: its group's record */

/* $1600C: touches; outside the boss rooms (WORLD_FLAGS bit 5) hit like any monster (EnemyCheckHit:
 * 1, the action returns); in them, with the shield while it blinks (TIMER) */
static int guard_touch(Ent e)
{
    if (!(rd8(WORLD_FLAGS) & 0x20)) { call(PLAYER_CONTACT_ALL); return hit(); }
    call(rd8(TIMER(e)) ? PLAYER_TOUCH : PLAYER_CONTACT_ALL);
    return 0;
}

/* $15F9C: its group calls it back (bit 1 of the record): step 8, and the original leaves past the
 * action: 1 */
static int called_back(Ent e)
{
    c->a[3] = group(e);
    uint8_t g = rd8(c->a[3]);
    wr8(c->a[3], g & (uint8_t)~2);
    if (!(g & 2)) return 0;
    wr8(STEP(e), 8);
    return 1;
}

/* $1602A: towards its home height (VEL_Y), $30 above it at most; z when there */
static int glided_home(void) { call(GLIDE_HOME); return (c->ccr & CC_Z) != 0; }

/* $15A86: Gragg (or Glagg) appears with its three companions (a group record from $127FE, three
 * free entities, its own drawing), falls, lands with a shake, its companions shown; in the boss
 * rooms it then throws up four rocks ($212) and drops back to the floor, and opens the fight
 * (message $67 the first time). Steps of its own in TIMER2 (0-8) */
static void a15A86(Ent e)
{
    if (begins(e)) {
        wr8(ANIM_RATE(e), 0x0C);
        wr32(TIMER(e), 0);
        call(FACE_PLAYER);
        if (rd8(WORLD_FLAGS) & 0x20) {
            set8(0xFFFF9858u, 8);
            wr8(0xFFFF9F05u, 8);
            wrw(VEL_X(c->a[2]), 0);
            wr8(0xFFFFA14Au, 0xFF);
            c->a[0] = 0xFFFFA102u;
            if (rdw(c->a[0])) c->a[0] += 2;
            wrw(c->a[0], (int)(c->a[4] & 0xFFFF));
            wrw(MAKER(e), (int)(c->a[0] & 0xFFFF));
            wr8(0xFFFFA114u, (uint8_t)(rd8(0xFFFFA114u) + 1));
        }
        call(GROUP_SLOT);
        if (c->ccr & CC_N) stop("no free group record ($15AD2 waits for ever)", e);
        set8(c->a[3], 0x80);
        wrw(GOAL(e), (int)(c->a[3] & 0xFFFF));
        c->d[1] = 3;
        do {
            call(FIND_FREE_ENTITY);
            if (c->ccr & CC_N) stop("no free entity ($15AE4 waits for ever)", e);
            uint32_t t = c->a[4]; c->a[4] = c->a[2]; c->a[2] = t;
            call(CLEAR_ENTITY);
            t = c->a[4]; c->a[4] = c->a[2]; c->a[2] = t;
            Ent m = c->a[2];
            set8(ALIVE(m), 0x80);
            wr8(TYPE(m), 0xFF);
            wrw(GOAL(m), (int)(c->a[4] & 0xFFFF));
            wrw(WORD_3DFE(m), rdw(WORD_3DFE(e)));
            wrw(ANIM_SET(m), rdw(ANIM_SET(e)));
            wrw(X(m), rdw(X(e)));
            c->d[0] = rd8(ATTR(e)) & 8;
            wr8(ATTR(m), (uint8_t)c->d[0]);
            c->d[0] = set_w(3, (3 - (c->d[1] & 0xFFFF)) & 0xFFFF);
            c->a[0] = 0x15C9Au;
            wr8(ANIM_RATE(m), rd8(0x15C9Au + (uint32_t)(int32_t)(int16_t)(c->d[0] & 0xFFFF)));
            c->d[0] = set_w(c->d[0], (c->d[0] * 2) & 0xFFFF);
            wrw(c->a[3] + 2 + (uint32_t)(int32_t)(int16_t)(c->d[0] & 0xFFFF), (int)(m & 0xFFFF));
            c->d[1] = set_b(c->d[1], (uint8_t)(c->d[1] - 1));
        } while ((uint8_t)c->d[1]);
        call_d0(SET_ANIM, 0);
        set8(ALIVE(e), 0x40);
        set8(MOVE_FLAGS(e), 3);
    }
    uint8_t sub = rd8(TIMER2(e)) & 0x7F;
    c->d[0] = sub;
    if (sub > 8 || (sub & 1)) stop("a step of $15B44 past its table", e);
    c->a[0] = 0x15B54u + (uint32_t)(int32_t)rdw(0x15B54u + sub);
    switch (sub) {
    case 0:                                         /* falling */
        call(FALL_AND_MOVE);
        call(CHECK_GROUNDED);
        if (!(c->ccr & CC_Z)) return;
        call_d0(QUEUE_SOUND, 57);
        wrw(SHAKE, -5);
        wrw(HOME_X(e), rdw(X(e)));
        wrw(HOME_Y(e), rdw(Y(e)));
        wr8(TIMER2(e), 2);
        call_d0(SET_ANIM, 2);
        return;
    case 2:                                         /* landed: its companions shown */
        if (rd8(ANIM_BUSY(e))) return;
        call_d0(SET_ANIM, 4);
        call(GROUP_TO_IT);
        {
            uint32_t t = c->a[4]; c->a[4] = c->a[2]; c->a[2] = t;
            call_d0(SET_ANIM, 6);
            set8(ALIVE(c->a[4]), 0x40);
            set8(ATTR(c->a[4]), 0x80);
            t = c->a[4]; c->a[4] = c->a[2]; c->a[2] = t;
        }
        if (!(rd8(WORLD_FLAGS) & 0x20)) { call(ENEMY_NEXT_STATE); return; }
        wrw(HOME_Y(e), rdw(HOME_Y(e)) + 0x10);
        wr8(TIMER2(e), 4);
        wrw(VAR(e), 0x0301);
        return;
    case 4:                                         /* four rocks, one every other frame */
        wr8(SHOT(e), (uint8_t)(rd8(SHOT(e)) - 1));
        if (rd8(SHOT(e))) return;
        wr8(SHOT(e), 2);
        c->d[2] = rd8(HALF_H(e));
        c->d[2] = set_w(c->d[2], (c->d[2] - 0x10 + (uint16_t)rdw(HOME_Y(e))) & 0xFFFF);
        c->d[1] = rd8(VAR(e));
        if (rd8(ATTR(e)) & 8) c->d[1] = set_w(c->d[1], (c->d[1] + 4) & 0xFFFF);
        c->d[1] = set_b(c->d[1], rd8(0x15C18u + (c->d[1] & 0xFFFF)));
        c->d[1] = ext_w(c->d[1]);
        c->d[1] = set_w(c->d[1], (c->d[1] + (uint16_t)rdw(HOME_X(e))) & 0xFFFF);
        set_d0w(0x212);
        call(SPAWN_ITEM_AT);
        wr8(VAR(e), (uint8_t)(rd8(VAR(e)) - 1));
        if (!(rd8(VAR(e)) & 0x80)) return;
        wr8(TIMER2(e), 6);
        /* fall through */
    case 6:                                         /* back down to the floor */
        call(ACCEL_Y);
        call(APPLY_VEL_Y);
        set_d0w(rdw(HOME_Y(e)));
        if ((uint16_t)rdw(HOME_Y(e)) > (uint16_t)rdw(Y(e))) { call(GROUP_TO_IT); return; }
        wrw(Y(e), (int)(c->d[0] & 0xFFFF));
        call(GROUP_TO_IT);
        wrw(SHAKE, -5);
        wr8(TIMER2(e), 8);
        call_d0(QUEUE_SOUND, 57);
        return;
    case 8: {                                       /* 10 frames, then the fight */
        uint8_t t = rd8(TIMER2(e));
        wr8(TIMER2(e), t | 0x80);
        if (!(t & 0x80)) wr8(SHOT(e), 0x0A);
        wr8(SHOT(e), (uint8_t)(rd8(SHOT(e)) - 1));
        if (rd8(SHOT(e))) return;
        uint8_t f = rd8(G_FLAGS);
        wr8(G_FLAGS, f | 2);
        if (f & 2) {
            wr8(0xFFFF9F05u, 0);
            wrw(TEXT_BUSY, 8);
            wrw(MESSAGE, 0x67);
        }
        call(ENEMY_NEXT_STATE);
        return;
    }
    }
}

/* $15C9E: defeated: blinking, puffs from its body for 16 x 4 frames (in the boss room the floor
 * is opened, $2812, and its partner told), then EnemyDefeated, and gone with its companions */
static void a15C9E(Ent e)
{
    if (begins(e)) {
        call(GROUP_BLINK);
        wrw(TIMER(e), 0x0810);
        if (rd8(WORLD_FLAGS) & 0x20) {
            c->a[0] = group(e);
            c->a[0] = (uint32_t)(int32_t)rdw(c->a[0] + 2);
            c->d[6] = set_w(c->d[6], (uint16_t)(rdw(X(c->a[0])) - 0x20));
            c->d[7] = set_w(c->d[7], (uint16_t)(rdw(Y(c->a[0])) + 0x10));
            c->d[6] = set_w(c->d[6], (c->d[6] & 0xFFFF) >> 4);
            c->d[7] = set_w(c->d[7], ((c->d[7] & 0xFFF0) << 2) & 0xFFFF);
            c->d[0] = 4; c->d[1] = 1;
            c->d[2] = set_w(c->d[2], 0x297);
            call(SET_MAP_BLOCKS);
            if (rd8(G_MODE)) {
                call(FREEZE_ENEMIES);
                set_d0w(rdw(X(e)));
                c->d[1] = set_w(c->d[1], (uint16_t)rdw(Y(e)));
                c->d[2] = 16; c->d[3] = 8;
                c->a[0] = 0x15DC0u;
                call(SHOW_TILES);
                set_d0w(rdw(0xFFFFA108u));
                if (c->d[0] & 0xFFFF) call(PARTNER_DOWN);
                set_d0w(rdw(0xFFFFA10Au));
                if (c->d[0] & 0xFFFF) call(PARTNER_DOWN);
            }
            c->a[3] = (uint32_t)(int32_t)rdw(MAKER(e));
            wr8(c->a[3], 0xFF);
            wr8(G_MODE, 0xFF);
            c->a[3] = group(e);
            wrw(c->a[3], 0);
            wr8(G_OPEN, 0);
        }
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) {
        wr8(TIMER(e), 4);
        c->d[4] = rd8(HALF_W(e));
        c->d[5] = rd8(HALF_H(e));
        call(RANDOM);
        c->d[0] = set_b(c->d[0], c->d[0] & 3);
        call(c->d[0] & 3 ? PUFF_B : PUFF_A);
        wr8(ANIM_RATE(c->a[2]), 8);
        return;
    }
    call(ENEMY_DEFEATED);
    wr8(0xFFFFA114u, (uint8_t)(rd8(0xFFFFA114u) - 1));
    if (!rd8(0xFFFFA114u)) {
        if (rd8(AREA) != 0x12) call_d0(QUEUE_SOUND, 19);
        wr8(0xFFFFA14Au, 0);
    }
    c->a[3] = group(e);
    for (int k = 2; k <= 6; k += 2) {
        c->a[2] = (uint32_t)(int32_t)rdw(c->a[3] + (uint32_t)k);
        wrw(ALIVE(c->a[2]), 0);
    }
    wrw(ALIVE(e), 0);
}

/* $15DD2: hurt: the damage (beaten: state 1), the group blinking, hit again meanwhile */
static void a15DD2(Ent e)
{
    if (begins(e)) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return; }
        call(GROUP_BLINK);
    }
    call(PLAYER_TOUCH);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = set_b(c->d[0], 1); new_state(0); return; }
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    call(GROUP_UNBLINK);
    call(ENEMY_NEXT_STATE);
}

/* $15E54: waits on the floor; in the boss room it fires when the fight lets it ($FFA111, taking
 * turns: $FFA112) and the player is more than $48 away, and now and then (one in four, two at a
 * time at most: $FFA106) goes to step 6 */
static void a15E54(Ent e)
{
    if (begins(e)) {
        wr8(NEXT_STATE(e), 0xFF);
        call(RANDOM_DELAY);
    }
    if (guard_touch(e)) return;
    if ((rd8(WORLD_FLAGS) & 0x20) && rd8(G_OPEN)
        && (rd8(G_MODE) || (int32_t)(int16_t)rdw(G_WORD) != (int32_t)c->a[4])) {
        wr8(G_OPEN, (uint8_t)(rd8(G_OPEN) - 1));
        wrw(G_WORD, (int)(c->a[4] & 0xFFFF));
        uint16_t d = (uint16_t)(rdw(X(c->a[2])) - rdw(X(e)));
        if (d & 0x8000) d = (uint16_t)-d;
        set_d0w(d);
        if (d > 0x48) shoot();
    }
    if (called_back(e)) return;
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(RANDOM);
    c->d[0] = set_w(c->d[0], c->d[0] & 3);
    if (c->d[0] & 3) return;
    if (rd8(WORLD_FLAGS) & 0x20) {
        if (rd8(G_COUNT6) >= 2) return;
        if (!(rd8(G_MODE) & 0x80) && (int32_t)(int16_t)rdw(G_LAST) == (int32_t)c->a[4]) return;
        wr8(G_COUNT6, (uint8_t)(rd8(G_COUNT6) + 1));
        wrw(G_LAST, (int)(c->a[4] & 0xFFFF));
    }
    wr8(STEP(e), 6);
}

/* $15EE6: rises from the floor bringing up a companion object (kind $800, $1A438), waits 30
 * frames at the top (its group told: record bit 0), and sinks back (step 3) */
static void a15EE6(Ent e)
{
    if (begins(e)) {
        wr8(TIMER2(e), 0xFF);
        c->d[1] = set_w(c->d[1], 0x800);
        call(ROOM_KIND);                            /* a0: kind $800's entry in the room's list */
        c->a[1] = 0xFFFFA394u;
        wrw(c->a[1], (int)(c->d[1] & 0xFFFF));
        wrw(c->a[1] + 2, rdw(X(e)));
        wrw(c->a[1] + 4, rdw(HOME_Y(e)));
        wrw(c->a[1] + 6, rdw(c->a[0]));
        c->a[1] += 6;
        int made;
        if (!(rd8(WORLD_FLAGS) & 0x20)) {
            call(SPAWN_FROM_A394);
            made = !(c->ccr & CC_N);
        } else {
            call(SPAWN_FROM_A394);
            if (c->ccr & CC_N) { wr8(G_COUNT6, (uint8_t)(rd8(G_COUNT6) - 1)); made = 0; }
            else {
                Ent o = c->a[2];
                wrw(GOAL(o), rdw(GOAL(e)));
                c->d[0] = rdw(0xFFFFA108u) ? 2 : 0;
                wr8(DELAY(o), (uint8_t)c->d[0]);
                c->a[3] = 0xFFFFA108u;
                wrw(c->a[3] + c->d[0], (int)(o & 0xFFFF));
                made = 1;
            }
        }
        if (made) wr8(TIMER2(e), 0);
    }
    if (guard_touch(e)) return;
    wr8(NEXT_STATE(e), rd8(STEP(e)));
    if (guard_touch(e)) return;
    uint8_t t = rd8(TIMER2(e));
    if (t & 0x80) {                                 /* sinking */
        if (called_back(e)) return;
        wrw(VEL_Y(e), 0x200);
        if (!glided_home()) return;
        wr8(STEP(e), 3);
        return;
    }
    if (!t) {                                       /* rising */
        wrw(VEL_Y(e), -0x200);
        if (!glided_home()) return;
        wr8(TIMER2(e), 0x1E);
        c->a[3] = group(e);
        set8(c->a[3], 1);
        return;
    }
    if (called_back(e)) return;                     /* at the top */
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    wr8(TIMER2(e), 0xFF);
}

/* $15FB0: rises, and sinks back when its group says (record bit 2) */
static void a15FB0(Ent e)
{
    if (begins(e)) wr8(TIMER2(e), 0);
    wr8(NEXT_STATE(e), rd8(STEP(e)));
    if (guard_touch(e)) return;
    c->a[3] = group(e);
    uint8_t t = rd8(TIMER2(e));
    if (!(t & 0x80)) {
        if (!t) {
            wrw(VEL_Y(e), -0x200);
            if (!glided_home()) return;
            wr8(TIMER2(e), 1);
        }
        uint8_t g = rd8(c->a[3]);
        wr8(c->a[3], g & (uint8_t)~4);
        if (!(g & 4)) { wr8(c->a[3], rd8(c->a[3]) & 0xFD); return; }
        wr8(TIMER2(e), 0xFF);
    }
    if (called_back(e)) return;
    wrw(VEL_Y(e), 0x200);
    if (!glided_home()) return;
    wr8(STEP(e), 3);
}

/* ---- on the water (batch 16) ---- */

/* $16E8E: the sword; hit, state 2 (the original leaves past the action: 1). Touching the player
 * hurts it: 20 less the player's armour (-$26FE), a quarter for kind $26, at least 1 */
static int burns(Ent e)
{
    c->a[2] = (Ent)(int32_t)rdw(PLAYER_W);
    call(BODY_VS_ATTACK);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) { c->d[0] = set_b(c->d[0], 2); new_state(0); return 1; }
    call(BODY_OVERLAP);
    if (!(c->ccr & (CC_C | CC_Z))) return 0;        /* bhi: apart */
    int16_t harm = (int16_t)(20 - rdw(ARMOUR(c->a[2])));
    c->d[0] = (uint16_t)harm;
    if (harm >= 0) {
        if ((uint16_t)rdw(TYPE(e)) == 0x26) harm = (int16_t)((uint16_t)harm >> 2);
        set_d0w(harm);
    }
    if (harm <= 0) { c->d[0] = 1; harm = 1; }
    wrw(PLAYER_HARM, harm);
    set8(0xFFFF9854u, 2);
    return 0;
}

/* swims along the surface (a height from the map, in WORD_31FE), turning at walls, at touches
 * from behind and at its range; facing the player $10-$6F away it goes on */
static void a16D70(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 0x77);
        call_d0(SET_ANIM, 14);
        set_d0w(rdw(Y(e)) & 0xFFF0);
        wrw(WORD_31FE(e), (int)(c->d[0] & 0xFFFF));
        wr8(VAR(e), 0xFF);
        call(RANDOM_TIMER_32);
    }
    if (despawned()) return;
    c->d[6] = set_w(c->d[6], (uint16_t)rdw(X(e)) & 0xFFF0);
    if ((uint16_t)(c->d[6] & 0xFFFF) != (uint16_t)rdw(VAR(e))) {   /* a new column: the surface's height */
        c->d[7] = 16;
        c->d[7] = set_w(c->d[7], (c->d[7] + (uint16_t)rdw(WORD_31FE(e))) & 0xFFFF);
        call(TYPE_AT_PIXEL);
        c->d[2] = set_w(c->d[2], ((c->d[2] * 2) & 0x10));
        wrw(WORD_31FE(e), rdw(WORD_31FE(e)) - (int16_t)(c->d[2] & 0xFFFF));
        c->d[7] = set_w(c->d[7], (c->d[7] + 0x40) & 0xFFFF);
        call(TILE_ATTR_AT);
        int carry = (c->d[2] >> 2) & 1;
        c->d[2] = set_w(c->d[2], (c->d[2] & 0xFFFF) >> 3);
        if (!carry) wrw(WORD_31FE(e), rdw(WORD_31FE(e)) + 0x10);
    }
    set_d0w(rdw(WORD_31FE(e)));
    call(TO_HEIGHT);
    call_d0(ACCEL_FACING, 16);
    call(ACCEL_X_);
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(ENEMY_MOVE);
    int turn = 0, back = 0;                         /* back: reversed as well */
    c->d[0] = (uint8_t)((rd8(COLL(e)) & 0x41) - 1);
    if (!c->d[0]) back = 1;
    else {
        uint8_t t = rd8(TOUCH(e));
        c->d[0] = set_b(c->d[0], t);
        if (t & 4) {                                /* touched: from behind, it turns */
            c->d[0] = set_b(c->d[0], (uint8_t)(t >> 1));
            if (t & 1) back = (rd8(ATTR(e)) & 8) != 0;
            else back = !(rd8(ATTR(e)) & 8);
        }
    }
    if (!back) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        call(PAST_RANGE_80);
        if (!(c->ccr & CC_C)) turn = 1;
        else {
            call(FACE_PLAYER);
            uint16_t d = (uint16_t)c->d[0];
            if (d & 0x8000) { d = (uint16_t)-d; set_d0w(d); }
            if (d >= 0x10 && d < 0x70) {
                wrw(GRAVITY(e), 0);
                wrw(VEL_Y(e), 0);
                call(ENEMY_NEXT_STATE);
                return;
            }
            call(RANDOM_TIMER_32);
            return;
        }
    }
    if (back) wrw(VEL_X(e), -rdw(VEL_X(e)));
    if (back || turn) wr8(ATTR(e), rd8(ATTR(e)) ^ 8);
    call(RANDOM_TIMER_32);
}

/* $16EDE: the damage (beaten: state 1), knocked away from the side it was hit on */
static void knocked_by_hit(Ent e)
{
    call(DAMAGE_ENEMY);
    if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return; }
    wrw(VEL_Y(e), 0);
    c->d[2] = set_w(c->d[2], 0x200);
    c->d[0] = set_b(c->d[0], rd8(TOUCH(e)) & 1 ? 0 : 0xFF);   /* seq */
    call(FACE_BY_D0);
    wrw(VEL_X(e), (int)(c->d[2] & 0xFFFF));
    call_d0(SET_ANIM, 4);
}

/* hurt: knocked away, slowing; hit again meanwhile */
static void a16ED0(Ent e)
{
    if (begins(e)) { clr8(BLINK(e), 0x40); knocked_by_hit(e); return; }
    call(APPLY_VEL_X);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { knocked_by_hit(e); return; }   /* beq $16EDE: the damage again */
    }
    call_d0(FRICTION_X, 16);
    if (c->ccr & CC_Z) call(ENEMY_NEXT_STATE);      /* stopped */
}

/* swims about its home height, burning what it touches; now and then, the player $10-$5F aside
 * and up to $5F below, it leaps at it (on, with WORD_31FE the leap's time) */
static void a16F50(Ent e)
{
    if (begins(e)) {
        call_d0(BURNING, 6);
        call(TURN_IN_RANGE);
    }
    if (despawned()) return;
    call(ACCEL_X_);
    set_d0w(rdw(HOME_Y(e)));
    call(TO_HEIGHT);
    call(APPLY_VEL);
    if (burns(e)) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    Ent t = (Ent)(int32_t)rdw(TARGET_W);
    c->a[2] = t;
    int16_t dx = (int16_t)(rdw(X(t)) - rdw(X(e)));
    c->d[2] = set_w(c->d[2], (uint16_t)dx);
    c->d[3] = set_w(c->d[3], (uint16_t)dx);
    c->d[0] = set_b(c->d[0], dx < 0 ? 0xFF : 0);   /* smi */
    call(FACE_BY_D0);                               /* d2: the distance */
    uint16_t d = (uint16_t)c->d[2];
    if (d < 0x10 || d >= 0x60) { call(TURN_IN_RANGE); return; }
    call(RANDOM);
    int carry = c->d[0] & 1;
    set_d0w((c->d[0] & 0xFFFF) >> 1);
    if (!carry) { call(TURN_IN_RANGE); return; }
    int16_t dy = (int16_t)(rdw(Y(t)) - rdw(Y(e)));
    c->d[1] = set_w(c->d[1], (uint16_t)dy);
    if (dy < 0 || (uint16_t)dy >= 0x60) { call(TURN_IN_RANGE); return; }
    set_d0w((int)(c->d[3] & 0xFFFF));
    c->d[2] = set_w(c->d[2], 0x200);
    call(ACCEL_TO);
    wrw(VEL_X(e), (int)(c->d[4] & 0xFFFF));
    wrw(VEL_Y(e), (int)(c->d[5] & 0xFFFF));
    c->d[2] = set_w(c->d[2], (((c->d[2] & 0xFFFF) >> 1) + 1) & 0xFFFF);
    wrw(WORD_31FE(e), (int)(c->d[2] & 0xFFFF));
    call(ENEMY_NEXT_STATE);
}

/* the leap: burning, for WORD_31FE frames */
static void a16FF0(Ent e)
{
    if (begins(e)) call_d0(BURNING, 14);
    if (despawned()) return;
    if (burns(e)) return;
    call(APPLY_VEL);
    wrw(WORD_31FE(e), rdw(WORD_31FE(e)) - 1);
    if (!rdw(WORD_31FE(e))) call(ENEMY_NEXT_STATE);
}

/* ---- the hopper and the chain (batch 17) ---- */

/* $17014: waits about its home height facing the player; with the player ahead within $60 and
 * below the right height, it crouches (TIMER: the height / 3) and springs, then swings about for
 * $15B frames ($170F4, $170EE) before it starts again ($17118). Phases in -$31FF */
static void a17014(Ent e)
{
    if (begins(e)) call(HOPPER_START);
    if (despawned()) return;
    call(APPLY_VEL);
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    uint8_t ph = rd8(PHASE(e));
    c->d[0] = set_b(c->d[0], ph);
    if (ph == 0) {
        set_d0w(rdw(HOME_Y(e)));
        call(TO_HEIGHT);
        call_d0(FRICTION_X, 7);
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        call(FACE_PLAYER);
        uint16_t d = (uint16_t)c->d[0];
        if (d & 0x8000) { d = (uint16_t)-d; set_d0w(d); }
        if (d >= 0x60) { call(RANDOM_TIMER_32); return; }
        int16_t h = (int16_t)(rdw(Y(c->a[2])) - 0x3C);
        set_d0w(h);
        if (h < 0) {
            h = (int16_t)(h + 8);
            set_d0w(h);
            if (h < 0) { call(RANDOM_TIMER_32); return; }
            c->d[0] = 1;
        } else {
            h = (int16_t)(h - rdw(Y(e)));
            set_d0w(h);
            if (h < 0 || (uint16_t)h >= 0x80) { call(RANDOM_TIMER_32); return; }
            c->d[1] = 3;
            uint32_t q = c->d[0] / 3;                   /* divu.w #3 of the whole of d0 */
            if (q <= 0xFFFF) c->d[0] = (c->d[0] % 3) << 16 | q;
            c->d[0] = set_b(c->d[0], (uint8_t)(c->d[0] + 1));
        }
        wr8(TIMER(e), (uint8_t)c->d[0]);
        set_d0w(0x180);
        wrw(SPEED_X(e), 0x180);
        wrw(SPEED_Y(e), 0x180);
        wrw(VEL_X(e), 0);
        wrw(VEL_Y(e), 0x300);
        wrw(GRAVITY(e), -0x18);
        wr8(PHASE(e), 1);
        call_d0(SET_ANIM, 14);
        return;
    }
    c->d[0] = set_b(c->d[0], (uint8_t)(ph - 1));
    if (ph == 1) {                                  /* crouching */
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (!rd8(TIMER(e))) wr8(PHASE(e), 2);
        return;
    }
    c->d[0] = set_b(c->d[0], (uint8_t)(ph - 2));
    if (ph == 2) {                                  /* springing up */
        call(ACCEL_Y);
        set_d0w(rdw(SPEED_Y(e)));
        if ((uint16_t)rdw(SPEED_Y(e)) < (uint16_t)rdw(VEL_Y(e))) return;
        wr8(PHASE(e), 3);
        wrw(WORD_31FE(e), 0x15B);
        wrw(GRAVITY(e), rdw(GRAVITY(e)) >> 1);
        call_d0(ACCEL_FACING, 12);
        return;
    }
    call(SWING_X);
    call(SWING_Y);
    wrw(WORD_31FE(e), rdw(WORD_31FE(e)) - 1);
    if (!rdw(WORD_31FE(e))) call(HOPPER_START);
}

/* $17132: a link of the chain. A follower (GOAL: the link ahead) moves as that link moved two
 * frames ago (the offsets in HIT_A/HIT_B); the head wanders about its home by its own offsets
 * (VAR, SHOT), every 16 frames towards the target, and grows the chain (kind $2E, up to 5 links,
 * DELAY frames apart). When no link is on the screen (checked every 16 frames) the head removes
 * the whole chain: the original leaves past the action then: 1 */
static int chain_link(Ent e)
{
    int16_t ahead = rdw(GOAL(e));
    set_d0w(ahead);
    if (ahead >= 0) {
        c->a[2] = (uint32_t)(int32_t)ahead;
        Ent a = c->a[2];
        c->d[1] = rd32(HIT_A(e));
        wr32(HIT_A(e), rd32(HIT_B(e)));
        uint16_t dx = (uint16_t)(rdw(X(a)) - rdw(X(e))), dy = (uint16_t)(rdw(Y(a)) - rdw(Y(e)));
        c->d[0] = (uint32_t)dx << 16 | dy;
        wr32(HIT_B(e), c->d[0]);
        c->d[0] = 4;
        c->d[1] = set_w(c->d[1], ((c->d[1] & 0xFFFF) << 4) & 0xFFFF);
        wrw(VEL_Y(e), (int)(c->d[1] & 0xFFFF));
        c->d[1] = (c->d[1] << 16) | (c->d[1] >> 16);
        c->d[1] = set_w(c->d[1], ((c->d[1] & 0xFFFF) << 4) & 0xFFFF);
        wrw(VEL_X(e), (int)(c->d[1] & 0xFFFF));
    } else {
        c->d[0] = (uint32_t)rdw(FRAME) & 15;
        int wander = 1;
        if (!c->d[0]) {                             /* is any link on the screen? */
            c->d[1] = set_w(c->d[1], c->a[4] & 0xFFFF);
            do {
                c->a[0] = (uint32_t)(int32_t)(int16_t)(c->d[1] & 0xFFFF);
                c->d[0] = set_b(c->d[0], (uint8_t)c->d[0] | rd8(ALIVE(c->a[0])));
                c->d[1] = set_w(c->d[1], (uint16_t)rdw(MAKER(c->a[0])));
            } while (!(c->d[1] & 0x8000));
            int seen = c->d[0] & 1;
            c->d[0] = set_b(c->d[0], (uint8_t)c->d[0] >> 1);
            if (!seen) {
                c->a[0] = c->a[4];
                for (;;) {
                    c->d[1] = set_w(c->d[1], (uint16_t)rdw(MAKER(c->a[0])));
                    if (c->d[1] & 0x8000) break;
                    c->a[0] = (uint32_t)(int32_t)(int16_t)(c->d[1] & 0xFFFF);
                    wr8(ALIVE(c->a[0]), 0);
                }
                call(DESPAWN);
                return 1;
            }
        }
        if (wander) {
            c->d[0] = set_b(c->d[0], rd8(VAR(e))); c->d[0] = ext_w(c->d[0]);
            set_d0w((int)((c->d[0] + (uint16_t)rdw(HOME_X(e))) & 0xFFFF));
            call(TO_X);
            c->d[0] = set_b(c->d[0], rd8(SHOT(e))); c->d[0] = ext_w(c->d[0]);
            set_d0w((int)((c->d[0] + (uint16_t)rdw(HOME_Y(e))) & 0xFFFF));
            call(TO_HEIGHT);
        }
    }
    call(APPLY_VEL);
    call(FACE_VELOCITY);
    c->d[0] = (uint8_t)((((rd8(ATTR(e)) & 8) >> 2) + 0x0C) & 0xFF);
    wr8(ANIM_RATE(e), (uint8_t)c->d[0]);
    if (rdw(MAKER(e)) < 0 && rd8(DELAY(e))) {       /* the last link: time for another? */
        wr8(DELAY(e), (uint8_t)(rd8(DELAY(e)) - 1));
        if (!rd8(DELAY(e))) {
            if (rd8(PHASE(e)) >= 5) return 0;
            c->a[0] = 0xFFFFA394u;
            wrw(c->a[0], 0x2E);
            wr32(c->a[0] + 2, rd32(HOME_X(e)));
            wrw(c->a[0] + 6, rdw(WORD_3DFE(e)));
            c->a[0] += 6;
            call(SPAWN_FROM_A394);
            if (c->ccr & CC_N) { wr8(DELAY(e), (uint8_t)(rd8(DELAY(e)) + 1)); return 0; }
            Ent n = c->a[2];
            c->d[1] = set_w(c->d[1], (uint16_t)rdw(TYPE(n)));
            call(ROOM_KIND);
            wrw(WORD_3DFE(n), rdw(c->a[0]));
            wr32(HOME_X(n), rd32(HOME_X(e)));
            wrw(MAKER(e), (int)(n & 0xFFFF));
            wrw(GOAL(n), (int)(c->a[4] & 0xFFFF));
            wr8(MAKER(n), 0xFF);
            wr8(LINK(n), 0xFF);
            wr8(CHAIN_B(n), 0xFF);
            wr8(MOVE_FLAGS(n), 3);
            wr32(HIT_A(e), 0);
            wr32(HIT_B(e), 0);
            c->d[0] = set_b(c->d[0], (uint8_t)(rd8(PHASE(e)) + 1));
            wr8(PHASE(n), (uint8_t)c->d[0]);
        }
    }
    if (rdw(GOAL(e)) >= 0) return 0;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));    /* the head: every 16 frames towards the target */
    if (rd8(TIMER(e))) return 0;
    wr8(TIMER(e), 0x10);
    Ent t = (Ent)(int32_t)rdw(TARGET_W);
    c->a[2] = t;
    c->d[1] = 16;
    set_d0w(rdw(X(t)));
    if ((uint16_t)rdw(X(t)) < (uint16_t)rdw(X(e))) c->d[1] = set_b(c->d[1], 0xF0);
    {
        int8_t v = (int8_t)rd8(VAR(e)), s = (int8_t)(uint8_t)c->d[1];
        int r = v + s;
        wr8(VAR(e), (uint8_t)r);
        if (r > 127 || r < -128) {                  /* overflow: back the other way */
            c->d[1] = set_b(c->d[1], (uint8_t)(s * 2));
            wr8(VAR(e), (uint8_t)(rd8(VAR(e)) - (uint8_t)c->d[1]));
        }
    }
    c->d[1] = 16;
    set_d0w(rdw(Y(t)));
    if ((uint16_t)rdw(Y(t)) < (uint16_t)rdw(Y(e))) c->d[1] = set_b(c->d[1], 0xF0);
    wr8(SHOT(e), (uint8_t)(rd8(SHOT(e)) + (uint8_t)c->d[1]));
    if ((int8_t)rd8(SHOT(e)) > (int8_t)0xE0) {      /* not higher than $E0 above home */
        c->d[1] = set_b(c->d[1], (uint8_t)(c->d[1] * 2));
        wr8(SHOT(e), (uint8_t)(rd8(SHOT(e)) - (uint8_t)c->d[1]));
    }
    return 0;
}

/* $1729A: a link defeated; the chain closes up behind it (the next link takes its place, or as
 * the head its looks), then as $142E4 */
static void a1729A(Ent e)
{
    if (!(rd8(STEP(e)) & 0x80)) {
        int16_t ahead = rdw(GOAL(e));
        set_d0w(ahead);
        int restart = 0;
        if (ahead < 0) {                            /* the head: the next link becomes the head */
            int16_t nx = rdw(MAKER(e));
            set_d0w(nx);
            if (nx >= 0) {
                c->a[2] = (uint32_t)(int32_t)nx;
                Ent n = c->a[2];
                wr8(LINK(e), 0xFF);
                wr8(GOAL(n), 0xFF);
                wr8(TIMER(n), 0x10);
                c->a[0] = 0x172CEu;
                call(COPY_FIELDS);
                clr8(BLINK(n), 0x81);
                restart = 1;
            }
        } else {                                    /* the links on either side joined */
            c->a[2] = (uint32_t)(int32_t)ahead;
            Ent a = c->a[2];
            int16_t nx = rdw(MAKER(e));
            set_d0w(nx);
            wrw(MAKER(a), nx);
            if (nx >= 0) {
                c->a[0] = (uint32_t)(int32_t)nx;
                wrw(GOAL(c->a[0]), (int)(a & 0xFFFF));
            } else if (rdw(GOAL(a)) < 0) restart = 1;
        }
        if (restart) {                              /* a new chain grows from it */
            wr8(DELAY(c->a[2]), 0x78);
            wr8(PHASE(c->a[2]), 0);
        }
    }
    a142E4(e);
}

/* $17310: a link hurt (flashing), the chain moving on */
static void a17310(Ent e)
{
    if (begins(e)) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return; }
        set8(BLINK(e), 0x40);
        wrw(FLASH(e), 0x2101);
        wr8(TIMER2(e), 0x1E);
        call_d0(SET_ANIM, 4);
        return;
    }
    if (chain_link(e)) return;
    call(PLAYER_TOUCH);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return; }
    }
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    clr8(BLINK(e), 0x40);
    call(ENEMY_NEXT_STATE);
}

/* $17362: a link moving with the chain; the first time, a link without a leader becomes a head */
static void a17362(Ent e)
{
    if (begins(e)) {
        if (!rd8(CHAIN_A(e))) {
            wr8(CHAIN_A(e), 0xFF);
            uint8_t lead = rd8(CHAIN_B(e));
            c->d[0] = set_b(c->d[0], lead);
            if (!lead) {
                wr8(GOAL(e), 0xFF);
                wr8(MAKER(e), 0xFF);
                wr8(PHASE(e), 0);
                wr8(SHOT(e), 0xE0);
            }
            c->d[0] = 16;
            wr8(TIMER(e), 16);
            wr8(DELAY(e), 16);
        }
        call_d0(SET_ANIM, 14);
    }
    if (chain_link(e)) return;
    call(PLAYER_CONTACT_ALL);
    hit();
}

/* ---- bouncers, a shooter, a dodger (batch 18) ---- */

/* $17418: |d0| / 64 (a word; the caller's own move.w sets the flags it tests) */
static void slow_of(int16_t v) { uint16_t a = (uint16_t)v; if (v < 0) a = (uint16_t)-a; set_d0w(a >> 6); }

/* drifts as it was thrown, slowing, bouncing off walls ($17420), for 60 frames; then on, or
 * (kind $18, one time in two) step 6 */
static void a173AA(Ent e)
{
    if (begins(e)) {
        slow_of(rdw(VEL_X(e)));
        wrw(ACCEL_X(e), (int)(c->d[0] & 0xFFFF));
        slow_of(rdw(VEL_Y(e)));
        wrw(GRAVITY(e), (int)(c->d[0] & 0xFFFF));
        wr8(TIMER(e), 0x3C);
        call_d0(SET_ANIM, 6);
    }
    if (despawned()) return;
    set_d0w(rdw(ACCEL_X(e)));
    call(FRICTION_X);
    set_d0w(rdw(GRAVITY(e)));
    call(FRICTION_Y);
    call(ENEMY_MOVE);
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(BOUNCE);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    if ((uint16_t)rdw(TYPE(e)) == 0x18) {
        call(RANDOM);
        int carry = c->d[0] & 1;
        c->d[0] = set_b(c->d[0], (uint8_t)c->d[0] >> 1);
        if (!carry) { wr8(STEP(e), 6); return; }
    }
    call(ENEMY_NEXT_STATE);
}

/* flies at the target within an eighth of a turn of level ($174B0), speeding up to $100, bouncing
 * off walls, for 32 frames */
static void a1743C(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 0x7F);
        c->a[2] = (Ent)(int32_t)rdw(TARGET_W);
        call(FACE_PLAYER);
        call(AIM_LEVEL);
        c->d[4] = set_w(c->d[4], 0x100);
        call(VEL_AT_COARSE);
        uint16_t v = (uint16_t)c->d[0];
        c->d[1] = set_w(c->d[1], v);
        if (v & 0x8000) c->d[1] = set_w(c->d[1], (uint16_t)-v);
        wrw(SPEED_X(e), (int)(c->d[1] & 0xFFFF));
        v = (uint16_t)c->d[5];
        c->d[1] = set_w(c->d[1], v);
        if (v & 0x8000) c->d[1] = set_w(c->d[1], (uint16_t)-v);
        wrw(SPEED_Y(e), (int)(c->d[1] & 0xFFFF));
        set_d0w((int16_t)c->d[0] >> 4);
        wrw(ACCEL_X(e), (int)(c->d[0] & 0xFFFF));
        c->d[5] = set_w(c->d[5], (uint16_t)((int16_t)c->d[5] >> 4));
        wrw(GRAVITY(e), (int)(c->d[5] & 0xFFFF));
        wr8(TIMER(e), 0x20);
        call_d0(SET_ANIM, 8);
    }
    if (despawned()) return;
    call(ACCEL_X_);
    call(ACCEL_Y);
    call(ENEMY_MOVE);
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    call(BOUNCE);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) call(ENEMY_NEXT_STATE);
}

/* stands firing three times ($16 shots from $17546), when the animation marks it */
static void a174E6(Ent e)
{
    if (begins(e)) {
        call(FACE_PLAYER);
        call(STOP);
        wr32(ACCEL(e), c->d[0]);
        clr8(ATTR(e), 3);
        wr8(TIMER(e), 3);
        call_d0(SET_ANIM_NOW, 12);
    }
    if (despawned()) return;
    call(CONTACT_AND_MOVE);
    uint8_t a = rd8(ATTR(e));
    wr8(ATTR(e), a & (uint8_t)~1);
    if (a & 1) {
        set_d0w(0x16);
        c->a[0] = 0x17546u;
        call(SHOT_AT);
        if (c->ccr & CC_N) return;
        c->d[0] = rd8(ATTR(e)) & 8;
        wr8(ATTR(c->a[2]), (uint8_t)c->d[0]);
        return;
    }
    if (rd8(ANIM_BUSY(e))) return;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) { call(ENEMY_NEXT_STATE); return; }
    call_d0(SET_ANIM_NOW, 12);
}

/* a shot set off at the target, give or take a random angle, at $180 (its graphics from the
 * room's list, $4910) */
static void a17548(Ent e)
{
    set8(ALIVE(e), 0x40);
    call_d0(SET_ANIM_NOW, 0);
    call_d0(SET_ANIM, 8);
    c->d[1] = set_w(c->d[1], (uint16_t)rdw(TYPE(e)));
    call(ROOM_KIND);
    wrw(WORD_3DFE(e), rdw(c->a[0]));
    c->a[2] = (Ent)(int32_t)rdw(TARGET_W);
    call(AIM_LEVEL);
    call(RANDOM);
    c->d[0] = set_w(c->d[0], ((c->d[0] & 0xFFF) - 0x800) & 0xFFFF);
    c->d[3] = set_w(c->d[3], (c->d[3] + c->d[0]) & 0xFFFF);
    c->d[4] = set_w(c->d[4], 0x180);
    call(SET_VEL_AT);
    call(ENEMY_NEXT_STATE);
}

/* the shot flying */
static void a17588(Ent e)
{
    (void)e;
    call(APPLY_VEL);
    call(PLAYER_CONTACT_ALL);
    if (hit()) return;
    despawned();
}

/* $17652: dodging the player's sword: back, at half its speed, for 16 frames (PHASE set) */
static void dodge(Ent e)
{
    wr8(PHASE(e), 0xFF);
    wr8(TIMER(e), 0x10);
    int16_t v = rdw(VEL_X(e));
    set_d0w(v);
    if (v >= 0) { v = (int16_t)-v; set_d0w(v); }
    set_d0w(v >> 1);
    call(SET_VEL_X_FACING);
}

/* burning, it swims at the player about its home height and slows near it; when the player
 * faces it with the sword out (the target's hit records), it dodges back */
static void a1759A(Ent e)
{
    if (begins(e)) {
        wr8(MOVE_FLAGS(e), 0x77);
        set8(BLINK(e), 0x40);
        wrw(FLASH(e), 1);
        call(FACE_PLAYER);
        call_d0(SET_ANIM, 14);
        wr8(TIMER(e), 0);
    }
    if (despawned()) return;
    if (burns(e)) return;
    set_d0w(rdw(HOME_Y(e)));
    call(TO_HEIGHT);
    Ent t = (Ent)(int32_t)rdw(TARGET_W);
    c->a[2] = t;
    if (rd8(TIMER(e))) {
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        c->d[0] = 0xFFFFFFE0u;                      /* -32: backing off */
        if (!rd8(PHASE(e))) {
            c->d[1] = set_b(c->d[1], rd8(ATTR(t)));
            c->d[2] = set_b(c->d[2], rd8(ATTR(e)));
            c->d[1] = set_b(c->d[1], (uint8_t)(c->d[1] ^ c->d[2]));
            if ((c->d[1] & 8) && rd32(HIT_A(t))) dodge(e);
            else c->d[0] = 32;
        }
        call(ACCEL_FACING);
        call(ACCEL_X_);
        call(APPLY_VEL);
        return;
    }
    int16_t s = (int16_t)(rdw(VEL_X(e)) >> 4);
    if (!s) s = 1;
    set_d0w(s);
    if (s == 1 && (rdw(VEL_X(e)) >> 4) == 0) c->d[0] = 1;
    wrw(VEL_X(e), rdw(VEL_X(e)) - s);
    call(APPLY_VEL);
    call(FACE_ENTITY);
    uint16_t d = (uint16_t)c->d[0];
    if (d & 0x8000) { d = (uint16_t)-d; set_d0w(d); }
    if (d >= 0x40) return;
    wr8(PHASE(e), 0);
    wr8(TIMER(e), 0x10);
    if (!rd32(HIT_A(t))) return;
    c->d[0] = set_b(c->d[0], rd8(ATTR(t)));
    c->d[1] = set_b(c->d[1], rd8(ATTR(e)));
    c->d[0] = set_b(c->d[0], (uint8_t)(c->d[0] ^ c->d[1]));
    if (!(c->d[0] & 8)) return;
    dodge(e);
}

/* ---- a boss and its brood (batch 19) ---- */

/* $17704: touched from behind (TOUCH bit 2): TIMER 1 and state 5, leaving past the action */
static int touched_back(Ent e)
{
    if (!(rd8(TOUCH(e)) & 4)) return 0;
    wr8(TIMER(e), 1);
    c->d[0] = 5;
    new_state(0);
    return 1;
}

/* the boss rises by its height, facing the player; a frame later it marks itself the room's boss
 * and goes to state 3 */
static void a1771C(Ent e)
{
    if (!begins(e)) {
        wrw(BOSS_MARK, (uint16_t)e);
        c->d[0] = 3;
        new_state(0);
        return;
    }
    wr8(ANIM_RATE(e), 0x0E);
    call_d0(SET_ANIM_NOW, 0);
    c->d[0] = rd8(HALF_H(e));
    wrw(Y(e), (uint16_t)(rdw(Y(e)) - c->d[0]));
    set8(ALIVE(e), 0x40);
    call(FACE_PLAYER);
}

/* the boss beaten: the music stopped, the room frozen, its tiles shown ($177C4); 20 puffs, 4
 * frames apart, then 16 frames on, it is gone (sound 19) */
static void a1774E(Ent e)
{
    if (begins(e)) {
        if (rd8(SOUND_MODE) != 0x12) call(QUEUE_SOUND_FF);
        call(FREEZE_ENEMIES);
        call(START_BLINK);
        wrw(TIMER(e), 0x1406);
        set_d0w(rdw(X(e)));
        c->d[1] = set_w(c->d[1], rdw(Y(e)));
        c->d[2] = 32; c->d[3] = 16;
        c->a[0] = 0x177C4u;
        call(SHOW_TILES);
        call_d0(SET_ANIM, 2);
        return;
    }
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    c->d[0] = set_b(c->d[0], rd8(TIMER(e)));
    if (!(c->d[0] & 0xFF)) {
        wr8(ALIVE(e), 0);
        wr8(BOSS_MARK, 0xFF);
        if (rd8(SOUND_MODE) != 0x12) call_d0(QUEUE_SOUND, 19);
        wrw(Y(e), (uint16_t)(rdw(Y(e)) + 0x10));
        call(ENEMY_DEFEATED);
        return;
    }
    c->d[4] = rd8(BODY_W(e));
    c->d[5] = rd8(BODY_H(e));
    call(PUFF_B);
    c->d[0] = 4;
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (!rd8(TIMER(e))) c->d[0] = 16;
    wr8(TIMER2(e), (uint8_t)c->d[0]);
}

/* stands a random while on the ground, then walks (4) or, one time in two, leaps (5) */
static void a177D6(Ent e)
{
    if (begins(e)) {
        wrw(VEL_X(e), 0);
        call_d0(SET_ANIM, 6);
        call(RANDOM_DELAY);
        return;
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (touched_back(e)) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    if (rd8(TIMER2(e))) {
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
    }
    call(RANDOM);
    c->d[1] = 4;
    int odd = c->d[0] & 1;
    c->d[0] = set_b(c->d[0], (c->d[0] & 0xFF) >> 1);
    if (odd) { wr8(TIMER(e), 0); c->d[1] = 5; }
    set_d0w(c->d[1]);
    new_state(0);
}

/* walks a random while, turning at walls, then leaps (5) */
static void a17822(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 8);
        call(WALK_FORWARD);
        call(RANDOM_DELAY);
        return;
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    call(TURN_AT_WALL);
    if (touched_back(e)) return;
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    wr8(TIMER(e), 0);
    c->d[0] = 5;
    new_state(0);
}

/* leaps: at the target (or, TIMER bit 0, back from the player), turning at walls; landed, a
 * random next state ($178C8) */
static void a17860(Ent e)
{
    if (begins(e)) {
        call_d0(SET_ANIM, 10);
        uint8_t t = rd8(TIMER(e));
        wr8(TIMER(e), t & 0xFE);
        if (!(t & 1)) {
            c->a[2] = (uint32_t)(int32_t)(int16_t)rdw(TARGET_W);
            c->d[1] = 48;
            call(VELOCITY_TOWARD);
        } else {
            call(FACE_PLAYER);
            set_d0w(0xFE00);
            call(SET_VEL_X_FACING);
        }
        set_d0w(0xFA00);
        call(SET_VEL_Y);
        return;
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    c->d[0] = (uint8_t)((65 & rd8(COLL(e))) - 1);
    if (!c->d[0]) {
        wrw(VEL_X(e), (uint16_t)-rdw(VEL_X(e)));
        call(FACE_VELOCITY);
    }
    call(CHECK_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    call(RANDOM);
    set_d0w(c->d[0] & 7);
    c->d[0] = set_b(c->d[0], rd8(0x178C8u + (c->d[0] & 7)));
    new_state(0);
}

/* with five of the room's enemies about, leaps (5); else it stops, facing the player, and lays
 * one young (kind 8, or 6, from $1793C) when its animation marks it; 30 frames, then leaps back */
static void a178D0(Ent e)
{
    if (begins(e)) {
        uint32_t a0 = (uint32_t)(int32_t)(int16_t)rdw(0xFFFFA11Au);
        c->d[0] = 15; c->d[1] = 4;
        for (;;) {
            if (rd8(ALIVE(a0)) & 0x80) {
                c->d[1] = set_w(c->d[1], c->d[1] - 1);
                if ((c->d[1] & 0xFFFF) == 0xFFFF) { c->a[0] = a0; c->d[0] = 5; new_state(0); return; }
            }
            a0 += 4;
            c->d[0] = set_w(c->d[0], c->d[0] - 1);
            if ((c->d[0] & 0xFFFF) == 0xFFFF) break;
        }
        c->a[0] = a0;
        call_d0(SET_ANIM, 12);
        wr8(TIMER2(e), 0x1E);
        call(STOP);
        wr8(VAR(e), 0);
        call(FACE_PLAYER);
        return;
    }
    call(CONTACT_AND_MOVE);
    if (hit()) return;
    if (!rd8(VAR(e)) && rd8(FRAME_SEEN(e))) {
        wr8(VAR(e), (uint8_t)(rd8(VAR(e)) + 1));
        call(FIND_FREE_ENTITY);
        if (!(c->ccr & CC_N)) {
            call(RANDOM);
            set_d0w(c->d[0] & 0xE);
            c->d[1] = set_w(c->d[1], rdw(0x1793Cu + (c->d[0] & 0xE)));
            c->d[0] = 0xFFFFFFFFu;
            c->d[6] = set_w(c->d[6], rdw(X(e)));
            c->d[7] = set_w(c->d[7], (uint16_t)(rdw(Y(e)) - 8));
            call(SPAWN_KIND);
            Ent s = c->a[2];                        /* exg a2,a4: the young one's turn */
            c->a[4] = s; c->a[2] = e;
            wr8(STEP(s), 5);
            call_d0(SET_ANIM_NOW, 0);
            c->d[1] = set_w(c->d[1], rdw(SPEED_X(s)));
            c->d[0] = 8 & rd8(ATTR(e));
            if (c->d[0]) c->d[1] = set_w(c->d[1], (uint16_t)-c->d[1]);
            set8(ATTR(s), (uint8_t)c->d[0]);
            wrw(VEL_X(s), (uint16_t)c->d[1]);
            c->d[0] = 3 | rd8(MOVE_FLAGS(s));       /* its flags, with the parent's bit 7 */
            c->d[1] = set_b(c->d[1], (uint8_t)(rd8(MOVE_FLAGS(e)) << 1));
            c->d[0] = set_b(c->d[0], (uint8_t)((rd8(MOVE_FLAGS(e)) & 0x80) | (c->d[0] & 0x7F)));
            wr8(MOVE_FLAGS(s), (uint8_t)c->d[0]);
            c->a[4] = e; c->a[2] = s;
        }
    }
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    wr8(TIMER(e), 1);
    c->d[0] = 5;
    new_state(0);
}

/* ---- the two-part boss (batch 20) ----
 * A head and a body, partners through MAKER. Its helpers step a4 to the partner and back
 * (movea.w MAKER(a4),a4 twice), so after them the code goes on with whatever a4 then holds:
 * c->a[4] read again, not the entity the action began with. */

static Ent partner_of(Ent e) { return (uint32_t)(int32_t)(int16_t)rdw(MAKER(e)); }
static void swap_a2_a4(void) { uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t; }

/* $179CC, $1838A: touching the player, and the sword; hit, state 2, leaving past the action;
 * else the partner's touch (PAIR_TOUCH, MOUNT_TOUCH) */
static int pair_hit(uint32_t touch)
{
    call(PLAYER_CONTACT_ALL);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) { c->d[0] = 2; new_state(0); return 1; }
    call(touch);
    return 0;
}

/* $17DB8: VEL_X += ACCEL_X, moved across */
static void push_x(Ent e)
{
    set_d0w(rdw(ACCEL_X(e)));
    wrw(VEL_X(e), (uint16_t)(rdw(VEL_X(e)) + c->d[0]));
    call(APPLY_VEL_X);
}

/* landed: a bounce at half the speed across, up at d0, for $20 frames (sound 57) */
static void bounce_up(uint16_t vy)
{
    call_d0(QUEUE_SOUND, 57);
    Ent e = c->a[4];
    wrw(VEL_X(e), (uint16_t)((int16_t)rdw(VEL_X(e)) >> 1));
    wrw(VEL_Y(e), vy);
    wr8(TIMER(e), 0x20);
}

/* the pair leaps in: the body made (MAKE_PARTNER), both thrown up from $100 to the left
 * ($17AD2) and landing twice, the body first; then the room's walls set ($9C16, $9962), the head
 * on the body, and on to state 3 */
static void a17A44(Ent e)
{
    if (begins(e)) {
        wr8(0xFFFF9F05u, 4);
        call(FIND_FREE_ENTITY);
        wrw(X(e), (uint16_t)(rdw(X(e)) - 0x100));
        call(MAKE_PARTNER);
        wr8(TYPE(c->a[2]), 0xFF);
        call_d0(PAIR_ANIM, 0);
        call(UPDATE_ANIM);
        swap_a2_a4(); call(UPDATE_ANIM); swap_a2_a4();
        e = c->a[4];
        Ent p = c->a[2];
        c->d[0] = 8;
        set8(ATTR(e), 8); set8(ATTR(p), 8);
        c->d[0] = set_b(c->d[0], rd8(HALF_H(e))); wrw(Y(e), (uint16_t)(rdw(Y(e)) - c->d[0]));
        c->d[0] = set_b(c->d[0], rd8(HALF_H(p))); wrw(Y(p), (uint16_t)(rdw(Y(p)) - c->d[0]));
        uint32_t t = 0x17AD2u;
        wr8(MOVE_FLAGS(e), rd8(t)); wr8(MOVE_FLAGS(p), rd8(t + 1));
        wrw(SPEED_X(e), rdw(t + 2)); wrw(SPEED_Y(p), rdw(t + 4));
        wr8(ANIM_RATE(e), rd8(t + 6)); wr8(ANIM_RATE(p), rd8(t + 7));
        wrw(VEL_X(e), rdw(t + 8)); wrw(VEL_X(p), rdw(t + 8));
        wrw(VEL_Y(e), rdw(t + 10)); wrw(VEL_Y(p), rdw(t + 10));
        wr8(TIMER(e), rd8(t + 12)); wr8(TIMER(p), rd8(t + 12));
        c->a[0] = t + 13;
        c->d[0] = 64;
        set8(ALIVE(e), 64); set8(ALIVE(p), 64);
        return;
    }
    c->d[0] = rd8(PHASE(e));
    c->a[0] = 0x17AF0u + (uint32_t)(int32_t)(int16_t)rdw(0x17AF0u + c->d[0]);
    switch (c->d[0]) {
    case 0:                                         /* the body falls; landed, it bounces */
        call(PAIR_FALL);
        if (!(c->ccr & CC_Z)) { c->a[4] = partner_of(c->a[4]); return; }
        bounce_up(0xFB80);
        c->a[4] = partner_of(c->a[4]);
        break;
    case 2:                                         /* the body falls; landed, it stops */
        call(PAIR_FALL);
        if (!(c->ccr & CC_Z)) { c->a[4] = partner_of(c->a[4]); return; }
        call(STOP);
        c->a[4] = partner_of(c->a[4]);
        break;
    case 4:                                         /* the head falls; landed, it bounces */
        call(PAIR_FALL_HERE);
        if (!(c->ccr & CC_Z)) return;
        bounce_up(0xFA00);
        break;
    case 6:                                         /* the head lands: the walls; on the body */
        call(PAIR_FALL_HERE);
        if (!(c->ccr & CC_Z)) return;
        wrw(0xFFFF9C16u, 0x68);
        wrw(0xFFFF9962u, 8);
        call(PAIR_ALIGN_X);
        call(PAIR_ON_BODY);
        break;
    default:                                        /* 8 */
        wr8(0xFFFF9F05u, 0);
        set8(0xFFFF99DEu, 1);
        c->d[0] = 3;
        new_state(0);
        return;
    }
    e = c->a[4];
    wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 2));
}

/* the pair beaten: the music stopped, the room frozen, the head's tiles shown ($17C72), the
 * screen's two strips cleared ($FF5200), the body's tiles fading ($17C84) and the head breaking
 * up ($17C32, by $182AA) over $80 frames; then both gone (sound 19) */
static void a17B8A(Ent e)
{
    if (begins(e)) {
        if (rd8(SOUND_MODE) != 0x12) call(QUEUE_SOUND_FF);
        call(FREEZE_ENEMIES);
        set_d0w(rdw(X(e)));
        c->d[1] = set_w(c->d[1], rdw(Y(e)));
        c->d[2] = 32; c->d[3] = 16;
        c->d[1] = set_w(c->d[1], c->d[1] + 16);
        c->a[0] = 0x17C72u;
        call(SHOW_TILES);
        call_d0(PAIR_ANIM, 2);
        e = c->a[4];
        wrw(TIMER(e), 0x8000);
        set8(0xFFFF99DEu, 2);
        uint32_t a0 = 0xFF5200u;
        for (int row = 0; row < 5; row++, a0 += 0x100)
            for (int i = 0; i < 32; i++, a0 += 4) wr32(a0, 0);
        c->a[0] = a0; c->d[0] = 0; c->d[1] = set_w(c->d[1], 0xFFFF); c->d[2] = set_w(c->d[2], 0xFFFF);
        call(FADE_TILES);
        c->d[0] = 0xFFFFFFFFu;
        wrw(0xFFFF8E36u, 0xFFFF);
        wrw(0xFFFF8EB6u, 0xFFFF);
        c->d[2] = 13;
        call(BREAK_UP_START);
        return;
    }
    c->d[1] = set_w(c->d[1], 0x80);
    c->d[1] = set_b(c->d[1], (uint8_t)(0x80 - rd8(TIMER(e))));
    int odd = c->d[1] & 1;
    c->d[1] = set_b(c->d[1], (c->d[1] & 0xFF) >> 1);
    if (!odd) {
        c->d[1] = set_b(c->d[1], rd8(0x17C32u + (c->d[1] & 0xFFFF)));
        c->d[2] = 13;
        call(BREAK_UP);
    }
    e = c->a[4];
    wr8(VAR(e), (uint8_t)(rd8(VAR(e)) - 1));
    if (!rd8(VAR(e))) call(FADE_TILES);
    e = c->a[4];
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    wrw(Y(e), (uint16_t)(rdw(Y(e)) + 0x20));
    call(ENEMY_DEFEATED);
    if (rd8(SOUND_MODE) != 0x12) call_d0(QUEUE_SOUND, 19);
    e = c->a[4];
    c->a[2] = partner_of(e);
    wr8(ALIVE(e), 0);
    wr8(ALIVE(c->a[2]), 0);
}

/* hurt: both knocked away from the sword at $400, slowing by $40 a frame for 16 frames, the head
 * first, then the body; then state 3 */
static void a17D22(Ent e)
{
    if (begins(e)) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return; }
        call_d0(PAIR_ANIM, 4);
        e = c->a[4];
        set_d0w(0x400);
        c->d[1] = 0xFFFFFFC0u;
        if (!(rd8(TOUCH(e)) & 1)) {
            set_d0w(-0x400);
            c->d[1] = set_w(c->d[1], 0x40);
        }
        wrw(VEL_X(e), (uint16_t)c->d[0]);
        wrw(ACCEL_X(e), (uint16_t)c->d[1]);
        c->a[0] = partner_of(e);
        wrw(VEL_X(c->a[0]), (uint16_t)c->d[0]);
        wrw(ACCEL_X(c->a[0]), (uint16_t)c->d[1]);
        c->d[0] = 16;
        wr8(TIMER(e), 16);
        wr8(TIMER(c->a[0]), 16);
        wr8(TIMER2(e), 0);
        return;
    }
    call(PLAYER_TOUCH);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return; }
    }
    call(PAIR_TOUCH);
    e = c->a[4];
    if (!rd8(TIMER2(e))) {
        push_x(e);
        e = c->a[4];
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) + 1));
        call(STOP);
        return;
    }
    c->a[4] = partner_of(e);
    push_x(c->a[4]);
    c->a[2] = partner_of(c->a[4]);
    swap_a2_a4();
    Ent b = c->a[2];
    wr8(TIMER(b), (uint8_t)(rd8(TIMER(b)) - 1));
    if (rd8(TIMER(b))) return;
    wrw(VEL_X(b), 0);
    c->d[0] = 3;
    new_state(0);
}

/* $17DE6: a random while, doubled */
static void long_delay(void)
{
    call(RANDOM_DELAY);
    Ent e = c->a[4];
    c->d[0] = set_b(c->d[0], rd8(TIMER2(e)));
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) * 2));
}

/* the head on the body, rocking about its home: when near home and the while is up, on to a
 * random state ($17E44), or another while */
static void a17DC4(Ent e)
{
    if (begins(e)) {
        call(PAIR_ALIGN_X);
        call(PAIR_ON_BODY);
        set_d0w((uint16_t)(rdw(Y(c->a[0])) - 0x2C));
        e = c->a[4];
        wrw(Y(e), (uint16_t)c->d[0]);
        call_d0(PAIR_ANIM, 6);
        long_delay();
        return;
    }
    if (pair_hit(PAIR_TOUCH)) return;
    call(PAIR_HOVER);
    e = c->a[4];
    c->d[7] = set_w(c->d[7], (uint16_t)(rdw(HOME_X(e)) - 2));
    if ((int16_t)rdw(VEL_X(e)) < 0) c->d[7] = set_w(c->d[7], c->d[7] + 4);
    c->d[0] = 16;
    uint16_t x = rdw(X(e)), d7 = (uint16_t)c->d[7];
    c->d[7] = set_w(c->d[7], (uint16_t)(d7 - x));
    if (d7 < x) {
        c->d[7] = set_w(c->d[7], (uint16_t)-(c->d[7] & 0xFFFF));
        c->d[0] = set_w(c->d[0], 0xFFF0);
    }
    wrw(ACCEL_X(e), (uint16_t)c->d[0]);
    call(ACCEL_X_);
    call(APPLY_VEL_X);
    call(PAIR_ALIGN_X);
    if ((c->d[7] & 0xFFFF) >= 0x10) return;
    e = c->a[4];
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    call(RANDOM);
    set_d0w(c->d[0] & 7);
    c->d[0] = set_b(c->d[0], rd8(0x17E44u + (c->d[0] & 7)));
    if (!(c->d[0] & 0xFF)) { long_delay(); return; }
    new_state(0);
}

/* the head on the body, carried left: $80 past home, back to state 3 */
static void a17E4C(Ent e)
{
    if (begins(e)) {
        call_d0(PAIR_ANIM, 6);
        wrw(ACCEL_X(c->a[4]), 0xFFF0);
        return;
    }
    if (pair_hit(PAIR_TOUCH)) return;
    call(PAIR_HOVER);
    call(ACCEL_X_);
    call(APPLY_VEL_X);
    call(PAIR_ALIGN_X);
    e = c->a[4];
    c->d[0] = set_w(0xFFFFFF80u, (uint16_t)(0xFF80 + rdw(HOME_X(e))));
    if ((uint16_t)c->d[0] < (uint16_t)rdw(X(e))) return;
    c->d[0] = 3;
    new_state(0);
}

/* the head off the body, shooting: up to five shots of kind $16 (by $18256, the offsets at
 * $17EE4), 30 frames apart; then state 3 */
static void a17E88(Ent e)
{
    if (begins(e)) {
        call_d0(PAIR_ANIM, 8);
        e = c->a[4];
        c->a[0] = partner_of(e);
        c->d[0] = set_w(0xFFFFFFF0u, (uint16_t)(0xFFF0 + rdw(X(c->a[0]))));
        wrw(X(e), (uint16_t)c->d[0]);
        c->d[0] = set_w(0xFFFFFFE0u, (uint16_t)(0xFFE0 + rdw(Y(c->a[0]))));
        wrw(Y(e), (uint16_t)c->d[0]);
        wrw(TIMER(e), 0x053C);
        call(STOP);
        return;
    }
    if (pair_hit(PAIR_TOUCH)) return;
    e = c->a[4];
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    wr8(TIMER2(e), 0x1E);
    set_d0w(0x216);
    c->a[0] = 0x17EE4u;
    call(SHOT_AT);
    if (!(c->ccr & CC_N)) {
        e = c->a[4];
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
    }
    c->d[0] = 3;
    new_state(0);
}

/* ---- the rider and its mount (batch 21) ----
 * Another pair through MAKER: the rider (a4) on its mount, placed by the mount's frame ($18332). */

/* the pair drops in from $100 above: the mount made (MAKE_PARTNER), the rider marked the room's
 * boss; $20 frames falling, then landed (the screen shaken, $183D4), the walls set when the
 * shake ends ($9C16, $9962), and state 3 */
static void a183E0(Ent e)
{
    if (begins(e)) {
        wrw(BOSS_MARK, (uint16_t)e);
        wrw(0xFFFF974Au, 0);
        wr8(NEXT_STATE(e), 0);
        call(FIND_FREE_ENTITY);
        call(MAKE_PARTNER);
        swap_a2_a4();
        call_d0(SET_ANIM_NOW, 0);
        c->a[1] = c->a[2];
        call(FACE_PLAYER);
        c->a[2] = c->a[1];
        swap_a2_a4();
        e = c->a[4];
        Ent m = c->a[2];
        c->d[0] = rd8(HALF_H(m));
        set_d0w(c->d[0] + 0x100);
        wrw(Y(m), (uint16_t)(rdw(Y(m)) - c->d[0]));
        wr8(ANIM_RATE(m), 0x0C);
        wr8(MOVE_FLAGS(m), 0x43);
        wrw(GRAVITY(m), 0x40);
        wr32(SPEED(m), 0x01000800u);
        wr8(BLINK(e), 0x13);
        wr8(ANIM_BUSY(e), 0);
        wr8(TYPE(m), 0xFF);
        call(MOUNT_PLACE);
        e = c->a[4];
        wrw(TIMER(e), 0x2000);
        set8(ALIVE(e), 0x42);
        set8(ALIVE(c->a[2]), 0x40);
        c->d[0] = 8;
        wr8(0xFFFF9F05u, 8);
        set8(0xFFFF9858u, 8);
        return;
    }
    c->d[0] = set_b(c->d[0], rd8(TIMER2(e)));
    if (!(c->d[0] & 0xFF)) {
        if (rd8(TIMER(e))) {
            wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
            c->a[4] = partner_of(e);
            call(ACCEL_Y);
            call(APPLY_VEL_Y);
            c->a[4] = partner_of(c->a[4]);
            call(MOUNT_PLACE);
            return;
        }
        call(MOUNT_FALL);
        call(MOUNT_GROUNDED);
        if (!(c->ccr & CC_Z)) return;
        e = c->a[4];
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) + 1));
        call(SHAKE_SCREEN);
        return;
    }
    c->d[0] = set_b(c->d[0], c->d[0] - 1);
    if (!(c->d[0] & 0xFF)) {
        if (rdw(0xFFFF9738u)) return;
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) + 1));
        wrw(0xFFFF9C16u, 0x69);
        wrw(0xFFFF9962u, 8);
        return;
    }
    wr8(0xFFFF9F05u, 0);
    c->d[0] = 3;
    new_state(0);
}

/* the pair beaten: the music stopped, the room frozen, the rider's tiles shown ($18572), the
 * mount breaking up ($182AA, every other frame) and $20 puffs 3 frames apart, over $80 frames;
 * then both gone (sound 19) */
static void a184C4(Ent e)
{
    if (begins(e)) {
        if (rd8(SOUND_MODE) != 0x12) call(QUEUE_SOUND_FF);
        call(FREEZE_ENEMIES);
        set_d0w(rdw(X(e)));
        c->d[1] = set_w(c->d[1], rdw(Y(e)));
        c->d[2] = 32; c->d[3] = 16;
        c->d[1] = set_w(c->d[1], c->d[1] + 16);
        c->a[0] = 0x18572u;
        call(SHOW_TILES);
        call_d0(MOUNT_ANIM, 10);
        wr32(TIMER(c->a[4]), 0x80142001u);
        c->d[2] = 11;
        call(BREAK_UP_START);
        return;
    }
    if (rd8(TIMER(e)) & 1) {
        c->d[1] = rd8(TIMER2(e));
        c->d[0] = set_b(c->d[0], (uint8_t)c->d[1]);
        c->d[1] = set_b(c->d[1], (uint8_t)(c->d[1] ^ 0x15));
        c->d[0] = set_b(c->d[0], (uint8_t)(c->d[0] + 0x25));
        set_d0w(c->d[0] & 0x3F);
        wr8(TIMER2(e), (uint8_t)c->d[0]);
        c->d[2] = 11;
        call(BREAK_UP);
    }
    e = c->a[4];
    if (rd8(VAR(e))) {
        wr8(SHOT(e), (uint8_t)(rd8(SHOT(e)) - 1));
        if (!rd8(SHOT(e))) {
            wr8(SHOT(e), 3);
            c->d[4] = 24; c->d[5] = 40;
            wrw(Y(e), (uint16_t)(rdw(Y(e)) + 24));
            call(PUFF_B);
            e = c->a[4];
            wrw(Y(e), (uint16_t)(rdw(Y(e)) - 0x18));
            wr8(VAR(e), (uint8_t)(rd8(VAR(e)) - 1));
        }
    }
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    wrw(Y(e), (uint16_t)(rdw(Y(e)) + 0x30));
    call(ENEMY_DEFEATED);
    wr8(BOSS_MARK, 0xFF);
    if (rd8(SOUND_MODE) != 0x12) call_d0(QUEUE_SOUND, 19);
    e = c->a[4];
    c->a[2] = partner_of(e);
    wr8(ALIVE(e), 0);
    wr8(ALIVE(c->a[2]), 0);
}

/* hurt: the mount knocked away from the sword at $200, slowing, for $20 frames; then state 3 */
static void a18584(Ent e)
{
    if (begins(e)) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return; }
        call_d0(MOUNT_ANIM, 10);
        e = c->a[4];
        set_d0w(0x200);
        if (!(rd8(TOUCH(e)) & 1)) set_d0w(-0x200);
        c->a[0] = partner_of(e);
        wrw(VEL_X(c->a[0]), (uint16_t)c->d[0]);
        wr8(TIMER(e), 0x20);
        return;
    }
    c->a[4] = partner_of(e);
    call_d0(FRICTION_X, 32);
    c->a[4] = partner_of(c->a[4]);
    call(MOUNT_FALL);
    call(PLAYER_TOUCH);
    call(ENEMY_TAKE_HIT);
    if (c->ccr & CC_N) {
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = 1; new_state(0); return; }
    }
    call(MOUNT_TOUCH);
    e = c->a[4];
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    c->d[0] = 3;
    new_state(0);
}

/* $186BC: the next state d0, the pending one cleared */
static void ride_on(void)
{
    wr8(NEXT_STATE(c->a[4]), 0);
    new_state(0);
}

/* $18620, $18630: the mount turned to the player (d0 0, d1 0), or not; TIMER d1, the pair's
 * animation d0, placed, a random while */
static void ride_settle(int face)
{
    if (face) {
        c->a[4] = partner_of(c->a[4]);
        call(FACE_PLAYER);
        c->a[4] = partner_of(c->a[4]);
        c->d[0] = 0; c->d[1] = 0;
    }
    wr8(TIMER(c->a[4]), (uint8_t)c->d[1]);
    call(MOUNT_ANIM);
    call(MOUNT_PLACE);
    call(RANDOM_DELAY);
}

/* riding about: stands a random while (the mount slowing), then on by the player's distance
 * ($186C6); in the air it lands with a shake; touched from behind, it runs (5); a pending state
 * from before decides its first frame */
static void a185EC(Ent e)
{
    if (begins(e)) {
        c->d[2] = set_b(c->d[2], rd8(NEXT_STATE(e)));
        wr8(NEXT_STATE(e), 0xFF);
        call(MOUNT_GROUNDED);
        if (!(c->ccr & CC_Z)) {
            c->d[0] = 4;
            if (!(c->d[2] & 0xFF)) { ride_settle(0); return; }
            c->d[0] = 5; ride_on(); return;
        }
        if (!(c->d[2] & 0xFF)) { ride_settle(1); return; }
        call(RANDOM);
        int odd = c->d[0] & 1;
        c->d[0] = set_b(c->d[0], (c->d[0] & 0xFF) >> 1);
        c->d[0] = odd ? 6 : 5;
        ride_on();
        return;
    }
    call(MOUNT_FALL);
    if (pair_hit(MOUNT_TOUCH)) return;
    e = c->a[4];
    c->a[0] = partner_of(e);
    c->d[0] = set_b(c->d[0], (uint8_t)((rd8(TOUCH(c->a[0])) | rd8(TOUCH(e))) & 4));
    if (c->d[0] & 0xFF) {
        wr8(NEXT_STATE(e), 0xFF);
        c->d[0] = 5;
        ride_on();
        return;
    }
    if (rd8(TIMER(e))) {
        call(MOUNT_GROUNDED);
        if (!(c->ccr & CC_Z)) return;
        call(SHAKE_SCREEN);
        ride_settle(1);
        return;
    }
    c->a[4] = partner_of(e);
    call_d0(FRICTION_X, 112);
    c->a[4] = partner_of(c->a[4]);
    e = c->a[4];
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    c->a[0] = partner_of(e);
    c->a[2] = (uint32_t)(int32_t)(int16_t)rdw(TARGET_W);
    call(RANDOM);
    set_d0w(c->d[0] & 7);
    c->a[1] = 0x186C6u;
    uint16_t d = (uint16_t)(rdw(X(c->a[2])) - rdw(X(c->a[0])));
    if ((int16_t)d < 0) d = (uint16_t)-d;
    c->d[1] = set_w(c->d[1], d);
    if (d >= 0x40 && d < 0xB0) c->a[1] += 8;
    c->d[0] = set_b(c->d[0], rd8(c->a[1] + (c->d[0] & 0xFFFF)));
    if (!(c->d[0] & 0xFF)) { ride_settle(1); return; }
    ride_on();
}

/* ---- the rider and its mount, on (batch 22) ---- */

/* the mount walks at the player a random while, until blocked; then state 3 */
static void a186D6(Ent e)
{
    if (begins(e)) {
        c->a[4] = partner_of(e);
        call(FACE_PLAYER_AND_WALK);
        c->a[4] = partner_of(c->a[4]);
        call_d0(MOUNT_ANIM, 2);
        call(RANDOM_TIMER);
        return;
    }
    call(MOUNT_FALL);
    if (pair_hit(MOUNT_TOUCH)) return;
    call(MOUNT_BLOCKED);
    if (c->ccr & CC_Z) {
        e = c->a[4];
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
    }
    c->d[0] = 3;
    new_state(0);
}

/* the mount leaps: away (a pending state, or near the player three times in four) at $200, or at
 * the player, 8 times its distance up to $300; landed or blocked, a shake and state 3 */
static void a18710(Ent e)
{
    if (begins(e)) {
        call_d0(MOUNT_ANIM, 4);
        c->a[0] = (uint32_t)(int32_t)(int16_t)rdw(TARGET_W);
        Ent m = c->a[4] = partner_of(c->a[4]);
        wrw(VEL_Y(m), 0xFC00);
        uint16_t d = (uint16_t)(rdw(X(c->a[0])) - rdw(X(m)));
        int ahead = (int16_t)d >= 0;
        c->d[1] = set_b(c->d[1], ahead ? 0xFF : 0);
        if (!ahead) d = (uint16_t)-d;
        set_d0w(d);
        c->d[2] = set_b(c->d[2], rd8(NEXT_STATE(m)));
        wr8(NEXT_STATE(m), 0);
        int run = (c->d[2] & 0xFF) != 0;
        if (!run && d < 0x40) {
            c->d[2] = set_w(c->d[2], d);
            call(RANDOM);
            uint32_t t = c->d[0]; c->d[0] = c->d[2]; c->d[2] = t;
            c->d[2] = set_w(c->d[2], c->d[2] & 3);
            run = (c->d[2] & 3) != 0;
        }
        ahead = (c->d[1] & 0xFF) != 0;              /* d1 as Random left it */
        if (run) {
            set_d0w(ahead ? 0xFE00 : 0x0200);
        } else {
            uint16_t v = (uint16_t)((c->d[0] & 0xFFFF) << 3);
            if (v > 0x300) v = 0x300;
            clr8(ATTR(m), 0x08);
            if (!ahead) { v = (uint16_t)-v; set8(ATTR(m), 8); }
            set_d0w(v);
        }
        wrw(VEL_X(m), (uint16_t)c->d[0]);
        c->a[4] = partner_of(m);
        return;
    }
    call(MOUNT_FALL);
    if (pair_hit(MOUNT_TOUCH)) return;
    call(MOUNT_BLOCKED);
    if (c->ccr & CC_Z) {
        call(MOUNT_GROUNDED);
        if (!(c->ccr & CC_Z)) return;
    }
    call(SHAKE_SCREEN);
    c->d[0] = 3;
    new_state(0);
}

/* the rider turns both to the player and aims at it, level or down ($1890C: the muzzle), the
 * angle held to its side ($109C, $FA8: the shot's velocity at $200); then up to 6 shots of kind
 * $21A, 4 frames apart, after $20 frames; then state 3 */
static void a187AC(Ent e)
{
    if (begins(e)) {
        c->a[2] = (uint32_t)(int32_t)(int16_t)rdw(TARGET_W);
        Ent m = c->a[0] = partner_of(e);
        c->d[0] = 0xFFFFFFF7u;
        clr8(ATTR(e), 0x08); clr8(ATTR(m), 0x08);
        set_d0w(rdw(X(c->a[2])));
        if ((uint16_t)rdw(X(c->a[2])) < (uint16_t)rdw(X(m))) {
            c->d[0] = 8;
            set8(ATTR(e), 8); set8(ATTR(m), 8);
        }
        c->d[1] = set_w(c->d[1], rdw(Y(c->a[2])));
        c->d[0] = 6; c->d[2] = 0;
        if ((uint16_t)rdw(Y(c->a[2])) < (uint16_t)rdw(Y(m))) { c->d[0] = 8; c->d[2] = 2; }
        wr8(VAR(e), (uint8_t)c->d[2]);
        c->a[4] = m;
        call(SET_ANIM_NOW);
        c->a[4] = partner_of(c->a[4]);
        call(MOUNT_PLACE);
        e = c->a[4];
        c->d[2] = rd8(VAR(e));
        c->a[1] = 0x1890Cu;
        c->d[0] = rd8(c->a[1] + c->d[2]);
        c->d[1] = rd8(c->a[1] + c->d[2] + 1);
        if (rd8(ATTR(e)) & 8) set_d0w(-(int)c->d[0]);
        set_d0w(c->d[0] + rdw(X(e)));
        c->d[1] = set_w(c->d[1], c->d[1] + rdw(Y(e)));
        c->a[2] = (uint32_t)(int32_t)(int16_t)rdw(TARGET_W);
        set_d0w(rdw(X(c->a[2])) - c->d[0]);
        c->d[1] = set_w(c->d[1], rdw(Y(c->a[2])) - c->d[1]);
        call(ANGLE_OF);
        e = c->a[4];
        int down = rd8(VAR(e)) != 0, left = rd8(ATTR(e)) & 8;
        int neg = (int16_t)c->d[3] < 0;
        uint16_t lim = 0;
        int clamp = 0;                              /* 1: at most lim; 2: at least lim */
        if (!down && !left) { if (neg) c->d[3] = 0; else { lim = 0x3000; clamp = 1; } }
        else if (!down)     { if (neg) c->d[3] = set_w(c->d[3], 0x8000); else { lim = 0x5000; clamp = 2; } }
        else if (!left)     { if (!neg) c->d[3] = 0; else { lim = 0xD000; clamp = 2; } }
        else                { if (!neg) c->d[3] = set_w(c->d[3], 0x8000); else { lim = 0xB000; clamp = 1; } }
        if (clamp) {
            set_d0w(lim);
            uint16_t a = (uint16_t)c->d[3];
            if (clamp == 1 ? a > lim : a <= lim) c->d[3] = set_w(c->d[3], lim);
        }
        c->d[4] = set_w(c->d[4], 0x200);
        call(VEL_OF_ANGLE);
        e = c->a[4];
        wrw(ACCEL_X(e), (uint16_t)c->d[0]);
        wrw(GRAVITY(e), (uint16_t)c->d[5]);
        wrw(TIMER(e), 0x0620);
        return;
    }
    if (pair_hit(MOUNT_TOUCH)) return;
    e = c->a[4];
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    if (!rd8(TIMER(e))) { c->d[0] = 3; new_state(0); return; }
    set_d0w(0x21A);
    c->d[1] = rd8(VAR(e));
    c->a[0] = 0x1890Cu + c->d[1];
    call(SHOT_AT);
    e = c->a[4];
    if (c->ccr & CC_N) {
        wr8(TIMER(e), 0);
        c->d[0] = 32;
    } else {
        Ent s = c->a[2];
        wrw(VEL_X(s), rdw(ACCEL_X(e)));
        wrw(VEL_Y(s), rdw(GRAVITY(e)));
        if (rd8(TIMER(e)) == 6) { call_d0(QUEUE_SOUND, 55); e = c->a[4]; s = c->a[2]; }
        c->a[0] = partner_of(e);
        c->d[0] = 8 & rd8(ATTR(c->a[0]));
        wr8(ATTR(s), (uint8_t)c->d[0]);
        c->d[0] = 4;
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (!rd8(TIMER(e))) c->d[0] = 32;
    }
    wr8(TIMER2(e), (uint8_t)c->d[0]);
}

/* a party of four set up at once: a mount (MAKE_PARTNER) under this rider, and two followers
 * (kinds +2, +4, $41AA) chained by GOAL, each with its own pending state and delay; the rider
 * placed on the mount ($18A3E) and on to state 3 */
static void a18ADE(Ent e)
{
    call(FIND_FREE_ENTITY);
    call(MAKE_PARTNER);
    swap_a2_a4();
    Ent m = c->a[4];
    call_d0(SET_ANIM_NOW, 0);
    c->d[0] = rd8(HALF_H(m));
    wrw(Y(m), (uint16_t)(rdw(Y(m)) - c->d[0]));
    call(FACE_PLAYER);
    m = c->a[4];
    wr8(TYPE(m), 0xFF);
    wrw(SPEED_Y(m), 0x800);
    wrw(GRAVITY(m), 0x40);
    c->a[0] = (uint32_t)(int32_t)(int16_t)m;
    e = c->a[4] = partner_of(m);
    wr8(NEXT_STATE(e), 0x0E);
    wrw(DELAY(e), 0x1007);
    call(FIND_FREE_ENTITY);
    call(COPY_TO_A2);
    e = c->a[4];
    Ent f = c->a[2];
    c->d[0] = set_b(0xFFFFFFFFu, (uint8_t)(rd8(TYPE(e) + 1) + 2));
    wrw(TYPE(f), (uint16_t)c->d[0]);
    wr8(NEXT_STATE(f), 0x14);
    wrw(DELAY(f), 0x1604);
    wrw(GOAL(e), (uint16_t)f);
    wrw(MAKER(f), (uint16_t)c->a[0]);
    c->a[1] = (uint32_t)(int32_t)(int16_t)f;
    call(FIND_FREE_ENTITY);
    call(COPY_TO_A2);
    e = c->a[4];
    Ent g = c->a[2];
    c->d[0] = set_b(0xFFFFFFFFu, (uint8_t)(rd8(TYPE(e) + 1) + 4));
    wrw(TYPE(g), (uint16_t)c->d[0]);
    wr8(NEXT_STATE(g), 0x20);
    wrw(GOAL(c->a[1]), (uint16_t)g);
    wrw(MAKER(g), (uint16_t)c->a[0]);
    wr8(GOAL(g), 0xFF);
    wr8(ANIM_RATE(c->a[0]), 0x0E);
    c->d[0] = 16;
    wr8(ANIM_RATE(c->a[1]), 16);
    wr8(ANIM_RATE(g), 16);
    wr8(ANIM_RATE(e), 0x0C);
    wr8(Y_OFFSET(e), 0xE3);
    call(RIDE_PLACE);
    call_d0(SET_ANIM, 14);
    e = c->a[4];
    c->a[2] = partner_of(e);
    wr8(MOVE_FLAGS(c->a[2]), 3);
    wr32(HIT_B(c->a[2]), 0x18BB8u);
    c->d[0] = 64;
    set8(ALIVE(e), 64); set8(ALIVE(c->a[2]), 64);
    wr8(STEP(e), 3);
}

/* ---- the third pair: a rider and its beast (batch 23) ---- */

/* $18D74: the beast's looks for a step of the rider's fall (d1: 0 its own, 1 or 2 the rider's
 * $1620 set, animation 6 or 8) */
static void beast_looks(void)
{
    c->a[2] = partner_of(c->a[4]);
    swap_a2_a4();
    Ent b = c->a[4], r = c->a[2];
    if (!(c->d[1] & 0xFF)) {
        clr8(BLINK(r), 0x10);
        clr8(BLINK(b), 0x08);
        wrw(ANIM_SET(b), rdw(ANIM_SET(r)));
        set_d0w(2);
    } else {
        set8(BLINK(r), 0x10);
        set8(BLINK(b), 0x08);
        wrw(ANIM_SET(b), 0x1620);
        c->d[0] = 6;
        c->d[1] = set_b(c->d[1], c->d[1] - 1);
        if (c->d[1] & 0xFF) c->d[0] = 8;
    }
    call(SET_ANIM);
    swap_a2_a4();
}

/* $18D74's table ($18C94): five bytes a step, from WORD_31FE */
static uint8_t fall_byte(Ent e, uint32_t at) { return rd8(at + (uint32_t)(int32_t)(int16_t)rdw(WORD_31FE(e))); }

/* the pair beaten, in phases: $20 puffs about the rider; then the rider thrown off the beast step
 * by step ($18C94); then the rider alone, a game flag set (17), the room's walls, a jump and gone */
static void a18BBE(Ent e)
{
    if (begins(e)) {
        call(QUEUE_SOUND_FF);
        call(FREEZE_ENEMIES);
        call_d0(START_EFFECT, 16);
        wr8(Y_OFFSET(c->a[4]), 0xE4);
        call(RIDE_PLACE);
        call(BLINK_BOTH);
        e = c->a[4];
        wr8(PHASE(e), 0);
        wrw(TIMER(e), 0x2001);
        call_d0(RIDE_ANIM, 2);
        return;
    }
    c->d[0] = rd8(PHASE(e)) * 2;
    c->a[0] = 0x18C06u + (uint32_t)(int32_t)(int16_t)rdw(0x18C06u + c->d[0]);
    switch (c->d[0] >> 1) {
    case 0:                                         /* puffs */
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
        if (!rd8(TIMER(e))) {
            c->a[2] = partner_of(e);
            c->d[0] = 0xFFFFFFBFu;
            clr8(BLINK(e), 0x40); clr8(BLINK(c->a[2]), 0x40);
            call_d0(QUEUE_SOUND, 69);
            e = c->a[4];
            wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
            wrw(TIMER(e), 0x0101);
            wrw(WORD_31FE(e), 0xFFFB);
            wr8(VAR(e), 1);
            wr8(0xFFFF9F05u, 8);
            return;
        }
        c->d[4] = 32; c->d[5] = 48;
        wrw(Y(e), (uint16_t)(rdw(Y(e)) + 0x18));
        call(PUFF_B);
        e = c->a[4];
        wrw(Y(e), (uint16_t)(rdw(Y(e)) - 0x18));
        c->d[0] = 4;
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (!rd8(TIMER(e))) c->d[0] = 16;
        wr8(TIMER2(e), (uint8_t)c->d[0]);
        return;
    case 1: {                                       /* thrown off, a step at a time */
        wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
        if (rd8(TIMER2(e))) return;
        uint8_t v = rd8(VAR(e));
        wr8(VAR(e), v ^ 1);
        if (!(v & 1)) {
            set_d0w(rdw(WORD_31FE(e)));
            wr8(TIMER2(e), fall_byte(e, 0x18C97u));
            c->d[1] = set_b(c->d[1], fall_byte(e, 0x18C98u));
            beast_looks();
            return;
        }
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (!rd8(TIMER(e))) {
            set_d0w(rdw(WORD_31FE(e)) + 5);
            wrw(WORD_31FE(e), (uint16_t)c->d[0]);
            wr8(TIMER(e), fall_byte(e, 0x18C94u));
            if (!rd8(TIMER(e))) {                   /* off: the rider alone */
                wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
                c->a[2] = partner_of(e);
                wrw(X(e), rdw(X(c->a[2])));
                wrw(Y(e), rdw(Y(c->a[2])));
                wrw(HALF_W(e), rdw(HALF_W(c->a[2])));
                wr8(ALIVE(c->a[2]), 0);
                call(FACE_PLAYER);
                e = c->a[4];
                clr8(BLINK(e), 0x10);
                wr8(0xFFFF9F05u, 6);
                c->a[0] = rd8(ATTR(e)) & 8 ? 0x18DB8u : 0x18DC0u;
                wr32(0xFFFF9F22u, c->a[0]);
                call_d0(GAME_FLAG_BIT, 17);
                uint32_t at = c->a[0] + (uint32_t)(int32_t)(int16_t)c->d[1];
                wr8(at, (uint8_t)(rd8(at) | 1 << (c->d[0] & 7)));
                e = c->a[4];
                wrw(ANIM_SET(e), 0x1620);
                set8(BLINK(e), 8);
                call_d0(SET_ANIM, 0);
                wr8(TIMER(c->a[4]), 0x1E);
                call_d0(QUEUE_SOUND, 20);
                return;
            }
        }
        set_d0w(rdw(WORD_31FE(e)));
        wr8(TIMER2(e), fall_byte(e, 0x18C95u));
        c->d[1] = set_b(c->d[1], fall_byte(e, 0x18C96u));
        beast_looks();
        return;
    }
    case 2:                                         /* the walls */
        wr8(0xFFFF9F05u, 8);
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
        c->d[0] = 8;
        wr8(TIMER(e), 8);
        wrw(0xFFFF9962u, 8);
        wrw(0xFFFF9C16u, 0x6F);
        return;
    case 3:
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
        wr8(TIMER(e), 3);
        call_d0(SET_ANIM, 2);
        return;
    case 4:                                         /* the jump */
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
        wrw(VEL_Y(e), 0xE000);
        call_d0(QUEUE_SOUND, 60);
        call_d0(SET_ANIM, 4);
        return;
    default:                                        /* 5: up and off the screen, gone */
        call(APPLY_VEL_Y);
        e = c->a[4];
        if (rd8(ALIVE(e)) & 1) return;
        wr8(ALIVE(e), 0);
        wr8(0xFFFF9F05u, 0);
        wrw(Y(e), (uint16_t)(rdw(Y(e)) + 0x80));
        call(ENEMY_DEFEATED);
        return;
    }
}

/* $18E4C: the rider beaten; a follower (GOAL) takes its place on the beast (step 2, phase 1,
 * its kind), or, with none, state 1 */
static void rider_beaten(Ent e)
{
    set_d0w(rdw(GOAL(e)));
    if ((int16_t)c->d[0] < 0) { c->d[0] = 1; new_state(0); return; }
    c->a[0] = (uint32_t)(int32_t)(int16_t)c->d[0];
    Ent f = c->a[0];
    wr8(STEP(f), 0x82);
    wr8(PHASE(f), 1);
    c->d[0] = rd8(TYPE(f) + 1);
    wrw(TYPE(f), rdw(TYPE(e)));
    c->a[2] = partner_of(e);
    wrw(MAKER(c->a[2]), (uint16_t)f);
    wr8(TIMER(f), 0x3C);
    wr8(ANIM_RATE(e), (uint8_t)(rd8(ANIM_RATE(e)) - 2));
    wrw(TYPE(e), (uint16_t)c->d[0]);
    c->d[0] = 0;
    new_state(0);
}

/* $18F28: on the beast, and its touch */
static void ride_and_touch(void) { call(RIDE_PLACE); call(RIDE_TOUCH); }

/* hurt: the beast knocked from the sword at $400, slowing; then on (3 or 6). Phases 1-3 are a
 * follower mounting: it becomes its kind ($1274A), rises to $E0 and settles at $E4 */
static void a18E3A(Ent e)
{
    if (begins(e)) {
        wr8(PHASE(e), 0);
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { rider_beaten(c->a[4]); return; }
        e = c->a[4];
        c->a[2] = partner_of(e);
        set_d0w(0x400);
        if (!(rd8(TOUCH(e)) & 1)) set_d0w(-0x400);
        wrw(VEL_X(c->a[2]), (uint16_t)c->d[0]);
        call(BLINK_BOTH);
        call_d0(RIDE_ANIM, 4);
        return;
    }
    c->d[0] = set_b(c->d[0], rd8(PHASE(e)));
    if (!(c->d[0] & 0xFF)) {
        call(PLAYER_TOUCH);
        call(ENEMY_TAKE_HIT);
        if (c->ccr & CC_N) {
            call(DAMAGE_ENEMY);
            if (c->ccr & CC_Z) { rider_beaten(c->a[4]); return; }
        }
        c->a[4] = partner_of(c->a[4]);
        call_d0(FRICTION_X, 64);
        c->a[4] = partner_of(c->a[4]);
        call(RIDE_FALL);
        call(RIDE_TOUCH);
        e = c->a[4];
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
        c->d[0] = 0xFFFFFFBFu;
        clr8(BLINK(e), 0x40);
        c->a[0] = partner_of(e);
        clr8(BLINK(c->a[0]), 0x40);
        call(RANDOM);
        c->d[0] = c->d[0] & 1 ? 3 : 6;
        new_state(0);
        return;
    }
    c->d[0] = set_b(c->d[0], c->d[0] - 1);
    if (!(c->d[0] & 0xFF)) {                        /* the follower becomes its kind */
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) { call(RIDE_TOUCH); return; }
        c->d[1] = set_w(c->d[1], rdw(TYPE(e)));
        call(BECOME_KIND);
        e = c->a[4];
        wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
        wr8(Y_OFFSET(e), 0);
        c->d[0] = set_b(c->d[0], rd8(NEXT_STATE(e)));
        call(SET_ANIM);
        set8(ALIVE(c->a[4]), 0x40);
        ride_and_touch();
        return;
    }
    c->d[0] = set_b(c->d[0], c->d[0] - 1);
    e = c->a[4];
    if (!(c->d[0] & 0xFF)) {                        /* rising */
        wr8(Y_OFFSET(e), (uint8_t)(rd8(Y_OFFSET(e)) - 1));
        ride_and_touch();
        e = c->a[4];
        if ((int8_t)rd8(Y_OFFSET(e)) > (int8_t)0xE0) return;
        wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
        wr8(ANIM_RATE(e), 0x0C);
        return;
    }
    wr8(Y_OFFSET(e), (uint8_t)(rd8(Y_OFFSET(e)) + 1));  /* settling */
    ride_and_touch();
    if ((int8_t)rd8(Y_OFFSET(c->a[4])) < (int8_t)0xE4) return;
    c->d[0] = 3;
    new_state(0);
}

/* $18F70: the beast turned to the player, the rider on it, a random while */
static void beast_settle(void)
{
    c->a[4] = partner_of(c->a[4]);
    call(FACE_PLAYER);
    c->a[4] = partner_of(c->a[4]);
    call(RIDE_PLACE);
    call(RANDOM_DELAY);
}

/* the beast stands a random while, slowing, then on ($18FBA), or another while */
static void a18F62(Ent e)
{
    if (begins(e)) {
        call_d0(RIDE_ANIM, 6);
        beast_settle();
        return;
    }
    call(RIDE_FALL);
    if (pair_hit(RIDE_TOUCH)) return;
    call(RIDE_GROUNDED);
    if (!(c->ccr & CC_Z)) return;
    c->a[4] = partner_of(c->a[4]);
    call_d0(FRICTION_X, 64);
    c->a[4] = partner_of(c->a[4]);
    e = c->a[4];
    wr8(TIMER2(e), (uint8_t)(rd8(TIMER2(e)) - 1));
    if (rd8(TIMER2(e))) return;
    call(RANDOM);
    set_d0w(c->d[0] & 7);
    c->d[0] = set_b(c->d[0], rd8(0x18FBAu + (c->d[0] & 7)));
    if (!(c->d[0] & 0xFF)) { beast_settle(); return; }
    new_state(0);
}

/* the beast walks at the player at $100, turned back past x $1140 (right) or $10C0 (left), for
 * a random while or until blocked; then state 3 */
static void a18FC2(Ent e)
{
    if (begins(e)) {
        Ent b = c->a[4] = partner_of(e);
        wrw(SPEED_X(b), 0x100);
        call(FACE_PLAYER_AND_WALK);
        b = c->a[2] = (uint32_t)(int32_t)(int16_t)c->a[4];
        c->a[4] = partner_of(c->a[4]);
        uint16_t x = (uint16_t)rdw(X(b));
        if (rd8(ATTR(b)) & 8 ? x < 0x10C0 : x > 0x1140) wrw(VEL_X(b), (uint16_t)-rdw(VEL_X(b)));
        call_d0(RIDE_ANIM, 8);
        call(RIDE_PLACE);
        call(RANDOM_TIMER_32);
        return;
    }
    call(RIDE_FALL);
    if (pair_hit(RIDE_TOUCH)) return;
    call(RIDE_BLOCKED);
    if (c->ccr & CC_Z) {
        e = c->a[4];
        wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
        if (rd8(TIMER(e))) return;
    }
    c->d[0] = 3;
    new_state(0);
}

/* ---- the third pair's throw, and its followers (batch 24) ---- */

/* the rider throws a shot of kind $220 ($190C6) and holds it (WORD_31FE) while the beast's
 * animation runs; the beast's marks (its ATTR bits 0, 1) step the shot on; hit, the shot is gone
 * and state 2 */
static void a19026(Ent e)
{
    if (begins(e)) {
        c->a[4] = partner_of(e);
        call(FACE_PLAYER);
        clr8(ATTR(c->a[4]), 0x03);
        c->a[4] = partner_of(c->a[4]);
        call(RIDE_PLACE);
        set_d0w(0x220);
        c->a[0] = 0x190C6u;
        call(SHOT_AT);
        if (c->ccr & CC_N) { c->d[0] = 3; new_state(0); return; }
        e = c->a[4];
        c->d[0] = 8 & rd8(ATTR(e));
        wr8(ATTR(c->a[2]), (uint8_t)c->d[0]);
        wrw(WORD_31FE(e), (uint16_t)c->a[2]);
        call_d0(RIDE_ANIM, 12);
        return;
    }
    call(PLAYER_CONTACT_ALL);
    call(ENEMY_TAKE_HIT);
    e = c->a[4];
    if (c->ccr & CC_N) {
        set_d0w(rdw(WORD_31FE(e)));
        if ((int16_t)c->d[0] >= 0) {
            c->a[0] = (uint32_t)(int32_t)(int16_t)c->d[0];
            wr8(ALIVE(c->a[0]), 0);
        }
        c->d[0] = 2;
        new_state(0);
        return;
    }
    call(RIDE_TOUCH);
    e = c->a[4];
    Ent b = c->a[2] = partner_of(e);
    uint8_t marks = rd8(ATTR(b));
    if (marks & 1) {
        wr8(ATTR(b), marks & 0xFE);
        c->a[0] = (uint32_t)(int32_t)rdw(WORD_31FE(e));
        wr8(STEP(c->a[0]), (uint8_t)(rd8(STEP(c->a[0])) + 1));
    }
    marks = rd8(ATTR(b));
    if (marks & 2) {
        wr8(ATTR(b), marks & 0xFD);
        c->a[0] = (uint32_t)(int32_t)rdw(WORD_31FE(e));
        wr8(STEP(c->a[0]), (uint8_t)(rd8(STEP(c->a[0])) + 1));
        wr8(WORD_31FE(e), 0xFF);
    }
    c->a[0] = partner_of(e);
    if (rd8(ANIM_BUSY(c->a[0]))) return;
    c->d[0] = 3;
    new_state(0);
}

/* a follower readied: speeds $180, $400; flags $13; no health yet; state 3 */
static void a190C8(Ent e)
{
    wr32(SPEED(e), 0x01800400u);
    wr8(MOVE_FLAGS(e), 0x13);
    wr8(PHASE(e), 0);
    wrw(HEALTH(e), 0);
    c->d[0] = 3;
    new_state(0);
}

/* a follower hidden as a puff (kind $218, the $120A set) */
static void a190E4(Ent e)
{
    clr8(ALIVE(e), 0x20);
    wrw(TYPE(e), 0x218);
    set8(BLINK(e), 8);
    wrw(ANIM_SET(e), 0x120A);
    call_d0(SET_ANIM, 0);
}

/* a follower hurt: knocked from the sword at $400, blinking and frozen; then its next state
 * (DELAY's second byte; its animation the first); beaten, state 3 */
static void a19102(Ent e)
{
    if (begins(e)) {
        clr8(ATTR(e), 0x10);
        call(DAMAGE_ENEMY);
        if (c->ccr & CC_Z) { c->d[0] = 3; new_state(0); return; }
        e = c->a[4];
        set_d0w(0x400);
        if (!(rd8(TOUCH(e)) & 1)) set_d0w(-0x400);
        wrw(VEL_X(e), (uint16_t)c->d[0]);
        wrw(GRAVITY(e), 0x40);
        c->d[0] = set_b(c->d[0], rd8(DELAY(e)));
        call(SET_ANIM);
        call(START_BLINK_FROZEN);
        return;
    }
    call_d0(FRICTION_X, 32);
    call(ENEMY_MOVE);
    call(PLAYER_TOUCH);
    e = c->a[4];
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    clr8(ALIVE(e), 0x20);
    clr8(BLINK(e), 0x40);
    c->d[0] = set_b(c->d[0], rd8(DELAY(e) + 1));
    new_state(0);
}

/* a follower thrown off: up at $300 and away from the sword at $180, upside down while falling;
 * landed, $78 frames lying, $1E more, then its health back and state 8 */
static void a1916A(Ent e)
{
    if (begins(e)) {
        clr8(ATTR(e), 0x10);
        wrw(VEL_Y(e), 0xFD00);
        wrw(GRAVITY(e), 0x40);
        wrw(SPEED_Y(e), 0x800);
        set_d0w(0x180);
        if (!(rd8(TOUCH(e)) & 1)) set_d0w(-0x180);
        wrw(VEL_X(e), (uint16_t)c->d[0]);
        wr8(PHASE(e), 0);
        c->d[0] = set_b(c->d[0], rd8(NEXT_STATE(e)));
        call(SET_ANIM);
        return;
    }
    c->d[0] = set_b(c->d[0], rd8(PHASE(e)));
    if (!(c->d[0] & 0xFF)) {
        call(FALL_AND_MOVE);
        e = c->a[4];
        if ((int16_t)rdw(VEL_Y(e)) >= 0) set8(ATTR(e), 0x10);
        call(CHECK_GROUNDED);
        if (!(c->ccr & CC_Z)) return;
        e = c->a[4];
        wr8(TIMER(e), 0x78);
        wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
        return;
    }
    c->d[0] = set_b(c->d[0], c->d[0] - 1);
    wr8(TIMER(e), (uint8_t)(rd8(TIMER(e)) - 1));
    if (rd8(TIMER(e))) return;
    if (!(c->d[0] & 0xFF)) {
        wr8(PHASE(e), (uint8_t)(rd8(PHASE(e)) + 1));
        wr8(TIMER(e), 0x1E);
        clr8(ATTR(e), 0x10);
        return;
    }
    call(ENEMY_MAX_HEALTH);
    wrw(HEALTH(c->a[4]), (uint16_t)c->d[0]);
    c->d[0] = 8;
    new_state(0);
}

/* the actions, registered by their addresses (registry.c). call_68k leaves its mark in ret: it
 * is cleared unless the action went on elsewhere */
#define ACTION(addr) void Act##addr(Cpu *cpu) { c = cpu; went_on = 0; a##addr((Ent)c->a[4]); if (!went_on) c->ret = 0; }
ACTION(140F2) ACTION(140FC) ACTION(1410E) ACTION(14120) ACTION(1416E) ACTION(141A4)
ACTION(1429E) ACTION(142D6) ACTION(142E4) ACTION(142F6) ACTION(14308)
ACTION(1436C) ACTION(14394) ACTION(143BC) ACTION(143F2) ACTION(14420) ACTION(14456)
ACTION(14518) ACTION(1454C) ACTION(145C4) ACTION(1463C) ACTION(14682) ACTION(146FC) ACTION(1476C)
ACTION(14822) ACTION(14888) ACTION(1492A) ACTION(14974) ACTION(149D0) ACTION(14A82) ACTION(14AEA)
ACTION(14AFA) ACTION(14B7E) ACTION(14C1A)
ACTION(14C50) ACTION(14CA2) ACTION(14CF0) ACTION(14D74) ACTION(14DC8) ACTION(14E1A) ACTION(14E6C)
ACTION(14EC8) ACTION(14EF4) ACTION(14F28) ACTION(14F54) ACTION(14F80)
ACTION(14FD2) ACTION(14FFE) ACTION(15010) ACTION(15066) ACTION(150D8) ACTION(15126) ACTION(151A0)
ACTION(151D6) ACTION(152A0)
ACTION(15352) ACTION(153BE) ACTION(153F0) ACTION(15464) ACTION(154B2) ACTION(15504) ACTION(15576)
ACTION(155E2) ACTION(15630) ACTION(15704) ACTION(1577A) ACTION(157A6) ACTION(157A8) ACTION(157E4)
ACTION(15830) ACTION(1589A) ACTION(158E6) ACTION(15994)
ACTION(15A86) ACTION(15C9E) ACTION(15DD2) ACTION(15E54) ACTION(15EE6) ACTION(15FB0)
ACTION(16D70) ACTION(16ED0) ACTION(16F50) ACTION(16FF0)
ACTION(17014) ACTION(1729A) ACTION(17310) ACTION(17362)
ACTION(173AA) ACTION(1743C) ACTION(174E6) ACTION(17548) ACTION(17588) ACTION(1759A)
ACTION(1771C) ACTION(1774E) ACTION(177D6) ACTION(17822) ACTION(17860) ACTION(178D0)
ACTION(17A44) ACTION(17B8A) ACTION(17D22) ACTION(17DC4) ACTION(17E4C) ACTION(17E88)
ACTION(183E0) ACTION(184C4) ACTION(18584) ACTION(185EC)
ACTION(186D6) ACTION(18710) ACTION(187AC) ACTION(18ADE)
ACTION(18BBE) ACTION(18E3A) ACTION(18F62) ACTION(18FC2)
ACTION(19026) ACTION(190C8) ACTION(190E4) ACTION(19102) ACTION(1916A)
