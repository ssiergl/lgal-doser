#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

"""Long-term test of an LGAL Doser on the network (24 hours by default). The script only watches:
the pumps run by their own schedules, so their tubes must end in a container. Every minute it
reads the device; at the end it checks the whole run and every dose the schedules planned in it.

Usage:
    tools/endurance.py HOST                         24 hours, a sample every minute
    tools/endurance.py HOST --hours 2 --log run.csv a shorter run, every sample into a CSV file

Checked: restarts (and their reason), a new core dump, outages and response times, free memory
and its trend, clock (valid, NTP sync, difference to this computer), network interface, 24 V
supply (board v1.1), the live connection (WebSocket frames, drops, longest gap), alarms, and
per pump that every scheduled dose ran, how much it dosed or why it was skipped. With the
engineering key also the drivers, the UART error rate, the chip temperature and the history
storage. Ctrl-C ends the run early and checks what was recorded so far.

Sign-in as in tools/acceptance.py: LGAL_API_KEY or LGAL_PASSWORD (otherwise asked), LGAL_ENG_KEY
for the hardware. Exit code: 0 passed (warnings allowed), 1 a check failed, 2 the device cannot be
reached or the sign-in failed at the start.
"""
import argparse
import csv
import datetime
import getpass
import os
import pathlib
import statistics
import sys
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from acceptance import CRASH_RESETS, HEAP_MIN_FREE, ApiError, Device, LoginError, Report, WebSocket  # noqa: E402

RESTART_SLACK_S = 30           # uptime may lag behind this computer's clock by this much (response times)
OUTAGE_FAIL_S = 180            # unanswered this long in one piece: failed
LATENCY_WARN_S = 5.0
HEAP_DROP_WARN = 16 * 1024     # free memory in the last hour this much below the first hour: a leak?
LARGEST_BLOCK_WARN = 16 * 1024
CLOCK_OFF_WARN_S = 10
WS_GAP_WARN_S = 30             # the firmware pushes the status at least every 10 s
WS_PING_S = 30                 # a client that only listens is the first one closed when the sockets run out
NTP_EXPECT_S = 4 * 3600        # a run this long should see an NTP sync
SLOW_POLL_S = 300              # schedules and hardware are read every 5 minutes
END_MARGIN_S = 15 * 60         # doses planned this close to the end are not checked
EARLY_S = 60                   # a dose event this much before its slot still belongs to it
DEFAULT_MAX_DELAY_MIN = 120    # engineering/scheduler max_delay_min (factory value)
BAD_ALARMS = {"fault", "driver_unavailable", "rtc_fault", "supply_missing"}


def fmt_dur(s):
    s = int(s)
    if s < 60:
        return f"{s} s"
    if s < 3600:
        return f"{s // 60} min"
    return f"{s // 3600} h {s % 3600 // 60:02d} min"


def local_dt(text):
    """'2026-11-18 07:43:13 CET' or '2026-11-18 07:43:13' -> naive datetime (device local time)"""
    return datetime.datetime.strptime(text[:19], "%Y-%m-%d %H:%M:%S")


# --- Live connection --------------------------------------------------------------------------------

