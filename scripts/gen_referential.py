#!/usr/bin/env python3
"""Generate the line/stop referential copied to the SD card (/idfm/*.json).

Offline generator, Python 3.10+, standard library only. It downloads public
Ile-de-France Mobilites open data (no API key needed) and writes:

    <out>/lines.json            lines of the requested modes, except bus
    <out>/lines_bus.json        bus lines, split into selection groups (~1700 lines)
    <out>/stops_<lineId>.json   stops of one line, in route order
    <out>/bus/stops_<lineId>.json   same for bus lines (git-ignored, regenerate with --modes BUS)

Each stop carries the MonitoringRef to use with PRIM stop-monitoring:
    STIF:StopArea:SP:<ZdAId>:   (zone d'arret / monomodal stop place)
Metro, tram and bus stops also list their directions, one per platform / physical stop:
    "dirs": [{"name": "<terminus>", "ref": "STIF:StopPoint:Q:<quay>:"}, ...]

Sources (see docs/REFERENTIEL.md):
  - referentiel-des-lignes  : line id, short name, mode, colours
  - arrets-lignes           : which stops (ZdA or quay) belong to which line
  - zones-d-arrets          : ZdA names
  - arrets                  : quay (ArR) -> ZdA mapping
  - GTFS IDFM (IDFM-gtfs.zip): stop order, from the trips of each line

Usage:
    python scripts/gen_referential.py                      # RER A-E
    python scripts/gen_referential.py --modes RER,METRO,TRANSILIEN,TRAM,BUS   # full SD content
    python scripts/gen_referential.py --gtfs path/to/IDFM-gtfs.zip
"""

from __future__ import annotations

import argparse
import csv
import hashlib
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

# Modes whose displays are per platform: each stop also lists its directions ("dirs"), each with
# the platform (quay) MonitoringRef STIF:StopPoint:Q:<quay>:. Metro and tram platform displays
# show one direction; a bus shelter shows every line of its physical stop (= quay).
# RER / Transilien: stop area ref only (SNCF real time is only published per stop area).
PLATFORM_MODES = {"METRO", "TRAM", "BUS"}

SCHEMA_VERSION = 1


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


# ---------------------------------------------------------------------------
# Downloads (with a small on-disk cache)
# ---------------------------------------------------------------------------


def stops_path(out: Path, mode: str, line_id: str) -> Path:
    """Bus stop files (~1900 files, ~7 MB) go to <out>/bus/, git-ignored: regenerate them with
    --modes BUS instead of versioning them. Must match idfmLoadStops() in referential.cpp."""
    return (out / "bus" if mode == "BUS" else out) / f"stops_{line_id}.json"


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
    # Readable prefix + hash of the whole query (a truncated name alone made queries collide).
    digest = hashlib.sha1(f"{dataset}|{select}|{where or ''}".encode()).hexdigest()[:10]
    key = re.sub(r"[^A-Za-z0-9]+", "_", f"{dataset}_{where or 'all'}")[:80] + "_" + digest
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


def read_gtfs_routes(gtfs: Path) -> dict[str, str]:
    """{lineId: route_long_name} (used to drop replacement buses and to label homonym lines)."""
    with zipfile.ZipFile(gtfs) as z, z.open("routes.txt") as f:
        return {bare_line_id(r["route_id"]): r.get("route_long_name") or ""
                for r in csv.DictReader(io.TextIOWrapper(f, encoding="utf-8-sig"))}


