"""GEM mission for the build-26734796 vehicle-control map. Coordinates are world units."""

import math
import re
import struct
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

SHA = "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a"
MEMBER = "single/00-bootcamp/02_vehicle-control/0.mi"
E2E_MAP_SRC = "multi/1v1_oryol"
E2E_MAP_FILES = (".materials", "map", "map.net", "map.ply", "map.grs", "minimap.dds")
E2E_CAMERA_REG = Path("resource/set/registry/camera.reg")
E2E_DIFFICULTY = Path("resource/set/difficulty/heroic.dl")
E2E_LOD_FILES = (
    Path("resource/set/level_of_detail.set"),
    Path("resource/set/level_of_detail_scenario.set"),
    Path("resource/set/level_of_detail.inc"),
)
E2E_ENV = "goh/multi/1v1_kalinin/var1"
E2E_FLAG = (-11, -7)
E2E_FLAG_RADIUS_M = 30.0
CQB_FLAG = (620, 360)
CQB_FLAG_RADIUS_M = 22.0
# GEM entity IDs are 16-bit; multi/1v1_oryol/map leaves 0x7877..0xFFEF free.
E2E_EID_BASE = 0x8000
MI = Path("resource/map") / MEMBER
SCALE = 20.0
INF = "infantry_combined_ops"
ARM = "armor_combined_ops"
CLOSE = "close_at_and_authority"
BENCHMARK_SUITES = ("e2e", "cqb")
SUITES = ("full", "infantry", "armor", "close_at", "e2e", "cqb")
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
    "t35": "driver gunner charger commander gunner3 charger3 gunner4 charger4 gunner5 gunner6",
    "sdkfz222a": "driver gunner commander",
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
class Tactical:
    """Battle manifest for the staged 1v1 Oryol center-flag battle."""
    duration_s: int = 420
    infantry_checkpoint_s: int = 180
    flag_xy: tuple[float, float] = E2E_FLAG
    flag_radius_m: float = E2E_FLAG_RADIUS_M
    friendly_player: int = 2
    enemy_player: int = 3
    friendly_infantry: list[str] = field(default_factory=list)
    friendly_vehicles: list[str] = field(default_factory=list)
    friendly_support: list[str] = field(default_factory=list)
    enemy_infantry: list[str] = field(default_factory=list)
    enemy_vehicles: list[str] = field(default_factory=list)
    enemy_support: list[str] = field(default_factory=list)
    points: dict[str, int] = field(default_factory=dict)
    waves: dict[str, int] = field(default_factory=dict)

    def deploy(self, player, role, group, points, wave):
        side = "friendly" if player == self.friendly_player else "enemy"
        getattr(self, f"{side}_{group}").append(role)
        self.points[role] = points
        self.waves[role] = WAVE_START[wave]


