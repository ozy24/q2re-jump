# q2re-jump

A port of the Quake II jump mod to the Quake II Remastered (KEX) game DLL.

Movement is **stock rerelease physics**. There is no attempt to emulate the old
engine's 125 fps quirks, so times are not comparable with classic q2jump
servers — treat this as a fresh records database.

Quake II Remastered has no dedicated server, so this is built for **listen
server** hosting: the host plays too, and all state survives map changes.

## Installing

Build with `src\build.bat`, then copy `dist\game_x64.dll` to
`<Quake II>\rerelease\baseq2\game_x64.dll`. Start with `deathmatch 1`; jump
mode forces it on if you forget.

## Playing

Bind the two commands you'll use constantly:

```
bind mouse4 store
bind mouse5 recall
```

Chat commands are not available — the rerelease routes chat through the engine
lobby, so everything below is a console command.

| Command | What it does |
|---|---|
| `store` | Save your position (practice only) |
| `recall [1-5]` | Return to a saved position; 1 is the most recent |
| `reset` | Discard all saved positions |
| `kill` | Go again: recalls on practice, restarts on ranked |
| `team practice\|ranked\|spectator` | Change team (`easy`/`hard` still work as aliases) |
| `maptimes` | Full list of best times on this map |
| `playertimes` | Your completions and points |
| `ranks` | Points for everyone connected |
| `maplist` | Maps in the rotation |
| `msets` | Per-map settings in force (gravity, fasttele, which weapons are tools) |
| `votemap <map>` | Call a vote to change map (the menu does this too) |
| `timeextend [minutes]` | Call a vote to add time (default 15) |
| `yes` / `no` | Vote on the current call |
| `idle` | Move yourself to spectator |
| `eyecam` | Toggle first-person follow while spectating |
| `jumpers` | Hide/show other players' models and body sounds |
| `jumphelp` | Command list in game |

### Joining

Connecting puts you in the map as a spectator with the menu already open. Pick
Practice or Ranked; nothing spawns until you do. Every map change asks again,
so the team you play a map on is always one you chose for that map.

Move the cursor with the inventory keys (`invnext` / `invprev`) and select with
`invuse`. While you are spectating, forward/back also move the cursor and
attack or jump selects; once you are in the map they do not, because those
inputs are driving your player. Dismiss
the menu with the same key that opens it — `inven`, conventionally TAB — and
the first time you do, the game tells you which key that is on your own setup.
Dismissing without choosing leaves you spectating; `team practice` and
`team ranked` work from the console at any time.

The menu comes in two forms, because half of it only means something on one
side of the line. It swaps as soon as you change team, even if it is open:

| Spectating | In the game |
|---|---|
| Join Practice | Restart Run |
| Join Ranked | Save Position |
| | Load Position |
| Follow Player / Stop Following | Join Practice / Ranked (whichever you are not on) |
| Follow View: First- / Third-Person | Spectate |
| Vote Map, Extend Time | Vote Map, Extend Time |
| How to Play | How to Play |
| Close | Close |

The mod version sits along the bottom of both, under Close — the same string
the `version` command prints.

The team you are already on has no row of its own — the title block above the
menu names it, so the join row is always the team you are not on.

**How to Play** follows the gameplay rows on both menus, a blank line below
them, and is a one-page summary — what you are racing, what separates the two
teams, how a map ends, and the two binds worth setting. It is the answer to a
player joining with no idea what a jump server is, without them having to read
this document.

**Save Position** and **Load Position** are the `store` and `recall` commands.
On Ranked they stay listed but read `(Locked)` and cannot be picked — Ranked
refuses `store` and turns `recall` into a restart, which is what keeps its
times comparable. Rows that cannot be picked are not dimmed, because the layout
language the menu is drawn in has only two colours (normal and the cursor's
green); the cursor skips straight past them and the row says why.

The rows are there to be found; for actually running a map, bind the commands:
`bind mouse4 store; bind mouse5 recall`.

The cursor opens on **Restart Run** in the game and on a join row while
spectating — in both cases the row you are most likely to have opened the menu
for. Be aware that this puts a run-ending action under a reflexive attack press
if you open the menu mid-run.

### Teams

**Practice** is for learning a map. You can store and recall freely, and
recalling carries your elapsed time with it. Practice runs are timed for your
own benefit but are never broadcast and never recorded.

**Ranked** is for setting times. `store` is refused and `recall` restarts your
run from the spawn, so the only way to a time is one clean run. Only ranked
times are saved.

