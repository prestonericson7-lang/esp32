#!/usr/bin/env python3
"""
Pan/tilt camera head v2 - ESP32-S3-CAM pod, 2x MG90S (SG90 footprint) servos,
2x HLK-LD2410C radars on the pan head looking +/-45 deg from the camera axis.

v2.1: the pan axis is a FIXED HOLLOW POST on the base (wires go up through it), the pan
servo sits on the head and walks a 40T pinion around a fixed 20T gear on the post, so a
180 deg servo turns the head a full 360 deg and nothing but the two power wires crosses
the joint.  The pod's leads all leave through slots in the back cover.

Everything is generated from the parameters in P below with manifold3d (exact CSG) and
written out as STLs plus a Bambu Studio 3MF (two A1 mini plates).

World frame (assembled, pan = 0, tilt = 0):
  X = right when standing behind the camera, Y = backwards (camera looks -Y), Z = up.
  Base plate top is Z = 0.  Pan axis is the Z axis.
"""
import math, os, sys
import numpy as np
from manifold3d import Manifold, CrossSection, JoinType

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
SEG = 96          # segments per full circle for big round features
SEG_SMALL = 48    # for small holes

# --------------------------------------------------------------------------
# Parameters (mm)
# --------------------------------------------------------------------------
P = dict(
    # ---- MG90S / SG90 micro servo (TowerPro drawing + MG90S listing) ----
    srv_L=22.8, srv_W=12.2,          # case footprint
    srv_below=16.0,                  # case bottom -> underside of ears (seat)
    srv_ear_span=32.5, srv_ear_t=2.5,
    srv_shaft_off=6.0,               # shaft axis -> nearest end face
    srv_hole_cc=28.0,                # ear screw holes centre-to-centre (27.5..28.5 seen)
    srv_upper_h=4.0,                 # ear top -> flat top of upper case
    srv_dome_d=12.0, srv_dome_top=8.6,   # round gear cover, height above ear UNDERSIDE
    srv_collar_d=6.0, srv_collar_top=9.0,
    srv_spline_d=4.8, srv_spline_z0=9.0, srv_spline_z1=12.4,  # above ear underside (MG90S)
    srv_lead_w=4.0, srv_lead_t=1.5,
    # pockets / sockets cut for the servo
    pocket_L=23.4, pocket_W=12.9,
    pilot_w=1.8, pilot_len=2.4, pilot_depth=6.0,     # slotted pilot holes for the kit screws
    socket_d=4.7, socket_depth=5.0,                  # spline press-fit socket
    socket_cb_d=13.0, socket_cb_depth=0.8,           # clearance for the gear-cover dome
    screw_d=2.3,                                     # kit M2 centre screw
    # servo inserted through a wall/plate with its ear TOP against the surface: the socket entrance
    # face sits (8.5 - ear_t) = 6.0 past that surface; spline 6.5..9.9, dome top 6.1 past it.
    seat_to_face=8.5,

    # ---- base plate + fixed hollow post (journal) with the fixed sun gear ----
    base_x=100.0, base_y=80.0, base_t=4.0,
    base_hole_d=4.5, base_cb_d=9.0, base_cb_depth=1.5, base_hole_inset=8.0,
    head_under=20.0,                 # world Z of the head plate underside = thrust face (top of the sun flange)
    post_od=30.0, post_bore=18.0,    # hollow journal post; a USB-C plug (12 x 6.6) passes the bore
    post_top_above=14.0,             # post top above the head plate top
    gear_m=1.5, gear_base_T=28, gear_pin_T=28, gear_pa=20.0, gear_backlash=0.15,
    gear_cd_extra=0.1,               # extra centre distance for printed gears
    gear_h=10.5,                     # sun face width; teeth top 0.5 below the thrust face
    gear_root_fillet=0.45, gear_tip_round=0.2,
    foot_d=60.0, foot_h=9.0,         # cone foot under the sun flange
    cable_slot_w=13.0, cable_slot_h=7.5,   # side entry into the bore (USB-C plug passes)
    bush_clear=0.2,                  # radial clearance head bore / post
    clip_t=1.5, clip_groove_d=27.0, clip_play=0.3, clip_od=35.0,
    clip_groove_above=8.3,           # groove bottom above the head plate top WITHOUT the washer (base stays as printed)
    washer_t=2.0, washer_od=38.0, washer_clear=0.3, use_washer=True,   # 2 mm spacer on the thrust ring, always fitted
    thrust_ring_w=4.0,               # smooth annulus on the sun flange top, from the post outwards
    magnet_d=8.2, magnet_h=5.0, home_r=30.0,   # optional reed-switch homing: magnet in head, switch in base

    # ---- pan head (yoke plate) ----
    head_t=4.5,
    hub_od=36.0, hub_h=6.0,          # bushing tube above the plate (2 mm shorter: the washer lifts the head 2 mm)
    tilt_z=88.0,                     # tilt axis above head plate top (pod raised ~40 mm for wire room)
    wall_t=4.0, wall_w=28.0, wall_h=101.0, wall_x=-49.5,  # wall_x = wall OUTER face (tilt servo ear TOP lands here)
    col_x0=43.0, col_x1=50.0, col_y_back=16.0, col_y_front=-6.0, col_top=94.0,
    col_slope_h=10.0,                # 45 deg lean-forward below the pin hole
    pin_d=6.0, pin_hole_col=6.1, pin_hole_pod=6.4, pin_head_d=10.0, pin_head_t=2.5,
    pan_srv_tooth=10,                # pan servo shaft sits on sun tooth line number k (angle k*360/N_sun)
    pinion_t=8.0, pinion_top_gap=0.5,  # pinion top face below the plate underside
    pan_pad_h=2.0,                   # raised pad under the pan servo ears (keeps 8 mm ear screws inside the plate)
    pinion_screw_groove=1.8,         # clearance grooves in the pinion top under the ear-screw tips
    radar_cx=54.0, radar_cy=-32.0, radar_yaw=45.0,
    # LD2410C holder
    rad_L=22.0, rad_H=16.0, rad_pcb_t=1.6, rad_groove_w=2.0, rad_groove_eng=1.2,
    rad_rail_w=4.0, rad_rail_t=5.0, rad_ledge_h=3.0, rad_rail_h=20.0,
    rad_pin_edge=1.7,                # pin row from header edge
    head_main=(-58.0, 53.0, -34.0, 58.0),   # x0,x1,y0,y1 of main plate
    wing=30.0,

    # ---- camera pod ----
    pod_L=77.0, pod_D=35.0, pod_H=36.0, pod_wall=2.5, pod_front=3.0,
    cav_x=36.0, cav_z=15.5,          # half extents of cavity
    cover_t=2.5, lip_t=1.2, lip_h=3.0, lip_clear=0.15,
    axis_y=5.0,                      # tilt axis behind pod depth centre
    lens_hole_d=9.0, cam_recess=10.0, cam_recess_depth=1.5,
    usb_y0=-15.5, usb_y1=-7.5, usb_z=12.5,
    boss_l_r=9.0, boss_l_h=5.0, boss_r_r=6.0, boss_r_h=3.0,
    post_d=4.0, post_mod_d=3.5, post_mod_x=-31.8,
    # board mounting holes (measured off the product photo, scale checked on the USB-C shells): two at the USB
    # end only, O3.1, 3.0 from the USB edge, 24.2 apart.  Locating pegs: one round, one flattened in Z.
    hole_d=3.1, hole_from_end=3.0, hole_z=12.1, peg_d=2.4, peg_flat=1.4, peg_h=1.8, block_margin=1.5,
    cover_slot_w=14.0, cover_slot_h=3.6,   # wire slots through the cover's back face (5-pin Dupont passes)

    # ---- ESP32-S3-CAM board (68 x 29 incl. module overhang) ----
    pcb_L=63.6, pcb_W=27.6, pcb_t=1.6, mod_L=25.5, mod_W=18.0, mod_h=3.2, mod_overhang=4.4,
    hdr_edge=1.3, hdr_t=2.5, pin_len=6.0, hdr_z0=2.3, hdr_z1=52.0,
    usb_w=9.0, usb_h=3.2, usb_depth=7.0, usb_cc=10.6, plug_w=12.0, plug_h=6.6, plug_len=30.0,
    cam_base=8.0, cam_base_t=1.9, cam_barrel_d=6.4, cam_barrel_h=4.3, lens_from_mod_end=18.4,
    mod_gap=1.2,                     # module can -> front wall inner face (thermal pad)
)

