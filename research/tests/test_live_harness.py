import contextlib
import json
import math
import tempfile
import unittest
from copy import deepcopy
from io import StringIO
from pathlib import Path
from unittest.mock import patch

from scripts import live_grade as grader
from scripts import live_harness as cli
from scripts import live_launch as launch
from scripts import live_mission as m

LOG = (
    f"=== goh_ai InitThread starting (73 hooks across 4 TUs; ini=loaded log=summary) ===\n"
    f"PE SHA-256 verified: {m.SHA} (build 26734796)\n"
    "PE32+ live RTTI vtable audit: checked=10 matched=10 mismatched=0 missing=0\n"
    "Initial state: remote=0 hostEmit=1 clock=0 L=20.0\n"
    "=== 73 hooks active (slot23=1 combat=20 infantry=43 tanks=9 runtime_allowed=0) ===\n"
)


def row(event, tick, **kw):
    return {"event": event, "sim_tick": tick, "ms": tick * 1000, **kw}


def observation(mid, tick, **kw):
    data = {
        "mid": mid,
        "actor": mid + 0x8000,
        "address": mid * 100,
        "player": 0,
        "is_human": True,
        "control": 1,
        "fire_mode": 0,
        "move_mode": 0,
        "pos_x": 100.0,
        "pos_y": 10.0,
        "hp_ratio": 1.0,
        "morale": 100.0,
        "behavior": "hold",
        "stats_tick": tick,
        "ticks_per_second": 1,
    }
    return row("actor_snapshot", tick, **{**data, **kw})


def fixture(suite, tuning=None):
    rows = [
        row(
            "session_start",
            0,
            exe="call_to_arms.exe",
            build=26734796,
            sha256=m.SHA,
            local_day_ms=0,
        )
    ]
    markers = []
    for index, p in enumerate(m.phases_for(suite, tuning)):
        begin = 100 * (index + 1)
        markers.extend([f"GOH_L1_BEGIN_{p.name}", f"GOH_L1_END_{p.name}"])
        if p.probe:
            rows.append(
                row(
                    "module_event",
                    begin - 1,
                    module="tuning",
                    action=p.probe,
                    mid=0,
                    value=p.setting,
                )
            )
            value = m.PROBES[p.probe][-1]
            keys = (
                (value,)
                if (p.name.endswith("_below") != p.probe.endswith("_deg"))
                else ()
            )
        else:
            keys = [k[0] for k in grader.CHECKS[p.name].values()]
            if (suite, p.name) in (("full", m.ARM), ("infantry", m.INF)):
                keys.append("obj_pursue")
        for t in (begin, begin + 10):
            for role, mid in p.mids.items():
                extra = {}
                if p.probe:
                    if role in ("target", "truck"):
                        extra["pos_x"] = 100 + p.distance * m.SCALE
                    elif role in ("subject", "mg"):
                        extra.update(heading_deg=p.distance, grenade_land_m=p.distance)
                if role == "tank" and p.name == m.ARM:
                    extra.update(
                        cannon_target=p.mids["hostile_tank"] * 100,
                        mg_target=p.mids["lone"] * 100,
                    )
                if p.name == m.CLOSE and role.startswith("hold"):
                    extra.update(move_mode=1, fire_mode=int(role == "hold_fire"))
                rows.append(observation(mid, t, **extra))
        if keys:
            rows.append(
                row(
                    "stats_delta",
                    begin + 10,
                    since_tick=begin,
                    counters=dict.fromkeys(keys, 1),
                )
            )
        if p.name == m.CLOSE:
            rows.append(
                row(
                    "module_event",
                    begin + 4,
                    module="close_grenade",
                    action="release",
                    mid=p.mids["mobile"],
                )
            )
    markers.append("GOH_L1_DONE_" + suite)
    rows.append(observation((90 + m.SUITES.index(suite)) * 100 + 1, begin + 11))
    return sorted(rows, key=lambda r: r["ms"]), "\n".join(markers)


def desant_line(suite):
    address = m.armor().mids["desant_tank"] * 100
    stamp = "00:03:25" if suite == "full" else "00:01:45"
    return (f"[{stamp}.000] TANK CREW FILL #2: tank={address:#x} human=0xabcd "
            "seat1(4) -> gunner3(2) tier=4 freeCrew=1 returning=0\n")


