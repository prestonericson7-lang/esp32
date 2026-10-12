#!/usr/bin/env python3
"""Software-rendered figures of the v2.1 pan/tilt head (no OpenGL needed).  Writes PNGs into out/.
Camera convention: az=0 looks from the FRONT (viewer at -Y), az=90 from the LEFT (-X), az=180 from the rear."""
import os, math, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import gen_pantilt_v2 as g
from gen_pantilt_v2 import P, to_trimesh
import check_v2 as c

OUT = g.OUT

def view_matrix(az, el):
    a, e = math.radians(az), math.radians(el)
    Rz = np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])
    Rx = np.array([[1, 0, 0], [0, math.cos(e), -math.sin(e)], [0, math.sin(e), math.cos(e)]])
    return Rx @ Rz

def render(items, out_png, az=35, el=25, size=(1600, 1200), labels=(), title="", margin=0.08, scale=None, center=None):
    W, H = size
    M = view_matrix(az, el)
    light = np.array([0.3, -0.5, 0.8]); light /= np.linalg.norm(light)
    allv = np.vstack([it[0].vertices for it in items])
    cam = allv @ M.T
    if center is None:
        lo, hi = cam.min(axis=0), cam.max(axis=0)
        cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    else:
        cc = np.array(center) @ M.T; cx, cz = cc[0], cc[2]
    if scale is None:
        lo, hi = cam.min(axis=0), cam.max(axis=0)
        ext = max(hi[0] - lo[0], (hi[2] - lo[2]) * W / H)
        scale = W * (1 - 2 * margin) / ext
    zbuf = np.full((H, W), np.inf)
    img = np.full((H, W, 3), 255, dtype=np.float32)
    for tm, col, alpha in items:
        v = tm.vertices @ M.T
        sx = (v[:, 0] - cx) * scale + W / 2
        sy = H / 2 - (v[:, 2] - cz) * scale
        depth = v[:, 1]
        n = tm.face_normals @ M.T
        shade = np.clip(np.abs(n @ light), 0, 1) * 0.75 + 0.25
        col = np.array(col, dtype=np.float32)
        F = tm.faces
        for fi in range(len(F)):
            i0, i1, i2 = F[fi]
            xs = np.array([sx[i0], sx[i1], sx[i2]]); ys = np.array([sy[i0], sy[i1], sy[i2]])
            x0, x1 = int(max(np.floor(xs.min()), 0)), int(min(np.ceil(xs.max()), W - 1))
            y0, y1 = int(max(np.floor(ys.min()), 0)), int(min(np.ceil(ys.max()), H - 1))
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            d = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
            if abs(d) < 1e-9:
                continue
            l1 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / d
            l2 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / d
            l3 = 1 - l1 - l2
            inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
            if not inside.any():
                continue
            dep = l1 * depth[i0] + l2 * depth[i1] + l3 * depth[i2]
            zb = zbuf[y0:y1 + 1, x0:x1 + 1]
            upd = inside & (dep < zb)
            if not upd.any():
                continue
            cpix = col * shade[fi]
            if alpha >= 1.0:
                zb[upd] = dep[upd]
                img[y0:y1 + 1, x0:x1 + 1][upd] = cpix
            else:
                sub = img[y0:y1 + 1, x0:x1 + 1]
                sub[upd] = sub[upd] * (1 - alpha) + cpix * alpha
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 22)
        fontb = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30)
    except Exception:
        font = ImageFont.load_default(); fontb = font
    if title:
        dr.text((20, 15), title, fill=(0, 0, 0), font=fontb)
    for text, pt3, off in labels:
        p = np.array(pt3) @ M.T
        x = (p[0] - cx) * scale + W / 2; y = H / 2 - (p[2] - cz) * scale
        tx, ty = x + off[0], y + off[1]
        dr.line([(x, y), (tx, ty)], fill=(200, 0, 0), width=2)
        dr.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(200, 0, 0))
        bb = dr.textbbox((tx, ty), text, font=font)
        dr.rectangle([bb[0] - 4, bb[1] - 2, bb[2] + 4, bb[3] + 2], fill=(255, 255, 255), outline=(200, 0, 0))
        dr.text((tx, ty), text, fill=(0, 0, 0), font=font)
    im.save(out_png)
    print("wrote", out_png)

COL = dict(base=(200, 200, 205), head=(120, 150, 215), pod=(230, 190, 90), cover=(240, 215, 140), pin=(90, 90, 90),
           pinion=(220, 120, 60), clip=(200, 60, 60),
           servo=(40, 40, 60), spline=(220, 220, 220), radar=(40, 90, 200), radar_hdr=(20, 20, 20), housing=(60, 60, 60),
           pcb=(30, 110, 50), module=(160, 160, 165), usb=(120, 120, 120), plug=(20, 20, 20), cam=(20, 20, 20), hdr=(25, 25, 25),
           wires=(200, 40, 40))

ALL = ("base", "head", "pod", "cover", "pin", "pinion", "clip")

