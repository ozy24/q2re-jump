#!/usr/bin/env python3
"""Static q2jump-corpus compatibility scanner for the q2rejump mod.

Reads every BSP in a map directory and answers, without launching an engine:

  * will the map load at all (header, lump bounds, engine limits)
  * does it have a finish path and checkpoints under this mod's entity contract
  * which of its entities the mod drops, and which it has never heard of

The vocabularies are *parsed out of the mod source* rather than hardcoded, so
this cannot drift from the DLL:

  src/jump/jump_ents.cpp   jump_spawns[]              recognised jump entities
  src/jump/jump_ents.cpp   jump_ignored_classnames[]  deliberately freed
  src/g_spawn.cpp          spawns[]                   stock Q2RE spawn table
  src/g_items.cpp          itemlist[]                 stock Q2RE items + flags

The structural checks mirror q2repro's own validator (src/common/bsp.c,
src/common/bsp_template.c) so that a static BROKEN verdict predicts the
engine's "Couldn't load %s: %s".
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

# ---------------------------------------------------------------------------
# Source-derived vocabularies
# ---------------------------------------------------------------------------

# g_spawn.cpp:468-473 rewrites these before the item lookup, so they resolve to
# real items even though the names are not in itemlist.
PMM_CLASSNAME_ALIASES = {
    "weapon_nailgun": "weapon_etf_rifle",
    "ammo_nails": "ammo_flechettes",
    "weapon_heatbeam": "weapon_plasmabeam",
}


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _brace_block(text: str, start_pat: str, what: str) -> str:
    """Return the text between the { } that follows a declaration match."""
    m = re.search(start_pat, text)
    if not m:
        raise SystemExit(f"could not locate {what} (pattern {start_pat!r})")
    open_at = text.index("{", m.end() - 1 if text[m.end() - 1] == "{" else m.end())
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : i]
    raise SystemExit(f"unterminated block for {what}")


class Vocab:
    """Classname vocabularies and item flags, parsed from the mod source."""

    def __init__(self, src: Path):
        ents = _read(src / "jump" / "jump_ents.cpp")
        spawn = _read(src / "g_spawn.cpp")
        items = _read(src / "g_items.cpp")

        block = _brace_block(ents, r"jump_spawns\[\]\s*=\s*", "jump_spawns[]")
        self.jump_spawns = [m.group(1) for m in re.finditer(r'\{\s*"([^"]+)"', block)]

        block = _brace_block(
            ents, r"jump_ignored_classnames\[\]\s*=\s*", "jump_ignored_classnames[]"
        )
        self.jump_ignored = re.findall(r'"([^"]+)"', block)

        block = _brace_block(
            spawn, r"initializer_list<spawn_t>\s*spawns\s*=\s*", "spawns[]"
        )
        self.q2_spawns = [m.group(1) for m in re.finditer(r'\{\s*"([^"]+)"', block)]

        # itemlist entries are regular: /* id */ IT_X, /* classname */ "name",
        # ... /* flags */ IF_A | IF_B,
        self.item_flags: dict[str, set[str]] = {}
        self.item_ids: dict[str, str] = {}
        body = _brace_block(items, r"gitem_t\s+itemlist\[\]\s*=\s*", "itemlist[]")
        for chunk in body.split("/* id */")[1:]:
            cls = re.search(r'/\* classname \*/\s*"([^"]+)"', chunk)
            if not cls:
                continue  # items with no world classname (techs handled below)
            iid = re.match(r"\s*(IT_\w+)", chunk)
            flags = re.search(r"/\* flags \*/\s*([^\n]*)", chunk)
            names = set()
            if flags:
                names = {t.strip().rstrip(",") for t in flags.group(1).split("|")}
                names = {n for n in names if n.startswith("IF_")}
            self.item_flags[cls.group(1)] = names
            if iid:
                self.item_ids[cls.group(1)] = iid.group(1)

        for alias, target in PMM_CLASSNAME_ALIASES.items():
            if target in self.item_flags:
                self.item_flags[alias] = self.item_flags[target]
                self.item_ids[alias] = self.item_ids.get(target, "")

        self.jump_spawn_set = set(self.jump_spawns)
        self.jump_ignored_set = set(self.jump_ignored)
        self.q2_spawn_set = set(self.q2_spawns)

        if len(self.jump_spawns) < 5 or len(self.q2_spawns) < 50 or len(self.item_flags) < 30:
            raise SystemExit(
                "vocabulary extraction looks wrong: "
                f"{len(self.jump_spawns)} jump spawns, {len(self.q2_spawns)} q2 spawns, "
                f"{len(self.item_flags)} items"
            )

    def disposition(self, classname: str) -> str:
        """What ED_CallSpawn will do with this classname (g_spawn.cpp:447-522)."""
        if classname in self.item_flags:
            return "q2_item"
        if classname in self.q2_spawn_set:
            return "q2_spawn"
        if classname in self.jump_spawn_set:
            return "jump"
        if classname in self.jump_ignored_set:
            return "jump_ignored"
        return "unknown"


# ---------------------------------------------------------------------------
# BSP structure
# ---------------------------------------------------------------------------

IDBSPHEADER = b"IBSP"
IDBSPHEADER_EXT = b"QBSP"
BSPVERSION = 38

Q1_BSP_VERSIONS = {29, 30}  # raw leading int, no ident string

MAX_MAP_AREAS = 256
MAX_MAP_CLUSTERS = 65536
MAX_MODELS = 8192  # inline models must be <= MAX_MODELS - 2

CONTENTS_SOLID = 1

# (lump index, name, classic disk size, extended disk size, validated by the
# dedicated server).
#
# Copied from q2repro's bsp_lumps[] in src/common/bsp.c, *including its order* --
# the engine reports the first lump that fails, so matching the order makes the
# reason strings line up too. The six render lumps sit behind USE_REF, which the
# dedicated build compiles out; they are still checked here, because a client
# would reject a map the dedicated server accepts.
LUMPS = [
    (3, "Visibility", 1, 1, True),
    (5, "Texinfo", 76, 76, True),
    (1, "Planes", 20, 20, True),
    (15, "BrushSides", 4, 8, True),
    (14, "Brushes", 12, 12, True),
    (10, "LeafBrushes", 2, 4, True),
    (18, "AreaPortals", 8, 8, True),
    (17, "Areas", 8, 8, True),
    (7, "Lightmap", 1, 1, False),
    (2, "Vertices", 12, 12, False),
    (11, "Edges", 4, 8, False),
    (12, "SurfEdges", 4, 4, False),
    (6, "Faces", 20, 28, False),
    (9, "LeafFaces", 2, 4, False),
    (8, "Leafs", 28, 52, True),
    (4, "Nodes", 28, 44, True),
    (13, "SubModels", 48, 48, True),
    (0, "EntString", 1, 1, True),
    # 16 = Pop, not read by q2repro at all
]


class BspError(Exception):
    """A map that cannot load, carrying the engine's own wording."""

    def __init__(self, flag: str, message: str, server_fatal: bool = True):
        super().__init__(message)
        self.flag = flag
        self.message = message
        self.server_fatal = server_fatal


