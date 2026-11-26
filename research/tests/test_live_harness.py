import configparser
import contextlib
import hashlib
import json
import math
import re
import tempfile
import unittest
from collections import Counter
from copy import deepcopy
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
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
        keys = [k[0] for k in grader.CHECKS[p.name].values()]
        if (suite, p.name) in (("full", m.ARM), ("infantry", m.INF)):
            keys.append("obj_pursue")
        for t in (begin, begin + 10):
            for role, mid in p.mids.items():
                extra = {}
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



TACTICAL_NAMES = (
    "tac_infantry_grenade_line",
    "tac_armor_flank_ambush",
    "tac_sustained_logistics",
    "tac_combined_assault",
)
TACTICAL_KNOBS = (
    "grenade_danger_radius_m medic_search_radius_m squad_share_radius_m smoke_infantry_cooldown_s overflow_cover_radius_m",
    "tank_facing_min_angle_deg smoke_vehicle_cooldown_s tank_memory_hold_s cannon_settle_max_s crew_fill_emit_backoff_s",
    "resupply_search_radius_m medic_search_radius_m recrew_search_radius_m scavenge_search_radius_m squad_share_radius_m lull_reload_ratio",
    "movement_stall_s convoy_gap_m objectives_pursue_radius_m attack_detour soft_cover_lof transport_unload safe_disembark close_grenade fast_weapon_swap crush_avoidance",
)
ROSTERS = tuple(f"{team}_{group}" for team in ("friendly", "enemy") for group in ("infantry", "vehicles", "support"))


def mission_records(phase):
    return m.parse("{mission " + " ".join(phase.objects + phase.relations) + "}")[1:]


def unit_records(phase):
    return {int(n[2], 16) - 0x8000: n for n in mission_records(phase) if n[0] in ("Entity", "Human")}


def action_timeline(phase):
    elapsed = 0
    result = []
    for a in m.parse("{mission " + " ".join(phase.actions) + "}")[1:]:
        result.append((elapsed, a))
        if a[0] == "delay":
            elapsed += float(a[1][1])
    return elapsed, result


def tactical_fixture():
    rows = [row("session_start", 0, exe="call_to_arms.exe", build=26734796, sha256=m.SHA, local_day_ms=0)]
    markers = []
    begin = 100
    tps = 10
    for phase in m.phases_for("calibration"):
        t = phase.tactical
        end = begin + t.duration_s * tps
        markers.extend(f"GOH_L1_{edge}_{phase.name}" for edge in ("BEGIN", "END"))
        # The observer never emits the hidden driver/gunner seats, scenery, etc.
        units = unit_records(phase)
        for group in ROSTERS:
            for role in getattr(t, group):
                mid = phase.mids[role]
                obj = units[mid]
                x, y = map(float, obj[3][1:])
                player = int(next(n[1] for n in obj[4:] if n[0] == "Player"))
                for tick in (begin, end):
                    rows.append(observation(
                        mid, tick, player=player, is_human=obj[0] == "Human", pos_x=x, pos_y=y,
                        control=0, ticks_per_second=tps, ms=tick * 25,
                    ))
        begin = end + 3 * tps
    markers.append("GOH_L1_DONE_calibration")
    done = (90 + m.SUITES.index("calibration")) * 100 + 1
    rows.append(observation(done, begin, ticks_per_second=tps, ms=begin * 25))
    return sorted(rows, key=lambda r: r["sim_tick"]), "\n".join(markers)


def tactical_grade(rows, markers):
    return grade(sorted(rows, key=lambda r: r["sim_tick"]), "calibration", markers=markers)


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

    def test_functional_missions_match_0b44c6c_byte_for_byte(self):
        # SHA-256 of UTF-8 .mi files from the pinned generator, in normal / fast / very_fast order.
        expected = {
            "full": (
                "c6f8fd89abb5b6470c6ae12612ab0113d4a2f657e9b24fbdb33752593bd277ce",
                "13fba55fa3a53c91ec053ce45d2bf3a4a87eee93659ef7d44d5b7a4d4c947292",
                "3f3553a239de783a1497a57561b1cc1de0ba0fe54f13157a5a6ae492a7397c3d",
            ),
            "infantry": (
                "4d684a262c9c0d6be5cdd10c3336ce4ebd8633a3703a33f64140e8f94ffba54a",
                "0eb3934ce9a32a04fd5301409e86a619e9b8cc8e301e9309b263c8a8a0640028",
                "f1cec8589e8df6984b0084f7b56101005b916c978a8cdc41d9ef4276be15258c",
            ),
            "armor": (
                "66f6e2fd8d6fd422a5a8af88c9cfa5b02f98dee713728b35cdbaa53f1d2558a9",
                "9d69bb4ff5cd5d5b940f22d970f8580b1ae28394ca431076db40a8f801933a79",
                "eefb72613f30119b990ef49d68180c9ae0541d9b4db5579e1b14facf94afa6c5",
            ),
            "close_at": (
                "fe9dbe0f95efcd8f4065653132548d6d9002fb6108f8867e5622668febb411e2",
                "719faa00ad68f461d6f5a1edb3f6be5480f310890019ffdaea6b7155cd94cd94",
                "bb538ac4159519e79c03bdb771c19d2e66088b70aa734b502f44cc482f0017cd",
            ),
        }
        for suite, hashes in expected.items():
            for speed, digest in zip(("normal", "fast", "very_fast"), hashes):
                with self.subTest(suite=suite, speed=speed):
                    self.assertEqual(hashlib.sha256(m.generate(suite, speed).encode()).hexdigest(), digest)

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
                        re.findall(r"GOH_L1_(\w+)", text),
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
        p = m.infantry()
        eid = hex(0x8000 + p.mids["empty"])
        boxes = m.parse("{mission " + " ".join(p.relations) + "}")
        kit = next(n[2] for n in boxes[1:] if n[:2] == ["Inventory", eid])
        self.assertEqual([item[1] for item in kit[2:]], ["mp40_unfolded"])
        self.assertIn("filled", kit[2])
        self.assertIn(["user", "hand_right"], kit[2])
        self.assertIn('{Extender "supply_zone" {enabled}{current 0}}', " ".join(p.objects))

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

    def test_functional_medic_placement(self):
        inf = m.parse(m.generate("infantry"))
        positions = {
            n[2]: next(f for f in n[3:] if f[0] == "Position")[1:]
            for n in inf
            if isinstance(n, list) and n[0] == "Human"
        }
        roles = m.infantry().mids
        xy = lambda role: tuple(map(float, positions[hex(0x8000 + roles[role])]))

        self.assertEqual(
            [
                math.dist(xy("medic"), xy(r)) / m.SCALE
                for r in ("patient_in", "patient_out")
            ],
            [28, 44],
        )


