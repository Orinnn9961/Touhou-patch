# Phase 13: LAN lockstep

## Scope

Protocol version 7 applies network input to the simulation. The host owns P1
and the client owns P2. On both computers the local player uses the original
arrow, `Z`, `X` and `Shift` controls; the local same-keyboard P2 bindings are
ignored while network mode is active.

The room handshake validates:

- room/session identifier;
- minimum input delay, adaptive-delay policy and redundancy window;
- host P1 and client P2 selections captured from the original game menu;
- protocol version and packet checksum.

V22 defaults to the V20 manual-delay mode: both peers use the same configured
`input_delay` and no runtime negotiation changes it. The first delay frames are
neutral warm-up frames. Each subsequent
simulation frame waits until both role-scoped inputs exist. Every outgoing input
packet repeats the configured number of recent frames. A new timeline identifier
is created on stage start, restart and continue so delayed packets from an older
stage cannot be consumed after frame numbers return to one.

`Pause` toggles a synchronized pause at a frame boundary. During a pause the
peers exchange control heartbeats. A disconnect, receive failure, configuration
mismatch, timeout, or state-hash mismatch stops the session and shows an error
instead of allowing one machine to continue silently.

The deterministic player/RNG state hash is exchanged every 600 frames by
default. This is detection, not rollback or state repair.

## Launcher commands

Create a room on the host:

```powershell
.\coop-launcher.exe --host --room 12012 --port 28765
```

Join from the second computer, replacing the address with the host's LAN IPv4:

```powershell
.\coop-launcher.exe --join 192.168.1.10 --room 12012 --port 28765
```

Both commands must use the same room, port, delay and redundancy.
Available airframes are `reimu_a`, `reimu_b`, `marisa_a`, `marisa_b`,
`sanae_a`, and `sanae_b`. Optional network arguments are `--delay 3` and
`--redundancy 8`.

Append `--configure-only` to save the room without starting the game.

Return the launcher to local same-keyboard mode with:

```powershell
.\coop-launcher.exe --local
```

The launcher writes network settings to `coop/config.ini` before starting
TH12. The host's original selection becomes P1 and the client's original
selection becomes P2. The client replaces its native initialization selection
with the negotiated host P1, then both sides load the same independent P2
resource bank. A Ready/Start barrier prevents either simulation from entering
frame one before both resource layouts match.

## LAN prerequisites

Allow UDP on the selected port through Windows Firewall. Both machines must use
the Japanese TH12 1.00b executable and the same co-op DLL. Select the same game
mode, difficulty and stage route on both machines. The host may enter the stage
first and wait at the first lockstep boundary for the client.

Internet play can use the same protocol when the host forwards the UDP port and
the client uses the host's public address. NAT traversal, relay service and room
discovery are not part of this LAN phase.

## Acceptance

1. Select P1 on the host and P2 on the client, then verify both screens show
   the negotiated pair.
2. Confirm arrows/Z/X/Shift control only P1 on the host and only P2 on the
   client.
3. Hold movement and shooting on both sides for several minutes. Positions,
   resources, enemies and Boss HP must remain identical.
4. Press `Pause` on one machine. Both simulations must stop at a frame boundary
   and resume only after the pausing machine presses it again.
5. Temporarily block UDP for less than the disconnect timeout. The game must
   wait and resume when redundant input arrives.
6. Close one game or block UDP beyond the timeout. The other machine must show
   a network error and stop the session.
7. Finish a stage, continue, restart, and return to title. No old-frame input may
   leak into the new timeline.