# --------------------------------------------------------------------------
# small CSG helpers
# --------------------------------------------------------------------------
def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])

def cyl_z(r, z0, z1, x=0.0, y=0.0, seg=SEG):
    return Manifold.cylinder(z1 - z0, r, r, seg).translate([x, y, z0])

def cyl_x(r, x0, x1, y=0.0, z=0.0, seg=SEG):
    return Manifold.cylinder(x1 - x0, r, r, seg).rotate([0, 90, 0]).translate([x0, y, z])

def cyl_y(r, y0, y1, x=0.0, z=0.0, seg=SEG):
    return Manifold.cylinder(y1 - y0, r, r, seg).rotate([-90, 0, 0]).translate([x, y0, z])

def rrect(w, d, r, seg=32):
    """rounded rectangle cross-section centred at origin"""
    return CrossSection.square([w - 2 * r, d - 2 * r], True).offset(r, JoinType.Round, 2.0, seg)

def rrect_prism(x0, x1, y0, y1, z0, z1, r):
    cs = rrect(x1 - x0, y1 - y0, r).translate([(x0 + x1) / 2, (y0 + y1) / 2])
    return cs.extrude(z1 - z0).translate([0, 0, z0])

def poly(pts):
    """CrossSection from a single polygon, forced counter-clockwise (positive area)."""
    a = 0.0
    for i in range(len(pts)):
        x0, y0 = pts[i]; x1, y1 = pts[(i + 1) % len(pts)]
        a += x0 * y1 - x1 * y0
    if a < 0:
        pts = list(reversed(pts))
    return CrossSection([pts])

def teardrop_cs(r, seg=SEG_SMALL):
    """circle of radius r with a 45deg point towards +Y (printable overhang)."""
    pts = []
    n = seg
    a0, a1 = math.radians(135), math.radians(405)
    for i in range(n + 1):
        a = a0 + (a1 - a0) * i / n
        pts.append([r * math.cos(a), r * math.sin(a)])
    pts.append([0.0, r * math.sqrt(2)])
    return poly(pts)

def teardrop_x(r, x0, x1, y=0.0, z=0.0, point="-y", seg=SEG_SMALL):
    """prism along +X between x0..x1 with a teardrop section in the Y-Z plane.
    point: direction of the 45deg apex: '-y', '+y', '+z', '-z'."""
    m = teardrop_cs(r, seg).extrude(x1 - x0)          # along +Z, apex +Y
    m = m.rotate([0, 90, 0])                           # axis -> +X, apex still +Y
    if point == "+y":
        pass
    elif point == "-y":
        m = m.rotate([180, 0, 0])
    elif point == "+z":
        m = m.rotate([90, 0, 0])
    elif point == "-z":
        m = m.rotate([-90, 0, 0])
    return m.translate([x0, y, z])

def slot_z(w, length, z0, z1, x=0.0, y=0.0, along="x", seg=24):
    """stadium slot extruded along Z; length is overall, w is width."""
    r = w / 2
    cs = CrossSection.square([max(length - w, 0.001), w], True).offset(r, JoinType.Round, 2.0, seg)
    if along == "y":
        cs = cs.rotate(90)
    return cs.extrude(z1 - z0).translate([x, y, z0])

def slot_x(w, length, x0, x1, y=0.0, z=0.0, along="z", seg=24):
    """stadium slot extruded along X (hole through a wall). along: direction of elongation."""
    r = w / 2
    cs = CrossSection.square([max(length - w, 0.001), w], True).offset(r, JoinType.Round, 2.0, seg)
    m = cs.extrude(x1 - x0)                 # along Z, elongated along X
    m = m.rotate([0, 90, 0])                # axis -> X ; elongation now along Z
    if along == "y":
        m = m.rotate([90, 0, 0])
    return m.translate([x0, y, z])