Upstream q2jump calls these Easy and Hard; those names still work as command
aliases, but they read as map difficulty rather than what they actually are.

Switching teams abandons the run in progress, but your stores are kept for as
long as the map is running — so you can duck into Ranked and come back to
Practice without losing them. Joining Practice puts you straight back on your
most recent store. Stores are cleared by `reset`, by a map change, and when you
disconnect.

### Voting

**Vote Map** in the menu lists every configured map, paged, with the current
map marked `(Playing)` and unpickable; pick another and it calls a vote. While a vote is running the menu
key goes straight to the cast screen instead, showing what was called, who
called it, the tally and the countdown, with Yes and No rows. `votemap <map>`,
`yes` and `no` still work from the console.

A vote passes at 75% of connected players and runs for 30 seconds, resolving
early once it cannot pass. A vote never forces the menu open on anyone — being
interrupted mid-run by a popup is the last thing you want on a jump server. The
join menu is the one exception, and only at the moment you arrive on a map,
before a run can be underway.

### The scoreboard

The scoreboard key (`F1` by default) cycles through two pages and then closes:

1. **Players** — who is connected, with two times each: `session`, their best
   run on this map since it loaded, and `pb`, their all-time best on it. Sorted
   by the session column, so the page reads as tonight's leaderboard. The `pb`
   column is what shows who is quick before they have posted anything today.
   Spectators are listed separately, with who each of them is watching.
2. **Records** — the top times on this map, so you can see what you are chasing
   even when the holder is offline, with your own time and place underneath.

Only ranked runs fill in the `session` column, the same rule the records table
follows: practice runs can recall from a store, so they are not comparable.

A third press closes the board. (`Tab` is the menu, not the scoreboard.)