def scene(tilt=0.0, pan=0.0, with_cover=True, with_ghosts=True, explode=0.0, parts=None):
    W = c.world_parts(tilt, pan)
    G = c.ghosts(tilt, pan) if with_ghosts else {}
    items = []
    def add(m, col, alpha=1.0, dz=0.0, dx=0.0, dy=0.0):
        tm = to_trimesh(m)
        if dz or dx or dy:
            tm.apply_translation([dx, dy, dz])
        items.append((tm, col, alpha))
    sel = parts or ALL
    e = explode
    if "base" in sel: add(W["base"], COL["base"])
    if "head" in sel: add(W["head"], COL["head"], dz=e * 1.0)
    if "pinion" in sel: add(W["pinion"], COL["pinion"], dz=e * 0.6)
    if "clip" in sel: add(W["clip"], COL["clip"], dz=e * 1.6)
    if "pod" in sel: add(W["pod"], COL["pod"], dz=e * 2.2)
    if "cover" in sel and with_cover: add(W["cover"], COL["cover"], dz=e * 2.2, dy=e * 1.2)
    if "pin" in sel: add(W["pin"], COL["pin"], dz=e * 2.2, dx=e * 0.6)
    if with_ghosts:
        add(G["pan_servo_body"], COL["servo"], dz=e * 1.4); add(G["pan_servo_spline"], COL["spline"], dz=e * 1.4)
        add(G["tilt_servo_body"], COL["servo"], dz=e * 1.0, dx=-e * 0.6); add(G["tilt_servo_spline"], COL["spline"], dz=e * 1.0, dx=-e * 0.6)
        for s in ("L", "R"):
            add(G[f"radar{s}_pcb"], COL["radar"], dz=e * 1.0); add(G[f"radar{s}_comps"], COL["radar_hdr"], dz=e * 1.0); add(G[f"radar{s}_housing"], COL["housing"], dz=e * 1.0)
        add(G["esp_pcb"], COL["pcb"], dz=e * 2.2); add(G["esp_module"], COL["module"], dz=e * 2.2)
        add(G["esp_hdr"], COL["hdr"], dz=e * 2.2); add(G["esp_pins"], COL["spline"], dz=e * 2.2)
        add(G["esp_usb"], COL["usb"], dz=e * 2.2); add(G["esp_cam"], COL["cam"], dz=e * 2.2)
        add(G["post_wires"], COL["wires"])
    return items

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    hz = g.head_z(); tz = P["tilt_z"] + hz
    render(scene(0, 0), os.path.join(OUT, "fig1_assembled_iso.png"), az=-35, el=25, title="v2.1 assembled, pan 0 / tilt 0, seen from front-right (lens faces you)",
           labels=[("ESP32-S3-CAM pod, board lies along the tilt axis", (0, -17.5, tz + 10), (-420, -160)),
                   ("LD2410C radar L, 45deg out, slides in from top", (-54, -32, hz + 10), (-380, 120)),
                   ("LD2410C radar R", (54, -32, hz + 10), (120, 120)),
                   ("MG90S tilt servo through the wall, ears outside", (-60, 0, tz - 10), (-300, 180)),
                   ("pin column + printed pivot pin", (50, 0, tz), (120, -140)),
                   ("hollow post: power wires come up through it", (0, 0, hz + 8), (-120, 200)),
                   ("base plate 100x80x4, 4x M4 holes", (45, 35, 0), (60, 120))])
    render(scene(0, 0), os.path.join(OUT, "fig2_assembled_rear.png"), az=145, el=25, title="rear-left view: pan servo on the head drives the 40T pinion round the fixed 20T gear; leads leave through the cover's back slots",
           labels=[("cover: 3 wire slots through the back face", (0, 17.5, tz - 7), (-300, -170)),
                   ("MG90S pan servo, shaft down through the plate", (5, 30, hz + 12), (120, -120)),
                   ("40T pinion under the plate", (0, 30, hz - 8), (160, 60)),
                   ("C-clip keeps the head on the post (ceiling mount)", (0, 0, hz + 9), (-380, 120))])
    render(scene(0, 0), os.path.join(OUT, "fig3_front.png"), az=0, el=0, title="front view (what a visitor sees): lens left of centre, radars at the plate corners")
    render(scene(45, 0), os.path.join(OUT, "fig4_side_tilt45.png"), az=-90, el=0, title="right side (pin column), tilt +45deg = camera looking down; pod clears post, plate and radar holders")
    render(scene(-45, 0), os.path.join(OUT, "fig4b_side_tilt-45.png"), az=-90, el=0, title="right side, tilt -45deg = camera looking up")
    render(scene(0, 0, explode=22), os.path.join(OUT, "fig5_exploded.png"), az=-35, el=22, title="exploded: base+post / pinion / head / clip / pod+board / cover / pin  (servos + radars as ghosts)")
    render(scene(0, 0, with_cover=False, parts=("pod",)), os.path.join(OUT, "fig6_pod_open_back.png"), az=180, el=0,
           title="pod from the back, cover off: board lies on its module against the front wall, header pins point at you")
    render(scene(0, 0, parts=("head", "pinion", "clip")), os.path.join(OUT, "fig7_head_top.png"), az=0, el=89, title="pan head from above (front = bottom of picture): servo wall left, pin column right, pan servo at the back, radars +/-45deg")
    render(scene(0, 0, parts=("base", "pinion", "clip"), with_ghosts=False), os.path.join(OUT, "fig8_base.png"), az=-30, el=35, title="base: hollow post with the fixed 20T gear, thrust collar, C-clip groove, side wire slot; pinion shown meshed")
    render(scene(0, 0, parts=("base", "head", "pinion", "clip"), with_ghosts=True), os.path.join(OUT, "fig9_head_underside.png"), az=160, el=-35,
           title="from below/behind: pinion on the pan servo meshing with the post gear; wires up the bore")
    render(scene(0, 180), os.path.join(OUT, "fig10_pan180.png"), az=-35, el=25, title="pan = 180deg (servo at 90 of its 180): the head has turned half way round the fixed post")
    print("done")
