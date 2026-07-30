# mapscan — corpus compatibility audit

Answers "which of the original q2jump maps work with this mod" over the
4,252-map corpus in `E:\code\projects\q2re-jump\q2jump-maps\`, in two passes.

The question needs both passes because **nothing in the mod ever refuses a
map**. `Jump_CallSpawn` (`src/jump/jump_ents.cpp:345`) returns false for
anything it does not know and `ED_CallSpawn` (`src/g_spawn.cpp:516-521`) just
prints and frees the entity. A lap map loads perfectly and is simply
unfinishable, so "it loads" is not the same as "it works".

## Pass 1 — static (`scan_bsp.py`)

Pure stdlib Python, no engine. Reads every BSP's header, lump table and entity
lump and classifies it.

```bat
python tools\mapscan\scan_bsp.py                          :: the whole corpus
python tools\mapscan\scan_bsp.py --ext .bsp,.tmp,.bsp_old :: include the strays
python tools\mapscan\scan_bsp.py --maps-dir maps          :: control set
```

Structural checks mirror q2repro's own validator (`src/common/bsp.c`,
`src/common/bsp_template.c`) so a static `BROKEN` predicts the engine's
`Couldn't load %s: %s`.

The entity vocabularies are **parsed out of the mod source**, not hardcoded, so
this cannot drift from the DLL:

| source | table | meaning |
|---|---|---|
| `src/jump/jump_ents.cpp` | `jump_spawns[]` | recognised jump entities |
| `src/jump/jump_ents.cpp` | `jump_ignored_classnames[]` | deliberately freed |
| `src/g_spawn.cpp` | `spawns[]` | stock Q2RE spawn functions |
| `src/g_items.cpp` | `itemlist[]` | stock items, with `IF_WEAPON` / `IF_KEY` |

That last one matters: `weapon_pballgun` looks like a weapon but has no Q2RE
item, so it is freed rather than treated as a finish line. A `weapon_*` prefix
test would misclassify every paintball map as playable.

Verdicts: `PLAYABLE`, `DEGRADED`, `UNFINISHABLE`, `NOT_JUMP`, `BROKEN`. Every
row also carries a `flags` column naming each individual reason, and a
`jump_evidence` column of circumstantial signals — the corpus turns out to use
almost no jump-specific classnames, so telling a classic jump map from a stock
DM map is a judgement call the data should expose rather than hide.

`dump_ents.py <mapname>` prints one map's entity lump when a verdict needs
checking by hand.

## Pass 2 — engine (`setup_scan_dir.ps1`, `run_engine_scan.ps1`, `parse_log.py`)

Loads every map for real in `q2reproded.exe` with the mod's `game_x64.dll`.
This works because q2repro accepts game API 2023 natively
(`src/server/game.c:1119-1131`), which is what the mod exports
(`src/game.h:113`); the dedicated server needs only `GetGameAPI`.

```bat
powershell -File tools\mapscan\setup_scan_dir.ps1     :: stage the game dir
powershell -File tools\mapscan\run_engine_scan.ps1    :: grind the corpus (~5 min)
python  tools\mapscan\parse_log.py                    :: raw output -> engine.csv
powershell -File tools\mapscan\setup_scan_dir.ps1 -Remove
```

`setup_scan_dir.ps1` builds `<q2repro>\mapscan\` with `maps` as a **directory
junction** to the corpus (no 2.9 GB copy) plus a copy of the DLL, and leaves
`q2repro\baseq2` alone — its `game_x64.dll.vanilla` stays inactive so a stray
run cannot silently test the wrong module.

`run_engine_scan.ps1` drives the server from generated cfg files. Notes worth
keeping in mind if you change it:

- **stdin piping does not work.** `Sys_RunConsole` only reads input when it owns
  a real console, so commands have to come from cfg files or the command line.
- Chunk cfgs must stay under the 64 KB `CMD_BUFFER_SIZE`
  (`inc/common/cmd.h:25`); `Cmd_ExecuteFile` rejects anything larger with
  `EFBIG`.
- `+set sys_exitonerror 1` is required. On `ERR_FATAL` with an owned console,
  `Sys_Error` prints "Press Ctrl+C to exit." and `Sleep(INFINITE)`s, which would
  hang the batch. Do **not** set `com_fatal_error 1` — that promotes every
  recoverable `ERR_DROP` to fatal.
- `+set jump_debug 1` enables `[jump] ignoring legacy entity <name>`, which is
  what separates *handled by design* from *genuinely unsupported*.
- `+set jump_data_dir` points the mod's own file I/O
  (`src/jump/jump_files.cpp`) at the output dir, or it would litter thousands of
  `maptimes/*.json` into the game dir.
- A map that crashes or hangs the server is recorded and the run resumes after
  it. `-Resume` picks up from `engine_done.txt`.

Each `map` command fully restarts the game DLL, so every map gets a clean
`InitGame` / `SpawnEntities` — good isolation, and about 15 maps/second.

## Reconciling (`merge.py`)

```bat
python tools\mapscan\merge.py
```

Writes `maps_final.csv`, `report.md` and `gaps.md`. The engine is ground truth
for *does it load*; the static scan is the only source for *can it be
completed*. Load-verdict disagreements are listed rather than resolved — each
one is either a scanner bug or engine behaviour that was not modelled.

## Filing the corpus (`sort_corpus.py`)

```bat
python tools\mapscan\sort_corpus.py                 :: dry run
python tools\mapscan\sort_corpus.py --apply         :: move into folders
python tools\mapscan\sort_corpus.py --undo --apply  :: back to flat
```

Moves each map into `playable/`, `degraded/`, `unfinishable/`, `not-jump/`,
`broken/` or `strays/` based on `out/maps_final.csv`. Every move is recorded in
`_sorted.csv` inside the corpus, which is what `--undo` reads, so it is
reversible without re-running the audit. `.filelist` manifests and stray `.map`
source stay at the top level — they are not maps.

Two consequences worth knowing:

- **`map <name>` stops resolving.** Once a map lives in a folder the engine wants
  `map playable/<name>`. The tooling handles this — `scan_bsp.py` walks
  recursively and records a `mappath` column, and the engine driver uses it for
  the `map` command while keeping the bare stem as the log marker so every
  downstream join stays stable.
- **Records are keyed on the map name.** `Jump_MapTimesPath` runs the name
  through `jump::SafeName`, so `playable/42` is stored as `playable_42.json`. If
  you serve maps out of the sorted tree and a later audit moves one between
  folders, its records are orphaned. Treat the sorted tree as a curation view and
  serve from a flat directory.

**There is deliberately no difficulty split.** Nothing in a BSP says how hard a
map is: only ~10% carry a difficulty word in the filename or worldspawn title,
and a good share of those are thematic ("hell", "torture") rather than a rating.
Difficulty is an empirical property — the mod already records per-map times and
completions, so completion rate and median time answer it properly once a server
has traffic.

## Full run

```bat
python     tools\mapscan\scan_bsp.py --ext .bsp,.tmp,.bsp_old
powershell -File tools\mapscan\setup_scan_dir.ps1
powershell -File tools\mapscan\run_engine_scan.ps1
python     tools\mapscan\parse_log.py
python     tools\mapscan\merge.py
python     tools\mapscan\sort_corpus.py --apply      :: optional
```

Output lands in `out/` (gitignored).