def union(*ms):
    out = ms[0]
    for m in ms[1:]:
        out = out + m
    return out

def to_trimesh(m):
    import trimesh
    mesh = m.to_mesh()
    v = np.array(mesh.vert_properties)[:, :3]
    f = np.array(mesh.tri_verts)
    return trimesh.Trimesh(v, f, process=False)

# --------------------------------------------------------------------------
# involute spur gear (external), 20 deg pressure angle
# --------------------------------------------------------------------------
def involute_gear_cs(m, N, pa_deg=20.0, backlash=0.0, flank_pts=10, addendum=1.0, dedendum=1.25):
    """CrossSection of an external involute gear centred at the origin, tooth centred on +X."""
    pa = math.radians(pa_deg)
    rp = m * N / 2.0
    rb = rp * math.cos(pa)
    ra = rp + addendum * m
    rf = rp - dedendum * m
    inv = lambda a: math.tan(a) - a
    psi_p = math.pi / (2 * N) - backlash / (2 * rp)          # half tooth angle at pitch circle
    def flank_angle(r):
        a_r = math.acos(min(1.0, rb / r))
        return psi_p + inv(pa) - inv(a_r)
    tooth = []
    r_start = max(rf, rb)
    # right flank (negative angles), root -> tip
    rs = [r_start + (ra - r_start) * i / flank_pts for i in range(flank_pts + 1)]
    right = [(r, -flank_angle(r)) for r in rs]
    left = [(r, flank_angle(r)) for r in reversed(rs)]
    pts_polar = []
    if rf < rb:
        pts_polar.append((rf, -flank_angle(rb)))
    pts_polar += right + left
    if rf < rb:
        pts_polar.append((rf, flank_angle(rb)))
    pts = []
    pitch = 2 * math.pi / N
    for k in range(N):
        rot = k * pitch
        for (r, th) in pts_polar:
            pts.append([r * math.cos(th + rot), r * math.sin(th + rot)])
        # root arc point between teeth
        pts.append([rf * math.cos(rot + pitch / 2), rf * math.sin(rot + pitch / 2)])
    return poly(pts), dict(rp=rp, ra=ra, rf=rf, rb=rb)

def gear_centre_distance():
    return P["gear_m"] * (P["gear_base_T"] + P["gear_pin_T"]) / 2.0 + P["gear_cd_extra"]

def pan_servo_xy():
    """pan servo shaft position on the head: on sun tooth line k, at the centre distance"""
    a = math.radians(P["pan_srv_tooth"] * 360.0 / P["gear_base_T"])
    c = gear_centre_distance()
    return c * math.cos(a), c * math.sin(a)

def pinion_phase_deg():
    """pinion rotation (about its own axis, head frame) so that a tooth GAP faces the sun's tooth on the
    line of centres at pan 0: tooth centres at phase + j*pitch, we want a gap at angle (theta_c + 180)."""
    theta_c = P["pan_srv_tooth"] * 360.0 / P["gear_base_T"]
    pitch = 360.0 / P["gear_pin_T"]
    return (theta_c + 180.0 - pitch / 2.0) % pitch

def finished_gear_cs(m, N):
    """involute gear with root fillets and tip rounding (offset tricks), ready to extrude"""
    cs, gi = involute_gear_cs(m, N, P["gear_pa"], P["gear_backlash"])
    rf = P["gear_root_fillet"]; tr = P["gear_tip_round"]
    cs = cs.offset(rf, JoinType.Round, 2.0, 16).offset(-rf, JoinType.Round, 2.0, 16)   # fill root corners
    cs = cs.offset(-tr, JoinType.Round, 2.0, 16).offset(tr, JoinType.Round, 2.0, 16)   # round tips
    return cs, gi

# --------------------------------------------------------------------------
# Ghost components (for clearance checks and renders) - NOT printed
# --------------------------------------------------------------------------
def servo_ghost(spline_r=None):
    """MG90S in its local frame: shaft axis = +Z, ear UNDERSIDE at z=0 (ears z 0..2.5),
    case length along X with the shaft srv_shaft_off from the -X end face.
    Returns (body_parts_manifold, spline_manifold, lead_exit_marker)."""
    L, W = P["srv_L"], P["srv_W"]
    x0 = -P["srv_shaft_off"]; x1 = x0 + L
    xc = (x0 + x1) / 2
    lower = box(x0, x1, -W / 2, W / 2, -P["srv_below"], 0)
    ears = box(xc - P["srv_ear_span"] / 2, xc + P["srv_ear_span"] / 2, -W / 2, W / 2, 0, P["srv_ear_t"])
    upper = box(x0, x1, -W / 2, W / 2, P["srv_ear_t"], P["srv_ear_t"] + P["srv_upper_h"])
    dome = cyl_z(P["srv_dome_d"] / 2, P["srv_ear_t"] + P["srv_upper_h"] - 0.01, P["srv_dome_top"], seg=SEG_SMALL)
    collar = cyl_z(P["srv_collar_d"] / 2, P["srv_dome_top"] - 0.01, P["srv_collar_top"], seg=SEG_SMALL)
    r = P["srv_spline_d"] / 2 if spline_r is None else spline_r
    spline = cyl_z(r, P["srv_spline_z0"], P["srv_spline_z1"], seg=SEG_SMALL)
    lead = box(x0 - 3.0, x0, -P["srv_lead_w"] / 2, P["srv_lead_w"] / 2, -P["srv_below"] + 1.0, -P["srv_below"] + 1.0 + P["srv_lead_t"])
    body = union(lower, ears, upper, dome, collar)
    return body, spline, lead