class Bsp:
    def __init__(self, path: Path):
        self.path = path
        self.size = path.stat().st_size
        self.ident = ""
        self.version = 0
        self.extended = False
        self.counts: dict[str, int] = {}
        self.warnings: list[str] = []
        self.entities = ""
        self.textures: list[str] = []
        self.numclusters = 0

        data = path.read_bytes()
        self.sha1 = hashlib.sha1(data).hexdigest()
        self._parse(data)

    def _parse(self, data: bytes) -> None:
        if len(data) < 8:
            raise BspError("truncated", "File too small")

        ident = data[0:4]
        version = struct.unpack_from("<i", data, 4)[0]

        if ident == IDBSPHEADER:
            self.ident = "IBSP"
        elif ident == IDBSPHEADER_EXT:
            self.ident = "QBSP"
            self.extended = True
        else:
            raw = struct.unpack_from("<i", data, 0)[0]
            if raw in Q1_BSP_VERSIONS:
                raise BspError("q1_bsp", f"Unknown file format (Quake 1 BSP v{raw})")
            raise BspError("bad_ident", "Unknown file format")

        self.version = version
        if version != BSPVERSION:
            raise BspError("bad_version", f"Unknown file format (version {version})")

        if len(data) < 4 + 4 + 19 * 8:
            raise BspError("truncated", "File too small")

        ext = 1 if self.extended else 0
        offsets: dict[str, int] = {}
        for index, name, classic, extended_size, server in LUMPS:
            ofs, length = struct.unpack_from("<ii", data, 8 + index * 8)
            if ofs < 0 or length < 0:
                raise BspError("lump_bounds", f"{name} lump out of bounds", server)
            if ofs + length > len(data):
                if index == 0 and ofs < len(data):
                    # EntString workaround for eg ztnmap1 (bsp.c:879-881)
                    self.warnings.append("entstring_lump_fixed")
                    length = len(data) - ofs
                else:
                    raise BspError("lump_bounds", f"{name} lump out of bounds", server)
            disk = extended_size if ext else classic
            if length % disk:
                raise BspError("lump_odd_size", f"{name} lump has odd size", server)
            offsets[name] = ofs
            self.counts[name] = length // disk

        self._check_limits()
        self._read_visibility(data, offsets["Visibility"])
        self._read_leaf0(data, offsets["Leafs"])
        self._read_model0(data, offsets["SubModels"])
        self.entities = data[
            offsets["EntString"] : offsets["EntString"] + self.counts["EntString"]
        ].decode("latin-1")
        self._read_textures(data, offsets["Texinfo"])

    def _check_limits(self) -> None:
        if self.counts["SubModels"] == 0:
            raise BspError("no_models", "Map with no models")
        if self.counts["SubModels"] > MAX_MODELS - 2:
            raise BspError("too_many_models", "Too many models")
        if self.counts["Leafs"] == 0:
            raise BspError("no_leafs", "Map with no leafs")
        if self.counts["Nodes"] == 0:
            raise BspError("no_nodes", "Map with no nodes")
        if self.counts["Areas"] > MAX_MAP_AREAS:
            raise BspError("too_many_areas", "Too many areas")

    def _read_visibility(self, data: bytes, ofs: int) -> None:
        count = self.counts["Visibility"]
        if count == 0:
            self.warnings.append("no_visibility")
            return
        if count < 4:
            raise BspError("vis_header", "Too small header")
        self.numclusters = struct.unpack_from("<I", data, ofs)[0]
        if self.numclusters > MAX_MAP_CLUSTERS:
            raise BspError("too_many_clusters", "Too many clusters")
        if count < 4 + self.numclusters * 8:
            raise BspError("vis_header", "Too small header")

    def _read_leaf0(self, data: bytes, ofs: int) -> None:
        if self.extended:
            return  # different layout; no QBSP maps in this corpus
        contents = struct.unpack_from("<i", data, ofs)[0]
        if contents != CONTENTS_SOLID:
            raise BspError("leaf0_not_solid", "Map leaf 0 is not CONTENTS_SOLID")

    def _read_model0(self, data: bytes, ofs: int) -> None:
        headnode = struct.unpack_from("<i", data, ofs + 36)[0]
        if headnode != 0:
            raise BspError(
                "model0_headnode", "Map model 0 headnode is not the first node"
            )

    def _read_textures(self, data: bytes, ofs: int) -> None:
        seen: set[str] = set()
        for i in range(self.counts["Texinfo"]):
            raw = data[ofs + i * 76 + 40 : ofs + i * 76 + 72]
            name = raw.split(b"\x00", 1)[0].decode("latin-1").strip().lower()
            if name and name not in seen:
                seen.add(name)
                self.textures.append(name)


