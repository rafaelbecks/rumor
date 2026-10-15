"""Clean laser-cut plate of the alcantarilla: a disk with arc slots and round holes.

Outer diameter 400 mm. Seven-fold pattern measured from the photogrammetry.
Writes a GLB plate, an SVG, and a DXF (millimetres).
"""
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

# RUMOR monorepo: outputs live next to this script.
ROOT = Path(__file__).resolve().parent
GLB_PATH = ROOT / "alcantarilla-metal.glb"
SVG_PATH = ROOT / "alcantarilla-metal.svg"
DXF_PATH = ROOT / "alcantarilla-metal.dxf"
PREVIEW_PATH = Path("/tmp/alc_metal_preview.png")

# Millimetres. Outer diameter 400.
R = 200.0
THICK_MM = 3.0
N_SPOKES = 7
SPOKE_DEG = 12.0
# Slot rings (inner radius, outer radius), outside the hub.
SLOT_RINGS = [(70.0, 86.0), (100.0, 116.0), (130.0, 146.0), (160.0, 176.0)]
CENTER_HOLE_R = 9.0
SMALL_HOLE_R = 7.0
SMALL_HOLE_AT = 44.0
# One spoke points up. Small hub holes sit on the spoke centerlines.
SPOKE0 = np.pi / 2

PERIOD = 2 * np.pi / N_SPOKES
SPOKE = np.deg2rad(SPOKE_DEG)
SLOT = PERIOD - SPOKE


def spoke_angles():
    return SPOKE0 + np.arange(N_SPOKES) * PERIOD


def slot_angles():
    return spoke_angles() + PERIOD / 2


def circle(radius, n, phase=0.0):
    ang = phase + np.linspace(0, 2 * np.pi, n, endpoint=False)
    return np.stack([radius * np.cos(ang), radius * np.sin(ang)], axis=1)


def arc(radius, a0, a1, n):
    ang = np.linspace(a0, a1, n)
    return np.stack([radius * np.cos(ang), radius * np.sin(ang)], axis=1)


def slot_loop(r0, r1, a_center, n_arc=18, n_cap=10):
    """Rounded annular slot, centered on a_center. Ends are semicircular."""
    a0 = a_center - SLOT / 2
    a1 = a_center + SLOT / 2
    rm = 0.5 * (r0 + r1)
    cr = 0.5 * (r1 - r0)
    cap1 = np.array([rm * np.cos(a1), rm * np.sin(a1)])
    sweep1 = a1 + np.pi - np.linspace(0, np.pi, n_cap, endpoint=False)
    cap0 = np.array([rm * np.cos(a0), rm * np.sin(a0)])
    sweep0 = a0 - np.linspace(0, np.pi, n_cap, endpoint=False)
    return np.vstack([
        arc(r0, a0, a1, n_arc),
        cap1 + cr * np.stack([np.cos(sweep1), np.sin(sweep1)], axis=1),
        arc(r1, a1, a0, n_arc),
        cap0 + cr * np.stack([np.cos(sweep0), np.sin(sweep0)], axis=1),
    ])


def _in_loop(pt, loop):
    """Ray-cast point in polygon. Boundary counts as inside."""
    x, y = pt
    inside = False
    n = len(loop)
    j = n - 1
    for i in range(n):
        xi, yi = loop[i]
        xj, yj = loop[j]
        if ((yi > y) != (yj > y)) and (x < (xj - xi) * (y - yi) / (yj - yi + 1e-15) + xi):
            inside = not inside
        j = i
    return inside


def collect_plate(loops):
    """Top face from the cut contours. Holes stay open."""
    from scipy.spatial import Delaunay

    verts = np.vstack(loops)
    # A few interior samples keep the plate from becoming long sliver triangles.
    samples = []
    for rad in np.linspace(CENTER_HOLE_R + 6, R - 4, 18):
        for ang in np.linspace(0, 2 * np.pi, int(24 + rad / 4), endpoint=False):
            p = np.array([rad * np.cos(ang), rad * np.sin(ang)])
            if _in_loop(p, loops[0]) and not any(_in_loop(p, h) for h in loops[1:]):
                samples.append(p)
    if samples:
        verts = np.vstack([verts, np.array(samples)])
    faces = Delaunay(verts).simplices
    keep = []
    for a, b, c in faces:
        mid = (verts[a] + verts[b] + verts[c]) / 3
        if not _in_loop(mid, loops[0]):
            continue
        if any(_in_loop(mid, h) for h in loops[1:]):
            continue
        keep.append((int(a), int(b), int(c)))
    return verts, np.array(keep, dtype=np.int64)