def radar_ghost():
    """LD2410C in holder-local frame: u (X) along the 22 mm edge, v (Y) = board normal towards the
    component/header side (antenna faces -Y), Z up.  Header edge is the BOTTOM edge, PCB bottom edge at
    z = rad_ledge_h.  Returns (pcb, header+pins, dupont housing zone)."""
    L, H, t = P["rad_L"], P["rad_H"], P["rad_pcb_t"]
    z0 = P["rad_ledge_h"]
    pcb = box(-L / 2, L / 2, -t / 2, t / 2, z0, z0 + H)
    comps = box(-L / 2 + 1.0, L / 2 - 1.0, t / 2, t / 2 + 2.2, z0 + 1.0, z0 + H - 0.5)   # parts on the back
    zc = z0 + P["rad_pin_edge"]
    hdr = box(-6.35, 6.35, t / 2, t / 2 + P["hdr_t"], zc - 1.27, zc + 1.27)
    pins = box(-6.35, 6.35, t / 2 + P["hdr_t"], t / 2 + P["hdr_t"] + P["pin_len"], zc - 0.4, zc + 0.4)
    tails = box(-6.35, 6.35, -t / 2 - 1.2, -t / 2, zc - 0.5, zc + 0.5)   # solder tails on antenna side
    housing = box(-6.35, 6.35, t / 2 + P["hdr_t"], t / 2 + P["hdr_t"] + 14.0, zc - 1.27, zc + 1.27)
    wires = box(-6.35, 6.35, t / 2 + P["hdr_t"] + 14.0, t / 2 + P["hdr_t"] + 20.0, zc - 1.5, zc + 1.5)
    return union(pcb, tails), union(comps, hdr, pins), union(housing, wires)

def esp32cam_ghost():
    """ESP32-S3-CAM in pod-local frame.  Board lies in the X-Z plane, component side facing -Y
    (the pod front).  Module/camera end at -X, USB end at +X.  Returns dict of manifolds."""
    L, W, t = P["pcb_L"], P["pcb_W"], P["pcb_t"]
    ov = P["mod_overhang"]
    x_mod_end = -P["cav_x"] + 2.5                       # module tip 2.5 mm from cavity end wall
    x_pcb0 = x_mod_end + ov
    x_pcb1 = x_pcb0 + L
    y_front_in = -P["pod_D"] / 2 + P["pod_front"]        # front wall inner face
    y_mod_top = y_front_in + P["mod_gap"]
    y_pcb_top = y_mod_top + P["mod_h"]
    y_pcb_bot = y_pcb_top + t
    pcb = box(x_pcb0, x_pcb1, y_pcb_top, y_pcb_bot, -W / 2, W / 2)
    x_hole = x_pcb1 - P["hole_from_end"]
    for sz in (-1, 1):
        pcb -= cyl_y(P["hole_d"] / 2, y_pcb_top - 1, y_pcb_bot + 1, x_hole, sz * P["hole_z"], seg=SEG_SMALL)
    module = box(x_mod_end, x_mod_end + P["mod_L"], y_mod_top, y_pcb_top, -P["mod_W"] / 2, P["mod_W"] / 2)
    he = P["hdr_edge"]
    hz0 = x_pcb0 + P["hdr_z0"]; hz1 = x_pcb0 + P["hdr_z1"]
    hdr = union(
        box(hz0, hz1, y_pcb_bot, y_pcb_bot + P["hdr_t"], W / 2 - he - 1.27, W / 2 - he + 1.27),
        box(hz0, hz1, y_pcb_bot, y_pcb_bot + P["hdr_t"], -W / 2 + he - 1.27, -W / 2 + he + 1.27))
    pins = union(
        box(hz0, hz1, y_pcb_bot + P["hdr_t"], y_pcb_bot + P["hdr_t"] + P["pin_len"], W / 2 - he - 0.4, W / 2 - he + 0.4),
        box(hz0, hz1, y_pcb_bot + P["hdr_t"], y_pcb_bot + P["hdr_t"] + P["pin_len"], -W / 2 + he - 0.4, -W / 2 + he + 0.4))
    hous = union(
        box(hz0, hz1, y_pcb_bot + P["hdr_t"], y_pcb_bot + P["hdr_t"] + 14.0, W / 2 - he - 1.27, W / 2 - he + 1.27),
        box(hz0, hz1, y_pcb_bot + P["hdr_t"], y_pcb_bot + P["hdr_t"] + 14.0, -W / 2 + he - 1.27, -W / 2 + he + 1.27))
    bumps = union(
        box(hz0, hz1, y_pcb_top - 1.2, y_pcb_top, W / 2 - he - 1.0, W / 2 - he + 1.0),
        box(hz0, hz1, y_pcb_top - 1.2, y_pcb_top, -W / 2 + he - 1.0, -W / 2 + he + 1.0))
    yc = y_pcb_top - P["usb_h"] / 2
    usb = union(*[box(x_pcb1 - P["usb_depth"], x_pcb1 + 0.5, y_pcb_top - P["usb_h"], y_pcb_top, zc - P["usb_w"] / 2, zc + P["usb_w"] / 2)
                  for zc in (-P["usb_cc"] / 2, P["usb_cc"] / 2)])
    plugs = union(*[box(x_pcb1 + 0.5, x_pcb1 + P["plug_len"], yc - P["plug_h"] / 2, yc + P["plug_h"] / 2, zc - P["plug_w"] / 2, zc + P["plug_w"] / 2)
                    for zc in (-P["usb_cc"] / 2, P["usb_cc"] / 2)])
    xl = x_mod_end + P["lens_from_mod_end"]
    cb = P["cam_base"] / 2
    cam_base = box(xl - cb, xl + cb, y_mod_top - P["cam_base_t"], y_mod_top, -cb, cb)
    barrel = cyl_y(P["cam_barrel_d"] / 2, y_mod_top - P["cam_base_t"] - P["cam_barrel_h"], y_mod_top - P["cam_base_t"] + 0.01, xl, 0, seg=SEG_SMALL)
    parts = box(x_pcb0 + 6, x_pcb1 - 9, y_pcb_top - 4.0, y_pcb_top, -W / 2 + 4, W / 2 - 4)
    return dict(pcb=pcb, module=module, hdr=hdr, pins=pins, housings=hous, bumps=bumps, usb=usb, plugs=plugs,
                cam=union(cam_base, barrel), parts=parts,
                lens_x=xl, y_pcb_top=y_pcb_top, y_pcb_bot=y_pcb_bot, y_mod_top=y_mod_top, x_pcb1=x_pcb1, x_mod_end=x_mod_end,
                x_hole=x_hole, y_front_in=y_front_in)

