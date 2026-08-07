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
- Team switch mid-run resets the run in both mods, but neither clears the stores: in A they live
  in `client_respawn_t` and the reset call in `CTFJoinTeam` is commented out as a deliberate bug
  fix (`g_ctf.c:3672`, and again for observer at `g_ctf.c:4041`). Joining Easy restores the most
  recent store if one exists — A recalls outright (`g_ctf.c:3678`), as does B.
- There is no separate "time invalidation" flag — Hard simply cannot recall, and Easy times are
  never saved. A's `item_timer_allow` is dead code (only ever set true).

**Port decision (join):** Easy/Hard/Spectator as above, renamed Practice/Ranked/Spectator.
Matching B, a connecting player arrives as a Spectator and the menu opens unprompted; nothing
spawns until they pick. **Divergence:** this port re-asks on every map change, where B and
MuffMode both ask once per connection and carry the team across levels. Map changes on a jump
server are frequent and voted for, so the map you land on is often not the one you chose a team
for; re-asking also means an idle server does not accumulate players parked in Ranked.

The prompt is armed in `Jump_PreSpawn` (top of `PutClientInServer`, above the spectator branch
that would otherwise return before any jump hook runs) and fires from `Jump_ClientThink` ~100 ms
later — the same shape as MuffMode's `initial_menu_delay` / `initial_menu_shown`, and for the
same reason: `ClientThink` is the first point at which the client is really in the world.

**Port decision (stores across a team switch):** match both upstreams — the ring survives the
switch for the current map, and joining Practice recalls store 1. The port originally wiped it to
stop a ranked run inheriting practice shortcuts, but that is already impossible: Ranked refuses
`store` and turns `recall` into a restart. The marker entity is still freed on the way out and
re-placed by the recall, since it is visible to everyone.

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

**Port decision (fasttele granularity):** the velocity clear and the freeze are one decision,
following B. A gates them separately — `spawnflags & 1` on the teleporter suppresses the velocity
clear, `spawnflags & 3` suppresses the view-angle snap, and only the freeze answers to the mset
(`g_misc.c:1985-2006`). The per-teleporter flags are **not** ported: a scan of all 4241 parseable
maps in the corpus found zero `misc_teleporter` setting either bit (the only values present are
`0` and one stray `256`), and in KEX those bits already mean `NO_SOUND` and `NO_TELEPORT_EFFECT`.
Angles are always snapped, matching B. `fasttele` covers `misc_teleporter` and `trigger_teleport`
(39 corpus maps, 405 entities); `trigger_ctf_teleport` is left alone as CTF is forced off.

**Port decision (mset parsing):** keys match case-insensitively and values are validated —
booleans accept `0`/`1`, `on`/`off`, `true`/`false`, `yes`/`no`, integers must be whole tokens.
Both upstreams use `atoi`, so `fasttele on` silently means off and `gravity abc` silently means
zero. Rejected values leave the previous value in place and are reported, since a mset that
quietly did nothing is indistinguishable from a mset that does not work.

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

**Speedometer** — a four-digit `num` with a "Speed" label, bottom centre-right, gated on the stat
so 0 hides it. Both mods compute it every server frame from `(vx, vy, 0)` — vertical excluded —
and cast the length to an int.
- A: `STAT_JUMP_SPEED_MAX` (`g_ctf.h:38`, slot 7) — **the name is vestigial**. `g_ctf.c:1226`
  publishes `resp.cur_speed`; the real `resp.max_speed` tracking in `p_view.c:1089-1099` is never
  networked, and `changelog.txt:319` records the switch from top speed to current. Players on a
  team only; spectators get 0. No toggle beyond the blanket `cleanhud`.
- B: `STAT_JUMP_SPEED` (`jump_hud.h:31`, slot 19), layout at `jump_hud.cpp:173-181`, value at
  `jump_hud.cpp:478-546`. Live players and chase viewers get the raw XY speed; free spectators get
  0; replay viewers get speed derived from a 10-frame position delta with a ±10 ups hysteresis.

