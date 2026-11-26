"""GEM mission for the build-26734796 vehicle-control map. Coordinates are world units."""

import math
import re
import struct
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

SHA = "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a"
MEMBER = "single/00-bootcamp/02_vehicle-control/0.mi"
MAP_DIR = "single/00-bootcamp/02_vehicle-control"
MAP_FILES = ("map", "map.net", "map.ply", "map.grs")
MI = Path("resource/map") / MEMBER
SCALE = 20.0
INF = "infantry_combined_ops"
ARM = "armor_combined_ops"
CLOSE = "close_at_and_authority"
SUITES = ("full", "infantry", "armor", "close_at", "e2e")
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
class Tactical:
    """Battle manifest. Named combatants carry their original value through all three waves."""
    duration_s: int = 300
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
    capacity: int = 99

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
        if role in self.mids or len(self.mids) >= self.capacity:
            raise ValueError(f"duplicate role or phase capacity exceeded: {role}")
        base = self.index * 100 if self.mid_base is None else self.mid_base
        mid = base + len(self.mids) + 1
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
        wid = str(self.index * (100 if self.tactical else 10) + len(self.waypoints))
        self.waypoints.append(f'{{"{wid}" {{position {x:g} {y:g} {z:g}}}}}')
        return f'{{waypoint "{wid}"}}'

    def advance(self, role, x, y, z):
        self.act("actor_to_waypoint", role, "{drop orders}" + self.point(x, y, z))

    def reveal(self, wave):
        self.act("entity_state", wave, "{tag_remove hidden}{inactive off}")

    def objective(self, name, x, y, player=2):
        # Use the stock battle_zones unlinked ammo place for a contested objective.
        placer = '{Placer {State "ammo" {Unlinked}}}' if player is None else ""
        self.add(
            "flag", "flag_point_1", x, y, player,
            body=placer + f'{{Extender "map_point" {{name {name}}}{{zone {{radius 0}}{{zoneId "{name}"}}}}'
                "{phase 0}{order 1}{show All}{visor MapPoint}{allow {enableCapture}{enableRelease}}}",
        )
        self.zones.append(f'{{zone "circle" {{position {x:g} {y:g} 0}}{{Name "{name}"}}{{Radius 120}}}}')

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


WAVE_START = {"entry": 0, "wave2": 90, "wave3": 180}
VEHICLE_POINTS = {
    "panzer4d": 10, "t3476_40": 10, "sdkfz251c": 5, "45mm_m37": 5,
    "ba64": 3, "blitz3_6_art_ammo": 3,
}
FRAG = '"m24" "grenade"'


@dataclass
class BattleSide:
    """Author positions and headings on the west side and reflect them across x=0."""
    phase: Phase
    label: str
    player: int
    direction: int
    wave: str = "entry"

    def role(self, name):
        return f"{self.label}_{name}"

    def roles(self, names):
        return tuple(self.role(name) for name in names)

    def yaw(self, degrees):
        return degrees if self.direction == 1 else 180 - degrees

    def human(self, name, breed, x, y, groups=(), **kw):
        role = self.role(name)
        self.phase.human(role, breed, self.direction * x, y, self.player, yaw=self.yaw(0),
                         groups=self.roles(groups), wave=self.wave, **kw)
        if breed == "medic":
            self.phase.tactical.deploy(self.player, role, "support", 2, self.wave)
        else:
            self.phase.tactical.deploy(self.player, role, "infantry", 1, self.wave)
        return role

    def vehicle(self, name, model, x, y, yaw=0, riders=0, **kw):
        role = self.role(name)
        self.phase.vehicle(role, model, self.direction * x, y, self.player, yaw=self.yaw(yaw),
                           wave=self.wave, riders=riders, **kw)
        group = "support" if model == "blitz3_6_art_ammo" else "vehicles"
        self.phase.tactical.deploy(self.player, role, group, VEHICLE_POINTS[model], self.wave)
        # Halftrack assault seats are visible to the observer. Tank hull riders
        # can be promoted into invisible crew stations, so they stay crew reserves.
        if model == "sdkfz251c":
            for i in range(1, riders + 1):
                self.phase.tactical.deploy(self.player, f"{role}_seat{i}", "infantry", 1, self.wave)
        return role

    def cover(self, name, model, x, y, yaw=0):
        self.phase.add(self.role(name), model, self.direction * x, y, yaw=self.yaw(yaw))

    def advance(self, name, x, y):
        self.phase.advance(self.role(name), self.direction * x, y, 0)


