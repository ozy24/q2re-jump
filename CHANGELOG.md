# Changelog

All notable changes to this project are documented here. Versions follow
[Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`, currently pre-1.0 so
expect breaking changes on minor bumps.

Day-to-day notes go under `[Unreleased]`; the version number is bumped only when
cutting a tagged release — see [docs/release-process.md](docs/release-process.md).

## [Unreleased]

### Added

- `jumpers` toggles other players' models and body sounds for that viewer only
  (default ON). Models use Q2RE per-viewer entity instancing (which also drops
  footsteps/falls for hidden players). Rare body sounds (jump, water, gasp) are
  filtered via `local_sound`. Preference survives map changes; reconnect resets
  it.

## [0.1.3] - 2026-08-02

### Fixed

- Spectators see who they are following on the HUD (`FOLLOWING` + name), plus a
  `SPECTATOR` mode line — the jump statusbar had omitted the stock chase identity.

## [0.1.2] - 2026-08-01

### Fixed

- The PB HUD readout no longer floats disconnected from its label. It moved from an
  awkward spot under the timer/checkpoint stack (where a `stat_string`'s lack of a fixed
  width made it impossible to align against the digit boxes above it) to its own row
  directly above PRACTICE/RANKED, bottom-right.
- The value itself now uses `loc_stat_rstring` instead of `stat_string`, which measures
  its own rendered width and right-aligns — so it ends flush with PRACTICE/RANKED's right
  edge regardless of how many digits the time has, instead of drifting short of it.

## [0.1.1] - 2026-08-01

### Changed

- The HUD timer now shows milliseconds (three decimal digits) instead of hundredths (two),
  matching the precision already used everywhere else — records, prints, the scoreboard.
- Personal best went from three per-digit HUD stats to one `stat_string` pointing at a
  per-client configstring holding the fully-formatted time. It only updates on an actual
  new PB rather than every frame, which is what freed a stat slot for the timer's extra
  digit above. See `CLAUDE.md`'s "HUD stat slots" section for the mechanics.
- `docs/JUMP_MOD.md` and `CLAUDE.md` updated to match.

## [0.1.0] - 2026-08-01

Initial tagged release. A partial port of q2jump/Q2JumpRefresh onto the Quake II
Remastered game DLL — see the README's "Feature parity with q2jump" section for exactly
what is and isn't ported.

### Added

- Practice/Ranked teams, 5-deep store/recall ring, movement-triggered timer.
- Finish detection via `trigger_finish`/`weapon_finish` and weapon pickups; checkpoints via
  `key_*` items and `cpbox_*` volumes.
- Q2JumpRefresh's eight core msets, settable live with `sv jump_mset`.
- Per-map records with the standard 15-place points table, `maptimes`/`playertimes`/`ranks`.
- Map voting (`votemap`, `nominate`, TAB menu) and timelimit extension.
- Idle-to-spectator, map rotation and vote pool.
- Full stock-client-compatible HUD and scoreboard over the standard Quake II protocol.
- `jump_version` cvar and a versioned startup banner.
