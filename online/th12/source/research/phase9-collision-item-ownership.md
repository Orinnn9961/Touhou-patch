# Phase 9: collision and item ownership

## Implemented boundaries

- Enemy and Boss damage calls at `0x413D8B` and `0x41AC77` scan P1 and P2
  bullet/laser pools independently, then add the damage.
- The ordinary-enemy caller reads the global P1 state after `0x413D8B` and
  divides the combined return value by five while P1 is in state 0 or 2. The
  co-op hook pre-scales only an active P2 contribution at this call site, so
  P1 retains the original death/entry reduction without reducing P2. The Boss
  call site has no matching post-call division and receives no compensation.
- All confirmed lethal player collision calls are dispatched once for each player.
  A hit is applied by the original routine to the selected `PlayerInf`; P2-only graze
  is recorded but is not returned to the original global graze settlement path.
- P2 never enters the original full-player recovery update at `0x436BA0`. A private
  four-step state machine mirrors the safe visual states: eight frames in state 4,
  30 hidden frames in state 2, a 60-frame state-0 entry from Y=480 to Y=400, and
  180 active frames of invincibility. The P2 render registration is disabled on
  the first recovery tick; Option VMs are removed at the same boundary. Rendering
  is restored only after the entry position is set to X=0/Y=480. During all 60
  entry frames, the main VM is explicitly synchronized with the original render
  transform from `0x437600` (`X + 224`, `Y + 16`) and consumes a 240-frame invincibility timer,
  leaving 180 frames after entry. Options are rebuilt after entry. The player VM
  uses the original `0x10000` color flag cadence from the first visible entry frame.
  A confirmed hit stops the P2 Option scheduler immediately. The eight Option VM
  pairs are deleted and the native `+0xC414/+0xC418` update state is cleared, then
  the cleanup is reasserted after bullet update during recovery so a late VM cannot
  remain on screen.
  No lives, fragments, bombs, Power, point value, game-manager state, or P1 UI are
  written by the recovery state machine itself. The original death Power settlement
  is restored as an explicit shared-resource action on hidden frame 3: `0x439440`
  lowers the shared Power value and seven type-1 P items are spawned through
  `0x4273F0`. Lives and Bombs remain untouched.
- At the end of the hidden wait, `0x4390F0` creates the original 30-frame,
  parameter-150 death area in P2's isolated player attack pool before the entry
  position is changed. It supplies the enemy/Boss damage behavior without invoking
  the full player recovery update. The P2 `0x439B10` bullet update advances the
  area exactly once per frame, matching the original tail at `0x436F90`; no
  additional manual damage-area tick is applied. Its radius grows by 16 per frame
  from 32 and its scaled lifetime is retired after 30 frames.
- Enemy bullets are cleared through the original `0x40D230` 2000-slot manager
  traversal, followed by the original `0x428750` laser/callback notification.
  These calls run once at recovery frame 40 (eight death-confirmation frames,
  30 hidden-wait frames, then two entry frames). This remains separate from the
  death damage area and does not invoke life settlement.
- The item manager selects an owner at the loop-state boundary `0x425C47`, before
  every movement, point-of-collection, contact, and attraction branch. The selected
  player's input mask is installed for the whole item slot and restored at
  `0x426F53`. Items retain that owner while in attraction states 3 and 4, so
  `0x4377A0` calculates the return direction against the correct player.
- Item ownership priority is direct contact, local attraction bounds, then the
  original Y < 128 point-of-collection rule. Equal priorities use the smaller
  squared distance and P1 wins an exact tie. The original item settlement still
  runs once; Phase 10 will replace its global resource writes with the final
  shared-resource rules.
- Outside the item-manager scope, the player-angle helper at `0x4377A0` selects the
  nearest active player. Enemy aimed-shot users of this common helper can therefore
  target P2; exact distance ties remain P1-first.

## Determinism rules

Collision dispatch order is P1 then P2. Item ownership uses player state, current
positions, item state/position, current focus input, the fixed Y < 128 threshold,
and a P1 tie break. `phase7.movement_only_test=1` disables all P2 collision, damage
and item additions so the Phase 7 movement trace remains stable.

## Known scope

This phase does not implement Bomb collision isolation or shared-resource conflict
resolution. A P2-only graze follows the original visible graze path and therefore
uses its provisional global reward until shared settlement is implemented. Item settlement uses existing globals;
only the owner selection and attribution event are established here.
