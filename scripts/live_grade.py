"""Grade one DLL session and its most recent mission restart, using only observed evidence."""

import json
import math
import re
from collections import Counter, defaultdict

if __package__:
    from . import live_mission as mission
else:
    import live_mission as mission

# A row needs an actual action. Rejections and mere entry into a hook do not count.
CHECKS = {
    mission.INF: {
        "grenade_dodge": ("dodges",),
        "soft_cover_lof": ("soft_lof",),
        "tactical_infantry": ("tact_ord",),
        "auto_smoke": ("smoke_rel",),
        "auto_medic": ("med_rev",),
        "squad_logistics": ("squad_ammo", "squad_bandages"),
        "auto_resupply": ("sup_refill",),
        "lull_reload": ("reload",),
    },
    mission.ARM: {
        "tank_facing": ("tank_face",),
        "penetration_at": ("at_ammo", "at_holds", "at_volumes"),
        "auto_smoke": ("smoke_veh",),
        "tank_auto_repair": ("rep_tt", "rep_full"),
        "tank_crew_fill": ("crew_total",),
        "safe_disembark": ("safe_doors",),
        "transport_unload": ("transport_unloads",),
        "tank_crusher": ("tank_disc", "tank_rev"),
    },
    mission.CLOSE: {"fast_weapon_swap": ("fast_swaps",), "lof_detour": ("det_inf",)},
}

# Per short scenario window. These count issued actions and support trips.
# Path-cost evaluations, target scoring and rejection callbacks run per candidate.
ACTION_LIMITS = {
    "crew_total": 50,
    "transport_unloads": 12,
    "rep_assign": 64,
    "sup_trip": 32,
    "med_claim": 60,
    "squad_ammo": 80,
    "squad_bandages": 80,
    "tank_face": 60,
    "det_inf": 64,
    "fast_swaps": 120,
    "tact_ord": 150,
    "smoke_rel": 40,
    "obj_pursue": 64,
    "obj_cap": 64,
}


def latest(lines):
    header, rows, tuning, errors = {}, [], {}, []
    for i, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
            if not isinstance(row, dict) or not isinstance(row.get("event"), str):
                raise TypeError("expected an object with an event name")
            for key in ("ms", "sim_tick"):
                if type(row.get(key)) is not int or row[key] < 0:
                    raise ValueError(f"invalid {key}")
            kind = row["event"]
            if kind == "session_start":
                header, rows, tuning, errors = {}, [], {}, []
                if (
                    type(row.get("local_day_ms")) is not int
                    or not 0 <= row["local_day_ms"] < 86400000
                ):
                    raise ValueError("invalid local clock")
                header, rows, tuning, errors = row, [], {}, []
            elif kind == "scene_reset":
                rows, errors = list(tuning.values()), []
            elif header:
                if kind == "module_event" and (
                    not isinstance(row.get("module"), str)
                    or not isinstance(row.get("action"), str)
                    or type(row.get("mid")) is not int
                ):
                    raise ValueError("invalid module event")
                if kind == "stats_delta" and (
                    not isinstance(row.get("counters"), dict)
                    or any(
                        type(v) is not int or v <= 0 for v in row["counters"].values()
                    )
                    or type(row.get("since_tick")) is not int
                    or not 0 <= row["since_tick"] <= row["sim_tick"]
                ):
                    raise ValueError("invalid stats interval or counters")
                if kind == "actor_snapshot":
                    for key in (
                        "actor",
                        "mid",
                        "player",
                        "control",
                        "move_mode",
                        "fire_mode",
                        "stats_tick",
                    ):
                        if type(row.get(key)) is not int:
                            raise ValueError(f"missing or invalid snapshot {key}")
                    for key in ("pos_x", "pos_y", "ticks_per_second"):
                        if not finite(row.get(key)):
                            raise ValueError(f"missing or invalid snapshot {key}")
                if kind == "module_event" and row.get("module") == "tuning":
                    tuning[row["action"]] = row
                rows.append(row)
        except (KeyError, ValueError, TypeError) as e:
            errors.append(f"JSONL line {i}: {e}")
    return header, rows, errors


def finite(x):
    return type(x) in (int, float) and math.isfinite(x)


def pointer(line):
    m = re.search(r"actor=(?:0x)?([0-9a-f]+)\b", line, re.IGNORECASE)
    return int(m[1], 16) if m else None


def log_in_window(line, header, start, end):
    m = re.search(r"^\[(\d\d):(\d\d):(\d\d)\.(\d{3})\]", line)
    if not m or "local_day_ms" not in header:
        return False
    h, minute, sec, milli = map(int, m.groups())
    ms = (
        ((h * 60 + minute) * 60 + sec) * 1000 + milli - header["local_day_ms"]
    ) % 86400000
    return start <= ms + header["ms"] <= end