def assault_kit(p, role):
    p.kit(role, '"k98" filled {cell 0 0}', '"panzerfaust_60" "weapon" filled {cell 0 2}{user "hand_right"}',
          '"rifle_ger" "clip" "ammo" 30 {cell 0 3}')
    p.inventory(role, AT, 2)
    p.inventory(role, SMOKE, 2)


def assault_halftrack(s, name, x, y):
    role = s.vehicle(name, "sdkfz251c", x, y, riders=4)
    assault_kit(s.phase, role + "_seat1")
    for seat in ("seat2", "seat3", "seat4"):
        passenger = role + "_" + seat
        s.phase.inventory(passenger, AT, 2)
        s.phase.inventory(passenger, SMOKE, 2)


def battle_terrain(s):
    # Symmetric 3-lane obstacle layout on the flat z=0 arena.
    # South lane: urban CQB walls, sandbags, soft wooden fence, and rear aid cover.
    s.cover("south_wall", "dutch_brick_wall01_straight_short", -285, 34, yaw=90)
    s.cover("south_fence", "fence-wood_3", -205, 115)
    s.cover("south_bags", "sandbag_structured_straight", -110, 30)
    s.cover("south_mid_wall", "dutch_brick_wall01_straight_short", -110, 100, yaw=90)
    s.cover("south_inner_wall", "dutch_brick_wall01_straight_short", -68, 155, yaw=90)
    s.cover("south_aid_wall", "dutch_brick_wall01_straight_short", -395, 75, yaw=90)
    # East-west sightline dividers keeping center vehicles from immediately sniping south CQB.
    s.cover("south_div_inner", "dutch_brick_wall01_straight_short", -120, 190)
    s.cover("south_div_outer", "dutch_brick_wall01_straight_short", -240, 190)
    # Center lane: flag-flank cover and rear aid-station wall.
    s.cover("center_bags", "sandbag_structured_straight", -150, 235)
    s.cover("center_wall", "dutch_brick_wall01_straight_short", -120, 390, yaw=90)
    s.cover("center_aid_wall", "dutch_brick_wall01_straight_short", -395, 252, yaw=90)
    # North lane: armor corridor with crushable wooden fences and AT-gun sandbags.
    s.cover("north_fence", "fence-wood_3", -325, 605)
    s.cover("north_bags", "sandbag_structured_straight", -240, 485)
    s.cover("north_flank_fence", "fence-wood_3", -200, 675)
    s.cover("north_at_bags", "sandbag_structured_straight", -390, 688)


