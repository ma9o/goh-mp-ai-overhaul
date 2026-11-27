"""Project E2E telemetry onto captured bird's-eye screenshots for visual tactical analysis."""

import ctypes
import json
import math
import re
import sys
from pathlib import Path

if __package__:
    from . import live_mission as mission
else:
    import live_mission as mission


def _e2e_camera():
    p = mission.e2e()
    xs, ys = zip(*p.positions)
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    span = math.hypot(max(xs) - min(xs), max(ys) - min(ys))
    scale = max(1, span / 1100)
    dy = min(1200 * scale, 2400)
    dz = max(950 * scale, math.ceil(span * 1.15))
    eye = (cx, cy - dy, 10.0 + dz)
    look = (cx, cy, 10.0)
    return p, eye, look, 50.0


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _norm(a):
    length = math.hypot(*a)
    return (a[0] / length, a[1] / length, a[2] / length)


def make_projector(eye, look, hfov_deg, width, height):
    """Build a world (x, y, z) -> pixel (px, py_bottom_up) projector for GEM's left-handed Z-up camera."""
    fwd = _norm(_sub(look, eye))
    right = _norm(_cross((0.0, 0.0, 1.0), fwd))
    up = _norm(_cross(fwd, right))
    fx = (width / 2.0) / math.tan(math.radians(hfov_deg / 2.0))

    def project(wx, wy, wz=10.0):
        v = _sub((wx, wy, wz), eye)
        zc = _dot(v, fwd)
        if zc <= 1.0:
            return None
        xc = _dot(v, right)
        yc = _dot(v, up)
        px = (width / 2.0) + (xc / zc) * fx
        # CoreGraphics CGBitmapContext uses bottom-left origin (y increases upward).
        py = (height / 2.0) + (yc / zc) * fx
        return (px, py)

    return project


def latest_sim_tick(telemetry_path):
    """Read the latest sim_tick from the tail of goh_telemetry.jsonl in O(1)."""
    if not telemetry_path or not telemetry_path.is_file():
        return None
    try:
        size = telemetry_path.stat().st_size
        if size <= 0:
            return None
        with telemetry_path.open("rb") as f:
            f.seek(max(0, size - 4096))
            chunk = f.read().decode("utf-8", errors="replace")
        for line in reversed(chunk.splitlines()):
            line = line.strip()
            if not line.endswith("}"):
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            tick = row.get("sim_tick")
            if isinstance(tick, (int, float)):
                return int(tick)
    except OSError:
        return None
    return None


class _CGPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]


class _CGSize(ctypes.Structure):
    _fields_ = [("width", ctypes.c_double), ("height", ctypes.c_double)]


class _CGRect(ctypes.Structure):
    _fields_ = [("origin", _CGPoint), ("size", _CGSize)]


