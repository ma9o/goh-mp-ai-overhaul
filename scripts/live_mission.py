"""GEM mission for the build-26734796 vehicle-control map. Coordinates are world units."""

import math
import re
import struct
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

SHA = "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a"
MEMBER = "single/00-bootcamp/02_vehicle-control/0.mi"
MI = Path("resource/map") / MEMBER
SCALE = 20.0
INF = "infantry_combined_ops"
ARM = "armor_combined_ops"
CLOSE = "close_at_and_authority"
# Each probe gets two isolated observation windows and disjoint mission IDs.
# (default, below margin, above margin, engine min, engine max, qualifying counter)
PROBES = {
    "grenade_danger_radius_m": (5, 1.5, 4, 2, 20, "dodges"),
    "medic_search_radius_m": (36, 2, 2, 10, 100, "med_claim"),
    "squad_share_radius_m": (18, 2, 2, 6, 40, "squad_ammo"),
    "resupply_search_radius_m": (60, 3, 3, 15, 150, "sup_trip"),
    "tank_facing_min_angle_deg": (24, 8, 18, 15, 60, "tank_face"),
}
SUITES = ("full", "infantry", "armor", "close_at", "calibration")
SPEEDS = ("normal", "fast", "very_fast")
GER = "mp/ger/mid/"
RUS = "mp/rus/mid/"
RIFLE = "rifleman_1"
BELT = '"mgun_ger" "belt" "ammo"'
SMOKE = '"m24_smoke" "grenade"'
AT = '"m24_geballteladung" "grenade"'
MOLOTOV = '"molotov_cocktail" "grenade"'
SEATS = {
    "panzer4d": "driver gunner charger gunner3 commander",
    "t3476_40": "driver gunner gunner3 commander",
    "ba64": "driver gunner",
    "45mm_m37": "gunner charger charger2",
    "sdkfz251c": "driver gunner2",
    "blitz3_6": "driver",
    "blitz3_6_art_ammo": "driver",
}
ENVIRONMENT = "goh/single/02-ger/1944_09_yugoslavia/01"


def command(name, body=""):
    return '{"' + name + '" ' + body + "}"


def delay(seconds):
    return command("delay", f"{{time {seconds:g}}}")


def marker(edge, name):
    return command("con", f"{{text GOH_L1_{edge}_{name}}}")


def overview_cut(name):
    return command(
        "scenario",
        '{message {text ""}}'
        f'{{camera {{name "cam_{name}"}}{{smooth_begin 0}}{{smooth_end 0}}}}'
        "{do_not_store_camera}{do_not_restore_camera}{force_auto_complete}",
    )


