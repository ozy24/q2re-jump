# Testing the port with the jumptest maps

Twelve small maps, each aimed at a different part of the mod, so a failure points
at one code path instead of "something is broken". Several of them exercise
entities that had never been compiled into a map before, so they are the first
real test of that code.

| Map | Tests | Notes |
|---|---|---|
| `jumptest1` | the basics | nine platforms over lava, one checkpoint, weapon finish |
| `jumptest2` | **movement feel**, checkpoints | outdoor downhill circuit: ramps you carry speed down, three corner checkpoints, finishes beneath its own start |
| `jumptest3` | precision, **store/recall** | 48-unit pads that weave side to side |
| `jumptest4` | **checkpoint counting** and `jump_cpwall` | five keys to collect, then a barrier that refuses to open without them |
| `jumptest5` | **teleporters** and the `fasttele` mset | four ledges joined only by teleports |
| `jumptest6` | **water and ladders** | swim a pool, climb a ladder, then jump |
| `jumptest7` | the **rocket mset**, `trigger_weapon`, and knockback | the ledge is out of jump range; you must rocket jump |
| `jumptest8` | **legacy entities** | `trigger_finish` brush, `one_way_wall`, `jump_clip` |
| `jumptest9` | **`trigger_hurt dmg 1`** weapon strip | walk-only; must strip the launcher and *not* kill |
| `jumptest10` | **`trigger_push` checkpoint barrier** | walk-only; shoves you back until you hold both keys |
| `jumptest11` | **finishing at a map exit** | walk-only; no weapon at all, so the exit is the only finish |
| `jumptest12` | the **`gravity` mset**, hazard regression | walk-only; `gravity 200`, plus lava that must still kill |

Gaps in 1-8 run 96-144 units against a running jump of roughly 200, so those
maps test the mod rather than your aim. `jumptest3` and `jumptest7` are deliberately
harder, since the thing being tested *is* the movement, and `jumptest2` is
about whether descending and jumping *flows*. If a
jump feels impossible, that is a finding about rerelease physics worth
reporting.

**9-12 need no jumping at all** — one flat floor, walk from end to end. They came
out of the corpus audit (`tools/mapscan`), and what they test is "did this code
path fire", a question a map you might fall off cannot answer. Each one
centreprints what to do and what to expect as you walk through it.

Falling into the lava kills you, which is what makes the two modes differ in
practice: practice lets you store on each platform and recall after a miss,
ranked makes you start over from the spawn.

## Running it

> **Check the DLL is the one you just built.** Building with `build.bat` writes
> `dist\game_x64.dll` and installs nothing — `play.bat` is what copies it into
> the game. Testing a stale DLL looks exactly like the feature being broken, so
> if a fix appears to do nothing, compare timestamps first:
>
> ```
> dir dist\game_x64.dll
> dir "%ProgramFiles(x86)%\Steam\steamapps\common\Quake 2\rerelease\baseq2\game_x64.dll"
> ```
>
> Note the KEX **user-data** folder shadows the install dir. If
> `…\Saved Games\Nightdive Studios\Quake II\baseq2\game_x64.dll` exists, that is
> the one that loads and a stale copy there wins silently.

