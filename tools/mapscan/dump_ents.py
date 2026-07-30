#!/usr/bin/env python3
"""Dump the entity lump of one or more BSPs, for eyeballing a specific map.

    python dump_ents.py 4c3jump1 aljump26
    python dump_ents.py --raw makorace1 | less

Names resolve against --maps-dir; a path also works.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from scan_bsp import Bsp, parse_entities  # noqa: E402


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("maps", nargs="+", help="map names (without .bsp) or paths")
    ap.add_argument(
        "--maps-dir",
        type=Path,
        default=Path(r"E:\code\projects\q2re-jump\q2jump-maps"),
    )
    ap.add_argument("--raw", action="store_true", help="print the lump verbatim")
    ap.add_argument("--counts", action="store_true", help="classname histogram only")
    args = ap.parse_args(argv)

    for name in args.maps:
        p = Path(name)
        if not p.is_file():
            p = args.maps_dir / (name if name.lower().endswith(".bsp") else name + ".bsp")
        if not p.is_file():
            print(f"!! not found: {name}", file=sys.stderr)
            continue

        bsp = Bsp(p)
        print(f"===== {p.name}  ({bsp.ident} v{bsp.version}, {bsp.size} bytes) =====")
        if args.raw:
            print(bsp.entities)
            continue

        blocks, problems = parse_entities(bsp.entities)
        if problems:
            print(f"  !! parse problems: {problems}")
        if args.counts:
            for cls, n in Counter(b.get("classname", "?") for b in blocks).most_common():
                print(f"  {n:5}  {cls}")
            continue

        for i, b in enumerate(blocks):
            cls = b.get("classname", "?")
            rest = {k: v for k, v in b.items() if k != "classname"}
            print(f"  [{i:3}] {cls}")
            for k, v in sorted(rest.items()):
                print(f"        {k} = {v}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