# --------------------------------------------------------------------------
# PART 1: base plate + fixed hollow post with the 20T gear   (print: plate down)
# --------------------------------------------------------------------------
def part_base():
    bx, by, bt = P["base_x"], P["base_y"], P["base_t"]
    plate = rrect_prism(-bx / 2, bx / 2, -by / 2, by / 2, -bt, 0, 6.0)
    ins = P["base_hole_inset"]
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * (bx / 2 - ins), sy * (by / 2 - ins)
            plate -= cyl_z(P["base_hole_d"] / 2, -bt - 1, 1, x, y, SEG_SMALL)
            plate -= cyl_z(P["base_cb_d"] / 2, -P["base_cb_depth"], 1, x, y, SEG_SMALL)
    zu = P["head_under"]
    z_gear1 = zu - 0.5                       # teeth top 0.5 below the thrust face
    z_gear0 = z_gear1 - P["gear_h"]
    z_top = zu + P["head_t"] + P["post_top_above"]
    gcs, gi = finished_gear_cs(P["gear_m"], P["gear_base_T"])
    # sun gear flange with the smooth thrust ring on top (post radius .. post radius + thrust_ring_w)
    gear = gcs.extrude(P["gear_h"]).translate([0, 0, z_gear0])
    thrust = cyl_z(P["post_od"] / 2 + P["thrust_ring_w"], z_gear1 - 0.01, zu, seg=SEG)
    # journal post up to the top
    post = cyl_z(P["post_od"] / 2, -0.01, z_top, seg=SEG)
    # cone foot under the gear: foot_d at the plate -> gear root diameter at z_gear0
    foot = Manifold.cylinder(P["foot_h"] + 0.01, P["foot_d"] / 2, gi["ra"] + 0.3, SEG).translate([0, 0, -0.01])   # supports the tooth bottoms
    body = plate + post + gear + thrust + foot
    # hollow bore all the way, open at the top
    body -= cyl_z(P["post_bore"] / 2, -bt - 1, z_top + 1, seg=SEG)
    # side slot at the top of the post for thin leads (towards +Y = back), above the C-clip
    zs0 = zu + P["head_t"] + P["clip_groove_above"] + P["clip_t"] + 0.8   # = 35.1, as on the printed base
    body -= box(-7.0, 7.0, 0, P["post_od"] / 2 + 1, zs0, z_top + 1)
    # C-clip groove (fixed height: the base is already printed)
    zc0 = zu + P["head_t"] + P["clip_groove_above"]
    body -= (cyl_z(P["post_od"] / 2 + 1, zc0, zc0 + P["clip_t"] + 0.2, seg=SEG) - cyl_z(P["clip_groove_d"] / 2, zc0 - 1, zc0 + P["clip_t"] + 1, seg=SEG))
    # cable entry: notch through the foot/gear/post wall at plate level on the -Y side, into the bore
    body -= box(-P["cable_slot_w"] / 2, P["cable_slot_w"] / 2, -P["foot_d"] / 2 - 1, 0, -bt + 1.5, P["cable_slot_h"])
    # and a hole straight down through the plate under the bore (for an under-plate feed)
    body -= cyl_z(P["post_bore"] / 2, -bt - 1, 1, seg=SEG)
    # optional reed switch slot (glass reed 2.5 x 14) in the plate top, under the head's magnet pocket
    body -= box(-8.0, 8.0, -P["home_r"] - 1.6, -P["home_r"] + 1.6, -2.0, 1.0)
    body -= box(-P["base_y"] / 2 - 1, -P["home_r"], -P["home_r"] - 1.2, -P["home_r"] + 1.2, -2.0, 1.0)
    return body

# --------------------------------------------------------------------------
# PART 2: pan head (yoke plate + hub bushing + pan servo pocket + tilt wall + pin column + radar holders)
# head-local frame: z = 0 is the PLATE TOP; plate occupies z in [-head_t, 0]
# --------------------------------------------------------------------------
def radar_holder_local():
    """holder standing on z=0 in holder-local frame (see radar_ghost)."""
    L = P["rad_L"]; gw = P["rad_groove_w"]; eng = P["rad_groove_eng"]
    rw, rt, h = P["rad_rail_w"], P["rad_rail_t"], P["rad_rail_h"]
    half = L / 2 + 0.3                      # groove bottom (0.3 mm clearance per side)
    inner = half - eng                      # rail inner face (lips overlap board by eng)
    m = None
    for s in (-1, 1):
        rail = box(min(s * inner, s * (inner + rw)), max(s * inner, s * (inner + rw)), -rt / 2, rt / 2, 0, h)
        groove = box(min(s * (inner - 0.01), s * half), max(s * (inner - 0.01), s * half), -gw / 2, gw / 2, P["rad_ledge_h"], h + 1)
        rail -= groove
        m = rail if m is None else m + rail
    ledge = box(-inner - 0.01, inner + 0.01, -rt / 2, 0.6, 0, P["rad_ledge_h"])
    m += ledge
    return m

def head_plate_outline():
    x0, x1, y0, y1 = P["head_main"]
    main = rrect(x1 - x0, y1 - y0, 6.0).translate([(x0 + x1) / 2, (y0 + y1) / 2])
    # disc under the pinion's sweep around the pan servo
    sx, sy = pan_servo_xy()
    main = main + CrossSection.circle(P["gear_m"] * (P["gear_pin_T"] + 2) / 2 + 4.0, SEG).translate([sx, sy])
    w = P["wing"]
    wl = rrect(w, w, 4.0).rotate(-P["radar_yaw"]).translate([-P["radar_cx"], P["radar_cy"]])
    wr = rrect(w, w, 4.0).rotate(P["radar_yaw"]).translate([P["radar_cx"], P["radar_cy"]])
    return main + wl + wr

def column_profile_yz():
    """shark-fin column cross-section in (y,z): leans forward 45deg below the pin hole."""
    yf, yb, zt = P["col_y_front"], P["col_y_back"], P["col_top"]
    tz = P["tilt_z"]; sh = P["col_slope_h"]
    pts = [[yb, -0.5], [yf + sh, -0.5], [yf + sh, tz - sh], [yf, tz], [yf, zt], [yb, zt]]
    return poly(pts)