@dataclass
class Phase:
    name: str
    index: int
    objects: list = field(default_factory=list)
    relations: list = field(default_factory=list)
    actions: list = field(default_factory=list)
    waypoints: list = field(default_factory=list)
    zones: list = field(default_factory=list)
    positions: list = field(default_factory=list)
    mids: dict = field(default_factory=dict)
    tactical: Tactical | None = None
    mid_base: int | None = None
    eid_base: int = 0x8000
    capacity: int = 99

    def overview(self):
        xs, ys = zip(*self.positions)
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        span = math.hypot(max(xs) - min(xs), max(ys) - min(ys))
        scale = max(1, span / 1100)
        dy, dz = 1200 * scale, 950 * scale
        if self.tactical:
            # High bird's-eye angle matching the unlocked E2E camera_clip and scaled LODs:
            # frames every SW/NE spawn inside the 50° FOV cone while keeping eye inside map bounds.
            dy = min(dy, 2400)
            dz = max(dz, math.ceil(span * 1.15))
        view = (
            f"{{position {cx:g} {cy - dy:g} {10 + dz:g}}}"
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
        if role in self.mids or len(self.mids) >= self.capacity:
            raise ValueError(f"duplicate role or phase capacity exceeded: {role}")
        base = self.index * 100 if self.mid_base is None else self.mid_base
        mid = base + len(self.mids) + 1
        self.mids[role] = mid
        self.positions.append((x, y))
        eid = hex(self.eid_base + mid)
        human = model.startswith("mp/")
        kind = "Human" if human else "Entity"
        owner = "" if player is None else f"{{Player {player}}}"
        mission_id = f"{{MID {mid}}}" if player is not None else ""
        self.objects.append(
            f'{{{kind} "{model}" {eid} {{Position {x:g} {y:g}}}{{xform zl {yaw:g}}}{body}{owner}{mission_id}}}'
        )
        if not self.tactical and player == 2 and (human or model in SEATS):
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
        nation = GER if self.tactical or player == 0 else RUS
        return self.add(role, nation + breed, x, y, player, **kw)

    def vehicle(self, role, model, x, y, player=0, yaw=0, riders=0, rider_groups=(), **kw):
        eid = self.add(role, model, x, y, player, yaw, **kw)
        seats = SEATS[model].split() + [f"seat{i + 1}" for i in range(riders)]
        for seat in seats:
            passenger = seat.startswith("seat")
            crew = self.human(role + "_" + seat, RIFLE if passenger else "tankman", x, y, player,
                              yaw=yaw if self.tactical else 0, linked=True, groups=rider_groups if passenger else ())
            self.relations.append(f'{{Link {crew} {{{eid} "{seat}"}}}}')
        return eid

    def kit(self, role, *items):
        eid = hex(self.eid_base + self.mids[role])
        box = "".join("{item " + item + "}" for item in items)
        self.relations.append(f"{{Inventory {eid} {{box {{clear}}{box}}}}}")

    def inventory(self, role, item, amount):
        self.act(
            "inventory", role, f"{{action add}}{{item {item}}}{{amount {amount:d}}}"
        )

    def squad(self, role="squad"):
        self.act("squad_create", role, f"{{name {self.tag(role)}}}")

    def point(self, x, y, z):
        wid = str(self.index * (100 if self.tactical else 10) + len(self.waypoints))
        self.waypoints.append(f'{{"{wid}" {{position {x:g} {y:g} {z:g}}}}}')
        return f'{{waypoint "{wid}"}}'

    def advance(self, role, x, y, z):
        self.act("actor_to_waypoint", role, "{drop orders}" + self.point(x, y, z))

    def reveal(self, wave):
        self.act("entity_state", wave, "{tag_remove hidden}{inactive off}")

    def objective(self, name, x, y, player=2, radius=120):
        # Use the stock battle_zones unlinked ammo place for a contested objective.
        placer = '{Placer {State "ammo" {Unlinked}}}' if player is None else ""
        self.add(
            "flag", "flag_point_1", x, y, player,
            body=placer + f'{{Extender "map_point" {{name {name}}}{{zone {{radius 0}}{{zoneId "{name}"}}}}'
                "{phase 0}{order 1}{show All}{visor MapPoint}{allow {enableCapture}{enableRelease}}}",
        )
        self.zones.append(f'{{zone "circle" {{position {x:g} {y:g} 0}}{{Name "{name}"}}{{Radius {radius:g}}}}}')

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
    p.objective("f1", 30, 520)
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


WAVE_START = {"entry": 0, "armor": 180}
# Canonical Gates of Hell multiplayer (mid-war) unit point costs from resource/gamelogic.pak
# (set/multiplayer/units/mid/{inf_ger,inf_rus,units_ger,units_rus}.set).
# Vehicle costs apply stock vehicle/vehicle2 {squad_cost_factor 1}{round_multiple 5.0}:
# chassis cost + crew cost, rounded to the nearest 5 MP.
MP_UNIT_COSTS = {
    "rifleman_1": 8,
    "smg_1": 10,
    "sl": 10,
    "antitank_1": 30,
    "mgun_1": 40,
    "medic": 40,
    "tankman": 5,
    "tankman_scout": 5,
    "tank_commander": 5,
    "tank_commander_scout": 5,
    "sdkfz222a": 240,  # 225 chassis + 3*5 crew
    "panzer4d": 395,   # 370 chassis + 5*5 crew
    "t35": 390,        # 340 chassis + 10*5 crew
}
FRAG = '"m24" "grenade"'


def load_mp_unit_costs(game_dir, period="mid"):
    """Parse canonical GoH multiplayer unit MP costs from resource/gamelogic.pak."""
    pak = Path(game_dir) / "resource" / "gamelogic.pak"
    costs = {}
    with zipfile.ZipFile(pak) as z:
        inf_costs = {}
        for side in ("ger", "rus"):
            member = f"set/multiplayer/units/{period}/inf_{side}.set"
            if member not in z.namelist():
                continue
            text = z.read(member).decode("utf-8", "replace")
            for m in re.finditer(
                r'\{"mp/([^/\s"]+)/[^/\s"]+/([^"\s]+)"[^\n]*?\bcost\(\s*([0-9.]+)\s*\)',
                text,
            ):
                s_name, breed, val = m.group(1), m.group(2), float(m.group(3))
                cost_int = int(round(val))
                inf_costs[(s_name, breed)] = cost_int
                costs.setdefault(breed, cost_int)
        for side in ("ger", "rus"):
            member = f"set/multiplayer/units/{period}/units_{side}.set"
            if member not in z.namelist():
                continue
            text = z.read(member).decode("utf-8", "replace")
            for m in re.finditer(r'\{"([^"\s]+)"([\s\S]*?)\n\}', text):
                unit, body = m.group(1), m.group(2)
                cost_m = re.search(r'\bcost\(\s*([0-9.]+)\s*\)|\{cost\s+([0-9.]+)\}', body)
                if not cost_m:
                    continue
                chassis = float(cost_m.group(1) or cost_m.group(2))
                unit_side_m = re.search(r'\bside\(\s*([a-z]+)\s*\)', body)
                unit_side = unit_side_m.group(1) if unit_side_m else side
                crew_cost = 0.0
                for cm in re.finditer(r'\bcrew\d*\(\s*([^)]+)\)', body):
                    for part in cm.group(1).split():
                        if ":" in part:
                            cbreed, count = part.split(":", 1)
                            n = int(count)
                        else:
                            cbreed, n = part, 1
                        crew_cost += n * inf_costs.get((unit_side, cbreed), costs.get(cbreed, 0))
                total = chassis + crew_cost
                # Stock vehicle / vehicle2 templates round total cost to a multiple of 5 MP.
                if re.search(r'"vehicle2?"', body):
                    total = round(total / 5.0) * 5
                costs[unit] = int(round(total))
    return costs


@dataclass
class BattleSide:
    """Spawn an identical staged force on one approach to the multi/1v1_oryol center flag."""
    phase: Phase
    label: str
    player: int
    origin: tuple[float, float]
    yaw_deg: float
    medic_wp: tuple[float, float]
    car_spawn: tuple[float, float, float] = (0.0, 0.0, 0.0)
    car_rush_wp: tuple[float, float] = (0.0, 0.0)
    pz4_spawn: tuple[float, float, float] = (0.0, 0.0, 0.0)
    pz4_wp: tuple[float, float] = (0.0, 0.0)
    t35_spawn: tuple[float, float, float] = (0.0, 0.0, 0.0)
    t35_wp: tuple[float, float] = (0.0, 0.0)
    wave: str = "entry"

    def role(self, name):
        return f"{self.label}_{name}"

    def roles(self, names):
        return tuple(self.role(name) for name in names)

    def local_xy(self, fwd, lat):
        ang = math.radians(self.yaw_deg)
        ca, sa = math.cos(ang), math.sin(ang)
        x = round(self.origin[0] + fwd * ca - lat * sa, 1)
        y = round(self.origin[1] + fwd * sa + lat * ca, 1)
        return x, y

    def human(self, name, breed, fwd, lat, groups=(), **kw):
        role = self.role(name)
        x, y = self.local_xy(fwd, lat)
        self.phase.human(role, breed, x, y, self.player, yaw=self.yaw_deg,
                         groups=self.roles(groups), wave=self.wave, **kw)
        group = "support" if breed == "medic" else "infantry"
        self.phase.tactical.deploy(self.player, role, group, MP_UNIT_COSTS[breed], self.wave)
        return role

    def vehicle(self, name, model, x, y, yaw, riders=0, **kw):
        role = self.role(name)
        self.phase.vehicle(role, model, x, y, self.player, yaw=yaw,
                           wave=self.wave, riders=riders, **kw)
        self.phase.tactical.deploy(self.player, role, "vehicles", MP_UNIT_COSTS[model], self.wave)
        return role

    def advance(self, name, x, y):
        self.phase.advance(self.role(name), x, y, 0)


def assault_kit(p, role):
    p.kit(role, '"k98" filled {cell 0 0}', '"panzerfaust_60" "weapon" filled {cell 0 2}{user "hand_right"}',
          '"rifle_ger" "clip" "ammo" 30 {cell 0 3}')
    p.inventory(role, AT, 2)
    p.inventory(role, SMOKE, 2)


def infantry_stage(s):
    """Stage 1 (0-180s): 10-man German infantry squad + 1 rear medic contesting the center flag."""
    p = s.phase
    sq = ("squad",)
    front = (
        ("sq_smg1", "smg_1", -72),
        ("sq_rifle1", RIFLE, -36),
        ("sq_leader", "sl", 0),
        ("sq_rifle2", RIFLE, 36),
        ("sq_smg2", "smg_1", 72),
    )
    for name, breed, lat in front:
        role = s.human(name, breed, 25, lat, groups=sq)
        p.inventory(role, SMOKE, 2)
        p.inventory(role, FRAG, 2)
    # Spare MG belt on rifle1 so squad_logistics can feed the low-ammo MG gunner.
    p.inventory(s.role("sq_rifle1"), BELT, 100)

    assault_kit(p, s.human("sq_at1", "antitank_1", -25, -72, groups=sq))
    s.human("sq_mg1", "mgun_1", -25, -36, groups=sq)
    rifle3 = s.human("sq_rifle3", RIFLE, -25, 0, groups=sq)
    p.inventory(rifle3, SMOKE, 2)
    p.inventory(rifle3, FRAG, 2)
    mg2 = s.human("sq_mg2", "mgun_1", -25, 36, groups=sq)
    p.kit(mg2, '"mg34_belt" {cell 0 0}{user "hand_right"}', f"{BELT} 3 {{cell 0 3}}")
    assault_kit(p, s.human("sq_at2", "antitank_1", -25, 72, groups=sq))
    p.squad(s.role("squad"))

    s.human("medic", "medic", -90, 0)


def armor_stage(s):
    """Stage 2 (180-420s): fast Sd.Kfz.222 rushing through center + Panzer IV + 5-turret T-35."""
    s.vehicle("car_222", "sdkfz222a", *s.car_spawn)
    pz4 = s.vehicle("tank_pz4", "panzer4d", *s.pz4_spawn)
    s.phase.inventory(pz4, '"repair_kit"', 4)
    t35 = s.vehicle("tank_t35", "t35", *s.t35_spawn, riders=4)
    s.phase.inventory(t35, '"repair_kit"', 4)


def e2e():
    p = Phase(
        "e2e_flag_battle",
        4,
        tactical=Tactical(),
        mid_base=4000,
        eid_base=E2E_EID_BASE,
        capacity=500,
    )
    sides = (
        BattleSide(
            p, "a", 2,
            origin=(-520, -1040), yaw_deg=64,
            medic_wp=(-320, -560),
            car_spawn=(-1250, -925, 38), car_rush_wp=(180, 420),
            pz4_spawn=(-1835, -920, 0), pz4_wp=(-360, -260),
            t35_spawn=(-2185, -1320, 70), t35_wp=(-440, -100),
        ),
        BattleSide(
            p, "b", 3,
            origin=(660, 940), yaw_deg=-125,
            medic_wp=(360, 500),
            car_spawn=(1200, 970, -142), car_rush_wp=(-180, -420),
            pz4_spawn=(1220, 1460, -90), pz4_wp=(340, 250),
            t35_spawn=(1930, 1270, -138), t35_wp=(420, 80),
        ),
    )
    p.objective("center", *E2E_FLAG, player=None, radius=int(E2E_FLAG_RADIUS_M * SCALE))
    for s in sides:
        infantry_stage(s)
        s.advance("squad", *E2E_FLAG)
        s.advance("medic", *s.medic_wp)

    p.actions.append(delay(60))  # t=60, re-push infantry onto the contested flag
    for s in sides:
        s.advance("squad", *E2E_FLAG)

    p.actions.append(delay(60))  # t=120, third infantry push before armor arrives
    for s in sides:
        s.advance("squad", *E2E_FLAG)

    p.actions.append(delay(60))  # t=180, Stage 1 checkpoint -> car zooms through center + tanks reinforce
    p.reveal("armor")
    for s in sides:
        s.wave = "armor"
        armor_stage(s)
        s.advance("car_222", *s.car_rush_wp)
    p.actions.append(delay(2))
    for s in sides:
        s.advance("tank_pz4", *s.pz4_wp)
    p.actions.append(delay(2))
    for s in sides:
        s.advance("tank_t35", *s.t35_wp)

    p.actions.append(delay(86))  # t=270, mid-Stage-2 combined push onto center flag
    for s in sides:
        s.advance("squad", *E2E_FLAG)
        s.advance("car_222", *E2E_FLAG)
        s.advance("tank_pz4", *E2E_FLAG)
        s.advance("tank_t35", *E2E_FLAG)

    p.actions.append(delay(75))  # t=345, final combined push onto center flag
    for s in sides:
        s.advance("squad", *E2E_FLAG)
        s.advance("car_222", *E2E_FLAG)
        s.advance("tank_pz4", *E2E_FLAG)
        s.advance("tank_t35", *E2E_FLAG)
    p.actions.append(delay(75))  # t=420, end of battle
    return p


def cqb():
    """Close-quarters (26-32 m) flat mirror-symmetric infantry skirmish with sandbag trenches and flanking lanes."""
    cx, cy = CQB_FLAG
    p = Phase(
        "cqb_infantry_skirmish",
        5,
        tactical=Tactical(
            duration_s=150,
            infantry_checkpoint_s=75,
            flag_xy=CQB_FLAG,
            flag_radius_m=CQB_FLAG_RADIUS_M,
        ),
        mid_base=4000,
        eid_base=0x8000,
        capacity=500,
    )
    # 100% mirror-symmetric sandbag lines (~26 m apart; yaw=90/270 faces N-S) on the completely flat,
    # prop-free east field of 02_vehicle-control, plus angled West/East flanking cover and midfield cover.
    cover_layout = (
        ("sb_s1", cx - 135, cy - 260, 90), ("sb_s2", cx - 45, cy - 260, 90),
        ("sb_s3", cx + 45, cy - 260, 90), ("sb_s4", cx + 135, cy - 260, 90),
        ("sb_n1", cx - 135, cy + 260, 270), ("sb_n2", cx - 45, cy + 260, 270),
        ("sb_n3", cx + 45, cy + 260, 270), ("sb_n4", cx + 135, cy + 260, 270),
        ("sb_w1", cx - 250, cy - 170, 65), ("sb_w2", cx - 330, cy - 60, 45),
        ("sb_w3", cx - 330, cy + 60, 315), ("sb_w4", cx - 250, cy + 170, 295),
        ("sb_e1", cx + 250, cy - 170, 115), ("sb_e2", cx + 330, cy - 60, 135),
        ("sb_e3", cx + 330, cy + 60, 225), ("sb_e4", cx + 250, cy + 170, 245),
        ("sb_ms1", cx - 110, cy - 90, 90), ("sb_ms2", cx + 110, cy - 90, 90),
        ("sb_mn1", cx - 110, cy + 90, 270), ("sb_mn2", cx + 110, cy + 90, 270),
    )
    for role, x, y, yaw in cover_layout:
        p.add(role, "sandbag_structured_straight", x, y, yaw=yaw)

    sides = (
        BattleSide(p, "a", 2, origin=(cx, cy - 320), yaw_deg=90, medic_wp=(cx, cy - 380)),
        BattleSide(p, "b", 3, origin=(cx, cy + 320), yaw_deg=-90, medic_wp=(cx, cy + 380)),
    )
    p.objective("center", *CQB_FLAG, player=None, radius=int(CQB_FLAG_RADIUS_M * SCALE))
    for s in sides:
        infantry_stage(s)
        # Step onto each side's own front sandbag trench so the initial waypoint finishes immediately
        # and tactical flanking/grenade AI has full autonomy across the 26-32 m clearing.
        s.advance("squad", *s.local_xy(35, 0))
        s.advance("medic", *s.medic_wp)

    p.actions.append(delay(70))  # t=70, mid-fight push onto center flag
    for s in sides:
        s.advance("squad", *CQB_FLAG)
    p.actions.append(delay(45))  # t=115, late push onto center flag
    for s in sides:
        s.advance("squad", *CQB_FLAG)
    p.actions.append(delay(35))  # t=150, end of battle
    return p


def is_benchmark_text(text):
    return any(f"GOH_L1_DONE_{s}" in text for s in BENCHMARK_SUITES)


def phases_for(suite):
    if suite == "e2e":
        return [e2e()]
    if suite == "cqb":
        return [cqb()]
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
    if not all(re.fullmatch(r"0x[0-9a-f]+", eid) and int(eid, 16) <= 0xFFFF for eid in ids):
        raise ValueError("invalid or >16-bit hex entity ID")
    if len(ids) != len(set(ids)) or len(mids) != len(set(mids)):
        raise ValueError("missing or duplicate entity ID/MID")
    for e in objects:
        fields = [n[0] for n in e[3:]]
        if fields.count("MID") != int("Player" in fields):
            raise ValueError(f"expected one MID per actor and none on scenery: {e[1]}")
        if "Player" in fields and fields[-2:] != ["Player", "MID"]:
            raise ValueError(f"expected player and MID after extenders: {e[1]}")
    labels = {s for r in root[1:] if r[0] == "Tags" for s in r[1:-1]}
    waypoints = [wp[0] for n in nodes(root) if n[0] == "waypoints" for wp in n[1:]]
    wps = set(waypoints)
    if len(wps) != len(waypoints):
        raise ValueError("duplicate waypoint ID")
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
            in_bounds = (
                (-4475 < x < 4498 and -4481 < y < 4490)
                if "GOH_L1_DONE_e2e" in text
                else (-1394 < x < 1400 and -5000 < y < 5000)
            )
            if not in_bounds:
                raise ValueError(f"outside map pather bounds: {x}, {y}")
    return objects


def generate(suite="full", speed="normal"):
    if speed not in SPEEDS or suite not in SUITES:
        raise ValueError("unknown suite or speed")
    phases = phases_for(suite)
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
    is_e2e = suite in BENCHMARK_SUITES
    is_oryol = suite == "e2e"
    witness = Phase(
        "complete",
        90 + SUITES.index(suite),
        eid_base=E2E_EID_BASE if is_oryol else 0x8000,
    )
    wx, wy = E2E_FLAG if is_oryol else (CQB_FLAG if suite == "cqb" else (580, 300))
    witness.human("done", RIFLE, wx, wy, mode="hold", fire="hold")
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
    aliases = '{Players {user 0}{alias 0 "player"}{alias 1 "ai"}'
    if is_e2e:
        aliases += '{alias 2 "west_ai"}{alias 3 "east_ai"}'
    else:
        aliases += '{alias 2 "enemy"}'
    env = E2E_ENV if is_e2e else ENVIRONMENT
    if is_e2e:
        # Spectator Player 0 has zero active units during combat (done witness is hidden until END).
        # Allying P0 with SW (P2) and keeping P0 hostile to NE (P3) enables native GoH allied-blue vs
        # hostile-red coloring while P2/P3 stay mutually hostile (1); E2E overlays symmetric 1.0/1.0
        # heroic.dl HP/accuracy (matching MP) so allied P2 and hostile P3 have identical stats.
        diplomacy = [
            "1 2 2 1 1 1 1 1",
            "2 1 2 1 1 1 1 1",
            "2 2 1 1 1 1 1 1",
            *(["1 1 1 1 1 1 1 1"] * 5),
        ]
    else:
        diplomacy = [
            "1 2 1 1 1 1 1 1",
            "2 1 1 1 1 1 1 1",
            *(["1 1 1 1 1 1 1 1"] * 6),
        ]
    rows = [
        "{mission",
        *(o for p in phases for o in p.objects),
        *(r for p in phases for r in p.relations),
        f'{{Environment "{env}"}}',
        "{Helpers",
        "{cameras", *cameras, "}",
        *(zone for p in phases for zone in p.zones),
        "{waypoints",
        *(w for p in phases for w in p.waypoints),
        "}",
        '{triggers {"harness/run" {condition} {actions',
        *actions,
        "}}}}",
        # Bootcamp uses 1 for hostile and 2 for allied. Only 0 and 1 are
        # allied here. 2/3, 3/2, and their entries against 0 are all hostile.
        aliases + "{diplomacy",
        *diplomacy,
        "}}",
        "{Fog 0}",
        "}",
    ]
    text = "\n".join(rows) + "\n"
    validate(text)
    return text


def e2e_map_files(game):
    """Read the stock multi/1v1_oryol map files from multi.pak (read-only) for the E2E overlay."""
    with zipfile.ZipFile(game / "resource/map/multi.pak") as pak:
        return {name: pak.read(f"{E2E_MAP_SRC}/{name}") for name in E2E_MAP_FILES}


def patch_camera_reg(stock_reg):
    """Unlock bird's-eye zoom and enable high-contrast team-colored unit outlines in the E2E overlay."""
    text = stock_reg.decode("latin1")
    text = re.sub(r"(\{zfar\s+)\d+(\})", r"\g<1>25000\2", text)
    text = re.sub(
        r"(\{max_distance\s*\{distance\s+)\d+(\}\s*\{viewMin\s+\d+\}\s*\{viewMax\s+)\d+(\})",
        r"\g<1>8000\g<2>89\3",
        text,
    )
    text = re.sub(
        r"(\{default\s*\{distance\s+)\d+(\}\s*\{viewAngle\s+)[\d.]+(\})",
        r"\g<1>4200\g<2>65\3",
        text,
    )
    text = re.sub(r"(\{intensity_regular\s+)[\d.]+(\s*\})", r"\g<1>5.0\2", text)
    text = re.sub(r"(\{intensity_emphasized\s+)[\d.]+(\s*\})", r"\g<1>6.0\2", text)
    text = re.sub(r"(\{outline_width\s+)[\d.]+(\s*\})", r"\g<1>3.0\2", text)
    # Enable both neutral and hostile outlines even on heroic/hardcore (_hc).
    text = re.sub(
        r"(\{active_outlines(?:_hc)?\s*\{friendly\s+)\d+(\}\s*\{hostile\s+)\d+(\}\s*\{allied\s+)\d+(\}\s*\{neutral\s+)\d+(\})",
        r"\g<1>1\g<2>1\g<3>1\g<4>1\5",
        text,
    )
    # Vivid blue for SW (neutral to spectator P0) and vivid red for NE (hostile to P0).
    text = re.sub(r"(\{allied_color\s+)0x[0-9a-fA-F]+(\})", r"\g<1>0xFF2898FF\2", text)
    text = re.sub(r"(\{neutral_color\s+)0x[0-9a-fA-F]+(\})", r"\g<1>0xFF2898FF\2", text)
    text = re.sub(r"(\{hostile_color\s+)0x[0-9a-fA-F]+(\})", r"\g<1>0xFFFF2424\2", text)
    text = re.sub(r"(\{distance_fade_start\s+)\d+(\})", r"\g<1>8000\2", text)
    text = re.sub(r"(\{distance_fade_end\s+)\d+(\})", r"\g<1>10000\2", text)
    text = re.sub(r"(\{time_active(?:_HC)?\s+)[\d.]+(\})", r"\g<1>999.0\2", text)
    return text.encode("latin1")


def birdseye_camera_reg(game):
    """Build the unlocked E2E camera.reg from stock gamelogic.pak (read-only)."""
    with zipfile.ZipFile(game / "resource/gamelogic.pak") as pak:
        return patch_camera_reg(pak.read("set/registry/camera.reg"))


def patch_lod_set(stock_set):
    """Scale RTS/scenario LOD distances 5x so infantry/weapons/tracers stay rendered at bird's-eye height."""
    text = stock_set.decode("latin1")
    text = re.sub(r"(\(\*\s+)[\d.]+(\s+%d\))", r"\g<1>5.00\2", text)
    text = re.sub(r"(\(\*\s+)[\d.]+(\s+%d2\))", r"\g<1>5.00\2", text)
    return text.encode("latin1")


def patch_lod_inc(stock_inc):
    """Push combat-entity LOD off-cutoffs out past bird's-eye distance even under low detail presets."""
    text = stock_inc.decode("latin1")
    for cls in ("human", "stuff_weapon", "stuff_grenade", "tracer", "cannon", "vehicle"):
        text = re.sub(
            rf'(\{{"{cls}".*?\{{distance\s+\("scale"\s+d\()\d+(\)\)\s*\("off(?:_small)?"\))',
            r"\g<1>25000\2",
            text,
            count=1,
            flags=re.S,
        )
    return text.encode("latin1")


def e2e_symmetric_difficulty():
    """Return a symmetric MP-parity heroic.dl overlay (1.0 HP / 1.0 accuracy for all sides)."""
    return (
        '{difficulty\r\n'
        '\t(include "difficulty_generic.inc")\r\n'
        '\t{any\r\n\t\t{health 1.0}\r\n\t\t{accuracy 1.0}\r\n\t}\r\n'
        '\t{enemy\r\n\t\t{health 1.0}\r\n\t\t{accuracy 1.0}\r\n\t}\r\n'
        '\t{ally\r\n\t\t{health 1.0}\r\n\t\t{accuracy 1.0}\r\n\t}\r\n'
        '\t{player\r\n\t\t{health 1.0}\r\n\t\t{accuracy 1.0}\r\n\t}\r\n'
        '}\r\n'
    ).encode("latin1")


def birdseye_lod_files(game):
    """Build scaled E2E LOD overlays from stock gamelogic.pak (read-only)."""
    with zipfile.ZipFile(game / "resource/gamelogic.pak") as pak:
        return {
            Path("resource/set/level_of_detail.set"): patch_lod_set(pak.read("set/level_of_detail.set")),
            Path("resource/set/level_of_detail_scenario.set"): patch_lod_set(
                pak.read("set/level_of_detail_scenario.set")
            ),
            Path("resource/set/level_of_detail.inc"): patch_lod_inc(pak.read("set/level_of_detail.inc")),
        }


def check_stock(game, text):
    """Validate model/item catalogue, baked terrain and scene-ID namespace from the installed archives."""
    objects = validate(text)
    is_e2e = "GOH_L1_DONE_e2e" in text
    resource = game / "resource"
    if is_e2e:
        map_data = e2e_map_files(game)
        stock = map_data["map"].decode("latin1")
        grid = map_data["map.net"]
    else:
        with zipfile.ZipFile(resource / "map/single.pak") as pak:
            pak.getinfo(MEMBER)
            stock = pak.read(MEMBER.replace("0.mi", "map")).decode("latin1")
            grid = pak.read(MEMBER.replace("0.mi", "map.net"))
    width, height, ox, oy = struct.unpack_from("<HHff", grid, 2)
    offset = grid.index(b"GRID") + 4
    if len(grid) - offset != width * height * 4 or grid[:2] != b"\x05\x02":
        raise ValueError("unexpected map.net grid")
    map_models = {int(eid, 16) & 0xFFFF: model for model, eid in re.findall(r'\{(?:Entity|Human) "([^"]+)" (0x[0-9a-f]+)', stock)}
    stock_mids = {int(mid) for mid in re.findall(r'\{MID\s+(\d+)\}', stock)}
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
        if not valid or (int(eid, 16) & 0xFFFF) in map_models:
            raise ValueError(f"unknown model or map ID collision: {model} {eid}")
        for f in fields:
            if f[0] == "MID" and int(f[1]) in stock_mids:
                raise ValueError(f"stock map MID collision: {model} {f[1]}")
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