class TacticalMissionTests(unittest.TestCase):
    def test_mirrored_combatants_crew_cover_and_diplomacy(self):
        text = m.generate("calibration", "fast")
        self.assertEqual(text, m.generate("calibration", "fast", {"medic_search_radius_m": 52}))
        root = m.parse(text)
        m.validate(text)
        matrix = next(n[1:] for n in m.nodes(root) if n[0] == "diplomacy")
        for a, b in ((2, 3), (3, 2), (0, 2), (2, 0), (0, 3), (3, 0)):
            self.assertEqual(matrix[a * 8 + b], "1")
        phases = m.phases_for("calibration")
        self.assertEqual(tuple(p.name for p in phases), TACTICAL_NAMES)
        seen = set()
        for phase, duration, knobs in zip(phases, (65, 75, 85, 75), TACTICAL_KNOBS):
            with self.subTest(phase=phase.name):
                t = phase.tactical
                self.assertEqual((t.friendly_player, t.enemy_player), (2, 3))
                self.assertEqual(action_timeline(phase)[0], duration)
                self.assertEqual(t.duration_s, duration)
                self.assertEqual(set(t.knobs), set(knobs.split()))
                self.assertTrue(seen.isdisjoint(phase.mids.values()))
                seen.update(phase.mids.values())
                units = unit_records(phase)
                records = mission_records(phase)
                brains = {n[1]: n[2] for n in records if n[0] == "Brain"}
                kits = {n[1]: n[2:] for n in records if n[0] == "Inventory"}
                links = {n[1]: n[2] for n in records if n[0] == "Link"}
                scored = [role for k in ROSTERS for role in getattr(t, k)]
                self.assertEqual(len(scored), len(set(scored)))
                for group in ("infantry", "vehicles", "support"):
                    friendly = getattr(t, "friendly_" + group)
                    enemy = getattr(t, "enemy_" + group)
                    self.assertEqual({r.removeprefix("a_") for r in friendly}, {r.removeprefix("b_") for r in enemy})
                left = {r[2:] for r in phase.mids if r.startswith("a_")}
                right = {r[2:] for r in phase.mids if r.startswith("b_")}
                self.assertEqual(left, right)
                for name in left:
                    a, b = (units[phase.mids[side + name]] for side in ("a_", "b_"))
                    self.assertEqual(a[:2], b[:2], name)
                    self.assertEqual(float(a[3][1]), -float(b[3][1]), name)
                    self.assertEqual(float(a[3][2]), float(b[3][2]), name)
                    self.assertEqual(kits.get(a[2]), kits.get(b[2]), name)
                    if a[0] == "Human":
                        self.assertTrue(a[1].startswith(m.GER))
                    if a[2] in brains:
                        self.assertIn(["Player", "2"], a)
                        self.assertIn(["Player", "3"], b)
                        self.assertEqual(brains[a[2]], brains[b[2]])
                        self.assertIn(["control", "AI"], brains[a[2]])
                    if "a_" + name in scored:
                        self.assertNotIn(["Impregnability", "full"], a)
                        self.assertNotIn(["Impregnability", "full"], b)
                        if a[2] in links:
                            self.assertTrue(links[a[2]][1].startswith("seat"))
                    if "throw" in name:
                        self.assertNotIn("a_" + name, scored)
                # Item grants and waypoint orders can differ even with identical
                # spawn records. Compare their recipients, timing and destinations.
                inventories = {"a": [], "b": []}
                orders = {"a": [], "b": []}
                points = {n[0]: tuple(map(float, n[1][1:3]))
                          for n in m.parse("{mission " + " ".join(phase.waypoints) + "}")[1:]}
                for when, action in action_timeline(phase)[1]:
                    if action[0] not in ("inventory", "actor_to_waypoint"):
                        continue
                    label = next(n[1] for n in action[1] if n[0] == "tag").removeprefix(f"h{phase.index}_")
                    side, role = label.split("_", 1)
                    if action[0] == "inventory":
                        inventories[side].append((when, role, action[2:]))
                    else:
                        x, y = points[next(n[1] for n in action if n[0] == "waypoint")]
                        orders[side].append((when, role, x if side == "a" else -x, y))
                self.assertEqual(sorted(inventories["a"]), sorted(inventories["b"]))
                self.assertEqual(sorted(orders["a"]), sorted(orders["b"]))
                self.assertEqual(t.anchor_xy, (-t.enemy_anchor_xy[0], t.enemy_anchor_xy[1]))

    def test_equal_force_compositions_and_seated_passengers(self):
        line, armor, logistics, assault = m.phases_for("calibration")

        def models(phase, group):
            units = unit_records(phase)
            return Counter(units[phase.mids[r]][1].rsplit("/", 1)[-1] for r in getattr(phase.tactical, group))

        self.assertEqual(models(line, "friendly_infantry"), {"smg_1": 1, "rifleman_1": 3, "mgun_1": 1, "antitank_1": 1})
        self.assertEqual(models(line, "friendly_support"), {"medic": 1})
        self.assertEqual(models(armor, "friendly_vehicles"), {"panzer4d": 1, "t3476_40": 1, "45mm_m37": 1})
        self.assertEqual(len(armor.tactical.friendly_infantry), 4)
        self.assertEqual(models(logistics, "friendly_infantry"), {"smg_1": 2, "rifleman_1": 2})
        self.assertEqual(models(logistics, "friendly_vehicles"), {"45mm_m37": 1, "ba64": 1})
        self.assertEqual(models(logistics, "friendly_support"), {"medic": 1, "blitz3_6_art_ammo": 1})
        self.assertEqual(models(assault, "friendly_vehicles"), {"panzer4d": 1, "sdkfz251c": 1})
        self.assertEqual(len(assault.tactical.friendly_infantry), 8)
        flag, = (n for n in mission_records(assault) if n[:2] == ["Entity", "flag_point_1"])
        self.assertEqual(float(flag[3][1]), 0)
        self.assertFalse(any(n[0] == "Player" for n in flag[3:]))
        for phase, model, score_riders in ((armor, "t3476_40", False), (assault, "sdkfz251c", True)):
            records = mission_records(phase)
            for player, roster in ((2, phase.tactical.friendly_infantry), (3, phase.tactical.enemy_infantry)):
                hull = next(n[2] for n in records if n[:2] == ["Entity", model] and ["Player", str(player)] in n)
                seats = {n[2][1]: int(n[1], 16) - 0x8000 for n in records if n[0] == "Link" and n[2][0] == hull}
                for seat in ("seat1", "seat2", "seat3", "seat4"):
                    self.assertEqual(seats[seat] in {phase.mids[r] for r in roster}, score_riders)

    def test_both_lines_get_timed_grenades_and_equal_supply(self):
        line, armor, logistics, assault = m.phases_for("calibration")
        throws = [(time, a) for time, a in action_timeline(line)[1]
                  if a[0] == "actor_fire" and ["attack", "position"] in a]
        self.assertEqual(sorted(time for time, _ in throws), [6, 6, 18, 18])
        wp = {n[0]: tuple(map(float, n[1][1:3])) for n in m.parse("{mission " + " ".join(line.waypoints) + "}")[1:]}
        for time in (6, 18):
            aims = [wp[next(n[1] for n in action if n[0] == "waypoint")] for when, action in throws if when == time]
            for center in (line.tactical.anchor_xy, line.tactical.enemy_anchor_xy):
                self.assertAlmostEqual(min(math.dist(center, xy) for xy in aims) / m.SCALE,
                                       0 if time == 6 else 7, delta=1)
        for side, anchor in (("a_", "anchor_xy"), ("b_", "enemy_anchor_xy")):
            for phase, role, dist in ((line, "medic", 24), (logistics, "medic", 26), (logistics, "truck", 40)):
                obj = unit_records(phase)[phase.mids[side + role]]
                xy = tuple(map(float, obj[3][1:]))
                self.assertAlmostEqual(math.dist(xy, getattr(phase.tactical, anchor)) / m.SCALE, dist, delta=1)
            records = mission_records(logistics)
            for role in ("smg1", "smg2", "rifle1", "rifle2"):
                eid = hex(0x8000 + logistics.mids[side + role])
                box = next(n[2] for n in records if n[:2] == ["Inventory", eid])
                self.assertIn(["clear"], box)
                self.assertEqual(sum("filled" in item and ["user", "hand_right"] in item for item in box[2:]), 1)
                for item in box[2:]:
                    if "ammo" in item:
                        self.assertLessEqual(int(item[item.index("ammo") + 1]), 5)
        self.assertEqual(" ".join(logistics.objects).count('{Extender "supply_zone" {enabled}{current 0}}'), 2)

    def test_stock_archives_and_baked_terrain(self):
        try:
            game = cli.game_dir()
        except (FileNotFoundError, ValueError):
            self.skipTest("requires local Gates of Hell stock archives (GOH_GAME_DIR)")
        for suite in ("full", "calibration"):
            with self.subTest(suite=suite):
                m.check_stock(game, m.generate(suite))


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


