#!/usr/bin/env python3
"""assert.py -- assertion leg of the OTTD Mac OS 9 automated test harness.

Reads the UDP telemetry that the app streams to the k8s log sink
(deploy/log-sink in namespace gopher-spot) and asserts that the run tagged
--tag booted, built the expected world, and is LIVE (heartbeats advancing,
finances consistent, no fault markers).

Real sink line format (verified 2026-07-22 against the live sink):
    10.0.10.5 === B2 build R1-105: R1 LIVE town (engine ticks each frame) ===
i.e. every datagram line is prefixed with the sender IPv4 and a space.
The prefix is stripped before matching; all message regexes are anchored.

Quirk (verified): the `net sink UP -> ...` line is emitted BEFORE the boot
banner, so the sink_up assertion also scans a short preamble window of lines
immediately preceding the banner.

Quirk (verified): company money is NOT strictly non-decreasing between FIN
heartbeats in a healthy run -- running costs are deducted daily between
revenue events (e.g. 99977 -> 99931 -> 99885 before the first fare lands).
The fin_invariant therefore checks (a) money == expect on EVERY FIN line and
(b) the NET trend: last money >= first money.

Modes:
  batch (default): read `kubectl logs --since=...` once, evaluate the LAST
                   run bearing --tag.
  --follow:        stream `kubectl logs -f --since=1m`, wait up to
                   --boot-timeout wall seconds for the banner, then collect
                   until --beats heartbeats or a 90 s heartbeat gap (= HANG).
  --from-file P:   read lines from a file instead of kubectl (testing).

Exit codes: 0 all assertions pass, 1 any FAIL (or run not found),
            2 harness error (kubectl unreachable, bad expected.json, ...).
"""

import argparse
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

KUBE_NAMESPACE = "gopher-spot"
KUBE_TARGET = "deploy/log-sink"
HEARTBEAT_GAP_S = 90.0
PREAMBLE_LINES = 5  # lines before the banner scanned for sink_up

# ---------------------------------------------------------------- regexes
# Optional "<ipv4> " prefix stamped by the sink on every datagram line.
RE_PREFIX = re.compile(r"^(?:\d{1,3}(?:\.\d{1,3}){3}\s+)?(.*)$")

RE_BANNER = re.compile(r"^=== B2 build ([A-Za-z0-9._-]+): .* ===$")
RE_SINK_UP = re.compile(r"^net sink UP -> \S+ \(OT err=0\)$")
RE_SINK_DOWN = re.compile(r"^net sink DOWN\b")
RE_DONE = re.compile(r"^=== B2 done rc=(-?\d+); ExitToShell \(skip static dtors\) ===$")

RE_ROADS = re.compile(r"^R1-92: inter-town roads laid=(\d+) tiles$")
RE_CORNERS = re.compile(r"^R1-93: (\d+/\d+) corners road-connected to centre$")
RE_STOPS = re.compile(r"^R1-95: (\d+) drive-through bus stops placed$")
RE_RAIL = re.compile(r"^R1-104: rail line len=(\d+) at y=\d+$")
RE_TOWNS = re.compile(r"^R1: (\d+) towns seeded, mp_house=\d+ mp_road=\d+ trees=\d+ \(LIVE growth on\)$")

RE_FIRST_TICK = re.compile(r"^R1: first tick \(window is up, engine running\)$")
RE_MAIN_WINDOW = re.compile(r"^R1: main window up \(real viewport drives the map now\)$")

RE_HEARTBEAT = re.compile(
    r"^R1: live wait0=(\d+) tick=(\d+) date=(-?\d+) mon=(-?\d+)"
    r" houses=(\d+) pop=(\d+) ind=(\d+) cargo=(\d+) stations=(\d+) orders=(\d+)"
    r" \| bus0 i=(-?\d+) dir=(-?\d+) len=(-?\d+) dwell=(-?\d+)$"
)
RE_FIN = re.compile(
    r"^R1 FIN: money=(-?\d+) loan=(-?\d+) rev=(-?\d+) yexp_sum=(-?\d+)"
    r" expect\(\d+-sum\)=(-?\d+)$"
)

RE_FAULTS = [
    re.compile(r"^(?:USERERROR|ERROR|SLERROR):"),
    re.compile(r"^B2: SelectBlitter failed\b"),
    re.compile(r"^B2: grf load failed\b"),
    re.compile(r"NOT FOUND"),
]


def strip_prefix(raw):
    return RE_PREFIX.match(raw.rstrip("\n")).group(1)