def fixture_log(suite):
    return LOG + (desant_line(suite) if suite in ("armor", "full") else "")


def grade(rows, suite="full", log=None, markers=None, tuning=None):
    if log is None:
        log = fixture_log(suite)
    return grader.grade(map(json.dumps, rows), log, markers, suite, tuning)


class MissionTests(unittest.TestCase):
    def test_activation_skips_linked_crew_and_delays_armor_contact(self):
        phase = m.armor()
        root = m.parse(m.generate("armor"))
        tags = {r[-1]: r[1:-1] for r in root if isinstance(r, list) and r[0] == "Tags"}
        for role in ("tank_driver", "hostile_tank_gunner"):
            self.assertNotIn("harness_all", tags[hex(0x8000 + phase.mids[role])])
        self.assertIn("h2_entry", tags[hex(0x8000 + phase.mids["tank"])])
        self.assertIn("h2_opponents", tags[hex(0x8000 + phase.mids["gun"])])
        actions = next(r[1:] for r in m.nodes(root) if r[0] == "actions")
        entry, contact = [i for i, r in enumerate(actions) if ["inactive", "off"] in r][
            :2
        ]
        before_contact = actions[entry + 1 : contact]
        self.assertIn("delay", [r[0] for r in before_contact])
        for role in ("halftrack", "troop_truck"):
            self.assertTrue(
                any(
                    r[0] == "actor_component"
                    and ["tag", phase.tag(role)] in r[1]
                    and ["component", "engine"] in r
                    and ["state", "broken"] in r
                    for r in before_contact
                )
            )

    def test_all_suites_and_speeds(self):
        for suite in m.SUITES:
            for speed in m.SPEEDS:
                with self.subTest(suite=suite, speed=speed):
                    text = m.generate(suite, speed)
                    objects = m.validate(text)
                    ids = [int(obj[2], 16) for obj in objects]
                    self.assertEqual(len(ids), len(set(ids)))
                    self.assertIn(f'{{"game_speed" {{mode {speed}}}}}', text)
                    ps = m.phases_for(suite)
                    markers = [
                        part
                        for p in ps
                        for part in ("BEGIN_" + p.name, "END_" + p.name)
                    ]
                    self.assertEqual(
                        __import__("re").findall(r"GOH_L1_(\w+)", text),
                        markers + ["DONE_" + suite],
                    )
                    for p in ps:
                        self.assertIn(m.command("delete", p.selector("phase")), text)

    def test_overview_frames_each_phase_at_its_begin_marker(self):
        for suite in ("full", "calibration"):
            root = m.parse(m.generate(suite))
            helpers = next(h for h in root[1:] if h[0] == "Helpers")
            cameras = next(h[1:] for h in helpers[1:] if h[0] == "cameras")
            actions = next(n[1:] for n in m.nodes(helpers) if n[0] == "actions")
            phases = m.phases_for(suite)
            self.assertEqual(len(cameras), len(phases))
            for phase, (name, first, second) in zip(phases, cameras):
                self.assertEqual(name, "cam_" + phase.name)
                self.assertEqual(second, ["key001", ["time", "0.5"], *first[1:]])
                self.assertEqual(first[0], "key000")
                eye, aim = (tuple(map(float, v[1:])) for v in first[1:3])
                positions = [
                    tuple(map(float, e[3][1:])) for e in root[1:]
                    if e[0] in ("Entity", "Human")
                    and int(e[2], 16) - 0x8000 in phase.mids.values()
                ]
                xs, ys = zip(*positions)
                self.assertAlmostEqual(aim[0], (min(xs) + max(xs)) / 2, places=2)
                self.assertAlmostEqual(aim[1], (min(ys) + max(ys)) / 2, places=2)
                self.assertEqual(eye[0], aim[0])
                self.assertGreaterEqual(aim[1] - eye[1], 1200)
                self.assertGreaterEqual(eye[2] - aim[2], 950)
                radius = math.hypot(max(xs) - min(xs), max(ys) - min(ys)) / 2
                self.assertLess(radius, math.dist(eye, aim) * math.sin(math.radians(25)))
                i = actions.index(["con", ["text", "GOH_L1_BEGIN_" + phase.name]])
                self.assertEqual(
                    actions[i + 1],
                    ["scenario", ["message", ["text", ""]],
                     ["camera", ["name", name], ["smooth_begin", "0"], ["smooth_end", "0"]],
                     ["do_not_store_camera"], ["do_not_restore_camera"], ["force_auto_complete"]],
                )

    def test_invalid_references_and_syntax(self):
        objects = m.validate(m.generate("infantry"))
        by_name = {e[1]: [n[0] for n in e[3:]] for e in objects}
        for name in ("flag_point_1", "blitz3_6_art_ammo"):
            self.assertEqual(by_name[name][-3:], ["Extender", "Player", "MID"])
        for name in ("sandbag_structured_straight", "fence-wood_3"):
            self.assertEqual(by_name[name], ["Position", "xform"])
        text = m.generate("armor")
        for broken in [
            text.replace('name "cam_armor_combined_ops"', 'name "absent"'),
            text.replace('{"cam_armor_combined_ops"', '{"cam_wrong"'),
            text.replace("{do_not_restore_camera}", ""),
            text.replace("{fov 50}", "{fov 0}"),
            text.replace("{position ", "{position nan ", 1),
            text[:-3],
            text + "}",
            text.replace("0x80c9", "0x80ca", 1),
            text.replace("{MID 201}", "{MID 202}", 1),
            text.replace("{MID 201}", "", 1),
            text.replace("{Position 140 480}", "{Position 140 480}{MID 900}", 1),
            text.replace("{tag h2_tank}", "{tag absent}"),
            text.replace("{Position -340 480}", "{Position 100000 0}", 1),
            text.replace("{Link 0x", "{Link 0xf", 1),
        ]:
            with self.assertRaises(ValueError):
                m.validate(broken)
        with self.assertRaisesRegex(ValueError, "waypoint"):
            m.validate(
                m.generate("close_at").replace('{waypoint "30"}', '{waypoint "9999"}')
            )

    def test_low_ammo_mg_inventory(self):
        phase = m.infantry()
        eid = hex(0x8000 + phase.mids["mg"])
        inventory = next(
            n
            for n in m.parse(m.generate("infantry"))
            if isinstance(n, list) and n[:2] == ["Inventory", eid]
        )
        self.assertIn(
            ["item", "mg34_belt", ["cell", "0", "0"], ["user", "hand_right"]],
            inventory[2],
        )
        self.assertIn(
            ["item", "mgun_ger", "belt", "ammo", "3", ["cell", "0", "3"]], inventory[2]
        )

    def test_resupply_has_a_dry_primary_and_stocked_source(self):
        phases = [
            m.infantry(),
            *(p for p in m.calibration({}) if p.probe == "resupply_search_radius_m"),
        ]
        for p in phases:
            eid = hex(0x8000 + p.mids["empty" if p.name == m.INF else "subject"])
            boxes = m.parse("{mission " + " ".join(p.relations) + "}")
            kit = next(n[2] for n in boxes[1:] if n[:2] == ["Inventory", eid])
            self.assertEqual([item[1] for item in kit[2:]], ["mp40_unfolded"])
            self.assertIn("filled", kit[2])
            self.assertIn(["user", "hand_right"], kit[2])
            self.assertIn(
                '{Extender "supply_zone" {enabled}{current 0}}', " ".join(p.objects)
            )
            if p.probe:
                self.assertEqual(p.actions[0], m.delay(15))
                self.assertIn('"delete"', p.actions[1])
                self.assertEqual(p.actions[2], m.delay(7))
                self.assertIn(p.selector("delivery"), p.actions[3])
                self.assertNotIn('"inventory"', " ".join(p.actions))

    def test_armor_and_close_contact_orders(self):
        armor = m.armor()
        vehicles = m.parse("{mission " + " ".join(armor.objects) + "}")[1:]
        tank = next(n for n in vehicles if n[1] == "panzer4d")
        self.assertIn(["Impregnability", "full"], tank)
        advance = next(
            i for i, cmd in enumerate(armor.actions) if '"actor_to_waypoint"' in cmd
        )
        track_damage = next(
            i for i, cmd in enumerate(armor.actions) if "{component trackright}" in cmd
        )
        self.assertLess(advance, track_damage)
        self.assertIn(armor.selector("tank"), armor.actions[advance])
        self.assertTrue(
            any(
                armor.selector("halftrack") in a
                and "{component engine}{state broken}" in a
                for a in armor.actions
            )
        )
        close = m.close_at()
        actions = " ".join(close.actions)
        attack = m.command("action", close.selector("flanker") + "{drop orders}{action attack}"
                           + "{target {tag " + close.tag("enemy") + "}}")
        self.assertIn(attack, close.actions)
        self.assertLess(actions.index(attack), actions.index(close.selector("armor_contact")))
        flanker = close.positions[close.mids["flanker"] % 100 - 1]
        for role in ("hostile_tank", "car"):
            self.assertGreater(math.dist(flanker, close.positions[close.mids[role] % 100 - 1]) / m.SCALE, 75)
        relations = m.parse("{mission " + " ".join(close.relations) + "}")[1:]
        hold_fire = hex(0x8000 + close.mids["hold_fire"])
        self.assertNotIn(close.tag("squad"), next(n for n in relations if n[0] == "Tags" and n[-1] == hold_fire))
        for role in ("mobile", "molotov"):
            eid = hex(0x8000 + close.mids[role])
            kit = next(n[2] for n in relations if n[:2] == ["Inventory", eid])
            self.assertTrue(any(it[:4] == ["item", "rifle_ger", "clip", "ammo"] for it in kit))
            self.assertTrue(any(it[:2] == ["item", "bandage_ger"] for it in kit))
        self.assertIn('"panzerfaust_60" "weapon" filled', " ".join(close.relations))
        for role in ("hold", "hold_fire"):
            self.assertNotIn(
                close.selector(role),
                " ".join(
                    a
                    for a in close.actions
                    if '"actor_to_waypoint"' in a or '"actor_fire"' in a
                ),
            )

    def test_desant_crew_and_gunner_exit_before_combat(self):
        p = m.armor()
        records = m.parse(m.generate("armor"))[1:]
        tank = hex(0x8000 + p.mids["desant_tank"])
        obj = next(n for n in records if n[:3] == ["Entity", "t3476_40", tank])
        self.assertIn(["Impregnability", "full"], obj)
        self.assertIn(["Player", "0"], obj)
        for i in range(1, 5):
            rider = hex(0x8000 + p.mids[f"desant_tank_seat{i}"])
            self.assertIn(["Link", rider, [tank, f"seat{i}"]], records)
        actions = [m.parse("{mission " + a + "}")[1] for a in p.actions]
        emit, = [a for a in actions if a[0] == "emit"]
        self.assertIn(["tag", p.tag("desant_tank")], emit[1])
        self.assertIn(["tag", p.tag("desant_tank_gunner3")], next(n for n in emit if n[0] == "crew"))
        remove = next(i for i, a in enumerate(actions) if a[0] == "delete" and ["tag", p.tag("desant_tank_gunner3")] in a[1])
        self.assertLess(actions.index(emit), remove)
        self.assertLess(remove, next(i for i, a in enumerate(actions) if a[0] == "entity_state"))
        self.assertTrue(all(["tag", p.tag("desant_tank")] not in a[1] for a in actions if a[0] == "actor_component"))

    def test_capture_and_diplomacy(self):
        root = m.parse(m.generate("infantry"))
        flag = next(
            e
            for e in root
            if isinstance(e, list) and e[0] == "Entity" and e[1] == "flag_point_1"
        )
        self.assertIn(["Player", "2"], flag)
        self.assertIn(
            ["zone", ["radius", "0"], ["zoneId", "f1"]],
            next(n for n in flag if n[:2] == ["Extender", "map_point"]),
        )
        self.assertIn(
            [
                "zone",
                "circle",
                ["position", "30", "520", "0"],
                ["Name", "f1"],
                ["Radius", "120"],
            ],
            list(m.nodes(root)),
        )
        matrix = next(n[1:] for n in m.nodes(root) if n[0] == "diplomacy")
        self.assertEqual(
            (len(matrix), matrix[2], matrix[16], matrix[1]), (64, "1", "1", "2")
        )
        with self.assertRaisesRegex(ValueError, "zone"):
            m.validate(m.generate("infantry").replace('{Name "f1"}', '{Name "typo"}'))

    def test_calibration_and_placement(self):
        phases = m.phases_for("calibration", {"medic_search_radius_m": 40})
        self.assertEqual(len(phases), 10)
        self.assertEqual(
            [p.distance for p in phases if p.probe == "medic_search_radius_m"], [38, 42]
        )
        for key, expected in (
            ("grenade_danger_radius_m", [3.5, 9]),
            ("tank_facing_min_angle_deg", [16, 42]),
            ("squad_share_radius_m", [16, 20]),
            ("resupply_search_radius_m", [57, 63]),
        ):
            self.assertEqual([p.distance for p in phases if p.probe == key], expected)
        for p in (p for p in phases if p.probe == "tank_facing_min_angle_deg"):
            objects = m.parse("{mission " + " ".join(p.objects + p.relations) + "}")[1:]
            for role, x in (("subject", "-340"), ("target", "560")):
                eid = hex(0x8000 + p.mids[role])
                tank = next(n for n in objects if n[0] == "Entity" and n[2] == eid)
                self.assertIn(["Position", x, "480"], tank)
                self.assertIn(["Impregnability", "full"], tank)
                brain = next(n for n in objects if n[:2] == ["Brain", eid])
                self.assertIn(["fire_mode", "open"], brain[2])
            self.assertFalse(any("{fire_mode hold}" in action for action in p.actions))
        edge = m.calibration(
            {"tank_facing_min_angle_deg": 15, "grenade_danger_radius_m": 2}
        )
        self.assertEqual(
            [p.distance for p in edge if p.probe == "tank_facing_min_angle_deg"],
            [10, 42],
        )
        self.assertEqual(edge[0].distance, 2)
        with self.assertRaises(ValueError):
            m.generate("calibration", tuning={"medic_search_radius_m": float("nan")})
        inf = m.parse(m.generate("infantry"))
        positions = {
            n[2]: next(f for f in n[3:] if f[0] == "Position")[1:]
            for n in inf
            if isinstance(n, list) and n[0] == "Human"
        }
        roles = m.infantry().mids
        xy = lambda role: tuple(map(float, positions[hex(0x8000 + roles[role])]))
        import math

        self.assertEqual(
            [
                math.dist(xy("medic"), xy(r)) / m.SCALE
                for r in ("patient_in", "patient_out")
            ],
            [28, 44],
        )


