#!/usr/bin/env python3
"""
Pan/tilt camera head v2 - ESP32-S3-CAM pod, 2x MG90S (SG90 footprint) servos,
2x HLK-LD2410C radars on the pan head looking +/-45 deg from the camera axis.

Everything is generated from the parameters in P below with manifold3d (exact
CSG) and written out as STLs plus a Bambu Studio 3MF (two A1 mini plates).

World frame (assembled, pan = 0, tilt = 0):
  X = right when standing behind the camera, Y = backwards (camera looks -Y),
  Z = up.  Base plate top is Z = 0.  Pan axis is the Z axis.

Run:  python3 gen_pantilt_v2.py            -> writes out/*.stl, out/*.3mf, checks
"""
import math, os, sys, json
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
    srv_dome_d=12.0, srv_dome_top=8.6,   # round gear cover, height above seat
    srv_collar_d=6.0, srv_collar_top=9.0,
    srv_spline_d=4.8, srv_spline_z0=9.0, srv_spline_z1=12.4,  # above seat (MG90S)
    srv_lead_w=4.0, srv_lead_t=1.5,
    # pockets / sockets cut for the servo
    pocket_L=23.4, pocket_W=12.9, pocket_depth=17.0,
    pilot_w=1.8, pilot_len=2.4, pilot_depth=6.0,     # slotted pilot holes for the kit screws
    socket_d=4.7, socket_depth=5.0,                  # spline press-fit socket
    socket_cb_d=13.0, socket_cb_depth=0.8,           # clearance for the gear-cover dome
    screw_d=2.3,                                     # kit M2 centre screw
    seat_to_face=8.5,                                # ear UNDERSIDE (seat) -> socket entrance face (pan)
    # tilt servo goes through the wall from outside, so its ear TOP face is the contact plane:
    # socket entrance face is (seat_to_face - srv_ear_t) = 6.0 inboard of the wall's outer face

    # ---- base plate + pan turret ----
    base_x=100.0, base_y=80.0, base_t=4.0,
    base_hole_d=4.5, base_cb_d=9.0, base_cb_depth=1.5, base_hole_inset=8.0,
    seat_z=15.0,                     # pan servo ear seat above plate top
    turret_od=51.0, ring_id=47.0, ring_top=10.0,     # ring top above seat
    turret_foot_od=59.0, turret_foot_h=4.0,
    lead_w=9.0, lead_h=4.5,          # pan lead tunnel (JR plug 7.7 x 2.6 passes)
    drain_d=5.0,

    # ---- pan head (yoke plate) ----
    head_t=4.5, groove_depth=2.5, groove_clear=1.0,  # radial clearance ring/groove
    hub_d=16.0, hub_h=3.0,
    tilt_z=38.0,                     # tilt axis above head plate top
    wall_t=3.0, wall_w=28.0, wall_h=50.0, wall_x=-49.5,   # wall_x = wall OUTER face (tilt servo ear TOP lands here)
    col_x0=43.0, col_x1=50.0, col_y_back=16.0, col_y_front=-6.0, col_top=44.0,
    col_slope_h=10.0,                # 45 deg lean-forward below the pin hole
    pin_d=4.8, pin_hole_col=4.9, pin_hole_pod=5.2, pin_head_d=8.0, pin_head_t=2.5,
    radar_cx=54.0, radar_cy=-32.0, radar_yaw=45.0,
    # LD2410C holder
    rad_L=22.0, rad_H=16.0, rad_pcb_t=1.6, rad_groove_w=2.0, rad_groove_eng=1.2,
    rad_rail_w=4.0, rad_rail_t=5.0, rad_ledge_h=3.0, rad_rail_h=20.0,
    rad_pin_edge=1.7,                # pin row from header edge
    head_main=(-58.0, 53.0, -34.0, 32.0),   # x0,x1,y0,y1 of main plate
    wing=30.0,

    # ---- camera pod ----
    pod_L=77.0, pod_D=35.0, pod_H=36.0, pod_wall=2.5, pod_front=3.0,
    cav_x=36.0, cav_z=15.5,          # half extents of cavity
    cover_t=2.5, lip_t=1.2, lip_h=3.0, lip_clear=0.15,
    axis_y=5.0,                      # tilt axis behind pod depth centre
    lens_hole_d=8.0, cam_recess=9.4, cam_recess_depth=1.5,
    usb_y0=-15.5, usb_y1=-7.5, usb_z=12.5,
    boss_l_r=9.0, boss_l_h=5.0, boss_r_r=5.0, boss_r_h=3.0,
    post_d=5.0, post_x=28.0, post_z=12.0, post_mod_d=3.5, post_mod_x=-31.8,

    # ---- ESP32-S3-CAM board (68 x 29 incl. module overhang) ----
    pcb_L=63.7, pcb_W=29.0, pcb_t=1.6, mod_L=25.5, mod_W=18.0, mod_h=3.2, mod_overhang=4.3,
    hdr_edge=1.3, hdr_t=2.5, pin_len=6.0, hdr_z0=2.3, hdr_z1=52.0,
    usb_w=9.0, usb_h=3.2, usb_depth=7.0, usb_cc=11.6, plug_w=12.0, plug_h=6.6, plug_len=30.0,
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