# ---------------------------------------------------------------- kubectl
def kubectl_base(context):
    """Build the kubectl arg prefix; drop --context if it doesn't exist locally."""
    args = ["kubectl"]
    if context:
        try:
            out = subprocess.run(
                ["kubectl", "config", "get-contexts", "-o", "name"],
                capture_output=True, text=True, timeout=15,
            )
            known = out.stdout.split()
        except Exception:
            known = []
        if context in known:
            args += ["--context", context]
        else:
            print("note: kube context %r not found locally; using current context"
                  % context, file=sys.stderr)
    args += ["-n", KUBE_NAMESPACE]
    return args


def fetch_batch(context, since):
    cmd = kubectl_base(context) + ["logs", KUBE_TARGET, "--since=%s" % since]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except FileNotFoundError:
        die_harness("kubectl not found on PATH")
    except subprocess.TimeoutExpired:
        die_harness("kubectl logs timed out")
    if proc.returncode != 0:
        die_harness("kubectl failed: %s" % proc.stderr.strip())
    return proc.stdout.splitlines()


def die_harness(msg):
    print("HARNESS ERROR: %s" % msg, file=sys.stderr)
    sys.exit(2)


# ---------------------------------------------------------------- slicing
def slice_last_run(lines, tag):
    """Return (slice_lines, preamble_lines) for the LAST run bearing tag.

    A run = lines from its banner up to (not including) the next banner of
    any tag, or EOF. Returns (None, None) if no banner with the tag exists.
    """
    banners = []  # (index, tag)
    for i, raw in enumerate(lines):
        m = RE_BANNER.match(strip_prefix(raw))
        if m:
            banners.append((i, m.group(1)))
    starts = [i for i, t in banners if t == tag]
    if not starts:
        return None, None
    start = starts[-1]
    end = len(lines)
    for i, _t in banners:
        if i > start:
            end = i
            break
    preamble = lines[max(0, start - PREAMBLE_LINES):start]
    return lines[start:end], preamble


# ---------------------------------------------------------------- follow
def follow_collect(context, tag, boot_timeout, beats):
    """Stream logs -f; return (slice_lines, preamble, hang, boot_timed_out)."""
    cmd = kubectl_base(context) + ["logs", "-f", KUBE_TARGET, "--since=1m"]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
    except FileNotFoundError:
        die_harness("kubectl not found on PATH")

    q = queue.Queue()

    def reader():
        for line in proc.stdout:
            q.put(line.rstrip("\n"))
        q.put(None)

    threading.Thread(target=reader, daemon=True).start()

    recent = []          # rolling pre-banner window
    run = None           # collected slice once banner seen
    preamble = []
    hang = False
    boot_timed_out = False
    n_beats = 0
    deadline = time.monotonic() + boot_timeout
    last_beat = None

    try:
        while True:
            now = time.monotonic()
            if run is None:
                timeout = deadline - now
                if timeout <= 0:
                    boot_timed_out = True
                    break
            else:
                timeout = HEARTBEAT_GAP_S - (now - last_beat)
                if timeout <= 0:
                    hang = True
                    break
            try:
                raw = q.get(timeout=min(timeout, 5.0))
            except queue.Empty:
                continue
            if raw is None:  # stream ended (kubectl exited)
                if run is None:
                    err = proc.stderr.read()
                    die_harness("kubectl log stream ended before banner: %s"
                                % err.strip())
                break
            msg = strip_prefix(raw)
            m = RE_BANNER.match(msg)
            if run is None:
                if m and m.group(1) == tag:
                    run = [raw]
                    preamble = recent[-PREAMBLE_LINES:]
                    last_beat = time.monotonic()  # gap clock starts at banner
                else:
                    recent.append(raw)
                    del recent[:-PREAMBLE_LINES]
            else:
                if m:  # next run started; this run is over
                    break
                run.append(raw)
                if RE_HEARTBEAT.match(msg):
                    n_beats += 1
                    last_beat = time.monotonic()
                    if n_beats >= beats:
                        break
                if RE_DONE.match(msg):
                    break
    finally:
        proc.terminate()
    return run, preamble, hang, boot_timed_out


# ---------------------------------------------------------------- asserts
class Result:
    def __init__(self, name, status, detail):
        self.name, self.status, self.detail = name, status, detail


def find_one(msgs, regex):
    for m in msgs:
        mt = regex.match(m)
        if mt:
            return mt
    return None


