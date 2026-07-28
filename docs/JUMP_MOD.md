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
| `store` | Save your position (Easy only) |
| `recall [1-5]` | Return to a saved position; 1 is the most recent |
| `reset` | Discard all saved positions |
| `kill` | Restart the run from the spawn |
| `team easy\|hard\|spectator` | Change team |
| `maptimes` | Best times on this map |
| `playertimes` | Your completions and points |
| `ranks` | Points for everyone connected |
| `maplist` | Maps in the rotation |
| `votemap <map>` | Call a vote to change map |
| `timeextend [minutes]` | Call a vote to add time (default 15) |
| `yes` / `no` | Vote on the current call |
| `idle` | Move yourself to spectator |
| `jumphelp` | Command list in game |

### Teams

**Easy** is practice. You can store and recall freely, and recalling carries
your elapsed time with it. Easy runs are timed for your own benefit but are
never broadcast and never recorded.

**Hard** is competitive. `store` is refused and `recall` restarts your run from
the spawn, so the only way to a time is a clean run. Only Hard times are saved.

Switching teams abandons the run in progress and clears your stores.

### Finishing

Touching any weapon finishes the run, as does a `trigger_finish` or
`weapon_finish` entity. Key items are checkpoints; if a map has checkpoints you
must collect them all before a finish counts. Everything else in a map — ammo,
armour, health, powerups — is inert.

Combat damage does nothing. World hazards (lava, slime, hurt triggers, crushers)
still kill, so maps keep their fail conditions.

## Map compatibility

Nothing below has been confirmed in game yet — it describes what the code
supports, not what has been played.

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

Cosmetic leftovers (`jump_time`, `jump_score`, `jumpmod_effect`,
`jump_cpeffect`) are removed silently rather than logging an error.

### What works

- **Classic q2jump maps** are the main target. Finish-by-weapon, key
  checkpoints, cpboxes, clip walls, one-way walls and checkpoint barriers are
  all handled.
- **Q2JumpRefresh-era maps** using `trigger_finish` and `cpbox_*` work.
- **Stock deathmatch maps** load and time correctly, but the first weapon you
  touch ends the run, so they are only useful for smoke-testing.

### Known gaps

- **Physics.** This runs stock rerelease movement. Maps built around the old
  engine's 125 fps behaviour may have jumps that are harder or outright
  impossible. This is a deliberate design decision, not a bug — see the top of
  this document.
- **Lap maps.** `trigger_lapcounter` and `trigger_lapcp` are ignored, so lap
  counting does not gate the finish.
- **`trigger_push` checkpoint barriers.** The Refresh variant keyed on a
  `target` of `checkpoint…` is not implemented; use `jump_cpwall` instead.
- **`trigger_hurt` with `dmg 1`** does not strip weapons the way Refresh does.
- **`trigger_quad`, `cp_clear`, `trigger_single_cp_clear`** are ignored.
- **Box models.** `jumpbox_*` and `cpbox_*` reference `models/jump/*box3`,
  which ship with jump map packs rather than with Quake II. Without them the
  boxes are still solid and still work, but may not draw; set
  `jump_box_models 0` if a missing model causes trouble.
- **Invisible brushes and prediction.** `jump_clip` is invisible, so the client
  cannot predict against it and you may see a one-frame correction on contact.

## Server configuration

### Cvars

| Cvar | Default | Meaning |
|---|---|---|
| `g_jump` | `1` | Master switch. `0` restores stock deathmatch entirely (latched) |
| `jump_data_dir` | *(empty)* | Where records live; empty means `jump/` next to the DLL |
| `jump_records_max` | `15` | Rows shown on the times board |
| `jump_idle_time` | `300` | Seconds of inactivity before a player is moved to spectator; `0` disables |
| `jump_box_models` | `1` | Draw jumpbox/cpbox models (they ship with map packs, not with Quake II) |
| `jump_debug` | `0` | Verbose mod logging |

### Map rotation

Create `jump/maplist.txt` next to the DLL, one map per line (`#` starts a
comment). It is loaded into the engine's own `g_map_list`, so rotation, voting
and `nextmap` all behave normally.

### Per-map settings (msets)

Two sources, applied in order so the server always wins per key:

1. the map's own `worldspawn` `mset` key, e.g. `mset "gravity 400 rocket 1"`
2. `jump/mset/<mapname>.cfg`, one `key value` per line

| Key | Default | Meaning |
|---|---|---|
| `gravity` | `800` | Per-map gravity |
| `checkpoints` / `checkpoint_total` | *(counted)* | Checkpoints required to finish; overrides the entity count |
| `damage` | `1` | `0` disables all damage, including hazards |
| `fasttele` | `0` | `1` skips the teleporter freeze |
| `rocket` | `0` | `1` makes the rocket launcher a usable pickup instead of the finish |
| `grenadelauncher` | `0` | As above for the grenade launcher |
| `hyperblaster` | `0` | As above for the hyperblaster |
| `bfg` | `0` | As above for the BFG |

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
