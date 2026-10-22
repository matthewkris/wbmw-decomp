# Enemy health

The native build can show the damage each hit does and how much health an enemy has left
(menu: **Enemy health**; `--no-enemy-health` or `enemy_health=0` in `wbmw.ini` turns it off). It
is on by default. It is drawn by the host over the finished picture (`hits_frame` in `native/wbmw.c`):
the game's code and RAM are untouched.

## What it shows

* **Damage numbers:** when an enemy loses health, the amount rises from above it for a second
  and fades. A killing blow's number is gold, the others white.
* **A health bar** over the enemy for two seconds after each hit: its health against its
  maximum, green above two thirds, yellow above one third, red below.

## How

* Each frame the host compares every enemy's health (`-$2600`, a word) with the frame before;
  a drop is damage. Enemies are the entities of category 0 (`-$2BFE`, the test the game's own
  `$4948` uses before it shows an enemy's health), not Wonder Boy (`$9EEE`).
* The maximum is the enemy's kind's (`-$2BFD`) in the table `MaxHealth` ($4928) reads: at
  `$1BA6C` plus its first word, 3 bytes a kind, the first being the health in 32s. Every enemy of
  a kind starts with the same health; the mushrooms of the first forest have 96.
* A hit's damage is the sword's, scaled by the enemy's level (`-$26FE`, the table at `$4972`, over
  64), doubled by a falling strike, plus any magic (`DamageEnemy`, $4986), so the numbers vary
  with the weapon and the enemy. The game also keeps the last enemy's maximum and health at
  `$9628`/`$962A`.
* The numbers are placed where the hit landed, in the world's coordinates, so they stay there as
  the view scrolls (an enemy knocked back moves away from its number); the bar follows the enemy.
* They go under the window (the HUD's lines, and a "Found ..." box under it: the lines register 18
  gives the window from the top) and under an open message's box (`$8CA4` bit 7: the box at
  `$8D92`-`$8D98`, with the pointer's row above it).

* Loading a state clears them (`overlays_reset`, with the gold numbers and the talk zoom): they
  belong to the run before it, and at first its bars stayed up over the loaded scene.

## Tried

The mushrooms of the forest path: a hit of 32 (white, the bar red at 32 of 96), then the killing
blow (gold), with widescreen on. The Myconid (a boss: kind $5C, 256) counts as an enemy too.
