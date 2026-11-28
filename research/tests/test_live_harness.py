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


def fixture(suite):
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
    for index, p in enumerate(m.phases_for(suite)):
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



BATTLE_NAME = "e2e_flag_battle"
ROSTERS = tuple(f"{team}_{group}" for team in ("friendly", "enemy") for group in ("infantry", "vehicles", "support"))


def mission_records(phase):
    return m.parse("{mission " + " ".join(phase.objects + phase.relations) + "}")[1:]


def unit_records(phase):
    return {int(n[2], 16) - phase.eid_base: n for n in mission_records(phase) if n[0] in ("Entity", "Human")}


def action_timeline(phase):
    elapsed = 0
    result = []
    for a in m.parse("{mission " + " ".join(phase.actions) + "}")[1:]:
        result.append((elapsed, a))
        if a[0] == "delay":
            elapsed += float(a[1][1])
    return elapsed, result


def e2e_fixture():
    phase, = m.phases_for("e2e")
    t = phase.tactical
    start, tps = 100, 10
    end = start + t.duration_s * tps
    rows = [row("session_start", 0, exe="call_to_arms.exe", build=26734796, sha256=m.SHA, local_day_ms=0)]
    units = unit_records(phase)
    for role in t.points:
        mid = phase.mids[role]
        obj = units[mid]
        x, y = map(float, obj[3][1:])
        player = int(next(n[1] for n in obj[4:] if n[0] == "Player"))
        arrival = start + t.waves[role] * tps
        # Regular snapshots under the 8x benchmark default. Interior crew never appear.
        for tick in (*range(arrival, end, 4 * tps), end):
            rows.append(observation(mid, tick, player=player, control=0, is_human=obj[0] == "Human",
                                    pos_x=x, pos_y=y, ticks_per_second=tps, ms=tick * 100 // 8))
    done = end + 3 * tps
    rows.append(observation(9401, done, ticks_per_second=tps, ms=done * 100 // 8))
    markers = "\n".join((f"GOH_L1_BEGIN_{phase.name}", f"GOH_L1_END_{phase.name}", "GOH_L1_DONE_e2e"))
    return sorted(rows, key=lambda r: r["sim_tick"]), markers


def e2e_grade(rows, markers, log=None):
    return grade(sorted(rows, key=lambda r: r["sim_tick"]), "e2e", markers=markers, log=log)


def desant_line(suite):
    address = m.armor().mids["desant_tank"] * 100
    stamp = "00:03:25" if suite == "full" else "00:01:45"
    return (f"[{stamp}.000] TANK CREW FILL #2: tank={address:#x} human=0xabcd "
            "seat1(4) -> gunner3(2) tier=4 freeCrew=1 returning=0\n")


def fixture_log(suite):
    return LOG + (desant_line(suite) if suite in ("armor", "full") else "")


def grade(rows, suite="full", log=None, markers=None):
    if log is None:
        log = fixture_log(suite)
    return grader.grade(map(json.dumps, rows), log, markers, suite)


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
        for suite in ("full", "e2e"):
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
                    and int(e[2], 16) - phase.eid_base in phase.mids.values()
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


class E2EMissionTests(unittest.TestCase):
    def setUp(self):
        self.phase, = m.phases_for("e2e")
        self.roster = self.phase.tactical
        self.objects = unit_records(self.phase)
        self.records = mission_records(self.phase)

    def test_single_mirrored_company_and_witness(self):
        self.assertEqual(m.SUITES, ("full", "infantry", "armor", "close_at", "e2e", "cqb"))
        p, t = self.phase, self.roster
        self.assertEqual((p.name, t.duration_s, t.infantry_checkpoint_s, t.friendly_player, t.enemy_player),
                         (BATTLE_NAME, 420, 180, 2, 3))
        text = m.generate("e2e", "fast")
        objects = m.validate(text)
        self.assertEqual(p.eid_base, m.E2E_EID_BASE)
        self.assertTrue(all(4000 <= mid < 4500 for mid in p.mids.values()))
        self.assertEqual(len(set(p.mids.values())), len(p.mids))
        spectator = [r for r in objects if ["Player", "0"] in r]
        self.assertEqual(len(spectator), 1)
        self.assertIn(["MID", "9401"], spectator[0])
        root = m.parse(text)
        players = next(n for n in root[1:] if n[0] == "Players")
        self.assertIn(["user", "0"], players)
        self.assertEqual({n[1]: n[2] for n in players[1:] if n[0] == "alias"},
                         {"0": "player", "1": "ai", "2": "west_ai", "3": "east_ai"})
        matrix = next(n[1:] for n in m.nodes(root) if n[0] == "diplomacy")
        for a, b in ((2, 3), (3, 2), (0, 3), (3, 0)):
            self.assertEqual(matrix[a * 8 + b], "1")
        # Spectator P0 is allied (2) with SW (P2) and hostile (1) to NE (P3) for native allied-blue vs
        # hostile-red coloring; E2E overlays symmetric 1.0/1.0 heroic.dl HP/accuracy for both sides.
        for a, b in ((0, 2), (2, 0)):
            self.assertEqual(matrix[a * 8 + b], "2")
        flags = [n for n in objects if n[:2] == ["Entity", "flag_point_1"]]
        self.assertEqual([list(map(float, f[3][1:])) for f in flags], [list(m.E2E_FLAG)])
        self.assertFalse(any(n[0] == "Player" for n in flags[0][4:]))

        brains = {r[1]: r[2] for r in self.records if r[0] == "Brain"}
        kits = {r[1]: r[2:] for r in self.records if r[0] == "Inventory"}
        teams = [{r[2:] for r in p.mids if r.startswith(side)} for side in ("a_", "b_")]
        self.assertEqual(*teams)
        for name in teams[0]:
            a, b = (self.objects[p.mids[side + name]] for side in ("a_", "b_"))
            self.assertEqual(a[:2], b[:2], name)
            self.assertEqual(kits.get(a[2]), kits.get(b[2]), name)
            if a[0] == "Human":
                self.assertTrue(a[1].startswith(m.GER))
            if a[2] in brains:
                self.assertIn(["Player", "2"], a)
                self.assertIn(["Player", "3"], b)
                self.assertEqual(brains[a[2]], brains[b[2]])
                self.assertIn(["control", "AI"], brains[a[2]])
                self.assertNotIn(["Impregnability", "full"], a)
                self.assertNotIn(["Impregnability", "full"], b)

    def test_cqb_skirmish_symmetry(self):
        (p,) = m.phases_for("cqb")
        t = p.tactical
        self.assertEqual((p.name, t.duration_s, t.infantry_checkpoint_s, t.friendly_player, t.enemy_player),
                         ("cqb_infantry_skirmish", 150, 75, 2, 3))
        self.assertEqual(t.friendly_vehicles, [])
        self.assertEqual(t.enemy_vehicles, [])
        text = m.generate("cqb", "fast")
        m.validate(text)
        self.assertIn("GOH_L1_DONE_cqb", text)
        teams = [{r[2:] for r in p.mids if r.startswith(side)} for side in ("a_", "b_")]
        self.assertEqual(*teams)

    def test_deployments_and_points_by_stage(self):
        p, t = self.phase, self.roster
        scored = [role for g in ROSTERS for role in getattr(t, g)]
        self.assertEqual(set(scored), set(t.points))
        self.assertEqual(len(scored), len(t.points))
        self.assertEqual(set(t.waves), set(scored))
        tags = {r[-1]: set(r[1:-1]) for r in self.records if r[0] == "Tags"}
        links = {r[1]: r[2] for r in self.records if r[0] == "Link"}
        duration, actions = action_timeline(p)
        self.assertEqual(duration, 420)
        reveals = {p.tag("entry"): 0}
        reveals.update({
            next(n[1] for n in r[1] if n[0] == "tag"): when
            for when, r in actions if r[0] == "entity_state" and ["inactive", "off"] in r
        })
        self.assertEqual(set(reveals.values()), {0, 180})
        for side in ("friendly", "enemy"):
            roles = [r for kind in ("infantry", "vehicles", "support") for r in getattr(t, side + "_" + kind)]
            wave_points = Counter()
            for role in roles:
                obj = self.objects[p.mids[role]]
                model = obj[1].rsplit("/", 1)[-1]
                self.assertEqual(t.points[role], m.MP_UNIT_COSTS[model], role)
                hint = links.get(obj[2])
                actor = hint[0] if hint else obj[2]
                stages = tags[actor].intersection(reveals)
                self.assertEqual(len(stages), 1, role)
                deploy = reveals[stages.pop()]
                self.assertEqual(t.waves[role], deploy, role)
                wave_points[deploy] += t.points[role]
            # Stage 1: 3*8 (rifle) + 2*10 (smg) + 10 (sl) + 2*30 (at) + 2*40 (mg) + 40 (medic) = 234 MP.
            # Stage 2: 240 (sdkfz222a) + 395 (panzer4d) + 390 (t35) = 1025 MP.
            self.assertEqual(wave_points, {0: 234, 180: 1025})
            self.assertEqual(tuple(len(getattr(t, side + "_" + k)) for k in ("infantry", "vehicles", "support")),
                             (10, 3, 1))
            models = Counter(self.objects[p.mids[r]][1] for r in getattr(t, side + "_vehicles"))
            self.assertEqual(models, {"panzer4d": 1, "t35": 1, "sdkfz222a": 1})

        scored_mids = {p.mids[r] for r in scored}
        for hull in (r for r in self.objects.values() if r[1] == "t35"):
            passengers = [int(eid, 16) - p.eid_base for eid, (carrier, seat) in links.items()
                          if carrier == hull[2] and seat.startswith("seat")]
            self.assertEqual(len(passengers), 4)
            for rider in passengers:
                self.assertNotIn(rider, scored_mids)
        for eid, (_, seat) in links.items():
            if not seat.startswith("seat"):
                self.assertNotIn(int(eid, 16) - p.eid_base, scored_mids)

    def test_staged_orders_and_squad_kits(self):
        p = self.phase
        waypoints = {n[0]: tuple(map(float, n[1][1:3]))
                     for n in m.parse("{mission " + " ".join(p.waypoints) + "}")[1:]}
        orders = {"a": [], "b": []}
        grants = {"a": [], "b": []}
        for when, action in action_timeline(p)[1]:
            if action[0] not in ("inventory", "actor_to_waypoint"):
                continue
            tag = next(n[1] for n in action[1] if n[0] == "tag")
            side, role = tag.removeprefix(f"h{p.index}_").split("_", 1)
            if action[0] == "inventory":
                grants[side].append((when, role, action[2:]))
            else:
                x, y = waypoints[next(n[1] for n in action if n[0] == "waypoint")]
                orders[side].append((when, role, math.hypot(x - m.E2E_FLAG[0], y - m.E2E_FLAG[1]) / m.SCALE))
        self.assertEqual(sorted(grants["a"]), sorted(grants["b"]))
        self.assertEqual([(w, r) for w, r, _ in orders["a"]], [(w, r) for w, r, _ in orders["b"]])
        self.assertIn((180, "tank_pz4", [["action", "add"], ["item", "repair_kit"], ["amount", "4"]]), grants["a"])
        self.assertIn((180, "tank_t35", [["action", "add"], ["item", "repair_kit"], ["amount", "4"]]), grants["a"])
        self.assertIn((0, "sq_rifle1", [["action", "add"], ["item", "mgun_ger", "belt", "ammo"],
                                        ["amount", "100"]]), grants["a"])
        # Both squads advance onto the center flag at t=0, 60, 120, 270, 345; car rushes at t=180, tanks at t=182/184.
        self.assertTrue(any(w == 0 and r == "squad" and d < 1 for w, r, d in orders["a"]))
        self.assertTrue(any(w == 60 and r == "squad" and d < 1 for w, r, d in orders["a"]))
        self.assertTrue(any(w == 120 and r == "squad" and d < 1 for w, r, d in orders["a"]))
        self.assertTrue(any(w == 180 and r == "car_222" for w, r, _ in orders["a"]))
        self.assertTrue(any(w == 182 and r == "tank_pz4" for w, r, _ in orders["a"]))
        self.assertTrue(any(w == 270 and r == "tank_t35" and d < 1 for w, r, d in orders["a"]))
        self.assertTrue(any(w == 345 and r == "tank_t35" and d < 1 for w, r, d in orders["a"]))
        kits = {r[1]: r[2] for r in self.records if r[0] == "Inventory"}
        for side in ("a", "b"):
            mg2 = self.objects[p.mids[f"{side}_sq_mg2"]][2]
            spare, = (item for item in kits[mg2] if "belt" in item)
            self.assertLessEqual(int(spare[4]), 5)

    def test_stock_archives_and_baked_terrain(self):
        try:
            game = cli.game_dir()
        except (FileNotFoundError, ValueError):
            self.skipTest("requires local Gates of Hell stock archives (GOH_GAME_DIR)")
        for suite in ("full", "e2e"):
            with self.subTest(suite=suite):
                m.check_stock(game, m.generate(suite))
        parsed_costs = m.load_mp_unit_costs(game)
        for unit, expected in m.MP_UNIT_COSTS.items():
            self.assertEqual(parsed_costs.get(unit), expected, f"MP cost mismatch for {unit}")

    def test_mp_unit_costs_parser(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            pak = game / "resource" / "gamelogic.pak"
            pak.parent.mkdir(parents=True)
            with m.zipfile.ZipFile(pak, "w") as z:
                z.writestr(
                    "set/multiplayer/units/mid/inf_ger.set",
                    '{"mp/ger/mid/rifleman_1" ("inf_tier2_lite" side(ger) period(mid) cost(8.0))}\n'
                    '{"mp/ger/mid/tankman" ("inf_tankman" side(ger) period(mid) cost(5.0))}\n'
                    '{"mp/ger/mid/tank_commander" ("inf_tankman" side(ger) period(mid) cost(5.0))}\n',
                )
                z.writestr(
                    "set/multiplayer/units/mid/units_ger.set",
                    '{"panzer4d"\n'
                    '\t("vehicle2" period(mid) side(ger) crew1(tankman:4) crew2(tank_commander:1) cost(370))\n'
                    '}\n',
                )
            costs = m.load_mp_unit_costs(game)
            self.assertEqual(costs["rifleman_1"], 8)
            self.assertEqual(costs["tankman"], 5)
            self.assertEqual(costs["panzer4d"], 395)


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


class E2EGradeTests(unittest.TestCase):
    def setUp(self):
        self.rows, self.markers = e2e_fixture()
        self.phase, = m.phases_for("e2e")
        self.roster = self.phase.tactical

    def last(self, role):
        return next(r for r in reversed(self.rows) if r.get("mid") == self.phase.mids[role])

    def result(self):
        report = e2e_grade(self.rows, self.markers)
        self.assertTrue(report["passed"], [c for c in report["checks"] if not c["passed"]])
        outcome = report["tactical"]["scenarios"][BATTLE_NAME]
        self.assertEqual(report["tactical"]["overall_score"], outcome["score"])
        return outcome

    def test_weighted_damage_exchange_and_class_breakdown(self):
        t = self.roster
        car, pz4, t35 = t.friendly_vehicles
        for role, hp in ((pz4, 0), (t35, .5), (t.enemy_infantry[0], 0), (t.enemy_support[0], 0)):
            self.last(role)["hp_ratio"] = hp
        # Place one friendly soldier on the flag at t=180s and end to verify flag_units tracking.
        flag_mid = self.phase.mids[t.friendly_infantry[0]]
        for r in self.rows:
            if r.get("mid") == flag_mid:
                r["pos_x"], r["pos_y"] = m.E2E_FLAG
        # Explicitly observe a dead interior driver and a reserve rider. Neither belongs in either force.
        scored_mids = {self.phase.mids[r] for r in t.points}
        extras = [n[1] for n in mission_records(self.phase) if n[0] == "Link"
                  and n[2][1] in ("driver", "seat1") and int(n[1], 16) - self.phase.eid_base not in scored_mids]
        end_tick = 100 + t.duration_s * 10
        for eid in extras:
            self.rows.append(observation(int(eid, 16) - self.phase.eid_base, end_tick, ms=end_tick * 100 // 8,
                                         ticks_per_second=10, player=2, control=0, hp_ratio=0))
        self.assertTrue(extras)
        result = self.result()
        a, b = (result["sides"][k] for k in ("A", "B"))
        self.assertEqual((a["player"], b["player"]), (2, 3))
        # Deployed (real GoH mid MP): inf=194 + medic=40 + sdkfz222a=240 + pz4=395 + t35=390 = 1259 MP.
        # Side A lost pz4 (395) and half HP on t35 (195 HP-MP): alive=864, hp_weighted=669, remaining=786.0, lost=473.0.
        # Side B lost smg_1 (10) and medic (40): remaining=1209.0, lost=50.0.
        for key, expected in (("deployed_pts", 1259), ("alive_pts", 864), ("hp_weighted_pts", 669.0),
                              ("remaining_pts", 786.0), ("lost_pts", 473.0)):
            self.assertAlmostEqual(a["force_value"][key], expected)
        self.assertAlmostEqual(b["force_value"]["remaining_pts"], 1209.0)
        self.assertAlmostEqual(result["score"], 50 + 50 * (786.0 - 1209.0) / 1259, delta=.01)
        self.assertAlmostEqual(a["exchange_ratio"], 50.0 / 473.0, delta=.005)
        self.assertAlmostEqual(b["exchange_ratio"], 473.0 / 50.0, delta=.005)
        self.assertEqual((a["flag_units"], b["flag_units"]), (1, 0))
        for side, totals in ((a, (10, 2, 1)), (b, (9, 3, 0))):
            for kind, alive, deployed in zip(("infantry", "vehicles", "support"), totals, (10, 3, 1)):
                self.assertEqual((side[kind]["alive"], side[kind]["total"]), (alive, deployed))
        self.assertAlmostEqual(a["vehicles"]["remaining_hp_pct"], 100 * 1.5 / 3, delta=.01)
        self.assertAlmostEqual(a["vehicles"]["mean_hp_pct"], 100 * 1.5 / 2, delta=.01)
        # Stage 1 checkpoint at t=180s (before tanks spawn): all infantry + medics intact at 180s!
        st1 = result["infantry_stage"]
        self.assertEqual(st1["checkpoint_s"], 180)
        self.assertEqual(st1["score"], 50.0)
        self.assertEqual((st1["sides"]["A"]["flag_units"], st1["sides"]["B"]["flag_units"]), (1, 0))

    def test_exact_parity_and_reciprocal_decisive_wins(self):
        self.assertEqual(self.result()["score"], 50.0)
        for loser, expected in ((3, 100.0), (2, 0.0)):
            with self.subTest(loser=loser):
                for r in self.rows:
                    if r.get("player") in (2, 3):
                        r["hp_ratio"] = float(r["player"] != loser)
                self.assertEqual(self.result()["score"], expected)

    def test_final_five_sim_seconds_and_mutual_wipeout(self):
        t = self.roster
        lost, survivor = (self.phase.mids[r] for r in t.friendly_infantry[:2])
        end = 100 + t.duration_s * 10
        self.rows = [r for r in self.rows if not ((r.get("mid") == lost and r["sim_tick"] > end - 60) or
                                                  (r.get("mid") == survivor and r["sim_tick"] > end - 40))]
        self.last(t.friendly_infantry[1])["hp_ratio"] = .4
        a = self.result()["sides"]["A"]["infantry"]
        self.assertEqual(a["alive"], 9)
        self.assertAlmostEqual(a["remaining_hp_pct"], 100 * 8.4 / 10, delta=.01)
        self.rows = [r for r in self.rows if r.get("player") not in (2, 3) or r["sim_tick"] < end - 120]
        wiped = self.result()
        self.assertEqual(wiped["score"], 50.0)
        self.assertTrue(all(s["force_value"]["alive_pts"] == 0 for s in wiped["sides"].values()))

    def test_absent_wave_or_opposition_and_missing_individual(self):
        t = self.roster
        lost = self.phase.mids[t.friendly_infantry[0]]
        self.rows = [r for r in self.rows if r.get("mid") != lost]
        self.assertEqual(self.result()["sides"]["A"]["infantry"]["alive"], 9)
        armor_wave = {self.phase.mids[r] for r in t.points if t.waves[r] == t.infantry_checkpoint_s}
        for keep in (lambda r: r.get("mid") not in armor_wave, lambda r: r.get("player") != 3):
            report = e2e_grade([r for r in self.rows if keep(r)], self.markers)
            self.assertFalse(report["passed"])
            self.assertIsNone(report["tactical"]["overall_score"])

    def test_activity_shutdown_and_company_runaway_limits(self):
        limit = grader.E2E_ACTION_LIMITS["crew_total"]
        self.assertGreater(limit, grader.ACTION_LIMITS["crew_total"])
        counters = {"crew_total": limit, "tank_face": 3, "natural_event": 4}
        end_tick = 100 + self.roster.duration_s * 10
        self.rows.append(row("stats_delta", end_tick - 10, ms=(end_tick - 10) * 100 // 8,
                             since_tick=end_tick - 20, counters=counters))
        self.rows.append(row("stats_delta", 101, ms=1262, since_tick=99, counters={"tank_face": 20}))
        self.assertEqual(self.result()["activity"], counters)
        shutdown = "ShutdownAndUnload complete recrew=3 rearm=2 heavyPickup=1 corpseLoot=4"
        report = e2e_grade(self.rows, self.markers, log=LOG + shutdown)
        self.assertTrue(report["passed"], report["checks"])
        totals = report["tactical"]["session_field_support_totals"]
        self.assertEqual(sorted(totals.values()), [1, 2, 3, 4])
        table = grader.table(report)
        for token in ("Stage 1 Infantry Flag Fight", "50.0", "A (P2)", "B (P3)", "natural_event", "crew_total"):
            self.assertIn(token, table)
        counters["crew_total"] += 1
        runaway = e2e_grade(self.rows, self.markers)
        self.assertFalse(runaway["passed"])
        self.assertIsNone(runaway["tactical"]["overall_score"])
        self.assertEqual(runaway["runaway_actions"][0]["counter"], "crew_total")

    def test_run_integrity(self):
        self.assertFalse(e2e_grade(self.rows, self.markers[:-4])["passed"])
        self.assertFalse(e2e_grade(self.rows[:-1], self.markers)["passed"])
        stamp = "Start mission single/00-bootcamp/02_vehicle-control:0 on difficulty {} with options []\n"
        for setting in ("easy", "heroic"):
            self.assertEqual(e2e_grade(self.rows, stamp.format(setting) + self.markers)["passed"], setting == "heroic")
        r = self.last(self.roster.enemy_infantry[0])
        r["control"] = 1
        self.assertFalse(e2e_grade(self.rows, self.markers)["passed"])
        r["control"] = 0
        self.rows[0]["sha256"] = "stale"
        self.assertFalse(e2e_grade(self.rows, self.markers)["passed"])


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

    def test_benchmark_graphics_and_exact_restore(self):
        display = (b'{adapter "NVIDIA GeForce {Virtual}"}{resolution 1920 1080}'
                   b'{window_mode borderless}{custom_video {preset ultra}{grass}}')
        heavy = (b'{vertical_sync}{antialiasing temporal}{grass}{dof}{glow}{rim_light}{filmGrain}{specular}'
                 b'{environment}{lens_refraction}{softIntersection}{fake_translucency}{alphaToCoverage}'
                 b'{_ssao {mode high}}')
        graphics = (b'{options {video ' + display + heavy + b'{lens_effects_options {glare {intensity 1}}}'
                    b'{preset ultra}{detail detail_high}'
                    b'{shadow high}{filtering trilinear}{model_quality high}{fx_quality high}{water_quality high}}'
                    b'{game {difficulty easy}{fog_of_war fog_on}}{audio {environment}}}')
        patched = launch.benchmark_options(graphics)
        self.assertEqual(launch.benchmark_options(patched), patched)
        self.assertIn(display, patched)
        self.assertIn(b"{audio {environment}}", patched)
        root = m.parse("{mission " + patched.decode() + "}")[1]
        video, game = (next(v[1:] for v in root[1:] if v[0] == k) for k in ("video", "game"))
        self.assertEqual(game, [["difficulty", "heroic"], ["fog_of_war", "fog_off"]])
        names = [v[0] for v in video]
        removed = re.findall(rb"\{([a-zA-Z_]+)", heavy)
        self.assertTrue(set(n.decode() for n in removed).isdisjoint(names))
        for key, value in (("preset", "low"), ("detail", "detail_low"), ("shadow", "off"),
                           ("filtering", "bilinear"), ("model_quality", "low"), ("fx_quality", "low"),
                           ("water_quality", "low"), ("lens_effects_options", "low")):
            self.assertEqual([v for v in video if v[0] == key], [[key, value]])
        originals = (
            graphics,
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
            # Recover the player's bytes even if the game lost its options file while exiting.
            launch.profile_mod(options, cli.atomic_write, heroic=True)
            options.unlink()
            launch.profile_mod(options, cli.atomic_write, restore=True, heroic=True)
            self.assertEqual(options.read_bytes(), original)

    def test_only_e2e_forces_heroic_and_restores_after_launch_failure(self):
        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.object(cli, "verify_game"),
            patch.object(m, "check_stock"),
            patch.object(m, "e2e_map_files", return_value={}),
            patch.object(m, "birdseye_camera_reg", return_value=b"{camera}"),
            patch.object(m, "birdseye_lod_files", return_value={}),
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
            original = (b"{options\r\n {game {difficulty easy}{fog_of_war fog_realistic}}\r\n"
                        b" {video {preset high}{vertical_sync}{resolution 1366 768}{window_mode windowed}}}\r\n")
            options = root / "options.set"
            ini = binaries / "goh_ai.ini"
            personal_ini = b"; personal\r\n[testing]\r\nsim_speed=15.5\r\n"
            ini.write_bytes(personal_ini)
            preflight.return_value.returncode = 3
            backup = options.with_name("options.set.ai_test_harness.bak")
            for suite, difficulty, fow, pending in (
                ("full", b"easy", b"fog_realistic", False),
                ("e2e", b"heroic", b"fog_off", False),
                ("e2e", b"heroic", b"fog_off", True),
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
                        cfg = configparser.ConfigParser()
                        cfg.read(ini)
                        at_launch.append((options.read_bytes(), cfg.getfloat("testing", "sim_speed")))
                        raise OSError("offline launcher fixture")

                    spawn.side_effect = failed_start
                    args = SimpleNamespace(profile=options, timeout=5, game_log=root / "game.log",
                                           restore_options=False, suite=suite, speed="normal")
                    speed = 0 if pending else 12.5
                    settings = cli.overrides([f"testing.sim_speed={speed}"]) if suite == "e2e" else {}
                    report = launch.run(args, root, settings, cli, evidence=evidence)
                    self.assertFalse(report["passed"])
                    (active_options, active_speed), = at_launch
                    self.assertEqual(active_speed, speed if suite == "e2e" else 15.5)
                    self.assertEqual(ini.read_bytes(), personal_ini)
                    self.assertIn(b"{difficulty " + difficulty + b"}", active_options)
                    self.assertIn(b"{fog_of_war " + fow + b"}", active_options)
                    self.assertIn(b"{resolution 1366 768}{window_mode windowed}", active_options)
                    self.assertEqual(b"{vertical_sync}" in active_options, suite == "full")
                    if suite == "e2e":
                        self.assertIn(b"{preset low}", active_options)
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
        module_defaults = {k: v == "true" for k, v in re.findall(r"bool (\w+)\{(true|false)\}", block)}
        module_keys = set(module_defaults)
        self.assertEqual(len(module_keys), 42)
        self.assertTrue(module_defaults["cover_frag"])
        self.assertTrue(module_defaults["offensive_smoke"])
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
            self.assertEqual(cfg.getfloat("testing", "sim_speed"), 8)
            self.assertLessEqual(cfg.getint("telemetry", "snapshot_interval_ms"), 250)
            self.assertEqual(int(cfg["testing"]["ai_player_mask"], 0), 0 if name == "vanilla" else 8)
            self.assertEqual(set(cfg["modules"]), module_keys)
            for key in module_keys:
                if name == "vanilla":
                    want = False
                elif (name, key) == ("local", "recrew_weapons"):
                    want = False
                else:
                    want = module_defaults[key]
                self.assertEqual(cfg.getboolean("modules", key), want)
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
            self.assertEqual({k: cfg.getboolean("modules", k) for k in module_keys}, module_defaults)
        for bad in ("typo:no_such_radius=8", "vanilla:medic_search_radius_m=22", "../bad:medic_search_radius_m=22",
                    "bad:modules.auto_medic=2", "bad:medic_search_radius_m=nan", "bad:telemetry.enabled=false",
                    "bad:testing.ai_player_mask=4294967296", "bad:testing.ai_player_mask=-1"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                launch.sweep_profiles("default", [bad])

    def test_sim_speed_boundaries_and_validation(self):
        for speed in (0, .25, 1, 8.5, 20):
            cfg = configparser.ConfigParser()
            cfg.read_string(launch.render_profile(launch.sweep_profiles("vanilla", sim_speed=speed)["vanilla"]).decode())
            self.assertEqual(cfg.has_option("testing", "sim_speed"), speed > 0)
            if speed:
                self.assertEqual(cfg.getfloat("testing", "sim_speed"), speed)
            self.assertEqual(launch.launch_timeout("e2e", cfg, None), 1980 if speed == .25 else 720)
            self.assertEqual(launch.launch_timeout("e2e", cfg, 60), 60)
            self.assertEqual(launch.launch_timeout("full", cfg, None), 720)
            self.assertEqual(float(cli.overrides([f"testing.sim_speed={speed}"])[("testing", "sim_speed")]), speed)
        for bad in ("-1", "0.24", "20.001", "nan", "inf", "invalid"):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    launch.sweep_profiles("default", sim_speed=bad)
                with self.assertRaises(ValueError):
                    cli.overrides(["testing.sim_speed=" + bad])
                with self.assertRaises(ValueError):
                    launch.render_profile({("testing", "sim_speed"): bad})
        with patch.object(launch, "sweep") as sweep, contextlib.redirect_stderr(StringIO()):
            self.assertEqual(cli.main(["sweep", "--sim-speed", "-2"]), 2)
            sweep.assert_not_called()

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
        rows, markers = e2e_fixture()
        worn = deepcopy(rows)
        phase, = m.phases_for("e2e")
        t = phase.tactical
        tanks = [role for role in t.enemy_vehicles if t.points[role] >= 300]
        medic = next(role for role in t.enemy_support if t.points[role] == m.MP_UNIT_COSTS["medic"])
        for role, hp in ((tanks[0], .5), (tanks[1], 0), (medic, 0)):
            mid = phase.mids[role]
            next(r for r in reversed(worn) if r.get("mid") == mid)["hp_ratio"] = hp
        baseline = e2e_grade(worn, markers)
        for r in rows:
            if r.get("player") == 2:
                r["hp_ratio"] = 0
        victory = e2e_grade(rows, markers)
        # Single-side E2E sweeps evaluate from the disadvantaged East side (Side B, Player 3).
        vscore = round(100 - victory["tactical"]["overall_score"], 2)
        bscore = round(100 - baseline["tactical"]["overall_score"], 2)
        self.assertGreater(vscore, bscore)
        reports = {"vanilla": [baseline, victory], "custom": [victory, victory]}
        trials = [
            {"profile": name, "run": index, "side": "B", "report": r}
            for name, rr in reports.items() for index, r in enumerate(rr, 1)
        ]
        comparison = launch.compare_trials(trials)
        vanilla = comparison["profiles"]["vanilla"]
        better = comparison["profiles"]["custom"]
        self.assertAlmostEqual(vanilla["score"], (vscore + bscore) / 2, delta=.02)
        self.assertEqual((vanilla["runs"], vanilla["valid_runs"]), (2, 2))
        # Three vehicles and one medic deploy per side per trial. In trial one
        # Side B (evaluated NE P3) loses one tank and a medic, and the second tank has 50% HP.
        self.assertEqual(vanilla["sides"]["A"]["infantry_survival_pct"], 100)
        self.assertAlmostEqual(vanilla["sides"]["A"]["vehicle_survival_pct"], 100 * 5 / 6, delta=.01)
        self.assertAlmostEqual(vanilla["sides"]["A"]["vehicle_hp_pct"], 100 * 4.5 / 6, delta=.01)
        self.assertAlmostEqual(vanilla["sides"]["A"]["support_survival_pct"], 100 * 1 / 2, delta=.01)
        self.assertEqual(vanilla["sides"]["B"]["vehicle_hp_pct"], 50)
        self.assertAlmostEqual(vanilla["advantage_pp"]["vehicle_hp_pct"], 100 * 4.5 / 6 - 50, delta=.01)
        self.assertAlmostEqual(better["delta_vs_vanilla"], (vscore - bscore) / 2, delta=.02)
        self.assertEqual(comparison["best"]["overall"], ["custom"])
        self.assertEqual(comparison["best"]["scenarios"], {BATTLE_NAME: ["custom"]})
        self.assertAlmostEqual(vanilla["scenarios"][BATTLE_NAME], (vscore + bscore) / 2, delta=.02)
        failed = dict(victory, passed=False)
        invalid = launch.compare_trials([*trials, {"profile": "broken", "run": 1, "side": "B", "report": failed}])
        self.assertEqual(invalid["profiles"]["broken"]["valid_runs"], 0)
        self.assertNotIn("broken", invalid["best"]["overall"])
        self.assertEqual(cli.run_settings("e2e", {})[launch.PLAYER_MASK], "8")

        # Use the real profile writer, installer, and archiver against a temporary
        # game directory. Replace the UI preflight and process session with fixtures.
        counts = Counter()

        def fake_session(args, harness, binaries, options, ui, prefix, env, evidence):
            self.assertEqual((args.suite, args.speed), ("e2e", "fast"))
            self.assertTrue(args.restore_options)
            data = (binaries / "goh_ai.ini").read_bytes()
            self.assertEqual(data, (evidence / "goh_ai.ini").read_bytes())
            cfg = configparser.ConfigParser()
            cfg.read_string(data.decode())
            self.assertFalse(cfg.has_option("tuning", "movement_stall_s"))
            self.assertEqual(cfg.getfloat("testing", "sim_speed"), 12.5)
            name = "custom" if cfg.getboolean("modules", "tactical_infantry") else "vanilla"
            self.assertEqual(cfg.getint("testing", "ai_player_mask"), 8 if name == "custom" else 0)
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
            patch.object(m, "e2e_map_files", return_value={}),
            patch.object(m, "birdseye_camera_reg", return_value=b"{camera}"),
            patch.object(m, "birdseye_lod_files", return_value={}),
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
                    "--runs", "2", "--speed", "fast", "--sim-speed", "12.5", "--restore-options"]
            out = StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(cli.main(args), 0)
            self.assertEqual(ini.read_bytes(), original)
            self.assertEqual(counts, {"vanilla": 2, "custom": 2})
            sweeps = Path(tmp) / "local-work/level1/sweeps"
            archive, = sweeps.glob("sweep-*")
            saved = json.loads((archive / "summary.json").read_text())
            self.assertEqual(saved["evaluated_side"], "B")
            self.assertEqual(saved["ai_player_masks"], {"vanilla": 0, "custom": 8})
            self.assertEqual(saved["profiles"], comparison["profiles"])
            self.assertEqual(len(list(archive.rglob("grade.json"))), 4)
            self.assertEqual(len(list(archive.rglob("grade.txt"))), 4)
            self.assertIn("NE−SW (pp)", out.getvalue())
            self.assertIn("Stage 1 Infantry score", out.getvalue())
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
        base_rows, markers = e2e_fixture()

        def scaled(p2_hp, p3_hp):
            rows = deepcopy(base_rows)
            for r in rows:
                if r.get("player") == 2:
                    r["hp_ratio"] = p2_hp
                elif r.get("player") == 3:
                    r["hp_ratio"] = p3_hp
            return e2e_grade(rows, markers)

        west_vanilla = scaled(0.4, 1.0)
        mod_west = scaled(0.7, 1.0)
        mod_east = scaled(0.2, 1.0)
        # If the profile changes nothing, a biased physical west score cancels against the east result.
        self.assertLess(west_vanilla["tactical"]["overall_score"], 50)
        unchanged = [{"profile": "default", "side": s, "report": west_vanilla} for s in ("A", "B")]
        neutral = launch.compare_trials(unchanged)["profiles"]["default"]
        self.assertEqual(neutral["score"], 50)
        self.assertTrue(all(delta == 0 for delta in neutral["advantage_pp"].values()))
        seen_masks = []

        def fake_session(args, harness, binaries, options, ui, prefix, env, evidence):
            cfg = configparser.ConfigParser()
            cfg.read_string((binaries / "goh_ai.ini").read_text())
            mask = cfg.getint("testing", "ai_player_mask")
            self.assertEqual(cfg.getfloat("testing", "sim_speed"), 8)
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
            patch.object(m, "e2e_map_files", return_value={}),
            patch.object(m, "birdseye_camera_reg", return_value=b"{camera}"),
            patch.object(m, "birdseye_lod_files", return_value={}),
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
            self.assertEqual(len(list(archive.rglob("grade.json"))), 3)
            self.assertEqual(saved["profiles"]["vanilla"]["runs"], 1)
            baseline, = (t for t in saved["trials"] if t["profile"] == "vanilla")
            self.assertEqual(baseline["west_score"], west_vanilla["tactical"]["overall_score"])
            self.assertEqual(baseline["score"], 50)
            self.assertEqual(saved["ai_player_masks"], {"vanilla": 0, "default": "4,8"})
            self.assertAlmostEqual(saved["profiles"]["vanilla"]["score"], 50.0, delta=0.01)
            expected_default = (mod_west["tactical"]["overall_score"]
                                + (100.0 - mod_east["tactical"]["overall_score"])) / 2
            self.assertAlmostEqual(saved["profiles"]["default"]["score"], expected_default, delta=0.05)
            self.assertGreater(saved["profiles"]["default"]["delta_vs_vanilla"], 0)
            self.assertIn("Modded−Vanilla (pp)", out.getvalue())
            self.assertIn("4,8", out.getvalue())

    def test_drive_auto_launch_and_restart_zero_ui_calls(self):
        class FakeProc:
            def poll(self):
                return None

        ui_calls = []

        def ui(*args):
            ui_calls.append(args)
            return 0

        steps = iter([
            "Mod: activate mod ai_test_harness\nGUI: Activate page page_main\n",
            f"Mod: activate mod ai_test_harness\nStart mission {launch.AUTO_MISSION_PATH}\n",
            f"Mod: activate mod ai_test_harness\nStart mission {launch.AUTO_MISSION_PATH}\nGOH_L1_BEGIN_e2e_flag_battle\n",
            (f"Mod: activate mod ai_test_harness\nStart mission {launch.AUTO_MISSION_PATH}\n"
             "GOH_L1_BEGIN_e2e_flag_battle\nGUI: Activate page page_scene_game\nGOH_L1_DONE_e2e\n"),
        ])

        class FakeLog:
            cur = ""

            def read(self):
                self.cur = next(steps, self.cur)
                return self.cur

        with patch.object(launch.time, "sleep"):
            launch.drive(ui, FakeProc(), FakeLog(), "e2e", launch.time.monotonic() + 30, auto_launch=True)
        self.assertEqual(ui_calls, [])

        r_steps = iter([
            f"Start mission {launch.AUTO_MISSION_PATH}\n",
            f"Start mission {launch.AUTO_MISSION_PATH}\nGUI: Activate page page_scene_game\nGOH_L1_DONE_e2e\n",
        ])

        class RestartLog:
            cur = ""

            def read(self):
                self.cur = next(r_steps, self.cur)
                return self.cur

        with patch.object(launch.time, "sleep"):
            launch.drive_restart(ui, FakeProc(), RestartLog(), "e2e", launch.time.monotonic() + 30)
        self.assertEqual(ui_calls, [])

    def test_stop_game_native_quit_and_cursor_safe_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            ini = Path(tmp) / "goh_ai.ini"
            ini.write_text("[testing]\nsuppress_cursor_grab = true\nquit_seq = 2\n", encoding="utf-8")
            ui_calls = []

            class QuitSession:
                exited = False

                def poll(self):
                    if not self.exited:
                        c = configparser.ConfigParser(strict=False, interpolation=None)
                        c.read(ini, encoding="utf-8-sig")
                        if c.getint("testing", "quit_seq", fallback=0) == 3:
                            self.exited = True
                    return 0 if self.exited else None

                def read(self):
                    return ""

                def ui(self, *args):
                    ui_calls.append(args)
                    return 3 if args == ("dismiss",) else 0

            sess = QuitSession()
            with patch.object(launch.time, "sleep"):
                self.assertTrue(
                    launch.stop_game(sess.ui, sess, sess, ini=ini, write=cli.atomic_write, merge_ini=cli.merged_ini)
                )
            # Only non-intrusive dismiss check; never clicks, closes, or sends keys.
            self.assertEqual(ui_calls, [("dismiss",)])

            # If quit_seq times out under suppress_cursor_grab, fall back to close + terminate without clicks.
            ui_calls.clear()

            class HungSession:
                terminated = False

                def poll(self):
                    return 0 if self.terminated else None

                def terminate(self):
                    self.terminated = True

                def wait(self, timeout=None):
                    return 0

                def read(self):
                    return "GUI: Activate page genereic_status_dialog"

                def ui(self, *args):
                    ui_calls.append(args)
                    return 3 if args == ("dismiss",) else 0

            hung = HungSession()
            with patch.object(launch.time, "sleep"):
                self.assertTrue(
                    launch.stop_game(hung.ui, hung, hung, ini=ini, write=cli.atomic_write, merge_ini=cli.merged_ini)
                )
            self.assertTrue(hung.terminated)
            self.assertEqual(ui_calls, [("dismiss",), ("close",)])

    def test_persistent_sweep_single_process_keep_open_reload_and_stop(self):
        base_rows, markers = e2e_fixture()
        ai_init = (
            "=== goh_ai InitThread starting (79 hooks across 4 TUs; ini=loaded log=summary) ===\n"
            f"PE SHA-256 verified: {m.SHA} (build 26734796)\n"
            "PE32+ live RTTI vtable audit: checked=38 matched=38 mismatched=0 missing=0\n"
            "Initial state: remote=0 hostEmit=0 clock=0 L=20.0 G=0.0001962\n"
            "=== 79 hooks active (slot23=1 combat=23 infantry=46 tanks=9 runtime_allowed=1) ===\n"
        )
        trial_log = (
            f"Mod: activate mod ai_test_harness\n"
            f"Start mission {launch.AUTO_MISSION_PATH} on difficulty heroic with options []\n"
            f"GUI: Activate page page_scene_game\n{markers}\n"
        )
        trial_jsonl = "".join(json.dumps(r) + "\n" for r in base_rows)
        trial_jsonl_no_header = "".join(json.dumps(r) + "\n" for r in base_rows[1:])

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            game = root / "game"
            binaries = game / "binaries/x64"
            binaries.mkdir(parents=True)
            (binaries / "version.dll").write_bytes(b"proxy")
            (binaries / "goh_ai_mod.dll").write_bytes(b"dll-v1")
            original_ini = b"; user ini\n[modules]\nauto_smoke = true\n"
            ini = binaries / "goh_ai.ini"
            ini.write_bytes(original_ini)

            profile_dir = root / "Documents/My Games/gates of hell/profiles/123"
            profile_dir.mkdir(parents=True)
            options = profile_dir / "options.set"
            original_options = b"{options\n\t{video {preset high}}\n}\n"
            options.write_bytes(original_options)
            game_log = root / "Documents/My Games/gates of hell/log/game.log"
            game_log.parent.mkdir(parents=True)

            state = {
                "running": False,
                "popen_cmds": [],
                "restart_seq": 0,
                "quit_seq": 0,
                "gen": 1,
                "ui_calls": [],
            }

            def append_trial(*, include_header):
                with game_log.open("a", encoding="utf-8") as f:
                    f.write(trial_log)
                with (binaries / "goh_telemetry.jsonl").open("a", encoding="utf-8") as f:
                    f.write(trial_jsonl if include_header else trial_jsonl_no_header)
                if include_header:
                    with (binaries / "goh_ai.log").open("a", encoding="utf-8") as f:
                        f.write(ai_init)

            class FakeProc:
                def poll(self):
                    return None if state["running"] else 0

            def fake_popen(cmd, **kwargs):
                state["popen_cmds"].append(cmd)
                state["running"] = True
                cfg = configparser.ConfigParser(strict=False, interpolation=None)
                cfg.read(ini, encoding="utf-8-sig")
                self.assertTrue(cfg.getboolean("testing", "suppress_cursor_grab"))
                self.assertTrue(cfg.getboolean("testing", "hot_reload"))
                state["restart_seq"] = cfg.getint("testing", "restart_seq", fallback=0)
                state["quit_seq"] = cfg.getint("testing", "quit_seq", fallback=0)
                game_log.write_text("", encoding="utf-8")
                append_trial(include_header=True)
                return FakeProc()

            orig_atomic_write = cli.atomic_write

            def watching_atomic_write(path, data):
                orig_atomic_write(path, data)
                if path == ini and state["running"]:
                    cfg = configparser.ConfigParser(strict=False, interpolation=None)
                    cfg.read_string(data.decode("utf-8-sig"))
                    rseq = cfg.getint("testing", "restart_seq", fallback=state["restart_seq"])
                    qseq = cfg.getint("testing", "quit_seq", fallback=state["quit_seq"])
                    if qseq != state["quit_seq"]:
                        state["quit_seq"] = qseq
                        state["running"] = False
                    elif rseq != state["restart_seq"]:
                        state["restart_seq"] = rseq
                        append_trial(include_header=False)

            def fake_ui(*args):
                state["ui_calls"].append(args)
                if args == ("present",):
                    return 0 if state["running"] else 3
                if args == ("dismiss",):
                    return 3
                return 0

            def fake_sleep(_):
                trigger = binaries / "goh_ai.reload"
                if trigger.exists():
                    trigger.unlink()
                    state["gen"] += 1
                    with (binaries / "version_proxy.log").open("a", encoding="utf-8") as f:
                        f.write(f"Hot-reload complete gen={state['gen']} (size=12345)\n")
                    with (binaries / "goh_ai.log").open("a", encoding="utf-8") as f:
                        f.write(ai_init)

            with (
                patch.object(launch, "ROOT", root),
                patch.object(launch, "wine_prefix", return_value=([], {})),
                patch.object(launch, "build_helper", return_value=root / "helper"),
                patch.object(launch, "_make_ui_env", return_value=fake_ui),
                patch.object(launch.subprocess, "Popen", side_effect=fake_popen),
                patch.object(launch.time, "sleep", side_effect=fake_sleep),
                patch.object(cli, "atomic_write", side_effect=watching_atomic_write),
                patch.object(cli, "verify_game"),
                patch.object(m, "check_stock"),
                patch.object(m, "e2e_map_files", return_value={}),
                patch.object(m, "birdseye_camera_reg", return_value=b"{camera}"),
                patch.object(m, "birdseye_lod_files", return_value={}),
            ):
                # 1. Default two-trial sweep keeps the game open and uses ONE process launch + in-process cmGameRestart.
                out = StringIO()
                with contextlib.redirect_stdout(out):
                    rc = cli.main([
                        "sweep", "--game-dir", str(game), "--player-profile", str(options),
                        "--profiles", "default", "--runs", "2",
                        "--screenshot-interval", "0", "--timeout", "5",
                    ])
                self.assertEqual(rc, 0)
                self.assertEqual(len(state["popen_cmds"]), 1)
                self.assertIn("-no_preload", state["popen_cmds"][0])
                self.assertNotIn("-no_reload_caching", state["popen_cmds"][0])
                self.assertTrue(state["running"])
                self.assertTrue(launch.session_path().is_file())
                self.assertEqual(state["restart_seq"], 1)

                # 2. Rebuild DLL, reload --force-dll, and run a second sweep with default args (auto-reuses & stays open).
                (binaries / "goh_ai_mod.dll").write_bytes(b"dll-v2-rebuilt")
                out2 = StringIO()
                with contextlib.redirect_stdout(out2):
                    self.assertEqual(cli.main(["reload", "--game-dir", str(game), "--force-dll"]), 0)
                self.assertIn("dll_reloaded=True", out2.getvalue())
                self.assertEqual(state["gen"], 2)
                self.assertEqual(state["restart_seq"], 2)

                out3 = StringIO()
                with contextlib.redirect_stdout(out3):
                    rc3 = cli.main([
                        "sweep", "--game-dir", str(game), "--player-profile", str(options),
                        "--profiles", "default", "--runs", "1",
                        "--screenshot-interval", "0", "--timeout", "5",
                    ])
                self.assertEqual(rc3, 0)
                self.assertEqual(len(state["popen_cmds"]), 1)
                self.assertEqual(state["restart_seq"], 3)
                self.assertTrue(state["running"])

                # 3. Passing --close reuses the open session for one last trial, then exits via native cmQuit & restores.
                out4 = StringIO()
                with contextlib.redirect_stdout(out4):
                    rc4 = cli.main([
                        "sweep", "--game-dir", str(game), "--player-profile", str(options),
                        "--profiles", "default", "--runs", "1", "--close",
                        "--screenshot-interval", "0", "--timeout", "5",
                    ])
                self.assertEqual(rc4, 0)
                self.assertEqual(len(state["popen_cmds"]), 1)
                self.assertEqual(state["restart_seq"], 4)
                self.assertFalse(state["running"])
                self.assertFalse(launch.session_path().is_file())
                self.assertEqual(options.read_bytes(), original_options)
                self.assertEqual(ini.read_bytes(), original_ini)

                # 4. Default sweep followed by explicit `stop` also closes cleanly via cmQuit & restores.
                with contextlib.redirect_stdout(StringIO()):
                    rc5 = cli.main([
                        "sweep", "--game-dir", str(game), "--player-profile", str(options),
                        "--profiles", "default", "--runs", "1",
                        "--screenshot-interval", "0", "--timeout", "5",
                    ])
                self.assertEqual(rc5, 0)
                self.assertTrue(state["running"])
                self.assertEqual(len(state["popen_cmds"]), 2)
                with contextlib.redirect_stdout(StringIO()):
                    self.assertEqual(cli.main(["stop", "--game-dir", str(game)]), 0)
                self.assertFalse(state["running"])
                self.assertFalse(launch.session_path().is_file())
                self.assertEqual(options.read_bytes(), original_options)
                self.assertEqual(ini.read_bytes(), original_ini)
                # Ensure zero interactive UI actions (click/esc/space/close) were ever invoked.
                for call in state["ui_calls"]:
                    self.assertIn(call[0], ("present", "dismiss"))


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
        fake_oryol = {name: name.encode() for name in m.E2E_MAP_FILES}
        fake_lod = {p: p.name.encode() for p in m.E2E_LOD_FILES}
        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.object(cli, "verify_game"),
            patch.object(m, "check_stock"),
            patch.object(m, "e2e_map_files", return_value=fake_oryol),
            patch.object(m, "birdseye_camera_reg", return_value=b"{camera_clip}"),
            patch.object(m, "birdseye_lod_files", return_value=fake_lod),
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
            cli.install(
                game,
                "e2e",
                "fast",
                cli.overrides(["medic_search_radius_m=42", "logging.level=verbose", "testing.sim_speed=12"]),
            )
            self.assertEqual(
                {p.name for p in (game / "mods").rglob("*") if p.is_file()},
                {"0.mi", "mod.info", "camera.reg", "heroic.dl", *(p.name for p in m.E2E_LOD_FILES), *m.E2E_MAP_FILES},
            )
            self.assertIn('{name "ai_test_harness"}', descriptor.read_text())
            self.assertIn('{minGameVersion "1.069"}', descriptor.read_text())
            self.assertIn('{maxGameVersion "1.069"}', descriptor.read_text())
            self.assertIn("extra_grenades=original", ini.read_text())
            self.assertIn("enabled=true", ini.read_text())
            self.assertIn("medic_search_radius_m=42", ini.read_text())
            cfg = configparser.ConfigParser()
            cfg.read(ini)
            self.assertEqual(cfg.getfloat("testing", "sim_speed"), 12)
            self.assertEqual(target.read_text(), m.generate("e2e", "fast"))
            # Switching back to a component suite strips the 1v1 Oryol map overlay cleanly.
            cli.install(game, "full", "normal", {})
            self.assertEqual(
                {p.name for p in (game / "mods").rglob("*") if p.is_file()},
                {"0.mi", "mod.info"},
            )
            cli.install(game, "e2e", "fast", {})
            extra = game / "mods" / cli.MOD / "personal"
            extra.touch()
            with self.assertRaises(ValueError):
                cli.clean(game)
            extra.unlink()
            cli.clean(game)
            cli.clean(game)
            self.assertTrue(ini.exists())
            self.assertFalse(target.exists())
            self.assertFalse((game / "mods" / cli.MOD).exists())

    def test_e2e_map_files_and_birdseye_camera_reg(self):
        import zipfile

        sample_reg = (
            "{camera\n\t{main\n\t\t{zfar\t10000}\n\t}\n}\n"
            "{camera_clip\n\t{max_distance\n\t\t{distance\t1800}\n\t\t{viewMin\t10}\n\t\t{viewMax\t85}\n\t}\n"
            "\t{default\n\t\t{distance\t850}\n\t\t{viewAngle\t37.5}\n\t}\n}\n"
            "{lens_effects {outlines {intensity_regular 3.0}{intensity_emphasized 5.0}{outline_width 1.0}"
            "{active_outlines {friendly 1}{hostile 1}{allied 1}{neutral 0}}"
            "{active_outlines_hc {friendly 1}{hostile 0}{allied 1}{neutral 1}}"
            "{allied_color 0xFF89734F}{neutral_color 0xFFB8B800}{hostile_color 0xFFB80000}"
            "{distance_fade_start 2500}{distance_fade_end 3000}{time_active 3.0}{time_active_HC 2.0}}}\n"
        ).encode("latin1")
        sample_lod_set = b'{levelofdetail (define "scale" (* 1.00 %d)) (define "scale2" (* 1.4 %d2))}\n'
        sample_lod_inc = (
            b'{"human" {distance ("scale" d(400))}{distance ("scale" d(6000)) ("off")}}\n'
            b'{"stuff_grenade" {distance ("scale" d(4000)) ("off_small")}}\n'
            b'{"vehicle" {distance ("scale" d(9000)) ("off")}}\n'
        )
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            (game / "resource/map").mkdir(parents=True)
            with zipfile.ZipFile(game / "resource/map/multi.pak", "w") as z:
                for name in m.E2E_MAP_FILES:
                    z.writestr(f"{m.E2E_MAP_SRC}/{name}", b"payload:" + name.encode())
            with zipfile.ZipFile(game / "resource/gamelogic.pak", "w") as z:
                z.writestr("set/registry/camera.reg", sample_reg)
                z.writestr("set/level_of_detail.set", sample_lod_set)
                z.writestr("set/level_of_detail_scenario.set", sample_lod_set)
                z.writestr("set/level_of_detail.inc", sample_lod_inc)
            files = m.e2e_map_files(game)
            cam = m.birdseye_camera_reg(game).decode("latin1")
            lods = {k: v.decode("latin1") for k, v in m.birdseye_lod_files(game).items()}
        self.assertEqual(set(files), set(m.E2E_MAP_FILES))
        self.assertEqual(set(lods), set(m.E2E_LOD_FILES))
        self.assertIn("(* 5.00 %d)", lods[Path("resource/set/level_of_detail.set")])
        self.assertIn("(* 5.00 %d2)", lods[Path("resource/set/level_of_detail_scenario.set")])
        self.assertEqual(lods[Path("resource/set/level_of_detail.inc")].count("d(25000)"), 3)
        for name in m.E2E_MAP_FILES:
            self.assertEqual(files[name], b"payload:" + name.encode())
        for tok in ("{zfar\t25000}", "{distance\t8000}", "{viewMax\t89}", "{distance\t4200}", "{viewAngle\t65}",
                    "{outline_width 3.0}", "{active_outlines {friendly 1}{hostile 1}{allied 1}{neutral 1}}",
                    "{active_outlines_hc {friendly 1}{hostile 1}{allied 1}{neutral 1}}",
                    "{allied_color 0xFF2898FF}", "{neutral_color 0xFF2898FF}", "{hostile_color 0xFFFF2424}",
                    "{distance_fade_start 8000}", "{distance_fade_end 10000}",
                    "{time_active 999.0}", "{time_active_HC 999.0}"):
            self.assertIn(tok, cam)
        self.assertEqual(
            launch.enable_silhouettes_bytes(b"{settings\n\t{highlight_silhouettes 0}\n}\n"),
            b"{settings\n\t{highlight_silhouettes}\n}\n",
        )
        self.assertIn(b"{highlight_silhouettes}", launch.enable_silhouettes_bytes(b"{settings\n}\n"))
        from scripts import live_visual

        proj = live_visual.make_projector((125.0, -2600.0, 5768.0), (125.0, -200.0, 10.0), 50.0, 1800, 1125)
        cx, cy = proj(125.0, -200.0, 10.0)
        self.assertAlmostEqual(cx, 900.0, places=3)
        self.assertAlmostEqual(cy, 562.5, places=3)
        # GEM left-handed Z-up: +X projects left of center, +Y projects above center (bottom-up CoreGraphics Y).
        lx, _ = proj(1000.0, -200.0, 10.0)
        _, uy = proj(125.0, 600.0, 10.0)
        self.assertLess(lx, cx)
        self.assertGreater(uy, cy)
        with tempfile.TemporaryDirectory() as tmp:
            tl = Path(tmp) / "goh_telemetry.jsonl"
            tl.write_text('{"sim_tick": 100}\n{"sim_tick": 91234}\n{"partial": ', encoding="utf-8")
            self.assertEqual(live_visual.latest_sim_tick(tl), 91234)

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