class GraderTests(unittest.TestCase):
    def test_desant_requires_scoped_passenger_receipt_and_counter(self):
        rows, _ = fixture("armor")
        def accepted(log):
            report = grade(rows, "armor", LOG + log)
            return next(c["passed"] for c in report["checks"] if c["check"] == "tank_crew_fill")

        line = desant_line("armor")
        self.assertTrue(accepted(line))
        p = m.armor()
        for wrong in ("", line.replace("seat1(4)", "charger(1)"),
                      line.replace("returning=0", "returning=2"),
                      line.replace("00:01:45", "00:02:45"),
                      line.replace(hex(p.mids["desant_tank"] * 100), hex(p.mids["halftrack"] * 100))):
            self.assertFalse(accepted(wrong))
        counts = next(r["counters"] for r in rows if "crew_total" in r.get("counters", {}))
        counts.update(crew_internal=counts["crew_total"], crew_infantry=1)
        self.assertFalse(accepted(line))

    def test_objectives_across_observed_phases_and_opportunistic_veto(self):
        rows, _ = fixture("full")
        counts = next(
            r["counters"] for r in rows if "obj_pursue" in r.get("counters", {})
        )
        for key in ("obj_pursue", "obj_cap"):
            counts[key] = 1
            report = grade(rows)
            self.assertTrue(report["passed"])
            objective = next(
                c for c in report["checks"] if c["check"] == "tactical_objectives"
            )
            self.assertIn(m.ARM, objective["evidence"])
            self.assertIn("ff_vetoes=0", report["observations"][0]["evidence"])
            text = grader.table(report)
            self.assertIn("INFO", text)
            self.assertIn("runaway counters 0 | authority preserved", text)
            del counts[key]
        # A pursuit in the gap between phases cannot satisfy this run.
        rows.append(row("stats_delta", 150, since_tick=145, counters={"obj_pursue": 1}))
        self.assertFalse(grade(rows)["passed"])

    def test_runaway_limits_and_summary(self):
        rows, _ = fixture("armor")
        counts = next(
            r["counters"] for r in rows if "crew_total" in r.get("counters", {})
        )
        for total, ok in ((0, False), (50, True), (51, False), (3932, False)):
            if total:
                counts["crew_total"] = total
            else:
                del counts["crew_total"]
            report = grade(rows, "armor")
            crew = next(c for c in report["checks"] if c["check"] == "tank_crew_fill")
            self.assertEqual((crew["passed"], report["passed"]), (ok, ok))
            self.assertEqual(len(report["runaway_actions"]), int(total > 50))
        self.assertIn("3932 (limit 50)", crew["evidence"])
        counts.update(crew_total=1, sup_trip=1000)
        report = grade(rows, "armor")
        self.assertFalse(report["passed"])
        self.assertEqual(report["runaway_actions"][0]["counter"], "sup_trip")
        snaps = [r for r in rows if r["event"] == "actor_snapshot"]
        self.assertEqual(report["snapshots"], len(snaps))
        self.assertEqual(report["actors"], len({r["actor"] for r in snaps}))
        self.assertIn("1 failed", grader.table(report))
        self.assertIn("authority not probed", grader.table(report))

    def test_full_and_scoped_counter(self):
        rows, markers = fixture("full")
        self.assertTrue(grade(rows, markers=markers)["passed"])
        bad = deepcopy(rows)
        delta = next(
            r for r in bad if r["event"] == "stats_delta" and "dodges" in r["counters"]
        )
        delta["since_tick"] -= 1
        self.assertFalse(grade(bad)["passed"])
        for log in [
            LOG.replace("remote=0", "remote=1"),
            LOG.replace("matched=10", "matched=9"),
            LOG + "ABORT:",
        ]:
            self.assertFalse(grade(rows, log=log)["passed"])
        self.assertFalse(grade(rows, markers=markers[:-4])["passed"])
        self.assertFalse(grade(rows[:-1])["passed"])

    def test_hook_count_checks_sum_and_minimum(self):
        rows, _ = fixture("armor")
        for counts, expected in (
            ((74, 21, 43, 9), True),
            ((73, 21, 43, 9), False),
            ((72, 19, 43, 9), False),
        ):
            total, combat, infantry, tanks = counts
            log = fixture_log("armor").replace(
                "73 hooks active (slot23=1 combat=20 infantry=43 tanks=9",
                f"{total} hooks active (slot23=1 combat={combat} infantry={infantry} tanks={tanks}",
            )
            self.assertEqual(grade(rows, "armor", log)["passed"], expected)

    def test_authority_and_log_fallback(self):
        rows, _ = fixture("close_at")
        event = next(r for r in rows if r["event"] == "module_event")
        rows.remove(event)
        self.assertFalse(grade(rows, "close_at")["passed"])
        line = f"[00:01:45.000] close_grenade release #1 actor={event['mid'] * 100:x} target=abc payload=1"
        self.assertTrue(grade(rows, "close_at", LOG + line)["passed"])
        self.assertFalse(
            grade(rows, "close_at", LOG + line.replace("01:45", "02:45"))["passed"]
        )
        hold = [r for r in rows if r.get("mid") == m.close_at().mids["hold"]]
        hold[-1]["pos_x"] += 100
        report = grade(rows, "close_at", LOG + line)
        self.assertFalse(report["passed"])
        self.assertIn("authority failed", grader.table(report))

    def test_calibration_positive_negative_and_missing_observations(self):
        rows, _ = fixture("calibration")
        report = grade(rows, "calibration")
        self.assertTrue(
            report["passed"], [c for c in report["checks"] if not c["passed"]]
        )
        self.assertEqual(len(report["calibration"]), 10)
        self.assertFalse(
            grade(
                rows
                + [row("stats_delta", 210, since_tick=200, counters={"dodges": 1})],
                "calibration",
            )["passed"]
        )
        for r in rows:
            if r["event"] == "actor_snapshot":
                r["grenade_land_m"] = None
        self.assertFalse(grade(rows, "calibration")["passed"])

    def test_restarts_and_corruption(self):
        rows, _ = fixture("infantry")
        self.assertFalse(grade(rows + [row("scene_reset", 0)], "infantry")["passed"])
        self.assertTrue(
            grade(rows + [row("scene_reset", 0)] + rows[1:], "infantry")["passed"]
        )
        self.assertFalse(
            grade(rows + [row("session_start", 0, exe="other")], "infantry")["passed"]
        )
        report = grader.grade([*map(json.dumps, rows), "{oops"], LOG, suite="infantry")
        self.assertFalse(report["passed"])

    def test_cli_json_and_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            rows, _ = fixture("armor")
            (base / "log").write_text(fixture_log("armor"))
            (base / "tel").write_text("\n".join(map(json.dumps, rows)))
            args = [
                "grade",
                "--suite",
                "armor",
                "--telemetry",
                str(base / "tel"),
                "--ai-log",
                str(base / "log"),
            ]
            for suffix in ([], ["--json"]):
                with contextlib.redirect_stdout(StringIO()) as out:
                    self.assertEqual(cli.main(args + suffix), 0)
                if suffix:
                    self.assertTrue(json.loads(out.getvalue())["passed"])
                else:
                    self.assertIn("Overall PASS", out.getvalue())
            (base / "tel").write_text("")
            with contextlib.redirect_stdout(StringIO()):
                self.assertEqual(cli.main(args), 1)