Two pages rather than one board because the engine caps a scoreboard message at
1024 bytes, which is not enough for both tables at once — see
[the byte budget](#scoreboard-byte-budget) below. Use `maptimes` for the full
records list and `ranks` for everyone's points.

### The speedometer

A four-digit readout sits at the bottom of the screen, centre-right, showing how
fast you are moving in units per second. It hides itself when you are standing
still.

It measures **horizontal speed only** — vertical movement is excluded. That is
how both upstream jump mods measure it, and it is the number that matters:
strafe jumping is horizontal acceleration, a reading that included the vertical
component would spike on every jump and every fall, and the speed gates some
maps put on a teleporter or a push compare against the horizontal figure too. A
HUD that disagreed with the gate would be worse than no HUD.

Everyone sees it, including players on a stock client, because the server draws
it as part of the status bar. Following another player shows *their* speed;
free-flying spectators see nothing, since a camera's speed is not a jump.

If you are running this DLL yourself, `jump_hud 1` adds the best speed of your
current jump above the number, with a signed figure showing whether you are
gaining or losing it — see [the client-side overlay](#the-client-side-overlay).

### Finishing

Touching any weapon finishes the run, as does a `trigger_finish` or
`weapon_finish` entity. Key items are checkpoints; if a map has checkpoints you
must collect them all before a finish counts. Everything else in a map — ammo,
armour, health, powerups — is inert.

Combat damage does nothing. World hazards (lava, slime, hurt triggers, crushers)
still kill, so maps keep their fail conditions.

Two ranked results are announced to the whole server, with a sound and a banner
across the top of the HUD:

| Result | Sound | Banner |
|---|---|---|
| Personal best | `misc/secret.wav` | 4 seconds |
| Map record (1st place) | `ctf/flagcap.wav` | 6 seconds, plus a centre print |

A record is a personal best as well, so only the record announcement fires. The
centre print skips the player who set it — they are already reading their own
"Finished in…" message, and a client only has one centre print at a time.

The banner is drawn as part of the status bar rather than as a chat print
because a print goes to the notify area, where the next line of chat scrolls it
away. Practice runs announce nothing; they are never recorded.

## Stock clients

**A player running unmodified Quake II Remastered can join and play normally.** Nothing
about the mod requires a modified client.

Every gameplay decision — the timer, stores, teams, checkpoints, records, damage rules — is made
on the server. The only things sent to clients are stock mechanisms: the `CS_STATUSBAR` layout
script, `svc_layout` for the times board, `CS_PLAYERSKINS` for team colours, and ordinary print
messages. There are no custom network messages and no new configstring ranges, and the statusbar
uses only layout tokens the stock client already understands.

Movement matters just as much. Prediction runs through `Pmove` in the client, and this port makes
no pmove changes at all, so a stock client predicts identically to the server. That is the real
payoff of building on rerelease physics rather than emulating the old engine.

So a stock player sees the full HUD — timer, checkpoints, stores, team, personal best, speed — plus
every message and the times board. What they miss is the client-side overlay described below.

### The client-side overlay

Two things a layout script cannot express are drawn by the client instead: the coloured delta
against your personal best after a finish, and — sitting on top of the speedometer, never instead
of it — the best speed of the current jump with a signed gain/loss figure beside it.

Both come from movement the client samples every rendered frame, out of its own prediction, so they
are smoother and finer-grained than anything the server could send at the tick rate. Nothing is
sent over the network for them.

**The overlay is off by default**, so out of the box you see exactly what everyone else sees —
which is the useful default when you are hosting.

| Cvar | Default | Effect |
|---|---|---|
| `jump_hud` | `0` | Master switch for the client overlay |
| `jump_hud_speed` | `1` | Peak speed and the gain/loss figure (needs `jump_hud 1`) |

Both are archived, so they persist. With `jump_hud 0` nothing is sampled at all.

### Scoreboard byte budget

Both scoreboard pages are `svc_layout` messages, and the engine's receive buffer
for one is a fixed 1024 bytes that a mod cannot raise — the mod-side constants
are only mirrors of it. Overrunning it does not truncate gracefully: the client
parses whatever token stream it is handed, so a half-written token raises a
**fatal client-side parser error**. That is why vanilla caps its own scoreboard
at 16 players.

So the players page is built to a budget rather than to a player count:

- Rows are appended whole or not at all. The loop stops as soon as the next row
  would not fit alongside a reserve for the trailing lines — it never truncates
  mid-token.
- Whatever did not fit is reported as a `+N more` line, so a short board is
  visibly short rather than quietly wrong.
- Ordering decides who earns the space: whoever has posted a time on this map
  first, fastest first, then the rest by all-time best, then spectators. The
  session leaderboard is the part you cannot afford to lose.
- Names are cut to 14 characters and passed through the layout sanitiser, which
  strips the quotes, backslashes and control bytes that would break the stream —
  a player name is untrusted input.
- A hard 12-row cap sits behind all of that as a backstop.

Measured worst case — 32 connected players, 14-character names, four-digit run
times — is **7 rows in 953 of 1023 bytes**. Typical names and times give 8–9
rows. A full server is therefore a truncated board, never a crashed one.

## Map compatibility

Measured against the full original q2jump corpus — 4,252 maps — by
`tools/mapscan`. Every map was parsed statically *and* loaded for real in
`q2reproded.exe` with this DLL. The two passes agreed on the load verdict for
all 4,252, so the numbers below are observed, not inferred. What they do **not**
cover is physics: a map counted as playable here may still have jumps that are
impossible under stock movement.

| Verdict | Maps | |
|---|---|---|
| Playable | 2,746 | Loads, has a finish path, hits no known gap |
| Degraded | 64 | Finishable, but loses decorative content |
| Unfinishable | 298 | Loads, but the run can never be completed or recorded |
| Not a jump map | 1,132 | Another mod's map — mostly Quake 2 Paintball |
| Will not load | 12 | 10 Quake 1 BSPs, one truncated, one malformed |

Narrowing to maps that actually look like jump maps (excluding paintball, CTF
and single-player content) gives **2,131**, of which 2,052 are playable, 16
degraded and 63 unfinishable.

### The corpus does not use the Refresh-era entities

This is the single most useful thing the scan turned up, and it reshapes the gap
list below. Verified twice — by entity-lump parse and by a raw byte search of
every file:

| Entity | Maps using it |
|---|---|
| `weapon_finish` | 35 |
| any `key_*` item (checkpoints) | 87 |
| `jump_time` / `jump_score` | 1 |
| `trigger_finish`, `cpbox_*`, `jumpbox_*`, `jump_clip`, `one_way_wall`, `jump_cpwall`, `jump_cpbrush`, `trigger_weapon`, `trigger_lapcounter`, `trigger_lapcp`, `cp_clear`, `trigger_quad` | **0** |

Classic maps finish on an ordinary **weapon pickup** and checkpoint on **key
items** — both already handled. The `trigger_finish` / `cpbox_*` vocabulary
belongs to Q2JumpRefresh and does not appear in this corpus at all. Support for
it is still correct to have; it just is not what makes these 4,252 maps work.

### Supported entities

| Entity | Behaviour |
|---|---|
| `trigger_finish`, `weapon_finish` | Ends the run |
| any `weapon_*` item | Ends the run, unless the map enables it via an mset |
| any `key_*` item | Checkpoint |
| `cpbox_small` / `_medium` / `_large` | Checkpoint volume |
| `jumpbox_small` / `_medium` / `_large` | Solid box |
| `jump_clip` | Invisible solid wall, or a checkpoint volume when `message` is `checkpoint` |
| `one_way_wall` | Passable one way only; spawnflag 1 lets fast players through |
| `jump_cpwall`, `jump_cpbrush` | Checkpoint barrier; spawnflag 1 inverts the test |
| `trigger_weapon` | Gives the weapon named by `count` (blaster 1 … rail 9, BFG 0) |

Ten legacy classnames are removed silently rather than logging an error —
`jump_time`, `jump_score`, `jumpmod_effect`, `jump_cpeffect`,
`trigger_lapcounter`, `trigger_lapcp`, `trigger_quad`, `trigger_quad_clear`,
`cp_clear` and `trigger_single_cp_clear`. The list lives in
`jump_ignored_classnames[]` in `src/jump/jump_ents.cpp`; anything that walls off
part of a map must get a real spawn function instead of going here.

### What works

- **Classic q2jump maps** are the main target and the bulk of the corpus.
  Finish-by-weapon and key checkpoints carry almost all of them.
- **Q2JumpRefresh-era maps** using `trigger_finish` and `cpbox_*` work, though
  none appear in the original corpus.
- **Stock deathmatch maps** load and time correctly, but the first weapon you
  touch ends the run, so they are only useful for smoke-testing. They are
  counted as playable above; treat that as "loads and times", not "worth
  playing".

### Known gaps

Impact is the number of maps affected out of the 4,252 scanned.

| Gap | Maps | |
|---|---|---|
| **Physics** | unmeasured | Stock rerelease movement. Jumps built around the old engine's 125 fps behaviour may be harder or impossible. Deliberate — see the top of this document. Only playtesting can quantify it, and it is very likely the largest real-world gap. |
| **No mset data exists** | ~122 | See below. The most actionable gap remaining. |
| **Worldspawn `nextmap` with no exit entity** | 6 | `nextmap` is consumed by `EndDMLevel` on the timelimit, never by a player, so it ends nothing. Not fixable from the mod side: the map has no player-reachable exit. |
| **Lap maps** | 0 | `trigger_lapcounter` / `trigger_lapcp` ignored, so lap counting does not gate the finish. No corpus map uses them. |
| **`trigger_quad`, `cp_clear`, `trigger_single_cp_clear`** | 0 | Ignored. No corpus map uses them. |
| **Box models** | 0 | `jumpbox_*` / `cpbox_*` reference `models/jump/*box3`, which ship with map packs rather than with Quake II. Boxes stay solid either way; set `jump_box_models 0` if a missing model causes trouble. No corpus map uses them. |
| **Invisible brushes and prediction** | 0 | `jump_clip` is invisible, so the client cannot predict against it and you may see a one-frame correction on contact. No corpus map uses it. |

Separately, **298 maps have no finish entity of any kind**, which is now the only
thing left making a map unfinishable. Those are practice and test maps
(`aimtrain`, `admintryouts`, `arcrates`) — there is nothing to implement, they
simply have no finish line.

### Gaps the audit closed

| Fix | Maps | |
|---|---|---|
| **`trigger_hurt` with `dmg 1`** | 24 | Used to **kill the player outright** — `MOD_TRIGGER_HURT` was listed among the fail conditions, so a zone the map meant as "drop your weapon here" ended the run. Now resets to the empty spawn loadout and the run continues. |
| **Finish at a map exit** | 5 | A `target_changelevel` reached by a player now records the run. The level deliberately does **not** change: a jump server picks its next map by vote or rotation. New behaviour — no upstream mod recorded a run on a level change. |
| **`trigger_push` checkpoint barriers** | 2 | A push whose `target` starts with `checkpoint` passes you through at or above its `count`, and otherwise prints and lets the ordinary push shove you back. It gates, it does not block — that is the difference from `jump_cpwall`. |
| **The `gravity` mset never applied** | all | Found while testing the above. `SP_worldspawn` sets `sv_gravity` from the map's own key and spawns *after* `Jump_InitLevel`, so the mset was overwritten a moment after being written. Now latched and applied on the next frame — a one-shot, so a map's own `target_gravity` still works. |

Decorative losses are minor: the most common unsupported classname is
`func_model` (95 maps), an old mapping-tool model placer with no Q2RE spawn
function. Those maps lose scenery and stay completable. Roughly 270 legacy
entity keys are rejected with `<key> is not a valid field`; every one is
paintball team plumbing, editor metadata or a typo. **No map loses jump
behaviour to a rejected key.**

### Weapons that are tools, not finish lines

No map in the corpus carries a worldspawn `mset`, and no `mset/<map>.cfg` files
ship with the mod. So every map runs on defaults, and the default is that *any*
weapon ends the run.

That is right for the 1,879 of those 2,131 that carry exactly one weapon pickup —
it is the finish, and it works. It is wrong wherever a weapon is meant as a
tool: rocket jumping, grenade boosting. **122 maps carry more than one weapon
type**, which is the population where a run may end early:

| Weapon | Maps containing it | …as the only weapon |
|---|---|---|
| `weapon_rocketlauncher` | 132 | 28 |
| `weapon_bfg` | 77 | 41 |
| `weapon_grenadelauncher` | 55 | 6 |
| `weapon_hyperblaster` | 53 | 18 |

Where the gated weapon is the *only* weapon it is the finish and needs nothing.
The rest need an mset per map — `rocket 1`, `hyperblaster 1` and so on — and
that data has to be written by hand or recovered from an old server. The code
path already exists (`Jump_IsUsableWeapon`); what is missing is the data.

`sv jump_mset` exists to make that data tractable. It is a console/rcon command
rather than a client one, because these settings decide whether a run counts:

```
sv jump_mset                  list every mset and the checkpoint requirement
sv jump_mset rocket 1         apply live to the running map
sv jump_mset save             write mset/<map>.cfg
sv jump_mset reload           re-read that file
```

The workflow is to play the map, set the weapons that are tools rather than
finish lines, confirm the run behaves, then `save`. `reload` re-reads the file
over the current values, so keys the file omits keep what they hold now —
restart the map for a clean slate. An unknown key is reported rather than
silently ignored.

### Maps that will not load

Twelve of the 4,252, all confirmed against the engine:

- **Ten Quake 1 BSPs** — `1on1r`, `bases`, `genders2`, `h4rdcore`, `mbasesr`,
  `rs_zz1`, `tf2k`, `vote40`, `well6`, `xpress3`. Wrong game; nothing to fix.
- **`tehjump6`** — truncated file (`Visibility lump out of bounds`).
- **`putt-jumps`** — its entity lump begins with a stray `; worldspawn`, giving
  `ED_LoadFromFile: found ";" when expecting {`. Worth knowing that this one
  aborts via `ERR_DROP`, and the engine's `longjmp` **discards the pending
  command buffer** — so a map like this in a rotation silently drops whatever
  was queued behind it. Keep it out of `maplist.txt`.

The corpus also holds ten `.tmp` interrupted downloads and one `.bsp_old`, which
`map <name>` cannot reach in any case — the ten `.tmp` files are truncated, the
`.bsp_old` is intact. Forty-three groups of maps are byte-identical under
different filenames.

### Re-running the audit

`tools/mapscan/README.md` has the details. The short version, about five minutes
end to end:

```bat
python     tools\mapscan\scan_bsp.py --ext .bsp,.tmp,.bsp_old
powershell -File tools\mapscan\setup_scan_dir.ps1
powershell -File tools\mapscan\run_engine_scan.ps1
python     tools\mapscan\parse_log.py
python     tools\mapscan\merge.py
```

Per-map verdicts land in `tools/mapscan/out/maps_final.csv`, with `report.md` and
`gaps.md` alongside. The scanner parses its entity vocabularies out of
`jump_ents.cpp`, `g_spawn.cpp` and `g_items.cpp`, so adding a spawn function
changes the numbers on the next run without touching the tool. Re-run it after
any change to the entity contract.

## Server configuration

### Cvars

| Cvar | Default | Meaning |
|---|---|---|
| `g_jump` | `1` | Master switch. `0` restores stock deathmatch entirely (latched) |
| `jump_version` | *(set at init)* | Read-only. The mod version; matches root `VERSION` |
| `jump_data_dir` | *(empty)* | Where records live; empty means `jump/` next to the DLL |
| `jump_records_max` | `15` | Rows shown on the times board |
| `jump_idle_time` | `300` | Seconds of inactivity before a player is moved to spectator; `0` disables |
| `jump_box_models` | `1` | Draw jumpbox/cpbox models (they ship with map packs, not with Quake II) |
| `jump_debug` | `0` | Verbose mod logging |
| `jump_hud` | `0` | **Client-side**, unlike the others. Off means you see the exact stock-client view; `1` adds the coloured PB delta after a finish |

### Map rotation and the vote pool

Two cvars, matching MuffMode's meaning, both plain whitespace-separated lists of
map names:

| Cvar | Meaning |
|---|---|
| `g_map_list` | the rotation — played in order, and votable |
| `g_map_pool` | votable only, never rotated into automatically |

`jump/maplist.txt` next to the DLL (one map per line, `#` starts a comment) is
loaded into `g_map_list` at startup, so you can keep the rotation in a file
rather than a long cvar. The vote menu offers the pool first, then the list,
de-duplicated.

Map names are validated before use: anything with quotes, shell characters,
`..` or an over-long path is rejected, since the name ends up in a `gamemap`
command.

### Per-map settings (msets)

Two sources, applied in order so the server always wins per key:

1. the map's own `worldspawn` `mset` key, e.g. `mset "gravity 400 rocket 1"`
2. `jump/mset/<mapname>.cfg`, one `key value` per line

| Key | Default | Meaning |
|---|---|---|
| `gravity` | `800` | Per-map gravity |
| `checkpoints` / `checkpoint_total` | *(counted)* | Checkpoints required to finish; overrides the entity count |
| `damage` | `1` | `0` disables all damage, including hazards |
| `fasttele` | `0` | `1` carries your speed through a teleporter instead of clearing velocity and freezing you for 160 ms. Applies to `misc_teleporter` and `trigger_teleport` alike |
| `rocket` | `0` | `1` makes the rocket launcher a usable pickup instead of the finish |
| `grenadelauncher` | `0` | As above for the grenade launcher |
| `hyperblaster` | `0` | As above for the hyperblaster |
| `bfg` | `0` | As above for the BFG |

Almost no map in the wild carries a worldspawn `mset` — classic q2jump kept these
server-side — so in practice everything comes from the cfg files, and those have
to be written. `sv jump_mset` (console/rcon only) does that without editing files
blind:

| Command | Effect |
|---|---|
| `sv jump_mset` | List every key, its current value, and the checkpoint requirement |
| `sv jump_mset <key> <value>` | Apply live to the running map |
| `sv jump_mset save` | Write `jump/mset/<mapname>.cfg` |
| `sv jump_mset reload` | Re-read that file over the current values |

Keys are case-insensitive. Booleans take `0`/`1`, `on`/`off`, `true`/`false` or
`yes`/`no`; anything else is rejected and reported rather than read as `0`, which
is what both upstream mods do and what makes a mistyped mset look like a broken
one.

`gravity` is pushed to `sv_gravity` and a checkpoint change invalidates the
cached total, so a live edit behaves exactly like one read from the file.
`reload` does not undo live edits to keys the file omits — restart the map for
that. See "Weapons that are tools, not finish lines" for why this matters.

**Setting msets is console/rcon only.** Both upstream mods expose a settable
`mset` command to players, gated on `sv_cheats`; this port does not, because
client commands carry no privilege model and these settings decide whether a run
counts. Players get the read-only `msets` command instead, and typing `mset`
prints the same list plus a pointer to `sv jump_mset`.

### Records

One JSON file per map under `jump/maptimes/<mapname>.json`, holding a single
entry per player — their personal best. Points come from placement
(25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 for 1st through 15th) and are
derived by scanning those files, so there is no separate total to drift.

Files are written through a temporary file and renamed, so quitting mid-write
cannot corrupt them. A file that fails to parse, or that was written by a newer
schema, is reported and left alone rather than overwritten.

## Development

Architecture follows `THIN_VANILLA_PRINCIPLES.md`: all mod logic lives in
`src/jump/`, and upstream files carry only small hooks tagged `// [Jump]`. Mod
state is module-owned, so nothing needs describing to the savegame tables in
`g_save.cpp`.

`src/jump/jump_logic.{h,cpp}` is engine-free and covered by
`tests/jump_logic_test.cpp`; build `tests/jump_tests.vcxproj` and run
`dist/jump_tests_x64.exe`.

Behavioural reference for the port decisions is in `JUMPMOD_SEMANTICS.md`.