Maps install to `…\Saved Games\Nightdive Studios\Quake II\baseq2\maps\` when you
run `maps\build_jumptest.bat`.

In the console:

```
deathmatch 1
map jumptest1
```

Press **TAB** for the vote menu to hop between them.

Rebuild either with `src\build.bat` and `maps\build_jumptest.bat`.

To go back to stock Quake II, delete that `game_x64.dll` — or set `g_jump 0`,
which makes every hook inert without removing anything.

## Checklist

Bind the two commands first: `bind mouse4 store` and `bind mouse5 recall`.

### Timer and finish

- [ ] On spawn the timer reads `0.00` and does **not** move until you press a
      movement key. Standing still, looking around, and firing should all leave
      it at zero.
- [ ] The timer starts on your first movement input and counts up smoothly.
- [ ] Walking into the railgun at the end **without** the checkpoint refuses the
      finish and tells you a checkpoint is missing.
- [ ] Collecting the data CD on platform 5 prints `checkpoint 1/1` with a split.
- [ ] Reaching the railgun **with** the checkpoint stops the timer, turns it
      green, and broadcasts your time.
- [ ] The railgun is still there afterwards and you never actually receive it.
- [ ] After finishing you stay put - upstream never respawns you - and a centre
      print tells you to `kill` to run again.

### Teams

- [ ] `team practice` — `store` places a marker, `recall` returns you to it with the
      elapsed time carried over (the timer does **not** reset to zero).
- [ ] `recall 2` returns you to the store before last.
- [ ] `reset` clears your stores; `recall` then says you have none.
- [ ] `kill` on practice with a store recalls you to it; with no stores it restarts
      you at the spawn. Either works immediately, with no cooldown.
- [ ] Finishing on practice prints a private message and does **not** broadcast.
- [ ] `team ranked` — `store` is refused, and `recall` restarts you at the spawn
      with a fresh timer.
- [ ] Finishing on ranked broadcasts and records the time.
- [ ] `team spectator` puts you in spectator; teams change your skin colour.

### Records

- [ ] `maptimes` lists your time after a ranked finish.
- [ ] `playertimes` shows one completion and 25 points for first place.
- [ ] Press `Tab` (score) to see the map times board.
- [ ] Finishing slower than your PB keeps the old time; faster replaces it, and
      the message shows the delta.
- [ ] A file appears at `baseq2\jump\maptimes\jumptest1.json`.
- [ ] Times survive a `map jumptest1` reload and a full game restart.

### Hazards and misc

- [ ] Falling into the lava kills you immediately.
- [ ] Firing the blaster at another player does nothing.
- [ ] `jumphelp` lists the commands.
- [ ] `votemap`, `timeextend`, `yes`/`no` behave (a solo host passes their own
      vote immediately, which is intended).
- [ ] `g_jump 0` then `map q2dm1` behaves like stock deathmatch: weapons are
      picked up normally, damage works, no jump HUD — and specifically **no
      stray icon bottom right**, which is what a stat-slot clash would look
      like.

### The audit fixes (jumptest9-12)

All four are walk-only and signposted in-game. Each map prints what to expect,
so a mismatch between the sign and what happens *is* the bug.

`jumptest9` — `trigger_hurt dmg 1`. Before the fix this killed you outright.

- [ ] Picking up the rocket launcher does **not** end the run (the `rocket 1`
      mset makes it a tool). You keep it and can fire it.
- [ ] Walking through the **lit gateway** (pillars either side, mid-corridor)
      leaves you **alive**, holding a blaster, timer still running.
- [ ] The **lit patch on your right**, further along, is an ordinary
      `trigger_hurt` and **does** kill — the regression half, so do not skip it.

`jumptest10` — `trigger_push` checkpoint barrier.

- [ ] Walking at the **lit gateway** with no keys shoves you back west and prints
      `You need 2 checkpoint(s) to pass this barrier.`
- [ ] The message repeats no more than once every 5 seconds while you lean on it.
- [ ] Collecting both keys (one bay each side) lets you walk straight through
      with no push and no message.
- [ ] The railgun then finishes normally at `2/2` checkpoints.

`jumptest11` — finishing at a map exit. There is no weapon here at all.

- [ ] Walking into the **brightly lit gateway** at the east end prints
      `Finished in …` and stops the timer.
- [ ] **The map does not change.** It targets `jumptest1`, so if you end up
      there, suppression is broken.
- [ ] The time appears in `maptimes` and the board.
- [ ] Touching the exit again after finishing does nothing untoward.

`jumptest12` — the `gravity` mset and hazards.

- [ ] Jumping is obviously floaty. `sv_gravity` reads `200`, not `800` — this is
      the fix; `SP_worldspawn` used to overwrite it.
- [ ] Stepping into the **orange lava pool** in the centre of the corridor ends
      the run. There is walkable floor either side, so you have to mean it.
- [ ] The **water pool** further along is harmless to walk through.

And the command that makes mset data practical to write (console, not chat):

- [ ] `sv jump_mset` lists all eight keys and the checkpoint requirement.
- [ ] `sv jump_mset rocket 1` on a map with a launcher makes it a pickup rather
      than the finish, immediately.
- [ ] `sv jump_mset gravity 400` changes `sv_gravity` there and then.
- [ ] `sv jump_mset save` writes `baseq2\jump\mset\<map>.cfg`; reloading the map
      applies it.
- [ ] `sv jump_mset bogus 1` reports the unknown key.

### Stock-client view

- [ ] The statusbar shows timer, checkpoints, stores, team and PB. All of this
      is server-drawn, so a vanilla player sees it too.
- [ ] By default (`jump_hud 0`) you are seeing exactly what a stock client
      sees — no client-drawn elements at all.
- [ ] `jump_hud 1` adds one thing: the coloured PB delta after a finish.
      Nothing else changes, and the setting survives a restart.

## Things I could not test

Written blind, so these are the likeliest places to find breakage:

- HUD element positions and whether the statusbar numbers land sensibly
- whether the checkpoint total (auto-counted from entities) matches intent
- the `jumpbox_*` / `cpbox_*` models, which are not in this map at all
- `trigger_finish` as a brush entity — `jumptest1` finishes on the weapon instead

For 9-12 specifically, what *is* verified is that they compile without leaking,
that every entity and key survives into the BSP, that all four load in a real
server with no spawn errors, and that their msets parse (`sv_gravity` really does
read 200 on `jumptest12`). What is **not** verified is anything that needs a
player: no touch function has ever fired, because a dedicated server has no
client to touch anything. That is exactly what the checklist above is for.

You may see `mset is not a valid field` in the console on any map with an mset.
That is expected and harmless — the mod reads the worldspawn `mset` key straight
out of the entity string on purpose, so it never appears in the upstream spawn
field table.