def first_wave(s):
    p = s.phase
    south = ("south_squad",)
    s.human("south_leader", "smg_1", -380, 75, groups=south)
    for name, y in (("south_rifle1", 15), ("south_rifle2", 142), ("south_donor", 107)):
        role = s.human(name, RIFLE, -385, y, groups=south)
        p.inventory(role, SMOKE, 2)
        p.inventory(role, FRAG, 2)
    p.inventory(s.role("south_donor"), BELT, 100)
    mg = s.human("south_mg", "mgun_1", -410, 89, groups=south)
    p.kit(mg, '"mg34_belt" {cell 0 0}{user "hand_right"}', f"{BELT} 3 {{cell 0 3}}")
    assault_kit(p, s.human("south_at", "antitank_1", -385, 171, groups=south))
    p.squad(s.role("south_squad"))
    # Protected aid station 19.5 m behind the South fighting line (inside 36 m medic radius).
    s.human("south_medic", "medic", -450, 75, mode="hold")

    for i, name in enumerate(("smg1", "rifle1", "smg2", "rifle2")):
        smg = i % 2 == 0
        role = s.human("center_" + name, "smg_1" if smg else RIFLE, -285, 210 + 28*i,
                       groups=("center_front",))
        # Replace the entire breed inventory. Only the loaded 32-round MP40
        # magazine or five K98 rounds are available until the unit resupplies.
        weapon = "mp40_unfolded" if smg else "k98"
        p.kit(role, f'"{weapon}" filled {{cell 0 0}}{{user "hand_right"}}',
              '"bandage_ger" 2 {cell 3 2}')
    p.squad(s.role("center_front"))
    s.human("center_medic", "medic", -450, 252, mode="hold")
    s.vehicle("center_truck", "blitz3_6_art_ammo", -680, 252, mode="hold",
              body='{Extender "supply_zone" {enabled}{current 0}}')
    assault_halftrack(s, "center_halftrack", -620, 340)
    s.vehicle("center_car", "ba64", -530, 416)

    tank = s.vehicle("north_tank", "panzer4d", -570, 537)
    p.inventory(tank, '"repair_kit"', 4)
    s.vehicle("north_desant", "t3476_40", -710, 642, riders=4)
    s.vehicle("north_gun", "45mm_m37", -430, 688, yaw=-37, mode="hold")
    for i in range(4):
        s.human(f"north_screen{i+1}", RIFLE if i % 2 == 0 else "smg_1",
                -407 - (i % 2)*38, 473 + 33*i, groups=("north_screen",))
    p.squad(s.role("north_screen"))


def second_wave(s):
    p = s.phase
    for i, (name, breed) in enumerate((("smg", "smg_1"), ("rifle", RIFLE),
                                       ("at", "antitank_1"), ("mg", "mgun_1"))):
        role = s.human("south2_" + name, breed, -1000, 22 + i*42, groups=("south2_squad",))
        p.inventory(role, SMOKE, 2)
        p.inventory(role, FRAG, 2)
        if name == "at":
            p.inventory(role, AT, 2)
    p.squad(s.role("south2_squad"))
    assault_halftrack(s, "center2_halftrack", -990, 344)
    s.vehicle("center2_car", "ba64", -1020, 428)
    tank = s.vehicle("north2_tank", "panzer4d", -1085, 569)
    p.inventory(tank, '"repair_kit"', 4)
    for i in range(4):
        s.human(f"north2_escort{i+1}", RIFLE if i % 2 == 0 else "smg_1",
                -973 - (i % 2)*38, 478 + i*29, groups=("north2_escort",))
    p.squad(s.role("north2_escort"))


def third_wave(s):
    s.vehicle("north3_desant", "t3476_40", -1115, 668, riders=4)
    for i in range(4):
        role = s.human(f"north3_assault{i+1}", "antitank_1" if i == 0 else "smg_1",
                       -985 - (i % 2)*34, 599 + i*31, groups=("north3_assault",))
        if i == 0:
            assault_kit(s.phase, role)
        else:
            s.phase.inventory(role, SMOKE, 2)
            s.phase.inventory(role, AT, 2)
    s.phase.squad(s.role("north3_assault"))