def _load_quartz():
    cg = ctypes.CDLL("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")
    cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
    iio = ctypes.CDLL("/System/Library/Frameworks/ImageIO.framework/ImageIO")

    cf.CFURLCreateFromFileSystemRepresentation.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_bool
    ]
    cf.CFURLCreateFromFileSystemRepresentation.restype = ctypes.c_void_p
    cf.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
    cf.CFStringCreateWithCString.restype = ctypes.c_void_p
    cf.CFRelease.argtypes = [ctypes.c_void_p]

    iio.CGImageSourceCreateWithURL.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    iio.CGImageSourceCreateWithURL.restype = ctypes.c_void_p
    iio.CGImageSourceCreateImageAtIndex.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
    iio.CGImageSourceCreateImageAtIndex.restype = ctypes.c_void_p
    iio.CGImageDestinationCreateWithURL.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
    iio.CGImageDestinationCreateWithURL.restype = ctypes.c_void_p
    iio.CGImageDestinationAddImage.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    iio.CGImageDestinationFinalize.argtypes = [ctypes.c_void_p]
    iio.CGImageDestinationFinalize.restype = ctypes.c_bool

    cg.CGImageGetWidth.argtypes = [ctypes.c_void_p]
    cg.CGImageGetWidth.restype = ctypes.c_size_t
    cg.CGImageGetHeight.argtypes = [ctypes.c_void_p]
    cg.CGImageGetHeight.restype = ctypes.c_size_t
    cg.CGColorSpaceCreateDeviceRGB.restype = ctypes.c_void_p
    cg.CGBitmapContextCreate.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t,
        ctypes.c_size_t, ctypes.c_void_p, ctypes.c_uint32,
    ]
    cg.CGBitmapContextCreate.restype = ctypes.c_void_p
    cg.CGBitmapContextCreateImage.argtypes = [ctypes.c_void_p]
    cg.CGBitmapContextCreateImage.restype = ctypes.c_void_p
    cg.CGContextDrawImage.argtypes = [ctypes.c_void_p, _CGRect, ctypes.c_void_p]
    cg.CGContextSetRGBFillColor.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double]
    cg.CGContextSetRGBStrokeColor.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double]
    cg.CGContextSetLineWidth.argtypes = [ctypes.c_void_p, ctypes.c_double]
    cg.CGContextFillEllipseInRect.argtypes = [ctypes.c_void_p, _CGRect]
    cg.CGContextStrokeEllipseInRect.argtypes = [ctypes.c_void_p, _CGRect]
    cg.CGContextFillRect.argtypes = [ctypes.c_void_p, _CGRect]
    cg.CGContextStrokeRect.argtypes = [ctypes.c_void_p, _CGRect]
    cg.CGContextBeginPath.argtypes = [ctypes.c_void_p]
    cg.CGContextMoveToPoint.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double]
    cg.CGContextAddLineToPoint.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double]
    cg.CGContextClosePath.argtypes = [ctypes.c_void_p]
    cg.CGContextStrokePath.argtypes = [ctypes.c_void_p]
    cg.CGContextFillPath.argtypes = [ctypes.c_void_p]
    return cg, cf, iio


