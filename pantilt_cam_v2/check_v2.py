#!/usr/bin/env python3
"""Validation for the v2 pan/tilt head: manifoldness, assembly interference, component
clearance, tilt sweep (pod + USB plug vs head), print overhangs, A1 mini bed fit."""
import os, sys, math, json
import numpy as np
from manifold3d import Manifold
import gen_pantilt_v2 as g
from gen_pantilt_v2 import P, box, to_trimesh, union

def vol(m):
    return m.volume()

def world_parts(tilt=0.0, pan=0.0):
    """return dict name -> Manifold in WORLD frame for a given pan/tilt (deg)."""
    hz = g.head_z()
    base = g.part_base()
    head = g.part_head()
    pod = g.part_pod()
    cover = g.part_cover()
    pin, _ = g.part_pin()
    f = g.pod_in_head(tilt)
    W = {}
    W["base"] = base
    W["head"] = head.rotate([0, 0, pan]).translate([0, 0, hz])
    W["pod"] = f(pod).rotate([0, 0, pan]).translate([0, 0, hz])
    W["cover"] = f(cover).rotate([0, 0, pan]).translate([0, 0, hz])
    # pin: local +Z (shank direction) -> world -X ; head outer face at x = col_x1
    W["pin"] = pin.rotate([0, -90, 0]).translate([P["col_x1"] + P["pin_head_t"], 0, P["tilt_z"]]).rotate([0, 0, pan]).translate([0, 0, hz])
    return W

def ghosts(tilt=0.0):
    """component ghosts in WORLD frame (pan = 0)."""
    hz = g.head_z()
    G = {}
    body, spline, lead = g.servo_ghost()
    body2, spline_r, _ = g.servo_ghost(spline_r=P["socket_d"] / 2 - 0.05)
    # pan servo: seat at z = seat_z, shaft on Z axis
    G["pan_servo_body"] = body.translate([0, 0, P["seat_z"]])
    G["pan_servo_spline"] = spline_r.translate([0, 0, P["seat_z"]])
    G["pan_servo_lead"] = lead.translate([0, 0, P["seat_z"]])
    # tilt servo: local +Z -> world +X, local +X (case) -> world -Z; ear TOP face (local z = ear_t) on wall outer face
    R = lambda m: m.rotate([0, 90, 0]).translate([P["wall_x"] - P["srv_ear_t"], 0, P["tilt_z"] + hz])
    G["tilt_servo_body"] = R(body)
    G["tilt_servo_spline"] = R(spline_r)
    G["tilt_servo_lead"] = R(lead)
    # radars
    for side in (-1, 1):
        yaw, (x, y) = g.radar_transform(side)
        pcb, comps, hous = g.radar_ghost()
        T = lambda m: m.rotate([0, 0, yaw]).translate([x, y, hz])
        nm = "L" if side < 0 else "R"
        G[f"radar{nm}_pcb"] = T(pcb); G[f"radar{nm}_comps"] = T(comps); G[f"radar{nm}_housing"] = T(hous)
    # ESP32 board in the pod
    e = g.esp32cam_ghost()
    f = g.pod_in_head(tilt)
    for k in ("pcb", "module", "hdr", "pins", "housings", "bumps", "usb", "plugs", "cam", "parts"):
        G["esp_" + k] = f(e[k]).translate([0, 0, hz])
    return G

def inter(a, b):
    return (a ^ b).volume()

def report_pairs(title, pairs):
    print(f"\n== {title}")
    bad = 0
    for name, a, b in pairs:
        v = inter(a, b)
        flag = "" if v < 1e-6 else "   <-- INTERFERENCE"
        if v >= 1e-6: bad += 1
        print(f"  {name:38s} intersect = {v:9.3f} mm3{flag}")
    return bad