@dataclass
class Phase:
    name: str
    index: int
    objects: list = field(default_factory=list)
    relations: list = field(default_factory=list)
    actions: list = field(default_factory=list)
    waypoints: list = field(default_factory=list)
    positions: list = field(default_factory=list)
    mids: dict = field(default_factory=dict)
    probe: str = ""
    setting: float = 0
    distance: float = 0

    def overview(self):
        xs, ys = zip(*self.positions)
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        span = math.hypot(max(xs) - min(xs), max(ys) - min(ys))
        scale = max(1, span / 1100)
        view = (
            f"{{position {cx:g} {cy - 1200 * scale:g} {10 + 950 * scale:g}}}"
            f"{{look_at {cx:g} {cy:g} 10}}{{fov 50}}"
        )
        return f'{{"cam_{self.name}" {{"key000" {view}}}{{"key001" {{time 0.5}}{view}}}}}'

    def tag(self, role):
        return f"h{self.index}_{role}"

    def selector(self, role):
        return f"{{selector {{ignore_captured_by_user 0}}{{tag {self.tag(role)}}}}}"

    def act(self, name, role, body):
        self.actions.append(command(name, self.selector(role) + body))

    def add(
        self,
        role,
        model,
        x,
        y,
        player=None,
        yaw=0,
        mode="free",
        groups=(),
        body="",
        fire="open",
        linked=False,
        wave="entry",
    ):
        if role in self.mids or len(self.mids) >= 99:
            raise ValueError(f"duplicate role or phase capacity exceeded: {role}")
        mid = self.index * 100 + len(self.mids) + 1
        self.mids[role] = mid
        self.positions.append((x, y))
        eid = hex(0x8000 + mid)
        human = model.startswith("mp/")
        kind = "Human" if human else "Entity"
        owner = "" if player is None else f"{{Player {player}}}"
        mission_id = f"{{MID {mid}}}" if player is not None else ""
        self.objects.append(
            f'{{{kind} "{model}" {eid} {{Position {x:g} {y:g}}}{{xform zl {yaw:g}}}{body}{owner}{mission_id}}}'
        )
        if player == 2 and (human or model in SEATS):
            groups = (*groups, "opposition")
        # GEM propagates a carrier's inactive state to its linked crew.
        stage = () if linked else ("hidden", "harness_all", self.tag(wave))
        labels = (
            *stage,
            self.tag("phase"),
            self.tag(role),
            *(self.tag(g) for g in groups),
        )
        self.relations.append(
            "{Tags " + " ".join(f'"{label}"' for label in labels) + " " + eid + "}"
        )
        if player is not None and (human or model in SEATS):
            control = "user" if player == 0 else "AI"
            self.relations.append(
                f"{{Brain {eid} {{Properties {{control {control}}}{{move_mode {mode}}}{{fire_mode {fire}}}}}}}"
            )
        return eid

    def human(self, role, breed, x, y, player=0, **kw):
        return self.add(role, (GER if player == 0 else RUS) + breed, x, y, player, **kw)

    def vehicle(self, role, model, x, y, player=0, yaw=0, riders=0, **kw):
        eid = self.add(role, model, x, y, player, yaw, **kw)
        seats = SEATS[model].split() + [f"seat{i + 1}" for i in range(riders)]
        for seat in seats:
            breed = "tankman" if not seat.startswith("seat") else RIFLE
            crew = self.human(role + "_" + seat, breed, x, y, player, linked=True)
            self.relations.append(f'{{Link {crew} {{{eid} "{seat}"}}}}')
        return eid

    def kit(self, role, *items):
        eid = hex(0x8000 + self.mids[role])
        box = "".join("{item " + item + "}" for item in items)
        self.relations.append(f"{{Inventory {eid} {{box {{clear}}{box}}}}}")

    def inventory(self, role, item, amount):
        self.act(
            "inventory", role, f"{{action add}}{{item {item}}}{{amount {amount:d}}}"
        )

    def squad(self, role="squad"):
        self.act("squad_create", role, f"{{name {self.tag(role)}}}")

    def point(self, x, y, z):
        wid = str(self.index * 10 + len(self.waypoints))
        self.waypoints.append(f'{{"{wid}" {{position {x:g} {y:g} {z:g}}}}}')
        return f'{{waypoint "{wid}"}}'

    def thrower(self, x, y, aim):
        self.human(
            "thrower",
            RIFLE,
            x,
            y,
            2,
            mode="hold",
            fire="hold",
            body="{Impregnability full}",
        )
        self.kit("thrower", '"f1" "grenade" {cell 0 0}{user "hand_right"}')
        self.act("actor_state", "thrower", "{fire_mode open}")
        self.act(
            "actor_fire",
            "thrower",
            "{attack position}" + self.point(*aim) + "{burst_mode once}{time 2}",
        )

    def mg(self, x, y, rounds=0):
        self.human("mg", "mgun_1", x, y, groups=("squad",))
        kit = ['"mg34_belt" {cell 0 0}{user "hand_right"}']
        if rounds:
            kit.append(f"{BELT} {rounds} {{cell 0 3}}")
        self.kit("mg", *kit)

    def dry_rifleman(self, role, x, y):
        # Stock firearms inherit {preloaded} from bullet.pattern. Give an SMG
        # soldier a single preloaded 32-round MP40 (500 rpm) with no spare mags;
        # expend_dry() fires it empty into nearby open ground.
        self.human(role, "smg_1", x, y, yaw=-90, mode="hold", fire="open", body="{Impregnability full}")
        self.kit(
            role,
            '"mp40_unfolded" filled {cell 0 0}{user "hand_right"}',
        )

    def expend_dry(self, role, x, y, duration=12):
        # Spawn a harmless impregnable target 5 m south so the SMG empties its
        # single 32-round mag natively at point-blank range, then remove it.
        target = f"{role}_target"
        self.human(
            target,
            RIFLE,
            x,
            y - 100,
            2,
            mode="hold",
            fire="hold",
            body="{Impregnability full}",
        )
        self.kit(target, '"k98" filled {cell 0 0}{user "hand_right"}')
        self.actions.append(delay(duration))
        self.act("delete", target, "")

    def supply(self, x, y, wave="entry"):
        self.vehicle(
            "truck",
            "blitz3_6_art_ammo",
            x,
            y,
            mode="hold",
            # Vehicle Control uses current 0 (spent supply). truck_ammo_heavy
            # includes German rifle clips and MG belts within its 12 m zone.
            body='{Extender "supply_zone" {enabled}{current 0}}',
            wave=wave,
        )

    def down(self, role):
        # Bootcamp 2-combat/events/03-rifledie uses this sequence for its revivable patient.
        self.act("actor_state", role, "{control user}{move_mode hold}")
        self.act("effect", role, "{effect die}")