def teardrop_cs(r, seg=SEG_SMALL):
    """circle of radius r with a 45deg point towards +Y (printable overhang)."""
    pts = []
    n = seg
    # arc from 135deg around through 270deg to 45deg (long way)
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
    # rotate extrusion axis Z -> X ; after rotate([0,90,0]): (x,y,z)->(z, y, -x)
    m = m.rotate([0, 90, 0])                           # apex now along +Y, axis +X
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
    m = m.rotate([0, 90, 0])                # axis -> X ; elongation now along -Z
    if along == "y":
        m = m.rotate([90, 0, 0])
    return m.translate([x0, y, z])

def poly(pts):
    """CrossSection from a single polygon, forced counter-clockwise (positive area)."""
    a = 0.0
    for i in range(len(pts)):
        x0, y0 = pts[i]; x1, y1 = pts[(i + 1) % len(pts)]
        a += x0 * y1 - x1 * y0
    if a < 0:
        pts = list(reversed(pts))
    return CrossSection([pts])

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
# Ghost components (for clearance checks and renders) - NOT printed
# --------------------------------------------------------------------------
def servo_ghost(spline_r=None):
    """MG90S in its local frame: shaft axis = +Z, ear seat (underside of ears) at z=0,
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
    module = box(x_mod_end, x_mod_end + P["mod_L"], y_mod_top, y_pcb_top, -P["mod_W"] / 2, P["mod_W"] / 2)
    # headers (plastic + pins) on the back along both long edges
    he = P["hdr_edge"]
    hz0 = x_pcb0 + P["hdr_z0"]; hz1 = x_pcb0 + P["hdr_z1"]
    hdr = union(
        box(hz0, hz1, y_pcb_bot, y_pcb_bot + P["hdr_t"], W / 2 - he - 1.27, W / 2 - he + 1.27),
        box(hz0, hz1, y_pcb_bot, y_pcb_bot + P["hdr_t"], -W / 2 + he - 1.27, -W / 2 + he + 1.27))
    pins = union(
        box(hz0, hz1, y_pcb_bot + P["hdr_t"], y_pcb_bot + P["hdr_t"] + P["pin_len"], W / 2 - he - 0.4, W / 2 - he + 0.4),
        box(hz0, hz1, y_pcb_bot + P["hdr_t"], y_pcb_bot + P["hdr_t"] + P["pin_len"], -W / 2 + he - 0.4, -W / 2 + he + 0.4))
    # dupont housings zone on the pins (worst case: whole rows populated)
    hous = union(
        box(hz0, hz1, y_pcb_bot + P["hdr_t"], y_pcb_bot + P["hdr_t"] + 14.0, W / 2 - he - 1.27, W / 2 - he + 1.27),
        box(hz0, hz1, y_pcb_bot + P["hdr_t"], y_pcb_bot + P["hdr_t"] + 14.0, -W / 2 + he - 1.27, -W / 2 + he + 1.27))
    # solder bumps on the component side at the pins
    bumps = union(
        box(hz0, hz1, y_pcb_top - 1.2, y_pcb_top, W / 2 - he - 1.0, W / 2 - he + 1.0),
        box(hz0, hz1, y_pcb_top - 1.2, y_pcb_top, -W / 2 + he - 1.0, -W / 2 + he + 1.0))
    # USB-C receptacles at +X end, on component side, and plug zones beyond the edge
    yc = y_pcb_top - P["usb_h"] / 2
    usb = union(*[box(x_pcb1 - P["usb_depth"], x_pcb1 + 0.5, y_pcb_top - P["usb_h"], y_pcb_top, zc - P["usb_w"] / 2, zc + P["usb_w"] / 2)
                  for zc in (-P["usb_cc"] / 2, P["usb_cc"] / 2)])
    plugs = union(*[box(x_pcb1 + 0.5, x_pcb1 + P["plug_len"], yc - P["plug_h"] / 2, yc + P["plug_h"] / 2, zc - P["plug_w"] / 2, zc + P["plug_w"] / 2)
                    for zc in (-P["usb_cc"] / 2, P["usb_cc"] / 2)])
    # camera head sitting on the module can, lens barrel through the front wall
    xl = x_mod_end + P["lens_from_mod_end"]
    cb = P["cam_base"] / 2
    cam_base = box(xl - cb, xl + cb, y_mod_top - P["cam_base_t"], y_mod_top, -cb, cb)
    barrel = cyl_y(P["cam_barrel_d"] / 2, y_mod_top - P["cam_base_t"] - P["cam_barrel_h"], y_mod_top - P["cam_base_t"] + 0.01, xl, 0, seg=SEG_SMALL)
    # misc parts on component side (buttons, LEDs, FPC connector) ~ 4 mm tall over the PCB middle
    parts = box(x_pcb0 + 6, x_pcb1 - 9, y_pcb_top - 4.0, y_pcb_top, -W / 2 + 4, W / 2 - 4)
    return dict(pcb=pcb, module=module, hdr=hdr, pins=pins, housings=hous, bumps=bumps, usb=usb, plugs=plugs,
                cam=union(cam_base, barrel), parts=parts,
                lens_x=xl, y_pcb_top=y_pcb_top, y_pcb_bot=y_pcb_bot, y_mod_top=y_mod_top, x_pcb1=x_pcb1, x_mod_end=x_mod_end)

# --------------------------------------------------------------------------
# PART 1: base plate + pan turret   (print: plate down)
# --------------------------------------------------------------------------
def servo_pocket_cut(seat_z, lead=True):
    """Cuts for a servo dropped in from +Z with its ear seat at z=seat_z, shaft on the Z axis,
    case along +X (shaft srv_shaft_off from the -X end)."""
    L, W = P["pocket_L"], P["pocket_W"]
    x0 = -P["srv_shaft_off"] - (L - P["srv_L"]) / 2
    x1 = x0 + L
    zb = seat_z - P["pocket_depth"]
    cut = box(x0, x1, -W / 2, W / 2, zb, seat_z + 30)
    xc = (x0 + x1) / 2
    for sx in (-1, 1):
        cx = xc + sx * P["srv_hole_cc"] / 2
        cut += slot_z(P["pilot_w"], P["pilot_len"], seat_z - P["pilot_depth"], seat_z + 30, cx, 0, along="x")
    return cut, x0, x1, zb

def part_base():
    bx, by, bt = P["base_x"], P["base_y"], P["base_t"]
    plate = rrect_prism(-bx / 2, bx / 2, -by / 2, by / 2, -bt, 0, 6.0)
    ins = P["base_hole_inset"]
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * (bx / 2 - ins), sy * (by / 2 - ins)
            plate -= cyl_z(P["base_hole_d"] / 2, -bt - 1, 1, x, y, SEG_SMALL)
            plate -= cyl_z(P["base_cb_d"] / 2, -P["base_cb_depth"], 1, x, y, SEG_SMALL)
    seat = P["seat_z"]
    top = seat + P["ring_top"]
    turret = cyl_z(P["turret_od"] / 2, -0.01, top)
    foot = Manifold.cylinder(P["turret_foot_h"], P["turret_foot_od"] / 2, P["turret_od"] / 2, SEG).translate([0, 0, -0.01])
    body = plate + turret + foot
    # counterbore above the seat (ring interior)
    body -= cyl_z(P["ring_id"] / 2, seat, top + 1)
    # servo pocket + pilot slots
    cut, px0, px1, pzb = servo_pocket_cut(seat)
    body -= cut
    # lead cavity at shaft end (bottom part of end wall cut back 3 mm, full width + offset part)
    lw = P["lead_w"]
    body -= box(px0 - 3.0, px0 + 0.01, -P["pocket_W"] / 2, 2.5 + lw, pzb, pzb + 10.0)
    # lead slot up to the seat (offset so the pilot slot at y=0 keeps solid material)
    body -= box(px0 - 3.0, px0 + 0.01, 2.5, 2.5 + lw, pzb, top + 1)
    # tunnel out to the side at plate level
    body -= box(-P["turret_foot_od"] / 2 - 1, px0 - 3.0 + 0.01, 2.5, 2.5 + lw, pzb, pzb + P["lead_h"])
    # drain hole through plate under the pocket
    body -= cyl_z(P["drain_d"] / 2, -bt - 1, pzb + 1, (px0 + px1) / 2, 0, SEG_SMALL)
    return body

# --------------------------------------------------------------------------
# PART 2: pan head (yoke plate + hub + servo wall + pin column + radar holders)  (print: plate down)
# head-local frame: plate top at z=0, pan axis = Z axis, tilt axis = line (y=0, z=tilt_z) along X
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
    # small fillet/foot flare for strength: 45deg wedge along the rails' outer faces
    return m

def head_plate_outline():
    x0, x1, y0, y1 = P["head_main"]
    main = rrect(x1 - x0, y1 - y0, 6.0).translate([(x0 + x1) / 2, (y0 + y1) / 2])
    w = P["wing"]
    wl = rrect(w, w, 4.0).rotate(-P["radar_yaw"]).translate([-P["radar_cx"], P["radar_cy"]])
    wr = rrect(w, w, 4.0).rotate(P["radar_yaw"]).translate([P["radar_cx"], P["radar_cy"]])
    return main + wl + wr

def column_profile_yz():
    """shark-fin column cross-section in (y,z): vertical back, front face vertical at y_front
    down to z_top - slope... ; actually front leans forward 45deg below the pin hole."""
    yf, yb, zt = P["col_y_front"], P["col_y_back"], P["col_top"]
    tz = P["tilt_z"]; sh = P["col_slope_h"]
    # polygon in (y,z): start bottom-back, go bottom-front, up the front
    pts = [[yb, -0.5], [yf + sh, -0.5], [yf + sh, tz - sh], [yf, tz], [yf, zt], [yb, zt]]
    return poly(pts)

def part_head(with_holders=True):
    """head-local frame: z = 0 is the PLATE TOP; plate occupies z in [-head_t, 0]; underside features
    (ring groove, spline socket) are cut from z = -head_t upward."""
    t = P["head_t"]
    plate = head_plate_outline().extrude(t).translate([0, 0, -t])
    # underside groove for the turret ring (ring: ID ring_id, OD turret_od)
    c = P["groove_clear"]
    g_id = P["ring_id"] - 2 * c; g_od = P["turret_od"] + 2 * c
    groove = cyl_z(g_od / 2, -t - 1, -t + P["groove_depth"]) - cyl_z(g_id / 2, -t - 2, -t + P["groove_depth"] + 1)
    plate -= groove
    # hub boss on top with the spline socket from below
    hub = cyl_z(P["hub_d"] / 2, -0.01, P["hub_h"], seg=SEG_SMALL)
    head = plate + hub
    head -= cyl_z(P["socket_d"] / 2, -t - 1, -t + P["socket_depth"], seg=SEG_SMALL)          # socket (from underside)
    head -= cyl_z(P["socket_cb_d"] / 2, -t - 1, -t + P["socket_cb_depth"], seg=SEG_SMALL)    # dome clearance
    head -= cyl_z(P["screw_d"] / 2, -t + P["socket_depth"] - 0.5, P["hub_h"] + 1, seg=SEG_SMALL)
    # ---- tilt servo wall (ears on OUTER face at x = wall_x) ----
    wx = P["wall_x"]; wt = P["wall_t"]; ww = P["wall_w"]; wh = P["wall_h"]; tz = P["tilt_z"]
    wall = box(wx, wx + wt, -ww / 2, ww / 2, -t + 0.5, wh)
    # window for the servo case (shaft end UP): case from tz - (srv_L - shaft_off) to tz + shaft_off
    zc0 = tz - (P["srv_L"] - P["srv_shaft_off"]) - (P["pocket_L"] - P["srv_L"]) / 2
    zc1 = zc0 + P["pocket_L"]
    wall -= box(wx - 1, wx + wt + 1, -P["pocket_W"] / 2, P["pocket_W"] / 2, zc0, zc1)
    zcen = (zc0 + zc1) / 2
    for s in (-1, 1):
        wall -= slot_x(P["pilot_w"], P["pilot_len"], wx - 1, wx + wt + 1, 0, zcen + s * P["srv_hole_cc"] / 2, along="z")
    # gussets: outboard (beside the servo, at the wall's y ends) and inboard (low, under the pod swing)
    gt = 2.5
    for s in (-1, 1):
        yg0 = s * (ww / 2) - (gt if s > 0 else 0)
        yg1 = yg0 + gt
        out_g = poly([[wx, -0.5], [wx - 6.0, -0.5], [wx, 35.0]]).extrude(gt).rotate([90, 0, 0]).translate([0, yg1, 0])
        in_g = poly([[wx + wt, -0.5], [wx + wt + 10.0, -0.5], [wx + wt, 12.0]]).extrude(gt).rotate([90, 0, 0]).translate([0, yg1, 0])
        wall += out_g + (in_g if s > 0 else Manifold())
    head += wall
    # ---- pin column (right) ----
    col = column_profile_yz().extrude(P["col_x1"] - P["col_x0"]).rotate([90, 0, 90]).translate([P["col_x0"], 0, 0])
    col -= teardrop_x(P["pin_hole_col"] / 2, P["col_x0"] - 1, P["col_x1"] + 1, 0, tz, point="+z")
    # rear gusset behind the column
    cg = poly([[P["col_y_back"], -0.5], [P["col_y_back"] + 10.0, -0.5], [P["col_y_back"], 30.0]]).extrude(P["col_x1"] - P["col_x0"]).rotate([90, 0, 90]).translate([P["col_x0"], 0, 0])
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
# PART 3: camera pod   (print: front face down)
# pod-local frame: centre of the box at origin; front wall at -Y; tilt axis = line (y=axis_y, z=0) along X
# --------------------------------------------------------------------------
def part_pod():
    L, D, H = P["pod_L"], P["pod_D"], P["pod_H"]
    w = P["pod_wall"]; fw = P["pod_front"]
    cx, cz = P["cav_x"], P["cav_z"]
    ay = P["axis_y"]
    # outline rounded in X-Z (so the rounded edges stand vertical when printed front-face-down)
    outer = rrect(L, H, 3.0).extrude(D - P["cover_t"]).rotate([-90, 0, 0]).translate([0, -D / 2, 0])
    y_in = -D / 2 + fw
    cav = box(-cx, cx, y_in, D / 2 + 1, -cz, cz)
    pod = outer - cav
    g = esp32cam_ghost()
    # lens hole + camera recess on the inner face of the front wall
    xl = g["lens_x"]
    pod -= cyl_y(P["lens_hole_d"] / 2, -D / 2 - 1, y_in + 1, xl, 0, seg=SEG_SMALL)
    r = P["cam_recess"] / 2
    pod -= box(xl - r, xl + r, y_in - P["cam_recess_depth"], y_in + 1, -r, r)
    # USB-C opening in the +X end wall
    pod -= box(cx - 1, L / 2 + 1, P["usb_y0"], P["usb_y1"], -P["usb_z"], P["usb_z"])
    # cover rabbet: nothing (cover lip goes inside cavity); snap grooves in top/bottom cavity walls
    yg0, yg1 = D / 2 - P["cover_t"] - P["lip_h"], D / 2 - P["cover_t"] - P["lip_h"] + 1.8
    for sx in (-1, 1):
        for sz in (-1, 1):
            pod -= box(min(sx * 9.0, sx * 21.0), max(sx * 9.0, sx * 21.0), yg0, yg1,
                       min(sz * (cz - 0.01), sz * (cz + 0.6)), max(sz * (cz - 0.01), sz * (cz + 0.6)))
    # finger notches at the +/-X ends of the rim so the cover can be pried
    for sx in (-1, 1):
        pod -= box(min(sx * (cx - 0.5), sx * (L / 2 + 1)), max(sx * (cx - 0.5), sx * (L / 2 + 1)), D / 2 - P["cover_t"] - 1.5, D / 2 + 1, -4, 4)
    # (rim ends at y = D/2 - cover_t; the cover plate sits on it and closes flush with the box outline)
    # ---- left end: spline socket boss (teardrop, apex towards -Y = down in the print) ----
    bl = P["boss_l_h"]
    boss_l = teardrop_x(P["boss_l_r"], -L / 2 - bl, -L / 2 + 0.01, ay, 0, point="-y")
    pod += boss_l
    pod -= teardrop_x(P["socket_d"] / 2, -L / 2 - bl - 1, -L / 2 - bl + P["socket_depth"], ay, 0, point="+y")
    pod -= teardrop_x(P["socket_cb_d"] / 2, -L / 2 - bl - 1, -L / 2 - bl + P["socket_cb_depth"], ay, 0, point="+y")
    pod -= cyl_x(P["screw_d"] / 2, -L / 2 - bl - 1, -cx + 1, ay, 0, SEG_SMALL)
    # ---- right end: pivot boss with pin hole ----
    br = P["boss_r_h"]
    boss_r = teardrop_x(P["boss_r_r"], L / 2 - 0.01, L / 2 + br, ay, 0, point="-y")
    pod += boss_r
    pod -= teardrop_x(P["pin_hole_pod"] / 2, cx - 1, L / 2 + br + 1, ay, 0, point="+y")
    return pod

def part_cover():
    L, D, H = P["pod_L"], P["pod_D"], P["pod_H"]
    cx, cz = P["cav_x"], P["cav_z"]
    ct, lt, lh, lc = P["cover_t"], P["lip_t"], P["lip_h"], P["lip_clear"]
    y1 = D / 2; y0 = y1 - ct
    plate = rrect(L, H, 3.0).extrude(ct).rotate([-90, 0, 0]).translate([0, y0, 0])
    # lip ring inside the cavity opening
    ox, oz = cx - lc, cz - lc
    lip = box(-ox, ox, y0 - lh, y0 + 0.01, -oz, oz) - box(-ox + lt, ox - lt, y0 - lh - 1, y0 + 1, -oz + lt, oz - lt)
    # free the snap tabs: slits in the lip at x = +/-8.5 and +/-21.5 on top & bottom edges
    for sx in (-1, 1):
        for xs in (8.5, 21.5):
            lip -= box(sx * xs - 0.5, sx * xs + 0.5, y0 - lh - 1, y0 - 0.3, -oz - 1, -oz + lt + 0.5)
            lip -= box(sx * xs - 0.5, sx * xs + 0.5, y0 - lh - 1, y0 - 0.3, oz - lt - 0.5, oz + 1)
    cover = plate + lip
    # snap bumps on the tabs (0.5 proud, with 45deg lead-in towards the insertion direction -Y)
    for sx in (-1, 1):
        for sz in (-1, 1):
            xa, xb = min(sx * 10.0, sx * 20.0), max(sx * 10.0, sx * 20.0)
            zb = sz * oz
            # bump = hull of a thin strip at the lip face and a 0.5 mm proud strip (gives a 45deg lead-in)
            b1 = box(xa, xb, y0 - lh + 0.4, y0 - lh + 1.6, min(zb, zb + sz * 0.5), max(zb, zb + sz * 0.5))
            b2 = box(xa, xb, y0 - lh - 0.1, y0 - lh + 1.6, min(zb, zb + sz * 0.05), max(zb, zb + sz * 0.05))
            cover += Manifold.batch_hull([b1, b2])
    # wire notches: bottom centre and both lower corners
    cover -= box(-8.0, 8.0, y0 - lh - 1, y1 + 1, -H / 2 - 1, -cz + 2.0)
    for sx in (-1, 1):
        cover -= box(min(sx * 30.0, sx * (L / 2 + 1)), max(sx * 30.0, sx * (L / 2 + 1)), y0 - lh - 1, y1 + 1, -H / 2 - 1, -cz + 3.0)
    # board retention posts
    g = esp32cam_ghost()
    for sz in (-1, 1):
        cover += cyl_y(P["post_d"] / 2, g["y_pcb_bot"] + 0.3, y0 + 0.01, P["post_x"], sz * P["post_z"], SEG_SMALL)
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
    """world Z of the head plate TOP (head-local z = 0)"""
    return P["seat_z"] + P["seat_to_face"] + P["head_t"]

def pod_in_head(tilt_deg=0.0):
    """pod-local -> head-local transform applied to a manifold"""
    def f(m):
        # move tilt axis (y=axis_y,z=0) to origin, rotate about X, then to (0, 0, tilt_z)
        return m.translate([0, -P["axis_y"], 0]).rotate([tilt_deg, 0, 0]).translate([0, 0, P["tilt_z"]])
    return f

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
    for k, m in parts.items():
        tm = to_trimesh(m)
        print(f"{k:6s} status={m.status()} tris={m.num_tri():6d} vol={m.volume():9.0f} mm3  bbox={np.round(tm.bounds,1).tolist()} watertight={tm.is_watertight}")
        tm.export(os.path.join(OUT, f"{k}.stl"))
    print("pin shank length", shank_len)
