# AGENTS.md

Guidance for coding agents working in this repository. This is the single source: Cursor and
Codex read it natively, and `CLAUDE.md` imports it so Claude Code sees the same text. Edit this
file, never a copy.

## What this is

A port of the **q2jump** mod (Quake II trickjump/racing) onto the **Quake II Remastered (KEX)**
game DLL. Output is a drop-in `game_x64.dll`.

`README.md` is the player-facing introduction and the build instructions.
`docs/JUMP_MOD.md` is the full reference — commands, server config, map compatibility.

## Commands

All from the repo root:

```bat
build.bat            :: build Release|x64 into dist/, then build and RUN the unit tests
build.bat x64 v143   :: optional platform / toolset override
play.bat             :: install the DLL locally and launch Quake II
deploy.bat           :: push the DLL to the server share, with a timestamped backup
```

Environment overrides: `Q2J_BUILD_CONFIG` (default `Release`), `Q2J_SKIP_TESTS=1`,
`Q2J_SKIP_VERSION_CHECK=1`, `Q2J_GAME_DIR`, `Q2J_EXE`, `Q2J_LAUNCH_ARGS`,
`Q2J_DEPLOY_DIR`, `Q2J_SKIP_USERDATA`.

`build.bat` fails if the unit tests fail. To run them alone:

```bat
dist\jump_tests_x64.exe
```

There is no test filter — the binary runs every check and prints `N checks, M failures`. To run a
subset, comment out calls in `main()` of `tests/jump_logic_test.cpp`.

Test maps are hand-maintained `.map` sources under `maps/`; there is no build script. See the
Maps section below for the compile line.

`play.bat` also refreshes the DLL in the KEX **user-data** folder when one exists there. That
folder shadows the install directory, so a stale copy silently means testing an older build.

## Versioning

SemVer lives in root `VERSION` and `src/jump/jump_version.h` (must match). Player- or
host-visible notes go under `## [Unreleased]` in `CHANGELOG.md`.

Bumping and releasing are **separate**. Notes stay under `[Unreleased]` across as many bumps
as it takes; only a release stamps them under a dated version heading, so a release lists
every change since the last one. Never tag or cut a release unless asked — that is done in
GitHub, on request.

- `scripts/check-version.ps1` — alignment gate (also run by `build.bat`). Requires an
  `[Unreleased]` section; deliberately does **not** require a dated section for the current
  `VERSION`
- `scripts/bump-version.ps1 -VersionMode patch` — version files only, never the changelog.
  Commit as `chore: bump version to X.Y.Z`
- `scripts/release.ps1` — stamps `[Unreleased]` under the version already in `VERSION`; does
  not commit or tag
- Full steps: `docs/release-process.md`
- Leave `GAMEVERSION` (`baseq2`) alone — that is not the mod version

## Architecture

### Thin vanilla

All mod logic lives in `src/jump/`. Upstream files carry only small hooks tagged `// [Jump]` —
currently ~120 lines across 17 files. Read `docs/THIN_VANILLA_PRINCIPLES.md` before adding one.
`grep -rn '\[Jump\]' src --include=*.cpp | grep -v /jump/` lists every upstream touch point.

Everything is gated on the latched `g_jump` cvar; every public `Jump_*` entry point early-returns
when inactive, so `g_jump 0` restores stock deathmatch exactly.

### Module-owned state

Per-client state lives in `jump_clients[MAX_CLIENTS]` (a module global), **not** on `gclient_t`.
This is deliberate: new fields on `gclient_t` / `level_locals_t` need matching entries in the
savegame reflection tables in `g_save.cpp`. Keep it that way.

### One DLL, two halves — and why the client half is tiny

The DLL exports both the game (`GetGameAPI`) and the cgame (`GetCGameAPI`). The client half is
**`jump_hud_draw.cpp`** (the overlay) and **`jump_cg_move.cpp`** (movement sampling), hooked with
two lines in `cg_screen.cpp` and two in `cg_main.cpp`.

This matters: a player on **stock, unmodified Q2RE can join and play**. Everything the mod sends is
stock protocol — the `CS_STATUSBAR` layout script, `svc_layout`, `CS_PLAYERSKINS`, and ordinary
prints. So the server-authored statusbar must carry the whole HUD (timer, checkpoints, stores,
team, PB, speed); the cgame overlay adds only what a layout script cannot express, and defaults
**off** (`jump_hud 0`) so the host sees what everyone else sees.

