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


def clamp(value):
    return min(1.0, max(0.0, value))


def tactical_side(p, actors, cutoff, end, player, infantry_roles, vehicle_roles, support_roles, anchor, advance_roles):
    missing = []
    invalid = []
    wrong_control = []

    def survivors(roles, human=None):
        alive = {}
        for role in roles:
            samples = actors[p.mids[role]]
            own = [r for r in samples if r["player"] == player]
            if not own:
                missing.append(role)
                continue
            if any(r["control"] != 0 for r in own):
                wrong_control.append(role)
            recent = sorted(
                (r for r in samples if cutoff <= r["sim_tick"] <= end),
                key=lambda r: (r["sim_tick"], r["ms"]),
            )
            if not recent or recent[-1]["player"] != player:
                continue
            # A captured gun no longer contributes to its original team's strength.
            last = recent[-1]
            hp = next((r["hp_ratio"] for r in reversed(recent) if finite(r.get("hp_ratio"))), None)
            observed_type = last.get("is_human")
            if hp is None or type(observed_type) is not bool or (human is not None and observed_type != human):
                invalid.append(role)
            elif hp > 0:
                alive[role] = {**last, "hp_ratio": clamp(hp)}
        return alive

    def health(alive, deployed):
        hp = math.fsum(r["hp_ratio"] for r in alive.values())
        return {
            "alive": len(alive),
            "total": len(deployed),
            "survival_pct": 100 * len(alive) / len(deployed) if deployed else None,
            "mean_hp_pct": 100 * hp / len(alive) if alive else None,
            "remaining_hp_pct": 100 * hp / len(deployed) if deployed else None,
        }

    infantry = survivors(infantry_roles, True)
    vehicles = survivors(vehicle_roles, False)
    support = survivors(support_roles)
    if p.tactical.role_kind == "defense":
        distances = [
            math.hypot(r["pos_x"] - anchor[0], r["pos_y"] - anchor[1]) / mission.SCALE
            for r in infantry.values()
        ]
        distance = sum(distances) / len(distances) if distances else None
        discipline = {
            "kind": "defense",
            "mean_distance_m": distance,
            "fraction": clamp((44 - distance) / 22) if distance is not None else 0,
        }
    else:
        spawns = dict(zip(p.mids, p.positions))
        advance, gains = [], []
        front = infantry | vehicles
        for role in advance_roles or front:
            if role not in front:
                continue
            r = front[role]
            start_x = spawns[role][0]
            path = p.tactical.target_x - start_x
            if path:
                dx = r["pos_x"] - start_x
                advance.append(math.copysign(1, path) * dx / mission.SCALE)
                gains.append(clamp(dx / path))
        discipline = {
            "kind": "assault",
            "mean_progress_m": sum(advance) / len(advance) if advance else None,
            "fraction": sum(gains) / len(gains) if gains else 0,
        }

    issues = []
    if set(missing).issuperset(infantry_roles + vehicle_roles):
        issues.append(f"no Player {player} combatants observed")
    if invalid:
        issues.append("invalid final HP or actor type: " + ", ".join(invalid))
    if wrong_control:
        issues.append(f"expected AI control on Player {player}: " + ", ".join(wrong_control))
    return {
        "player": player,
        "infantry": health(infantry, infantry_roles),
        "vehicles": health(vehicles, vehicle_roles),
        "support": {
            "alive": len(support), "total": len(support_roles),
            "survival_pct": 100 * len(support) / len(support_roles) if support_roles else None,
            "units": {r: r in support for r in support_roles},
        },
        "unobserved_roles": missing,
        "discipline": discipline,
    }, issues