class LaunchTests(unittest.TestCase):
    def test_exit_confirmation_after_escape(self):
        class Session:
            exited = False
            log = ""

            def poll(self):
                return 0 if self.exited else None

            def read(self):
                return self.log

            def ui(self, *args):
                if args == ("dismiss",):
                    return 3
                if args == ("esc",):
                    self.log += "GUI: Activate page genereic_status_dialog"
                if args == ("click", "483", "321"):
                    self.exited = True
                return 0

        session = Session()
        with patch.object(launch.time, "sleep"):
            self.assertTrue(launch.stop_game(session.ui, session, session))

    def test_restore_selection_preserves_other_settings_and_mods(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "options.set"
            for mods in (b"", b' {mods\r\n  "other:0"\r\n }\r\n'):
                original = b"{options\r\n {quality high}\r\n" + mods + b"}\r\n"
                path.write_bytes(original)
                launch.profile_mod(path, cli.atomic_write)
                launch.profile_mod(path, cli.atomic_write)
                self.assertEqual(path.read_bytes().count(launch.TOKEN), 1)
                self.assertEqual(
                    path.with_suffix(".set.ai_test_harness.bak").read_bytes(), original
                )
                path.write_bytes(
                    path.read_bytes().replace(
                        launch.TOKEN, b"\r\n  " + launch.TOKEN + b"\r\n "
                    )
                )
                launch.profile_mod(path, cli.atomic_write, restore=True)
                self.assertEqual(path.read_bytes(), original)
                launch.profile_mod(path, cli.atomic_write)
                path.write_bytes(path.read_bytes().replace(b"high", b"medium"))
                launch.profile_mod(path, cli.atomic_write, restore=True)
                self.assertIn(b"medium", path.read_bytes())
                self.assertNotIn(launch.TOKEN, path.read_bytes())
                self.assertEqual(b'"other:0"' in path.read_bytes(), bool(mods))

    def test_existing_selection_and_stale_log(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp) / "options.set"
            original = b'{options {mods "ai_test_harness:0"}}'
            p.write_bytes(original)
            launch.profile_mod(p, cli.atomic_write)
            launch.profile_mod(p, cli.atomic_write, restore=True)
            self.assertEqual(p.read_bytes(), original)
            p.write_text("old DONE_full\n")
            log = launch.GameLog(p)
            self.assertEqual(log.read(), "")
            with p.open("a") as out:
                out.write("new event\n")
            self.assertEqual(log.read(), "new event\n")
            p.write_text("new game\n")
            self.assertEqual(log.read(), "new game\n")

    def test_menu_sequence_and_loading_prompt(self):
        class Running:
            def poll(self):
                return None

        events = iter(
            (
                "",
                "",
                "Mod: activate mod ai_test_harness\nGUI: Activate page page_main",
                "Mod: activate mod ai_test_harness",
                "GUI: Activate page page_singleplayer",
                "GUI: Activate page eCampaignArmySelectionPopup",
                "GUI: Close active overlay page eCampaignArmySelectionPopup",
                "Start mission single/00-bootcamp/02_vehicle-control:0",
                "GOH_L1_BEGIN_infantry_combined_ops",
                "GUI: Activate page page_scene_game",
                "GOH_L1_DONE_full",
            )
        )
        calls = []
        with (
            patch.object(launch.GameLog, "read", lambda self: next(events)),
            patch.object(launch.time, "sleep"),
        ):
            launch.drive(
                lambda *args: calls.append(args) or 0,
                Running(),
                launch.GameLog(Path("absent.log")),
                "full",
                launch.time.monotonic() + 90,
            )
        self.assertEqual(calls[:2], [("esc",), ("space",)])
        self.assertEqual(sum(cmd[0] == "click" for cmd in calls), 6)
        self.assertEqual(calls[-1], ("space",))

    def test_new_session_witness_and_exception(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp) / "telemetry"
            line = b'{"event":"actor_snapshot","mid":9001}\n'
            p.write_bytes(line + line[:10])

            def finish(_):
                with p.open("ab") as out:
                    out.write(line[10:])

            with patch.object(launch.time, "sleep", finish):
                launch.wait_witness(p, len(line), 9001, launch.time.monotonic() + 1)
            self.assertEqual(p.read_bytes(), line * 2)
            log = launch.GameLog(p)
            p.write_text(
                "***************** Exception *****************\ninvalid mission record"
            )
            with self.assertRaisesRegex(ValueError, "invalid mission record"):
                launch.drive(None, None, log, "full", launch.time.monotonic() + 1)


class InstallTests(unittest.TestCase):
    def test_live_read_finishes_a_buffered_line(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "telemetry.jsonl"
            path.write_text('{"event":"session_')

            def flush(_):
                with path.open("a") as out:
                    out.write('start"}\n{"event":"next"}\n')

            with path.open() as stream, patch.object(cli.time, "sleep", flush):
                self.assertEqual(
                    list(cli.telemetry_lines(stream)), ['{"event":"session_start"}\n']
                )

    def test_repeat_install_clean_and_optional_ini(self):
        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.object(cli, "verify_game"),
            patch.object(m, "check_stock"),
        ):
            game = Path(tmp).resolve()
            ini = game / "binaries/x64/goh_ai.ini"
            ini.parent.mkdir(parents=True)
            ini.write_text("; keep\n[smoke]\nextra_grenades=original\n")
            target = cli.install(game, "full", "normal", {})
            self.assertNotIn("telemetry", ini.read_text())
            self.assertEqual(
                {p.name for p in (game / "mods").rglob("*") if p.is_file()},
                {"0.mi", "mod.info"},
            )
            descriptor = game / "mods" / cli.MOD / "mod.info"
            descriptor.write_text(cli.LEGACY_INFO)
            cli.install(
                game,
                "calibration",
                "fast",
                cli.overrides(["medic_search_radius_m=42", "logging.level=verbose"]),
            )
            self.assertIn('{name "ai_test_harness"}', descriptor.read_text())
            self.assertIn('{minGameVersion "1.069"}', descriptor.read_text())
            self.assertIn('{maxGameVersion "1.069"}', descriptor.read_text())
            self.assertIn("extra_grenades=original", ini.read_text())
            self.assertIn("enabled=true", ini.read_text())
            self.assertIn("medic_search_radius_m=42", ini.read_text())
            self.assertEqual(
                target.read_text(),
                m.generate("calibration", "fast", {"medic_search_radius_m": 42}),
            )
            extra = game / "mods" / cli.MOD / "personal"
            extra.touch()
            with self.assertRaises(ValueError):
                cli.clean(game)
            extra.unlink()
            cli.clean(game)
            cli.clean(game)
            self.assertTrue(ini.exists())
            self.assertFalse(target.exists())

    def test_refuses_symlink_or_unknown_mod(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp).resolve()
            stock = game / "resource"
            stock.mkdir()
            (game / "mods").symlink_to(stock)
            with self.assertRaises(ValueError):
                cli.clean(game)
            (game / "mods").unlink()
            (game / "mods" / cli.MOD).mkdir(parents=True)
            with self.assertRaises(ValueError):
                cli.clean(game)


if __name__ == "__main__":
    unittest.main()
