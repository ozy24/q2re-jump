# Changelog

All notable changes to this project are documented here. Versions follow
[Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`, currently pre-1.0 so
expect breaking changes on minor bumps.

Day-to-day notes go under `[Unreleased]` and accumulate there across as many version
bumps as it takes; cutting a release moves the whole section under the version being
released — see [docs/release-process.md](docs/release-process.md).

## [Unreleased]

### Added

- **A sound and a HUD banner when someone sets a personal best or a map record.** Both go to
  everyone on the server: `misc/secret.wav` and a 4-second banner for a PB,
  `ctf/flagcap.wav` and a 6-second banner for a record. A record also centre-prints to
  everyone except the finisher, who already has their own "Finished in…" message and only has
  one centre-print slot to lose.

  The banner is part of the status bar rather than an ordinary print because a print lands in
  the notify area, where the next line of chat scrolls it away. A record is always a personal
  best too, so the two never fire together — the record wins.

### Fixed

- **Being the first person to finish a map now counts as taking the record.** It previously
  announced nothing at all: the test asked for a *previous* record to beat, so setting the
  first time on a map was silently treated as an ordinary finish. Holding 1st place is the
  whole test now, which is safe because a time that does not improve on your own entry is
  never given a rank at all. This also restores the `has set a 1st place!` line in the same
  case.

- A **How to Play** page on both menus, a blank line under Extend Time. One panel covering what you
  are racing, what Practice and Ranked each mean, how a map ends, and the store/recall binds
  worth setting — so someone who joins with no idea what a jump server is has an answer in
  the game rather than in a README. It fills the panel exactly: 18 rows is the whole menu,
  and a left-aligned row holds about 26 characters before it draws out past the backdrop.

- The mod version is printed along the bottom of both main menus, under Close, so a player
  can tell a host what they are running without going to the console.

### Changed

- Joining the server now opens the menu instead of dropping you straight into the map. You
  arrive as a spectator and pick Practice, Ranked or Spectator from the menu; nothing spawns
  until you choose. The same prompt runs again on every map change. Both upstream mods do
  this on connect (Q2JumpRefresh: default team spectator, join menu opened), and it is the
  only thing that makes the menu discoverable — previously you had to already know that the
  inventory key opened it.

  Re-asking on every map change is a deliberate divergence: MuffMode and Q2JumpRefresh both
  ask once per connection and keep your team across levels.

- The in-game menu has **Save Position** and **Load Position** rows, so store and recall are
  findable without already knowing the commands. On Ranked they stay listed but read
  `(Locked)` and cannot be picked, so the feature is discoverable from either team.

- Menu wording is consistent Title Case throughout — `Restart Run`, `Vote Map`, `Extend Time`,
  `Follow Player` — instead of the mix of styles it had grown.

- The in-game menu no longer lists the team you are already on with a `(current)` marker, and
  Load Position no longer says `(none saved)`. The title block already names your team, and
  picking Load Position with nothing stored tells you so. That leaves one join row, for the
  team you are not on.

- The menu is now two menus, one for spectators and one for players in the game, because
  half of it only meant something on one side of that line. Spectators no longer see
  `Restart Run`, which did nothing for them; players in the game no longer see the follow
  controls. It swaps as soon as you change team, even while it is open.

  `Restart Run` is the first row of the in-game menu and the one the cursor opens on, since
  it is what you reach for most. Spectators open on a join row. Note the trade: opening the
  menu mid-run now puts a run-ending action under a reflexive attack press.

### Fixed

- The `store` and `recall` refusal messages called the ranked team "Hard" and pointed at
  `team easy`, which are the upstream q2jump names. They now say Ranked and `team practice`,
  matching the rest of the mod.

- The default team constant is now Spectator rather than Ranked. It is no longer a gameplay
  default — just where you wait until the menu is answered.

- Dismissing the menu prints the key that reopens it, once per session. The key comes from
  your own `inven` binding rather than being assumed to be TAB.

- Stores now survive a team switch for as long as the map is running, and joining Practice
  recalls your most recent store instead of dropping you at the map spawn. Previously a trip
  to Ranked and back meant re-running the map from the beginning to place them again.

  Both upstream mods behave this way — q2jump keeps the ring in `client_respawn_t` and has the
  reset call in `CTFJoinTeam` commented out as a deliberate bug fix. The wipe existed to stop a
  ranked run inheriting practice shortcuts, which the Ranked guards already prevent: `store` is
  refused there and `recall` restarts the run. Stores are still cleared by `reset`, by a map
  change, and on disconnect.

- `recall N` now confirms which slot it used — `Recalled store 2 of 3.` A request past the
  stack depth still clamps to the oldest store, but that is no longer silent, so a clamp can
  be told apart from a deep recall. A plain `recall` stays quiet: it is key-bound and constant.

### Fixed

- Crash after a map change if a menu was open when it happened — most easily hit by leaving
  the map vote menu up while the vote you just cast passes. The menu's allocations are freed
  with the level, but the pointer to them was not cleared, so the next press of the menu key
  freed them a second time.

## [0.3.0] - 2026-08-03

### Added

- A second scoreboard page listing the players on the server, with two times each: their best
  ranked run on this map since it loaded, and their all-time best on it. Sorted by the first,
  so it reads as the session leaderboard, and marked ranked vs practice. Spectators are listed
  separately alongside who they are watching. The scoreboard key now cycles players → records
  → closed.

  Two pages rather than one wider board because a scoreboard message is capped at 1024 bytes
  by the engine, which is not enough for both tables at once; alternating gives each of them
  the whole buffer. The players page drops whole rows and reports a `+N more` line when a busy
  server outgrows the budget, and puts whoever has posted a time at the top so a truncated
  board still shows the leaderboard. Worst case measured at 7 rows in 954 of 1024 bytes with
  32 players connected.

## [0.2.0] - 2026-08-03

### Changed

- Players spawn empty-handed instead of holding a blaster, matching Q2JumpRefresh. The blaster
  was fully functional here, which neither upstream mod allows — classic q2jump gates the bolt
  behind an mset that defaults off. Weapons still come from the weapon msets and
  `trigger_weapon`, and rocket jumping is unaffected.
- Agent instructions moved from `CLAUDE.md` to `AGENTS.md`, which Cursor and Codex read
  natively; `CLAUDE.md` is now a one-line import of it, so there is still one source.

### Fixed

- Crash on startup (`SZ_GetSpace: overflow ... with a length of 1`) with `g_jump 1`: unused
  footstep precaches were indexing sounds from `InitGame`, before the server exists.

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