def desant_transfers(log, header, samples):
    windows = {}
    for r in samples:
        address = r.get("address")
        if address:
            lo, hi = windows.get(address, (r["ms"], r["ms"]))
            windows[address] = min(lo, r["ms"]), max(hi, r["ms"])
    count = 0
    for line in log.splitlines():
        match = re.search(
            r"\bTANK CREW FILL #\d+: tank=0x([0-9a-fA-F]+) human=0x[0-9a-fA-F]+ "
            r"seat[1-4]\(\d+\) -> (?:driver|gunner|gunner3|commander)\(\d+\).*\breturning=0\b", line
        )
        if match:
            window = windows.get(int(match[1], 16))
            if window and log_in_window(line, header, *window):
                count += 1
    return count


def measured(p, actors):
    subject = actors[p.mids.get("subject", p.mids.get("mg"))]
    if p.probe == "grenade_danger_radius_m":
        values = [
            r["grenade_land_m"] for r in subject if finite(r.get("grenade_land_m"))
        ]
        return min(values, default=None)
    # Use the earliest co-sampled pair before autonomous movement/rotation.
    target = actors[p.mids.get("target", p.mids.get("truck"))]
    by_tick = {r["sim_tick"]: r for r in target}
    for a in subject:
        b = by_tick.get(a["sim_tick"])
        if b is None:
            continue
        dx, dy = b["pos_x"] - a["pos_x"], b["pos_y"] - a["pos_y"]
        if p.probe.endswith("_deg"):
            if finite(a.get("heading_deg")):
                return abs(
                    math.remainder(
                        a["heading_deg"] - math.degrees(math.atan2(dy, dx)), 360
                    )
                )
        else:
            return math.hypot(dx, dy) / mission.SCALE
    return None