# ---------------------------------------------------------------------------
# Entity lump
# ---------------------------------------------------------------------------

SEPARATORS = " \t\r\n"


def _com_tokens(text: str):
    """Yield (kind, value) the way COM_ParseEx does (src/q_std.cpp).

    Separators are whitespace only, "// comments" are skipped, and quoted
    strings come back whole. Matching this exactly is what lets the scanner
    predict "ED_LoadFromFile: found X when expecting {".
    """
    i, n = 0, len(text)
    while i < n:
        while i < n and text[i] in SEPARATORS:
            i += 1
        if i >= n:
            return
        if text[i] == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text[i] == '"':
            i += 1
            start = i
            while i < n and text[i] != '"':
                i += 1
            yield "str", text[start:i]
            i += 1
            continue
        start = i
        while i < n and text[i] not in SEPARATORS:
            i += 1
        yield "bare", text[start:i]


def parse_entities(text: str) -> tuple[list[dict[str, str]], list[str]]:
    """Tokenise a Q2 entity lump into dicts, mirroring ED_LoadFromFile.

    The engine only checks the *first character* of the token against '{' and
    '}' (src/g_spawn.cpp:1213, ED_ParseEdict), so this does too. Anything else
    at the top level is a hard Com_Error, which aborts the whole map load --
    reported as "malformed_entity_string".
    """
    blocks: list[dict[str, str]] = []
    problems: list[str] = []
    # COM_ParseEx returns end-of-data on a NUL byte, so the engine never sees
    # past the lump's terminator (nor any padding after it).
    tokens = _com_tokens(text.split("\x00", 1)[0])

    while True:
        tok = next(tokens, None)
        if tok is None:
            break
        kind, value = tok
        if kind != "bare" or not value.startswith("{"):
            problems.append("malformed_entity_string")
            break  # the engine errors out here; nothing after it is parsed

        current: dict[str, str] = {}
        key: str | None = None
        while True:
            nxt = next(tokens, None)
            if nxt is None:
                problems.append("unterminated_block")
                break
            k, v = nxt
            if k == "bare" and v.startswith("}"):
                break
            if key is None:
                key = v
            else:
                current.setdefault(key.strip().lower(), v)
                key = None
        if key is not None:
            problems.append("dangling_key")
        blocks.append(current)

    return blocks, problems


