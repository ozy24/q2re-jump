# q2jump Semantics Reference

Extracted from the two upstream jump mods (read-only clones in `../ref/`):

- **A** = `ref/q2jump` — classic jumpmod C source, version `1.45global`.
- **B** = `ref/Q2JumpRefresh` — C++17 rewrite, version `1.0slip`.

This is the behavioural contract the Q2RE port targets. Where A and B disagree, the
**Port decision** line records what this mod does.

---

## 1. Teams

| | Easy | Hard | Spectator |
|---|---|---|---|
| store / recall | yes, recall keeps elapsed timer | recall = full respawn (run restarts) | n/a |
| times recorded | never | yes | n/a |
| noclip | yes (B) | no | n/a |
| replays recorded | no | yes | n/a |

- Skins: Easy `female/ctf_r`, Hard `female/ctf_b`, Spectator `female/invis` (B `jump_spawn.cpp:299-322`).
- Default on join: **Spectator**, join menu opened (B `jump_spawn.cpp:358-378`).
- Team switch mid-run resets the run in both mods. Joining Easy in B restores the most recent
  store if one exists.
- There is no separate "time invalidation" flag — Hard simply cannot recall, and Easy times are
  never saved. A's `item_timer_allow` is dead code (only ever set true).

**Port decision:** Easy/Hard/Spectator as above. Phase 1 treats everyone as Easy (no teams yet);
Phase 2 introduces the split. Default team on join = Easy until the join menu exists.

---

## 2. Timer

- **Start**: first movement input. B: `abs(forwardmove)|abs(upmove)|abs(sidemove) > 0`
  (`p_client.c:797-810`). A additionally starts on `BUTTON_ATTACK` (`p_client.c:2139-2148`).
- **Stop**: on accepted finish.
- **Reset**: any respawn / spawn-variable init.
- **Recall (Easy)**: `timer_begin = now - store.time_interval`, i.e. elapsed time carries over.
- **Resolution**: A = 0.1 s accumulator corrected against a ms wall clock (plus a `time_adjust`
  gset). B = pure `Sys_Milliseconds()`, HUD shows seconds + tenths.