**Never add pmove changes.** `p_move.cpp` and `bg_local.h` are untouched by design, which is what
keeps a stock client's prediction identical to the server. Classic 125fps feel is explicitly out
of scope.

`jump_cg_move.cpp` wraps the cgame's `Pmove` export so the overlay can see the predicted velocity
and the raw `usercmd_t` — the server snapshot has neither at frame rate. That wrapper is
**observation only**: it must always call `Pmove` with an untouched `pmove_t`, or a stock client's
prediction and the host's diverge and the paragraph above stops being true. The server does not go
through it at all — `p_client.cpp` calls `Pmove` directly. Note also that prediction *replays*
every unacknowledged command each client frame, so the wrapper only overwrites a scratch struct and
the ring is appended once per rendered frame; anything that counts or accumulates inside the
wrapper will count the same movement several times.

### Engine-free logic layer

`src/jump/jump_logic.{h,cpp}` must not include any engine header. It is compiled into both the DLL
and the host test binary, and is where testable logic belongs: time formatting, the store ring,
records merge/rank/points, and the two safety functions below.

### HUD stat slots

The stat table is full, so jump reuses the CTF block (18–31), aliased in `jump_stats.h`. Safe only
because `Jump_Init()` forces `ctf`/`teamplay` off and `Jump_SetStats()` runs *after* `SetCTFStats()`
in `G_SetStats`. **Slot 27 (`STAT_CTF_TECH`) must stay unused** — the stock statusbar draws a pic
from it in every deathmatch game, so a value there renders as an arbitrary image. All 13 usable
slots (18–31 less 27) are taken, so **the CTF block is closed** — new stats go in 54–63, which are
genuinely free (`STAT_LAST` is 54, `MAX_STATS` is 64). `JUMP_STAT_SPEED` took 54; 55–63 remain.
That range is **ten slots, not eleven**: `bg_local.h`'s own `static_assert(STAT_LAST <= MAX_STATS +
1)` is off by one and would wave through index 64, one past the end of the array. It is also safer
ground than the CTF block — nothing reads 54–63, whereas 27's problem is precisely that the stock
statusbar reads it.

Personal best is the one exception to "one stat per digit": it changes only on a new PB, not every
frame, so it is a `stat_string` (`JUMP_STAT_PB_STRING`) pointing at a per-client configstring
(`CONFIG_JUMP_PB_STRING` in `bg_local.h`, 64 slots) holding the fully-formatted `jump::FormatTime`
string. That is what freed the slot for the run timer's third decimal digit
(`JUMP_STAT_TIME_THOU`). Don't do this for anything that changes every frame — a configstring write
broadcasts to every connected client, not just the owner, so it is only a win for rare updates.
`Jump_UpdatePbString()` (`jump_hud.cpp`) is the only thing that should write that configstring, and
only when the value actually changed.

The PB/record banner (`JUMP_STAT_ANNOUNCE` → `CONFIG_JUMP_ANNOUNCE`) is the same trade, one slot
rather than 64 because the text is global. Note the gate: `stat_string` indexes a configstring *by
the stat's value*, so an announcement row must sit inside `ifstat`/`endif` — an ungated 0 draws
configstring 0. `Jump_Announce()` is the only writer, and only the centred `loc_stat_*` tokens can
centre text, so the client runs the banner through `Localize` — never let a player name lead the
string, or one starting with `$` is read as a localization key.

## Traps that have already caused bugs

- **`pers.netname` is not a name.** The engine overwrites it with an encoded lobby token
  (`##P0`), meaningful only as an argument to the `Loc*` print imports. For anything written to a
  file or baked into a plain string, use `Jump_DisplayName()`, which reads the real name from
  `pers.userinfo`.
- **Layout strings can crash clients.** A malformed token stream makes the client's parser raise a
  fatal error. Always pass untrusted text through `jump::SanitizeLayoutText()`, keep a byte
  reserve, and drop rows rather than risk truncation. Map names go through
  `jump::IsSafeMapToken()` before reaching a `gamemap` command.
- **The `num` layout token** right-aligns in a fixed field, never pads, and truncates over-wide
  values to their *leading* digits. Its font has digits and minus only — no `.`, `:` or `/`, so
  separators are small text drawn between fields, and fractions are published as one stat per
  digit.
- **The client font is proportional** by default (`scr_usekfont`), so space-padded columns never
  line up. Use one cursor token per cell.
- **`Jump_InitLevel` runs while every client is flagged disconnected** — `SpawnEntities` clears
  `pers.connected` ten lines above the hook. Per-client work belongs in `Jump_ClientSpawn`.