def _draw_tactical_jpeg(src, dst, actors, roles, eye, look, hfov_deg):
    cg, cf, iio = _load_quartz()
    src_b = str(src).encode("utf-8")
    dst_b = str(dst).encode("utf-8")
    src_url = cf.CFURLCreateFromFileSystemRepresentation(None, src_b, len(src_b), False)
    img_src = iio.CGImageSourceCreateWithURL(src_url, None) if src_url else None
    img = iio.CGImageSourceCreateImageAtIndex(img_src, 0, None) if img_src else None
    if not img:
        for ref in (img_src, src_url):
            if ref:
                cf.CFRelease(ref)
        return False

    w = cg.CGImageGetWidth(img)
    h = cg.CGImageGetHeight(img)
    s = max(1.0, w / 1800.0)
    proj = make_projector(eye, look, hfov_deg, w, h)

    cs = cg.CGColorSpaceCreateDeviceRGB()
    # kCGImageAlphaPremultipliedLast (1)
    ctx = cg.CGBitmapContextCreate(None, w, h, 8, w * 4, cs, 1)
    cg.CGContextDrawImage(ctx, _CGRect(_CGPoint(0, 0), _CGSize(w, h)), img)

    # 1. Center flag 30m (600 GEM units) capture zone ring projected onto z=10 ground plane.
    fx, fy = mission.E2E_FLAG
    radius_gem = mission.E2E_FLAG_RADIUS_M * 20.0
    ring_pts = []
    for k in range(48):
        ang = 2.0 * math.pi * k / 48.0
        pt = proj(fx + radius_gem * math.cos(ang), fy + radius_gem * math.sin(ang), 10.0)
        if pt:
            ring_pts.append(pt)
    if len(ring_pts) >= 3:
        cg.CGContextSetRGBStrokeColor(ctx, 1.0, 0.85, 0.15, 0.78)
        cg.CGContextSetLineWidth(ctx, 2.5 * s)
        cg.CGContextBeginPath(ctx)
        cg.CGContextMoveToPoint(ctx, ring_pts[0][0], ring_pts[0][1])
        for px, py in ring_pts[1:]:
            cg.CGContextAddLineToPoint(ctx, px, py)
        cg.CGContextClosePath(ctx)
        cg.CGContextStrokePath(ctx)
    fpt = proj(fx, fy, 10.0)
    if fpt:
        r = 6.0 * s
        cg.CGContextSetRGBFillColor(ctx, 1.0, 0.85, 0.15, 0.9)
        cg.CGContextFillEllipseInRect(ctx, _CGRect(_CGPoint(fpt[0] - r, fpt[1] - r), _CGSize(2 * r, 2 * r)))

    # 2. Draw dead markers first so live units always sit cleanly on top.
    def team_rgb(player):
        return (0.14, 0.58, 1.0) if player == 2 else (1.0, 0.18, 0.18)

    def draw_x(px, py, size, r, g, b, alpha):
        cg.CGContextSetRGBStrokeColor(ctx, r, g, b, alpha)
        cg.CGContextSetLineWidth(ctx, 2.5 * s)
        cg.CGContextBeginPath(ctx)
        cg.CGContextMoveToPoint(ctx, px - size, py - size)
        cg.CGContextAddLineToPoint(ctx, px + size, py + size)
        cg.CGContextMoveToPoint(ctx, px - size, py + size)
        cg.CGContextAddLineToPoint(ctx, px + size, py - size)
        cg.CGContextStrokePath(ctx)

    def draw_hp_bar(px, py, bw, bh, hp_ratio, r, g, b):
        bx, by = px - bw / 2.0, py
        cg.CGContextSetRGBFillColor(ctx, 0.0, 0.0, 0.0, 0.78)
        cg.CGContextFillRect(ctx, _CGRect(_CGPoint(bx - s, by - s), _CGSize(bw + 2 * s, bh + 2 * s)))
        cg.CGContextSetRGBFillColor(ctx, r, g, b, 0.95)
        cg.CGContextFillRect(ctx, _CGRect(_CGPoint(bx, by), _CGSize(bw * max(0.0, min(1.0, hp_ratio)), bh)))

    ordered = sorted(actors.values(), key=lambda a: (0 if a["hp_ratio"] <= 0 else 1, 0 if a["is_human"] else 1))
    for a in ordered:
        mid = a["mid"]
        role = roles.get(mid, "")
        if not role:
            continue
        pt = proj(a["pos_x"], a["pos_y"], 10.0)
        if not pt:
            continue
        px, py = pt
        if not (-60 <= px <= w + 60 and -60 <= py <= h + 60):
            continue
        r, g, b = team_rgb(a["player"])
        alive = a["hp_ratio"] > 0.0
        if not alive:
            sz = (12.0 if not a["is_human"] else 6.0) * s
            draw_x(px, py, sz, r, g, b, 0.55)
            continue

        if a["is_human"]:
            is_squad = "_sq_" in role or role.endswith("_medic")
            is_medic = role.endswith("_medic")
            is_mg = "_mg" in role
            is_at = "_at" in role
            if not is_squad:
                # Tank crew / reserve hull riders: smaller translucent hollow rings to stay distinct from squad infantry.
                rad = 4.5 * s
                rect = _CGRect(_CGPoint(px - rad, py - rad), _CGSize(2 * rad, 2 * rad))
                cg.CGContextSetRGBFillColor(ctx, r, g, b, 0.35)
                cg.CGContextFillEllipseInRect(ctx, rect)
                cg.CGContextSetRGBStrokeColor(ctx, r, g, b, 0.90)
                cg.CGContextSetLineWidth(ctx, 1.5 * s)
                cg.CGContextStrokeEllipseInRect(ctx, rect)
                continue
            rad = (8.5 if is_medic else 7.0) * s
            if is_mg:
                # Triangle for machine gunner
                cg.CGContextSetRGBFillColor(ctx, r, g, b, 0.92)
                cg.CGContextSetRGBStrokeColor(ctx, 1.0, 1.0, 1.0, 0.95)
                cg.CGContextSetLineWidth(ctx, 1.8 * s)
                cg.CGContextBeginPath(ctx)
                cg.CGContextMoveToPoint(ctx, px, py + rad * 1.35)
                cg.CGContextAddLineToPoint(ctx, px - rad * 1.15, py - rad * 0.9)
                cg.CGContextAddLineToPoint(ctx, px + rad * 1.15, py - rad * 0.9)
                cg.CGContextClosePath(ctx)
                cg.CGContextFillPath(ctx)
                cg.CGContextBeginPath(ctx)
                cg.CGContextMoveToPoint(ctx, px, py + rad * 1.35)
                cg.CGContextAddLineToPoint(ctx, px - rad * 1.15, py - rad * 0.9)
                cg.CGContextAddLineToPoint(ctx, px + rad * 1.15, py - rad * 0.9)
                cg.CGContextClosePath(ctx)
                cg.CGContextStrokePath(ctx)
            else:
                rect = _CGRect(_CGPoint(px - rad, py - rad), _CGSize(2 * rad, 2 * rad))
                cg.CGContextSetRGBFillColor(ctx, r, g, b, 0.92)
                cg.CGContextFillEllipseInRect(ctx, rect)
                cg.CGContextSetRGBStrokeColor(ctx, 1.0, 1.0, 0.3 if is_at else 1.0, 0.95)
                cg.CGContextSetLineWidth(ctx, (2.4 if is_at else 1.6) * s)
                cg.CGContextStrokeEllipseInRect(ctx, rect)
            if is_medic:
                cr = 4.8 * s
                cg.CGContextSetRGBStrokeColor(ctx, 1.0, 1.0, 1.0, 0.98)
                cg.CGContextSetLineWidth(ctx, 2.2 * s)
                cg.CGContextBeginPath(ctx)
                cg.CGContextMoveToPoint(ctx, px - cr, py)
                cg.CGContextAddLineToPoint(ctx, px + cr, py)
                cg.CGContextMoveToPoint(ctx, px, py - cr)
                cg.CGContextAddLineToPoint(ctx, px, py + cr)
                cg.CGContextStrokePath(ctx)
            if a["hp_ratio"] < 0.95:
                draw_hp_bar(px, py - rad - 5.5 * s, 16.0 * s, 3.0 * s, a["hp_ratio"], r, g, b)
        else:
            # Vehicles: diamond = Sd.Kfz.222, square = Panzer IV, double rectangle = T-35
            if "car_222" in role:
                rad = 15.0 * s
                cg.CGContextSetRGBFillColor(ctx, r, g, b, 0.40)
                cg.CGContextSetRGBStrokeColor(ctx, r, g, b, 0.98)
                cg.CGContextSetLineWidth(ctx, 3.0 * s)
                for fill in (True, False):
                    cg.CGContextBeginPath(ctx)
                    cg.CGContextMoveToPoint(ctx, px, py + rad)
                    cg.CGContextAddLineToPoint(ctx, px + rad, py)
                    cg.CGContextAddLineToPoint(ctx, px, py - rad)
                    cg.CGContextAddLineToPoint(ctx, px - rad, py)
                    cg.CGContextClosePath(ctx)
                    cg.CGContextFillPath(ctx) if fill else cg.CGContextStrokePath(ctx)
                draw_hp_bar(px, py - rad - 7.5 * s, 30.0 * s, 4.5 * s, a["hp_ratio"], r, g, b)
            else:
                is_t35 = "tank_t35" in role
                hw = (28.0 if is_t35 else 21.0) * s
                hh = (19.0 if is_t35 else 16.0) * s
                rect = _CGRect(_CGPoint(px - hw, py - hh), _CGSize(2 * hw, 2 * hh))
                cg.CGContextSetRGBFillColor(ctx, r, g, b, 0.34)
                cg.CGContextFillRect(ctx, rect)
                cg.CGContextSetRGBStrokeColor(ctx, 1.0, 1.0, 1.0, 0.92)
                cg.CGContextSetLineWidth(ctx, 4.5 * s)
                cg.CGContextStrokeRect(ctx, rect)
                cg.CGContextSetRGBStrokeColor(ctx, r, g, b, 0.98)
                cg.CGContextSetLineWidth(ctx, 2.8 * s)
                cg.CGContextStrokeRect(ctx, rect)
                if is_t35:
                    inner = _CGRect(_CGPoint(px - hw + 5 * s, py - hh + 5 * s), _CGSize(2 * hw - 10 * s, 2 * hh - 10 * s))
                    cg.CGContextSetLineWidth(ctx, 1.8 * s)
                    cg.CGContextStrokeRect(ctx, inner)
                draw_hp_bar(px, py - hh - 8.5 * s, (48.0 if is_t35 else 36.0) * s, 5.0 * s, a["hp_ratio"], r, g, b)

    out_img = cg.CGBitmapContextCreateImage(ctx)
    dst_url = cf.CFURLCreateFromFileSystemRepresentation(None, dst_b, len(dst_b), False)
    uti = cf.CFStringCreateWithCString(None, b"public.jpeg", 0x08000100)
    dest = iio.CGImageDestinationCreateWithURL(dst_url, uti, 1, None) if dst_url else None
    ok = False
    if dest and out_img:
        iio.CGImageDestinationAddImage(dest, out_img, None)
        ok = bool(iio.CGImageDestinationFinalize(dest))
    for ref in (dest, uti, dst_url, out_img, ctx, cs, img, img_src, src_url):
        if ref:
            cf.CFRelease(ref)
    return ok