def e2e():
    p = Phase("e2e_combined_arms_battle", 4, tactical=Tactical(), mid_base=4000, capacity=500)
    sides = (BattleSide(p, "a", 2, 1), BattleSide(p, "b", 3, -1))
    p.objective("center", 0, 320, player=None)
    for s in sides:
        battle_terrain(s)
        first_wave(s)
        s.advance("south_squad", -60, 86)
        s.advance("center_front", -150, 245)
        s.advance("north_screen", -45, 490)
    # Armor follows the forward screen. Staggered waypoints keep the two
    # vehicles in each lane off one another's destinations.
    p.actions.append(delay(3))
    for s in sides:
        s.advance("center_car", -68, 414)
        s.advance("north_tank", -60, 545)
    p.actions.append(delay(4))
    for s in sides:
        s.advance("center_halftrack", -62, 340)
        s.advance("north_desant", -70, 634)

    p.actions.append(delay(83))  # t=90, first reinforcements
    p.reveal("wave2")
    for s in sides:
        s.wave = "wave2"
        second_wave(s)
        s.advance("south2_squad", -15, 122)
        s.advance("north2_escort", -75, 518)
    p.actions.append(delay(3))
    for s in sides:
        s.advance("center2_car", -165, 414)
        s.advance("north2_tank", -170, 569)
    p.actions.append(delay(4))
    for s in sides:
        s.advance("center2_halftrack", -150, 349)

    p.actions.append(delay(83))  # t=180, final push off the northern flank
    p.reveal("wave3")
    for s in sides:
        s.wave = "wave3"
        third_wave(s)
        s.advance("north3_assault", -20, 322)
    p.actions.append(delay(4))
    for s in sides:
        s.advance("north3_desant", -65, 295)
    p.actions.append(delay(116))
    return p