**Port decision:** match both on the *value* — XY only, truncated to an int, hidden at rest, and
the followed player's reading while chasing. Diverge on everything else, because this port draws it
**client-side** rather than on the status bar.

That is the significant one. Both mods publish a stat and draw it with the HUD's number pics, which
is the only way a stock client can be shown a live value at all. This port instead treats the
status bar as what a player needs to *play* (timer, checkpoints, stores, team, PB) and the cgame
overlay as what they need to play *better*. The speedometer is the only performance readout that
could have gone either way — a strafe meter, key display or per-jump figure cannot reach a layout
script at any price — and putting it with the rest keeps the tooling in one place. The cost,
accepted rather than worked around: a player on a stock client has no speedometer.

Smaller divergences. The element is **centred above the bottom edge** rather than in the
bottom-right corner, following `q2re-map-trainer`, because it is read mid-jump and a corner costs a
glance away from the map. It has **no caption** — both mods label theirs "Speed", which at
rerelease resolutions is a word you read once and never again. And the refresh rate is a cvar
(`jump_hud_speed_hz`, default 40), because both mods ran on a 10 Hz server and so changed theirs
ten times a second by construction rather than by choice; `10` restores that cadence. It is a
refresh rate, not the ±10 ups hysteresis B applies to replays — the value shown is exact, only
sampled less often.

**Movement overlay** — **new, not a port.** Neither mod shows peak speed or a gain/loss figure;
A's `showjumps` (`p_client.c:2308-2331`) prints a per-jump distance and its delta as chat text, and
both mods' real technique feedback is the key-state HUD plus watching replays. This port draws
peak-of-jump and a signed trend client-side (`jump_cg_move.cpp`, `jump_hud_draw.cpp`), sampled from
the client's own prediction rather than sent by the server, so it costs no stat and no bandwidth —
and, unlike upstream's, it is opt-in and invisible to everyone else.

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
   followed by the matching inventory clear. → **B**: reset via `Jump_StripInventory`, so every
   tool weapon is gone and the player ends up holding nothing, which is also the spawn loadout
   (divergence 10). B's BFG leak does not survive the port, since the inventory is cleared
   wholesale rather than slot by slot.
9. **Finishing at a map exit** — **neither mod does this.** Both leave `use_target_changelevel`
   vanilla and abandon the in-progress run; A's `BeginIntermission` only flushes stat files and
   stops demo recording. → **new behaviour, not a port**: `Jump_LevelExit` records the run when a
   player reaches a `target_changelevel` and suppresses the level change, because a jump server
   chooses its next map by vote or rotation. A handful of old series maps (`4c3jump1`…) put the
   finish line on the exit brush and are otherwise dead ends. Note that in jump mode the vanilla
   path never reached `BeginIntermission` anyway: `g_dm_allow_exit` defaults to `0`, so the exit
   damaged the firing trigger and returned.
10. **Spawn loadout** — A gives a blaster (`jumpmod.c:7129-7134`) but gates the bolt behind the
    `blaster` mset, default `0` (`p_weapon.c:960`), and has deleted the blaster's muzzleflash
    block outright, so a spawned player cannot shoot and produces nothing visible. B gives no
    weapon at all: `pers.weapon = nullptr` on every spawn path (`jump_spawn.cpp:139-144`,
    `:418-431`, `:641-671`), which is why B has no `blaster` or `weapons` mset. → **B**: spawn
    empty-handed. Reaching A's outcome would need the `blaster` and `weapons` msets plus a guard
    in every fire path, for a gun that does nothing. Consequence: with no spawn weapon,
    `G_CheckAutoSwitch`'s `SMART` policy (`g_items.cpp:572-579`) will not auto-switch in
    deathmatch, so `Jump_ItemTouch` sets `newweapon` itself when an mset weapon is picked up.