def pan_servo_window_cut():
    """cut for the pan servo dropped into the head plate from above, shaft DOWN, case along X,
    shaft at (pan_srv_x, pan_srv_y).  Includes the pilot slots (through the plate + bosses)."""
    L, W = P["pocket_L"], P["pocket_W"]
    sx, sy = pan_servo_xy()
    x0 = sx - P["srv_shaft_off"] - (L - P["srv_L"]) / 2
    x1 = x0 + L
    cut = box(x0, x1, sy - W / 2, sy + W / 2, -P["head_t"] - 1, 30)
    xc = (x0 + x1) / 2
    for s in (-1, 1):
        cut += slot_z(P["pilot_w"], P["pilot_len"], -P["head_t"] - 1, 30, xc + s * P["srv_hole_cc"] / 2, sy, along="x")
    return cut, xc

def part_head(with_holders=True):
    t = P["head_t"]
    plate = head_plate_outline().extrude(t).translate([0, 0, -t])
    # hub bushing tube above the plate + bore through everything
    hub = cyl_z(P["hub_od"] / 2, -0.01, P["hub_h"], seg=SEG)
    head = plate + hub
    # raised pad under the pan servo ears, then the window + pilot slots through pad and plate
    cut, xc = pan_servo_window_cut()
    sx, sy = pan_servo_xy()
    head += rrect_prism(xc - P["srv_ear_span"] / 2 - 1.0, xc + P["srv_ear_span"] / 2 + 1.0,
                        sy - P["srv_W"] / 2 - 1.0, sy + P["srv_W"] / 2 + 1.0, -0.01, P["pan_pad_h"], 2.0)
    head -= cut
    head -= cyl_z(P["post_od"] / 2 + P["bush_clear"], -t - 1, P["hub_h"] + 1, seg=SEG)
    # optional homing magnet pocket in the underside (front centre)
    head -= cyl_z(P["magnet_d"] / 2, -t - 1, -t + P["magnet_h"], 0, -P["home_r"], seg=SEG_SMALL)
    # ---- tilt servo wall (ears on OUTER face at x = wall_x) ----
    wx = P["wall_x"]; wt = P["wall_t"]; ww = P["wall_w"]; wh = P["wall_h"]; tz = P["tilt_z"]
    wall = box(wx, wx + wt, -ww / 2, ww / 2, -t + 0.5, wh)
    zc0 = tz - (P["srv_L"] - P["srv_shaft_off"]) - (P["pocket_L"] - P["srv_L"]) / 2
    zc1 = zc0 + P["pocket_L"]
    wall -= box(wx - 1, wx + wt + 1, -P["pocket_W"] / 2, P["pocket_W"] / 2, zc0, zc1)
    zcen = (zc0 + zc1) / 2
    for s in (-1, 1):
        wall -= slot_x(P["pilot_w"], P["pilot_len"], wx - 1, wx + wt + 1, 0, zcen + s * P["srv_hole_cc"] / 2, along="z")
    gt = 2.5
    for s in (-1, 1):
        yg0 = s * (ww / 2) - (gt if s > 0 else 0)
        yg1 = yg0 + gt
        out_g = poly([[wx, -0.5], [wx - 10.0, -0.5], [wx, 75.0]]).extrude(gt).rotate([90, 0, 0]).translate([0, yg1, 0])
        in_g = poly([[wx + wt, -0.5], [wx + wt + 14.0, -0.5], [wx + wt, 45.0]]).extrude(gt).rotate([90, 0, 0]).translate([0, yg1, 0])
        wall += out_g + (in_g if s > 0 else Manifold())
    head += wall
    # ---- pin column (right) ----
    col = column_profile_yz().extrude(P["col_x1"] - P["col_x0"]).rotate([90, 0, 90]).translate([P["col_x0"], 0, 0])
    col -= teardrop_x(P["pin_hole_col"] / 2, P["col_x0"] - 1, P["col_x1"] + 1, 0, tz, point="+z")
    cg = poly([[P["col_y_back"], -0.5], [P["col_y_back"] + 16.0, -0.5], [P["col_y_back"], 70.0]]).extrude(P["col_x1"] - P["col_x0"]).rotate([90, 0, 90]).translate([P["col_x0"], 0, 0])
    head += col + cg
    # ---- radar holders at the front corners, yawed +/-45 ----
    if with_holders:
        hl = radar_holder_local().translate([0, 0, -0.5]) + radar_holder_local()
        head += hl.rotate([0, 0, -P["radar_yaw"]]).translate([-P["radar_cx"], P["radar_cy"], 0])
        head += hl.rotate([0, 0, P["radar_yaw"]]).translate([P["radar_cx"], P["radar_cy"], 0])
    return head

def radar_transform(side):
    """(yaw_deg, (x,y)) for left (-1) / right (+1) holder in head frame"""
    return (-P["radar_yaw"] if side < 0 else P["radar_yaw"], (side * P["radar_cx"], P["radar_cy"]))

# --------------------------------------------------------------------------
# PART 3: pinion (40T) on the pan servo spline   (print: screw side down)
# pinion-local: z=0 bottom face (screw head side), top face at pinion_t with the spline socket
# --------------------------------------------------------------------------
def part_pinion():
    tcs, gi = finished_gear_cs(P["gear_m"], P["gear_pin_T"])
    h = P["pinion_t"]
    g = tcs.extrude(h)
    # spline socket from the top (the dome stays inside the plate window above the pinion)
    g -= cyl_z(P["socket_d"] / 2, h - 4.0, h + 1, seg=SEG_SMALL)
    # clearance grooves where the ear-screw tips would sweep if the kit screws are longer than 8 mm
    for rr in (abs(P["srv_hole_cc"] / 2 - (P["srv_L"] / 2 - P["srv_shaft_off"])), P["srv_hole_cc"] / 2 + (P["srv_L"] / 2 - P["srv_shaft_off"])):
        g -= cyl_z(rr + 1.4, h - P["pinion_screw_groove"], h + 1, seg=SEG) - cyl_z(rr - 1.4, h - P["pinion_screw_groove"] - 1, h + 2, seg=SEG)
    # centre screw hole + head recess from the bottom
    g -= cyl_z(P["screw_d"] / 2, -1, h, seg=SEG_SMALL)
    g -= cyl_z(2.6, -1, 1.5, seg=SEG_SMALL)
    # lightening: 6 holes between the hub and the rim
    rr = (gi["rf"] + 7.0) / 2 + 1.0
    for k in range(6):
        a = math.radians(60 * k + 30)
        g -= cyl_z(min(3.5, (gi["rf"] - 7.0) / 2 - 2.5), -1, h + 1, rr * math.cos(a), rr * math.sin(a), seg=SEG_SMALL)
    return g, gi

