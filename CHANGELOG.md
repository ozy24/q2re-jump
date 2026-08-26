# Changelog

All notable changes to this project are documented here. Versions follow
[Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`, currently pre-1.0 so
expect breaking changes on minor bumps.

Day-to-day notes go under `[Unreleased]` and accumulate there across as many version
bumps as it takes; cutting a release moves the whole section under the version being
released — see [docs/release-process.md](docs/release-process.md).

## [Unreleased]

### Added

- **Attempts and completions per player per map**, recorded into
  `jump/maptimes/<map>.json` alongside the times. An attempt is a Ranked run started; a
  completion is a Ranked run finished, whether or not it beat your best. Practice counts
  toward neither. Nothing displays these yet — this release only starts collecting them,
  because neither figure can be reconstructed after the fact.

  Attempts are counted when the clock starts rather than when a run is given up on, so
  every way of abandoning one — dying, `kill`, `recall`, changing team, disconnecting, a
  map change — is covered. The one visible consequence: on a map with a `start_line`,
  crossing the line again during the same life re-zeroes the clock without counting as a
  fresh attempt.

  Counters are batched and written at most every 15 seconds, unlike a personal best, which
  is still saved the moment it is set — so a server killed outright can lose up to 15
  seconds of counting. Existing records files are migrated on first load, seeding each
  entry with 1 attempt and 1 completion. That is a floor, not recovered history: real
  counts start from this version.

  **This raises the records schema to 2 and is effectively one-way.** An older DLL refuses
  to read a schema-2 file, which for the maps affected means no personal bests, no
  `maptimes` listing, no new times recordable, and the map dropping out of every player's
  points and completions totals until the DLL is upgraded again. Nothing on disk is
  destroyed.

## [0.11.0] - 2026-08-15

### Added

- **`replay`**, from classic q2jump - watch your own personal best back, frozen and moved
  through the saved frames. Recording samples origin, view angle, velocity and key inputs
  at a fixed 40 Hz, bucketed by elapsed run time rather than by server tick - both upstream
  mods this port is based on baked their server's 10 Hz tick straight into the recording,
  which plays back choppy at any other rate. This one is smooth regardless of the server's
  actual tick rate, and interpolates between recorded frames so movement never snaps. A run
  is recorded on Ranked only and saved whenever it beats your own personal best; `replay
  stop` ends one early, and reaching the end hands control back on its own.

- **`race`**, the classic mods' "race spark" - a short green trail tracing your own ghost's
  recent path while you play the course for real, so you can see whether you are ahead or
  behind as you go. Only you see your own ghost. Unlike the classic mods, which re-sent the
  trail as a temporary effect every single server tick, this one is drawn with a handful of
  persistent beam entities repositioned each tick - the same amount of visual information at
  a fraction of the network cost. `race off` stops it.

- Replays are saved one per player per map under `<data>/replays/<map>/<player-id>.jrep`, a
  compact binary format (delta-coded and varint-packed, not JSON) rather than a new external
  dependency.

## [0.10.0] - 2026-08-14

### Added

- **`start_line`**, from classic q2jump. A brush trigger a mapper places on the track; crossing it
  restarts your clock where you stand, clears your checkpoints and empties your hands. The run
  therefore begins at the line rather than on your first movement.

  It fires for as long as you are stood on it, which is what makes the timer start as you *leave*
  the line rather than as you enter it — walk up to it at your leisure, the clock only counts from
  the moment you step off. Maps without one are unaffected: they still start on first movement.

  Your recall stores survive, which is a deliberate departure from upstream. Stores are a Practice
  tool rather than part of the timed contract, and losing them on every lap of a map built around a
  start line would make Practice worse without making anything more comparable. The Practice
  grapple survives the wipe for the same reason.

- **`weapon_clear`**, also from classic q2jump — the same kind of brush trigger, emptying your hands
  without touching the run or your checkpoints.

### Changed

- **The grapple hook is much faster.** Fly speed 650 → 2000, pull speed 650 → 1100. Stock CTF's
  numbers meant watching the hook travel and then being dragged at running pace, which is not much
  of a shortcut past a jump you cannot do yet; classic q2jump shipped 1200/750 and this goes
  further. Servers that want different numbers can still set `g_grapple_fly_speed` and
  `g_grapple_pull_speed` in their config — both are read live, and the pull even retunes a hook
  that is already attached.

### Fixed

- **`start_line` entities no longer vanish at map load.** The classname had no spawn function, so
  the engine freed the entity before the map ran and the line silently was not there. This is why
  timing on maps with a start line began on first movement and never reset.

- **The strafe meter now works on ice.** On a slick surface you stay on the ground, so every frame
  of a slide was excluded as unmeasurable: the bar froze at its last airborne reading for half a
  second and then went blank for as long as you kept sliding — through what is a sustained strafing
  phase and exactly when the reading is worth having.

  Ice is the one kind of ground where the meter can be exact, because friction genuinely does not
  run there, so those frames are now graded against the ground acceleration model. Ordinary ground
  is still excluded, and so is the jump itself — the stroke you take off an ice brush with runs
  under air physics from a frame that started on the floor, and neither model describes it.

## [0.9.5] - 2026-08-10

### Added

- **The `singlespawn` mset**, from classic q2jump. `1` keeps the first `info_player_deathmatch` the
  map declares and drops the rest, so everyone starts a run from the same place.

  Most jump maps are built on deathmatch scaffolding and inherit a scattering of spawn points that
  stock spawn selection then picks between — which means two players race the same map from
  different starting positions, and their times are not measuring the same thing. This is the fix
  for a map whose author never cleaned that up. Set it per map like any other mset:
  `sv jump_mset singlespawn 1`.

- **A grapple hook on Practice**, and never on Ranked. `bind mouse3 "use Grapple"`, then hold
  attack. You always have it — it is not a pickup and no map has to place one.

  It is a practice tool in the same family as `store`: a way to reach the part of a map you are
  working on when the jump before it is still beyond you. Ranked cannot have one at any server
  setting, because a time set with a hook is not a time anyone can compare theirs to.

  This is the stock Quake II grapple, the one CTF uses, which is why it costs a player on an
  unmodified client nothing — the model, the sounds and the cable all ship with the game.
  Upstream q2jump has its own hook governed by server settings rather than by team; this reuses
  what the rerelease already has and gates it where the difference actually matters.

- **Takeoff speed**, above the speedometer: the same number, frozen at the moment your feet left the
  ground. Everyone can have it, including players on a stock client — turn it on with `takeoff` or
  from Options.

  This is the readout for someone learning the movement, and the first one that is. The top number
  is the mark, the bottom is you, bigger is better — no explaining required. In the air it shows what
  the jump has gained so far; across takeoffs it shows whether the whole cycle gained or lost, ground
  contact included. Freezing at takeoff rather than landing is what puts a slow ground contact inside
  the number instead of hiding it, and it is the only stable moment to read: friction removes around
  60 ups in a single frame at 400 ups, so anything sampled near a landing measures when you looked
  rather than how you jumped.

  Stop for about three quarters of a second and the mark clears rather than hanging above your speed
  from a run you have already left.

  It costs one stat and one configstring per player, rewritten only when the value changes — once per
  jump. That is what makes a teaching element affordable on the status bar, where it reaches
  everyone, rather than in the overlay behind a download.


- **A CGaz strip**, `jump_hud_cgaz 1`, for players running the mod's own DLL. Translucent bands just
  below the crosshair showing which view angles would accelerate you and which is best, drawn
  relative to your view so you steer the centre mark into the green. Off by default: unlike the
  speedometer and the strafe meter it is genuinely new on screen, it sits where you are aiming, and
  it needs explaining before it helps.

  It carries a red wedge in the middle that plain CGaz does not have, and that wedge is the reason
  it is worth drawing in Quake II rather than being a straight port. In the Q3-style physics CGaz
  was built for, the best angle sits comfortably inside its zone; here it is pinned to the wedge's
  edge, where turning a fraction less stops the acceleration entirely. At 400 ups and 125 fps that
  edge is half a degree away.

  It is live and follows whatever the game is doing, ground included — on the ground the
  acceleration is stronger and the best angle sits wider, and the strip says so rather than going
  blank every time you touch the floor. It goes quiet only on ladders and in water, where the game
  overrides your input and any angle it drew would be pointing somewhere else.

  It pairs with the strafe meter rather than replacing it — one shows where to look, the other how
  well you looked. Credit to Sata for that framing.

- **The strafe meter now says when you are turning too little**, with a `!` just past the fill —
  `[####!-------]` — at the edge of what you captured. Players running the mod's own DLL get a red
  track behind the bar for the same thing.

  This is the one distinction the bar was missing, and it matters more than it sounds. Being *wide*
  of the best angle is gentle: the gain tapers off over about forty degrees, so you still collect
  most of what was on offer. Being *inside* it is a wall — the engine stops accelerating you
  altogether, so the frame is worth exactly nothing however slightly you crossed. At 125 fps that
  boundary sits half a degree from the best angle, against forty-eight degrees of room on the other
  side. Both mistakes used to draw the same short bar.

  Thanks to Sata for the report that turned this up: "I'm doing perfect strafes and sometimes bar
  is empty and sometimes full." The meter was right — that is what riding a half-degree edge looks
  like — but it had no way to say so.

### Changed

- **Only the speedometer is on by default now**, and there is a **Reset Readouts** row in Options
  (and a `hudreset` command) that puts you back to that state.

  What the HUD shows unasked is what you need to *play* — timer, checkpoints, stores, personal best,
  team, time remaining — plus the one readout that needs no explaining. The takeoff mark, the strafe
  meter and CGaz all have to be read before they help, so they are things you turn on, and Options is
  where you find them.

  The reset row is not just a convenience. The rerelease writes these cvars to `system.cfg` whether
  or not you have ever touched one, so a changed default can only ever reach a *fresh* install — if
  you have played an earlier build, your old settings are pinned and no future default will reach
  you. This is how you catch up without editing that file by hand.

- **Options in the menu now covers every readout**, with rows for the **Takeoff Mark** and **CGaz**
  alongside the ones that were already there, grouped in pairs: the speedometer with the mark above
  it, the strafe meter with CGaz. Nothing on screen is now reachable only by typing a cvar.

  The two new rows work differently underneath, and the menu says so rather than hiding it. The
  takeoff mark is drawn by the status bar for everybody, so that row is a plain switch on any client.
  CGaz can only be drawn by the overlay, so on a stock client the row reads `CGaz: needs the DLL` and
  the cursor skips it instead of offering a switch that would do nothing. `takeoff` is also a console
  command now, for parity with `speedo` and `strafebar`.

- **The strafe meter is now off by default.** Turn it on with `strafebar`, from Options in the menu,
  or with `jump_hud_strafe 1`.

  It was defaulted on as the thing that would teach a new player what strafe jumping is, and it does
  not do that. It scores you against a total you cannot see, which changes every frame and shrinks
  as you improve — and below 300 ups it reads full whatever you do, because until your speed passes
  the target there is no wrong angle to find. So it tells a beginner they are perfect while they are
  doing nothing, then tells them they are failing the moment they start trying. Two experienced
  players read it as broken within an hour of picking it up.

  It is a good instrument once you can already strafe, and nothing about it has changed except who
  gets it unasked. The speedometer stays on for everybody: a number that goes up needs no explaining.

  Note that the rerelease archives these cvars even when you have never touched them, so the new
  default only reaches fresh installs — delete the `jump_hud_strafe` line from `system.cfg` to pick
  it up.

### Fixed

- **Dying on Practice puts you back on your last store**, rather than at the map spawn. Lava, a
  long fall and a crusher now cost the same as pressing `kill` — the time since the store — instead
  of the whole run. Practice exists to let you work on one jump; being sent back to the start for
  landing in the lava next to it was the one way left to lose a session's progress by accident.
  Unchanged without a store, on Ranked, and with `g_jump 0`.

- **`fasttele` now carries your view through the teleporter as well as your speed.** It used to
  keep the velocity but still snap you to face the destination's angle, which is the worst of the
  two halves: the velocity vector survives in world space, but the direction you were steering it
  does not, so a strafe chain dies on the far side of a teleport that exists precisely to let it
  continue. Nothing changes with `fasttele` off — there the freeze stops you anyway, so the snap
  costs nothing and still points you down the next corridor.

- **Speed-gated teleporters now work**, which some maps are built entirely around. A `speed` key on
  `trigger_teleport` or `misc_teleporter` is the horizontal speed you must be carrying before it
  fires; below that it does nothing and tells you what you need. Upstream q2jump has had this for
  years and the port was missing it.

  On `4kv3` the effect was total: its spawn point sits *inside* a teleport trigger covering the
  whole track, gated at 4000 ups, whose destination sits inside the finish. Without the gate the
  teleport fired on the first frame, so the map dropped you into a sealed room with a finished run
  before you had moved. Both touch paths are hooked — they are separate functions in the rerelease,
  the same split that once let `fasttele` work on `misc_teleporter` and not `trigger_teleport`.

  Thanks to Sata for the map and the report.

## [0.7.0] - 2026-08-08

### Added

- **The speedometer is drawn by the server too**, in ordinary small text rather than the HUD's
  chunky number pics — so every player has one, including anyone on a stock client, and it is a
  fraction of the size the pic font would be. `speedo` turns it off.

- **The strafe meter is now drawn by the server, so every player gets one** — including anyone on
  a stock, unmodified client, which is the whole point: it is the readout that teaches a new
  player what strafe jumping actually is, and hiding it behind a download served nobody.

  It shows as a short row of characters under a `strafe` label, and fills as you do well — a
  full bar means you are taking everything that was on offer. `strafebar` turns it off.

  Players running the mod's own DLL get the finer client-side version instead — their client tells
  the server to stop drawing its own, so nobody ends up with two.

- **An Options screen in the menu**, directly under How to Play, for the settings that change what
  you see and nothing anyone else does: **Speedometer**, **Strafe Meter** and **Hide Players**. Rows
  stay open and relabel as you pick them.

  A row means the readout, not one of the two copies of it — **On** gives you the best version you
  can get, the finer overlay one if you are running the mod's own DLL and the status bar one if you
  are not, and **Off** means gone everywhere. So there is finally one place to change this that does
  not depend on knowing whether you installed anything. **Centred** on the strafe row is DLL-only,
  since the status bar cannot anchor a bar in the middle.

### Changed

- **`jump_hud_speed` and `jump_hud_strafe` now default to `1`, and `0` means "no readout at all".**
  Since the server draws both readouts for every player, these no longer decide *whether* you have
  a speedometer and a strafe meter — only which half draws them. So the defaults put nothing new on
  your screen, and `0` now does what you would expect it to: your client asks the server to keep
  its copy hidden as well, so the readout is gone everywhere. That setting sticks, including across
  a map change, and it beats `jump_hud 0`.

  Previously `jump_hud_speed 0` meant only "don't draw it in the overlay" and actively told the
  server to draw its own, so there was no way for a player with the DLL to turn a readout off and
  have it stay off. If you had explicitly set either cvar to `0`, note that it now means off rather
  than "let the status bar draw it".

- **`speedo` and `strafebar` now say where the setting lives** if you are running the mod's own DLL,
  rather than setting a flag your client would immediately overrule. Without the DLL they work
  exactly as before. Either way, Options in the menu changes the right thing.

### Fixed

- **`jump_hud 0` gives the stock-client view again.** It was leaving the server's speedometer and
  strafe bar suppressed for the rest of the level, so turning the overlay off with a readout
  enabled left you with nothing at all on screen.

- **The two versions of each readout now sit in exactly the same place.** The status bar drew its
  speedometer and strafe bar a block higher than the overlay drew its own, so the reading jumped up
  the screen as it changed hands. Both halves now share one pair of anchors, and the overlay
  matches the baseline nudge the layout applies to text, so switching between them moves nothing.

## [0.5.0] - 2026-08-07

### Added

- **A speedometer**, centred just above the bottom of the screen, in units per second — where
  you can read it mid-jump without looking away from the map. It measures horizontal speed only,
  the way both upstream jump mods measure it and the way maps' own speed gates measure it, so it
  tracks the strafing that actually earns distance instead of spiking every time you jump or
  fall. Following another player shows their speed; free-flying spectators see nothing. It hides
  itself at rest, and carries no caption — both upstream mods label theirs "Speed", which at
  rerelease resolutions is a word you read once and never again.

  The reading is replaced forty times a second by default, matching the server frame rate. Both
  upstream mods ran on a 10 Hz server, so theirs changed ten times a second by construction, and
  four digits are much easier to read at that rate: **`jump_hud_speed_hz 10`** gives you it.
  That is a refresh rate rather than smoothing, so the number is always exact, just sampled less
  often.

  **It is drawn by this DLL's own client half, so it needs the DLL installed.** That is a
  deliberate split: the server's status bar carries everything needed to *play*, so a stock
  client can race with no download, and this DLL carries everything about playing *better*. Most
  of that could not be server-side at any price — a strafe meter needs your movement keys, a key
  display your buttons, a per-jump readout the exact frame you left the ground. The speedometer
  was the one piece that could have gone either way, and keeping it with the rest is what stops
  the performance HUD being half in one place and half in the other.

- **A strafe meter**, a small bar under the speedometer showing how much of the acceleration that
  was available to you on each frame your input actually took. This is the thing the speed number
  cannot tell you — it says where you ended up, not whether the input that got you there was any
  good. At 400 ups the difference between a 43° strafe and a 45° one is the difference between
  taking everything on offer and taking none of it.

  Two forms: a plain 0-100% bar by default, or `jump_hud_strafe 2` for a centre-anchored one that
  fills outward from the middle, so the side tells you which way to correct as well as how much
  you are losing.

  It is derived from the game's own acceleration maths rather than estimated, including the rule
  that pmove divides your view pitch by three before working out which way you are trying to go —
  so looking down really does change your strafe angle. It follows the server's
  `sv_airaccelerate`: on the default the best angle opens up as you get faster, around 43° at 400
  ups, while a non-zero value pins it near 90° and makes framerate matter. It grades your input,
  not your outcome, so clipping a wall costs you speed the bar will not blame you for. Air only,
  smoothed over about a second, and `jump_hud_strafe 0` turns it off.

- **The finish message now tells you how the run went against your best** — `Finished in 12.345
  (-1.234)`. It used to be a separate figure drawn on the side, and only for players running this
  DLL; putting it in the message gives it to everyone, and means it leaves the screen when the
  message does rather than needing its own rule for when to disappear. It is also now correct on
  the run that matters most: the old one compared against the best *including* the run just
  finished, so a new personal best read as no improvement at all. Practice runs get one too.

- **The client half now samples your movement every rendered frame**, out of the game's own
  prediction, which is what makes the readouts above finer-grained than anything the server
  could send — and it costs no network traffic at all. `jump_hud 0` turns the whole overlay off
  and gives you the exact view a stock client has.

- **The map's time remaining is now on the HUD**, at the foot of the right-hand column under
  your PB, instead of only on the scoreboard. It updates itself on the client, so it costs
  nothing per frame, and it re-syncs immediately when a `timeextend` vote passes or the
  `timelimit` cvar is changed from the console. Nothing shows when there is no time limit.

- **`msets` lists the settings in force on the current map** — gravity, `fasttele`, which weapons
  are tools rather than the finish, and how many checkpoints are required. Typing `mset` prints
  the same list plus a note that settings come from the server console, since that is the command
  the other jump mods have and players expect. Both are read-only; setting stays on
  `sv jump_mset`, because client commands carry no privilege model and these settings decide
  whether a run counts.

### Fixed

- **`fasttele` did nothing on maps built with `trigger_teleport`.** Only `misc_teleporter`
  honoured the mset, so on the 39 corpus maps that use the trigger form — `ataraxia`, `mako2-4`,
  the `acejumps` and `stonerjumps` sets among them — you were still frozen for 160 ms on every
  teleport with `fasttele 1` set.

- **Mistyped mset values were silently accepted.** `sv jump_mset fasttele on` reported success and
  set it to *off*, because values went through `atoi`; `gravity abc` set gravity to zero the same
  way. Booleans now take `0`/`1`, `on`/`off`, `true`/`false` and `yes`/`no`, integers must be whole
  numbers, and anything else is rejected with the old value left in place. Keys are matched
  case-insensitively, so `sv jump_mset FastTele 1` works.

## [0.4.7] - 2026-08-05

### Fixed

- **Players were invisible to each other.** Joining Practice or Ranked left `SVF_NOCLIENT` set
  from the spectator state everyone is pinned to before they answer the join menu, so nobody's
  model was sent to anyone else. The `jumpers` toggle could not help — that is a separate gate.
  Models reappeared only after a death-and-respawn, which is the one route that clears the flag.
  The respawn teleport effect on joining a team was being swallowed by the same flag and now
  plays.

- **Following someone no longer ends the moment they change team.** Switching between Practice
  and Ranked left every spectator watching that player dumped back into free-fly, even though
  the player was still perfectly followable. Followers now stay attached through the switch;
  if the player goes to Spectator, followers advance to the next player instead, and only fall
  back to free-fly when nobody else is playing.

## [0.4.6] - 2026-08-04

### Added

- **A sound and a HUD banner when someone sets a personal best or a map record.** Both go to
  everyone on the server: `misc/secret.wav` and a 4-second banner for a PB,
  `ctf/flagcap.wav` and a 6-second banner for a record. A record also centre-prints to
  everyone except the finisher, who already has their own "Finished in…" message and only has
  one centre-print slot to lose.

  The banner is part of the status bar rather than an ordinary print because a print lands in
  the notify area, where the next line of chat scrolls it away. A record is always a personal
  best too, so the two never fire together — the record wins.

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

- `team` with no argument now says that the old q2jump names still work — `team easy` for
  Practice and `team hard` for Ranked — and the unknown-team error lists them too. The
  aliases have always been accepted; nothing on screen said so, so anyone arriving with the
  muscle memory had no way to find out other than trying it.

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

- **Being the first person to finish a map now counts as taking the record.** It previously
  announced nothing at all: the test asked for a *previous* record to beat, so setting the
  first time on a map was silently treated as an ordinary finish. Holding 1st place is the
  whole test now, which is safe because a time that does not improve on your own entry is
  never given a rank at all. This also restores the `has set a 1st place!` line in the same
  case.

- The `store` and `recall` refusal messages called the ranked team "Hard" and pointed at
  `team easy`, which are the upstream q2jump names. They now say Ranked and `team practice`,
  matching the rest of the mod.

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