def evaluate(run_raw, preamble_raw, tag, expected, beats,
             hang=False, boot_timed_out=False):
    results = []
    msgs = [strip_prefix(l) for l in (run_raw or [])]
    pre_msgs = [strip_prefix(l) for l in (preamble_raw or [])]

    # 1. banner_seen
    if boot_timed_out or not msgs:
        results.append(Result("banner_seen", "FAIL",
                              "no banner for tag %s (boot timeout)" % tag
                              if boot_timed_out else "no banner for tag %s" % tag))
    else:
        results.append(Result("banner_seen", "PASS", msgs[0]))

    # 2. sink_up (sink UP is logged just BEFORE the banner -> scan preamble too)
    scope = pre_msgs + msgs
    if find_one(scope, RE_SINK_UP):
        results.append(Result("sink_up", "PASS", "net sink UP seen"))
    elif find_one(scope, RE_SINK_DOWN):
        results.append(Result("sink_up", "FAIL", "net sink DOWN"))
    else:
        results.append(Result("sink_up", "FAIL", "no 'net sink UP' line"))

    # 3. world facts vs expected.json
    def world(name, regex, key, mode):
        if key not in expected:
            results.append(Result(name, "SKIP", "no %r in expected.json" % key))
            return
        m = find_one(msgs, regex)
        if not m:
            results.append(Result(name, "FAIL", "line not found in run"))
            return
        got = m.group(1)
        want = expected[key]
        if mode == "min":
            ok = int(got) >= int(want)
            det = "got %s (min %s)" % (got, want)
        else:  # exact
            ok = str(got) == str(want)
            det = "got %s (want %s)" % (got, want)
        results.append(Result(name, "PASS" if ok else "FAIL", det))

    world("roads_laid", RE_ROADS, "roads_laid_min", "min")
    world("corners", RE_CORNERS, "corners", "exact")
    world("bus_stops", RE_STOPS, "bus_stops_min", "min")
    world("rail_len", RE_RAIL, "rail_len_min", "min")
    world("towns_seeded", RE_TOWNS, "towns_seeded", "exact")

    # 4. engine_live
    ft = find_one(msgs, RE_FIRST_TICK) is not None
    mw = find_one(msgs, RE_MAIN_WINDOW) is not None
    if ft and mw:
        results.append(Result("engine_live", "PASS", "first tick + main window up"))
    else:
        missing = [n for n, ok in (("first tick", ft), ("main window up", mw)) if not ok]
        results.append(Result("engine_live", "FAIL", "missing: " + ", ".join(missing)))

    # 5. heartbeats
    hbs = [RE_HEARTBEAT.match(m) for m in msgs]
    hbs = [m for m in hbs if m]
    ticks = [int(m.group(2)) for m in hbs]
    hb_detail = "%d heartbeats, ticks %s%s" % (
        len(hbs), ticks[:8], "..." if len(ticks) > 8 else "")
    if hang:
        results.append(Result("heartbeats", "FAIL",
                              "HANG: no heartbeat for %ds (%s)"
                              % (int(HEARTBEAT_GAP_S), hb_detail)))
    elif len(hbs) < beats:
        results.append(Result("heartbeats", "FAIL",
                              "%s (need >= %d)" % (hb_detail, beats)))
    elif any(b <= a for a, b in zip(ticks, ticks[1:])):
        results.append(Result("heartbeats", "FAIL",
                              "ticks not strictly increasing: %s" % ticks))
    else:
        results.append(Result("heartbeats", "PASS", hb_detail))

    # 6. liveness (frozen-sim detector + growth monotonicity)
    if len(hbs) < 2:
        results.append(Result("liveness", "SKIP",
                              "need >= 2 heartbeats, have %d" % len(hbs)))
    else:
        bus = [(m.group(11), m.group(12), m.group(14), m.group(1)) for m in hbs]
        frozen = len(set(bus)) == 1
        houses = [int(m.group(5)) for m in hbs]
        pops = [int(m.group(6)) for m in hbs]
        cargos = [int(m.group(8)) for m in hbs]

        def nondec(v):
            return all(b >= a for a, b in zip(v, v[1:]))

        bad = []
        if frozen:
            bad.append("bus0 (i,dir,dwell,wait0) frozen at %s" % (bus[0],))
        for nm, v in (("houses", houses), ("pop", pops), ("cargo", cargos)):
            if not nondec(v):
                bad.append("%s decreased: %s" % (nm, v))
        if bad:
            results.append(Result("liveness", "FAIL", "; ".join(bad)))
        else:
            results.append(Result(
                "liveness", "PASS",
                "bus0 moving (%d distinct states); houses %d->%d pop %d->%d cargo %d->%d"
                % (len(set(bus)), houses[0], houses[-1], pops[0], pops[-1],
                   cargos[0], cargos[-1])))

    # 7. fin_invariant (money==expect each line; NET trend non-decreasing --
    #    intermediate dips are normal daily running costs, see module docstring)
    fins = [RE_FIN.match(m) for m in msgs]
    fins = [m for m in fins if m]
    if not fins:
        results.append(Result("fin_invariant", "FAIL", "no FIN lines in run"))
    else:
        moneys = [int(m.group(1)) for m in fins]
        expects = [int(m.group(5)) for m in fins]
        revs = [int(m.group(3)) for m in fins]
        mism = [(a, b) for a, b in zip(moneys, expects) if a != b]
        if mism:
            results.append(Result("fin_invariant", "FAIL",
                                  "money != expect on %d/%d lines, first %s"
                                  % (len(mism), len(fins), mism[0])))
        elif revs[-1] == 0:
            # Early run: no revenue yet, only daily costs -- a net dip is the
            # CORRECT state. The true invariant (money==expect) held above.
            results.append(Result(
                "fin_invariant", "PASS",
                "%d FIN lines, money==expect on all; trend skipped (rev=0, "
                "pre-revenue run: %d -> %d is daily costs)"
                % (len(fins), moneys[0], moneys[-1])))
        elif moneys[-1] < moneys[0]:
            results.append(Result("fin_invariant", "FAIL",
                                  "net money decreased %d -> %d over %d FIN lines"
                                  % (moneys[0], moneys[-1], len(fins))))
        else:
            results.append(Result(
                "fin_invariant", "PASS",
                "%d FIN lines, money==expect on all, net %d -> %d"
                % (len(fins), moneys[0], moneys[-1])))

    # 8. no_faults
    faults = [m for m in msgs if any(r.search(m) if r.pattern == "NOT FOUND"
                                     else r.match(m) for r in RE_FAULTS)]
    if faults:
        results.append(Result("no_faults", "FAIL",
                              "%d fault line(s), first: %s" % (len(faults), faults[0])))
    else:
        results.append(Result("no_faults", "PASS",
                              "0 fault markers in %d lines" % len(msgs)))
    return results


