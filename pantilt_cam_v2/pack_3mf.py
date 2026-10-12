#!/usr/bin/env python3
"""Write a Bambu Studio style 3MF (two A1 mini plates) from the generated parts.
Structure mirrors a 3MF saved by Bambu Studio: 3D/3dmodel.model with one component object per
part referencing 3D/Objects/object_N.model, plus Metadata/model_settings.config with the plate
assignment.  Plate 2 is offset by plate_width * 1.2 in X (Bambu's LOGICAL_PART_PLATE_GAP = 1/5)."""
import os, sys, uuid, zipfile, datetime
import numpy as np
import gen_pantilt_v2 as g
from gen_pantilt_v2 import P, to_trimesh

OUT = g.OUT
PLATE = 180.0                 # A1 mini
STRIDE = PLATE * 1.2          # Bambu plate stride

def print_oriented():
    """parts in PRINT orientation, centred on XY, resting on z=0.  Returns list of (name, trimesh)."""
    base = g.part_base().translate([0, 0, P["base_t"]])
    head = g.part_head().translate([0, 0, P["head_t"]])
    pod = g.part_pod().rotate([90, 0, 0]).translate([0, 0, P["pod_D"] / 2])          # front face down
    cover = g.part_cover().rotate([-90, 0, 0]).translate([0, 0, P["pod_D"] / 2])    # outer face down
    pin, _ = g.part_pin()
    pinion, _ = g.part_pinion()
    clip = g.part_clip()
    out = []
    for name, m in (("1 Base plate + hollow post", base), ("2 Pan head (yoke + radar holders)", head),
                    ("3 Camera pod (lens face down)", pod), ("4 Pod back cover (outer face down)", cover),
                    ("5 Pivot pin (print 2 spares)", pin), ("6 Pan pinion 40T", pinion), ("7 Post C-clip (print 2 spares)", clip)):
        tm = to_trimesh(m)
        lo, hi = tm.bounds
        tm.apply_translation([-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2]])
        assert abs(tm.bounds[0][2]) < 1e-6
        out.append((name, tm))
    return out

# plate layout: (plate index, x, y) of each object's centre on its plate
LAYOUT = {
    "1 Base plate + hollow post": (1, 52.0, 42.0),
    "3 Camera pod (lens face down)": (1, 125.0, 144.0),
    "4 Pod back cover (outer face down)": (1, 125.0, 104.0),
    "6 Pan pinion 40T": (1, 132.0, 50.0),
    "5 Pivot pin (print 2 spares)": (1, 165.0, 72.0),
    "7 Post C-clip (print 2 spares)": (1, 167.0, 30.0),
    "2 Pan head (yoke + radar holders)": (2, 90.0, 95.0),
}
PLATE_NAMES = {1: "Plate 1 - base, pod, cover, pinion, pin, clip", 2: "Plate 2 - pan head"}

def mesh_xml(obj_id, tm):
    v = tm.vertices; f = tm.faces
    parts = ['<?xml version="1.0" encoding="UTF-8"?>\n',
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p">\n',
             ' <metadata name="BambuStudio:3mfVersion">1</metadata>\n <resources>\n',
             f'  <object id="{obj_id}" p:UUID="{uuid.uuid4()}" type="model">\n   <mesh>\n    <vertices>\n']
    parts += [f'     <vertex x="{x:.6g}" y="{y:.6g}" z="{z:.6g}"/>\n' for x, y, z in v]
    parts.append('    </vertices>\n    <triangles>\n')
    parts += [f'     <triangle v1="{a}" v2="{b}" v3="{c}"/>\n' for a, b, c in f]
    parts.append('    </triangles>\n   </mesh>\n  </object>\n </resources>\n <build/>\n</model>\n')
    return "".join(parts)