class TacticalGradeTests(unittest.TestCase):
    def setUp(self):
        self.rows, self.markers = tactical_fixture()
        self.phases = m.phases_for("calibration")

    def last(self, phase, role):
        return next(r for r in reversed(self.rows) if r.get("mid") == phase.mids[role])

    def report(self):
        return tactical_grade(self.rows, self.markers)

    def test_last_five_sim_seconds_hp_and_scoped_activity(self):
        line, armor = self.phases[:2]
        a, b, c = (self.last(line, r) for r in line.tactical.friendly_infantry[:3])
        end = b["sim_tick"]
        # At 4x speed the stale sample is still inside five wall-clock seconds.
        for r, ago in ((a, 6), (c, 4)):
            tick = end - ago * r["ticks_per_second"]
            r.update(sim_tick=tick, stats_tick=tick, ms=tick * 25)
        b["hp_ratio"], c["hp_ratio"] = 0, .5
        for role in line.tactical.enemy_infantry[:2]:
            self.rows.remove(self.last(line, role))
        self.last(line, "a_medic")["hp_ratio"] = 0
        self.last(armor, "a_tank")["hp_ratio"] = .4
        self.last(armor, "a_desant_tank")["hp_ratio"] = 0
        # Temporary throwers and interior crew must not affect the scored rosters.
        crew = next(int(n[1], 16) - 0x8000 for n in mission_records(armor)
                    if n[0] == "Link" and n[2][1] == "driver")
        armor_end = self.last(armor, "a_tank")["sim_tick"]
        self.rows.append(observation(crew, armor_end, player=2, hp_ratio=0, ms=armor_end*25, ticks_per_second=10))
        thrower = next(mid for role, mid in line.mids.items() if "throw" in role)
        self.rows.append(observation(thrower, end, player=3, hp_ratio=.01, ms=end*25, ticks_per_second=10))
        counters = {"dodges": 3, "sup_refill": 2, "rep_tt": 1, "recrews": 2, "scav_rearm": 4}
        self.rows.append(row("stats_delta", end - 10, since_tick=end - 20, counters=counters))
        self.rows.append(row("stats_delta", 101, since_tick=99, counters={"dodges": 20}))
        report = self.report()
        self.assertTrue(report["passed"], [c for c in report["checks"] if not c["passed"]])
        scenarios = report["tactical"]["scenarios"]
        front = scenarios[line.name]
        side_a, side_b = (front["sides"][side] for side in ("A", "B"))
        self.assertEqual((side_a["player"], side_b["player"]), (2, 3))
        self.assertEqual((side_a["infantry"]["alive"], side_b["infantry"]["alive"]), (4, 4))
        self.assertEqual((side_a["infantry"]["total"], side_b["infantry"]["total"]), (6, 6))
        self.assertAlmostEqual(side_a["infantry"]["mean_hp_pct"], 87.5)
        self.assertAlmostEqual(side_a["infantry"]["remaining_hp_pct"], 350 / 6, delta=.01)
        self.assertEqual((side_a["support"]["alive"], side_b["support"]["alive"]), (0, 1))
        self.assertLess(front["score"], 50)
        for key, value in counters.items():
            self.assertEqual(front["activity"][key], value)
        self.assertNotIn("activity", side_a)
        self.assertNotIn("activity", side_b)
        hull = scenarios[armor.name]["sides"]["A"]["vehicles"]
        self.assertEqual((hull["alive"], hull["total"], hull["mean_hp_pct"]), (2, 3, 70))

    def test_unsampled_early_casualty_and_absent_opposition(self):
        line = self.phases[0]
        lost = line.mids[line.tactical.enemy_infantry[0]]
        self.rows = [r for r in self.rows if r.get("mid") != lost]
        report = self.report()
        self.assertTrue(report["passed"], [c for c in report["checks"] if not c["passed"]])
        side_b = report["tactical"]["scenarios"][line.name]["sides"]["B"]
        self.assertEqual(side_b["unobserved_roles"], [line.tactical.enemy_infantry[0]])
        enemy = side_b["infantry"]
        self.assertEqual((enemy["alive"], enemy["total"]), (5, 6))
        enemy_mids = {line.mids[r] for k in ROSTERS if k.startswith("enemy") for r in getattr(line.tactical, k)}
        self.rows = [r for r in self.rows if r.get("mid") not in enemy_mids]
        invalid = self.report()
        self.assertFalse(invalid["passed"])
        self.assertIsNone(invalid["tactical"]["overall_score"])

    def test_live_difficulty_and_ai_control(self):
        start = "Start mission single/00-bootcamp/02_vehicle-control:0 on difficulty "
        for difficulty, expected in (("heroic", True), ("easy", False)):
            log = start + difficulty + " with options []\n" + self.markers
            report = tactical_grade(self.rows, log)
            self.assertEqual(report["passed"], expected)
        line = self.phases[0]
        self.last(line, line.tactical.enemy_infantry[0])["control"] = 1
        report = self.report()
        self.assertFalse(report["passed"])
        self.assertIsNone(report["tactical"]["overall_score"])

    def test_wipeout_earlier_than_final_window(self):
        line = self.phases[0]
        end = self.last(line, line.tactical.friendly_infantry[0])["sim_tick"]
        for r in self.rows:
            if r.get("mid") in line.mids.values() and r["sim_tick"] == end:
                early = end - 100
                r.update(sim_tick=early, stats_tick=early, ms=early * 25)
        # No actors remain to emit end-of-phase snapshots. The ordered GEM
        # markers and on-time next phase distinguish this mutual loss from a stalled run.
        report = self.report()
        self.assertTrue(report["passed"], [c for c in report["checks"] if not c["passed"]])
        result = report["tactical"]["scenarios"][line.name]
        for side in result["sides"].values():
            self.assertEqual(side["infantry"]["alive"], 0)
        self.assertEqual(result["score"], 50)

    def test_exact_parity_and_reciprocal_decisive_wins(self):
        tied = self.report()
        self.assertTrue(tied["passed"], tied["checks"])
        self.assertEqual(tied["tactical"]["overall_score"], 50.0)
        for result in tied["tactical"]["scenarios"].values():
            self.assertEqual(result["score"], 50.0)
        scores = []
        for losing_team in ("enemy", "friendly"):
            self.rows, self.markers = tactical_fixture()
            for phase in self.phases:
                for group in ("infantry", "vehicles", "support"):
                    for role in getattr(phase.tactical, losing_team + "_" + group):
                        self.last(phase, role)["hp_ratio"] = 0
            report = self.report()
            self.assertTrue(report["passed"], report["checks"])
            values = [r["score"] for r in report["tactical"]["scenarios"].values()]
            overall = report["tactical"]["overall_score"]
            self.assertAlmostEqual(overall, sum(values) / 4, delta=.01)
            for score in values + [overall]:
                with self.subTest(losing_team=losing_team, score=score):
                    self.assertTrue(80 < score <= 100 if losing_team == "enemy" else 0 <= score < 20)
            scores.append(overall)
        self.assertAlmostEqual(sum(scores), 100)
        table = grader.table(tied)
        self.assertIn("50.0/100", table)
        self.assertIn("A (P2)", table)
        self.assertIn("B (P3)", table)

    def test_integrity_and_runaway_caps_on_tactical_runs(self):
        self.assertTrue(self.report()["passed"])  # zero AI activity is valid for vanilla
        end = self.last(self.phases[0], self.phases[0].tactical.friendly_infantry[0])["sim_tick"]
        limit = grader.ACTION_LIMITS["crew_total"]
        self.rows.append(row("stats_delta", end, since_tick=end - 10, counters={"crew_total": limit}))
        self.assertTrue(self.report()["passed"])
        self.rows[-1]["counters"]["crew_total"] += 1
        runaway = self.report()
        self.assertFalse(runaway["passed"])
        self.assertIsNone(runaway["tactical"]["overall_score"])
        self.assertEqual(runaway["runaway_actions"][0]["counter"], "crew_total")
        self.rows.pop()
        self.assertFalse(tactical_grade(self.rows, self.markers[:-4])["passed"])
        self.assertFalse(tactical_grade(self.rows[:-1], self.markers)["passed"])
        self.rows[0]["sha256"] = "stale"
        self.assertFalse(self.report()["passed"])


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

    def test_heroic_options_restore_exact_bytes(self):
        originals = (
            b'{options\r\n {game {difficulty easy}{fog 1}}\r\n {mods "other:0"}\r\n}\r\n',
            b'{options {game {fog 1}}}',
            b'{options {mods "ai_test_harness:0"}}',
        )
        with tempfile.TemporaryDirectory() as tmp:
            options = Path(tmp) / "options.set"
            for original in originals:
                with self.subTest(original=original):
                    options.write_bytes(original)
                    launch.profile_mod(options, cli.atomic_write, heroic=True)
                    data = options.read_bytes()
                    self.assertIn(b"{difficulty heroic}", data)
                    self.assertIn(launch.TOKEN, data)
                    launch.profile_mod(options, cli.atomic_write, restore=True)
                    self.assertEqual(options.read_bytes(), original)

    def test_only_calibration_forces_heroic_and_restores_after_launch_failure(self):
        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.object(cli, "verify_game"),
            patch.object(m, "check_stock"),
            patch.object(launch, "wine_prefix", return_value=([], {})),
            patch.object(launch, "build_helper", return_value="fixture-ui"),
            patch.object(launch.subprocess, "run") as preflight,
            patch.object(launch.subprocess, "Popen") as spawn,
            contextlib.redirect_stderr(StringIO()),
        ):
            root = Path(tmp).resolve()
            binaries = root / "binaries/x64"
            binaries.mkdir(parents=True)
            for dll in ("goh_ai_mod.dll", "version.dll"):
                (binaries / dll).touch()
            original = b"{options\r\n {game {difficulty easy}{fog_of_war fog_realistic}}\r\n}\r\n"
            options = root / "options.set"
            preflight.return_value.returncode = 3
            backup = options.with_name("options.set.ai_test_harness.bak")
            for suite, difficulty, fow, pending in (
                ("full", b"easy", b"fog_realistic", False),
                ("calibration", b"heroic", b"fog_off", False),
                ("calibration", b"heroic", b"fog_off", True),
            ):
                with self.subTest(suite=suite, pending=pending):
                    options.write_bytes(original)
                    if pending:
                        launch.profile_mod(options, cli.atomic_write)
                        # The player changed a setting after a previous functional run.
                        options.write_bytes(options.read_bytes().replace(b"{difficulty easy}", b"{difficulty hard}"))
                    before = options.read_bytes()
                    evidence = root / (suite + ("-pending" if pending else ""))
                    evidence.mkdir()
                    at_launch = []

                    def failed_start(*args, **kwargs):
                        at_launch.append(options.read_bytes())
                        raise OSError("offline launcher fixture")

                    spawn.side_effect = failed_start
                    args = SimpleNamespace(profile=options, timeout=5, game_log=root / "game.log",
                                           restore_options=False, suite=suite, speed="normal")
                    report = launch.run(args, root, {}, cli, evidence=evidence)
                    self.assertFalse(report["passed"])
                    self.assertEqual(len(at_launch), 1)
                    self.assertIn(b"{difficulty " + difficulty + b"}", at_launch[0])
                    self.assertIn(b"{fog_of_war " + fow + b"}", at_launch[0])
                    if suite == "calibration":
                        self.assertEqual(options.read_bytes(), before)
                        self.assertEqual(backup.exists(), pending)
                        if pending:
                            self.assertEqual(backup.read_bytes(), original)
                    else:
                        launch.profile_mod(options, cli.atomic_write, restore=True)

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