def infantry():
    p = Phase(INF, 1)
    p.human("medic", "medic", 320, 300, groups=("squad",))
    p.kit(
        "medic",
        '"luger" filled {cell 0 0}{user "hand_right"}',
        '"bandage_ger" 8 {cell 3 0}',
    )
    p.human("donor", RIFLE, 435, 170, mode="hold", groups=("squad",), body="{Impregnability full}")
    p.inventory("donor", SMOKE, 3)
    p.inventory("donor", BELT, 100)
    p.mg(475, 335, 3)
    p.human("at", "antitank_1", 595, 325, groups=("squad",), body="{Impregnability full}")
    p.inventory("at", AT, 3)
    p.human("rifle", RIFLE, 545, 340, groups=("squad",))
    # Keep clear of flag_point_1's 15 m stock flagpoint_ammo aura while remaining
    # outside the supply truck's 12 m zone (15.2 m away) so a trip is required.
    p.dry_rifleman("empty", -160, 100)
    p.human("patient_in", RIFLE, 320, 860, mode="hold")
    p.human("patient_out", RIFLE, -560, 300, mode="hold")
    p.add("sandbags", "sandbag_structured_straight", 465, 385)
    p.add("timber", "fence-wood_3", 610, 402)
    p.add(
        "flag",
        "flag_point_1",
        30,
        520,
        2,
        body='{Extender "map_point" {name f1}{zone {radius 0}{zoneId "f1"}}'
        "{phase 0}{order 1}{show All}{visor MapPoint}{allow {enableCapture}{enableRelease}}}",
    )
    p.supply(-370, 320)
    p.vehicle("car", "ba64", 820, 540, 2, -135, mode="hold", body="{Impregnability full}")
    # This stationary scout shares the car's close blast footprint.
    p.human("scout", RIFLE, 820, 607, mode="hold", fire="hold")
    for i, (x, y) in enumerate(((670, 840), (785, 870), (825, 910))):
        p.human(f"enemy{i}", RIFLE, x, y, 2)
    p.squad()
    p.down("patient_in")
    p.down("patient_out")
    # Give the carrier a stationary source of incoming fire and time to release
    # before the F1 forces a dodge or changes the protected squad member.
    p.act("actor_fire", "car", f"{{attack enemies}}{{enemies {{tag {p.tag('donor')}}}}}{{time 12}}")
    p.expend_dry("empty", -160, 100, duration=12)
    p.thrower(515, 670, (508, 345, 9))
    p.actions.append(delay(40))
    # Give the low-ammo squad a lull and the flag a local uncontested approach.
    p.act("delete", "opposition", "")
    p.actions.append(delay(70))
    return p