def read_gtfs_trips(gtfs: Path, line_ids: set[str], resolver: StopResolver) -> dict[str, Counter]:
    """Return {lineId: Counter({(direction_id, ((zda, quay_id), ...)): trip_count})}.

    Each trip becomes its sequence of zones d'arret in travel order; consecutive stop_times
    in the same ZdA are collapsed, keeping the stop_id of the last one (the departure quay).
    stop_times.txt (~8 M rows) is streamed; each trip is reduced as soon as its rows end, so
    memory stays small even for every bus line (rows of a trip are contiguous in the IDFM GTFS;
    a trip split over several blocks still works, it is reduced once at the end).
    """
    route_ids = {f"IDFM:{lid}" for lid in line_ids}
    trip_info: dict[str, tuple[str, str]] = {}
    with zipfile.ZipFile(gtfs) as z:
        with z.open("trips.txt") as f:
            for row in csv.DictReader(io.TextIOWrapper(f, encoding="utf-8-sig")):
                if row["route_id"] in route_ids:
                    trip_info[row["trip_id"]] = (bare_line_id(row["route_id"]), row.get("direction_id") or "0")
        log(f"  {len(trip_info)} trips for {len(line_ids)} line(s); scanning stop_times.txt (large)...")

        trips: dict[str, Counter] = defaultdict(Counter)

        def reduce_trip(trip: str, stops: list[tuple[int, str]]) -> None:
            line_id, direction = trip_info[trip]
            stops.sort()
            seq: list[tuple[str, str]] = []
            for _, sid in stops:
                zd = resolver.zda(sid)
                if not zd:
                    continue
                if seq and seq[-1][0] == zd:
                    seq[-1] = (zd, sid)
                else:
                    seq.append((zd, sid))
            if len(seq) >= 2:
                trips[line_id][(direction, tuple(seq))] += 1

        split: dict[str, list[tuple[int, str]]] = {}  # trips seen in several blocks (not expected)
        done: set[str] = set()
        cur, rows = None, []
        with z.open("stop_times.txt") as f:
            text = io.TextIOWrapper(f, encoding="utf-8-sig", newline="")
            header = next(csv.reader([text.readline()]))
            i_trip, i_stop, i_seq = header.index("trip_id"), header.index("stop_id"), header.index("stop_sequence")
            ncols = max(i_trip, i_stop, i_seq) + 1

            def flush() -> None:
                if cur is None:
                    return
                if cur in done or cur in split:
                    split.setdefault(cur, []).extend(rows)
                else:
                    done.add(cur)
                    reduce_trip(cur, rows)

            for line in text:
                trip = line[:line.find(",")]
                if trip not in trip_info:
                    continue
                if trip != cur:
                    flush()
                    cur, rows = trip, []
                # Leading columns (ids, times, sequence) never contain commas.
                cols = line.split(",", ncols)
                rows.append((int(cols[i_seq]), cols[i_stop]))
            flush()
        if split:
            # Undo the partial reductions, then reduce the merged rows (rare / never in practice).
            log(f"  {len(split)} trip(s) not contiguous in stop_times.txt: rescanning them")
            return read_gtfs_trips_slow(gtfs, line_ids, resolver, trip_info)
    return trips


def read_gtfs_trips_slow(gtfs: Path, line_ids: set[str], resolver: StopResolver,
                         trip_info: dict[str, tuple[str, str]]) -> dict[str, Counter]:
    """Fallback of read_gtfs_trips() when stop_times.txt is not grouped by trip (keeps every row)."""
    seqs: dict[str, list[tuple[int, str]]] = defaultdict(list)
    with zipfile.ZipFile(gtfs) as z, z.open("stop_times.txt") as f:
        text = io.TextIOWrapper(f, encoding="utf-8-sig", newline="")
        header = next(csv.reader([text.readline()]))
        i_trip, i_stop, i_seq = header.index("trip_id"), header.index("stop_id"), header.index("stop_sequence")
        ncols = max(i_trip, i_stop, i_seq) + 1
        for line in text:
            trip = line[:line.find(",")]
            if trip in trip_info:
                cols = line.split(",", ncols)
                seqs[trip].append((int(cols[i_seq]), cols[i_stop]))
    trips: dict[str, Counter] = defaultdict(Counter)
    for trip, stops in seqs.items():
        line_id, direction = trip_info[trip]
        stops.sort()
        seq: list[tuple[str, str]] = []
        for _, sid in stops:
            zd = resolver.zda(sid)
            if not zd:
                continue
            if seq and seq[-1][0] == zd:
                seq[-1] = (zd, sid)
            else:
                seq.append((zd, sid))
        if len(seq) >= 2:
            trips[line_id][(direction, tuple(seq))] += 1
    return trips


def oriented(direction: str, seq: tuple) -> tuple[str, ...]:
    """ZdA sequence of a trip, oriented like direction_id 0."""
    zdas = [z for z, _ in seq]
    if direction == "1":
        zdas.reverse()
    return tuple(zdas)


def stop_patterns(trips: Counter) -> Counter:
    """{(zda, ...): trip_count}, every pattern oriented like direction_id 0."""
    patterns: Counter = Counter()
    for (direction, seq), count in trips.items():
        patterns[oriented(direction, seq)] += count
    return patterns