class SweepTests(unittest.TestCase):
    def test_profile_ini_matches_native_module_list_and_tunings(self):
        native = (launch.ROOT / "mods/config_and_log.hpp").read_text()
        block = native.split("struct ModuleToggles {", 1)[1].split("\n};", 1)[0]
        module_keys = set(re.findall(r"bool (\w+)\{true\}", block))
        self.assertEqual(len(module_keys), 40)
        knobs = ("grenade_danger_radius_m", "medic_search_radius_m", "squad_share_radius_m",
                 "resupply_search_radius_m", "recrew_search_radius_m", "scavenge_search_radius_m",
                 "tank_facing_min_angle_deg")
        profiles = launch.sweep_profiles(
            "vanilla,default,tight,tactical,wide",
            ["local:medic_search_radius_m=30,modules.recrew_weapons=false", "ambush:tank_memory_hold_s=6"],
        )
        expected = {
            "tight": (4, 22, 12, 32, 18, 10, 32),
            "tactical": (7, 28, 18, 45, 24, 14, 20),
            "wide": (10, 52, 28, 85, 40, 24, 15),
        }
        self.assertEqual(list(profiles), ["vanilla", "default", *expected, "local", "ambush"])
        for name, settings in profiles.items():
            cfg = configparser.ConfigParser()
            cfg.read_string(launch.render_profile(settings).decode())
            self.assertTrue(cfg.getboolean("telemetry", "enabled"))
            self.assertEqual(int(cfg["testing"]["ai_player_mask"], 0), 0 if name == "vanilla" else 4)
            self.assertEqual(set(cfg["modules"]), module_keys)
            for key in module_keys:
                self.assertEqual(cfg.getboolean("modules", key), name != "vanilla" and (name, key) != ("local", "recrew_weapons"))
            if name in expected:
                self.assertEqual(tuple(cfg.getfloat("tuning", k) for k in knobs), expected[name])
            elif name == "local":
                self.assertEqual(cfg.getfloat("tuning", "medic_search_radius_m"), 30)
            elif name in ("default", "vanilla"):
                self.assertFalse(dict(cfg["tuning"]))
        for literal, value in (("0x8", 8), ("0", 0), ("4294967295", 2**32-1)):
            custom = launch.sweep_profiles("default", ["control:testing.ai_player_mask=" + literal])
            cfg = configparser.ConfigParser()
            cfg.read_string(launch.render_profile(custom["control"]).decode())
            self.assertEqual(int(cfg["testing"]["ai_player_mask"], 0), value)
            self.assertTrue(all(cfg.getboolean("modules", k) for k in module_keys))
        for bad in ("typo:no_such_radius=8", "vanilla:medic_search_radius_m=22", "../bad:medic_search_radius_m=22",
                    "bad:modules.auto_medic=2", "bad:medic_search_radius_m=nan", "bad:telemetry.enabled=false",
                    "bad:testing.ai_player_mask=4294967296", "bad:testing.ai_player_mask=-1"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                launch.sweep_profiles("default", [bad])

    def test_original_ini_restored_on_success_and_error(self):
        data = launch.render_profile(launch.sweep_profiles("vanilla")["vanilla"])
        original = b"\xef\xbb\xbf; personal options\r\n[tuning]\r\nmedic_search_radius_m=57\r\n"
        with tempfile.TemporaryDirectory() as tmp:
            ini = Path(tmp) / "goh_ai.ini"
            for existed in (True, False):
                for fails in (True, False):
                    with self.subTest(existed=existed, fails=fails):
                        if existed:
                            ini.write_bytes(original)
                        else:
                            ini.unlink(missing_ok=True)
                        expected = self.assertRaisesRegex(RuntimeError, "trial") if fails else contextlib.nullcontext()
                        with expected, launch.temporary_ini(ini, data, cli.atomic_write):
                            self.assertEqual(ini.read_bytes(), data)
                            if fails:
                                raise RuntimeError("trial")
                        self.assertEqual(ini.exists(), existed)
                        if existed:
                            self.assertEqual(ini.read_bytes(), original)

    def test_multi_trial_comparison_and_cli_archives(self):
        rows, markers = tactical_fixture()
        worn = deepcopy(rows)
        line, armor = m.phases_for("calibration")[:2]
        for mid, hp in ((armor.mids["a_tank"], .5), (armor.mids["a_desant_tank"], 0), (line.mids["a_medic"], 0)):
            next(r for r in reversed(worn) if r.get("mid") == mid)["hp_ratio"] = hp
        baseline = tactical_grade(worn, markers)
        for r in rows:
            if r.get("player") == 3:
                r["hp_ratio"] = 0
        victory = tactical_grade(rows, markers)
        vscore = victory["tactical"]["overall_score"]
        bscore = baseline["tactical"]["overall_score"]
        self.assertGreater(vscore, bscore)
        reports = {"vanilla": [baseline, victory], "custom": [victory, victory]}
        trials = [
            {"profile": name, "run": index, "report": r}
            for name, rr in reports.items() for index, r in enumerate(rr, 1)
        ]
        comparison = launch.compare_trials(trials)
        vanilla = comparison["profiles"]["vanilla"]
        better = comparison["profiles"]["custom"]
        self.assertAlmostEqual(vanilla["score"], (vscore + bscore) / 2, delta=.02)
        self.assertEqual((vanilla["runs"], vanilla["valid_runs"]), (2, 2))
        # Seven vehicles and three support units deploy per side per trial. In
        # trial one Side A loses a tank and a medic, and another tank has 50% HP.
        self.assertEqual(vanilla["sides"]["A"]["infantry_survival_pct"], 100)
        self.assertAlmostEqual(vanilla["sides"]["A"]["vehicle_survival_pct"], 100 * 13 / 14, delta=.01)
        self.assertAlmostEqual(vanilla["sides"]["A"]["vehicle_hp_pct"], 100 * 12.5 / 14, delta=.01)
        self.assertAlmostEqual(vanilla["sides"]["A"]["support_survival_pct"], 100 * 5 / 6, delta=.01)
        self.assertEqual(vanilla["sides"]["B"]["vehicle_hp_pct"], 50)
        self.assertAlmostEqual(vanilla["advantage_pp"]["vehicle_hp_pct"], 100 * 12.5 / 14 - 50, delta=.01)
        self.assertAlmostEqual(better["delta_vs_vanilla"], (vscore - bscore) / 2, delta=.02)
        self.assertEqual(comparison["best"]["overall"], ["custom"])
        self.assertEqual(comparison["best"]["scenarios"], {s: ["custom"] for s in TACTICAL_NAMES})
        for s in TACTICAL_NAMES:
            expected = sum(r["tactical"]["scenarios"][s]["score"] for r in reports["vanilla"]) / 2
            self.assertAlmostEqual(vanilla["scenarios"][s], expected, delta=.02)
        failed = dict(victory, passed=False)
        invalid = launch.compare_trials([*trials, {"profile": "broken", "run": 1, "report": failed}])
        self.assertEqual(invalid["profiles"]["broken"]["valid_runs"], 0)
        self.assertNotIn("broken", invalid["best"]["overall"])

        # Use the real profile writer, installer, and archiver against a temporary
        # game directory. Replace the UI preflight and process session with fixtures.
        counts = Counter()

        def fake_session(args, harness, binaries, options, ui, prefix, env, evidence):
            self.assertEqual((args.suite, args.speed), ("calibration", "fast"))
            self.assertTrue(args.restore_options)
            data = (binaries / "goh_ai.ini").read_bytes()
            self.assertEqual(data, (evidence / "goh_ai.ini").read_bytes())
            cfg = configparser.ConfigParser()
            cfg.read_string(data.decode())
            self.assertFalse(cfg.has_option("tuning", "movement_stall_s"))
            name = "custom" if cfg.getboolean("modules", "tactical_infantry") else "vanilla"
            report = dict(reports[name][counts[name]], evidence_dir=str(evidence))
            counts[name] += 1
            return report

        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.object(launch, "ROOT", Path(tmp)),
            patch.object(launch, "wine_prefix", return_value=([], {})),
            patch.object(launch, "build_helper", return_value=Path(tmp) / "helper"),
            patch.object(launch.subprocess, "run") as preflight,
            patch.object(launch, "_record_run", side_effect=fake_session) as session,
            patch.object(cli, "verify_game"),
            patch.object(m, "check_stock"),
        ):
            game = Path(tmp) / "game"
            binaries = game / "binaries/x64"
            binaries.mkdir(parents=True)
            for dll in ("version.dll", "goh_ai_mod.dll"):
                (binaries / dll).touch()
            options = Path(tmp) / "options.set"
            options.write_bytes(b"{options}")
            original = b"; personal settings\r\n[tuning]\r\nmovement_stall_s=4\r\n"
            ini = binaries / "goh_ai.ini"
            ini.write_bytes(original)
            preflight.return_value.returncode = 3  # UI preflight found no running game
            args = ["sweep", "--game-dir", str(game), "--player-profile", str(options),
                    "--profiles", "vanilla", "--profile", "custom:medic_search_radius_m=30",
                    "--runs", "2", "--speed", "fast", "--restore-options"]
            out = StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(cli.main(args), 0)
            self.assertEqual(ini.read_bytes(), original)
            self.assertEqual(counts, {"vanilla": 2, "custom": 2})
            sweeps = Path(tmp) / "local-work/level1/sweeps"
            archive, = sweeps.glob("sweep-*")
            saved = json.loads((archive / "summary.json").read_text())
            self.assertEqual(saved["profiles"], comparison["profiles"])
            self.assertEqual(len(list(archive.rglob("grade.json"))), 4)
            self.assertEqual(len(list(archive.rglob("grade.txt"))), 4)
            self.assertIn((archive / "comparison.txt").read_text().strip(), out.getvalue())

            def interrupted(*unused):
                self.assertNotEqual(ini.read_bytes(), original)
                raise ValueError("fixture launch failure")

            session.side_effect = interrupted
            with contextlib.redirect_stderr(StringIO()):
                self.assertEqual(cli.main(args), 2)
            self.assertEqual(ini.read_bytes(), original)
            incomplete, = (s for s in sweeps.iterdir() if s != archive)
            summary = json.loads((incomplete / "summary.json").read_text())
            self.assertFalse(summary["passed"])
            self.assertEqual(summary["profiles"]["vanilla"]["valid_runs"], 0)
            error, = incomplete.rglob("error.txt")
            self.assertIn("fixture launch failure", error.read_text())

    def test_swap_sides_cancels_map_bias_and_flips_masks(self):
        # Simulate a map where West (Side A) loses in vanilla (terrain bias), while modded AI
        # performs better on whichever side it plays so the side-swapped mean beats 50.0 parity.
        base_rows, markers = tactical_fixture()

        def scaled(p2_hp, p3_hp):
            rows = deepcopy(base_rows)
            for r in rows:
                if r.get("player") == 2:
                    r["hp_ratio"] = p2_hp
                elif r.get("player") == 3:
                    r["hp_ratio"] = p3_hp
            return tactical_grade(rows, markers)

        west_vanilla = scaled(0.4, 1.0)
        mod_west = scaled(0.7, 1.0)
        mod_east = scaled(0.2, 1.0)
        seen_masks = []

        def fake_session(args, harness, binaries, options, ui, prefix, env, evidence):
            cfg = configparser.ConfigParser()
            cfg.read_string((binaries / "goh_ai.ini").read_text())
            mask = cfg.getint("testing", "ai_player_mask")
            seen_masks.append(mask)
            report = west_vanilla if mask == 0 else (mod_west if mask == 4 else mod_east)
            return dict(report, evidence_dir=str(evidence))

        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.object(launch, "ROOT", Path(tmp)),
            patch.object(launch, "wine_prefix", return_value=([], {})),
            patch.object(launch, "build_helper", return_value=Path(tmp) / "helper"),
            patch.object(launch.subprocess, "run") as preflight,
            patch.object(launch, "_record_run", side_effect=fake_session),
            patch.object(cli, "verify_game"),
            patch.object(m, "check_stock"),
        ):
            game = Path(tmp) / "game"
            binaries = game / "binaries/x64"
            binaries.mkdir(parents=True)
            for dll in ("version.dll", "goh_ai_mod.dll"):
                (binaries / dll).touch()
            options = Path(tmp) / "options.set"
            options.write_bytes(b"{options}")
            preflight.return_value.returncode = 3
            args = ["sweep", "--game-dir", str(game), "--player-profile", str(options),
                    "--profiles", "vanilla,default", "--runs", "1", "--swap-sides", "--speed", "fast"]
            out = StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(cli.main(args), 0)
            self.assertEqual(seen_masks, [0, 4, 8])
            archive, = (Path(tmp) / "local-work/level1/sweeps").glob("sweep-*")
            saved = json.loads((archive / "summary.json").read_text())
            self.assertTrue(saved["passed"])
            self.assertTrue(saved["swap_sides"])
            self.assertEqual(saved["ai_player_masks"], {"vanilla": 0, "default": "4,8"})
            self.assertAlmostEqual(saved["profiles"]["vanilla"]["score"], 50.0, delta=0.01)
            expected_default = (mod_west["tactical"]["overall_score"]
                                + (100.0 - mod_east["tactical"]["overall_score"])) / 2
            self.assertAlmostEqual(saved["profiles"]["default"]["score"], expected_default, delta=0.05)
            self.assertGreater(saved["profiles"]["default"]["delta_vs_vanilla"], 0)
            self.assertIn("Modded−Vanilla (pp)", out.getvalue())
            self.assertIn("4,8", out.getvalue())


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
