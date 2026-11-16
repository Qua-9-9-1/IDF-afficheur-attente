#!/usr/bin/env python3
"""Generate the line/stop referential copied to the SD card (/idfm/*.json).

Offline generator, Python 3.10+, standard library only. It downloads public
Ile-de-France Mobilites open data (no API key needed) and writes:

    <out>/lines.json            lines of the requested modes
    <out>/stops_<lineId>.json   stops of one line, in route order

Each stop carries the MonitoringRef to use with PRIM stop-monitoring:
    STIF:StopArea:SP:<ZdAId>:   (zone d'arret / monomodal stop place)

Sources (see docs/REFERENTIEL.md):
  - referentiel-des-lignes  : line id, short name, mode, colours
  - arrets-lignes           : which stops (ZdA or quay) belong to which line
  - zones-d-arrets          : ZdA names
  - arrets                  : quay (ArR) -> ZdA mapping
  - GTFS IDFM (IDFM-gtfs.zip): stop order, from the trips of each line

Usage:
    python scripts/gen_referential.py                      # RER A-E
    python scripts/gen_referential.py --modes RER,METRO
    python scripts/gen_referential.py --gtfs path/to/IDFM-gtfs.zip
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

ODS_BASE = "https://data.iledefrance-mobilites.fr/api/explore/v2.1/catalog/datasets"
GTFS_URL = "https://eu.ftp.opendatasoft.com/stif/GTFS/IDFM-gtfs.zip"
USER_AGENT = "idfm-board-referential-generator/1.0"

# Mode name written in lines.json -> Opendatasoft "where" clause on referentiel-des-lignes.
MODE_FILTERS = {
    "RER": 'transportmode="rail" AND networkname="RER"',
    "TRANSILIEN": 'transportmode="rail" AND networkname="Transilien"',
    "METRO": 'transportmode="metro"',
    "TRAM": 'transportmode="tram"',
    "BUS": 'transportmode="bus"',
}

SCHEMA_VERSION = 1


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


# ---------------------------------------------------------------------------
# Downloads (with a small on-disk cache)
# ---------------------------------------------------------------------------


def default_cache_dir() -> Path:
    # Repo-local and git-ignored, like every tool of the project (nothing outside the repo).
    return Path(__file__).resolve().parent.parent / ".tools" / "referential-cache"


def download(url: str, dest: Path, max_age_h: float, refresh: bool) -> Path:
    if dest.exists() and not refresh and (time.time() - dest.stat().st_mtime) < max_age_h * 3600:
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    log(f"  downloading {url}")
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=300) as resp, open(tmp, "wb") as out:
        while chunk := resp.read(1 << 20):
            out.write(chunk)
    tmp.replace(dest)
    return dest


def ods_export(dataset: str, cache: Path, select: str, where: str | None, max_age_h: float, refresh: bool) -> list[dict]:
    params = {"select": select}
    if where:
        params["where"] = where
    url = f"{ODS_BASE}/{dataset}/exports/json?{urllib.parse.urlencode(params)}"
    key = re.sub(r"[^A-Za-z0-9]+", "_", f"{dataset}_{select}_{where or ''}")[:120]
    path = download(url, cache / f"{key}.json", max_age_h, refresh)
    with open(path, encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def natural_key(s: str):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]


def bare_line_id(s: str) -> str:
    """'IDFM:C01742' / 'STIF:Line::C01742:' / 'C01742' -> 'C01742'."""
    m = re.search(r"C\d{5}", s)
    return m.group(0) if m else s


def hex_color(s: str | None, default: str) -> str:
    s = (s or "").strip().lstrip("#").upper()
    return s if re.fullmatch(r"[0-9A-F]{6}", s) else default


class StopResolver:
    """Maps any IDFM stop id (ZdA or quay) to its ZdA id."""

    def __init__(self, quay_to_zda: dict[str, str]):
        self.quay_to_zda = quay_to_zda

    def zda(self, stop_id: str) -> str | None:
        m = re.fullmatch(r"IDFM:monomodalStopPlace:(\d+)", stop_id)
        if m:
            return m.group(1)
        m = re.fullmatch(r"IDFM:(\d+)", stop_id)
        if m:
            return self.quay_to_zda.get(m.group(1))
        return None


# ---------------------------------------------------------------------------
# GTFS: stop patterns per line
# ---------------------------------------------------------------------------


def read_gtfs_patterns(gtfs: Path, line_ids: set[str], resolver: StopResolver) -> dict[str, Counter]:
    """Return {lineId: Counter({(zda, zda, ...): trip_count})}, oriented by direction_id 0."""
    route_ids = {f"IDFM:{lid}" for lid in line_ids}
    trip_info: dict[str, tuple[str, str]] = {}
    with zipfile.ZipFile(gtfs) as z:
        with z.open("trips.txt") as f:
            for row in csv.DictReader(io.TextIOWrapper(f, encoding="utf-8-sig")):
                if row["route_id"] in route_ids:
                    trip_info[row["trip_id"]] = (bare_line_id(row["route_id"]), row.get("direction_id") or "0")
        log(f"  {len(trip_info)} trips for {len(line_ids)} line(s); scanning stop_times.txt (large)...")

        seqs: dict[str, list[tuple[int, str]]] = defaultdict(list)
        with z.open("stop_times.txt") as f:
            text = io.TextIOWrapper(f, encoding="utf-8-sig", newline="")
            header = next(csv.reader([text.readline()]))
            i_trip, i_stop, i_seq = header.index("trip_id"), header.index("stop_id"), header.index("stop_sequence")
            ncols = max(i_trip, i_stop, i_seq) + 1
            for line in text:
                comma = line.find(",")
                trip = line[:comma]
                if trip not in trip_info:
                    continue
                # Leading columns (ids, times, sequence) never contain commas.
                cols = line.split(",", ncols)
                seqs[trip].append((int(cols[i_seq]), cols[i_stop]))

    patterns: dict[str, Counter] = defaultdict(Counter)
    for trip, stops in seqs.items():
        line_id, direction = trip_info[trip]
        stops.sort()
        zdas: list[str] = []
        for _, sid in stops:
            z = resolver.zda(sid)
            if z and (not zdas or zdas[-1] != z):
                zdas.append(z)
        if len(zdas) < 2:
            continue
        if direction == "1":
            zdas.reverse()
        patterns[line_id][tuple(zdas)] += 1
    return patterns


def merge_patterns(patterns: Counter) -> list[str]:
    """Merge stop patterns (branches, short turns) into one ordered list.

    The longest pattern is the backbone (trunk + main branches). Stops missing
    from it are inserted as contiguous blocks:
      - a branch (before the first or after the last common stop) is appended
        at the end of the list, read from the junction outward;
      - a detour between two common stops goes right after the first of them.
    Result: "main route end to end, then each extra branch", every branch being
    a readable block.
    """
    ordered = sorted(patterns.items(), key=lambda kv: (-len(set(kv[0])), -kv[1], kv[0]))
    result: list[str] = []
    for pat, _count in ordered:
        pat = list(dict.fromkeys(pat))  # drop repeated stops (loops)
        if not result:
            result = pat
            continue
        pos = {s: i for i, s in enumerate(result)}
        common = [pos[s] for s in pat if s in pos]
        if len(common) >= 2:
            ups = sum(1 for a, b in zip(common, common[1:]) if b > a)
            if ups < (len(common) - 1) - ups:
                pat.reverse()
        if not common:
            result.extend(pat)  # disconnected pattern (should not happen)
            continue

        pending: list[str] = []
        anchor: str | None = None
        for s in pat:
            if s not in pos:
                pending.append(s)
                continue
            if pending:
                if anchor is None:
                    result.extend(reversed(pending))  # branch before the junction, read outward
                else:
                    k = result.index(anchor) + 1
                    result[k:k] = pending
                pending = []
                pos = {x: i for i, x in enumerate(result)}
            anchor = s
        if pending:
            result.extend(pending)  # branch after the last common stop
    return result


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--modes", default="RER", help=f"comma-separated, among {','.join(MODE_FILTERS)} (default RER)")
    ap.add_argument("--lines", default="", help="optional comma-separated line ids (C01742,...) or short names to keep")
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "sd" / "idfm"))
    ap.add_argument("--gtfs", help="use this local IDFM-gtfs.zip instead of downloading it")
    ap.add_argument("--cache-dir", default=str(default_cache_dir()))
    ap.add_argument("--max-age", type=float, default=24.0, help="cache max age in hours (default 24)")
    ap.add_argument("--refresh", action="store_true", help="ignore the cache")
    ap.add_argument("--min-trips", type=int, default=2, help="ignore stop patterns run by fewer trips (default 2)")
    ap.add_argument("--keep-unserved", action="store_true",
                    help="keep stops listed in arrets-lignes but served by no GTFS trip (appended alphabetically)")
    args = ap.parse_args()

    modes = [m.strip().upper() for m in args.modes.split(",") if m.strip()]
    for m in modes:
        if m not in MODE_FILTERS:
            ap.error(f"unknown mode {m}; expected one of {','.join(MODE_FILTERS)}")
    keep = {s.strip() for s in args.lines.split(",") if s.strip()}
    cache = Path(args.cache_dir)
    out = Path(args.out)

    # 1. Lines
    log("Lines (referentiel-des-lignes)")
    lines: list[dict] = []
    for mode in modes:
        rows = ods_export(
            "referentiel-des-lignes",
            cache,
            "id_line,shortname_line,colourweb_hexa,textcolourweb_hexa,status",
            MODE_FILTERS[mode],
            args.max_age,
            args.refresh,
        )
        for r in rows:
            if (r.get("status") or "active") != "active":
                continue
            if keep and r["id_line"] not in keep and r["shortname_line"] not in keep:
                continue
            lines.append(
                {
                    "id": r["id_line"],
                    "shortName": r["shortname_line"],
                    "mode": mode,
                    "color": hex_color(r.get("colourweb_hexa"), "888888"),
                    "textColor": hex_color(r.get("textcolourweb_hexa"), "FFFFFF"),
                }
            )
    lines.sort(key=lambda l: (modes.index(l["mode"]), natural_key(l["shortName"])))
    line_ids = {l["id"] for l in lines}
    log(f"  {len(lines)} line(s): {' '.join(l['mode'] + ' ' + l['shortName'] for l in lines)}")
    if not lines:
        log("No line found, nothing to do.")
        return 1

    # 2. Stop referentials
    log("Stops (arrets, zones-d-arrets, arrets-lignes)")
    quay_to_zda = {
        str(r["arrid"]): str(r["zdaid"])
        for r in ods_export("arrets", cache, "arrid,zdaid", None, args.max_age, args.refresh)
        if r.get("arrid") and r.get("zdaid")
    }
    zda_name = {
        str(r["zdaid"]): r["zdaname"]
        for r in ods_export("zones-d-arrets", cache, "zdaid,zdaname", None, args.max_age, args.refresh)
    }
    resolver = StopResolver(quay_to_zda)

    official: dict[str, set[str]] = defaultdict(set)  # lineId -> ZdA ids served (arrets-lignes)
    where = " OR ".join(f'id="IDFM:{lid}"' for lid in sorted(line_ids))
    for r in ods_export("arrets-lignes", cache, "id,stop_id,stop_name", where, args.max_age, args.refresh):
        z = resolver.zda(r["stop_id"])
        if z:
            official[bare_line_id(r["id"])].add(z)
            zda_name.setdefault(z, r["stop_name"])

    # 3. Order from GTFS
    log("Order (GTFS IDFM)")
    gtfs = Path(args.gtfs) if args.gtfs else download(GTFS_URL, cache / "IDFM-gtfs.zip", args.max_age, args.refresh)
    patterns = read_gtfs_patterns(gtfs, line_ids, resolver)

    # 4. Write files
    out.mkdir(parents=True, exist_ok=True)
    written: list[dict] = []
    for line in lines:
        lid = line["id"]
        pats = Counter({p: c for p, c in patterns.get(lid, Counter()).items() if c >= args.min_trips})
        if not pats:
            pats = patterns.get(lid, Counter())
        allowed = official.get(lid)
        if allowed:
            # Drop stops not officially attached to the line (works detours, data noise).
            filtered: Counter = Counter()
            for p, c in pats.items():
                t = tuple(dict.fromkeys(s for s in p if s in allowed))
                if len(t) >= 2:
                    filtered[t] += c
            pats = filtered
        order = merge_patterns(pats) if pats else []
        missing = sorted((allowed or set()) - set(order), key=lambda z: natural_key(zda_name.get(z, z)))
        if missing:
            action = "appended" if args.keep_unserved or not order else "ignored"
            log(f"  {line['mode']} {line['shortName']}: {len(missing)} stop(s) of arrets-lignes without any "
                f"GTFS trip, {action}: " + ", ".join(zda_name.get(z, z) for z in missing))
            if action == "appended":
                order += missing
        if not order:
            log(f"  {line['mode']} {line['shortName']} ({lid}): no stop found, line skipped")
            continue
        stops = [{"name": zda_name.get(z, z), "ref": f"STIF:StopArea:SP:{z}:"} for z in order]
        path = out / f"stops_{lid}.json"
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"v": SCHEMA_VERSION, "line": lid, "stops": stops}, f, ensure_ascii=False, separators=(",", ":"))
            f.write("\n")
        log(f"  {line['mode']} {line['shortName']}: {len(stops)} stops, {len(pats)} pattern(s) -> {path.name} "
            f"({path.stat().st_size} B)")
        written.append(line)

    path = out / "lines.json"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(
            {"v": SCHEMA_VERSION, "generated": time.strftime("%Y-%m-%d"), "lines": written},
            f,
            ensure_ascii=False,
            separators=(",", ":"),
        )
        f.write("\n")
    log(f"Wrote {path} ({path.stat().st_size} B, {len(written)} line(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