def cut_loops():
    """Closed cut contours, each CCW in millimetres, y up."""
    loops = [circle(R, 180)]
    loops.append(circle(CENTER_HOLE_R, 48))
    for ang in spoke_angles():
        xy = np.array([SMALL_HOLE_AT * np.cos(ang), SMALL_HOLE_AT * np.sin(ang)])
        loops.append(circle(SMALL_HOLE_R, 36) + xy)
    for r0, r1 in SLOT_RINGS:
        for ang in slot_angles():
            loops.append(slot_loop(r0, r1, ang))
    return loops


def write_svg(loops, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="400mm" height="400mm" viewBox="-200 -200 400 400">',
        "<title>alcantarilla metal — 400 mm disk, cut on the lines</title>",
        '<g fill="none" stroke="#000" stroke-width="0.25" stroke-linejoin="round">',
    ]
    for loop in loops:
        d = "M " + " L ".join(f"{x:.3f} {-y:.3f}" for x, y in loop) + " Z"
        parts.append(f'<path d="{d}"/>')
    parts.append("</g></svg>")
    path.write_text("\n".join(parts) + "\n")


def write_dxf(loops, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    out = [
        "0", "SECTION", "2", "HEADER",
        "9", "$INSUNITS", "70", "4",
        "0", "ENDSEC",
        "0", "SECTION", "2", "ENTITIES",
    ]
    for loop in loops:
        out += ["0", "POLYLINE", "8", "CUT", "66", "1", "70", "1"]
        for x, y in loop:
            out += ["0", "VERTEX", "8", "CUT", "10", f"{x:.4f}", "20", f"{y:.4f}", "30", "0.0"]
        out += ["0", "SEQEND"]
    out += ["0", "ENDSEC", "0", "EOF"]
    path.write_text("\n".join(out) + "\n")


def write_glb(verts_mm, faces, path):
    scale = 0.001  # mm -> m
    thick = THICK_MM * scale
    top = np.column_stack([verts_mm[:, 0] * scale, verts_mm[:, 1] * scale, np.zeros(len(verts_mm))])
    bot = top.copy()
    bot[:, 2] = -thick
    n = len(top)
    pos = np.vstack([top, bot]).astype(np.float32)
    # bottom winding reversed so normals point down
    bot_faces = faces[:, ::-1] + n
    top_faces = faces
    # walls from boundary edges of the top
    from collections import Counter
    edges = Counter()
    for a, b, c in faces:
        for e in ((int(a), int(b)), (int(b), int(c)), (int(c), int(a))):
            edges[(min(e), max(e))] += 1
    wall = []
    for (a, b), count in edges.items():
        if count != 1:
            continue
        # Consistent outward wall: use the direction stored by walking the boundary.
        wall.append((a, b, b + n))
        wall.append((a, b + n, a + n))
    # Fix wall winding using the top face orientation: boundary edge a->b should match a face.
    directed = {}
    for a, b, c in faces:
        directed[(int(a), int(b))] = True
        directed[(int(b), int(c))] = True
        directed[(int(c), int(a))] = True
    wall = []
    seen = set()
    for (a, b) in list(directed):
        key = (min(a, b), max(a, b))
        if edges[key] != 1 or key in seen:
            continue
        seen.add(key)
        # a->b is CCW on the top, so the wall going downward is a, b, b+n / a, b+n, a+n
        # For an outward side when viewed from outside, the side face CCW is a, a+n, b if
        # the interior is to the left of a->b on the top. Exterior is to the right, so
        # looking from outside we want a, b, b+n? Let's use a, b+n, b which shows the side
        # with normal pointing right of a->b (outward) and downward component.
        wall.append((a, a + n, b + n))
        wall.append((a, b + n, b))
    faces_all = np.vstack([top_faces, bot_faces, np.array(wall, dtype=np.int64)])

    normals = np.zeros_like(pos, dtype=np.float64)
    f64 = faces_all.astype(np.int64)
    fn = np.cross(pos[f64[:, 1]] - pos[f64[:, 0]], pos[f64[:, 2]] - pos[f64[:, 0]])
    for k in range(3):
        np.add.at(normals, f64[:, k], fn)
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    normals = normals.astype(np.float32)

    uvs = np.zeros((len(pos), 2), dtype=np.float32)
    uvs[:, 0] = pos[:, 0] / (R * scale) * 0.5 + 0.5
    uvs[:, 1] = pos[:, 1] / (R * scale) * 0.5 + 0.5

    def pack(arr):
        return np.ascontiguousarray(arr).tobytes()

    idx = faces_all.astype(np.uint32)
    blobs = [pack(pos), pack(normals), pack(uvs), pack(idx)]
    bin_parts, views, cursor = [], [], 0
    for i, blob in enumerate(blobs):
        pad = (4 - cursor % 4) % 4
        if pad:
            bin_parts.append(b"\x00" * pad)
            cursor += pad
        views.append({"buffer": 0, "byteOffset": cursor, "byteLength": len(blob), "target": 34962 if i < 3 else 34963})
        bin_parts.append(blob)
        cursor += len(blob)
    bin_blob = b"".join(bin_parts)

    def acc(bv, comp, typ, count, mins=None, maxs=None):
        a = {"bufferView": bv, "componentType": comp, "count": int(count), "type": typ}
        if mins is not None:
            a["min"] = [float(x) for x in mins]
            a["max"] = [float(x) for x in maxs]
        return a

    gltf = {
        "asset": {"version": "2.0", "generator": "alcantarilla-metal"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{
            "name": "alcantarilla-metal",
            "mesh": 0,
            "rotation": [0.70710688829422, 0.0, 0.0, 0.7071066498756409],
        }],
        "materials": [{
            "name": "steel",
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.62, 0.63, 0.64, 1.0],
                "metallicFactor": 1.0,
                "roughnessFactor": 0.38,
            },
        }],
        "meshes": [{
            "name": "alcantarilla-metal",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3,
                "material": 0,
                "mode": 4,
            }],
        }],
        "accessors": [
            acc(0, 5126, "VEC3", len(pos), pos.min(0), pos.max(0)),
            acc(1, 5126, "VEC3", len(normals)),
            acc(2, 5126, "VEC2", len(uvs)),
            acc(3, 5125, "SCALAR", int(idx.size)),
        ],
        "bufferViews": views,
        "buffers": [{"byteLength": len(bin_blob)}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    bin_padded = bin_blob + b"\x00" * ((4 - len(bin_blob) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(bin_padded)
    glb = struct.pack("<4sII", b"glTF", 2, total)
    glb += struct.pack("<I", len(js)) + b"JSON" + js
    glb += struct.pack("<I", len(bin_padded)) + b"BIN\x00" + bin_padded
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(glb)
    print("glb", path, len(glb), "verts", len(pos), "tris", len(faces_all))


def preview(loops, path):
    S = 900
    img = Image.new("RGB", (S, S), (18, 18, 20))
    dr = ImageDraw.Draw(img)
    def xy(p):
        x = (p[0] / R * 0.46 + 0.5) * (S - 1)
        y = (-p[1] / R * 0.46 + 0.5) * (S - 1)
        return (x, y)
    # fill plate
    outer = [xy(p) for p in loops[0]]
    dr.polygon(outer, fill=(150, 154, 158))
    for loop in loops[1:]:
        dr.polygon([xy(p) for p in loop], fill=(18, 18, 20))
    for loop in loops:
        pts = [xy(p) for p in loop] + [xy(loop[0])]
        dr.line(pts, fill=(230, 230, 230), width=1)
    img.save(path)
    print("preview", path)


def main():
    loops = cut_loops()
    preview(loops, PREVIEW_PATH)
    write_svg(loops, SVG_PATH)
    write_dxf(loops, DXF_PATH)
    verts, faces = collect_plate(loops)
    oriented = []
    for a, b, c in faces:
        ab = verts[b] - verts[a]
        ac = verts[c] - verts[a]
        if ab[0] * ac[1] - ab[1] * ac[0] < 0:
            oriented.append((a, c, b))
        else:
            oriented.append((int(a), int(b), int(c)))
    faces = np.array(oriented, dtype=np.int64)
    write_glb(verts, faces, GLB_PATH)
    print("svg", SVG_PATH)
    print("dxf", DXF_PATH)
    print("loops", len(loops), "outer mm", 2 * R, "thickness mm", THICK_MM)


if __name__ == "__main__":
    main()
