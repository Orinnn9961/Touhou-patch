# Phase 10: six Bomb variants isolated per player

## Scope

This phase gives P2 an independent original-game BombManager. P1 and P2 can
use different Bomb variants sequentially, and the runtime also supports both
BombManagers being active in the same frame. Replay persistence and final
same-frame shared-resource arbitration remain out of scope.

The Bomb inventory is still the original shared global value. With at least
two Bombs available, P2 and P1 can both consume one and start in the same
frame. The current local callback order is P2 before P1; the later shared
resource phase will make that ordering an explicit lockstep settlement rule.

## Original interfaces

| Purpose | Address |
|---|---:|
| BombManager global | `0x004B43C4` |
| create and register | `0x00406B20` |
| destroy internals | `0x00406930` |
| start dispatch | `0x00406BF0` |
| update dispatch | `0x00406CE0` |
| enemy/Boss Bomb damage dispatch | `0x00406DE0` |
| consume shared Bomb stock | `0x00422F20` |

The object is `0x524` bytes. Its active state is at `+0x3C`; update and render
registration handles are at `+0x08` and `+0x0C`.

## Runtime ownership

`runtime_bomb.cpp` creates a second object through the original factory. Its
automatic callbacks are disabled because scheduler callbacks do not carry a
player identity. P2 manually updates this object once per P2 tick while all
three required bindings are active:

1. `ScopedPlayerContext(Player2)` selects P2 character, shot type, input, and
   `PlayerInf*`.
2. ANM slot 7 selects the P2 archive.
3. `0x004B43C4` selects the P2 BombManager.

The bindings are restored before P1 updates. P2 Bomb destruction runs before
P2 PlayerInf and the detached P2 ANM archive are released.

## Damage isolation

The original player damage scan at `0x00439ED0` scans player shots and then
calls `0x00406DE0` for Bomb damage. The existing two-player hook now performs:

```text
P1 context + P1 BombManager -> P1 shots and P1 Bomb damage
P2 context + P2 BombManager -> P2 shots and P2 Bomb damage
```

This also prevents an active P1 Bomb from being counted a second time during
the P2 shot scan. Bomb start functions write invincibility through the active
`PlayerInf*`, so P1 and P2 invincibility timers remain independent.

## Manual acceptance order

First verify sequential use:

1. Set `phase4.player2_airframe` to a different airframe from P1 and start a
   stage with at least two Bombs.
2. Press P1 `X`; after its Bomb ends, press P2 `K`.
3. Restart and reverse the order: P2 `K`, wait for completion, then P1 `X`.
4. Confirm each animation matches its own airframe, each damages enemies and
   the Boss, and only the releasing player receives Bomb invincibility.
5. Repeat with representative pairs until all six variants have been used at
   least once as P1 or P2.

After sequential use is stable, verify overlap with at least two Bombs in
stock: hold P1 `X` and P2 `K` on the same frame. Both animations and damage
effects must remain active, neither manager may terminate the other, and the
shared stock must decrease by two.

Return to the title, restart the stage, and repeat once to validate teardown.
Crashes, a stuck loading screen, a Bomb animation using the other player's
airframe, doubled P1-only Bomb damage, or missing P2 Bomb damage are failures.
