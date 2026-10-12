#!/usr/bin/env python3
"""Validation for the v2.1 pan/tilt head: manifoldness, assembly interference, component
clearance, gear mesh over a full pan turn, tilt sweep (pod + USB plug vs head), print overhangs."""
import os, sys, math
import numpy as np
from manifold3d import Manifold
import gen_pantilt_v2 as g
from gen_pantilt_v2 import P, box, to_trimesh, union

def world_parts(tilt=0.0, pan=0.0):
    """dict name -> Manifold in WORLD frame for a given pan/tilt (deg).  pan rotates the head about Z."""
    hz = g.head_z()
    base = g.part_base()
    head = g.part_head()
    pod = g.part_pod()
    cover = g.part_cover()
    pin, _ = g.part_pin()
    pinion, _ = g.part_pinion()
    clip = g.part_clip()
    f = g.pod_in_head(tilt)
    H = lambda m: m.rotate([0, 0, pan]).translate([0, 0, hz])      # head-local -> world
    W = {}
    W["base"] = base
    W["head"] = H(head)
    W["pod"] = H(f(pod))
    W["cover"] = H(f(cover))
    W["pin"] = H(pin.rotate([0, -90, 0]).translate([P["col_x1"] + P["pin_head_t"], 0, P["tilt_z"]]))
    # pinion: the servo on the head keeps it at a fixed place in the head frame; it spins about its own
    # axis by -pan * (base_T / pin_T) ... for the mesh check we rotate it by the correct rolling angle.
    # planet rolling round a fixed sun: relative to the carrier (head) it turns +pan * N_sun/N_planet
    roll = g.pinion_phase_deg() + pan * (P["gear_base_T"] / P["gear_pin_T"])
    W["pinion"] = H(g.pinion_in_head()(pinion.rotate([0, 0, roll])))
    W["clip"] = H(g.clip_in_head()(clip))
    if P["use_washer"]:
        W["washer"] = g.washer_world()(g.part_washer())
    return W

def ghosts(tilt=0.0, pan=0.0):
    """component ghosts in WORLD frame."""
    hz = g.head_z()
    H = lambda m: m.rotate([0, 0, pan]).translate([0, 0, hz])
    G = {}
    body, spline, lead = g.servo_ghost()
    _, spline_r, _ = g.servo_ghost(spline_r=P["socket_d"] / 2 - 0.05)
    # pan servo on the head, shaft down
    S = g.pan_servo_in_head()
    G["pan_servo_body"] = H(S(body)); G["pan_servo_spline"] = H(S(spline_r)); G["pan_servo_lead"] = H(S(lead))
    # tilt servo: local +Z -> world +X, local +X (case) -> world -Z; ear TOP face (local z = ear_t) on wall outer face
    R = lambda m: H(m.rotate([0, 90, 0]).translate([P["wall_x"] - P["srv_ear_t"], 0, P["tilt_z"]]))
    G["tilt_servo_body"] = R(body); G["tilt_servo_spline"] = R(spline_r); G["tilt_servo_lead"] = R(lead)
    for side in (-1, 1):
        yaw, (x, y) = g.radar_transform(side)
        pcb, comps, hous = g.radar_ghost()
        T = lambda m: H(m.rotate([0, 0, yaw]).translate([x, y, 0]))
        nm = "L" if side < 0 else "R"
        G[f"radar{nm}_pcb"] = T(pcb); G[f"radar{nm}_comps"] = T(comps); G[f"radar{nm}_housing"] = T(hous)
    e = g.esp32cam_ghost()
    f = g.pod_in_head(tilt)
    for k in ("pcb", "module", "hdr", "pins", "housings", "bumps", "usb", "plugs", "cam", "parts"):
        G["esp_" + k] = H(f(e[k]))
    # power wires coming up the post: a Ø4 flexible pair up the bore, then out of the side slot
    G["post_wires"] = cyl_z(2.0, -3, P["head_under"] + P["head_t"] + P["hub_h"] + 6, seg=24)
    # a USB-C plug going up the bore (12 x 6.6) must also pass
    G["post_plug"] = box(-6.0, 6.0, -3.3, 3.3, -3, P["head_under"] + P["head_t"] + P["post_top_above"] - 2)
    return G

def cyl_z(r, z0, z1, x=0.0, y=0.0, seg=48):
    return g.cyl_z(r, z0, z1, x, y, seg)

def inter(a, b):
    return (a ^ b).volume()

def report_pairs(title, pairs, tol=1e-6):
    print(f"\n== {title}")
    bad = 0
    for name, a, b in pairs:
        v = inter(a, b)
        flag = "" if v < tol else "   <-- INTERFERENCE"
        if v >= tol: bad += 1
        print(f"  {name:44s} intersect = {v:9.3f} mm3{flag}")
    return bad