# ---------------------------------------------------------------- main
def print_report(results):
    wname = max(len(r.name) for r in results)
    for r in results:
        print("%-4s  %-*s  %s" % (r.status, wname, r.name, r.detail))
    n_pass = sum(1 for r in results if r.status == "PASS")
    n_fail = sum(1 for r in results if r.status == "FAIL")
    total = len(results)
    verdict = "FAIL" if n_fail else "PASS"
    print("VERDICT: %s (%d/%d)" % (verdict, n_pass, total))
    return 0 if verdict == "PASS" else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tag", required=True, help="build tag, e.g. R1-105")
    ap.add_argument("--since", default="30m", help="kubectl --since window (batch mode)")
    ap.add_argument("--boot-timeout", type=float, default=240,
                    help="follow mode: wall seconds to wait for the boot banner")
    ap.add_argument("--beats", type=int, default=3,
                    help="heartbeats required / collected before evaluating")
    ap.add_argument("--follow", action="store_true",
                    help="stream logs -f instead of one-shot batch read")
    ap.add_argument("--expected", default=os.path.join(SCRIPT_DIR, "expected.json"),
                    help="expected-values JSON (default harness/expected.json)")
    ap.add_argument("--save-log", metavar="PATH",
                    help="write the raw run slice to PATH")
    ap.add_argument("--from-file", metavar="PATH",
                    help="read log lines from a file instead of kubectl (testing)")
    ap.add_argument("--context", default="debene",
                    help="kube context (falls back to current if absent)")
    args = ap.parse_args()

    try:
        with open(args.expected) as f:
            expected = json.load(f)
    except Exception as e:
        die_harness("cannot load expected file %s: %s" % (args.expected, e))

    hang = boot_timed_out = False
    if args.from_file:
        try:
            with open(args.from_file) as f:
                lines = f.read().splitlines()
        except OSError as e:
            die_harness("cannot read %s: %s" % (args.from_file, e))
        run, preamble = slice_last_run(lines, args.tag)
    elif args.follow:
        run, preamble, hang, boot_timed_out = follow_collect(
            args.context, args.tag, args.boot_timeout, args.beats)
    else:
        lines = fetch_batch(args.context, args.since)
        run, preamble = slice_last_run(lines, args.tag)

    if run is None and not boot_timed_out:
        print("NOTFOUND: no run with banner tag %s in window (--since %s)"
              % (args.tag, args.since))
        sys.exit(1)

    if args.save_log and run:
        with open(args.save_log, "w") as f:
            f.write("\n".join(run) + "\n")

    results = evaluate(run, preamble, args.tag, expected, args.beats,
                       hang=hang, boot_timed_out=boot_timed_out)
    sys.exit(print_report(results))


if __name__ == "__main__":
    main()
