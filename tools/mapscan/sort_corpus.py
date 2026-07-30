#!/usr/bin/env python3
"""Sort the map corpus into folders by compatibility verdict.

    python sort_corpus.py                 # dry run: show what would move
    python sort_corpus.py --apply         # do it
    python sort_corpus.py --undo --apply  # put everything back flat

Reads the verdicts from out/maps_final.csv, so run the audit first. Folders:

    playable/      loads, has a finish, hits no known gap
    degraded/      finishable, but loses some decorative entities
    unfinishable/  loads, but there is no way to complete a run
    not-jump/      another mod's map - paintball, CTF, single-player
    broken/        will not load at all
    strays/        .tmp / .bsp_old - `map <name>` cannot reach these anyway

Deliberately NOT sorted by difficulty. Nothing in a BSP says how hard a map is:
only ~10% carry any difficulty word in the filename or the worldspawn title, and
a good share of those are thematic ("hell", "torture") rather than a rating.
Difficulty is best derived from play: the mod records per-map times and
completions, so completion rate and median time answer it properly once a server
has traffic.

Every move is recorded in _sorted.csv inside the corpus, which is what --undo
reads, so this is fully reversible without re-running the audit.
"""

from __future__ import annotations

import argparse
import csv
import shutil
import sys
from collections import Counter
from pathlib import Path

TIER_DIR = {
    "PLAYABLE": "playable",
    "DEGRADED": "degraded",
    "UNFINISHABLE": "unfinishable",
    "NOT_JUMP": "not-jump",
    "BROKEN": "broken",
}
STRAYS = "strays"
MANIFEST = "_sorted.csv"

# Only map files move. .filelist manifests and stray .map source stay at the top
# level - they are not maps and pairing them with a verdict would be misleading.
MOVEABLE = {".bsp", ".tmp", ".bsp_old"}


def load_plan(corpus: Path, out: Path) -> list[tuple[Path, str]]:
    """Return [(current path, destination folder)] for everything to move."""
    csv_path = out / "maps_final.csv"
    if not csv_path.exists():
        raise SystemExit(f"missing {csv_path} - run the audit first "
                         "(scan_bsp.py, then merge.py)")

    with csv_path.open(newline="", encoding="utf-8-sig") as f:
        verdict = {r["file"].lower(): r["final_tier"] for r in csv.DictReader(f)}

    plan: list[tuple[Path, str]] = []
    unknown: list[str] = []

    # rglob so a partly-sorted corpus can be re-sorted or undone.
    for p in sorted(corpus.rglob("*")):
        if not p.is_file() or p.suffix.lower() not in MOVEABLE:
            continue
        if p.suffix.lower() != ".bsp":
            plan.append((p, STRAYS))
            continue
        tier = verdict.get(p.name.lower())
        if tier is None:
            unknown.append(p.name)
            continue
        plan.append((p, TIER_DIR.get(tier, "broken")))

    if unknown:
        print(f"!! {len(unknown)} file(s) not in maps_final.csv, left alone: "
              f"{', '.join(unknown[:5])}", file=sys.stderr)
    return plan


def load_undo(corpus: Path) -> list[tuple[Path, str]]:
    manifest = corpus / MANIFEST
    if not manifest.exists():
        raise SystemExit(f"no {MANIFEST} in {corpus} - nothing to undo")
    with manifest.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    return [(corpus / r["folder"] / r["file"], "") for r in rows]


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", type=Path,
                    default=Path(r"E:\code\projects\q2re-jump\q2jump-maps"))
    ap.add_argument("--out", type=Path, default=here / "out")
    ap.add_argument("--apply", action="store_true",
                    help="actually move files (default is a dry run)")
    ap.add_argument("--undo", action="store_true",
                    help="restore everything to the top level")
    args = ap.parse_args(argv)

    corpus: Path = args.corpus
    if not corpus.is_dir():
        raise SystemExit(f"no such directory: {corpus}")

    if args.undo:
        moves = [(src, corpus) for src, _ in load_undo(corpus) if src.exists()]
        print(f"undo: {len(moves)} file(s) back to {corpus}")
        if not args.apply:
            print("\ndry run - pass --apply to do it")
            return 0
        for src, dst_dir in moves:
            shutil.move(str(src), str(dst_dir / src.name))
        (corpus / MANIFEST).unlink(missing_ok=True)
        for d in list(TIER_DIR.values()) + [STRAYS]:
            p = corpus / d
            if p.is_dir() and not any(p.iterdir()):
                p.rmdir()
        print(f"restored {len(moves)} file(s)")
        return 0

    plan = load_plan(corpus, args.out)
    counts = Counter(folder for _, folder in plan)

    print(f"{len(plan)} map file(s) in {corpus}\n")
    for folder in list(TIER_DIR.values()) + [STRAYS]:
        if counts[folder]:
            print(f"  {folder + '/':<16}{counts[folder]:>6}")

    already = sum(1 for p, folder in plan if p.parent.name == folder)
    todo = [(p, folder) for p, folder in plan if p.parent.name != folder]
    if already:
        print(f"\n{already} already in place")
    print(f"{len(todo)} to move")

    if not args.apply:
        print("\ndry run - pass --apply to do it")
        return 0

    for folder in set(f for _, f in todo):
        (corpus / folder).mkdir(exist_ok=True)

    moved = 0
    for src, folder in todo:
        dst = corpus / folder / src.name
        if dst.exists():
            print(f"!! destination exists, skipping: {dst}", file=sys.stderr)
            continue
        shutil.move(str(src), str(dst))
        moved += 1

    # Manifest covers everything now sorted, not just this run's moves, so undo
    # works even if the sort was done in stages.
    with (corpus / MANIFEST).open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "folder"])
        for _src, folder in plan:
            w.writerow([_src.name, folder])

    print(f"\nmoved {moved} file(s); wrote {MANIFEST}")
    print("undo with: python sort_corpus.py --undo --apply")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
