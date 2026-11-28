"""Grade one DLL session and its most recent mission restart, using only observed evidence."""

import json
import math
import re
from collections import Counter, defaultdict
from statistics import median

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


# Seven-minute staged battle with infantry and armor waves: allow comfortable
# headroom over short unit-scenario limits while still catching runaway loops.
E2E_ACTION_LIMITS = {key: 12 * limit for key, limit in ACTION_LIMITS.items()}


def latest(lines):
    header, rows, errors = {}, [], []
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
                header, rows, errors = {}, [], []
                if (
                    type(row.get("local_day_ms")) is not int
                    or not 0 <= row["local_day_ms"] < 86400000
                ):
                    raise ValueError("invalid local clock")
                header, rows, errors = row, [], []
            elif kind == "scene_reset":
                rows, errors = [], []
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


def final_sample_window(samples, rate):
    """Allow a complete observer sampling interval at accelerated simulation speeds."""
    # stats_tick identifies each wall-clock batch, even if the sampled actor
    # callbacks span multiple simulation ticks. A sparse fixture or a gap at
    # wipeout alone cannot establish a regular sampling cadence.
    batches = sorted({r["stats_tick"] for r in samples})
    gaps = [b - a for a, b in zip(batches, batches[1:])]
    interval = median(gaps) if len(gaps) >= 5 else 0
    # The native 500 ms interval at the maximum 20x speed spans 10 sim s.
    # Widely separated observations cannot extend the life of a missing unit.
    if interval > 11 * rate:
        interval = 0
    return max(5 * rate, math.ceil(interval * 11 / 10))


def force_value(p, actors, player, roster, start, end, rate, window, expected_waves=None):
    t = p.tactical
    deployed_pts = alive_pts = hp_weighted_pts = 0
    flag_units = 0
    fx, fy = t.flag_xy
    flag_rad = t.flag_radius_m * mission.SCALE
    seen_waves = set()
    missing = []
    invalid = []
    wrong_control = []
    side = {"player": player}
    for kind, roles in roster:
        hp_values = []
        for role in roles:
            weight = t.points[role]
            wave = t.waves[role]
            deployed_pts += weight
            # The first combat sample and activation can fall in adjacent
            # sampling batches. Ignore any hidden pre-reinforcement records.
            samples = [
                r for r in actors[p.mids[role]]
                if start + wave * rate - window <= r["sim_tick"] <= end
            ]
            own = [r for r in samples if r["player"] == player]
            if not own:
                missing.append(role)
                continue
            seen_waves.add(wave)
            if any(r["control"] != 0 for r in own):
                wrong_control.append(role)
            recent = sorted((r for r in samples if r["sim_tick"] >= end - window),
                            key=lambda r: (r["sim_tick"], r["ms"]))
            # A captured gun no longer contributes to its original side.
            if not recent or recent[-1]["player"] != player:
                continue
            hp = next((r["hp_ratio"] for r in reversed(recent)
                       if r["player"] == player and finite(r.get("hp_ratio"))), None)
            human = recent[-1].get("is_human")
            if hp is None or type(human) is not bool or (kind != "support" and human != (kind == "infantry")):
                invalid.append(role)
            elif hp > 0:
                hp = min(1.0, hp)
                hp_values.append(hp)
                alive_pts += weight
                hp_weighted_pts += weight * hp
                if math.hypot(recent[-1]["pos_x"] - fx, recent[-1]["pos_y"] - fy) <= flag_rad:
                    flag_units += 1
        health, alive, total = math.fsum(hp_values), len(hp_values), len(roles)
        side[kind] = {
            "alive": alive, "total": total,
            "survival_pct": 100 * alive / total if total else None,
            "remaining_hp_pct": 100 * health / total if total else None,
            "mean_hp_pct": 100 * health / alive if alive else None,
        }

    remaining_pts = 0.6 * alive_pts + 0.4 * hp_weighted_pts
    side["force_value"] = {
        "deployed_pts": deployed_pts, "alive_pts": alive_pts, "hp_weighted_pts": hp_weighted_pts,
        "remaining_pts": remaining_pts, "remaining_pct": 100 * remaining_pts / deployed_pts,
        "lost_pts": deployed_pts - remaining_pts,
    }
    side["flag_units"] = flag_units
    side["unobserved_roles"] = missing
    wanted_waves = set(t.waves.values()) if expected_waves is None else set(expected_waves)
    issues = [f"no Player {player} units observed for the {w}s wave"
              for w in sorted(wanted_waves - seen_waves)]
    if invalid:
        issues.append("invalid final HP or actor type: " + ", ".join(invalid))
    if wrong_control:
        issues.append(f"expected AI control on Player {player}: " + ", ".join(wrong_control))
    return side, issues