**Port decision:** movement-axes only (B), monotonic millisecond wall clock via
`std::chrono::steady_clock` (matches B's `Sys_Milliseconds()`). Start/finish still
occur on a server think (40 Hz), so recorded values are not sub-tick collision times,
but they are no longer forced onto a 25 ms `level.time` grid. No attack-to-start, no
fudge factors, no time_adjust.

### Message formats (B `jump.cpp:226-291`)

```
"%s finished in %s seconds (1st completion on the map)\n"
"%s finished in %s seconds (1st %s)\n"
"%s finished in %s seconds (PB %s | 1st %s)\n"
"<name> has set a 1st place!"                            // green
"You would have obtained this weapon in %s seconds.\n"   // Easy
"You reached checkpoint %d/%d in %s seconds.\n"
"You reached checkpoint %d/%d in %s seconds. (split: %s)\n"
```

Time display `"%lld.%03lld"` (B `jump_utils.cpp:95-100`); deltas rendered `-x.xxx` (green) or
`+x.xxx` (B `jump_utils.cpp:222-234`). A uses `%1.3f` throughout.

---

## 3. Entities

### B spawn table (`jump_ents.cpp:11-20`)

| classname | notes |
|---|---|
| `jumpbox_small` / `_medium` / `_large` | solid decoration boxes, `models/jump/{small,medium,large}box3/tris.md2`, translucent. Sizes: ±16³; (−32,−32,−16)…(32,32,48); (−64,−64,−32)…(64,64,96) |
| `cpbox_small` / `_medium` / `_large` | same sizes, SOLID_TRIGGER, checkpoint pickup |
| `trigger_finish` | brush trigger, SVF_NOCLIENT, touch = finish |
| `weapon_finish` | deprecated alias of `trigger_finish` |

- **Checkpoints**: every `key_*` item except `key_commander_head` (that model is the store marker).
  Listed: `key_airstrike_target, key_blue_key, key_red_key, key_data_cd, key_data_spinner,
  key_pass, key_power_cube, key_pyramid`.
- `cpbox` ordered mode: `target "ordered"` + `count N` — must be taken in sequence, else
  `"You must pick up this checkpoint in order. This is checkpoint %d.\n"` (5 s anti-spam).
  Duplicates ignored per-entity. No-op when checkpoint total is 0.
- `trigger_weapon` — gives weapon by Q2 number (blaster 1 … rail 9, **bfg 0**, hand grenades 11).
- `trigger_hurt` with `dmg == 1` strips all weapons. **A differs** — it clears only the rocket
  launcher and re-arms the blaster; see divergence 8. Neither mod touches ammo, both still apply
  the 1 damage, both repeat per touch, and neither gates it on a spawnflag.
- `trigger_push` with `target` prefixed `"checkpoint"` + `count N` = barrier; passes if
  checkpoints ≥ N, else pushed with `"You need %d checkpoint(s) to pass this barrier.\n"`.
- Items and ammo are inert (`TouchDoNothing`).
- Not implemented in B: `cp_clear`, `trigger_single_cp_clear`, `trigger_quad`, `trigger_quad_clear`.

### A spawn table (`g_spawn.c:172-236`)

`jump_clip, jump_time, jump_score, jumpmod_effect, jumpbox_small/medium/large,
cpbox_small/medium/large, jump_cpwall, jump_cpbrush, jump_cpeffect, one_way_wall, trigger_finish,
trigger_lapcounter, trigger_lapcp, trigger_quad, trigger_quad_clear`.

A's `trigger_finish` (`g_trigger.c:944-968`) takes a `message` key naming a weapon classname
(default `weapon_railgun`) and impersonates a weapon pickup.

### Finish detection

- **A**: *any* `weapon_*` pickup finishes, plus powerups (`Pickup_Powerup`) and power armor.
  Carve-outs: msets `bfg`, `rocket` (covers RL **and** GL), unmet `lap_total`, unmet
  `checkpoint_total`.
- **B**: only `trigger_finish` / `weapon_finish` / weapon items via `PickupWeapon`. Powerups and
  items inert; keys are checkpoints only.

A special pickup names: `"weapon clear"`, `"start line"` (wipes inventory, restarts timer and
recording), `"cp clear"`.

**Port decision:** B's model — weapon item touch or `trigger_finish`/`weapon_finish` finishes;
key items are checkpoints; everything else inert. Weapon msets carve out usable weapons.

---

## 4. msets

**B storage**: `jump/ent/<mapname>.cfg`, one `key "value"` per line. Mapper msets come from the
`worldspawn` `"mset"` key as space-separated `key value` pairs. Apply order reset → mapper →
server, so **server overrides mapper** per key.

**A storage**: `<game>/ent/<mapname>.cfg` plus global `<game>/jump_mod.cfg`. A **ignores**
worldspawn msets entirely when a server cfg exists for the map. Ent overrides live in
`<game>/ent/<map>.add` and `.rem`.

### B implemented keys (`jump_msets.cpp:28-37`)

| key | type | default | meaning |
|---|---|---|---|
| `fasttele` | bool | false | skip teleport freeze (else velocity cleared + `pm_time` 20 + `PMF_TIME_TELEPORT`) |
| `grenadelauncher` | bool | false | GL usable rather than a finish |
| `rocket` | bool | false | RL usable |
| `hyperblaster` | bool | false | HB usable |
| `bfg` | bool | false | BFG usable |
| `checkpoints` | int | 0 | required checkpoint count (docs call it `checkpoint_count`/`checkpoint_total` — inconsistency) |
| `gravity` | int | 800 | per-map gravity |
| `damage` | bool | true | false ⇒ `T_Damage` no-ops |

### A keys, `min,max,default` (`jumpmod.c:84-278`)

`addedtimeoverride 0,1,0` · `allowsrj 0,1,0` · `bfg 0,1,0` · `blaster 0,1,0` ·
`checkpoint_total 0,28,0` · `cmsg 0,1,0` · `damage 0,1,1` · `droptofloor 0,1,1` · `edited_by` (str) ·
`ezmode 0,1,0` · `falldamage 0,1,1` · `fast_firing 0,1,0` · `fastdoors 0,1,0` · `fasttele 0,1,0` ·
`ghost 0,1,1` · `ghosty_model 0,128,0` · `gravity -10000,10000,800` · `health 0,999,400` ·
`lap_total 0,100,0` · `quad_damage 0,6,0` · `regen -100,100,100` · `rocket 0,1,0` ·
`rocketjump_fix 0,1,0` · `singlespawn 0,1,0` · `slowdoors 0,1,0` · `timelimit 0,999,20` ·
`weapons 0,1,0`.

Each mset has a `g`-prefixed server-wide default (`gbfg`, `gcheckpoint_total`, …).
`ezmode 1` forces a store on every hard respawn and makes recall behave like Easy.

**Port decision:** accept `checkpoints` and `checkpoint_total` as aliases. Implement B's eight keys
first; `health`, `falldamage`, `weapons`, `timelimit` follow if a test map needs them. JSON file
format (`jump/mset/<map>.json`) since jsoncpp is already linked, with the `.cfg` key/value form
accepted as a fallback for imported maps.

---

## 5. Points and rankings

Both mods use the same placement table for a map's top 15:

```
1st..15th = 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
```

(A `jumpmod.c:1718-1721`; B `jump_scores.cpp:249-268`.)

- **B percent score**: `total / (mapCount * 25) * 100`, returns 0 unless the user has ≥ 50
  completed maps.
- **A "israfel" score**: `(score / completions) * 4`, only when
  `maps_with_1st > 10 || maps_with_points > 50 || completions > 100`.
- Completions counted once per map per user for the "maps completed" tally; the per-map counter
  increments on every finish.
- `MAX_HIGHSCORES = 15` per map; console listings paginate 20/page (`maptimes` 15/page).

---

## 6. Store / recall

| | A | B |
|---|---|---|
| slots | `MAX_STORES 7` — slot 0 is the active recall slot, 1–6 the stack | `MAX_STORES 5` ring buffer |
| `recall n` | n ∈ 1..6, else `"Invalid number.\n"` | n defaults to 1, clamped to stack depth |
| ground requirement | gset `store_safe` (default **0** = off) | none |
| cooldown | none | none |
| velocity on recall | restored only if `velstore` toggle on, else zeroed | always zeroed |
| angles | ROLL zeroed | ROLL zeroed ("fixes tilted view after recall") |
| stored state | checkpoints, position, angles, velocity, elapsed timer, finished flag | elapsed interval, position, angles |
| marker entity | gset `model_store` | commander head model, translucent, non-solid |

`reset` in B clears the ring and frees the marker; in A it only frees the marker and re-arms
storing (slot contents survive).

**Port decision:** B's model — 5-deep ring, `recall [n]` with n=1 most recent, velocity zeroed,
ROLL zeroed, no ground requirement, no cooldown. `reset` clears the ring.

---

## 7. Other systems

**Idle** — B: cvar `jump_idle_time` default **60 s** (0 disables); movement = any change in
`forwardmove/sidemove/upmove`. States None/Auto/Self. A: 60 000 ms without button input, only when
`enable_autokick`. Messages: `"You are now marked as idle for being inactive for too long.\n"`,
`"You are no longer marked as idle!\n"`.

**Voting** — B: **75 %** yes of participants, **30 s** vote length; types MAPCHANGE, NOMINATE,
VOTETIME, MAPEND, SILENCE, KICK. `votetime <mins>` needs a non-zero timelimit, rejects 0 and
`|mins| > 1000`. Map-end menu vote adds +15 min for "extend". A: `votetime` ±1337 with a 300 s
lockout at map start, per-map extra time capped at 60 min, 3 failed elections per player;
`nominate` blocked in the last 120 s.

**Replay** — B: 48-byte frames at 10 Hz (480 B/s) holding angles, position, animation frame, fps,
key bitset (Jump 1, Crouch 2, Left 4, Right 8, Forward 16, Back 32, Attack 64), checkpoints,
weapon state. 19-step speed table from −100× to +100×. Stored in `local_db.sqlite3`.
A: `MAX_RECORD_FRAMES 200000`, demos in `<game>/jumpdemo/<map>.dj2` (1st place) and
`<map>_<uid>.dj3`, same 19-step speed table.

**jumpers** — toggle that hides other players for a clean view of the map.
- A: `hide_jumpers` (default show ON); models via per-viewer `CS_PLAYERSKINS` invis skins plus
  global translucent renderfx when anyone has them off; also mutes other players' movement sounds
  via `jumpmod_sound`; resets on map change; no chase-target exemption.
- B: `show_jumpers` (default true); models only via per-viewer `CS_PLAYERSKINS`; no sound filter;
  resets on map change.

**Port decision:** keep the classic name/feedback (`Player models/sounds are now ON./OFF.`),
hide models with Q2RE `SVF_INSTANCED` + `Entity_IsVisibleToPlayer` (no invis-skin / translucent
hack). Footsteps/falls/ladder steps stay as entity events — omitted with the model for
jumpers-off viewers (never fan out as unicasts; that overflows the datagram). Rare body
sounds (jump, water, gasp, drown, wade, burn) use `gi.local_sound` when anyone has jumpers
off. Leave weapons / checkpoints / map audio / teleport events alone; no chase-target
exemption. Preference is a session field (survives map changes like `eyecam`); reconnect
resets to ON.

**Race** — replays a stored run as a "spark" ghost with a configurable 0–10 s head start.

**Health/ammo** — B forces 1000 health and 1000 ammo everywhere. A uses msets `health` (400) and
`regen` (100).

---

## 8. Divergences requiring a port decision

1. **Timer start** — A also starts on `+attack`; B only on movement. → **B**.
2. **Timer resolution** — A 0.1 s + ms correction; B pure ms. → **B**, via `steady_clock`.
3. **Stores** — A 6 slots + optional velocity restore + optional ground check; B 5-deep, always
   zeroed. → **B**.
4. **mset name** — `checkpoint_total` (A, B docs) vs `checkpoints` (B code). → **accept both**.
5. **Finish detection** — A: any weapon/powerup pickup; B: `trigger_finish` + weapon items only.
   → **B**.
6. **mset precedence** — A: server cfg suppresses worldspawn entirely; B: per-key merge, server
   wins. → **B**.
7. **Spawn point** — B code prefers `info_player_deathmatch` then `info_player_start`; B docs
   recommend `info_player_start`. Q2RE already falls back this way, so no change needed.
8. **`trigger_hurt dmg 1`** — §5 above records only B's behaviour, but the mods disagree. A
   (`g_trigger.c:806-814`) clears the *rocket launcher slot only* and force-switches to the blaster;
   B (`jump.cpp:664-696`) clears all ten weapon slots and leaves `newweapon` null, so the player
   ends up holding nothing — and it leaks the BFG through, because `FindItem("BFG10K")` is not
   followed by the matching inventory clear. → **B's intent, A's end state**: reset to the standard
   jump loadout via `Jump_StripInventory`, so every tool weapon is gone and the player still holds
   a blaster. Avoids B's weaponless state and B's BFG bug in one move.
9. **Finishing at a map exit** — **neither mod does this.** Both leave `use_target_changelevel`
   vanilla and abandon the in-progress run; A's `BeginIntermission` only flushes stat files and
   stops demo recording. → **new behaviour, not a port**: `Jump_LevelExit` records the run when a
   player reaches a `target_changelevel` and suppresses the level change, because a jump server
   chooses its next map by vote or rotation. A handful of old series maps (`4c3jump1`…) put the
   finish line on the exit brush and are otherwise dead ends. Note that in jump mode the vanilla
   path never reached `BeginIntermission` anyway: `g_dm_allow_exit` defaults to `0`, so the exit
   damaged the firing trigger and returned.