# --------------------------------------------------------------------------
# PART 4b: 2 mm spacer washer on the thrust ring under the head (print flat)
# --------------------------------------------------------------------------
def part_washer():
    return cyl_z(P["washer_od"] / 2, 0, P["washer_t"], seg=SEG) - cyl_z(P["post_od"] / 2 + P["washer_clear"], -1, P["washer_t"] + 1, seg=SEG)

# --------------------------------------------------------------------------
# PART 4: C-clip for the post (print flat)
# --------------------------------------------------------------------------
def part_clip():
    t = P["clip_t"]
    ring = cyl_z(P["clip_od"] / 2, 0, t, seg=SEG) - cyl_z(P["clip_groove_d"] / 2 + 0.05, -1, t + 1, seg=SEG)
    # opening: remove a 140 deg sector so it snaps on sideways over the groove
    ang = 70.0
    opening = poly([[0, 0], [30 * math.cos(math.radians(-90 - ang)), 30 * math.sin(math.radians(-90 - ang))],
                    [0, -40], [30 * math.cos(math.radians(-90 + ang)), 30 * math.sin(math.radians(-90 + ang))]]).extrude(t + 2).translate([0, 0, -1])
    return ring - opening

# --------------------------------------------------------------------------
# PART 5: camera pod   (print: front face down)
# pod-local frame: centre of the box at origin; front wall at -Y; tilt axis = line (y=axis_y, z=0) along X
# --------------------------------------------------------------------------
def part_pod():
    L, D, H = P["pod_L"], P["pod_D"], P["pod_H"]
    w = P["pod_wall"]; fw = P["pod_front"]
    cx, cz = P["cav_x"], P["cav_z"]
    ay = P["axis_y"]
    outer = rrect(L, H, 3.0).extrude(D - P["cover_t"]).rotate([-90, 0, 0]).translate([0, -D / 2, 0])
    y_in = -D / 2 + fw
    cav = box(-cx, cx, y_in, D / 2 + 1, -cz, cz)
    pod = outer - cav
    g = esp32cam_ghost()
    xl = g["lens_x"]
    pod -= cyl_y(P["lens_hole_d"] / 2, -D / 2 - 1, y_in + 1, xl, 0, seg=SEG_SMALL)
    r = P["cam_recess"] / 2
    pod -= box(xl - r, xl + r, y_in - P["cam_recess_depth"], y_in + 1, -r, r)
    pod -= box(cx - 1, L / 2 + 1, P["usb_y0"], P["usb_y1"], -P["usb_z"], P["usb_z"])
    # board locating blocks in the two USB-end corners of the cavity, standing on the front wall, top at the
    # PCB's component face, with a peg through each mounting hole (round one at +z, flattened one at -z)
    xh = g["x_hole"]; yt = g["y_pcb_top"]; hz = P["hole_z"]
    for sz in (-1, 1):
        z_in = hz - P["block_margin"]
        # block stops 1.4 mm short of the end wall so a USB-C overmold (12 wide) clears it
        pod += box(xh - 3.0, xh + 3.0, y_in - 0.5, yt, min(sz * z_in, sz * (cz + 0.5)), max(sz * z_in, sz * (cz + 0.5)))
        if sz > 0:
            pod += cyl_y(P["peg_d"] / 2, yt - 0.01, yt + P["peg_h"], xh, sz * hz, seg=SEG_SMALL)
        else:
            pod += rrect(P["peg_d"], P["peg_flat"], P["peg_flat"] / 2 - 0.01).translate([xh, sz * hz]).extrude(P["peg_h"] + 0.01).rotate([-90, 0, 0]).translate([0, yt - 0.01, 0])
    yg0, yg1 = D / 2 - P["cover_t"] - P["lip_h"], D / 2 - P["cover_t"] - P["lip_h"] + 1.8
    for sx in (-1, 1):
        for sz in (-1, 1):
            pod -= box(min(sx * 9.0, sx * 21.0), max(sx * 9.0, sx * 21.0), yg0, yg1,
                       min(sz * (cz - 0.01), sz * (cz + 0.6)), max(sz * (cz - 0.01), sz * (cz + 0.6)))
    for sx in (-1, 1):
        pod -= box(min(sx * (cx - 0.5), sx * (L / 2 + 1)), max(sx * (cx - 0.5), sx * (L / 2 + 1)), D / 2 - P["cover_t"] - 1.5, D / 2 + 1, -4, 4)
    bl = P["boss_l_h"]
    pod += teardrop_x(P["boss_l_r"], -L / 2 - bl, -L / 2 + 0.01, ay, 0, point="-y")
    pod -= teardrop_x(P["socket_d"] / 2, -L / 2 - bl - 1, -L / 2 - bl + P["socket_depth"], ay, 0, point="+y")
    pod -= cyl_x(P["socket_cb_d"] / 2, -L / 2 - bl - 1, -L / 2 - bl + P["socket_cb_depth"], ay, 0, SEG_SMALL)
    pod -= cyl_x(P["screw_d"] / 2, -L / 2 - bl - 1, -cx + 1, ay, 0, SEG_SMALL)
    br = P["boss_r_h"]
    pod += teardrop_x(P["boss_r_r"], L / 2 - 0.01, L / 2 + br, ay, 0, point="-y")
    pod -= teardrop_x(P["pin_hole_pod"] / 2, cx - 1, L / 2 + br + 1, ay, 0, point="+y")
    return pod