def battle_outcome(p, actors, start, end, rate, window, delta, stage1_delta=None):
    t = p.tactical
    a, issues_a = force_value(p, actors, t.friendly_player, (
        ("infantry", t.friendly_infantry), ("vehicles", t.friendly_vehicles), ("support", t.friendly_support),
    ), start, end, rate, window)
    b, issues_b = force_value(p, actors, t.enemy_player, (
        ("infantry", t.enemy_infantry), ("vehicles", t.enemy_vehicles), ("support", t.enemy_support),
    ), start, end, rate, window)
    av, bv = a["force_value"], b["force_value"]
    loss_floor = min(t.points.values()) if t.points else 1
    a["exchange_ratio"] = bv["lost_pts"] / max(loss_floor, av["lost_pts"])
    b["exchange_ratio"] = av["lost_pts"] / max(loss_floor, bv["lost_pts"])
    outcome = {
        "duration_s": t.duration_s,
        "score": round(50 + (av["remaining_pct"] - bv["remaining_pct"]) / 2, 2),
        "sides": {"A": a, "B": b}, "activity": dict(sorted(delta.items())),
        "activity_scope": "all players during the battle",
        "final_window_s": window / rate if rate > 0 else None,
    }
    if t.infantry_checkpoint_s and rate > 0:
        ckpt_end = min(end, start + round(t.infantry_checkpoint_s * rate))
        ia, _ = force_value(p, actors, t.friendly_player, (
            ("infantry", [r for r in t.friendly_infantry if t.waves[r] == 0]),
            ("vehicles", ()),
            ("support", [r for r in t.friendly_support if t.waves[r] == 0]),
        ), start, ckpt_end, rate, window, expected_waves={0})
        ib, _ = force_value(p, actors, t.enemy_player, (
            ("infantry", [r for r in t.enemy_infantry if t.waves[r] == 0]),
            ("vehicles", ()),
            ("support", [r for r in t.enemy_support if t.waves[r] == 0]),
        ), start, ckpt_end, rate, window, expected_waves={0})
        iav, ibv = ia["force_value"], ib["force_value"]
        ia["exchange_ratio"] = ibv["lost_pts"] / max(loss_floor, iav["lost_pts"])
        ib["exchange_ratio"] = iav["lost_pts"] / max(loss_floor, ibv["lost_pts"])
        outcome["infantry_stage"] = {
            "checkpoint_s": t.infantry_checkpoint_s,
            "score": round(50 + (iav["remaining_pct"] - ibv["remaining_pct"]) / 2, 2),
            "sides": {"A": ia, "B": ib},
            "activity": dict(sorted((stage1_delta or Counter()).items())),
        }
    return outcome, issues_a + issues_b