def grade(lines, ai_log, game_log=None, suite="full", tuning=None):
    header, rows, errors = latest(lines)
    checks = []
    metrics = []
    observations = []
    overruns = []
    phase_counts = {}

    def check(phase, name, ok, evidence):
        checks.append(
            {"phase": phase, "check": name, "passed": bool(ok), "evidence": evidence}
        )

    check(
        "run",
        "telemetry identity",
        header.get("exe") == "call_to_arms.exe"
        and header.get("build") == 26734796
        and header.get("sha256") == mission.SHA,
        "latest session_start",
    )
    check("run", "JSONL integrity", not errors, ", ".join(errors[:3]) or "valid")
    log = ai_log.rsplit("=== goh_ai InitThread starting", 1)[-1]
    rtti = re.search(
        r"RTTI vtable audit: checked=(\d+) matched=(\d+) mismatched=0 missing=0", log
    )
    hooks = re.search(
        r"=== (\d+) hooks active \(slot23=1 combat=(\d+) infantry=(\d+) tanks=(\d+)\b",
        log,
    )
    total, combat, infantry, tanks = map(int, hooks.groups()) if hooks else (0, 0, 0, 0)
    ready = (
        f"PE SHA-256 verified: {mission.SHA} (build 26734796)" in log
        and re.search(r"Initial state: remote=0\b", log)
        and total >= 73
        and total == 1 + combat + infantry + tanks
        and rtti
        and int(rtti[1]) > 0
        and rtti[1] == rtti[2]
        and "ABORT:" not in log
    )
    check(
        "run",
        "DLL initialization",
        ready,
        f"host, fingerprint, RTTI and {total} reported hooks",
    )
    snapshots = [r for r in rows if r["event"] == "actor_snapshot"]
    actors = defaultdict(list)
    for r in snapshots:
        actors[r["mid"]].append(r)
    phases = mission.phases_for(suite, tuning)
    expected = [f"{edge}_{p.name}" for p in phases for edge in ("BEGIN", "END")] + [
        "DONE_" + suite
    ]
    if game_log is not None:
        markers = re.findall(r"\bGOH_L1_((?:BEGIN|END|DONE)_\w+)\b", game_log)
        check(
            "run",
            "GEM boundaries",
            markers[-len(expected) :] == expected,
            f"{len(markers)} markers, expecting {len(expected)} in order",
        )

    previous_end = -1
    for p in phases:
        group = [r for r in snapshots if r["mid"] in p.mids.values()]
        ticks = [r["sim_tick"] for r in group]
        start, end = min(ticks, default=-1), max(ticks, default=-1)
        check(
            p.name,
            "observation window",
            previous_end < start < end,
            f"ticks {start}..{end}",
        )
        previous_end = max(previous_end, end)
        delta = Counter()
        for r in rows:
            if (
                r["event"] == "stats_delta"
                and start <= r["since_tick"] <= r["sim_tick"] <= end
            ):
                delta.update(r["counters"])
        if start < end:
            phase_counts[p.name] = delta
        high = {k for k, limit in ACTION_LIMITS.items() if delta[k] > limit}
        for k in sorted(high):
            overruns.append(
                {
                    "phase": p.name,
                    "counter": k,
                    "count": delta[k],
                    "limit": ACTION_LIMITS[k],
                }
            )
        reported = set()
        for name, keys in CHECKS.get(p.name, {}).items():
            bounds = high.intersection(keys)
            reported.update(bounds)
            ok = any(delta[k] for k in keys) and not bounds
            evidence = ", ".join(
                f"{k}={delta[k]}" + (f" (limit {ACTION_LIMITS[k]})" if k in bounds else "")
                for k in keys
            )
            if p.name == mission.ARM and name == "tank_crew_fill":
                transfers = desant_transfers(log, header, actors[p.mids["desant_tank"]])
                ok = ok and delta["crew_total"] > delta["crew_internal"] and transfers > 0
                evidence += f", crew_internal={delta['crew_internal']}, desant hull transfers={transfers}"
            check(p.name, name, ok, evidence)
        if p.name == mission.ARM:
            friendly = actors[p.mids["tank"]]
            for weapon, role in (("cannon", "hostile_tank"), ("mg", "lone")):
                addresses = {r.get("address") for r in actors[p.mids[role]]} - {0, None}
                count = sum(r.get(weapon + "_target") in addresses for r in friendly)
                check(
                    p.name,
                    f"target_prioritization/{weapon}",
                    count,
                    f"{count} samples targeting {role}",
                )
        if p.name == mission.CLOSE:
            mobile_roles = ("hold", "mobile", "molotov")
            mids = {p.mids[role] for role in mobile_roles}
            ptrs = {r.get("address") for mid in mids for r in actors[mid]} - {0, None}
            hit = any(
                r.get("module") == "close_grenade"
                and r.get("action") == "release"
                and r.get("mid") in mids
                and start <= r["sim_tick"] <= end
                for r in rows
            )
            if group and not hit:
                begin_ms, end_ms = (
                    min(r["ms"] for r in group),
                    max(r["ms"] for r in group),
                )
                hit = any(
                    "close_grenade release #" in line
                    and pointer(line) in ptrs
                    and log_in_window(line, header, begin_ms, end_ms)
                    for line in log.splitlines()
                )
            check(
                p.name,
                "close_grenade",
                hit,
                "matched release" if hit else "no scoped release",
            )
            for role, move, fire in (
                ("hold", 1, 0),
                ("hold_fire", 1, 1),
                ("mobile", 0, 0),
            ):
                samples = actors[p.mids[role]]
                good = len({r["sim_tick"] for r in samples}) >= 2 and all(
                    (r["player"], r["control"], r["move_mode"], r["fire_mode"])
                    == (0, 1, move, fire)
                    for r in samples
                )
                displacement = max(
                    (
                        math.hypot(
                            r["pos_x"] - samples[0]["pos_x"],
                            r["pos_y"] - samples[0]["pos_y"],
                        )
                        / mission.SCALE
                        for r in samples
                    ),
                    default=0,
                )
                check(
                    p.name,
                    "authority/" + role,
                    good and (not move or displacement <= (0.5 if role == "hold_fire" else 1.0)),
                    f"{len(samples)} samples, maximum displacement {displacement:.2f} m",
                )
        if p.probe:
            key = p.probe
            subject = actors[p.mids.get("subject", p.mids.get("mg"))]
            v = measured(p, actors)
            below = p.name.endswith("_below")
            want = not below if key.endswith("_deg") else below
            counter = mission.PROBES[key][-1]
            value = delta[counter]
            config_values = [
                r
                for r in rows
                if r.get("module") == "tuning" and r.get("action") == key
            ]
            prior = [
                r
                for r in config_values
                if group and r["ms"] <= min(s["ms"] for s in group)
            ]
            active = prior[-1].get("value") if prior else None
            changes = any(
                r.get("value") != active
                and group
                and min(s["ms"] for s in group)
                <= r["ms"]
                <= max(s["ms"] for s in group)
                for r in config_values
            )
            seconds = 0.0
            if len(subject) >= 2 and subject[0]["ticks_per_second"] > 0:
                seconds = (
                    subject[-1]["stats_tick"] - subject[0]["stats_tick"]
                ) / subject[0]["ticks_per_second"]
            correct_side = v is not None and (v < p.setting if below else v > p.setting)
            # The 2 m placement floor coincides with the minimum configurable
            # grenade threshold. A landing on that boundary is the inside probe.
            if below and p.distance == p.setting and v is not None:
                correct_side = v <= p.setting or math.isclose(v, p.setting, abs_tol=0.1)
            configured = finite(active) and math.isclose(
                active, p.setting, rel_tol=1e-6
            )
            ok = (
                configured
                and not changes
                and correct_side
                and seconds >= 8
                and (value > 0) == want
                and counter not in high
            )
            reported.add(counter)
            metrics.append(
                {
                    "probe": key,
                    "side": "below" if below else "above",
                    "threshold": p.setting,
                    "configured": active,
                    "placed": p.distance,
                    "measured": v,
                    "unit": "deg" if key.endswith("_deg") else "m",
                    "count": value,
                    "expected_action": want,
                    "observed_seconds": round(seconds, 2),
                }
            )
            check(
                p.name,
                key,
                ok,
                f"threshold={active}, planned={p.distance:g}, measured={v} {'deg' if key.endswith('_deg') else 'm'}, "
                f"actions={value}, expected={'>=1' if want else '0'}, {seconds:.1f}s"
                + (f", limit {ACTION_LIMITS[counter]}" if counter in high else ""),
            )
        for k in sorted(high - reported):
            check(
                p.name,
                "runaway/" + k,
                False,
                f"{k}={delta[k]} (limit {ACTION_LIMITS[k]})",
            )
    if suite != "calibration":
        vetoes = sum(d["ff_vetoes"] for d in phase_counts.values())
        observations.append(
            {
                "phase": "run",
                "check": "grenade_ff_guard",
                "evidence": (
                    f"ff_vetoes={vetoes} in observed combat windows. "
                    "Opportunistic observation of native grenade releases with an ally in the blast area."
                    if phase_counts
                    else "No combat window observed."
                ),
            }
        )
    if suite in ("full", "infantry"):
        # Pursuit is a live SP objective even when the MP flag capture engine is absent.
        # In a full run a pursuit can begin during armor contact. Keep the source window in the evidence.
        activity = [
            f"{phase} obj_pursue={d['obj_pursue']}, obj_cap={d['obj_cap']}"
            for phase, d in phase_counts.items()
            if d["obj_pursue"] or d["obj_cap"]
        ]
        check(
            "run",
            "tactical_objectives",
            activity,
            " / ".join(activity) or "obj_pursue=0, obj_cap=0",
        )
    witness = actors[(90 + mission.SUITES.index(suite)) * 100 + 1]
    check(
        "run",
        "mission completion",
        any(r["sim_tick"] > previous_end for r in witness),
        f"{len(witness)} completion witness samples",
    )
    return {
        "suite": suite,
        "passed": all(c["passed"] for c in checks),
        "checks": checks,
        "calibration": metrics,
        "observations": observations,
        "snapshots": len(snapshots),
        "actors": len({r["actor"] for r in snapshots}),
        "runaway_actions": overruns,
    }