def armor():
    p = Phase(ARM, 2)
    p.vehicle("tank", "panzer4d", -340, 480, yaw=50, riders=2, body="{Impregnability full}")
    p.human("replacement", "tankman", -400, 415)
    p.vehicle("halftrack", "sdkfz251c", -180, 240, riders=4)
    # Broadside, off the fence line. Stock KwK 37 AP and HEAT get a clear
    # first contact on the T-34's side during the stationary engagement.
    p.vehicle("hostile_tank", "t3476_40", 480, 810, 2, 112, mode="hold",
              fire="hold", body="{Impregnability full}", wave="opponents")
    p.vehicle("gun", "45mm_m37", 900, 690, 2, 190, wave="opponents")
    p.human("lone", RIFLE, 100, 380, 2, body="{Impregnability full}", wave="opponents")
    # The unrotated fence runs north/south across the tank's eastbound route.
    p.add("fence", "fence-wood_3", 140, 480)
    # The stock Sd.Kfz. carries both the tank property and track components.
    # The wheeled troop truck also carries real passengers and qualifies for unload.
    p.vehicle("troop_truck", "blitz3_6", -440, 210, riders=3)
    p.vehicle("desant_tank", "t3476_40", -900, 2300, riders=4, mode="hold",
              fire="hold", body="{Impregnability full}")
    p.inventory("tank", '"repair_kit"', 4)
    # Sample the intact column first. Damage it while passengers are still
    # aboard, before stock emit-on-enemy behavior can evacuate either vehicle.
    p.actions.append(delay(3))
    # Vacate the bow gunner's seat before combat or repair dispatch. The
    # individual crew selector keeps all four exterior passengers on board.
    p.act("emit", "desant_tank", f"{{crew {{ignore_captured_by_user 0}}{{tag {p.tag('desant_tank_gunner3')}}}}}"
          "{emit {mode crew}{count 1}}")
    p.actions.append(delay(5))
    p.act("delete", "desant_tank_gunner3", "")
    p.actions.append(delay(15))
    p.act("actor_component", "halftrack", "{component engine}{state broken}")
    p.act("actor_component", "troop_truck", "{component engine}{state broken}")
    p.act("entity_state", "opponents", "{tag_remove hidden}{inactive off}")
    p.actions.append(delay(2))
    p.act(
        "actor_fire",
        "gun",
        "{{attack enemies}}{{enemies {{tag {}}}}}{{interval 4}}{{time 16}}".format(
            p.tag("tank")
        ),
    )
    # Let the stationary tank acquire both weapon targets before it advances.
    p.actions.append(delay(20))
    p.act("actor_state", "hostile_tank", "{fire_mode open}")
    p.act("actor_to_waypoint", "tank", "{drop orders}" + p.point(310, 480, 4))
    p.actions.append(delay(14))
    p.act("actor_to_waypoint", "tank", "{drop orders}" + p.point(-100, 480, 2))
    p.actions.append(delay(8))
    p.act("actor_component", "tank", "{component trackright}{state broken}")
    p.actions.append(delay(7))
    p.act("actor_component", "tank", "{component gun}{state broken}")
    p.actions.append(delay(65))
    return p