# ---------------------------------------------------------------------------
# Compatibility rules
# ---------------------------------------------------------------------------

MSET_WEAPON_GATES = {
    "rocket": "weapon_rocketlauncher",
    "grenadelauncher": "weapon_grenadelauncher",
    "hyperblaster": "weapon_hyperblaster",
    "bfg": "weapon_bfg",
}

LAP_CLASSNAMES = {"trigger_lapcounter", "trigger_lapcp"}
CP_CLEAR_CLASSNAMES = {
    "cp_clear",
    "trigger_single_cp_clear",
    "trigger_quad",
    "trigger_quad_clear",
}
CPBOX_CLASSNAMES = {"cpbox_small", "cpbox_medium", "cpbox_large"}
JUMPBOX_CLASSNAMES = {"jumpbox_small", "jumpbox_medium", "jumpbox_large"}
CP_BARRIER_CLASSNAMES = {"jump_cpwall", "jump_cpbrush"}
SPAWN_POINT_CLASSNAMES = {
    "info_player_deathmatch",
    "info_player_start",
    "info_player_coop",
    "info_player_team1",
    "info_player_team2",
}

# Positive evidence the map was built for a different gametype/mod.
OTHER_GAMETYPE_PREFIXES = ("item_pball", "weapon_pball", "monster_")
OTHER_GAMETYPE_CLASSNAMES = {
    "item_flag_team1",
    "item_flag_team2",
    "flag",
    "base",
    "trigger_ctf_teleport",
    "info_ctf_teleport_destination",
    "dm_tag_token",
    "dm_dball_ball",
    "dm_dball_goal",
}

TIER_BROKEN = "BROKEN"
TIER_NOT_JUMP = "NOT_JUMP"
TIER_PLAYABLE = "PLAYABLE"
TIER_DEGRADED = "DEGRADED"
TIER_UNFINISHABLE = "UNFINISHABLE"


def worldspawn_msets(blocks: list[dict[str, str]]) -> dict[str, str]:
    """Space-separated key/value pairs from worldspawn's "mset" key.

    Mirrors Jump_ApplyMsetPairs (src/jump/jump_mset.cpp:63-93), including its
    odd-token-count bail-out.
    """
    if not blocks:
        return {}
    text = blocks[0].get("mset", "")
    if not text:
        return {}
    tokens = text.split()
    if len(tokens) % 2:
        return {"__odd_tokens__": "1"}
    return {tokens[i].lower(): tokens[i + 1] for i in range(0, len(tokens), 2)}


