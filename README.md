# q2re-jump

**Trickjump racing for Quake II Remastered.** No shooting, no fragging — just
you, a clock, and a course you are trying to get through faster than everyone
else.

This is a port of the classic **q2jump** mod onto the 2023 Quake II Remastered
(KEX) game DLL, so the maps and the mode that ran on Quake II jump servers for
twenty years work on the version you already own.

Jump mode is switched on and off with one server cvar: **`g_jump`** — `1`
(the default) enables it, `0` restores stock deathmatch entirely. It is
latched, so a change needs a map restart to take effect.

Release notes live in [`CHANGELOG.md`](CHANGELOG.md); the running server
reports its own version with `jump_version`. How versions are bumped and tagged
is in [`docs/release-process.md`](docs/release-process.md).

---

## What playing it is actually like

You spawn at the start of a map empty-handed — jump maps are about movement, not
shooting, and only maps that ask for a weapon hand one out. The timer sits at
zero until you press a movement key, then it runs. You work your way along the
course — jumps, teleporters, ledges, water, whatever the mapper built — and the
run ends when you touch a weapon. Your time goes on the board.

Some maps make you collect **checkpoints** on the way — they look like the key
items from single-player, and if a map has them you need them all before a finish
counts. That stops you skipping half the course by dropping off a ledge. Ammo,
armour, health and powerups are all inert; there is nothing else to pick up.

Nothing in a map can hurt you except the map itself. Weapons do no damage to
other players, so nobody can interfere with your run; lava, slime, crushers and
hurt zones still kill you, because falling in is the whole point of a jump map.
Players do not collide, so a busy server never blocks a run.

Two ways to play any map:

- **Practice** — learn the route. `store` drops a marker anywhere, `recall`
  puts you back on it with your elapsed time intact. Practice times are shown
  to you and to nobody else, and are never recorded.
- **Ranked** — set a time. `store` is refused and `recall` sends you back to the
  spawn, so a recorded time is always one clean run.

Best times are kept per map, per player, forever, with points for placing in the
top fifteen. `Tab` shows who is connected and what they have done on this map,
plus the records you are chasing.

## Getting started

You need Quake II Remastered installed. Quake II Remastered has no dedicated
server, so this is built for **listen server** hosting — the host plays too.