def close_at():
    p = Phase(CLOSE, 3)
    for role, breed, x, y, mode in (
        ("hold", "antitank_1", 355, 435, "hold"),
        ("mobile", RIFLE, 495, 490, "free"),
        ("molotov", RIFLE, 485, 205, "free"),
        ("hold_fire", RIFLE, 290, 365, "hold"),
    ):
        p.human(
            role,
            breed,
            x,
            y,
            mode=mode,
            groups=() if role == "hold_fire" else ("squad",),
            fire="hold" if role == "hold_fire" else "open",
            # Preserve the Hold probes throughout the window. Death physics can move a body several metres.
            body="{Impregnability full}" if mode == "hold" else "",
        )
        if role in ("mobile", "molotov"):
            grenade = AT if role == "mobile" else MOLOTOV
            p.kit(
                role,
                '"k98" filled {cell 0 0}{user "hand_right"}',
                f"{grenade} 2 {{cell 0 2}}",
                '"rifle_ger" "clip" "ammo" 60 {cell 3 2}',
                '"bandage_ger" 4 {cell 4 2}',
            )
        else:
            p.inventory(role, AT, 2)
    p.squad()
    p.act("actor_state", "hold_fire", "{fire_mode hold}")
    # This short wall runs east/west. Both lateral approaches remain open.
    p.add("wall", "dutch_brick_wall01_straight_short", -900, 2300)
    p.add("corner", "dutch_brick_wall01_straight_short", -865, 2320, yaw=90)
    # Hold vehicle fire on the approach so the screening infantry can react.
    for role, model, x, y in (
        ("hostile_tank", "t3476_40", 915, 490),
        ("car", "ba64", 880, 205),
    ):
        p.vehicle(
            role,
            model,
            x,
            y,
            2,
            180,
            fire="hold",
            wave="armor_contact",
            body="{Impregnability full}",
        )
    p.human(
        "enemy",
        RIFLE,
        -900,
        2420,
        2,
        mode="hold",
        fire="hold",
        body="{Impregnability full}",
    )
    p.human("flanker", RIFLE, -900, 2240, yaw=90, body="{Impregnability full}")
    p.human("launcher", RIFLE, 260, 225, mode="hold", body="{Impregnability full}")
    # Start holding the launcher; bait infantry triggers a rifle swap (starting
    # stock's 5-10 s weapon-take lockout) right before armor_contact rolls in.
    p.kit(
        "launcher",
        '"k98" filled {cell 0 0}',
        '"panzerfaust_60" "weapon" filled {cell 0 2}{user "hand_right"}',
    )
    p.human(
        "bait_inf",
        RIFLE,
        480,
        140,
        2,
        mode="hold",
        fire="hold",
        body="{Impregnability full}",
        wave="swap_bait",
    )
    p.kit("bait_inf", '"k98" filled {cell 0 0}{user "hand_right"}')
    # Bootcamp 03 uses action attack with a target selector. actor_fire
    # enemies builds AttackBySelector/WeaponFire, bypassing eAttack's detour.
    p.actions.append(delay(2))
    p.act("action", "flanker", f"{{drop orders}}{{action attack}}{{target {{tag {p.tag('enemy')}}}}}")
    p.actions.append(delay(6))
    p.act("entity_state", "swap_bait", "{tag_remove hidden}{inactive off}")
    p.actions.append(delay(4))
    p.act("entity_state", "armor_contact", "{tag_remove hidden}{inactive off}")
    # Stop in separate clear throwing lanes. The stock AT bundle reaches 16 m
    # and the Molotov reaches just 8 m, measured to the vehicle damage volume.
    p.act("actor_to_waypoint", "hostile_tank", "{drop orders}" + p.point(705, 490, 19))
    p.act("actor_to_waypoint", "car", "{drop orders}" + p.point(600, 205, 14))
    p.actions.append(delay(52))
    return p