def overhangs(m, name, thresh=-0.72):
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
    print("== part sanity")
    W0 = world_parts(0.0)
    for k, m in W0.items():
        tm = to_trimesh(m)
        ok = m.status() == m.status().__class__.NoError and tm.is_watertight and m.volume() > 0
        print(f"  {k:6s} watertight={tm.is_watertight} genus={m.genus()} vol={m.volume():8.0f}")
        assert ok, k

    bad = 0
    bad += report_pairs("parts vs parts (pan 0, tilt 0)", [
        ("base ^ head (bushing + thrust collar)", W0["base"], W0["head"]),
        ("base ^ pinion (gear mesh + foot)", W0["base"], W0["pinion"]),
        ("head ^ pinion", W0["head"], W0["pinion"]),
        ("base ^ clip (sits in groove)", W0["base"], W0["clip"]),
        ("head ^ clip", W0["head"], W0["clip"]),
        ("head ^ pod", W0["head"], W0["pod"]),
        ("head ^ cover", W0["head"], W0["cover"]),
        ("pod ^ cover (lip in cavity)", W0["pod"], W0["cover"]),
        ("pin ^ head (hole 4.9 vs pin 4.8)", W0["pin"], W0["head"]),
        ("pin ^ pod (hole 5.2 vs pin 4.8)", W0["pin"], W0["pod"]),
        ("base ^ pod", W0["base"], W0["pod"]),
        ("pinion ^ pod", W0["pinion"], W0["pod"]),
    ] + ([("washer ^ base (on the ring)", W0["washer"], W0["base"]), ("washer ^ head", W0["washer"], W0["head"]), ("washer ^ pinion", W0["washer"], W0["pinion"])] if P["use_washer"] else []))
    # clip must sit just above the hub: report the gap
    hub_top = g.head_z() + P["hub_h"]; groove = P["head_under"] + P["head_t"] + P["clip_groove_above"]
    print(f"  hub top (world) = {hub_top:.2f}, clip groove bottom = {groove:.2f} -> clip clearance above the hub = {groove - hub_top:.2f} mm")
    # gear mesh + bushing over a full pan turn
    print("\n== pan sweep (gear mesh, bushing, pod/base) 0..360")
    worst = 0.0
    for pan in range(0, 360, 15):
        Wp = world_parts(0.0, pan)
        v1 = inter(Wp["base"], Wp["pinion"]); v2 = inter(Wp["base"], Wp["head"]); v3 = inter(Wp["base"], Wp["pod"]) + inter(Wp["base"], Wp["cover"])
        gap = Wp["pinion"].min_gap(Wp["base"], 5.0)
        print(f"  pan {pan:3d}: base^pinion = {v1:7.3f}  base^head = {v2:7.3f}  base^pod = {v3:7.3f}   pinion-gear min gap = {gap:5.2f} mm")
        worst = max(worst, v1, v2, v3)
    bad += worst > 1e-6

    G = ghosts(0.0)
    bad += report_pairs("components vs printed parts (tilt 0)", [
        ("pan servo body ^ head (window)", G["pan_servo_body"], W0["head"]),
        ("pan servo body ^ pod", G["pan_servo_body"], W0["pod"]),
        ("pan servo spline(4.6) ^ pinion", G["pan_servo_spline"], W0["pinion"]),
        ("pan servo spline(4.6) ^ head", G["pan_servo_spline"], W0["head"]),
        ("pan servo body ^ pinion (dome in counterbore)", G["pan_servo_body"], W0["pinion"]),
        ("pan servo body ^ base", G["pan_servo_body"], W0["base"]),
        ("pan servo lead ^ head", G["pan_servo_lead"], W0["head"]),
        ("post wires ^ base (bore)", G["post_wires"], W0["base"]),
        ("post wires ^ head (hub bore)", G["post_wires"], W0["head"]),
        ("post wires ^ clip", G["post_wires"], W0["clip"]),
        ("USB-C plug up the bore ^ base", G["post_plug"], W0["base"]),
        ("USB-C plug up the bore ^ head", G["post_plug"], W0["head"]),
        ("USB-C plug up the bore ^ clip", G["post_plug"], W0["clip"]),
        ("tilt servo body ^ head", G["tilt_servo_body"], W0["head"]),
        ("tilt servo body ^ pod", G["tilt_servo_body"], W0["pod"]),
        ("tilt servo spline(4.6) ^ pod", G["tilt_servo_spline"], W0["pod"]),
        ("tilt servo lead ^ head", G["tilt_servo_lead"], W0["head"]),
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
        ("esp usb receptacles ^ pod (locating blocks)", G["esp_usb"], W0["pod"]),
        ("esp pcb ^ pod (pegs through the holes)", G["esp_pcb"], W0["pod"]),
        ("esp usb receptacles ^ cover", G["esp_usb"], W0["cover"]),
        ("esp usb plugs ^ pod", G["esp_plugs"], W0["pod"]),
        ("esp usb plugs ^ head (tilt 0)", G["esp_plugs"], W0["head"]),
        ("esp camera ^ pod (barrel thru lens hole)", G["esp_cam"], W0["pod"]),
        ("esp top-side parts ^ pod", G["esp_parts"], W0["pod"]),
        ("esp pcb ^ cover posts", G["esp_pcb"], W0["cover"]),
        ("esp headers ^ cover", G["esp_hdr"], W0["cover"]),
        ("esp pins ^ cover", G["esp_pins"], W0["cover"]),
        ("esp module ^ cover", G["esp_module"], W0["cover"]),
    ])
    for nm, sp in (("pan", G["pan_servo_spline"]), ("tilt", G["tilt_servo_spline"])):
        bb = to_trimesh(sp).bounds
        print(f"  {nm} servo spline extent: {np.round(bb[0],2).tolist()} -> {np.round(bb[1],2).tolist()}")
    # spline engagement in the pinion socket: spline length inside the pinion's socket column
    pin_bb = to_trimesh(W0["pinion"]).bounds
    sp_bb = to_trimesh(G["pan_servo_spline"]).bounds
    print(f"  pinion z range {pin_bb[0][2]:.2f}..{pin_bb[1][2]:.2f} (socket 4.0 deep from the top); pan spline z {sp_bb[0][2]:.2f}..{sp_bb[1][2]:.2f} "
          f"-> engagement ~{min(pin_bb[1][2], sp_bb[1][2]) - max(pin_bb[1][2] - 4.0, sp_bb[0][2]):.2f} mm")

    print("\n== tilt sweep (pod+cover vs head/base/post/clip; USB plugs vs head). +tilt = camera looks DOWN")
    first_pod = None; first_plug_dn = None; first_plug_up = None
    for tilt in list(range(-90, 91, 5)):
        Wt = world_parts(float(tilt))
        Gt = ghosts(float(tilt))
        fixed = Wt["head"] + Wt["base"] + Wt["clip"] + Wt["pinion"]
        v_pod = inter(Wt["pod"], fixed) + inter(Wt["cover"], fixed)
        v_plug = inter(Gt["esp_plugs"], fixed)
        gap = (Wt["pod"] + Wt["cover"]).min_gap(Wt["base"] + Wt["clip"], 40.0)
        v_rad = sum(inter(Gt[k], Wt["pod"]) + inter(Gt[k], Wt["cover"]) for k in ("radarL_housing", "radarR_housing", "radarL_pcb", "radarR_pcb"))
        v_srv = sum(inter(Gt[k], Wt["pod"]) + inter(Gt[k], Wt["cover"]) for k in ("tilt_servo_body", "pan_servo_body"))
        v_wire = inter(Gt["post_wires"], Wt["pod"]) + inter(Gt["post_wires"], Wt["cover"])
        mark = ""
        if v_pod > 1e-6: mark += " POD-HITS-FIXED"; first_pod = first_pod if first_pod is not None else tilt
        if v_plug > 1e-6:
            mark += " PLUG-HITS"
            if tilt > 0 and first_plug_dn is None: first_plug_dn = tilt
            if tilt < 0: first_plug_up = tilt
        if v_rad > 1e-6: mark += " RADAR-HIT"
        if v_srv > 1e-6: mark += " SERVO-HIT"
        if v_wire > 1e-6: mark += " POST-WIRES-HIT"
        print(f"  tilt {tilt:4d}: pod-to-post/base gap = {gap:5.2f} mm   plug^fixed = {v_plug:7.1f} mm3{mark}")
    print(f"  -> pod never touches anything fixed: {first_pod is None};  USB plug clears down to +{(first_plug_dn or 95)-5} deg, up to {(first_plug_up or -95)+5} deg")
    bad += first_pod is not None

    base = g.part_base(); head = g.part_head(); pod = g.part_pod(); cover = g.part_cover(); pin, _ = g.part_pin()
    pinion, _ = g.part_pinion(); clip = g.part_clip()
    overhangs(base.translate([0, 0, P["base_t"]]), "base (plate down)")
    overhangs(head.translate([0, 0, P["head_t"]]), "head (plate down)")
    overhangs(pod.rotate([90, 0, 0]).translate([0, 0, P["pod_D"] / 2]), "pod (front face down)")
    overhangs(cover.rotate([-90, 0, 0]).translate([0, 0, P["pod_D"] / 2]), "cover (outer face down)")
    overhangs(pin, "pin (head down)")
    overhangs(pinion, "pinion (screw side down)")
    overhangs(clip, "clip (flat)")

    print("\nTOTAL interference flags:", bad)
    sys.exit(1 if bad else 0)