class LiveWatch(threading.Thread):
    """Keeps a WebSocket open, counts status frames, drops and the longest time without a frame."""

    def __init__(self, dev):
        super().__init__(daemon=True)
        self.dev = dev
        self.frames = self.drops = 0
        self.connected_once = False
        self.max_gap = 0.0
        self.last = None
        self.error = None
        self.done = threading.Event()
        self.ws = None
        self.lock = threading.Lock()

    def run(self):
        while not self.done.is_set():
            try:
                ws = WebSocket(self.dev)
                if ws.status == 401 and not self.dev.key:  # the session ended with a restart
                    ws.close()
                    self.dev.login()
                    ws = WebSocket(self.dev)
                if ws.status != 101:
                    ws.close()
                    raise ConnectionError(f"answers {ws.status}")
                ws.s.settimeout(2 * WS_GAP_WARN_S)
                with self.lock:
                    self.ws = ws
                    self.connected_once = True
                pinged = time.monotonic()
                while not self.done.is_set():
                    if time.monotonic() - pinged >= WS_PING_S:
                        ws.send({"type": "ping"})
                        pinged = time.monotonic()
                    m = ws.recv()
                    if m and m.get("type") == "status":
                        now = time.time()
                        with self.lock:
                            if self.last is not None:
                                self.max_gap = max(self.max_gap, now - self.last)
                            self.last = now
                            self.frames += 1
            except (OSError, ValueError, LoginError, ApiError) as e:
                if self.done.is_set():
                    break
                with self.lock:
                    if self.ws:
                        self.drops += 1
                    self.ws = None
                    self.error = str(e)
                self.done.wait(5)

    def stop(self):
        self.done.set()
        with self.lock:
            if self.ws:
                self.ws.close()
            if self.last is not None:
                self.max_gap = max(self.max_gap, time.time() - self.last)

    def state(self):
        with self.lock:
            return self.frames, self.drops, self.max_gap


# --- Doses ------------------------------------------------------------------------------------------

def match_doses(slots, events, start, end, late_max_s):
    """Assigns the history events of the schedule to the planned slots of one pump.

    slots: [(datetime, ml)] planned (device local time); events: history events of kind dose
    (source schedule) and skip, any order; start/end: the run (device local time). Slots from
    start - late_max to end take part, but only those between start and end - END_MARGIN_S are
    reported: the others absorb the events that belong to them. An event belongs to the latest
    open slot before it (a dose may start up to late_max after its slot); a skip with "count"
    covers that many slots, and a "missed" skip (written after an outage) any number of hours
    after them. Returns {"planned", "done", "ml", "short", "skipped": {code: n},
    "failed": {code: n}, "missing": [datetime], "extra": n}."""
    lo, hi = start - datetime.timedelta(seconds=late_max_s), end
    first, last = start, end - datetime.timedelta(seconds=END_MARGIN_S)
    plan = [[t, None] for t in sorted({t for t, _ml in slots if lo < t <= hi})]
    evs = sorted(({**e, "_t": local_dt(e["local"]), "_left": max(1, e.get("count", 1)) if e["kind"] == "skip" else 1}
                  for e in events), key=lambda e: e["_t"])
    out = {"planned": 0, "done": 0, "ml": 0.0, "short": 0, "skipped": {}, "failed": {}, "missing": [], "extra": 0}
    early = datetime.timedelta(seconds=EARLY_S)
    late = datetime.timedelta(seconds=late_max_s)
    for e in evs:
        missed = e["kind"] == "skip" and e["code"] == "missed"
        for slot in reversed(plan):
            if not e["_left"]:
                break
            if slot[1] is not None or slot[0] > e["_t"] + early:
                continue
            if e["_t"] - slot[0] > late and not missed:
                break
            slot[1] = e
            e["_left"] -= 1
    for t, hit in plan:
        if not first < t <= last:
            continue
        out["planned"] += 1
        if not hit:
            out["missing"].append(t)
        elif hit["kind"] == "skip":
            out["skipped"][hit["code"]] = out["skipped"].get(hit["code"], 0) + 1
        elif hit["code"] == "success":
            out["done"] += 1
            out["ml"] += hit.get("done_ml", 0.0)
            target = hit.get("target_ml", 0.0)
            if abs(hit.get("done_ml", 0.0) - target) > max(0.01, 0.01 * target):
                out["short"] += 1
        else:
            out["failed"][hit["code"]] = out["failed"].get(hit["code"], 0) + 1
    out["extra"] = sum(1 for e in evs if e["kind"] == "dose" and e["_left"] > 0 and first < e["_t"] <= last)
    return out