def calibration(tuning):
    phases = []
    for i, (key, (default, lower_gap, upper_gap, lo, hi, _)) in enumerate(
        PROBES.items()
    ):
        threshold = float(tuning.get(key, default))
        if not math.isfinite(threshold) or not lo <= threshold <= hi:
            raise ValueError(f"{key} must be in [{lo}, {hi}]")
        below, above = threshold - lower_gap, threshold + upper_gap
        if key == "grenade_danger_radius_m":
            below = max(2, below)
        elif key == "tank_facing_min_angle_deg":
            below, above = max(10, below), max(42, above)
        for side, distance in (("below", below), ("above", above)):
            p = Phase(
                f"cal_{key}_{side}",
                4 + 2 * i + (side == "above"),
                probe=key,
                setting=threshold,
                distance=distance,
            )
            # Stock open field at y=470. Long-range overrides use the longitudinal lane.
            span = distance * SCALE
            if span <= 2220:
                a, b = (-span / 2, 470), (span / 2, 470)
            else:
                a, b = (-630, 175), (-630, 175 + span)
            if key == "grenade_danger_radius_m":
                p.human("subject", RIFLE, 640, 370 + span, mode="hold", fire="hold")
                p.act("actor_state", "subject", "{fire_mode hold}")
                p.actions.append(delay(3))
                p.thrower(640, 120, (640, 370, 16))
            elif key == "medic_search_radius_m":
                p.human("subject", "medic", *a)
                p.human("target", RIFLE, *b, mode="hold")
                p.actions.append(delay(3))
                p.down("target")
            elif key == "squad_share_radius_m":
                p.mg(*a)
                p.human("target", RIFLE, *b, mode="hold", groups=("squad",))
                p.squad()
                p.actions.append(delay(3))
                p.inventory("target", BELT, 200)
            elif key == "resupply_search_radius_m":
                p.dry_rifleman("subject", *a)
                p.supply(*b, wave="delivery")
                p.expend_dry("subject", *a, duration=15)
                # Let the 5 s danger sighting memory expire before revealing the truck.
                p.actions.append(delay(7))
                p.act("entity_state", "delivery", "{tag_remove hidden}{inactive off}")
            else:
                p.vehicle("subject", "panzer4d", -340, 480, yaw=distance, body="{Impregnability full}")
                p.vehicle(
                    "target",
                    "t3476_40",
                    560,
                    480,
                    2,
                    180,
                    mode="hold",
                    fire="open",
                    body="{Impregnability full}",
                    wave="armor_contact",
                )
                p.actions.append(delay(3))
                p.act(
                    "entity_state", "armor_contact", "{tag_remove hidden}{inactive off}"
                )
            p.actions.append(delay(32))
            phases.append(p)
    return phases


def phases_for(suite, tuning=None):
    if suite == "calibration":
        return calibration(tuning or {})
    constructors = {"infantry": infantry, "armor": armor, "close_at": close_at}
    return (
        [f() for f in constructors.values()]
        if suite == "full"
        else [constructors[suite]()]
    )


def parse(text):
    """Read generated GEM brace records, treating quoted atoms as opaque."""
    stack = [[]]
    for token in re.findall(r'"[^"\n]*"|[{}]|[^{}\s"]+|"', text):
        if token == '"':
            raise ValueError("unterminated string")
        if token == "{":
            child = []
            stack[-1].append(child)
            stack.append(child)
        elif token == "}":
            if len(stack) == 1:
                raise ValueError("extra closing brace")
            stack.pop()
        else:
            stack[-1].append(token.strip('"'))
    if len(stack) != 1 or len(stack[0]) != 1 or stack[0][0][0] != "mission":
        raise ValueError("unbalanced or invalid mission root")
    return stack[0][0]


def nodes(tree):
    yield tree
    for item in tree:
        if isinstance(item, list):
            yield from nodes(item)