1. Drop `game_x64.dll` into `<Quake II>\rerelease\baseq2\`.
   (See [Building](#building) if you are compiling it yourself.)
2. Start a deathmatch game. `deathmatch 1` — jump mode forces it on if you
   forget.
3. `map jumptest1` to try the bundled test course, or any jump map you have
   installed.

Bind the two commands you will use constantly:

```
bind mouse4 store
bind mouse5 recall
```

Everything is a **console** command. The rerelease routes chat through its own
lobby, so the usual "say a command in chat" habit does not work here.

| Command | What it does |
|---|---|
| `store` / `recall [1-5]` | Save a position / go back to one (practice only) |
| `reset` | Discard your saved positions |
| `kill` | Go again — recalls on practice, restarts on ranked |
| `team practice`&#124;`ranked`&#124;`spectator` | Switch team |
| `maptimes` / `playertimes` / `ranks` | Records for this map / for you / for everyone |
| `votemap <map>` | Call a map vote — or just press **TAB** for a menu |
| `jumpers` | Hide/show other players' models and body sounds |
| `jumphelp` | The full list, in game |

[`docs/JUMP_MOD.md`](docs/JUMP_MOD.md) is the complete reference: every command,
the voting rules, the scoreboard, server configuration, and how records are
stored.

## Anyone can join, no download needed

A player on a **completely stock, unmodified Quake II Remastered** can connect
and play with everything working — timer, checkpoints, records, scoreboard. All
of it is drawn using the standard Quake II protocol, so there is no client-side
install and no version mismatch to manage.

There is exactly one optional extra for the host's own client: `jump_hud 1` adds
a coloured personal-best delta after a finish. It is **off** by default, so what
you see is what your players see.

## Maps

Classic q2jump maps work. The full 4,252-map original corpus from http://wiki.q2jump.net/downloads.html has been checked
against this mod, both by static analysis and by loading every single one autonomously:

| | Maps |
|---|---|
| Play and finish normally | 2,746 |
| Finishable, but some decorative entities are dropped | 64 |
| Load, but have no finish line at all (practice/test maps) | 298 |
| Not jump maps — mostly Quake 2 Paintball, plus CTF and single-player | 1,132 |
| Will not load — 10 are Quake 1 maps, one truncated, one malformed | 12 |

Two things worth knowing before you go looking for maps:

- **Physics is stock rerelease.** No attempt is made to emulate the old engine's
  125 fps quirks. Some jumps built for that behaviour may be harder, or
  impossible. Times are **not** comparable with classic q2jump servers — treat
  this as a fresh records database.
- **Maps where a weapon is a tool need one line of config.** On a rocket-jump
  map, touching the launcher would normally end your run. Tell the server it is
  a tool instead: `sv jump_mset rocket 1`, then `sv jump_mset save` to remember
  it for that map. About 122 corpus maps want this.

Twelve small test courses ship with the source (`maps/`), each aimed at one
feature so a problem points at one thing rather than "something is broken".
`jumptest9`–`jumptest12` need no jumping skill at all — flat floors, walk from
one end to the other. See [`maps/TESTING.md`](maps/TESTING.md).

The audit tooling lives in [`tools/mapscan/`](tools/mapscan/README.md) if you
want to check a map pack of your own; it reports a per-map verdict and the reason
behind it.

---

## Feature parity with q2jump

This is a **partial port**. It targets the mechanics that make classic maps
work — and 2,746 of the 4,252-map corpus play and finish normally with nothing
missing (see [Maps](#maps)) — but it is not a line-for-line reimplementation of
either upstream mod. `docs/JUMPMOD_SEMANTICS.md` has the full behavioural
comparison; this is the summary.

**Ported and working:**

- Practice/Ranked teams (`easy`/`hard` aliases), matching Q2JumpRefresh's
  store rules — free store/recall in Practice, restart-only in Ranked
- Store/recall as a 5-deep ring (`store`, `recall [1-5]`, `reset`)
- Movement-triggered timer at millisecond resolution
- Finish detection: `trigger_finish`/`weapon_finish` entities and weapon
  pickups
- Checkpoints: `key_*` items, `cpbox_*` volumes, ordered/barrier checkpoints
  via `trigger_push` and `jump_cpwall`/`jump_cpbrush`
- Q2JumpRefresh's eight core msets (`gravity`, `checkpoints`, `damage`,
  `fasttele`, `rocket`, `grenadelauncher`, `hyperblaster`, `bfg`), settable
  live with `sv jump_mset`
- Per-map records with the standard 15-place points table, `maptimes` /
  `playertimes` / `ranks`
- Map vote (`votemap`, `nominate`, TAB menu) and timelimit extension
  (`timeextend`/`votetime`)
- Idle-to-spectator, map rotation and vote pool (`g_map_list`/`g_map_pool`)
- Scoreboard and full HUD over the stock protocol — no client mod required

**Not (yet) ported:**

- **Replay / ghost racing.** Neither the frame-recording demo system (B's
  10 Hz replay format, A's `.dj2`/`.dj3` files) nor racing against a stored
  "spark" ghost exists. Finish times are recorded; runs are not.
- **Noclip in Practice.** Q2JumpRefresh gives the Easy team noclip; this port
  does not.
- **Skill/overall rating.** `ranks` shows raw points, completions and firsts
  per player — there is no normalized score (B's percent-of-maximum, A's
  "israfel" formula) for comparing players across map counts.
- **Admin vote types.** Only map-change and timelimit votes exist; B's kick
  and silence votes are not implemented.
- **Most of A's mset vocabulary.** Only Q2JumpRefresh's eight keys are
  wired up. A-only settings — `health`, `falldamage`, `weapons`,
  `timelimit`, `quad_damage`, `regen`, `singlespawn`, `ezmode`, `allowsrj`,
  `rocketjump_fix`, `fastdoors`/`slowdoors` and others — have no effect here.
- **Lap maps and quad entities.** `trigger_lapcounter`, `trigger_lapcp`,
  `trigger_quad`, `trigger_quad_clear`, `cp_clear`,
  `trigger_single_cp_clear` are all silently ignored. Zero corpus maps use
  them, so this has not cost any real map — but it means lap-based courses
  cannot be authored against this mod.
- **Chat-command entry.** Everything is a console command (see above); the
  rerelease's lobby chat can't be repurposed the way IRC-era Quake II
  clients used chat for jump commands.

Physics is also a deliberate non-goal, not a gap to close: this runs on
stock rerelease movement rather than emulating the original engine's 125 fps
quirks, so times are not comparable with classic q2jump servers.

---

## Building

Windows, **Visual Studio 2022** (Community is fine) with the *Desktop
development with C++* workload (MSVC v143), and Git.

The build uses [vcpkg](https://github.com/microsoft/vcpkg) in manifest mode
(`src/vcpkg.json` pulls in `fmt` and `jsoncpp`). Bootstrap it once at the repo
root — it is deliberately not committed:

```bat
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg.exe integrate install
```

Then, from the repo root:

```bat
build.bat     :: build Release|x64 into dist\, then build and RUN the unit tests
play.bat      :: install the DLL locally and launch Quake II
deploy.bat    :: push the DLL to a server share, with a timestamped backup
```

`build.bat` fails the build if the unit tests fail. It finds VS 2022 via
`vswhere`, so there are no hard-coded paths.

> **`build.bat` does not install anything.** It writes `dist\game_x64.dll`;
> `play.bat` is what copies it into the game. Testing a stale DLL looks exactly
> like a broken feature — and note the KEX **user-data** folder
> (`…\Saved Games\Nightdive Studios\Quake II\baseq2\`) shadows the install
> directory, so an old copy there wins silently.

Overridable by environment variable: `Q2J_BUILD_CONFIG`, `Q2J_SKIP_TESTS`,
`Q2J_GAME_DIR`, `Q2J_EXE`, `Q2J_LAUNCH_ARGS`, `Q2J_DEPLOY_DIR`,
`Q2J_SKIP_USERDATA`.

Map sources are generated rather than hand-built — `maps\build_jumptest.bat`
regenerates, compiles and installs the test courses, and needs
[ericw-tools](https://github.com/ericwa/ericw-tools) plus a directory of Quake II
`.wal` textures.

## Layout

```
.
├─ build.bat / play.bat / deploy.bat
├─ docs/
│  ├─ JUMP_MOD.md                 # the full user-facing reference
│  ├─ JUMPMOD_SEMANTICS.md        # behaviour extracted from both upstream mods
│  └─ THIN_VANILLA_PRINCIPLES.md  # how to extend without forking upstream
├─ maps/                          # test course generator + TESTING.md
├─ tests/                         # host-compiled unit tests (no engine needed)
├─ tools/mapscan/                 # map compatibility auditing
└─ src/
   ├─ jump/                       # all mod logic lives here (22 files)
   ├─ g_*.cpp / p_*.cpp / cg_*.cpp    # upstream rerelease source
   └─ bots/  ctf/  rogue/  xatrix/    # upstream mode / mission-pack code
```

The design rule is **thin vanilla**: mod logic stays in `src/jump/`, and the
upstream files carry only small hooks tagged `// [Jump]` — currently about a
hundred lines across sixteen files. Read
[`docs/THIN_VANILLA_PRINCIPLES.md`](docs/THIN_VANILLA_PRINCIPLES.md) before
adding one. Everything is gated behind the `g_jump` cvar, so `g_jump 0` restores
stock deathmatch exactly.

## Credits and license

The mode is not original work here — it is a port. Behaviour was taken from the
two upstream mods, and where they disagreed the choice is recorded in
`docs/JUMPMOD_SEMANTICS.md`:

- **q2jump** by R1ch — the original, `http://old.r1ch.net/q2/jump/`
- **Q2JumpRefresh** — the later rewrite

The game source is **Copyright © ZeniMax Media Inc.** and licensed under the
**GNU General Public License v2.0** — see [`LICENSE`](LICENSE). Everything added
here is bound by the same terms. This is an unofficial, fan-made mod, not
affiliated with or endorsed by id Software or ZeniMax Media.
