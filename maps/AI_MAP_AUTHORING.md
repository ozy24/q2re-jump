# Building a Quake II map from an agent session

How `jumptest1` was made, written so another agent can repeat the process
rather than rediscover it. The short version: **generate the `.map` with a
script, compile with ericw-tools, and verify the compiled BSP** — never
hand-write brush planes and never assume a texture or entity exists.

Everything below was actually done; the gotchas are ones that bit during the
build, not hypotheticals.

---

## 1. Find the toolchain before designing anything

You need two things, and neither ships with this repo:

| Need | What satisfied it here |
|---|---|
| A Quake II BSP compiler | `ericw-tools` — `qbsp.exe`, `vis.exe`, `light.exe` |
| Quake II `.wal` textures | a directory containing `textures/e1u1/...` etc. |

Both were found in a sibling project rather than installed:

```
E:\code\projects\q2-relighter\tools\ericw-tools\      qbsp, vis, light, bsputil
E:\code\projects\q2-relighter\gamedata\baseq2\        2730 .wal files
```

Search sibling repos before concluding you can't build a map. Confirm the
compiler targets Quake II specifically — `qbsp` defaults to Quake 1 BSP:

```bash
qbsp.exe 2>&1 | grep -i q2bsp     # look for "-q2bsp  target Quake II's BSP format"
```

If no compiler exists, stop and say so. Emitting `.map` source the user can't
compile is not a deliverable.

## 2. Verify every texture name you intend to use

A missing texture is a compile-time failure or an invisible surface. Check
first — this is cheap:

```bash
for t in e1u1/metal1_1 e1u1/floor3_1 e1u1/c_met5_2 e1u1/brlava; do
  test -f "$GAMEDATA/textures/$t.wal" && echo "OK $t" || echo "MISSING $t"
done
```

`e1u1/lava1` did not exist; `e1u1/brlava` did. Guessing would have cost a
compile cycle.

## 3. Check the engine's behaviour against the game source, not memory

The map has to work under *this* mod and *this* engine. Before placing an
entity, confirm it survives:

- **Do key items spawn in deathmatch?** Quake II strips a lot of items in DM.
  Reading `SpawnItem` in `src/g_items.cpp` showed keys are only removed by
  `g_no_items` and some coop cases — so key-as-checkpoint works. Had that been
  wrong, the checkpoint design would have silently never spawned.
- **Is there a spawn-point fallback?** `p_client.cpp` falls back to
  `info_player_start` when a map has no `info_player_deathmatch`. Placing both
  costs nothing and removes a failure mode.
- **Player bounding box** is `(-16,-16,-24)` to `(16,16,32)`. The *origin* must
  sit at least 24 units above the floor or the player spawns inside it. Entity
  origins were placed ~40 above the surface and left to drop.

## 4. Generate the `.map`, don't write it

A Quake II brush is six planes, each defined by three points, and **the winding
order decides which way the face points**. Getting one backwards produces an
inside-out brush that compiles cleanly and leaks. Do not hand-derive twelve
brushes' worth of planes.

`make_jumptest.py` emits axis-aligned boxes and checks each face:

```python
# qbsp derives the normal as (p0 - p1) x (p2 - p1), and expects it to point
# OUT of the brush.
normal = cross(sub(p0, p1), sub(p2, p1))
if dot(normal, desired_outward) < 0:
    p0, p2 = p2, p0        # reverse the winding
```

That check is the single most valuable line in the generator. Write the box
helper once, then describe the level as data:

```python
PLATFORMS = [          # (x_start, x_end, top_z)
    (64,   256,  128),  # start
    (352,  480,  128),  # gap 96
    ...
]
```

### Quake II `.map` syntax differs from Quake 1

Each face line carries **three extra integers** after the texture axes —
`contents`, `flags`, `value`:

```
( x1 y1 z1 ) ( x2 y2 z2 ) ( x3 y3 z3 ) TEXTURE 0 0 0 1 1 <contents> <flags> <value>
```

That is how lava is made lava. There is no other mechanism:

```python
CONTENTS_LAVA = 8
SURF_LIGHT    = 0x1
SURF_WARP     = 0x8
# lava brush: contents=8, flags=WARP|LIGHT, value=150 (light brightness)
```

A brush with `CONTENTS_LAVA` is not solid, so players fall in — which is what
makes it a hazard rather than a wall.

## 5. Compile: qbsp → vis → light

```bash
TOOLS=".../ericw-tools"
GAMEDATA=".../gamedata/baseq2"

"$TOOLS/qbsp.exe"  -q2bsp -path "$GAMEDATA" jumptest1.map jumptest1.bsp
"$TOOLS/vis.exe"   jumptest1.bsp
"$TOOLS/light.exe" -path "$GAMEDATA" -extra4 -bounce 8 jumptest1.bsp
```

`-path` points at the directory *containing* `textures/`. Skipping `light`
leaves the map fully black; skipping `vis` works but renders everything.

Read qbsp's output. It prints the lump table — `0 brushes` or a suspiciously
small face count means the geometry didn't survive.

## 6. Verify the compiled BSP, don't trust the source

Entities can be dropped between `.map` and `.bsp`. Extract and check:

```bash
bsputil.exe --extract-entities jumptest1.bsp    # writes jumptest1.ent
grep -E 'classname|origin' jumptest1.ent
```

Confirm every entity you placed is present — spawn points, the finish item, the
checkpoint, the lights, and any custom keys such as `mset`. This caught nothing
here, which is precisely why it is worth doing: it converts "probably fine" into
"verified".

## 7. Make it reproducible, then install

Commit the generator and a build script, not the BSP. `build_jumptest.bat`
regenerates, compiles and installs in one step, with the tool and game paths
overridable by environment variable. Add the compiled artifacts to
`.gitignore`:

```
maps/*.bsp
maps/*.ent
maps/*.prt
maps/*.json     # qbsp also writes .content.json / .texinfo.json
```

Install to the directory the game actually loads maps from — for the Quake II
remaster that is the user-data folder, not the install folder:

```
%USERPROFILE%\OneDrive\Saved Games\Nightdive Studios\Quake II\baseq2\maps\
```

## 8. Design for the thing you're testing

`jumptest1` exists to exercise a mod, not to be a good map. That shaped it:

- **Gaps of 96–144 units.** A running jump covers roughly 200 (jump velocity
  270, gravity 800 → ~0.675 s airborne, ~300 ups horizontal). Deliberately
  generous, so a failed jump means a physics finding rather than player skill.
- **One of each feature**, not many: one checkpoint, one finish, one hazard.
  Enough to prove each code path, few enough to diagnose.
- **A lava floor**, so a missed jump has a consequence — which is what makes
  practice-vs-ranked behaviour observable at all.
- **A no-op `mset` in worldspawn** (`gravity 800 checkpoint_total 1`) that
  agrees with what the mod would infer anyway. It exercises the config parser
  without changing behaviour, so a parser bug shows up as a log error rather
  than as broken gameplay.

## What this process does not give you

Be honest about this in the handover:

- **It does not prove the map is completable.** Nothing here runs the game. The
  jump distances are arithmetic, not measurement.
- **It does not validate visual quality** — lighting, texture alignment and
  scale are unverified.
- **Custom models are a separate problem.** `jumpbox_*` / `cpbox_*` reference
  `models/jump/*box3`, which ship with jump map packs. Not needed here, so the
  map avoids them entirely.

State plainly which claims are verified (it compiles, the entities are in the
BSP, it installs) and which are not (it plays well, the jumps are makeable).