def grade(lines, ai_log, game_log=None, suite="full"):
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
    phases = mission.phases_for(suite)
    expected = [f"{edge}_{p.name}" for p in phases for edge in ("BEGIN", "END")] + [
        "DONE_" + suite
    ]
    if game_log is not None or suite in mission.BENCHMARK_SUITES:
        markers = re.findall(r"\bGOH_L1_((?:BEGIN|END|DONE)_\w+)\b", game_log or "")
        check(
            "run",
            "GEM boundaries",
            markers[-len(expected):] == expected,
            f"{len(markers)} markers, expecting {len(expected)} in order",
        )
    if suite in mission.BENCHMARK_SUITES and "Start mission " in (game_log or ""):
        # A launched trial always has this line. Standalone marker extracts
        # can still be used to score a synthetic observation stream.
        starts = re.findall(r"\bStart mission single/00-bootcamp/02_vehicle-control:0\b([^\r\n]*)", game_log)
        difficulty = re.search(r"\bon difficulty (\w+)", starts[-1]) if starts else None
        loaded = difficulty[1] if difficulty else "unknown"
        check("run", "heroic difficulty", loaded == "heroic", f"game.log reported {loaded}")

    witness = actors[(90 + mission.SUITES.index(suite)) * 100 + 1]
    previous_end = -1
    for p in phases:
        roles = p.tactical.points if p.tactical else p.mids
        group = [r for role in roles for r in actors[p.mids[role]]]
        ticks = [r["sim_tick"] for r in group]
        start, end = min(ticks, default=-1), max(ticks, default=-1)
        if p.tactical:
            rates = {r["ticks_per_second"] for r in group}
            rate = rates.pop() if len(rates) == 1 else 0
            window = final_sample_window(group, rate)
            expected_end = start + round(p.tactical.duration_s * rate)
            # The completion witness activates after 3 s of empty battlefield.
            # This anchors the terminal observation even after a mutual wipe.
            witness_end = min((r["sim_tick"] for r in witness), default=-1) - round(3 * rate)
            timed_transition = bool(witness) and abs(witness_end - expected_end) <= window
            end = min(expected_end, witness_end) if timed_transition else expected_end
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
        limits = E2E_ACTION_LIMITS if p.tactical else ACTION_LIMITS
        high = {k for k, limit in limits.items() if delta[k] > limit}
        phase_overruns = [
            {"phase": p.name, "counter": k, "count": delta[k], "limit": limits[k]}
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
            stage1_delta = Counter()
            if p.tactical.infantry_checkpoint_s and rate > 0:
                ckpt_end = min(end, start + round(p.tactical.infantry_checkpoint_s * rate))
                for r in rows:
                    if r["event"] == "stats_delta" and start <= r["since_tick"] <= r["sim_tick"] <= ckpt_end:
                        stage1_delta.update(r["counters"])
            outcome, issues = battle_outcome(p, actors, start, end, rate, window, delta, stage1_delta)
            outcome["runaway_actions"] = phase_overruns
            if not 1 <= rate <= 1_000_000:
                issues.append("invalid simulation clock")
            if not timed_transition:
                issues.append(f"expected a completion witness after {p.tactical.duration_s}s of combat")
            missing = sum(len(side["unobserved_roles"]) for side in outcome["sides"].values())
            check(p.name, "battle observations", not issues, " / ".join(issues) or
                  f"all waves observed on both sides, {missing} individual roles never sampled")
            if issues:
                outcome["score"] = None
                if "infantry_stage" in outcome:
                    outcome["infantry_stage"]["score"] = None
            scenarios[p.name] = outcome
        for k in sorted(high - reported):
            check(
                p.name,
                "runaway/" + k,
                False,
                f"{k}={delta[k]} (limit {limits[k]})",
            )
    if suite not in mission.BENCHMARK_SUITES:
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
    return report


def battle_table(tactical, passed):
    result, = tactical["scenarios"].values()
    lines = []
    if "infantry_stage" in result:
        st1 = result["infantry_stage"]
        s1_score = f"{st1['score']:.2f}/100" if passed and st1["score"] is not None else "unavailable"
        lines.append(f"Stage 1 Infantry Flag Fight (0-{st1['checkpoint_s']}s)  {s1_score}  (50 = parity, higher favors Side A / SW P2)")
        s1_rows = [("Side", "Infantry", "Support", "Remaining MP", "On flag", "Exchange")]
        for label, side in st1["sides"].items():
            inf, sup, val = side["infantry"], side["support"], side["force_value"]
            s1_rows.append((
                f"{label} (P{side['player']})",
                f"{inf['alive']}/{inf['total']} {inf['remaining_hp_pct']:.1f}%",
                f"{sup['alive']}/{sup['total']} {sup['remaining_hp_pct']:.1f}%",
                f"{val['remaining_pts']:.1f}/{val['deployed_pts']} ({val['remaining_pct']:.1f}%)",
                str(side["flag_units"]),
                f"{side['exchange_ratio']:.2f}×",
            ))
        w1 = [max(map(len, col)) for col in zip(*s1_rows)]
        lines.extend("  ".join(v.ljust(w) for v, w in zip(row, w1)).rstrip() for row in s1_rows)
        if st1["activity"]:
            items = [f"{k}={v}" for k, v in st1["activity"].items()]
            lines.append("Stage 1 AI activity  " + ", ".join(items[:10]))

    rows = [("Side", "Infantry", "Vehicles", "Support", "Alive MP", "HP MP",
             "Remaining MP", "On flag", "Exchange")]
    for label, side in result["sides"].items():
        classes = []
        for key in ("infantry", "vehicles", "support"):
            units = side[key]
            hp_str = f"{units['remaining_hp_pct']:.1f}%" if units["remaining_hp_pct"] is not None else "—"
            classes.append(f"{units['alive']}/{units['total']} {hp_str}")
        value = side["force_value"]
        rows.append((f"{label} (P{side['player']})", *classes, f"{value['alive_pts']:g}",
                     f"{value['hp_weighted_pts']:.1f}",
                     f"{value['remaining_pts']:.1f}/{value['deployed_pts']} ({value['remaining_pct']:.1f}%)",
                     str(side["flag_units"]),
                     f"{side['exchange_ratio']:.2f}×"))
    widths = [max(map(len, column)) for column in zip(*rows)]
    overall = tactical["overall_score"]
    score = f"{overall:.2f}/100" if passed and overall is not None else "unavailable (failed run checks)"
    lines.append(f"E2E force-value score  {score}  (50 = parity, higher favors Side A / SW P2)")
    lines.append("Unit classes show survivors/deployed and remaining roster HP.")
    lines.extend("  ".join(value.ljust(w) for value, w in zip(row, widths)).rstrip() for row in rows)
    activity = result["activity"]
    lines.append("AI activity across both sides")
    if activity:
        items = [f"{k}={v}" for k, v in activity.items()]
        lines.extend("  " + ", ".join(items[i:i + 5]) for i in range(0, len(items), 5))
    else:
        lines.append("  No actions recorded")
    if "session_field_support_totals" in tactical:
        totals = tactical["session_field_support_totals"]
        lines.append("  DLL shutdown totals  " + ", ".join(f"{k}={v}" for k, v in totals.items()))
    return lines


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
        lines = [*battle_table(report["tactical"], report["passed"]), *failures]
    else:
        summary += f" | authority {status}"
    return "\n".join([*lines, summary])