def validate_overviews(root):
    (helpers,) = (n for n in root[1:] if n[0] == "Helpers")
    (cameras,) = (n[1:] for n in helpers[1:] if n[0] == "cameras")
    (actions,) = (n[1:] for n in nodes(helpers) if n[0] == "actions")
    prefix = "GOH_L1_BEGIN_"
    starts = [
        (i, a[1][1][len(prefix):]) for i, a in enumerate(actions)
        if a[0] == "con" and a[1][1].startswith(prefix)
    ]
    names = [f"cam_{name}" for _, name in starts]
    cuts = [a for a in actions if a[0] == "scenario"]
    if (not names or len(names) != len(set(names)) or len(cuts) != len(names)
            or [c[0] for c in cameras] != names):
        raise ValueError("expected one named overview camera per phase")
    for (i, name), camera in zip(starts, cameras):
        cut = parse("{mission " + overview_cut(name) + "}")[1]
        if actions[i + 1:i + 2] != [cut]:
            raise ValueError(f"missing camera cut at start of {name}")
        first, second = camera[1:]
        if (first[0] != "key000" or second != ["key001", ["time", "0.5"], *first[1:]]
                or [p[0] for p in first[1:]] != ["position", "look_at", "fov"]
                or first[-1] != ["fov", "50"]
                or any(len(p) != 4 or not all(math.isfinite(float(v)) for v in p[1:])
                       for p in first[1:3])):
            raise ValueError(f"invalid camera keys for {name}")


def validate(text):
    root = parse(text)
    validate_overviews(root)
    objects = [e for e in root[1:] if e[0] in ("Human", "Entity")]
    ids = [e[2] for e in objects]
    mids = [int(n[1]) for e in objects for n in e[3:] if n[0] == "MID"]
    if not all(re.fullmatch(r"0x[0-9a-f]+", eid) for eid in ids):
        raise ValueError("invalid hex entity ID")
    if len(ids) != len(set(ids)) or len(mids) != len(set(mids)):
        raise ValueError("missing or duplicate entity ID/MID")
    for e in objects:
        fields = [n[0] for n in e[3:]]
        if fields.count("MID") != int("Player" in fields):
            raise ValueError(f"expected one MID per actor and none on scenery: {e[1]}")
        if "Player" in fields and fields[-2:] != ["Player", "MID"]:
            raise ValueError(f"expected player and MID after extenders: {e[1]}")
    labels = {s for r in root[1:] if r[0] == "Tags" for s in r[1:-1]}
    wps = {wp[0] for n in nodes(root) if n[0] == "waypoints" for wp in n[1:]}
    for n in nodes(root):
        for atom in n:
            if isinstance(atom, str) and atom.startswith("0x") and atom not in ids:
                raise ValueError(f"undefined entity {atom}")
        if n[0] == "tag" and n[1] not in labels:
            raise ValueError(f"undefined tag {n[1]}")
        if n[0] == "zoneId" and not any(
            z[0] == "zone" and ["Name", n[1]] in z for z in nodes(root)
        ):
            raise ValueError(f"undefined zone {n[1]}")
        if n[0] == "waypoint" and n[1] not in wps:
            raise ValueError(f"undefined waypoint {n[1]}")
        if n[0].lower() == "position":
            x, y = map(float, n[1:3])
            if not (-1394 < x < 1400 and -5000 < y < 5000):
                raise ValueError(f"outside vehicle-control pather: {x}, {y}")
    return objects


