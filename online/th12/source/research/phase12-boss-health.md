# Phase 12: Boss effective health scaling

## Runtime path

TH12 1.00b writes Boss life through the ECL set-life path at
`0x00417A4C`. The integer parameter is read at `0x00417A50`, then written to:

- enemy `+0x1608`: current life;
- enemy `+0x160C`: maximum life used by the Boss health display.

The native damage path at `0x00413E9A` passes `&enemy+0x1608` and the
accumulated damage to `0x00412050`. This is the common application point for
player shots, Options, Bomb damage areas and death damage. The patch checks the
Boss flag `0x00400000` from the containing enemy and converts positive damage
to `2/3` of its original value. A three-step integer remainder carry preserves
the exact cumulative ratio instead of dropping every small hit to zero.

The set-life hook now only registers a Boss generation and resets that damage
remainder. It returns the native value unchanged. Consequently `current/max`
remain in the original coordinate system: the health bar keeps the original
red segment, and ECL fixed HP comparisons retain their original meaning.

This is intentional. TH12 cumulative life values contain several consecutive
attacks. Scaling the stored maximum and then separately scaling the spell
start creates an extra white segment and makes the non-spell duration too long.
Scaling the common damage application instead gives every non-spell, spell,
half-life transition and fixed-value bullet phase the same effective 1.5x
durability without stage- or spell-specific constants.

The damage remainder is included in the end-of-frame state hash so both LAN
peers detect an asymmetric fractional carry. Normal enemies, ECL timers,
spell parameters and non-damage health writes are not modified.

## Manual acceptance

1. Enter a Boss with a non-spell followed by at least two spell cards. The
   health bar must not gain an extra white segment; the spell segment remains
   the native red style.
2. Compare sustained fire against an unpatched reference. Each non-spell and
   spell should require approximately 1.5 times the original damage, including
   cards whose bullets change at half or low HP.
3. Use shots, Options, Bombs and a death damage area against the Boss. All
   damage types must be effective, with no one-frame zero-damage lockup for
   small hits.
4. In `coop/logs/patch.log`, verify entries such as:

   ```text
   phase12 Boss phase effective HP armed; ... native_current_max_retained=1; damage_ratio=2/3
   phase12 spell start observed; ... native_hp_coordinates=1
   ```

5. Verify ordinary stage enemies retain their original durability and that
   restart, return to title, continue and later stages do not accumulate the
   ratio.
