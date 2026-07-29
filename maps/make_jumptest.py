#!/usr/bin/env python3
"""Generate the jumptest series - Quake II .map source for testing the jump mod.

    python make_jumptest.py            # all maps
    python make_jumptest.py jumptest4  # just one

Each map targets a different feature of the mod, so a failure points at one
code path rather than a vague "something is broken". See MAPS below.

Compile with build_jumptest.bat, which drives ericw-tools. The authoring
process, and the reasoning behind the pieces that are easy to get wrong, is in
AI_MAP_AUTHORING.md - read that before changing the brush emitter.
"""

import os
import sys

# --- Quake II brush contents / surface flags -------------------------------
# These are the three trailing integers on every face line. There is no other
# way to declare lava, water or a ladder.
CONTENTS_SOLID = 1
CONTENTS_LAVA = 8
CONTENTS_WATER = 32
CONTENTS_PLAYERCLIP = 0x10000
CONTENTS_LADDER = 1 << 29

SURF_LIGHT = 0x1
SURF_WARP = 0x8
SURF_TRANS66 = 0x20
SURF_NODRAW = 0x80

WALL = "e1u1/metal1_1"
FLOOR = "e1u1/floor3_1"
PLATFORM = "e1u1/c_met5_2"
LAVA = "e1u1/brlava"
WATER = "e1u1/water1_8"
GRATE = "e1u1/grate1_1"
CLIP = "e1u1/clip"
TRIGGER = "e1u1/trigger"