def tactical_outcome(p, actors, cutoff, end, delta, extended_counters):
    t = p.tactical
    side_a, issues_a = tactical_side(p, actors, cutoff, end, t.friendly_player, t.friendly_infantry,
                                     t.friendly_vehicles, t.friendly_support, t.anchor_xy, t.friendly_advance)
    side_b, issues_b = tactical_side(p, actors, cutoff, end, t.enemy_player, t.enemy_infantry,
                                     t.enemy_vehicles, t.enemy_support, t.enemy_anchor_xy, t.enemy_advance)
    advantages = {}
    for kind in ("infantry", "vehicles"):
        a, b = side_a[kind], side_b[kind]
        advantages[kind] = None
        if a["total"] and b["total"]:
            advantages[kind] = (0.6 * (a["survival_pct"] - b["survival_pct"])
                                + 0.4 * (a["remaining_hp_pct"] - b["remaining_hp_pct"]))
    combat = [v for v in advantages.values() if v is not None]
    margin = sum(combat) / len(combat)
    advantages["support"] = None
    if t.friendly_support and t.enemy_support:
        advantages["support"] = side_a["support"]["survival_pct"] - side_b["support"]["survival_pct"]
        margin = 0.9 * margin + 0.1 * advantages["support"]

    activity = {k: delta[k] for k in (
        "dodges", "med_rev", "sup_refill", "rep_tt", "rep_full", "crew_total", "smoke_rel",
        "smoke_veh", "scavenge", "det_inf", "fast_swaps", "tank_face", "crush_escapes",
        "squad_ammo", "reload", "transport_unloads", "safe_doors", "obj_pursue",
        "overflow_cover", "settle_holds", "memory_holds", "move_nudge", "convoy_yield",
    )}
    # The current observer exports squad-logistics scavenge. Its dedicated
    # field-support breakdown is only printed at shutdown, across all phases.
    for k in ("recrews", "scav_rearm", "scav_heavy_pickup", "scav_corpse_loot"):
        activity[k] = delta.get(k)
    activity.update({k: delta[k] for k in extended_counters})

    return {
        "score": round(50 + margin / 2, 2),
        "sides": {"A": side_a, "B": side_b},
        "advantage_pct": advantages,
        "activity": activity,
        "activity_scope": "all players in this phase",
        "knobs": list(t.knobs),
    }, issues_a + issues_b


def grade(lines, ai_log, game_log=None, suite="full", tuning=None):
    header, rows, errors = latest(lines)
    checks = []
    scenarios = {}
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
    markers_ok = False
    if game_log is not None or suite == "calibration":
        markers = re.findall(r"\bGOH_L1_((?:BEGIN|END|DONE)_\w+)\b", game_log or "")
        markers_ok = markers[-len(expected):] == expected
        check(
            "run",
            "GEM boundaries",
            markers_ok,
            f"{len(markers)} markers, expecting {len(expected)} in order",
        )
    if suite == "calibration" and "Start mission " in (game_log or ""):
        # A launched trial always has this line. Standalone marker extracts
        # can still be used to score a synthetic observation stream.
        starts = re.findall(r"\bStart mission single/00-bootcamp/02_vehicle-control:0\b([^\r\n]*)", game_log)
        difficulty = re.search(r"\bon difficulty (\w+)", starts[-1]) if starts else None
        loaded = difficulty[1] if difficulty else "unknown"
        check("run", "heroic difficulty", loaded == "heroic", f"game.log reported {loaded}")

    witness = actors[(90 + mission.SUITES.index(suite)) * 100 + 1]
    groups = []
    for p in phases:
        roles = p.mids
        if p.tactical:
            t = p.tactical
            roles = (t.friendly_infantry + t.friendly_vehicles + t.friendly_support
                     + t.enemy_infantry + t.enemy_vehicles + t.enemy_support)
        groups.append([r for role in roles for r in actors[p.mids[role]]])
    extended_counters = {
        k for r in rows if r["event"] == "stats_delta" for k in r["counters"]
        if k == "recrews" or k.startswith("scav_")
    }
    previous_end = -1
    for index, (p, group) in enumerate(zip(phases, groups)):
        ticks = [r["sim_tick"] for r in group]
        start, end = min(ticks, default=-1), max(ticks, default=-1)
        if p.tactical:
            timed_transition = False
            rates = {r["ticks_per_second"] for r in group}
            rate = rates.pop() if len(rates) == 1 else 0
            if 1 <= rate <= 1_000_000:
                # All combatants can die early. GEM deletes the phase, waits
                # three simulation seconds, then activates the next roster.
                following = groups[index + 1] if index + 1 < len(groups) else witness
                expected_end = start + round(p.tactical.duration_s * rate)
                if following:
                    next_start = min(r["sim_tick"] for r in following)
                    prior_end = next_start - round(3 * rate)
                    timed_transition = markers_ok and abs(prior_end - expected_end) <= 5 * rate
                    expected_end = min(expected_end, prior_end)
                end = max(end, expected_end)
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
        phase_overruns = [
            {"phase": p.name, "counter": k, "count": delta[k], "limit": ACTION_LIMITS[k]}
            for k in sorted(high)
        ]
        overruns.extend(phase_overruns)
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
        if p.tactical:
            cutoff = end - 5 * rate
            outcome, issues = tactical_outcome(p, actors, cutoff, end, delta, extended_counters)
            outcome["runaway_actions"] = phase_overruns
            evidence = "final window sampled"
            if not any(cutoff <= r["sim_tick"] <= end for r in snapshots):
                # A mutual wipe can leave no actor available to call the
                # observer. The next activation can still bracket the fight.
                last_by_mid = {r["mid"]: r for r in sorted(group, key=lambda r: (r["sim_tick"], r["ms"]))}
                if timed_transition:
                    evidence = "terminal absence confirmed by scheduled next activation"
                elif last_by_mid and all(finite(r.get("hp_ratio")) and r["hp_ratio"] <= 0
                                         for r in last_by_mid.values()):
                    evidence = "all observed actors last sampled dead"
                else:
                    issues.append("no final five-second observations or confirmed terminal absence")
            if not 1 <= rate <= 1_000_000:
                issues.append("invalid simulation clock")
            missing = sum(len(side["unobserved_roles"]) for side in outcome["sides"].values())
            check(p.name, "tactical observations", not issues,
                  " / ".join(issues) or f"both sides sampled, {evidence}, {missing} roles never observed")
            if issues:
                outcome["score"] = None
            scenarios[p.name] = outcome
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
    check(
        "run",
        "mission completion",
        any(r["sim_tick"] > previous_end for r in witness),
        f"{len(witness)} completion witness samples",
    )
    report = {
        "suite": suite,
        "passed": all(c["passed"] for c in checks),
        "checks": checks,
        "observations": observations,
        "snapshots": len(snapshots),
        "actors": len({r["actor"] for r in snapshots}),
        "runaway_actions": overruns,
    }
    if scenarios:
        scores = [o["score"] for o in scenarios.values()]
        tactical = {"scenarios": scenarios,
                    "overall_score": round(sum(scores) / len(scores), 2) if report["passed"] else None}
        shutdown = re.search(r"ShutdownAndUnload complete [^\n]*?recrew=(\d+) rearm=(\d+) "
                             r"heavyPickup=(\d+) corpseLoot=(\d+)", log)
        if shutdown:
            tactical["session_field_support_totals"] = dict(zip(
                ("recrews", "scav_rearm", "scav_heavy_pickup", "scav_corpse_loot"),
                map(int, shutdown.groups())))
        report["tactical"] = tactical
    else:
        report["calibration"] = []
    return report


