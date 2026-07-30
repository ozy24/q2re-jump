#!/usr/bin/env python3
"""Parse the raw q2reproded output from run_engine_scan.ps1 into engine.csv.

Each map's output is delimited by the "===MAPSCAN <name>===" echo markers the
driver emits. The strings recognised here are the engine's and the game DLL's
own, quoted from source so they can be re-checked:

  src/server/init.c:314    Couldn't load %s: %s          (BSP rejected)
  src/server/init.c:110    SpawnServer: %s               (load succeeded)
  src/common/common.c:587  ERROR: %s between **** rules  (ERR_DROP)
  src/g_spawn.cpp:520      <edict> doesn't have a spawn function
  src/g_spawn.cpp:901      <key> is not a valid field
  src/g_spawn.cpp:1247     %d entities inhibited
  src/jump/jump_ents.cpp:363  [jump] ignoring legacy entity <name>

Note the "{}" in g_spawn.cpp:520 formats an edict_t (g_local.h:3536-3551), not a
string, so the line reads "<classname> @ <x> <y> <z> doesn't have ...".
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import Counter
from pathlib import Path

MARKER = re.compile(r"^===MAPSCAN (.+?)===\s*$", re.M)

RE_LOAD_FAIL = re.compile(r"^Couldn't load (\S+): (.+?)\s*$", re.M)
RE_SPAWNED = re.compile(r"^SpawnServer: (.+?)\s*$", re.M)
RE_ERROR = re.compile(r"^ERROR: (.+?)\s*$", re.M)
# Classnames can contain spaces ("Stair Maker", "flag 0"), so match up to the
# " @ <origin>" suffix rather than assuming the name is one word.
RE_NO_SPAWN_FN = re.compile(
    r"^(.+?) @ -?[\d.]+ -?[\d.]+ -?[\d.]+ doesn't have a spawn function\s*$", re.M
)
RE_NO_SPAWN_FN_LOOSE = re.compile(r"^(.+?) doesn't have a spawn function\s*$", re.M)
RE_BAD_FIELD = re.compile(r"^(\S+) is not a valid field\s*$", re.M)
RE_INHIBITED = re.compile(r"^(\d+) entities inhibited\s*$", re.M)
RE_LEGACY = re.compile(r"^\[jump\] ignoring legacy entity (\S+)\s*$", re.M)
RE_JUMP_MSG = re.compile(r"^\[jump\] (.+?)\s*$", re.M)
RE_NO_VIS = re.compile(r"^Map has no Visibility", re.M)
RE_BAD_MATERIAL = re.compile(r'^Bad material "([^"]*)"', re.M)
RE_LUMP_FIX = re.compile(r"^(\S+) lump out of bounds, fixing", re.M)

# [jump] lines that are ordinary progress, not signal
JUMP_NOISE = re.compile(
    r"^(data directory|level init|loaded \d+ record|mset |unknown mset|"
    r"ignoring legacy entity|loaded \d+ map)"
)

FIELDS = [
    "name",
    "status",
    "load_error",
    "err_drop",
    "unknown_ents",
    "unknown_kinds",
    "unknown_top",
    "invalid_keys",
    "invalid_key_kinds",
    "invalid_key_top",
    "inhibited",
    "legacy_dropped",
    "legacy_top",
    "no_visibility",
    "entstring_fixed",
    "bad_materials",
    "jump_warnings",
]


def parse_segment(name: str, text: str) -> dict:
    row = {f: "" for f in FIELDS}
    row["name"] = name

    fail = RE_LOAD_FAIL.search(text)
    spawned = RE_SPAWNED.search(text)
    err = RE_ERROR.search(text)

    if fail:
        row["status"] = "LOAD_FAILED"
        row["load_error"] = fail.group(2)
    elif err:
        # An ERR_DROP can arrive after "SpawnServer:" -- e.g. the game DLL
        # erroring inside SpawnEntities -- so the banner alone does not mean the
        # map came up. The error wins.
        row["status"] = "ERR_DROP"
        row["err_drop"] = err.group(1)
    elif spawned:
        row["status"] = "LOADED"
    else:
        row["status"] = "NO_OUTPUT"

    unknown = Counter(RE_NO_SPAWN_FN.findall(text))
    loose = Counter(RE_NO_SPAWN_FN_LOOSE.findall(text))
    if sum(loose.values()) > sum(unknown.values()):
        # Defensive: an entity printed without a parseable origin. Keep the
        # loose count so the total stays honest, trimming the " @ ..." suffix.
        unknown = Counter(
            {re.sub(r" @ .*$", "", c): n for c, n in loose.items()}
        )
    row["unknown_ents"] = sum(unknown.values())
    row["unknown_kinds"] = len(unknown)
    row["unknown_top"] = ",".join(c for c, _ in unknown.most_common(6))

    keys = Counter(RE_BAD_FIELD.findall(text))
    row["invalid_keys"] = sum(keys.values())
    row["invalid_key_kinds"] = len(keys)
    row["invalid_key_top"] = ",".join(k for k, _ in keys.most_common(6))

    inhib = RE_INHIBITED.search(text)
    row["inhibited"] = int(inhib.group(1)) if inhib else ""

    legacy = Counter(RE_LEGACY.findall(text))
    row["legacy_dropped"] = sum(legacy.values())
    row["legacy_top"] = ",".join(c for c, _ in legacy.most_common(6))

    row["no_visibility"] = 1 if RE_NO_VIS.search(text) else ""
    row["entstring_fixed"] = 1 if RE_LUMP_FIX.search(text) else ""
    row["bad_materials"] = len(set(RE_BAD_MATERIAL.findall(text))) or ""

    warnings = [
        m for m in RE_JUMP_MSG.findall(text) if not JUMP_NOISE.match(m)
    ]
    row["jump_warnings"] = ";".join(sorted(set(warnings)))[:300]
    return row


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=here / "out")
    ap.add_argument("--raw", type=Path, default=None, help="engine_raw directory")
    args = ap.parse_args(argv)

    raw_dir = args.raw or (args.out / "engine_raw")
    files = sorted(raw_dir.glob("*.txt"))
    if not files:
        raise SystemExit(f"no raw output in {raw_dir} -- run run_engine_scan.ps1 first")

    rows: dict[str, dict] = {}
    corpus_unknown = Counter()
    corpus_unknown_maps = Counter()
    corpus_keys = Counter()
    corpus_key_maps = Counter()

    for path in files:
        text = path.read_text(encoding="latin-1")
        marks = list(MARKER.finditer(text))
        for i, m in enumerate(marks):
            name = m.group(1)
            if name == "-DONE-" or name.startswith("-DONE"):
                continue
            end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
            segment = text[m.end() : end]
            row = parse_segment(name, segment)
            # A resumed chunk can revisit a name; last writer wins.
            rows[name.lower()] = row

            for cls, n in Counter(RE_NO_SPAWN_FN.findall(segment)).items():
                corpus_unknown[cls] += n
                corpus_unknown_maps[cls] += 1
            for key, n in Counter(RE_BAD_FIELD.findall(segment)).items():
                corpus_keys[key] += n
                corpus_key_maps[key] += 1

    # Maps the driver could not even attempt.
    for csv_name, status in (
        ("engine_skipped.csv", "SKIPPED"),
        ("engine_casualties.csv", "CRASHED"),
    ):
        p = args.out / csv_name
        if not p.exists():
            continue
        with p.open(newline="", encoding="utf-8-sig") as f:
            for r in csv.DictReader(f):
                name = (r.get("name") or "").lower()
                if not name:
                    continue
                existing = rows.get(name)
                if status == "CRASHED" and existing and existing.get("err_drop"):
                    # The driver only knows the chunk stopped here. If the map
                    # left a diagnosed ERR_DROP behind, the server survived and
                    # merely flushed the command buffer -- not a crash.
                    continue
                if status == "CRASHED" or existing is None:
                    row = existing or {f: "" for f in FIELDS}
                    row["name"] = r.get("name")
                    row["status"] = status
                    row["load_error"] = r.get("reason", "") or row.get("load_error", "")
                    rows[name] = row

    out_csv = args.out / "engine.csv"
    with out_csv.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, extrasaction="ignore")
        w.writeheader()
        for name in sorted(rows):
            w.writerow(rows[name])

    with (args.out / "engine_unknown_ents.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["classname", "entities", "maps"])
        for cls, n in corpus_unknown.most_common():
            w.writerow([cls, n, corpus_unknown_maps[cls]])

    with (args.out / "engine_invalid_keys.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["key", "occurrences", "maps"])
        for key, n in corpus_keys.most_common():
            w.writerow([key, n, corpus_key_maps[key]])

    status = Counter(r["status"] for r in rows.values())
    print(f"{len(rows)} maps parsed from {len(files)} chunk file(s) -> {out_csv}")
    for s, n in status.most_common():
        print(f"  {s:<12} {n}")
    print(f"\n{len(corpus_unknown)} distinct unknown classnames, "
          f"{len(corpus_keys)} distinct invalid keys")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