SHELL = 32          # room wall thickness
PLATFORM_H = 32     # platform slab thickness
STAND = 40          # entity origin height above a surface (player bbox is -24)


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def box_brush(mins, maxs, tex, contents=0, flags=0, value=0):
    """One axis-aligned box brush.

    qbsp derives each face normal as (p0 - p1) x (p2 - p1) and expects it to
    point OUT of the brush. Rather than hand-deriving six windings and hoping,
    every face is checked and reversed when it comes out inverted - a single
    inside-out face compiles cleanly and then leaks.
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
        if dot(cross(sub(p0, p1), sub(p2, p1)), want) < 0:
            p0, p2 = p2, p0

        pts = " ".join("( %d %d %d )" % p for p in (p0, p1, p2))
        lines.append("%s %s 0 0 0 1 1 %d %d %d" % (pts, tex, contents, flags, value))

    lines.append("}")
    return "\n".join(lines)


class MapBuilder:
    """Collects world brushes and entities, then emits .map source."""

    def __init__(self, name, message, **worldspawn):
        self.name = name
        self.world = []      # brush strings belonging to worldspawn
        self.entities = []   # (classname, keys, brushes)
        self.worldspawn = dict(message=message, **worldspawn)

    # --- world geometry ---
    def brush(self, mins, maxs, tex=WALL, contents=0, flags=0, value=0):
        self.world.append(box_brush(mins, maxs, tex, contents, flags, value))

    def room(self, mins, maxs, floor_tex=FLOOR, wall_tex=WALL):
        """A sealed box. Everything else must live inside it or the map leaks."""
        x1, y1, z1 = mins
        x2, y2, z2 = maxs
        s = SHELL

        self.brush((x1 - s, y1 - s, z1 - s), (x2 + s, y2 + s, z1), floor_tex)
        self.brush((x1 - s, y1 - s, z2), (x2 + s, y2 + s, z2 + s), wall_tex)
        self.brush((x1 - s, y1 - s, z1), (x1, y2 + s, z2), wall_tex)
        self.brush((x2, y1 - s, z1), (x2 + s, y2 + s, z2), wall_tex)
        self.brush((x1 - s, y1 - s, z1), (x2 + s, y1, z2), wall_tex)
        self.brush((x1 - s, y2, z1), (x2 + s, y2 + s, z2), wall_tex)

    def platform(self, x1, y1, x2, y2, top, tex=PLATFORM):
        self.brush((x1, y1, top - PLATFORM_H), (x2, y2, top), tex)

    def lava(self, mins, maxs, depth=48):
        x1, y1, z1 = mins
        x2, y2, _ = maxs
        self.brush((x1, y1, z1), (x2, y2, z1 + depth), LAVA,
                   CONTENTS_LAVA, SURF_WARP | SURF_LIGHT, 150)

    def water(self, mins, maxs):
        self.brush(mins, maxs, WATER, CONTENTS_WATER, SURF_WARP | SURF_TRANS66)

    def ladder(self, mins, maxs):
        # Non-solid, but pmove traces for CONTENTS_LADDER to allow climbing.
        # Needs a solid wall behind it to press against.
        self.brush(mins, maxs, GRATE, CONTENTS_LADDER)

    # --- entities ---
    def entity(self, classname, brushes=None, **keys):
        self.entities.append((classname, keys, brushes or []))

    def point(self, classname, origin, **keys):
        self.entity(classname, origin="%d %d %d" % origin, **keys)

    def brush_entity(self, classname, mins, maxs, tex=TRIGGER, **keys):
        self.entity(classname, brushes=[box_brush(mins, maxs, tex)], **keys)

    def spawn(self, origin, angle=0):
        self.point("info_player_deathmatch", origin, angle=angle)
        self.point("info_player_start", origin, angle=angle)

    def lights(self, x1, x2, y, z, step=384, value=350):
        for x in range(x1, x2, step):
            self.point("light", (x, y, z), light=value)

    # --- output ---
    def render(self):
        blocks = []

        head = ["{", '"classname" "worldspawn"']
        for k, v in self.worldspawn.items():
            head.append('"%s" "%s"' % (k, v))
        head.extend(self.world)
        head.append("}")
        blocks.append("\n".join(head))

        for classname, keys, brushes in self.entities:
            lines = ["{", '"classname" "%s"' % classname]
            for k, v in keys.items():
                lines.append('"%s" "%s"' % (k, v))
            lines.extend(brushes)
            lines.append("}")
            blocks.append("\n".join(lines))

        return "\n".join(blocks) + "\n"


# ---------------------------------------------------------------------------
# The maps
#
# Gaps are kept inside a standard running jump (~200 units at 300 ups) unless a
# map exists specifically to test something harder. The point is to exercise
# the mod, not the player.
# ---------------------------------------------------------------------------

def jumptest1():
    """Baseline: linear platforms over lava, one checkpoint, weapon finish."""
    m = MapBuilder("jumptest1", "Jump Test 1 - the basics",
                   mset="gravity 800 checkpoint_total 1")
    m.room((0, -384, 0), (2560, 384, 640))
    m.lava((0, -384, 0), (2560, 384, 0))

    plats = [(64, 256, 128), (352, 480, 128), (592, 720, 128), (832, 960, 160),
             (1072, 1264, 160), (1376, 1504, 192), (1632, 1760, 192),
             (1888, 2016, 224), (2160, 2432, 224)]

    for x1, x2, top in plats:
        m.platform(x1, -96, x2, 96, top)

    m.spawn((160, 0, plats[0][2] + STAND))
    m.point("key_data_cd", (1168, 0, plats[4][2] + STAND))
    m.point("weapon_railgun", (2320, 0, plats[-1][2] + STAND))
    m.lights(128, 2560, 0, 560)
    return m


def jumptest2():
    """Ascent: a vertical shaft climbed in low gravity. Tests the gravity mset."""
    m = MapBuilder("jumptest2", "Jump Test 2 - ascent (low gravity)",
                   mset="gravity 450")
    # The shaft has to be tall enough for the summit plus headroom - a
    # platform poking through the ceiling is a leak, and vis fails on it.
    m.room((0, -320, 0), (1024, 320, 1920))
    m.lava((0, -320, 0), (1024, 320, 0))

    m.platform(64, -128, 320, 128, 128)      # start
    m.spawn((190, 0, 128 + STAND))

    # Alternate sides going up. 224 of rise per step is only reachable with
    # the reduced gravity, which is the point of the map.
    top = 128
    left = False
    for i in range(6):
        top += 224
        if left:
            m.platform(64, -128, 384, 128, top)
        else:
            m.platform(640, -128, 960, 128, top)
        left = not left

    m.platform(64, -192, 960, 192, top + 224)   # summit spans the shaft
    m.point("weapon_railgun", (512, 0, top + 224 + STAND))

    for z in range(256, 1920, 384):
        m.point("light", (512, 0, z), light=300)
    return m


def jumptest3():
    """Precision: small pads, no margin for error. Tests store and recall."""
    m = MapBuilder("jumptest3", "Jump Test 3 - precision pads",
                   mset="gravity 800")
    m.room((0, -256, 0), (2304, 256, 512))
    m.lava((0, -256, 0), (2304, 256, 0))

    m.platform(64, -96, 256, 96, 128)
    m.spawn((160, 0, 128 + STAND))

    # 48-unit pads with a slight lateral weave, so each jump needs aiming as
    # well as distance.
    x = 384
    y = 0
    for i in range(10):
        m.platform(x, y - 24, x + 48, y + 24, 128)
        x += 152
        y = 96 if y <= 0 else -96

    m.platform(x, -128, x + 224, 128, 128)
    m.point("weapon_railgun", (x + 112, 0, 128 + STAND))
    m.lights(128, 2304, 0, 448)
    return m


def jumptest4():
    """Checkpoints: five to collect, and a barrier that will not open without
    them. Tests checkpoint counting and jump_cpwall."""
    m = MapBuilder("jumptest4", "Jump Test 4 - checkpoint gauntlet",
                   mset="gravity 800 checkpoint_total 5")
    m.room((0, -384, 0), (2816, 384, 640))
    m.lava((0, -384, 0), (2816, 384, 0))

    m.platform(64, -128, 320, 128, 128)
    m.spawn((190, 0, 128 + STAND))

    keys = ["key_blue_key", "key_red_key", "key_data_spinner",
            "key_pyramid", "key_power_cube"]

    x = 448
    for i, key in enumerate(keys):
        m.platform(x, -112, x + 224, 112, 128)
        m.point(key, (x + 112, 0, 128 + STAND))
        x += 352

    # The barrier sits in the corridor to the finish. count = checkpoints
    # required; without them it shoves you back.
    m.platform(x, -160, 2752, 160, 128)
    m.brush_entity("jump_cpwall", (x + 32, -160, 128), (x + 48, 160, 384), count=5)

    m.point("weapon_railgun", (2680, 0, 128 + STAND))
    m.lights(128, 2816, 0, 560)
    return m


def jumptest5():
    """Teleports: four chambers chained together, with the teleport freeze
    disabled. Tests the fasttele mset."""
    m = MapBuilder("jumptest5", "Jump Test 5 - teleport chain",
                   mset="gravity 800 fasttele 1")
    m.room((0, -320, 0), (2560, 320, 512))
    m.lava((0, -320, 0), (2560, 320, 0))

    # Four separate ledges; the only way between them is a teleporter, so a
    # broken teleport strands you rather than failing quietly.
    for i in range(4):
        x = 64 + i * 640
        m.platform(x, -160, x + 512, 160, 128)

    m.spawn((160, 0, 128 + STAND))

    for i in range(3):
        x = 64 + i * 640
        m.point("misc_teleporter", (x + 448, 0, 128 + 16), target="tp%d" % i)
        m.point("misc_teleporter_dest", (x + 640 + 96, 0, 128 + STAND),
                targetname="tp%d" % i, angle=0)

    # A short hop on the last ledge so the map is not purely teleports.
    m.platform(2112, -96, 2240, 96, 192)
    m.platform(2368, -96, 2496, 96, 256)
    m.point("weapon_railgun", (2432, 0, 256 + STAND))
    m.lights(128, 2560, 0, 448)
    return m


def jumptest6():
    """Water and ladders: swim, climb, then jump. Tests movement modes the
    other maps never touch."""
    m = MapBuilder("jumptest6", "Jump Test 6 - water and ladders",
                   mset="gravity 800")
    m.room((0, -320, 0), (2048, 320, 768))

    m.platform(64, -160, 320, 160, 192)
    m.spawn((190, 0, 192 + STAND))

    # A pool to swim across. The floor of the room is the bottom of the pool.
    m.water((384, -320, 0), (1024, 320, 224))

    # Far wall of the pool with a ladder up it.
    m.brush((1024, -320, 0), (1088, 320, 576))
    m.ladder((1008, -64, 0), (1024, 64, 576))

    m.platform(1088, -160, 1344, 160, 576)
    m.platform(1472, -96, 1600, 96, 576)
    m.platform(1728, -160, 1984, 160, 576)

    m.point("weapon_railgun", (1856, 0, 576 + STAND))
    m.lights(128, 2048, 0, 700)
    return m


def jumptest7():
    """Rocket jump: the only way up is to blast yourself. Tests the rocket
    mset, trigger_weapon, and that knockback survives damage being zeroed."""
    m = MapBuilder("jumptest7", "Jump Test 7 - rocket jump",
                   mset="gravity 800 rocket 1")
    m.room((0, -320, 0), (1536, 320, 1024))
    m.lava((0, -320, 0), (1536, 320, 0))

    m.platform(64, -192, 576, 192, 128)
    m.spawn((160, 0, 128 + STAND))

    # Walking over this hands out a rocket launcher (count 7 = RL).
    m.brush_entity("trigger_weapon", (320, -192, 128), (448, 192, 256), count=7)

    # A ledge 320 above the floor and 384 away: out of reach by jumping,
    # comfortable with one rocket.
    m.platform(960, -192, 1472, 192, 448)
    m.point("weapon_railgun", (1216, 0, 448 + STAND))

    for z in (300, 800):
        for x in (256, 768, 1280):
            m.point("light", (x, 0, z), light=300)
    return m


def jumptest8():
    """Legacy entities: a brush finish, a one-way wall and an invisible clip.
    Tests the classic jump-map entities, none of which the other maps use."""
    m = MapBuilder("jumptest8", "Jump Test 8 - legacy entities",
                   mset="gravity 800 checkpoint_total 1")
    m.room((0, -384, 0), (2304, 384, 640))
    m.lava((0, -384, 0), (2304, 384, 0))

    m.platform(64, -160, 384, 160, 128)
    m.spawn((190, 0, 128 + STAND))

    # A one-way wall: passable heading right, solid coming back.
    m.brush_entity("one_way_wall", (448, -160, 128), (464, 160, 384), angle=0)

    m.platform(448, -160, 768, 160, 128)
    m.platform(896, -128, 1088, 128, 160)

    # An invisible wall sealing the direct route, so the checkpoint detour is
    # the only way on. If jump_clip is broken this shortcut opens up.
    m.brush_entity("jump_clip", (1216, -128, 160), (1248, 128, 448), CLIP)

    m.platform(1216, -320, 1408, -128, 160)      # detour, off to one side
    m.point("key_blue_key", (1312, -224, 160 + STAND))
    m.platform(1216, 128, 1408, 320, 160)

    m.platform(1536, -160, 1856, 160, 192)

    # The finish is a brush volume rather than a weapon pickup.
    m.brush_entity("trigger_finish", (1984, -160, 192), (2240, 160, 448))
    m.platform(1984, -160, 2240, 160, 192)

    m.lights(128, 2304, 0, 560)
    return m


MAPS = {
    "jumptest1": jumptest1,
    "jumptest2": jumptest2,
    "jumptest3": jumptest3,
    "jumptest4": jumptest4,
    "jumptest5": jumptest5,
    "jumptest6": jumptest6,
    "jumptest7": jumptest7,
    "jumptest8": jumptest8,
}


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    wanted = sys.argv[1:] or sorted(MAPS)

    for name in wanted:
        if name not in MAPS:
            sys.stderr.write("unknown map: %s\n" % name)
            sys.exit(1)

        out = os.path.join(here, name + ".map")
        with open(out, "w") as f:
            f.write(MAPS[name]().render())
        print("wrote %s" % out)