def classify(bsp: Bsp, vocab: Vocab) -> dict:
    blocks, ent_problems = parse_entities(bsp.entities)

    if "malformed_entity_string" in ent_problems:
        # gi.Com_ErrorFmt at src/g_spawn.cpp:1213 -- ERR_DROP, so the map load
        # is abandoned entirely. The engine survives but the map never spawns.
        return {
            "row": {
                "tier": TIER_BROKEN,
                "flags": ";".join(dict.fromkeys(bsp.warnings + ent_problems)),
                "jump_evidence": "",
                "error": "Game Error: ED_LoadFromFile: found bad token when expecting {",
                "entities": len(blocks),
                "classname_kinds": 0,
            },
            "classnames": Counter(),
            "unknown": Counter(),
        }

    # G_Spawn defaults classname to "noclass" (src/g_utils.cpp:325), so a block
    # with no classname key still spawns -- as "noclass", which has no spawn
    # function and is freed with a console line. Count it the way the engine does.
    #
    # The first block is the exception: ED_LoadFromFile uses `ent = g_edicts` for
    # it rather than G_Spawn (src/g_spawn.cpp:1215-1218), so a missing classname
    # there stays null and takes the "ED_CallSpawn: nullptr classname" path
    # instead of becoming "noclass".
    classnames = Counter(
        (b.get("classname") or ("" if i == 0 else "noclass"))
        for i, b in enumerate(blocks)
    )
    world_unnamed = classnames.pop("", 0)

    flags: list[str] = list(bsp.warnings) + ent_problems
    row: dict = {
        "entities": len(blocks),
        "classname_kinds": len(classnames),
    }

    msets = worldspawn_msets(blocks)
    if "__odd_tokens__" in msets:
        flags.append("mset_odd_tokens")
        msets = {}
    usable_weapons = {
        cls for key, cls in MSET_WEAPON_GATES.items() if msets.get(key, "0") != "0"
    }
    if msets:
        flags.append("has_worldspawn_mset")

    by_disposition = Counter()
    unknown: Counter = Counter()
    for cls, n in classnames.items():
        d = vocab.disposition(cls)
        by_disposition[d] += n
        if d == "unknown":
            unknown[cls] += n

    # --- finish path (Jump_ItemTouch, src/jump/jump_finish.cpp:187-214) ------
    finish_triggers = classnames["trigger_finish"] + classnames["weapon_finish"]
    weapon_items = {
        cls: n
        for cls, n in classnames.items()
        if "IF_WEAPON" in vocab.item_flags.get(cls, set())
    }
    finish_weapons = {c: n for c, n in weapon_items.items() if c not in usable_weapons}

    # --- checkpoints ---------------------------------------------------------
    key_items = {
        cls: n
        for cls, n in classnames.items()
        if "IF_KEY" in vocab.item_flags.get(cls, set())
    }
    cp_clip = sum(
        1
        for b in blocks
        if b.get("classname") == "jump_clip"
        and b.get("message", "").lower() == "checkpoint"
    )
    checkpoints = (
        sum(classnames[c] for c in CPBOX_CLASSNAMES) + sum(key_items.values()) + cp_clip
    )

    # --- gaps (docs/JUMP_MOD.md:148-165) ------------------------------------
    lap = sum(classnames[c] for c in LAP_CLASSNAMES)
    cp_clear = sum(classnames[c] for c in CP_CLEAR_CLASSNAMES)
    cp_barrier = sum(classnames[c] for c in CP_BARRIER_CLASSNAMES)
    push_cp = sum(
        1
        for b in blocks
        if b.get("classname") == "trigger_push"
        and (
            b.get("target", "").lower().startswith("checkpoint")
            or b.get("targetname", "").lower().startswith("checkpoint")
        )
    )
    hurt_dmg1 = sum(
        1
        for b in blocks
        if b.get("classname") == "trigger_hurt" and b.get("dmg", "").strip() == "1"
    )
    # Some map series end at an exit brush rather than on a pickup. Jump_LevelExit
    # (src/jump/jump_finish.cpp) treats a player-activated exit as a finish, so a
    # target_changelevel entity IS a finish path.
    #
    # A bare worldspawn `nextmap` key is not: it is consumed by EndDMLevel on the
    # timelimit, never by a player, so it ends nothing. Count the two separately
    # or 97 maps look like they have an exit when they do not.
    changelevel_ents = sum(n for c, n in classnames.items() if c.endswith("changelevel"))
    nextmap_key = 1 if blocks and blocks[0].get("nextmap") else 0

    has_finish = bool(finish_triggers) or bool(finish_weapons) or bool(changelevel_ents)

    jumpboxes = sum(classnames[c] for c in JUMPBOX_CLASSNAMES)
    jump_clips = classnames["jump_clip"]

    jump_ents = sum(
        n
        for c, n in classnames.items()
        if c in vocab.jump_spawn_set or c in vocab.jump_ignored_set
    )
    monsters = sum(n for c, n in classnames.items() if c.startswith("monster_"))
    other_gametype = sum(
        n
        for c, n in classnames.items()
        if c in OTHER_GAMETYPE_CLASSNAMES or c.startswith(OTHER_GAMETYPE_PREFIXES)
    )
    spawn_points = sum(classnames[c] for c in SPAWN_POINT_CLASSNAMES)

    # --- verdict ------------------------------------------------------------
    if lap:
        flags.append("lap_map")
    if cp_clear:
        flags.append("cp_clear_ignored")
    if push_cp:
        flags.append("trigger_push_checkpoint")
    if changelevel_ents and not (finish_triggers or finish_weapons):
        flags.append("finish_via_exit")
    if nextmap_key and not has_finish:
        flags.append("nextmap_but_no_exit")
    if cp_barrier and not checkpoints:
        flags.append("cp_barrier_without_checkpoints")
    if hurt_dmg1:
        flags.append("trigger_hurt_dmg1")
    if jumpboxes:
        flags.append("jumpbox_models_missing")
    if jump_clips:
        flags.append("jump_clip_prediction")
    if unknown:
        flags.append("unknown_classnames")
    if by_disposition["jump_ignored"]:
        flags.append("legacy_ents_dropped")
    if usable_weapons:
        flags.append("mset_usable_weapons")
    if world_unnamed:
        flags.append("worldspawn_no_classname")

    # --- was this map built as a jump map? -----------------------------------
    # Only evidence, never a verdict: the corpus turns out to use almost no
    # jump-specific classnames, so telling a classic jump map apart from a
    # stock DM map comes down to circumstantial signals. Report them and let
    # the reader filter.
    evidence: list[str] = []
    if classnames["weapon_finish"] or classnames["trigger_finish"]:
        evidence.append("finish_ent")
    if msets:
        evidence.append("mset")
    if jump_ents:
        evidence.append("jump_ent")
    if key_items:
        evidence.append("key_checkpoints")
    if sum(finish_weapons.values()) == 1 and monsters == 0:
        evidence.append("single_weapon")
    if classnames["misc_teleporter"] >= 5:
        evidence.append("teleporter_heavy")
    if monsters:
        evidence.append("has_monsters")
    if other_gametype:
        evidence.append("other_gametype")
    if sum(weapon_items.values()) >= 5 and monsters == 0:
        evidence.append("dm_weapon_spread")

    tier: str
    if spawn_points == 0:
        flags.append("no_spawn_point")
        tier = TIER_BROKEN
    elif lap or cp_clear:
        # Still unimplemented, and still zero maps corpus-wide. trigger_push
        # barriers used to sit here too; Jump_PushBarrier now handles them, and a
        # barrier only gates progress rather than preventing completion.
        tier = TIER_UNFINISHABLE
    elif not has_finish:
        # A missing finish means different things depending on what the map is.
        # Positive evidence of another gametype (paintball guns, CTF flags,
        # monsters) says "wrong mod", not "broken jump map".
        if jump_ents == 0 and other_gametype:
            flags.append("no_finish_path")
            tier = TIER_NOT_JUMP
        else:
            flags.append("no_finish_path")
            flags.append("no_exit_at_all")
            tier = TIER_UNFINISHABLE
    elif jump_ents == 0 and (monsters or other_gametype):
        tier = TIER_NOT_JUMP
    elif jumpboxes or unknown:
        # jump_clip is fully supported; its prediction artefact stays an
        # informational flag rather than a demotion.
        tier = TIER_DEGRADED
    else:
        tier = TIER_PLAYABLE

    row.update(
        {
            "tier": tier,
            "flags": ";".join(dict.fromkeys(flags)),
            "jump_evidence": ";".join(evidence),
            "jump_ents": jump_ents,
            "finish_triggers": finish_triggers,
            "finish_weapons": sum(finish_weapons.values()),
            "checkpoints": checkpoints,
            "cp_barriers": cp_barrier,
            "key_items": sum(key_items.values()),
            "lap_ents": lap,
            "cp_clear_ents": cp_clear,
            "push_checkpoints": push_cp,
            "hurt_dmg1": hurt_dmg1,
            "changelevel": changelevel_ents,
            "nextmap_key": nextmap_key,
            "jumpboxes": jumpboxes,
            "jump_clips": jump_clips,
            "monsters": monsters,
            "other_gametype_ents": other_gametype,
            "spawn_points": spawn_points,
            "unknown_ents": sum(unknown.values()),
            "unknown_kinds": len(unknown),
            "unknown_top": ",".join(c for c, _ in unknown.most_common(6)),
            "legacy_dropped": by_disposition["jump_ignored"],
            "textures": len(bsp.textures),
        }
    )
    return {"row": row, "classnames": classnames, "unknown": unknown}


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

