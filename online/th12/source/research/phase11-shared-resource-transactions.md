# Phase 11: shared resource transactions

## Runtime boundary

The patch creates one `SharedResourceLedger` per game frame. The ledger covers
Power, lives, life fragments, Bombs, Bomb fragments, score, point value, and the
five original UFO slots (`0x4B0C4C` through `0x4B0C5C`). P2 Bomb consumption and
P2 death life/Power loss enqueue events instead of writing the globals directly.

For original P1 code, the frame boundary is:

1. Read the committed native state.
2. Let P2 enqueue its events.
3. Expose the projected state while the original P1 and item updates run.
4. Convert native deltas into owner-attributed events and restore the projection.
5. At the next P2 frame boundary, apply events in their fixed scheduler sequence,
clamp to the original resource ranges, then write the committed result once.
Queued events are exposed immediately so later original scheduler callbacks see
the current projection rather than a one-event-old snapshot.

This preserves the original game code's reads while making the final shared
state deterministic. If the phase is disabled, P2 falls back to the original
Power-loss and Bomb-consume functions.

## Same-frame rule

Event sequence is P2 update, P1 update, then the remaining scheduler callbacks.
Item-contact exact ties are resolved to P1 before an event is generated. Values
are clamped after each event: Power `0..400`, lives `0..9`, Bombs `0..8`, life
and Bomb fragments `0..4`, and score `0..999999999`. UFO slots use non-negative
integer state.

Either player's death subtracts `0.50P` from shared Power, with the original
`1.00P` floor preserved even after consecutive player deaths. P2 also applies
the original Bomb floor rule: fewer than two Bombs becomes two; two or more is
unchanged.

P2 Option rebuild and shot dispatch execute under an immutable P2 airframe scope
in addition to the player context and ANM slot scopes. The P2 Power cache is
reconciled only after the shared Power value actually changes; P1's earlier hit
state does not force a redundant rebuild. P1's character or shot type cannot be
substituted while the P2 SHT and Option layout are rebuilt.

## Shared revival rule

The lives value is the shared spare-revival inventory. A death reserves and
consumes one life only when the projected inventory is positive. With no spare
life, that player remains eliminated and the other player continues alone. The
original game-over path is released only after both players are eliminated.

A positive life event is settled as follows:

1. If P1 is eliminated, one gained life becomes a P1 revival permit.
2. Otherwise, if P2 is eliminated, one gained life becomes a P2 revival permit.
3. Only when neither player needs revival is the shared lives value increased.

The positive life event may come from a direct 1UP or from the original life
fragment conversion. TH12 stores fragment progress as `0..4`; the fourth
collected fragment completes the original conversion sequence and therefore
uses exactly the same revival reservation path.

Consuming a 1UP for revival forces the original life UI to refresh even though
the numeric shared inventory is unchanged. A revived player receives the normal
entry animation, invincibility, and Bomb floor of two.

UFO fields remain under the original state machine. Before the patch exposes a
queued resource projection at an item or player callback, it first captures any
native UFO transition that ran earlier in scheduler order. This preserves the
original summon/destroy reset side effects while keeping the resulting state in
the shared frame ledger.

## Manual acceptance

1. Collect Power, life, Bomb, fragment, UFO and score items with P1 only; the
   visible result must match the unpatched single-player game.
2. Place both players on one item and collect it on the same frame. Repeat with
   each item category; the final shared value must change once, with P1 winning
   exact ties.
3. With one Bomb remaining, press P1 `X` and P2 `K` together. Both input paths
   must remain deterministic and the shared Bomb count must never become
   negative.
4. Kill P2 while P1 collects an item in the same frame. Shared life/Power loss
   and item gain must both appear after the frame, with no intermediate duplicated
   settlement.
5. Restart, return to title, and repeat. A stuck resource value, double score,
   negative inventory, or crash is a failure.

The runtime transaction layer writes a diagnostic line to `coop\\logs\\patch.log`
when it is armed. Replay persistence is still out of scope.