def platform_directions(trips: Counter, zda_name: dict[str, str], label: str,
                        detail: bool = True) -> dict[str, list[dict]]:
    """Per station (ZdA): the directions a passenger can board, each with its platform ref.

    Returns {zda: [{"name": "<terminus>", "ref": "STIF:StopPoint:Q:<quay>:"}, ...]}, direction_id 0
    first. For each (station, direction_id), only trips that *leave* the station count (a train
    terminating there is not a departure), which drops the arrival platform at termini.
      - ref: the departure quay most used by those trips (GTFS stop_id IDFM:<n> = PRIM quay <n>);
      - name: the line ends reachable from the station, i.e. the "real" termini (a last stop
        that no other trip of the same direction runs through), most served first, joined with
        " / " on branched lines (e.g. line 13 northbound: "Les Courtilles / Saint-Denis Université").
        Short turns are ignored, like on the official platform signs.
    """
    # Real termini per direction: last stops never passed through by another trip of that direction.
    through: dict[str, set[str]] = defaultdict(set)
    lasts: dict[str, set[str]] = defaultdict(set)
    for (direction, seq), _count in trips.items():
        zdas = [z for z, _ in seq]
        lasts[direction].add(zdas[-1])
        through[direction].update(zdas[:-1])
    real_end = {d: lasts[d] - through[d] for d in lasts}

    quays: dict[tuple[str, str], Counter] = defaultdict(Counter)
    for (direction, seq), count in trips.items():
        for z, sid in seq[:-1]:
            quays[(z, direction)][sid] += count

    # A direction may leave from several platforms (line 13 northbound at La Fourche: one per
    # branch). Quays used by >= 20 % of the departures become separate directions; rarer ones
    # (works, data noise) are folded into the main quay.
    main_quay: dict[tuple[str, str, str], str] = {}
    for (z, direction), qc in quays.items():
        total = sum(qc.values())
        majors = [s for s, c in qc.most_common() if c * 5 >= total] or [qc.most_common(1)[0][0]]
        for s in qc:
            main_quay[(z, direction, s)] = s if s in majors else majors[0]
        if len(qc) > 1 and detail:
            quay_list = ", ".join(f"{s} x{c}" for s, c in qc.most_common())
            log(f"    {label} {zda_name.get(z, z)} dir {direction}: several quays ({quay_list}), "
                f"kept {' + '.join(majors)}")

    ends: dict[tuple[str, str, str], Counter] = defaultdict(Counter)
    lasts_any: dict[tuple[str, str, str], Counter] = defaultdict(Counter)
    firsts: dict[tuple[str, str, str], Counter] = defaultdict(Counter)
    for (direction, seq), count in trips.items():
        first, last = seq[0][0], seq[-1][0]
        for z, sid in seq[:-1]:
            key = (z, direction, main_quay[(z, direction, sid)])
            lasts_any[key][last] += count
            firsts[key][first] += count
            if last in real_end.get(direction, ()):
                ends[key][last] += count

    result: dict[str, list[dict]] = defaultdict(list)
    for key in sorted(lasts_any, key=lambda k: (k[0], k[1], -sum(lasts_any[k].values()))):
        z, direction, sid = key
        m = re.fullmatch(r"IDFM:(\d+)", sid)
        if not m:
            if detail:
                log(f"    {label} {zda_name.get(z, z)}: departure stop {sid} is not a quay, direction skipped")
            continue
        ec = ends.get(key) or lasts_any[key]
        tot = sum(ec.values())
        termini = [t for t, c in sorted(ec.items(), key=lambda kv: (-kv[1], zda_name.get(kv[0], kv[0])))
                   if c * 20 >= tot]  # ignore termini served by < 5 % of the trips
        name = " / ".join(zda_name.get(t, t) for t in termini)
        result[z].append({"name": name, "ref": f"STIF:StopPoint:Q:{m.group(1)}:", "_k": key})  # dir 0 first

    # Same terminus from two platforms (line 13 southbound at La Fourche, one per branch):
    # tell them apart by where their trains come from.
    for z, dirs in result.items():
        names = Counter(d["name"] for d in dirs)
        for d in dirs:
            key = d.pop("_k")
            if names[d["name"]] > 1:
                origin = firsts[key].most_common(1)[0][0]
                d["name"] += f" (depuis {zda_name.get(origin, origin)})"
                if detail:
                    log(f"    {label} {zda_name.get(z, z)}: same direction on several platforms -> {d['name']}")
        # Still ambiguous: same direction, same origin, several platforms used in turn (tram / bus
        # termini with two tracks or stands). One direction on the stop area: every departure.
        names = Counter(d["name"] for d in dirs)
        if any(c > 1 for c in names.values()):
            merged: list[dict] = []
            for d in dirs:
                if names[d["name"]] == 1:
                    merged.append(d)
                elif not any(m.get("_n") == d["name"] for m in merged):
                    plain = re.sub(r" \(depuis .*\)$", "", d["name"])
                    merged.append({"name": plain, "ref": f"STIF:StopArea:SP:{z}:", "_n": d["name"]})
                    if detail:
                        log(f"    {label} {zda_name.get(z, z)}: platforms used in turn -> {plain} on the stop area")
            for m in merged:
                m.pop("_n", None)
            dirs[:] = merged
    return result


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
    ap.add_argument("--verbose", action="store_true", help="per-line details for bus lines too")
    args = ap.parse_args()

    modes = [m.strip().upper() for m in args.modes.split(",") if m.strip()]
    for m in modes:
        if m not in MODE_FILTERS:
            ap.error(f"unknown mode {m}; expected one of {','.join(MODE_FILTERS)}")
    keep = {s.strip() for s in args.lines.split(",") if s.strip()}
    cache = Path(args.cache_dir)
    out = Path(args.out)

    gtfs = Path(args.gtfs) if args.gtfs else download(GTFS_URL, cache / "IDFM-gtfs.zip", args.max_age, args.refresh)
    long_names = read_gtfs_routes(gtfs)

    # 1. Lines
    log("Lines (referentiel-des-lignes)")
    lines: list[dict] = []
    for mode in modes:
        rows = ods_export(
            "referentiel-des-lignes",
            cache,
            "id_line,shortname_line,colourweb_hexa,textcolourweb_hexa,status,"
            "transportsubmode,networkname,operatorname",
            MODE_FILTERS[mode],
            args.max_age,
            args.refresh,
        )
        dropped: Counter = Counter()
        for r in rows:
            if (r.get("status") or "active") != "active":
                continue
            if keep and r["id_line"] not in keep and r["shortname_line"] not in keep:
                continue
            if mode == "BUS":
                why = bus_exclusion(r, long_names.get(r["id_line"], ""))
                if why:
                    dropped[why] += 1
                    continue
            line = {
                "id": r["id_line"],
                "shortName": r["shortname_line"],
                "mode": mode,
                "color": hex_color(r.get("colourweb_hexa"), "888888"),
                "textColor": hex_color(r.get("textcolourweb_hexa"), "FFFFFF"),
            }
            if mode == "BUS":  # private fields (grouping, homonym labels), not written as is
                line["_net"] = r.get("networkname") or r.get("operatorname") or ""
                line["_long"] = long_names.get(r["id_line"], "")
            lines.append(line)
        if dropped:
            log(f"  {mode}: excluded " + ", ".join(f"{c} {why}" for why, c in dropped.most_common()))
    lines.sort(key=lambda l: (modes.index(l["mode"]), natural_key(l["shortName"])))
    line_ids = {l["id"] for l in lines}
    summary = Counter(l["mode"] for l in lines)
    log(f"  {len(lines)} line(s): " + ", ".join(f"{m} {summary[m]}" for m in modes))
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
    # Whole dataset (one cached download, filtered here): a "where" listing ~1700 bus lines would
    # not fit in a URL.
    for r in ods_export("arrets-lignes", cache, "id,stop_id,stop_name", None, args.max_age, args.refresh):
        lid = bare_line_id(r.get("id") or "")
        if lid not in line_ids:
            continue
        z = resolver.zda(r["stop_id"])
        if z:
            official[lid].add(z)
            zda_name.setdefault(z, r["stop_name"])

    # 3. Order from GTFS
    log("Order (GTFS IDFM)")
    all_trips = read_gtfs_trips(gtfs, line_ids, resolver)

    # 4. Write files
    out.mkdir(parents=True, exist_ok=True)
    written: list[dict] = []
    for line in lines:
        if line["mode"] == "BUS" and not all_trips.get(line["id"]):
            # No trip in the next 30 days (seasonal, school-only...): no stop order nor platforms.
            log(f"  BUS {line['shortName']} ({line['id']}): no GTFS trip, line skipped")
            continue
        lid = line["id"]
        trips = all_trips.get(lid, Counter())
        # Ignore stop patterns run by fewer than --min-trips trips (unless nothing would remain).
        pat_count = stop_patterns(trips)
        kept = Counter({k: c for k, c in trips.items() if pat_count[oriented(*k)] >= args.min_trips})
        if kept:
            trips = kept
        allowed = official.get(lid)
        if allowed:
            # Drop stops not officially attached to the line (works detours, data noise).
            filtered: Counter = Counter()
            for (d, seq), c in trips.items():
                seen: dict[str, str] = {}
                for z, sid in seq:
                    if z in allowed:
                        seen.setdefault(z, sid)  # repeated stops (loops): keep the first passage
                t = tuple(seen.items())
                if len(t) >= 2:
                    filtered[(d, t)] += c
            trips = filtered
        pats = stop_patterns(trips)
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
        if line["mode"] in PLATFORM_MODES:
            dirs = platform_directions(trips, zda_name, f"{line['mode']} {line['shortName']}",
                                       detail=args.verbose or line["mode"] != "BUS")
            for z, stop in zip(order, stops):
                if dirs.get(z):
                    stop["dirs"] = dirs[z]
        path = stops_path(out, line["mode"], lid)
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"v": SCHEMA_VERSION, "line": lid, "stops": stops}, f, ensure_ascii=False, separators=(",", ":"))
            f.write("\n")
        if args.verbose or line["mode"] != "BUS":
            log(f"  {line['mode']} {line['shortName']}: {len(stops)} stops, {len(pats)} pattern(s) -> {path.name} "
                f"({path.stat().st_size} B)")
        written.append(line)

    def dump(path: Path, doc: dict) -> None:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(doc, f, ensure_ascii=False, separators=(",", ":"))
            f.write("\n")

    generated = time.strftime("%Y-%m-%d")
    small = [l for l in written if l["mode"] != "BUS"]
    path = out / "lines.json"
    dump(path, {"v": SCHEMA_VERSION, "generated": generated, "lines": small})
    log(f"Wrote {path} ({path.stat().st_size} B, {len(small)} line(s))")

    bus = [l for l in written if l["mode"] == "BUS"]
    path = out / "lines_bus.json"
    if bus:
        groups = bus_groups(bus)
        dump(path, {"v": SCHEMA_VERSION, "generated": generated, "mode": "BUS", "groups": groups})
        log(f"Wrote {path} ({path.stat().st_size} B, {len(bus)} line(s) in {len(groups)} group(s))")
        for g in groups:
            log(f"    {(g['in'] + ' > ') if g['in'] else ''}{g['name']}: {len(g['lines'])} line(s)")
        stops_size = sum(stops_path(out, "BUS", l["id"]).stat().st_size for l in bus)
        log(f"  bus stop files: {stops_size / 1e6:.1f} MB")
    elif "BUS" in modes and path.exists():
        path.unlink()
    return 0