CSV_FIELDS = [
    "name",
    "file",
    "tier",
    "flags",
    "jump_evidence",
    "size",
    "sha1",
    "ident",
    "version",
    "error",
    "server_fatal",
    "entities",
    "classname_kinds",
    "jump_ents",
    "finish_triggers",
    "finish_weapons",
    "checkpoints",
    "cp_barriers",
    "key_items",
    "lap_ents",
    "cp_clear_ents",
    "push_checkpoints",
    "hurt_dmg1",
    "changelevel",
    "nextmap_key",
    "jumpboxes",
    "jump_clips",
    "monsters",
    "other_gametype_ents",
    "spawn_points",
    "unknown_ents",
    "unknown_kinds",
    "unknown_top",
    "legacy_dropped",
    "textures",
    "models",
    "leafs",
    "nodes",
    "brushes",
    "areas",
    "clusters",
]


def scan_one(path: Path, vocab: Vocab) -> tuple[dict, Counter, Counter]:
    row = {
        "name": path.stem.lower(),
        "file": path.name,
        "size": path.stat().st_size,
        "tier": "",
        "flags": "",
        "error": "",
        "server_fatal": "",
    }
    try:
        bsp = Bsp(path)
    except BspError as e:
        row.update(
            {
                "tier": TIER_BROKEN,
                "flags": e.flag,
                "error": e.message,
                "server_fatal": "1" if e.server_fatal else "0",
                "sha1": hashlib.sha1(path.read_bytes()).hexdigest(),
            }
        )
        return row, Counter(), Counter()
    except (struct.error, OSError) as e:
        row.update({"tier": TIER_BROKEN, "flags": "parse_error", "error": str(e)})
        return row, Counter(), Counter()

    result = classify(bsp, vocab)
    row.update(result["row"])
    row.update(
        {
            "sha1": bsp.sha1,
            "ident": bsp.ident,
            "version": bsp.version,
            "server_fatal": "",
            "models": bsp.counts["SubModels"],
            "leafs": bsp.counts["Leafs"],
            "nodes": bsp.counts["Nodes"],
            "brushes": bsp.counts["Brushes"],
            "areas": bsp.counts["Areas"],
            "clusters": bsp.numclusters,
        }
    )
    return row, result["classnames"], result["unknown"]