# --- The run ----------------------------------------------------------------------------------------

class Run:
    def __init__(self, dev, log=None):
        self.dev, self.log = dev, log
        self.samples = []            # dicts of the answered samples
        self.unanswered = 0
        self.down_since = None
        self.outages = []            # (seconds, first error)
        self.restarts = []           # (host time, reset reason)
        self.versions = set()
        self.ota_states = set()
        self.coredump_at_start = None
        self.coredump_new = False
        self.slots = {}              # (pump, date) -> [(datetime, ml)]
        self.modes = {}              # pump -> set of modes seen
        self.fitted = []
        self.driver_problems = {}    # (pump, text) -> first host time
        self.uart = {}               # bus -> (first, last) counters
        self.temps = []
        self.dropped = []            # history_dropped values
        self.max_delay_min = DEFAULT_MAX_DELAY_MIN
        self.slow_at = 0.0
        self.writer = None
        if log:
            self.log_file = open(log, "w", newline="")
            self.writer = csv.writer(self.log_file)
            self.writer.writerow(["time", "device_utc", "uptime_s", "heap_free", "heap_min_free", "heap_largest_block",
                                  "response_ms", "network", "eth_link", "time_valid", "supply_v", "ws_frames",
                                  "ws_drops", "chip_temp_c"])

    def note(self, text):
        print(f"  {time.strftime('%H:%M')}  {text}", flush=True)

    def sample(self, live):
        t0 = time.time()
        try:
            info = self.dev.json("/api/v1/system/info")
        except (OSError, ApiError, ValueError, LoginError) as e:
            self.unanswered += 1
            if self.down_since is None:
                self.down_since = (t0, str(e))
                self.note(f"the device does not answer: {e}")
            return None
        lat = time.time() - t0
        if self.down_since is not None:
            gone = t0 - self.down_since[0]
            self.outages.append((gone, self.down_since[1]))
            self.note(f"the device answers again after {fmt_dur(gone)}")
            self.down_since = None
        sysi, tm, net, fw = info["system"], info["time"], info["network"], info["firmware"]
        prev = self.samples[-1] if self.samples else None
        if prev and sysi["uptime_s"] + RESTART_SLACK_S < prev["uptime_s"] + (t0 - prev["t"]):
            reason = sysi.get("reset_reason", "?")
            self.restarts.append((t0, reason))
            self.note(f"the device restarted ({reason}" + (", a crash" if reason in CRASH_RESETS else "") + ")")
        if fw["version"] not in self.versions and self.versions:
            self.note(f"firmware changed to {fw['version']}")
        self.versions.add(fw["version"])
        self.ota_states.add(fw.get("ota_state"))
        if self.coredump_at_start is None:
            self.coredump_at_start = bool(sysi.get("coredump"))
        elif sysi.get("coredump") and not self.coredump_at_start and not self.coredump_new:
            self.coredump_new = True
            self.note("a core dump was recorded (crash)")
        if prev and prev["network"] != net.get("active"):
            self.note(f"network: {prev['network']} → {net.get('active')}")
        if prev and prev["time_valid"] and not tm.get("valid"):
            self.note("the device lost its time")
        frames, drops, _ = live.state() if live else (0, 0, 0)
        s = {"t": t0, "utc": tm.get("utc"), "local": tm.get("local"), "uptime_s": sysi["uptime_s"],
             "heap_free": sysi.get("heap_free", 0), "heap_min_free": sysi.get("heap_min_free", 0),
             "largest": sysi.get("heap_largest_block", 0), "lat": lat, "network": net.get("active"),
             "eth_link": net.get("eth_link"), "time_valid": bool(tm.get("valid")),
             "ntp": tm.get("last_ntp_sync"), "ntp_server": tm.get("ntp_server"),
             "offset": (tm["utc"] - t0) if tm.get("valid") and tm.get("utc") else None,
             "supply": info.get("supply"), "board_rev": info.get("board_rev"), "temp": None}
        if time.time() >= self.slow_at:
            self.slow_at = time.time() + SLOW_POLL_S
            self.slow(s)
        self.samples.append(s)
        if self.writer:
            sup = s["supply"] or {}
            self.writer.writerow([time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t0)), s["utc"], s["uptime_s"],
                                  s["heap_free"], s["heap_min_free"], s["largest"], round(lat * 1000),
                                  s["network"], s["eth_link"], s["time_valid"], sup.get("volts", ""), frames, drops,
                                  "" if s["temp"] is None else s["temp"]])
            self.log_file.flush()
        return s

    def slow(self, s):
        """Every few minutes: the plans of today, and with the engineering key the hardware"""
        try:
            pumps = self.dev.json("/api/v1/pumps")["pumps"]
            self.fitted = [p["pump"] for p in pumps if p.get("fitted")]
            for p in pumps:
                if not p.get("fitted"):
                    continue
                self.modes.setdefault(p["pump"], set()).add(p["mode"])
                today = self.dev.json(f"/api/v1/pumps/schedule?pump={p['pump']}").get("today") or {}
                if today.get("date"):
                    self.slots[(p["pump"], today["date"])] = [
                        (datetime.datetime.strptime(f"{today['date']} {x['time']}", "%Y-%m-%d %H:%M"), x["ml"])
                        for x in today.get("slots", [])]
            if not self.dev.eng_key:
                return
            self.max_delay_min = self.dev.json("/api/v1/engineering/scheduler").get("max_delay_min",
                                                                                    self.max_delay_min)
            hw = self.dev.json("/api/v1/hw/status")
            for p in hw.get("pumps", []):
                if not p.get("fitted"):
                    continue
                f = p.get("flags") or {}
                for problem in (["does not answer on UART"] if not p.get("present") else []) + \
                        ([p["fault"]] if p.get("fault") else []) + \
                        [k for k in ("overtemp", "overtemp_warn", "short") if f.get(k)]:
                    if (p["pump"], problem) not in self.driver_problems:
                        self.driver_problems[(p["pump"], problem)] = time.time()
                        self.note(f"driver {p['pump']}: {problem}")
            for b in hw.get("uart", []):
                c = (b.get("transfers", 0), b.get("timeouts", 0) + b.get("crc_errors", 0) + b.get("echo_errors", 0))
                first = self.uart.get(b["bus"], (c, c))[0]
                self.uart[b["bus"]] = (first, c)
            tel = self.dev.json("/api/v1/engineering/telemetry")
            if tel.get("chip_temp_c") is not None:
                s["temp"] = tel["chip_temp_c"]
                self.temps.append(tel["chip_temp_c"])
            self.dropped.append((tel.get("storage") or {}).get("history_dropped", 0))
        except (OSError, ApiError, ValueError, LoginError, KeyError) as e:
            self.note(f"reading the plans or the hardware failed: {e}")

    def status_line(self, start, live):
        s = self.samples[-1] if self.samples else None
        frames, drops, _ = live.state() if live else (0, 0, 0)
        up = f"up {fmt_dur(s['uptime_s'])}, {s['heap_free'] // 1024} KiB free" if s else "no answer"
        self.note(f"[{fmt_dur(time.time() - start)}] {up}, {len(self.restarts)} restarts, "
                  f"{len(self.outages) + (self.down_since is not None)} outages, live {frames} frames / {drops} drops")

    def history(self, frm, to):
        out, offset = [], 0
        while True:
            j = self.dev.json(f"/api/v1/history?from={frm}&to={to}&kind=dose,skip,alarm&limit=500&offset={offset}")
            out += j["events"]
            if not j.get("more"):
                return out
            offset += len(j["events"])

    # --- evaluation ---------------------------------------------------------------------------------

    def evaluate(self, r, live, start, end):
        r.section(f"Run: {fmt_dur(end - start)}, {len(self.samples)} samples")
        S = self.samples
        if not S:
            r.fail("device answers", "not a single answer")
            return
        if self.down_since is not None:
            self.outages.append((end - self.down_since[0], self.down_since[1]))
        longest = max((o[0] for o in self.outages), default=0)
        lats = sorted(x["lat"] for x in S)
        p95 = lats[min(len(lats) - 1, int(0.95 * len(lats)))]
        if longest >= OUTAGE_FAIL_S:
            r.fail("device answers", f"{len(self.outages)} outages, the longest {fmt_dur(longest)} "
                                     f"({self.outages[0][1]})")
        elif self.unanswered:
            r.warn("device answers", f"{self.unanswered} of {self.unanswered + len(S)} samples unanswered")
        else:
            r.ok("device answers", f"all {len(S)} samples")
        (r.ok if lats[-1] <= LATENCY_WARN_S else r.warn)(
            "response time", f"95 % within {p95 * 1000:.0f} ms, the slowest {lats[-1] * 1000:.0f} ms")
        if self.restarts:
            r.fail("no restart", ", ".join(f"{time.strftime('%H:%M', time.localtime(t))} {why}"
                                           for t, why in self.restarts))
        else:
            r.ok("no restart", f"up {fmt_dur(S[-1]['uptime_s'])}")
        if self.coredump_new:
            r.fail("no crash", "a core dump was recorded: GET /api/v1/system/coredump")
        elif self.coredump_at_start:
            r.warn("no crash", "none in this run; an older core dump is still stored")
        else:
            r.ok("no crash")
        if len(self.versions) > 1:
            r.warn("firmware", "changed during the run: " + ", ".join(sorted(self.versions)))

        r.section("Memory")
        low = min(x["heap_free"] for x in S)
        dev_min = min(x["heap_min_free"] for x in S)
        (r.ok if dev_min >= HEAP_MIN_FREE else r.warn)(
            "free memory", f"at least {low // 1024} KiB in the samples, {dev_min // 1024} KiB at the lowest point")
        if S[-1]["t"] - S[0]["t"] >= 3 * 3600:
            a = statistics.median(x["heap_free"] for x in S if x["t"] < S[0]["t"] + 3600)
            b = statistics.median(x["heap_free"] for x in S if x["t"] > S[-1]["t"] - 3600)
            (r.ok if a - b <= HEAP_DROP_WARN else r.warn)(
                "no leak", f"first hour {a / 1024:.0f} KiB, last hour {b / 1024:.0f} KiB free (median)"
                + ("" if a - b <= HEAP_DROP_WARN else ": memory is getting less"))
        else:
            r.skip("no leak", "the trend needs at least 3 hours")
        big = min(x["largest"] for x in S)
        (r.ok if big >= LARGEST_BLOCK_WARN else r.warn)("largest free block", f"at least {big // 1024} KiB")

        r.section("Clock and network")
        invalid = sum(1 for x in S if not x["time_valid"])
        r.check(not invalid, "valid time", "all along", f"missing in {invalid} samples")
        offs = [abs(x["offset"]) for x in S if x["offset"] is not None]
        if offs:
            (r.ok if max(offs) <= CLOCK_OFF_WARN_S else r.warn)(
                "clock", f"at most {max(offs):.0f} s from this computer's clock")
        if S[-1]["ntp_server"]:
            synced = S[-1]["ntp"] and S[-1]["ntp"] != S[0]["ntp"]
            if synced:
                r.ok("NTP sync", f"last at {time.strftime('%H:%M', time.localtime(S[-1]['ntp']))}")
            elif S[-1]["t"] - S[0]["t"] >= NTP_EXPECT_S:
                r.warn("NTP sync", "none during the run")
            else:
                r.skip("NTP sync", "none yet, the run is short")
        else:
            r.skip("NTP sync", "no time server set")
        nets = [x["network"] for x in S]
        changes = sum(1 for a, b in zip(nets, nets[1:]) if a != b)
        if changes:
            r.warn("network", f"{changes} changes of the interface: " + " → ".join(dict.fromkeys(nets)))
        else:
            r.ok("network", f"{nets[0]} all along")
        if S[-1].get("board_rev") == "1.1":
            volts = [x["supply"].get("volts", 0) for x in S if x["supply"]]
            bad = sum(1 for x in S if (x["supply"] or {}).get("state") != "ok")
            r.check(not bad, "24 V supply", f"{min(volts):.1f} – {max(volts):.1f} V",
                    f"not ok in {bad} samples, {min(volts or [0]):.1f} – {max(volts or [0]):.1f} V")

        r.section("Live connection")
        frames, drops, gap = live.state() if live else (0, 0, 0)
        if not live or not live.connected_once:
            r.fail("WebSocket", f"never connected ({live.error if live else 'off'})")
        elif drops or gap > WS_GAP_WARN_S:
            r.warn("WebSocket", f"{frames} frames, {drops} drops, longest gap {gap:.0f} s")
        else:
            r.ok("WebSocket", f"{frames} frames, no drop, longest gap {gap:.0f} s")

        self.evaluate_doses(r, S)

        r.section("Hardware (engineering)")
        if not self.dev.eng_key:
            r.skip("drivers, UART, temperature", "no LGAL_ENG_KEY")
            return
        if self.driver_problems:
            for (pump, problem), t in sorted(self.driver_problems.items()):
                r.fail(f"driver {pump}", f"{problem} (first at {time.strftime('%H:%M', time.localtime(t))})")
        else:
            r.ok("drivers", "every fitted driver answered without a fault")
        for bus, (a, b) in sorted(self.uart.items()):
            n, err = b[0] - a[0], b[1] - a[1]
            (r.ok if err <= 0.01 * max(1, n) else r.warn)(f"UART bus {bus.split()[0]}", f"{n} transfers, {err} errors")
        if self.temps:
            r.ok("chip temperature", f"{min(self.temps):.0f} – {max(self.temps):.0f} °C")
        if self.dropped and self.dropped[-1] > self.dropped[0]:
            r.warn("history storage", f"{self.dropped[-1] - self.dropped[0]} events dropped during the run")

    def evaluate_doses(self, r, S):
        r.section("Doses by the schedule")
        if not S[0]["time_valid"] or not S[-1]["time_valid"]:
            r.skip("doses", "the device had no valid time")
            return
        start, end = local_dt(S[0]["local"]), local_dt(S[-1]["local"])
        try:
            events = self.history(S[0]["utc"], S[-1]["utc"])
        except (OSError, ApiError, ValueError) as e:
            r.fail("history", str(e))
            return
        late = (self.max_delay_min + 10) * 60
        for pump in self.fitted:
            name = f"pump {pump}"
            modes = self.modes.get(pump, set())
            if modes != {"auto"}:
                r.skip(name, "mode changed during the run" if len(modes) > 1 else f"mode {', '.join(modes)}: no schedule")
                continue
            slots = [x for (p, _d), v in self.slots.items() if p == pump for x in v]
            mine = [e for e in events if e["pump"] == pump and
                    (e["kind"] == "skip" or (e["kind"] == "dose" and e.get("source") == "schedule"))]
            m = match_doses(slots, mine, start, end, late)
            if not m["planned"]:
                r.skip(name, "no dose planned in the run")
                continue
            detail = f"{m['done']} of {m['planned']} doses, {m['ml']:.2f} ml"
            notes = []
            if m["skipped"]:
                notes.append("skipped: " + ", ".join(f"{n}× {c}" for c, n in m["skipped"].items()))
            if m["short"]:
                notes.append(f"{m['short']} not the full amount")
            if m["extra"]:
                notes.append(f"{m['extra']} doses outside the plan")
            if m["failed"] or m["missing"]:
                bad = [f"{n}× {c}" for c, n in m["failed"].items()]
                if m["missing"]:
                    bad.append(f"{len(m['missing'])} without an entry (first {m['missing'][0]:%d.%m. %H:%M})")
                r.fail(name, f"{detail}; " + "; ".join(bad + notes))
            elif notes:
                r.warn(name, f"{detail}; " + "; ".join(notes))
            else:
                r.ok(name, detail)
        alarms = [e for e in events if e["kind"] == "alarm"]
        if not alarms:
            r.ok("no alarm")
        for e in alarms:
            who = f"pump {e['pump']}" if e["pump"] else "device"
            (r.fail if e["code"] in BAD_ALARMS else r.warn)(f"alarm {e['code']}", f"{who}, {e['local'][:16]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", help="address of the device, e.g. lgal-doser-e4f5.local or 192.168.1.50")
    ap.add_argument("--hours", type=float, default=24.0, help="length of the run (default 24)")
    ap.add_argument("--interval", type=float, default=60.0, help="seconds between two samples (default 60, at least 1)")
    ap.add_argument("--report", type=float, default=15.0, help="minutes between two status lines (default 15)")
    ap.add_argument("--log", type=pathlib.Path, help="write every sample to this CSV file")
    args = ap.parse_args()
    if args.hours <= 0 or args.interval < 1:
        ap.error("--hours must be positive and --interval at least 1 s")

    key, password = os.environ.get("LGAL_API_KEY"), os.environ.get("LGAL_PASSWORD")
    if not key and not password:
        if not sys.stdin.isatty():
            sys.exit("Sign-in missing: LGAL_API_KEY or LGAL_PASSWORD")
        password = getpass.getpass("Admin password of the device: ")
    dev = Device(args.host, key=key, password=password, eng_key=os.environ.get("LGAL_ENG_KEY"))
    try:
        dev.login()
    except LoginError as e:
        print(f"Cannot sign in: {e}")
        sys.exit(2)
    except OSError as e:
        print(f"The device does not answer: {e}")
        sys.exit(2)

    run = Run(dev, args.log)
    live = LiveWatch(dev)
    print(f"LGAL Doser long-term test: {args.host}", flush=True)
    start = time.time()
    first = run.sample(None)
    if not first:
        print("The device does not answer.")
        sys.exit(2)
    live.start()
    planned = sum(len(v) for v in run.slots.values())
    print(f"  firmware {next(iter(run.versions))}, up {fmt_dur(first['uptime_s'])}")
    print(f"  {args.hours:g} h, a sample every {args.interval:g} s; pumps {', '.join(map(str, run.fitted)) or '–'}, "
          f"{planned} doses planned today. Ctrl-C ends the run early.", flush=True)
    stop_at = start + args.hours * 3600
    next_sample, next_report = start + args.interval, start + args.report * 60
    try:
        while True:
            now = time.time()
            if now >= stop_at:
                break
            time.sleep(max(0.0, min(next_sample, stop_at) - now))
            if time.time() >= stop_at:
                break
            run.sample(live)
            next_sample += args.interval
            if time.time() >= next_report:
                run.status_line(start, live)
                next_report += args.report * 60
    except KeyboardInterrupt:
        print("\n  stopped early", flush=True)
    run.sample(live)  # the state at the end
    live.stop()
    end = time.time()
    r = Report()
    try:
        run.evaluate(r, live, start, end)
    except (OSError, ApiError) as e:
        r.fail("evaluation", f"the device stopped answering: {e}")
    finally:
        dev.lock_engineering()
    c = r.counts
    print(f"\n{c['PASS']} passed, {c['WARN']} warnings, {c['FAIL']} failed, {c['SKIP']} skipped")
    sys.exit(1 if c["FAIL"] else 0)


if __name__ == "__main__":
    main()