def phases_for(suite):
    if suite == "e2e":
        return [e2e()]
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
            if not (-1394 < x < 1400 and -5000 < y < 5000):
                raise ValueError(f"outside vehicle-control pather: {x}, {y}")
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
    aliases = '{Players {user 0}{alias 0 "player"}{alias 1 "ai"}'
    if suite == "e2e":
        aliases += '{alias 2 "west_ai"}{alias 3 "east_ai"}'
    else:
        aliases += '{alias 2 "enemy"}'
    rows = [
        "{mission",
        *(o for p in phases for o in p.objects),
        *(r for p in phases for r in p.relations),
        f'{{Environment "{ENVIRONMENT}"}}',
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


def flatten_map_text(stock_map):
    """Strip stock map entities/inventories, sink Ocean below z=0, and symmetrize X clip bounds."""
    text = stock_map.decode("latin1")
    newline = "\r\n" if "\r\n" in text else "\n"
    kept = []
    depth = 0
    skipping = False
    in_ocean = False
    in_clips = False
    for line in text.splitlines():
        s = line.strip()
        if depth == 1:
            if s.startswith(("{Entity", "{Inventory")):
                skipping = True
            elif s.startswith("{Ocean"):
                in_ocean = True
            elif s.startswith("{Clips"):
                in_clips = True
        if not skipping:
            if in_ocean and s.startswith("{zlevel "):
                line = re.sub(r"\{zlevel\s+[^}]+\}", "{zlevel -100}", line)
            elif in_clips and s.startswith(("{camera ", "{pather ")):
                line = re.sub(
                    r"(\{(?:camera|pather)\s+)[-0-9.]+\s+([-0-9.]+)\s+[-0-9.]+\s+([-0-9.]+)\}",
                    r"\1-1400 \2 1400 \3}",
                    line,
                )
            kept.append(line)
        depth += s.count("{") - s.count("}")
        if depth == 1:
            skipping = False
            in_ocean = False
            in_clips = False
    if depth != 0:
        raise ValueError("unbalanced stock map file")
    return (newline.join(kept) + newline).encode("latin1")


def flatten_map_net(stock_net):
    """Flatten every map.net cell to height=0 and uniform ground material (map0=1, map1=1)."""
    if stock_net[:2] != b"\x05\x02":
        raise ValueError("unexpected map.net version header")
    width, height = struct.unpack_from("<HH", stock_net, 2)
    grid_off = stock_net.index(b"GRID") + 4
    if len(stock_net) - grid_off != width * height * 4:
        raise ValueError("unexpected map.net grid length")
    return stock_net[:grid_off] + (b"\x00\x00\x01\x01" * (width * height))


def flatten_map_ply(stock_ply):
    """Flatten every BPLY terrain vertex to z=0, normal=(0,0,1), and neutral grey vertex color."""
    if stock_ply[:8] != b"BPLYBNDS":
        raise ValueError("unexpected map.ply header")
    out = bytearray(stock_ply)
    # BNDS = <6f: minX, minY, minZ, maxX, maxY, maxZ (offsets 8..32).
    struct.pack_into("<f", out, 16, -0.5)
    struct.pack_into("<f", out, 28, 0.5)
    vert_off = stock_ply.index(b"VERT")
    vcount, vstride = struct.unpack_from("<II", out, vert_off + 4)
    vstart = vert_off + 12
    vend = vstart + vcount * vstride
    if vstride != 44 or vend + 4 > len(out) or out[vend : vend + 4] not in (b"IND4", b"INDX"):
        raise ValueError("unexpected map.ply VERT layout")
    flat_tail = struct.pack("<4fI", 0.0, 0.0, 0.0, 1.0, 0xFF7F7F7F)
    for pos in range(vstart + 8, vend, 44):
        out[pos : pos + 20] = flat_tail
    return bytes(out)


def flatten_map_grs(stock_grs):
    """Clear all decorative grass cells from map.grs while keeping its grid bounds intact."""
    if stock_grs[:4] != b"GRAS":
        raise ValueError("unexpected map.grs header")
    pmap = stock_grs.index(b"PMAP") + 4
    count = struct.unpack_from("<I", stock_grs, pmap)[0]
    off = pmap + 4
    for _ in range(count):
        off += 1 + stock_grs[off]
    x0, y0, x1, y1, _step = struct.unpack_from("<4if", stock_grs, off)
    grid_off = off + 20
    expected = (x1 - x0) * (y1 - y0) * 5
    if x1 <= x0 or y1 <= y0 or len(stock_grs) - grid_off != expected:
        raise ValueError("unexpected map.grs grid layout")
    return stock_grs[:grid_off] + (b"\x00" * expected)


def flat_arena(game):
    """Build the 4 flat z=0 symmetric map overlay files from stock single.pak (read-only)."""
    pak_path = game / "resource/map/single.pak"
    with zipfile.ZipFile(pak_path) as pak:
        return {
            "map": flatten_map_text(pak.read(f"{MAP_DIR}/map")),
            "map.net": flatten_map_net(pak.read(f"{MAP_DIR}/map.net")),
            "map.ply": flatten_map_ply(pak.read(f"{MAP_DIR}/map.ply")),
            "map.grs": flatten_map_grs(pak.read(f"{MAP_DIR}/map.grs")),
        }


def check_stock(game, text):
    """Validate model/item catalogue, baked terrain and scene-ID namespace from the installed archives."""
    objects = validate(text)
    flat_e2e = "GOH_L1_DONE_e2e" in text
    resource = game / "resource"
    with zipfile.ZipFile(resource / "map/single.pak") as pak:
        pak.getinfo(MEMBER)
        stock = pak.read(MEMBER.replace("0.mi", "map")).decode("latin1")
        grid = pak.read(MEMBER.replace("0.mi", "map.net"))
    if flat_e2e:
        flat_arena(game)
    width, height, ox, oy = struct.unpack_from("<HHff", grid, 2)
    offset = grid.index(b"GRID") + 4
    if len(grid) - offset != width * height * 4 or grid[:2] != b"\x05\x02":
        raise ValueError("unexpected map.net grid")
    map_models = (
        {}
        if flat_e2e
        else {eid: model for model, eid in re.findall(r'\{(?:Entity|Human) "([^"]+)" (0x[0-9a-f]+)', stock)}
    )
    stock_mids = set() if flat_e2e else {int(mid) for mid in re.findall(r'\{MID\s+(\d+)\}', stock)}
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
        if not valid or eid in map_models:
            raise ValueError(f"unknown model or map ID collision: {model} {eid}")
        for f in fields:
            if f[0] == "MID" and int(f[1]) in stock_mids:
                raise ValueError(f"stock map MID collision: {model} {f[1]}")
        if not flat_e2e:
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