def write_3mf(path, objs):
    today = datetime.date.today().isoformat()
    model = ['<?xml version="1.0" encoding="UTF-8"?>\n',
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p">\n',
             ' <metadata name="Application">BambuStudio-01.10.01.50</metadata>\n',
             ' <metadata name="BambuStudio:3mfVersion">1</metadata>\n',
             ' <metadata name="Copyright"></metadata>\n',
             f' <metadata name="CreationDate">{today}</metadata>\n',
             ' <metadata name="Description">v2.1 pan/tilt: ESP32-S3-CAM pod, 2x MG90S, 2x LD2410C on the head, hollow pan post with 2:1 gear drive.</metadata>\n',
             ' <metadata name="Designer"></metadata>\n <metadata name="DesignerCover"></metadata>\n <metadata name="DesignerUserId"></metadata>\n <metadata name="License"></metadata>\n',
             f' <metadata name="ModificationDate">{today}</metadata>\n',
             ' <metadata name="Origin"></metadata>\n <metadata name="Title">pantilt_cam_v2</metadata>\n <resources>\n']
    build = []
    rels = ['<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n']
    settings = ['<?xml version="1.0" encoding="UTF-8"?>\n<config>\n']
    plates = {1: [], 2: []}
    assemble = []
    files = {}
    for i, (name, tm) in enumerate(objs):
        mesh_id = 2 * i + 1
        comp_id = 2 * i + 2
        files[f"3D/Objects/object_{mesh_id}.model"] = mesh_xml(mesh_id, tm)
        model.append(f'  <object id="{comp_id}" p:UUID="{uuid.uuid4()}" type="model">\n   <components>\n'
                     f'    <component p:path="/3D/Objects/object_{mesh_id}.model" objectid="{mesh_id}" p:UUID="{uuid.uuid4()}" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>\n'
                     '   </components>\n  </object>\n')
        plate, px, py = LAYOUT[name]
        wx = px + (plate - 1) * STRIDE; wy = py
        tr = f"1 0 0 0 1 0 0 0 1 {wx:g} {wy:g} 0"
        build.append(f'  <item objectid="{comp_id}" p:UUID="{uuid.uuid4()}" transform="{tr}" printable="1"/>\n')
        rels.append(f' <Relationship Target="/3D/Objects/object_{mesh_id}.model" Id="rel-{i+1}" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n')
        hz = tm.bounds[1][2]
        settings.append(f'  <object id="{comp_id}">\n    <metadata key="name" value="{name}"/>\n    <metadata key="extruder" value="1"/>\n'
                        f'    <part id="{mesh_id}" subtype="normal_part">\n      <metadata key="name" value="{name}"/>\n'
                        '      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n'
                        f'      <metadata key="source_file" value="{name.split(" ")[1]}.stl"/>\n      <metadata key="source_object_id" value="0"/>\n      <metadata key="source_volume_id" value="0"/>\n'
                        f'      <metadata key="source_offset_x" value="{wx:g}"/>\n      <metadata key="source_offset_y" value="{wy:g}"/>\n      <metadata key="source_offset_z" value="{hz/2:g}"/>\n'
                        '      <mesh_stat edges_fixed="0" degenerate_facets="0" facets_removed="0" facets_reversed="0" backwards_edges="0"/>\n    </part>\n  </object>\n')
        plates[plate].append((comp_id, 100 + comp_id))
        assemble.append(f'   <assemble_item object_id="{comp_id}" instance_id="0" transform="{tr}" offset="0 0 0" />\n')
    model.append(' </resources>\n <build p:UUID="' + str(uuid.uuid4()) + '">\n' + "".join(build) + ' </build>\n</model>\n')
    rels.append('</Relationships>\n')
    for pl in (1, 2):
        settings.append(f'  <plate>\n    <metadata key="plater_id" value="{pl}"/>\n    <metadata key="plater_name" value="{PLATE_NAMES[pl]}"/>\n'
                        '    <metadata key="locked" value="false"/>\n    <metadata key="filament_map_mode" value="Auto For Flush"/>\n    <metadata key="filament_maps" value="1"/>\n')
        for cid, iid in plates[pl]:
            settings.append(f'    <model_instance>\n      <metadata key="object_id" value="{cid}"/>\n      <metadata key="instance_id" value="0"/>\n      <metadata key="identify_id" value="{iid}"/>\n    </model_instance>\n')
        settings.append('  </plate>\n')
    settings.append('  <assemble>\n' + "".join(assemble) + '  </assemble>\n</config>\n')
    files["3D/3dmodel.model"] = "".join(model)
    files["3D/_rels/3dmodel.model.rels"] = "".join(rels)
    files["Metadata/model_settings.config"] = "".join(settings)
    files["[Content_Types].xml"] = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
                                    ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
                                    ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
                                    ' <Default Extension="png" ContentType="image/png"/>\n <Default Extension="config" ContentType="application/xml"/>\n</Types>\n')
    files["_rels/.rels"] = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
                            ' <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')
    order = ["[Content_Types].xml", "_rels/.rels", "3D/3dmodel.model", "3D/_rels/3dmodel.model.rels"] + \
            [k for k in files if k.startswith("3D/Objects/")] + ["Metadata/model_settings.config"]
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for k in order:
            z.writestr(k, files[k])
    print("wrote", path)

def write_plain_3mf(path, objs):
    """generic (non-Bambu) 3MF with everything on one build for other slicers"""
    import trimesh
    sc = trimesh.Scene()
    for name, tm in objs:
        plate, px, py = LAYOUT[name]
        t = tm.copy(); t.apply_translation([px + (plate - 1) * STRIDE, py, 0])
        sc.add_geometry(t, node_name=name, geom_name=name)
    sc.export(path)
    print("wrote", path)

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith("_print_oriented.stl"):
            os.remove(os.path.join(OUT, f))
    objs = print_oriented()
    for name, tm in objs:
        plate, px, py = LAYOUT[name]
        lo, hi = tm.bounds
        x0, x1, y0, y1 = px + lo[0], px + hi[0], py + lo[1], py + hi[1]
        ok = 0 <= x0 and x1 <= PLATE and 0 <= y0 and y1 <= PLATE
        print(f"plate {plate}  {name:38s} footprint x[{x0:6.1f},{x1:6.1f}] y[{y0:6.1f},{y1:6.1f}] h={hi[2]:5.1f}  {'OK' if ok else 'OUTSIDE BED!'}")
        assert ok
        tm.export(os.path.join(OUT, name.split(" ")[1].lower() + "_print_oriented.stl"))
    for i in range(len(objs)):
        for j in range(i + 1, len(objs)):
            (n1, t1), (n2, t2) = objs[i], objs[j]
            p1, p2 = LAYOUT[n1], LAYOUT[n2]
            if p1[0] != p2[0]:
                continue
            a = t1.bounds + np.array([p1[1], p1[2], 0]); b = t2.bounds + np.array([p2[1], p2[2], 0])
            sep = a[1][0] + 3 < b[0][0] or b[1][0] + 3 < a[0][0] or a[1][1] + 3 < b[0][1] or b[1][1] + 3 < a[0][1]
            assert sep, f"footprints overlap/too close: {n1} / {n2}"
    write_3mf(os.path.join(OUT, "pantilt_cam_v2_A1mini.3mf"), objs)
    write_plain_3mf(os.path.join(OUT, "pantilt_cam_v2_generic.3mf"), objs)