def table(report):
    checks = report["checks"]
    rows = [
        (c["phase"], c["check"], "PASS" if c["passed"] else "FAIL", c["evidence"])
        for c in checks
    ]
    rows.extend(
        (o["phase"], o["check"], "INFO", o["evidence"])
        for o in report.get("observations", ())
    )
    rows.insert(0, ("Phase", "Check", "Result", "Evidence"))
    widths = [max(len(r[i]) for r in rows) for i in range(3)]
    lines = [
        "  ".join([*(r[i].ljust(widths[i]) for i in range(3)), r[3]]) for r in rows
    ]
    passed = sum(c["passed"] for c in checks)
    authority = [c["passed"] for c in checks if c["check"].startswith("authority/")]
    status = (
        ("preserved" if all(authority) else "failed") if authority else "not probed"
    )
    windows = [c["passed"] for c in checks if c["check"] == "observation window"]
    overruns = report.get("runaway_actions")
    thrashing = (
        len(overruns)
        if overruns or (overruns is not None and windows and all(windows))
        else "unmeasured"
    )
    summary = (
        f"Overall {'PASS' if report['passed'] else 'FAIL'} | {passed} passed, {len(checks) - passed} failed | "
        f"{report.get('snapshots', '?')} snapshots, {report.get('actors', '?')} actors | "
        f"runaway counters {thrashing} | authority {status}"
    )
    return "\n".join([*lines, summary])