def part_cover():
    L, D, H = P["pod_L"], P["pod_D"], P["pod_H"]
    cx, cz = P["cav_x"], P["cav_z"]
    ct, lt, lh, lc = P["cover_t"], P["lip_t"], P["lip_h"], P["lip_clear"]
    y1 = D / 2; y0 = y1 - ct
    plate = rrect(L, H, 3.0).extrude(ct).rotate([-90, 0, 0]).translate([0, y0, 0])
    ox, oz = cx - lc, cz - lc
    lip = box(-ox, ox, y0 - lh, y0 + 0.01, -oz, oz) - box(-ox + lt, ox - lt, y0 - lh - 1, y0 + 1, -oz + lt, oz - lt)
    for sx in (-1, 1):
        for xs in (8.5, 21.5):
            lip -= box(sx * xs - 0.5, sx * xs + 0.5, y0 - lh - 1, y0 - 0.3, -oz - 1, -oz + lt + 0.5)
            lip -= box(sx * xs - 0.5, sx * xs + 0.5, y0 - lh - 1, y0 - 0.3, oz - lt - 0.5, oz + 1)
    cover = plate + lip
    for sx in (-1, 1):
        for sz in (-1, 1):
            xa, xb = min(sx * 10.0, sx * 20.0), max(sx * 10.0, sx * 20.0)
            zb = sz * oz
            b1 = box(xa, xb, y0 - lh + 0.4, y0 - lh + 1.6, min(zb, zb + sz * 0.5), max(zb, zb + sz * 0.5))
            b2 = box(xa, xb, y0 - lh - 0.1, y0 - lh + 1.6, min(zb, zb + sz * 0.05), max(zb, zb + sz * 0.05))
            cover += Manifold.batch_hull([b1, b2])
    # wire slots THROUGH the back face (leads leave backwards, away from the swing): three slots in the
    # lower half, between the retention posts; a 5-pin Dupont housing (12.7 x 2.6) passes flat.
    sw, sh = P["cover_slot_w"], P["cover_slot_h"]
    for xcn in (-22.0, 0.0, 22.0):
        cover -= slot_z(sh, sw, y0 - lh - 1, y1 + 1, xcn, 0, along="x").rotate([90, 0, 0]).translate([0, 0, 0]) if False else \
                 rrect(sw, sh, sh / 2 - 0.01).translate([xcn, -7.0]).extrude(ct + lh + 2).rotate([-90, 0, 0]).translate([0, y0 - lh - 1, 0])
    # board retention posts
    g = esp32cam_ghost()
    for sz in (-1, 1):
        post = cyl_y(P["post_d"] / 2, g["y_pcb_bot"] + 0.1, y0 + 0.01, g["x_hole"], sz * P["hole_z"], SEG_SMALL)
        post -= cyl_y(P["peg_d"] / 2 + 0.4, g["y_pcb_bot"] - 1, g["y_pcb_bot"] + 0.9, g["x_hole"], sz * P["hole_z"], SEG_SMALL)
        cover += post
    cover += cyl_y(P["post_mod_d"] / 2, g["y_pcb_top"] + 0.3, y0 + 0.01, P["post_mod_x"], 0, SEG_SMALL)
    return cover

def part_pin():
    """pivot pin, printed standing on its head"""
    d, hd, ht = P["pin_d"], P["pin_head_d"], P["pin_head_t"]
    shank_len = (P["col_x1"] - P["col_x0"]) + 1.5 + P["boss_r_h"] + P["pod_wall"] - 0.3
    head = cyl_z(hd / 2, 0, ht, seg=SEG_SMALL)
    shank = cyl_z(d / 2, ht - 0.01, ht + shank_len, seg=SEG_SMALL)
    tip = Manifold.cylinder(0.6, d / 2, d / 2 - 0.5, SEG_SMALL).translate([0, 0, ht + shank_len - 0.01])
    return head + shank + tip, shank_len

# --------------------------------------------------------------------------
# Assembly transforms
# --------------------------------------------------------------------------
def head_z():
    """world Z of the head plate TOP (head-local z = 0); the washer lifts the whole head"""
    return P["head_under"] + P["head_t"] + (P["washer_t"] if P["use_washer"] else 0.0)

def pod_in_head(tilt_deg=0.0):
    """pod-local -> head-local transform applied to a manifold"""
    def f(m):
        return m.translate([0, -P["axis_y"], 0]).rotate([tilt_deg, 0, 0]).translate([0, 0, P["tilt_z"]])
    return f

def pinion_in_head():
    """pinion-local (bottom face z=0) -> head-local: hangs under the plate on the pan servo spline"""
    ztop = -P["head_t"] - P["pinion_top_gap"]
    sx, sy = pan_servo_xy()
    return lambda m: m.translate([sx, sy, ztop - P["pinion_t"]])

def pan_servo_in_head():
    """servo-local (ear underside z=0, shaft +Z) -> head-local: shaft DOWN, ear TOP on the plate top"""
    sx, sy = pan_servo_xy()
    return lambda m: m.rotate([180, 0, 0]).translate([sx, sy, P["srv_ear_t"] + P["pan_pad_h"]])

def clip_in_head():
    """clip-local (z=0 bottom) -> head-local: the groove is at a fixed world height on the base"""
    zg = P["head_under"] + P["head_t"] + P["clip_groove_above"] - head_z()
    return lambda m: m.translate([0, 0, zg])

def washer_world():
    """washer-local (z=0 bottom) -> world: on the thrust ring"""
    return lambda m: m.translate([0, 0, P["head_under"]])

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("building parts ...")
    parts = {}
    parts["base"] = part_base()
    parts["head"] = part_head()
    parts["pod"] = part_pod()
    parts["cover"] = part_cover()
    pin, shank_len = part_pin()
    parts["pin"] = pin
    pinion, gi = part_pinion()
    parts["pinion"] = pinion
    parts["clip"] = part_clip()
    parts["washer"] = part_washer()
    for k, m in parts.items():
        tm = to_trimesh(m)
        print(f"{k:6s} status={m.status()} tris={m.num_tri():6d} vol={m.volume():9.0f} mm3  bbox={np.round(tm.bounds,1).tolist()} watertight={tm.is_watertight}")
        tm.export(os.path.join(OUT, f"{k}.stl"))
    print("pin shank length", shank_len, " pinion rp/ra/rf", gi, " centre distance", gear_centre_distance(),
          " servo at", [round(v, 2) for v in pan_servo_xy()], " pinion phase", round(pinion_phase_deg(), 3))