def write_summary(out: Path, rows: list[dict], classnames: Counter, maps_per: Counter,
                  vocab: Vocab) -> None:
    tiers = Counter(r["tier"] for r in rows)
    flags = Counter()
    for r in rows:
        for f in (r["flags"] or "").split(";"):
            if f:
                flags[f] += 1

    dupes = {}
    for r in rows:
        dupes.setdefault(r.get("sha1", ""), []).append(r["file"])
    dupe_groups = {k: v for k, v in dupes.items() if k and len(v) > 1}

    lines = [
        "# Static scan summary",
        "",
        f"Scanned **{len(rows)}** files.",
        "",
        "## Verdict tiers",
        "",
        "| tier | maps |",
        "|---|---|",
    ]
    for tier in (TIER_PLAYABLE, TIER_DEGRADED, TIER_UNFINISHABLE, TIER_NOT_JUMP, TIER_BROKEN):
        lines.append(f"| `{tier}` | {tiers.get(tier, 0)} |")

    lines += ["", "## Flags (maps affected)", "", "| flag | maps |", "|---|---|"]
    for flag, n in flags.most_common():
        lines.append(f"| `{flag}` | {n} |")

    lines += [
        "",
        "## Load failures",
        "",
        "| reason | maps |",
        "|---|---|",
    ]
    reasons = Counter(r["error"] for r in rows if r["tier"] == TIER_BROKEN and r["error"])
    for reason, n in reasons.most_common():
        lines.append(f"| {reason} | {n} |")

    lines += [
        "",
        "## Duplicate content",
        "",
        f"{len(dupe_groups)} sha1 groups cover more than one filename.",
        "",
    ]
    for sha, names in sorted(dupe_groups.items(), key=lambda kv: -len(kv[1]))[:15]:
        lines.append(f"- `{sha[:12]}` ({len(names)}): {', '.join(sorted(names)[:8])}")

    lines += [
        "",
        "## Vocabulary in use",
        "",
        f"- {len(vocab.jump_spawns)} recognised jump classnames: "
        + ", ".join(f"`{c}`" for c in vocab.jump_spawns),
        f"- {len(vocab.jump_ignored)} deliberately dropped: "
        + ", ".join(f"`{c}`" for c in vocab.jump_ignored),
        f"- {len(vocab.q2_spawns)} stock Q2RE spawn functions, "
        f"{len(vocab.item_flags)} stock items",
        "",
        "## Top unknown classnames (would print \"doesn't have a spawn function\")",
        "",
        "| classname | entities | maps |",
        "|---|---|---|",
    ]
    unknown_total = Counter()
    unknown_maps = Counter()
    for cls, n in classnames.items():
        if vocab.disposition(cls) == "unknown":
            unknown_total[cls] = n
            unknown_maps[cls] = maps_per[cls]
    for cls, n in unknown_total.most_common(40):
        lines.append(f"| `{cls}` | {n} | {unknown_maps[cls]} |")

    (out / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent
    repo = here.parent.parent

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--maps-dir",
        type=Path,
        default=Path(r"E:\code\projects\q2re-jump\q2jump-maps"),
        help="directory of .bsp files to scan",
    )
    ap.add_argument("--src", type=Path, default=repo / "src", help="mod source root")
    ap.add_argument("--out", type=Path, default=here / "out", help="output directory")
    ap.add_argument(
        "--ext",
        default=".bsp",
        help="comma-separated extensions to scan (default .bsp; corpus also "
        "has .tmp and .bsp_old strays)",
    )
    ap.add_argument("--limit", type=int, default=0, help="stop after N files")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    vocab = Vocab(args.src)
    exts = {e if e.startswith(".") else "." + e for e in args.ext.split(",")}
    files = sorted(
        p for p in args.maps_dir.iterdir() if p.is_file() and p.suffix.lower() in exts
    )
    if args.limit:
        files = files[: args.limit]
    if not files:
        raise SystemExit(f"no files matching {sorted(exts)} in {args.maps_dir}")

    args.out.mkdir(parents=True, exist_ok=True)

    rows: list[dict] = []
    corpus_classnames = Counter()
    maps_per_classname = Counter()
    for i, path in enumerate(files, 1):
        row, classnames, _unknown = scan_one(path, vocab)
        rows.append(row)
        corpus_classnames.update(classnames)
        maps_per_classname.update(classnames.keys())
        if not args.quiet and (i % 250 == 0 or i == len(files)):
            print(f"  {i}/{len(files)}", file=sys.stderr, flush=True)

    with (args.out / "maps.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=CSV_FIELDS, extrasaction="ignore")
        w.writeheader()
        for r in sorted(rows, key=lambda r: r["name"]):
            w.writerow(r)

    (args.out / "maps.json").write_text(
        json.dumps(sorted(rows, key=lambda r: r["name"]), indent=1), encoding="utf-8"
    )

    with (args.out / "classnames.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["classname", "disposition", "entities", "maps"])
        for cls, n in corpus_classnames.most_common():
            w.writerow([cls, vocab.disposition(cls), n, maps_per_classname[cls]])

    write_summary(args.out, rows, corpus_classnames, maps_per_classname, vocab)

    tiers = Counter(r["tier"] for r in rows)
    print(f"\n{len(rows)} files scanned -> {args.out}")
    for tier in (TIER_PLAYABLE, TIER_DEGRADED, TIER_UNFINISHABLE, TIER_NOT_JUMP, TIER_BROKEN):
        print(f"  {tier:<13} {tiers.get(tier, 0)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