def overhangs(m, name, thresh=-0.72):
    """list downward-facing faces steeper than 45deg (n.z < thresh) above the bed, grouped by height"""
    tm = to_trimesh(m)
    n = tm.face_normals
    zmin = tm.vertices[tm.faces].min(axis=1)[:, 2]
    area = tm.area_faces
    sel = (n[:, 2] < thresh) & (zmin > 0.05)
    tot = area[sel].sum()
    print(f"\n== overhang faces for {name}: {sel.sum()} faces, {tot:.1f} mm2 (bed at z=0)")
    if sel.sum() == 0:
        return
    zs = np.round(zmin[sel], 1)
    for z in sorted(set(zs)):
        s = sel.copy(); s[sel] = (zs == z)
        c = tm.triangles_center[s]
        a = area[s].sum()
        ext = (c.min(axis=0), c.max(axis=0))
        print(f"   z={z:6.1f}  area={a:8.1f} mm2  x[{ext[0][0]:7.1f},{ext[1][0]:7.1f}] y[{ext[0][1]:7.1f},{ext[1][1]:7.1f}]")

if __name__ == "__main__":
    out = {}
    print("== part sanity")
    W0 = world_parts(0.0)
    for k, m in W0.items():
        tm = to_trimesh(m)
        ok = m.status() == m.status().__class__.NoError and tm.is_watertight and m.volume() > 0
        print(f"  {k:6s} watertight={tm.is_watertight} genus={m.genus()} vol={m.volume():8.0f}")
        assert ok, k

    # ---------------- assembly interference at rest ----------------
    bad = 0
    bad += report_pairs("parts vs parts (pan 0, tilt 0)", [
        ("base ^ head", W0["base"], W0["head"]),
        ("head ^ pod", W0["head"], W0["pod"]),
        ("head ^ cover", W0["head"], W0["cover"]),
        ("pod ^ cover (lip in cavity)", W0["pod"], W0["cover"]),
        ("pin ^ head (hole 4.9 vs pin 4.8)", W0["pin"], W0["head"]),
        ("pin ^ pod (hole 5.2 vs pin 4.8)", W0["pin"], W0["pod"]),
        ("base ^ pod", W0["base"], W0["pod"]),
    ])
    # ring / groove radial clearance + pan sweep
    for pan in (0, 30, 45, 90):
        Wp = world_parts(0.0, pan)
        v = inter(Wp["base"], Wp["head"])
        print(f"  base ^ head @ pan {pan:3d}: {v:.3f}")
        bad += v > 1e-6

    # ---------------- components ----------------
    G = ghosts(0.0)
    bad += report_pairs("components vs printed parts (tilt 0)", [
        ("pan servo body ^ base", G["pan_servo_body"], W0["base"]),
        ("pan servo body ^ head", G["pan_servo_body"], W0["head"]),
        ("pan servo spline(4.6) ^ head", G["pan_servo_spline"], W0["head"]),
        ("pan servo lead ^ base", G["pan_servo_lead"], W0["base"]),
        ("tilt servo body ^ head", G["tilt_servo_body"], W0["head"]),
        ("tilt servo body ^ pod", G["tilt_servo_body"], W0["pod"]),
        ("tilt servo spline(4.6) ^ pod", G["tilt_servo_spline"], W0["pod"]),
        ("radarL pcb ^ head", G["radarL_pcb"], W0["head"]),
        ("radarL comps ^ head", G["radarL_comps"], W0["head"]),
        ("radarL housing ^ head", G["radarL_housing"], W0["head"]),
        ("radarR pcb ^ head", G["radarR_pcb"], W0["head"]),
        ("radarR comps ^ head", G["radarR_comps"], W0["head"]),
        ("radarR housing ^ head", G["radarR_housing"], W0["head"]),
        ("esp pcb ^ pod", G["esp_pcb"], W0["pod"]),
        ("esp module ^ pod", G["esp_module"], W0["pod"]),
        ("esp headers ^ pod", G["esp_hdr"], W0["pod"]),
        ("esp pins ^ pod", G["esp_pins"], W0["pod"]),
        ("esp dupont housings ^ pod", G["esp_housings"], W0["pod"]),
        ("esp dupont housings ^ cover", G["esp_housings"], W0["cover"]),
        ("esp solder bumps ^ pod", G["esp_bumps"], W0["pod"]),
        ("esp usb receptacles ^ pod", G["esp_usb"], W0["pod"]),
        ("esp usb plugs ^ pod", G["esp_plugs"], W0["pod"]),
        ("esp usb plugs ^ head (tilt 0)", G["esp_plugs"], W0["head"]),
        ("esp camera ^ pod (barrel thru lens hole)", G["esp_cam"], W0["pod"]),
        ("esp top-side parts ^ pod", G["esp_parts"], W0["pod"]),
        ("esp pcb ^ cover posts", G["esp_pcb"], W0["cover"]),
        ("esp usb receptacles ^ cover", G["esp_usb"], W0["cover"]),
        ("esp headers ^ cover", G["esp_hdr"], W0["cover"]),
        ("esp pins ^ cover", G["esp_pins"], W0["cover"]),
        ("esp camera ^ head (tilt 0)", G["esp_cam"], W0["head"]),
        ("tilt servo lead ^ head", G["tilt_servo_lead"], W0["head"]),
        ("esp module ^ cover", G["esp_module"], W0["cover"]),
    ])
    # does the spline actually reach into the sockets?  (engagement length)
    for nm, sp, part in (("pan", G["pan_servo_spline"], W0["head"]), ("tilt", G["tilt_servo_spline"], W0["pod"])):
        # engagement = spline volume inside the part's convex hull region of the socket: approximate by
        # intersecting the spline with the part's bounding-box slab in the socket region.
        tm = to_trimesh(sp); bb = tm.bounds
        print(f"  {nm} servo spline extent: {np.round(bb[0],2).tolist()} -> {np.round(bb[1],2).tolist()}")

    # ---------------- tilt sweep ----------------
    print("\n== tilt sweep (pod+cover vs head; USB plugs vs head). +tilt = camera looks DOWN")
    first_pod = None; first_plug_dn = None; first_plug_up = None
    for tilt in list(range(-90, 91, 5)):
        Wt = world_parts(float(tilt))
        Gt = ghosts(float(tilt))
        v_pod = inter(Wt["pod"], Wt["head"]) + inter(Wt["cover"], Wt["head"])
        v_plug = inter(Gt["esp_plugs"], Wt["head"])
        gap = Wt["pod"].min_gap(Wt["head"], 30.0)
        v_rad = sum(inter(Gt[k], Wt["pod"]) + inter(Gt[k], Wt["cover"]) for k in ("radarL_housing", "radarR_housing", "radarL_pcb", "radarR_pcb"))
        v_srv = inter(Gt["tilt_servo_body"], Wt["pod"]) + inter(Gt["tilt_servo_body"], Wt["cover"])
        mark = ""
        if v_pod > 1e-6: mark += " POD-HITS-HEAD"; first_pod = first_pod if first_pod is not None else tilt
        if v_plug > 1e-6:
            mark += " PLUG-HITS-HEAD"
            if tilt > 0 and first_plug_dn is None: first_plug_dn = tilt
            if tilt < 0: first_plug_up = tilt
        if v_rad > 1e-6: mark += " RADAR-HIT"
        if v_srv > 1e-6: mark += " SERVO-HIT"
        print(f"  tilt {tilt:4d}: pod/head gap = {gap:5.2f} mm   plug^head = {v_plug:7.1f} mm3{mark}")
    print(f"  -> pod never touches head: {first_pod is None};  USB plug clears head down to +{(first_plug_dn or 95)-5} deg, up to {(first_plug_up or -95)+5} deg")

    # ---------------- overhangs in print orientation ----------------
    base = g.part_base(); head = g.part_head(); pod = g.part_pod(); cover = g.part_cover(); pin, _ = g.part_pin()
    overhangs(base.translate([0, 0, P["base_t"]]), "base (plate down)")
    overhangs(head.translate([0, 0, P["head_t"]]), "head (plate down)")
    overhangs(pod.rotate([90, 0, 0]).translate([0, 0, P["pod_D"] / 2]), "pod (front face down)")
    overhangs(cover.rotate([-90, 0, 0]).translate([0, 0, P["pod_D"] / 2]), "cover (outer face down)")
    overhangs(pin, "pin (head down)")

    print("\nTOTAL interference flags:", bad)
    sys.exit(1 if bad else 0)
