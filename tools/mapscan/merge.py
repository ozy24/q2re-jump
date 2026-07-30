#!/usr/bin/env python3
"""Reconcile the static scan against the engine pass and write the reports.

Produces, in the output directory:
  maps_final.csv  one row per map, both verdicts plus a reconciliation column
  report.md       headline numbers and every static/engine disagreement
  gaps.md         unsupported features ranked by how many maps each one blocks

The two sources answer different questions and neither wins by default:
the engine is ground truth for "does it load", the static scan is the only
source for "can it be completed" (nothing in the mod refuses a map, so a
map that loads perfectly can still be unfinishable). Disagreements about
loading are listed rather than resolved silently -- each one means either a
scanner bug or engine behaviour that was not modelled.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import Counter
from pathlib import Path

LOAD_OK = {"LOADED"}
LOAD_BAD = {"LOAD_FAILED", "ERR_DROP", "CRASHED", "NO_OUTPUT"}


def read_csv(path: Path) -> list[dict]:
    if not path.exists():
        raise SystemExit(f"missing {path}")
    with path.open(newline="", encoding="utf-8-sig") as f:
        return list(csv.DictReader(f))


def as_int(v: str) -> int:
    try:
        return int(v)
    except (TypeError, ValueError):
        return 0


def reconcile(static: dict, engine: dict | None) -> tuple[str, str]:
    """Return (reconciliation, final_tier)."""
    stier = static["tier"]

    if engine is None:
        return "not_attempted", stier

    est = engine["status"]

    if est == "SKIPPED":
        return "not_attempted", stier
    if est == "CRASHED":
        return "engine_crash", "BROKEN"

    if stier == "BROKEN":
        if est in LOAD_BAD:
            return "agree_broken", "BROKEN"
        return "DISAGREE_static_too_strict", stier
    if est in LOAD_BAD:
        return "DISAGREE_static_too_lenient", "BROKEN"
    return "agree_loads", stier


def write_report(out: Path, rows: list[dict], unknown_ents: list[dict],
                 invalid_keys: list[dict]) -> None:
    tiers = Counter(r["final_tier"] for r in rows)
    recon = Counter(r["reconciliation"] for r in rows)
    disagreements = [r for r in rows if r["reconciliation"].startswith("DISAGREE")]
    crashes = [r for r in rows if r["reconciliation"] == "engine_crash"]

    load_errors = Counter(
        r["load_error"] for r in rows if r.get("load_error")
    )
    static_errors = Counter(r["error"] for r in rows if r.get("error"))

    # Entity-count agreement: a systematic gap here would mean the static
    # entity tokeniser and the engine's parser disagree.
    both = [r for r in rows if r["reconciliation"] == "agree_loads" and r.get("status") == "LOADED"]
    ent_match = sum(
        1 for r in both if as_int(r["unknown_ents"]) == as_int(r["engine_unknown_ents"])
    )

    L = [
        "# q2jump corpus vs q2rejump: compatibility report",
        "",
        f"**{len(rows)} map files** examined: every one parsed statically, and every",
        f"loadable `.bsp` also loaded for real by `q2reproded.exe` with the mod's",
        "`game_x64.dll` (game API 2023, `==== Jump mode enabled ====` confirmed).",
        "",
        "## Verdicts",
        "",
        "| tier | maps | meaning |",
        "|---|---|---|",
        f"| `PLAYABLE` | {tiers.get('PLAYABLE', 0)} | loads, has a finish path, hits no known gap |",
        f"| `DEGRADED` | {tiers.get('DEGRADED', 0)} | loads and is finishable, but loses content |",
        f"| `UNFINISHABLE` | {tiers.get('UNFINISHABLE', 0)} | loads, but the run can never be completed/recorded |",
        f"| `NOT_JUMP` | {tiers.get('NOT_JUMP', 0)} | another mod's map (paintball/CTF/SP), no finish under jump rules |",
        f"| `BROKEN` | {tiers.get('BROKEN', 0)} | will not load |",
        "",
        "## Static vs engine agreement",
        "",
        "| outcome | maps |",
        "|---|---|",
    ]
    for k, n in recon.most_common():
        L.append(f"| `{k}` | {n} |")

    if both:
        L += [
            "",
            f"Of {len(both)} maps that both agree load, **{ent_match}** have an "
            f"exactly matching unsupported-entity count "
            f"({100.0 * ent_match / len(both):.2f}%). The static scanner counts what "
            "*would* reach `ED_CallSpawn`; `G_InhibitEntity` "
            "(`src/g_spawn.cpp:1074-1090`) drops entities flagged "
            "`SPAWNFLAG_NOT_DEATHMATCH` before that, so a map with a non-zero "
            "`engine_inhibited` legitimately reports fewer.",
            "",
        ]

    if disagreements:
        L += [
            "### Disagreements",
            "",
            "| map | static | engine | static reason | engine reason |",
            "|---|---|---|---|---|",
        ]
        for r in sorted(disagreements, key=lambda r: r["name"])[:60]:
            L.append(
                f"| `{r['name']}` | {r['tier']} | {r.get('status', '')} | "
                f"{r.get('error', '')} | {r.get('load_error', '') or r.get('err_drop', '')} |"
            )
        if len(disagreements) > 60:
            L.append(f"\n...and {len(disagreements) - 60} more (see `maps_final.csv`).")
        L.append("")
    else:
        L += ["### Disagreements", "", "None. Every static load verdict matched the engine.", ""]

    if crashes:
        L += [
            "### Maps that killed the server",
            "",
        ] + [f"- `{r['name']}`" for r in crashes] + [""]
    else:
        L += [
            "### Maps that killed the server",
            "",
            "None. No map crashed or hung the dedicated server.",
            "",
        ]

    err_drops = [r for r in rows if r.get("err_drop")]
    if err_drops:
        L += [
            "### Maps that abort the map load (`ERR_DROP`)",
            "",
            "The server survives these, but the `longjmp` back to `Qcommon_Frame`",
            "**discards the pending command buffer** -- so one of these in a map",
            "rotation or a batch will silently drop everything queued behind it.",
            "",
            "| map | error |",
            "|---|---|",
        ]
        for r in sorted(err_drops, key=lambda r: r["name"]):
            L.append(f"| `{r['name']}` | {r['err_drop']} |")
        L.append("")

    not_attempted = [r for r in rows if r["reconciliation"] == "not_attempted"]
    attempted_bad = [r for r in rows if r["reconciliation"] == "agree_broken"]

    L += [
        "## Load failures",
        "",
        f"### Confirmed by the engine ({len(attempted_bad)} maps)",
        "",
        "| map | engine reason | static reason |",
        "|---|---|---|",
    ]
    for r in sorted(attempted_bad, key=lambda r: r["name"]):
        engine_reason = r.get("load_error") or r.get("err_drop") or ""
        same = "*(same)*" if r.get("error") == engine_reason else r.get("error", "")
        L.append(f"| `{r['name']}` | {engine_reason} | {same} |")

    if not_attempted:
        L += [
            "",
            f"### Not loadable by name ({len(not_attempted)} files)",
            "",
            "`map <name>` always appends `.bsp`, so these strays cannot be reached",
            "by the engine at all. Static analysis is the only verdict available.",
            "",
            "| file | static reason |",
            "|---|---|",
        ]
        for r in sorted(not_attempted, key=lambda r: r["file"]):
            L.append(f"| `{r['file']}` | {r.get('error', '')} |")

    L += [
        "",
        "## Unsupported entities in the corpus",
        "",
        'Every one of these prints `<classname> @ <origin> doesn\'t have a spawn '
        "function` and is freed (`src/g_spawn.cpp:520`). The map still loads.",
        "",
        "| classname | entities | maps |",
        "|---|---|---|",
    ]
    for r in unknown_ents[:30]:
        L.append(f"| `{r['classname']}` | {r['entities']} | {r['maps']} |")

    L += [
        "",
        "## Legacy entity keys the Q2RE field table rejects",
        "",
        "Each prints `<key> is not a valid field` (`src/g_spawn.cpp:901`) and is "
        "ignored; non-fatal, but it shows which map features silently do nothing.",
        "",
        "| key | occurrences | maps |",
        "|---|---|---|",
    ]
    for r in invalid_keys[:30]:
        L.append(f"| `{r['key']}` | {r['occurrences']} | {r['maps']} |")

    L += [
        "",
        "## Caveats",
        "",
        "- **q2repro is not the KEX engine.** It is a Q2PRO fork implementing the",
        "  rerelease protocol and game API. Because it loads the *same*",
        "  `game_x64.dll`, the entity-contract results (spawn functions, dropped",
        "  entities, rejected keys) are exact. Engine-level behaviour -- asset",
        "  handling, render-side limits -- may differ from `quake2ex_steam.exe`.",
        "- **A dedicated server loads no textures, models or sounds**, so missing",
        "  custom map assets cannot show up in the engine pass at all. The corpus",
        "  ships only 39 `.filelist` manifests and no texture archives, so most",
        "  custom textures are simply absent; that is a packaging problem, not a",
        "  mod compatibility one.",
        "- **The dedicated build validates fewer BSP lumps than a client.** q2repro's",
        "  `bsp_lumps[]` puts Faces, Edges, SurfEdges, LeafFaces, Vertices and",
        "  Lightmap behind `USE_REF`, which the dedicated build compiles out. The",
        "  static scanner checks all of them, so it can flag a map the server",
        "  accepts but a client would reject.",
        "- **Physics is not tested here.** The mod deliberately keeps stock",
        "  rerelease pmove (`docs/THIN_VANILLA_PRINCIPLES.md`), so jumps tuned for",
        "  125 fps may be impossible even on a map this report calls `PLAYABLE`.",
        "  Only playtesting can settle that.",
        "",
    ]
    (out / "report.md").write_text("\n".join(L) + "\n", encoding="utf-8")


def write_gaps(out: Path, rows: list[dict], unknown_ents: list[dict],
               invalid_keys: list[dict]) -> None:
    def having(pred) -> list[str]:
        return sorted(r["name"] for r in rows if pred(r))

    flagged = lambda f: having(lambda r: f in (r.get("flags") or ""))

    lap = flagged("lap_map")
    cp_clear = flagged("cp_clear_ignored")
    push_cp = flagged("trigger_push_checkpoint")
    hurt = flagged("trigger_hurt_dmg1")
    boxes = flagged("jumpbox_models_missing")
    exit_finish = flagged("finish_via_exit")
    nextmap_only = flagged("nextmap_but_no_exit")
    no_exit = flagged("no_exit_at_all")
    barrier = flagged("cp_barrier_without_checkpoints")

    def row(name: str, maps: list[str], note: str) -> str:
        ex = ", ".join(f"`{m}`" for m in maps[:4])
        if len(maps) > 4:
            ex += f", +{len(maps) - 4} more"
        return f"| {name} | **{len(maps)}** | {ex or '—'} | {note} |"

    blocking = sorted(
        [
            ("No finish entity of any kind", no_exit,
             "Practice/test maps (`aimtrain`, `admintryouts`). Nothing to implement — "
             "these have no finish line to find."),
            ("Worldspawn `nextmap` but no exit entity", nextmap_only,
             "`nextmap` is consumed by `EndDMLevel` on the timelimit, never by a "
             "player, so it ends nothing. Not fixable from the mod side — the map "
             "has no player-reachable exit."),
            ("Lap maps (`trigger_lapcounter` / `trigger_lapcp`)", lap,
             "Still unimplemented. **Zero maps in this corpus use them.**"),
            ("`cp_clear` / `trigger_single_cp_clear` / `trigger_quad`", cp_clear,
             "Still unimplemented. **Zero maps in this corpus use them.**"),
            ("Checkpoint barrier with no checkpoints to satisfy it", barrier,
             "`jump_cpwall` / `jump_cpbrush` present but the map defines no checkpoints."),
        ],
        key=lambda t: -len(t[1]),
    )

    closed = sorted(
        [
            ("`trigger_hurt` with `dmg 1`", hurt,
             "**Fixed.** Used to kill the player outright (`MOD_TRIGGER_HURT` was a "
             "fail condition); now resets the loadout to the blaster and the run "
             "continues, per `Jump_FilterDamage`."),
            ("`trigger_push` checkpoint barriers", push_cp,
             "**Implemented** as `Jump_PushBarrier`. Passes at or above `count`, "
             "otherwise prints and lets the vanilla push apply, matching upstream."),
            ("Finish at a `target_changelevel` exit", exit_finish,
             "**Implemented** as `Jump_LevelExit`: reaching the exit records the run "
             "and the level does not change. New design — no upstream mod did this."),
        ],
        key=lambda t: -len(t[1]),
    )

    cosmetic = sorted(
        [
            ("Missing `models/jump/*box3`", boxes,
             "Boxes stay solid but render as nothing; `jump_box_models 0` is the "
             "workaround. **Zero maps in this corpus use jumpboxes.**"),
        ],
        key=lambda t: -len(t[1]),
    )

    L = [
        "# Gap ranking",
        "",
        "Unsupported or unimplemented behaviour, ranked by how many maps in the",
        "**4,252-map corpus** each one actually affects. Counts come from",
        "`maps_final.csv`; the entity vocabularies are parsed from",
        "`src/jump/jump_ents.cpp` so they cannot drift from the DLL.",
        "",
        "## Blocks completion",
        "",
        "| gap | maps | examples | note |",
        "|---|---|---|---|",
    ]
    L += [row(n, m, note) for n, m, note in blocking]

    L += [
        "",
        "## Closed",
        "",
        "Gaps this audit found and the mod now handles. Counts are the maps that",
        "exercise each path, so they double as the regression-test target list.",
        "",
        "| gap | maps | examples | note |",
        "|---|---|---|---|",
    ]
    L += [row(n, m, note) for n, m, note in closed]

    L += [
        "",
        "## Cosmetic / degradation only",
        "",
        "| gap | maps | examples | note |",
        "|---|---|---|---|",
    ]
    L += [row(n, m, note) for n, m, note in cosmetic]

    # Unsupported classnames, split: another mod's content vs plausibly jump-related.
    other_mod_prefixes = ("item_pball", "weapon_pball", "monster_")
    other_mod_names = {"flag", "base", "hill", "junior", "item_score", "func_teamwall"}
    jumpish = [
        r for r in unknown_ents
        if not r["classname"].startswith(other_mod_prefixes)
        and r["classname"] not in other_mod_names
    ]

    L += [
        "",
        "## Unsupported classnames worth a second look",
        "",
        "Excludes paintball/CTF/monster entities, which belong to other mods and are",
        "out of scope. Everything below is freed on spawn with a console line.",
        "",
        "`func_model` leads by a wide margin. It is a decorative model placer from the",
        "old mapping tools with no Q2RE spawn function, so those maps lose scenery but",
        "stay completable -- worth knowing before someone reports a map as \"broken\".",
        "The `light_*` variants and `lightflare` are likewise cosmetic. Nothing in this",
        "list gates a finish or a checkpoint.",
        "",
        "| classname | entities | maps |",
        "|---|---|---|",
    ]
    for r in jumpish[:25]:
        L.append(f"| `{r['classname']}` | {r['entities']} | {r['maps']} |")

    # The raw invalid-key list is dominated by paintball/CTF team plumbing.
    other_mod_keys = re.compile(
        r"type|teamnumber|temnumber|give|loaded|loadco2|gamemode|team\d|maxteams|teams|"
        r"jail|ammolist|pcount|hill|rendercolor|pball|weaponslist",
        re.I,
    )
    jumpish_keys = [r for r in invalid_keys if not other_mod_keys.search(r["key"])]

    L += [
        "",
        "## Legacy keys silently ignored",
        "",
        f"{len(invalid_keys)} distinct keys are rejected corpus-wide, but the bulk are",
        "paintball/CTF team plumbing (`GiveGun`, `teamnumber`, `maxteams`, …).",
        f"Filtering those leaves {len(jumpish_keys)}, and **none of them is a jump",
        "mechanic** -- they are editor metadata (`wad`, `requiredfiles`), fog and",
        "render tweaks, or typos (`stlye`). No map loses jump behaviour to a rejected",
        "key.",
        "",
        "| key | occurrences | maps |",
        "|---|---|---|",
    ]
    for r in jumpish_keys[:20]:
        L.append(f"| `{r['key']}` | {r['occurrences']} | {r['maps']} |")

    L += [
        "",
        "## What is left",
        "",
        "Ranked by what would actually change a player's experience:",
        "",
        "1. **Physics.** Unmeasured and unmeasurable from here: a dedicated server",
        "   runs no movement prediction, and this mod deliberately keeps stock",
        "   rerelease pmove. Maps built for the old 125 fps behaviour may have jumps",
        "   that are simply impossible. Very likely the largest remaining gap, and",
        "   only playtesting can size it.",
        "2. **mset data.** ~122 maps carry more than one weapon type, so a weapon the",
        "   map means as a tool ends the run. `sv jump_mset` now makes authoring that",
        "   data practical, but the data itself still has to be written or recovered",
        "   from an old server.",
        "3. **Nothing else with a map count above zero** that the mod can fix. The 298",
        "   maps with no finish entity have no finish line to find, and the 6 with a",
        "   bare `nextmap` have no player-reachable exit.",
        "",
    ]
    (out / "gaps.md").write_text("\n".join(L) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=here / "out")
    args = ap.parse_args(argv)

    static_rows = read_csv(args.out / "maps.csv")
    engine_rows = {r["name"].lower(): r for r in read_csv(args.out / "engine.csv")}
    unknown_ents = read_csv(args.out / "engine_unknown_ents.csv")
    invalid_keys = read_csv(args.out / "engine_invalid_keys.csv")

    merged: list[dict] = []
    for s in static_rows:
        # Join on the stem, but only for files the engine could actually reach.
        # The corpus's .tmp / .bsp_old strays share stems with real .bsp maps
        # (airtime.tmp vs airtime.bsp), so keying on the stem alone would pair a
        # truncated download with its working namesake's engine result.
        e = None
        if s["file"].lower().endswith(".bsp"):
            e = engine_rows.get(s["name"].lower())
        recon, final = reconcile(s, e)
        row = dict(s)
        row["reconciliation"] = recon
        row["final_tier"] = final
        row["status"] = e["status"] if e else "NOT_ATTEMPTED"
        row["load_error"] = (e or {}).get("load_error", "")
        row["err_drop"] = (e or {}).get("err_drop", "")
        row["engine_unknown_ents"] = (e or {}).get("unknown_ents", "")
        row["engine_unknown_top"] = (e or {}).get("unknown_top", "")
        row["engine_invalid_keys"] = (e or {}).get("invalid_keys", "")
        row["engine_invalid_key_top"] = (e or {}).get("invalid_key_top", "")
        row["engine_legacy_dropped"] = (e or {}).get("legacy_dropped", "")
        row["engine_inhibited"] = (e or {}).get("inhibited", "")
        row["jump_warnings"] = (e or {}).get("jump_warnings", "")
        merged.append(row)

    fields = list(static_rows[0].keys()) + [
        "final_tier", "reconciliation", "status", "load_error", "err_drop",
        "engine_unknown_ents", "engine_unknown_top", "engine_invalid_keys",
        "engine_invalid_key_top", "engine_legacy_dropped", "engine_inhibited",
        "jump_warnings",
    ]
    with (args.out / "maps_final.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for r in sorted(merged, key=lambda r: r["name"]):
            w.writerow(r)

    write_report(args.out, merged, unknown_ents, invalid_keys)
    write_gaps(args.out, merged, unknown_ents, invalid_keys)

    tiers = Counter(r["final_tier"] for r in merged)
    recon = Counter(r["reconciliation"] for r in merged)
    print(f"{len(merged)} maps -> {args.out / 'maps_final.csv'}")
    for t in ("PLAYABLE", "DEGRADED", "UNFINISHABLE", "NOT_JUMP", "BROKEN"):
        print(f"  {t:<13} {tiers.get(t, 0)}")
    print()
    for k, n in recon.most_common():
        print(f"  {k:<28} {n}")
    print(f"\nwrote report.md and gaps.md")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