# ---------------------------------------------------------------------------
# Bus lines: exclusions and selection groups
# ---------------------------------------------------------------------------

# Networks of SNCF replacement buses ("Remplacement RER A"...), not regular bus lines.
EXCLUDED_BUS_NETWORKS = {"RER", "Transilien", "TER"}
MENU_MAX = 60  # max entries of one menu on the device (rotary encoder)


def bus_exclusion(row: dict, long_name: str) -> str | None:
    """Why a bus line of referentiel-des-lignes is left out of the referential (None: kept)."""
    if row.get("transportsubmode") == "demandAndResponseBus":
        return "on-demand (TAD)"
    if (row.get("networkname") or "") in EXCLUDED_BUS_NETWORKS:
        return "SNCF replacement"
    if long_name.lower().startswith("remplacement"):
        return "replacement"
    return None


def bus_groups(bus: list[dict]) -> list[dict]:
    """Splits the bus lines into selection groups of <= ~MENU_MAX lines, in menu order.

    Returns [{"name": "20-99", "in": "", "lines": [...]}, {"name": "1100-1199 Vexin",
    "in": "1000-1999", "lines": [...]}, ...]: "in" is the parent menu entry ("" = top level).
      - numbers < 1000: one group per hundred ("1-19" and "20-99" for the first one: Paris is
        20-99), halved ("100-149", "150-199") when too big;
      - 4-digit numbers (networks of the outer suburbs, 2 first digits = network): one group per
        hundred, labelled with its main network, under a parent per thousand;
      - Noctilien (N + number): one group, split N1-N99 / N100+ when too big;
      - other names (TVM, Tzen, letters, "Soir"...): one group, or one per network.
    Homonyms inside a group (several "Soir"...) get a "label" with the GTFS long name or network.
    """
    def num(l: dict) -> int:
        return int(re.sub(r"\D", "", l["shortName"]))

    small: dict[int, list[dict]] = defaultdict(list)                      # hundred -> lines
    large: dict[int, dict[int, list[dict]]] = defaultdict(lambda: defaultdict(list))  # thousand -> hundred
    night: list[dict] = []
    other: list[dict] = []
    for l in bus:
        sn = l["shortName"]
        if re.fullmatch(r"\d+", sn):
            n = int(sn)
            if n < 1000:
                small[n // 100].append(l)
            else:
                large[n // 1000][n // 100].append(l)
        elif re.fullmatch(r"N\d+", sn):
            night.append(l)
        else:
            other.append(l)

    groups: list[dict] = []

    def add(name: str, parent: str, items: list[dict]) -> None:
        if items:
            groups.append({"name": name, "in": parent,
                           "lines": sorted(items, key=lambda l: natural_key(l["shortName"]))})

    def main_network(items: list[dict]) -> str:
        net, c = Counter(l["_net"] for l in items).most_common(1)[0]
        return net if net and c * 10 >= len(items) * 6 else ""

    for h, items in sorted(small.items()):
        if h == 0:
            add("1-19", "", [l for l in items if num(l) < 20])
            add("20-99", "", [l for l in items if num(l) >= 20])
        elif len(items) > MENU_MAX:
            add(f"{h}00-{h}49", "", [l for l in items if num(l) % 100 < 50])
            add(f"{h}50-{h}99", "", [l for l in items if num(l) % 100 >= 50])
        else:
            add(f"{h}00-{h}99", "", items)
    for t, children in sorted(large.items()):
        parent = f"{t}000-{t}999" if len(children) > 1 else ""
        for h, items in sorted(children.items()):
            net = main_network(items)
            suffix = f" {net}" if net else ""
            if len(items) > MENU_MAX:
                add(f"{h}00-{h}49{suffix}", parent, [l for l in items if num(l) % 100 < 50])
                add(f"{h}50-{h}99{suffix}", parent, [l for l in items if num(l) % 100 >= 50])
            else:
                add(f"{h}00-{h}99{suffix}", parent, items)
    if len(night) > MENU_MAX:
        add("N1-N99", "Noctilien", [l for l in night if num(l) < 100])
        add("N100+", "Noctilien", [l for l in night if num(l) >= 100])
    else:
        add("Noctilien", "", night)
    if len(other) <= MENU_MAX:
        add("Autres", "", other)
    else:
        by_net: dict[str, list[dict]] = defaultdict(list)
        for l in other:
            by_net[l["_net"] or "Divers"].append(l)
        divers = [l for net, its in by_net.items() if len(its) < 5 or net == "Divers" for l in its]
        for net in sorted(by_net, key=natural_key):
            if len(by_net[net]) >= 5 and net != "Divers":
                add(net, "Autres", by_net[net])
        add("Divers", "Autres", divers)  # small networks (< 5 named lines) together

    def out_line(l: dict) -> dict:
        return {k: v for k, v in l.items() if not k.startswith("_") and k != "mode"}

    for g in groups:
        names = Counter(l["shortName"] for l in g["lines"])
        labels: Counter = Counter()
        for l in g["lines"]:
            if names[l["shortName"]] > 1:
                long = l["_long"]
                if long and long != l["shortName"]:
                    # "Soir Franconville Sud" (already starts with the short name) or "100 <long>"
                    named = long.startswith(l["shortName"]) or l["shortName"] in long.split()
                    l["_label"] = long if named else f"{l['shortName']} {long}"
                else:
                    l["_label"] = f"{l['shortName']} {l['_net']}".strip()
                labels[l["_label"]] += 1
        for l in g["lines"]:
            if labels.get(l.get("_label", ""), 0) > 1:
                l["_label"] += f" ({l['id']})"
        g["lines"] = [dict(out_line(l), **({"label": l["_label"]} if "_label" in l else {}))
                      for l in g["lines"]]
        if len(g["lines"]) > MENU_MAX + 10:
            log(f"  warning: bus group {g['name']} has {len(g['lines'])} lines")
    # The device finds lines by raw text search (idfmFindLine): no brace inside the strings.
    for g in groups:
        for l in g["lines"]:
            for k in ("shortName", "label"):
                if k in l:
                    l[k] = l[k].replace("{", "(").replace("}", ")")
    return groups


if __name__ == "__main__":
    sys.exit(main())