- **Knockback is independent of damage.** Zero the damage value and let `T_Damage` continue;
  returning early kills rocket jumping.
- **No engine file I/O.** `game_import_t` has none, so persistence uses `<filesystem>`/`<fstream>`
  plus jsoncpp directly, under a directory resolved from the DLL's own path.
- **No asset indices in `Jump_Init()`.** `gi.soundindex` / `modelindex` / `imageindex` (and
  `gi.configstring`) allocate a configstring, which the server broadcasts unless it is loading a
  map. `Jump_Init` runs from `InitGame`, before a server exists, so the broadcast writes into an
  unallocated sizebuf and the game dies at startup with `SZ_GetSpace: overflow without
  allowoverflow set with a length of 1`. Precache from level-spawn code instead — see the
  `gi.modelindex` calls in `jump_ents.cpp` and `jump_store.cpp`.

## Maps

`maps/` holds hand-maintained `.map` sources, one per feature under test, and each `.map` is the
source of truth. Compiled BSPs and qbsp side outputs (`.bsp`, `.ent`, `.prt`, `.log`) are build
artifacts and are gitignored.

| Map | Covers |
|---|---|
| `jumptest1` | the basics — spawn, timer, a checkpoint, finish on a weapon |
| `jumptest_rocket` | the `rocket` mset: launchers as pickups that must auto-equip, plus a railgun finish on a ledge only a rocket jump reaches |

There is **no generator and no build script** — `make_jumptest.py`, `build_jumptest.bat` and
`jumptest2`-`12` were retired in `dd6e2bf`. Neither the compiler nor the textures ship with this
repo; both live in a sibling checkout. To compile and install one map:

```bat
set T=E:\code\projects\q2-relighter\tools\ericw-tools
set G=E:\code\projects\q2-relighter\gamedata\baseq2
%T%\qbsp.exe -q2bsp -path %G% jumptest_rocket.map jumptest_rocket.bsp
%T%\vis.exe jumptest_rocket.bsp
%T%\light.exe -path %G% -extra4 -bounce 8 jumptest_rocket.bsp
copy jumptest_rocket.bsp "%USERPROFILE%\OneDrive\Saved Games\Nightdive Studios\Quake II\baseq2\maps"
```

Maps load from that user-data `maps` folder, not the Steam install. `bsputil --extract-entities`
dumps a `.ent` next to the BSP, which is the quick way to confirm an `mset` key survived the
compile.

`tools/mapscan/` audits the real 4,252-map q2jump corpus against the entity contract — statically,
and by loading every map headlessly in `q2reproded.exe` with this DLL (q2repro accepts game API
2023, so the *same* module runs). It parses its vocabularies out of `jump_ents.cpp`, `g_spawn.cpp`
and `g_items.cpp`, so it cannot drift; **re-run it after changing the entity contract** and update
the measured numbers in `docs/JUMP_MOD.md`. The headline result: the corpus uses none of the
Refresh-era entities (`trigger_finish`, `cpbox_*`, `jumpbox_*`, lap counters — all zero maps),
finishing on plain weapon pickups and key items instead.

Authoring rules, learned the hard way and no longer written down anywhere else
(`AI_MAP_AUTHORING.md` went with the generator):

- **Never hand-write brush planes.** Winding is *derived* — qbsp takes the normal as
  `(p0-p1)×(p2-p1)` and expects it to point out of the brush. One reversed face compiles cleanly
  and then leaks. Emit boxes from a throwaway script that asserts the winding per face; keep the
  `.map` as the tracked artifact.
- Quake II face lines carry three trailing `contents flags value` integers after the texture
  axes. That is the only way to declare lava, water or a ladder.
- Check every texture exists as a `.wal` under `$G/textures/` before using it. Guessing costs a
  compile cycle — `e1u1/lava1` does not exist, `e1u1/brlava` does.
- The player box is `(-16,-16,-24)` to `(16,16,32)`, so an entity origin must sit at least 24
  above the floor. ~40 is safe and items drop to the floor themselves.

## Reference material

- `docs/JUMPMOD_SEMANTICS.md` — the behavioural contract extracted from the two upstream mods,
  with a **Port decision** line wherever they disagree. Check here before changing game rules.
- `../ref/q2jump` and `../ref/Q2JumpRefresh` — read-only clones of the upstream mods.
- `E:\code\projects\muffmode\MuffMode` — a mature Q2RE mod; the scoreboard, vote menu and
  build-script patterns here follow it.
