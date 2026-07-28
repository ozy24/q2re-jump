#!/usr/bin/env python3
"""Generate jumptest1.map - a straightforward linear race for testing the port.

Emits Quake II .map source. Compile with build_jumptest.bat, which drives
ericw-tools (qbsp -q2bsp, vis, light).

The course is a line of platforms over a lava floor. Every gap is comfortably
inside a standard Quake II running jump, because the point is to exercise the
mod rather than the player: timer start, a checkpoint that gates the finish,
lava as the fail condition, and a railgun as the finish line.

Falling means dying, which is what makes Easy and Hard feel different: Easy can
store on each platform and recall after a miss, Hard restarts from the spawn.
"""

import os

# Quake II brush contents / surface flags.
CONTENTS_LAVA = 8
SURF_LIGHT = 0x1
SURF_WARP = 0x8

WALL_TEX = "e1u1/metal1_1"
FLOOR_TEX = "e1u1/floor3_1"
PLATFORM_TEX = "e1u1/c_met5_2"
LAVA_TEX = "e1u1/brlava"

# Room interior.
ROOM = dict(x1=0, x2=2560, y1=-384, y2=384, z1=0, z2=640)
WALL = 32

# (x_start, x_end, top_z). Gaps run 96 -> 144 units; a running jump covers
# roughly 200, so the course is forgiving on purpose.
PLATFORMS = [
    (64, 256, 128),      # start, deep enough to build speed
    (352, 480, 128),     # gap  96
    (592, 720, 128),     # gap 112
    (832, 960, 160),     # gap 112, step up
    (1072, 1264, 160),   # gap 112, holds the checkpoint
    (1376, 1504, 192),   # gap 112, step up
    (1632, 1760, 192),   # gap 128
    (1888, 2016, 224),   # gap 128, step up
    (2160, 2432, 224),   # gap 144, holds the finish
]

PLATFORM_Y1, PLATFORM_Y2 = -96, 96
PLATFORM_THICKNESS = 32


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def box_brush(mins, maxs, tex, contents=0, flags=0, value=0):
    """One axis-aligned box brush.

    qbsp derives the face normal as (p0 - p1) x (p2 - p1) and expects it to
    point out of the brush, so each face is checked and its winding reversed
    when it comes out inverted. Deriving all six by hand is exactly the kind of
    thing that produces a subtly inside-out brush.
    """
    x1, y1, z1 = mins
    x2, y2, z2 = maxs

    faces = [
        ((0, 0, 1), [(x1, y1, z2), (x2, y1, z2), (x2, y2, z2)]),
        ((0, 0, -1), [(x1, y1, z1), (x2, y1, z1), (x2, y2, z1)]),
        ((-1, 0, 0), [(x1, y1, z1), (x1, y1, z2), (x1, y2, z2)]),
        ((1, 0, 0), [(x2, y1, z1), (x2, y1, z2), (x2, y2, z2)]),
        ((0, -1, 0), [(x1, y1, z1), (x2, y1, z1), (x2, y1, z2)]),
        ((0, 1, 0), [(x1, y2, z1), (x2, y2, z1), (x2, y2, z2)]),
    ]

    lines = ["{"]

    for want, (p0, p1, p2) in faces:
        normal = cross(sub(p0, p1), sub(p2, p1))

        if dot(normal, want) < 0:
            p0, p2 = p2, p0

        pts = " ".join("( %d %d %d )" % p for p in (p0, p1, p2))
        lines.append("%s %s 0 0 0 1 1 %d %d %d" % (pts, tex, contents, flags, value))

    lines.append("}")
    return "\n".join(lines)


def entity(classname, **keys):
    lines = ["{", '"classname" "%s"' % classname]

    for key, value in keys.items():
        lines.append('"%s" "%s"' % (key, value))

    lines.append("}")
    return "\n".join(lines)


def build():
    r = ROOM
    brushes = []

    # Room shell.
    brushes.append(box_brush((r["x1"] - WALL, r["y1"] - WALL, r["z1"] - WALL),
                             (r["x2"] + WALL, r["y2"] + WALL, r["z1"]), FLOOR_TEX))
    brushes.append(box_brush((r["x1"] - WALL, r["y1"] - WALL, r["z2"]),
                             (r["x2"] + WALL, r["y2"] + WALL, r["z2"] + WALL), WALL_TEX))
    brushes.append(box_brush((r["x1"] - WALL, r["y1"] - WALL, r["z1"]),
                             (r["x1"], r["y2"] + WALL, r["z2"]), WALL_TEX))
    brushes.append(box_brush((r["x2"], r["y1"] - WALL, r["z1"]),
                             (r["x2"] + WALL, r["y2"] + WALL, r["z2"]), WALL_TEX))
    brushes.append(box_brush((r["x1"] - WALL, r["y1"] - WALL, r["z1"]),
                             (r["x2"] + WALL, r["y1"], r["z2"]), WALL_TEX))
    brushes.append(box_brush((r["x1"] - WALL, r["y2"], r["z1"]),
                             (r["x2"] + WALL, r["y2"] + WALL, r["z2"]), WALL_TEX))

    # Lava covering the floor: this is the fail condition for a missed jump.
    brushes.append(box_brush((r["x1"], r["y1"], r["z1"]), (r["x2"], r["y2"], 48),
                             LAVA_TEX, contents=CONTENTS_LAVA,
                             flags=SURF_WARP | SURF_LIGHT, value=150))

    for x_start, x_end, top in PLATFORMS:
        brushes.append(box_brush((x_start, PLATFORM_Y1, top - PLATFORM_THICKNESS),
                                 (x_end, PLATFORM_Y2, top), PLATFORM_TEX))

    start_x = (PLATFORMS[0][0] + PLATFORMS[0][1]) // 2
    start_top = PLATFORMS[0][2]

    cp_x = (PLATFORMS[4][0] + PLATFORMS[4][1]) // 2
    cp_top = PLATFORMS[4][2]

    finish_x = PLATFORMS[-1][0] + 160
    finish_top = PLATFORMS[-1][2]

    entities = []

    # "mset" is a no-op here - one checkpoint entity is counted automatically -
    # but stating it exercises the worldspawn mset parser on a known-good value.
    entities.append(entity(
        "worldspawn",
        message="Jump Test 1",
        mset="gravity 800 checkpoint_total 1",
    ))

    # Player bbox reaches 24 units below the origin, so spawn clear of the floor.
    entities.append(entity("info_player_deathmatch",
                           origin="%d 0 %d" % (start_x, start_top + 40), angle=0))
    entities.append(entity("info_player_start",
                           origin="%d 0 %d" % (start_x, start_top + 40), angle=0))

    # Checkpoint: a key item, which the mod treats as a checkpoint. The finish
    # is refused until it has been collected.
    entities.append(entity("key_data_cd", origin="%d 0 %d" % (cp_x, cp_top + 40)))

    # Finish line: touching any weapon ends the run.
    entities.append(entity("weapon_railgun", origin="%d 0 %d" % (finish_x, finish_top + 40)))

    for x in range(128, r["x2"], 384):
        entities.append(entity("light", origin="%d 0 560" % x, light=350))

    parts = [entities[0].replace("\n}", "\n" + "\n".join(brushes) + "\n}")]
    parts.extend(entities[1:])

    return "\n".join(parts) + "\n"


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "jumptest1.map")

    with open(out, "w") as f:
        f.write(build())

    print("wrote %s" % out)
