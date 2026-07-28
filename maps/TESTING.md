# Testing the port with jumptest1

`jumptest1` is a linear race built to exercise the mod rather than your aim:
nine platforms over a lava floor, one checkpoint, a railgun at the end. Gaps run
96 to 144 units; a standard running jump covers roughly 200, so every jump
should be comfortable. If any of them feel impossible, that is a finding about
rerelease physics and worth reporting.

Falling into the lava kills you, which is what makes the two teams feel
different: Easy can store on each platform and recall after a miss, Hard has to
start over from the spawn.

## Running it

Both files are already installed:

- `game_x64.dll` → `…\Saved Games\Nightdive Studios\Quake II\baseq2\`
- `jumptest1.bsp` → `…\baseq2\maps\`

In the console:

```
deathmatch 1
map jumptest1
```

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

### Teams

- [ ] `team easy` — `store` places a marker, `recall` returns you to it with the
      elapsed time carried over (the timer does **not** reset to zero).
- [ ] `recall 2` returns you to the store before last.
- [ ] `reset` clears your stores; `recall` then says you have none.
- [ ] Finishing on Easy prints a private message and does **not** broadcast.
- [ ] `team hard` — `store` is refused, and `recall` restarts you at the spawn
      with a fresh timer.
- [ ] Finishing on Hard broadcasts and records the time.
- [ ] `team spectator` puts you in spectator; teams change your skin colour.

### Records

- [ ] `maptimes` lists your time after a Hard finish.
- [ ] `playertimes` shows one completion and 25 points for first place.
- [ ] Press `Tab` (score) to see the map times board.
- [ ] Finishing slower than your PB keeps the old time; faster replaces it, and
      the message shows the delta.
- [ ] A file appears at `baseq2\jump\maptimes\jumptest1.json`.
- [ ] Times survive a `map jumptest1` reload and a full game restart.

### Hazards and misc

- [ ] Falling into the lava kills you and restarts the run.
- [ ] Firing the blaster at another player does nothing.
- [ ] `jumphelp` lists the commands.
- [ ] `votemap`, `timeextend`, `yes`/`no` behave (a solo host passes their own
      vote immediately, which is intended).
- [ ] `g_jump 0` then `map q2dm1` behaves like stock deathmatch: weapons are
      picked up normally, damage works, no jump HUD.

## Things I could not test

Written blind, so these are the likeliest places to find breakage:

- HUD element positions and whether the statusbar numbers land sensibly
- whether the checkpoint total (auto-counted from entities) matches intent
- the `jumpbox_*` / `cpbox_*` models, which are not in this map at all
- `trigger_finish` as a brush entity — this map finishes on the weapon instead