def annotate_e2e_frames(evidence_dir):
    """Render team-colored tactical-*.jpg companion frames beside raw frame-*.jpg for an E2E trial."""
    if sys.platform != "darwin":
        return 0
    evidence = Path(evidence_dir)
    frames_dir = evidence / "frames"
    telemetry = evidence / "goh_telemetry.jsonl"
    if not frames_dir.is_dir() or not telemetry.is_file():
        return 0
    raw_frames = sorted(frames_dir.glob("frame-*.jpg"))
    if not raw_frames:
        return 0

    phase, eye, look, hfov = _e2e_camera()
    roles = {mid: role for role, mid in phase.mids.items()}
    snapshots = []
    with telemetry.open(encoding="utf-8", errors="replace") as inp:
        for line in inp:
            if '"actor_snapshot"' not in line:
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if row.get("event") == "actor_snapshot" and row.get("mid") in roles:
                snapshots.append(row)
    if not snapshots:
        return 0

    t_min = min(r["sim_tick"] for r in snapshots)
    t_max = max(r["sim_tick"] for r in snapshots)
    meta_path = frames_dir / "frames_meta.json"
    meta = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.is_file() else {}

    # Fallback wall-time -> sim-tick mapping if frames_meta.json isn't present.
    wall_secs = []
    for f in raw_frames:
        m = re.search(r"_(\d+)s(?:_end)?\.jpg$", f.name)
        wall_secs.append(int(m.group(1)) if m else 0)
    max_wall = max(wall_secs) or 1

    idx = 0
    state = {}
    written = 0
    for f, ws in zip(raw_frames, wall_secs):
        tick = meta.get(f.name)
        if tick is None:
            tick = t_max if f.name.endswith("_end.jpg") else int(t_min + (ws / max_wall) * (t_max - t_min))
        while idx < len(snapshots) and snapshots[idx]["sim_tick"] <= tick:
            r = snapshots[idx]
            state[r["mid"]] = r
            idx += 1
        sim_s = max(0, int(round((tick - t_min) / 1000.0)))
        dst = frames_dir / f.name.replace("frame-", "tactical-", 1)
        if "_sim" not in dst.name:
            dst = dst.with_name(re.sub(r"_\d+s(_end)?\.jpg$", rf"_sim{sim_s:03d}s\1.jpg", dst.name))
        if _draw_tactical_jpeg(f, dst, state, roles, eye, look, hfov):
            written += 1
    return written