def generate(suite="full", speed="normal", tuning=None):
    if speed not in SPEEDS or suite not in SUITES:
        raise ValueError("unknown suite or speed")
    phases = phases_for(suite, tuning)
    actions = [
        command(
            "entity_state",
            "{selector {ignore_captured_by_user 0}{tag harness_all}}{inactive on}",
        ),
        command("game_speed", f"{{mode {speed}}}"),
    ]
    for p in phases:
        actions.extend(
            (
                marker("BEGIN", p.name),
                overview_cut(p.name),
                command(
                    "entity_state",
                    p.selector("entry") + "{tag_remove hidden}{inactive off}",
                ),
            )
        )
        actions.extend(p.actions)
        actions.extend(
            (marker("END", p.name), command("delete", p.selector("phase")), delay(3))
        )
    # A live completion witness also works when GEM console output is unavailable.
    witness = Phase("complete", 90 + SUITES.index(suite))
    witness.human("done", RIFLE, 580, 300, mode="hold", fire="hold")
    actions.extend(
        (
            command("game_speed", "{mode normal}"),
            marker("DONE", suite),
            command(
                "entity_state",
                witness.selector("phase") + "{tag_remove hidden}{inactive off}",
            ),
        )
    )
    cameras = [p.overview() for p in phases]
    phases.append(witness)
    rows = [
        "{mission",
        *(o for p in phases for o in p.objects),
        *(r for p in phases for r in p.relations),
        f'{{Environment "{ENVIRONMENT}"}}',
        "{Helpers",
        "{cameras", *cameras, "}",
        *(
            ['{zone "circle" {position 30 520 0}{Name "f1"}{Radius 120}}']
            if any(p.name == INF for p in phases)
            else []
        ),
        "{waypoints",
        *(w for p in phases for w in p.waypoints),
        "}",
        '{triggers {"harness/run" {condition} {actions',
        *actions,
        "}}}}",
        '{Players {user 0}{alias 0 "player"}{alias 1 "ai"}{alias 2 "enemy"}{diplomacy',
        "1 2 1 1 1 1 1 1",
        "2 1 1 1 1 1 1 1",
        *(["1 1 1 1 1 1 1 1"] * 6),
        "}}",
        "{Fog 0}",
        "}",
    ]
    text = "\n".join(rows) + "\n"
    validate(text)
    return text


def check_stock(game, text):
    """Validate model/item catalogue, baked terrain and scene-ID namespace from the installed archives."""
    objects = validate(text)
    resource = game / "resource"
    with zipfile.ZipFile(resource / "map/single.pak") as pak:
        pak.getinfo(MEMBER)
        stock = pak.read(MEMBER.replace("0.mi", "map")).decode("latin1")
        grid = pak.read(MEMBER.replace("0.mi", "map.net"))
    width, height, ox, oy = struct.unpack_from("<HHff", grid, 2)
    offset = grid.index(b"GRID") + 4
    if len(grid) - offset != width * height * 4 or grid[:2] != b"\x05\x02":
        raise ValueError("unexpected map.net grid")
    map_ids = set(re.findall(r'\{Entity "[^"]+" (0x[0-9a-f]+)', stock))
    with zipfile.ZipFile(resource / "gamelogic.pak") as pak:
        logic = set(pak.namelist())
    models = set()
    for path in (resource / "entity").glob("*.pak"):
        with zipfile.ZipFile(path) as pak:
            models.update(Path(n).stem for n in pak.namelist() if n.endswith(".def"))

    def elevation(x, y):
        col, row = round((x + ox) / 5), round((y + oy) / 5)
        return struct.unpack_from("<h", grid, offset + 4 * (row * width + col))[0]

    for kind, model, eid, *fields in objects:
        valid = (
            "set/breed/" + model + ".set" in logic
            if kind == "Human"
            else model in models
        )
        if not valid or eid in map_ids:
            raise ValueError(f"unknown model or map ID collision: {model} {eid}")
        pos = next(f for f in fields if f[0] == "Position")
        x, y = map(float, pos[1:3])
        heights = [
            elevation(x + dx, y + dy) for dx in (-10, 0, 10) for dy in (-10, 0, 10)
        ]
        if max(heights) - min(heights) > 10:
            raise ValueError(f"steep spawn footprint at {x}, {y} for {model}")
    stuff = {Path(n).name for n in logic if n.startswith("set/stuff/")}
    for n in nodes(parse(text)):
        if n[0] == "item":
            words = []
            for v in n[1:]:
                if not isinstance(v, str) or v in ("filled", "filling") or v.isdigit():
                    break
                words.append(v)
            if ".".join(words) not in stuff:
                raise ValueError("unknown inventory item " + " ".join(words))