def tactical_table(tactical, passed):
    def strength(item, pct_key):
        if not item["total"]:
            return "—"
        pct = item[pct_key]
        suffix = f"  {pct:.0f}%" if pct is not None else ""
        return f"{item['alive']}/{item['total']}{suffix}"

    rows = [("Engagement", "A score", "Side", "Inf alive / HP", "Hull alive / HP", "Support alive", "Position")]
    activity = []
    for name, result in tactical["scenarios"].items():
        score = "—" if result["score"] is None else f"{result['score']:.1f}"
        for label, side in result["sides"].items():
            d = side["discipline"]
            if d["kind"] == "defense":
                distance, unit = d["mean_distance_m"], "from line"
            else:
                distance, unit = d["mean_progress_m"], "advance"
            rows.append((
                name if label == "A" else "", score if label == "A" else "", f"{label} (P{side['player']})",
                strength(side["infantry"], "mean_hp_pct"), strength(side["vehicles"], "mean_hp_pct"),
                strength(side["support"], "survival_pct"), "—" if distance is None else f"{distance:.1f} m {unit}",
            ))
        counts = ", ".join(f"{k}={v}" for k, v in result["activity"].items() if v)
        activity.append(f"  {name}  {counts or 'no recorded actions'}")
    widths = [max(map(len, column)) for column in zip(*rows)]
    lines = ["  ".join(value.ljust(w) for value, w in zip(row, widths)).rstrip() for row in rows]
    overall = tactical["overall_score"]
    score = f"{overall:.1f}/100" if passed and overall is not None else "unavailable (failed run checks)"
    if "session_field_support_totals" in tactical:
        totals = tactical["session_field_support_totals"]
        activity.append("  DLL session totals  " + ", ".join(f"{k}={v}" for k, v in totals.items()))
    return ["Head-to-head tactical scorecard (50.0 = tie, higher favors Side A)", *lines,
            f"Overall Tactical Score  {score}", "Recorded AI actions (phase totals across all players)", *activity]


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
        f"runaway counters {thrashing}"
    )
    if "tactical" in report:
        failures = [line for row, line in zip(rows, lines) if row[2] == "FAIL"]
        lines = [*tactical_table(report["tactical"], report["passed"]), *failures]
    else:
        summary += f" | authority {status}"
    return "\n".join([*lines, summary])
