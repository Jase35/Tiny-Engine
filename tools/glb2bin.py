"""Convert a GLB (or .gltf) file to the misha-petite package format (see src/common/io/package.hpp).

The output is either a raw binary package or a C++ header that embeds it as a byte array,
ready to be copied into the source tree and passed to package::load().

    py tools/glb2bin.py model.glb -o src/common/assets/model.hpp
    py tools/glb2bin.py model.glb -o model.mshp
    py tools/glb2bin.py model.glb -o model.hpp --name MODEL --max-texture-size 128

Requires Pillow for texture decoding (py -m pip install Pillow).

Conversion rules:
  * Every triangle primitive of every mesh node in the default scene becomes one package mesh.
  * Non-skinned meshes have their node's world transform baked into the vertices.
  * Skinned meshes stay in mesh space; each skin's joints become bones. Root joints get their
    full world transform as the local transform, so skinning matches the glTF result.
  * A material's baseColorTexture becomes the mesh texture, stored as RGB565 (alpha is dropped).
    Materials without one get a 1x1
    texture of their baseColorFactor. Only the first 4 bone influences (JOINTS_0/WEIGHTS_0) are used.
"""

import argparse
import base64
import io
import json
import math
import os
import re
import struct
import sys

MAGIC = b"MSHP"
VERSION = 2
NONE16 = 0xFFFF

COMPONENT_FORMATS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
COMPONENT_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
NORMALIZE_DIVISORS = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}


def warn(message):
    print(f"warning: {message}", file=sys.stderr)


# 8-bit channel -> nearest 5- or 6-bit value.
TO5 = [(v * 31 + 127) // 255 for v in range(256)]
TO6 = [(v * 63 + 127) // 255 for v in range(256)]


def pack_rgb565(pixels):
    """Packs an iterable of (r, g, b) 8-bit tuples as little-endian RGB565 (r << 11 | g << 5 | b)."""
    values = [(TO5[r] << 11) | (TO6[g] << 5) | TO5[b] for r, g, b in pixels]
    return struct.pack(f"<{len(values)}H", *values)


# --- Column-major 4x4 matrices stored as flat lists of 16 floats (m[col * 4 + row]). ---

IDENTITY = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def mat_mul(a, b):
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            out[c * 4 + r] = sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
    return out


def mat_from_trs(t, q, s):
    x, y, z, w = q
    rot = [
        1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
        2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
        2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y),
    ]
    m = [0.0] * 16
    for c in range(3):
        for r in range(3):
            m[c * 4 + r] = rot[c * 3 + r] * s[c]
    m[12], m[13], m[14], m[15] = t[0], t[1], t[2], 1.0
    return m


def transform_point(m, p):
    x, y, z = p
    out = [m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r] for r in range(4)]
    w = out[3] if out[3] != 0.0 else 1.0
    return [out[0] / w, out[1] / w, out[2] / w]


def normal_matrix(m):
    """Inverse-transpose of the upper 3x3 (as rows), for transforming normals."""
    a = [[m[c * 4 + r] for c in range(3)] for r in range(3)]  # a[row][col]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
           - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-12:
        return a
    # Cofactor matrix / det == inverse-transpose.
    cof = [[0.0] * 3 for _ in range(3)]
    for r in range(3):
        for c in range(3):
            rr = [i for i in range(3) if i != r]
            cc = [i for i in range(3) if i != c]
            minor = a[rr[0]][cc[0]] * a[rr[1]][cc[1]] - a[rr[0]][cc[1]] * a[rr[1]][cc[0]]
            cof[r][c] = (-1) ** (r + c) * minor / det
    return cof


def transform_normal(n3, n):
    out = [sum(n3[r][c] * n[c] for c in range(3)) for r in range(3)]
    length = math.sqrt(sum(v * v for v in out))
    return [v / length for v in out] if length > 0 else out


# --- glTF loading. ---

class Gltf:
    def __init__(self, path):
        self.dir = os.path.dirname(os.path.abspath(path))
        with open(path, "rb") as f:
            raw = f.read()

        self.glb_bin = None
        if raw[:4] == b"glTF":
            version, length = struct.unpack_from("<II", raw, 4)
            if version != 2:
                raise ValueError(f"unsupported GLB version {version}")
            offset, json_bytes = 12, None
            while offset + 8 <= min(length, len(raw)):
                chunk_len, chunk_type = struct.unpack_from("<II", raw, offset)
                chunk = raw[offset + 8: offset + 8 + chunk_len]
                if chunk_type == 0x4E4F534A:
                    json_bytes = chunk
                elif chunk_type == 0x004E4942 and self.glb_bin is None:
                    self.glb_bin = chunk
                offset += 8 + chunk_len
            if json_bytes is None:
                raise ValueError("GLB has no JSON chunk")
            self.json = json.loads(json_bytes.decode("utf-8"))
        else:
            self.json = json.loads(raw.decode("utf-8"))

        self.buffers = [self._load_buffer(i, b) for i, b in enumerate(self.json.get("buffers", []))]

    def _load_uri(self, uri):
        if uri.startswith("data:"):
            return base64.b64decode(uri.split(",", 1)[1])
        from urllib.parse import unquote
        with open(os.path.join(self.dir, unquote(uri)), "rb") as f:
            return f.read()

    def _load_buffer(self, index, buffer):
        if "uri" in buffer:
            return self._load_uri(buffer["uri"])
        if index == 0 and self.glb_bin is not None:
            return self.glb_bin
        raise ValueError(f"buffer {index} has no data")

    def buffer_view(self, index):
        view = self.json["bufferViews"][index]
        return self.buffers[view["buffer"]], view.get("byteOffset", 0), view.get("byteLength", 0), view.get("byteStride")

    def accessor(self, index):
        """Returns a list of tuples (one per element), with normalization applied."""
        acc = self.json["accessors"][index]
        ctype, count = acc["componentType"], acc["count"]
        ncomp = COMPONENT_COUNTS[acc["type"]]
        fmt = COMPONENT_FORMATS[ctype]
        csize = struct.calcsize(fmt)
        elem_fmt = "<" + fmt * ncomp

        if "bufferView" in acc:
            data, start, _, stride = self.buffer_view(acc["bufferView"])
            start += acc.get("byteOffset", 0)
            stride = stride or csize * ncomp
            values = [struct.unpack_from(elem_fmt, data, start + i * stride) for i in range(count)]
        else:
            values = [(0,) * ncomp] * count

        sparse = acc.get("sparse")
        if sparse:
            idx = sparse["indices"]
            idata, istart, _, _ = self.buffer_view(idx["bufferView"])
            ifmt = "<" + COMPONENT_FORMATS[idx["componentType"]]
            istart += idx.get("byteOffset", 0)
            isize = struct.calcsize(ifmt)
            vals = sparse["values"]
            vdata, vstart, _, _ = self.buffer_view(vals["bufferView"])
            vstart += vals.get("byteOffset", 0)
            for i in range(sparse["count"]):
                target = struct.unpack_from(ifmt, idata, istart + i * isize)[0]
                values[target] = struct.unpack_from(elem_fmt, vdata, vstart + i * csize * ncomp)

        if acc.get("normalized") and ctype in NORMALIZE_DIVISORS:
            d = NORMALIZE_DIVISORS[ctype]
            values = [tuple(max(v / d, -1.0) for v in e) for e in values]
        return values

    def image_bytes(self, image_index):
        image = self.json["images"][image_index]
        if "bufferView" in image:
            data, start, length, _ = self.buffer_view(image["bufferView"])
            return data[start:start + length]
        return self._load_uri(image["uri"])


def node_local_matrix(node):
    if "matrix" in node:
        return [float(v) for v in node["matrix"]]
    return mat_from_trs(node.get("translation", [0, 0, 0]),
                        node.get("rotation", [0, 0, 0, 1]),
                        node.get("scale", [1, 1, 1]))


# --- Conversion. ---

class Converter:
    def __init__(self, gltf, max_texture_size):
        self.gltf = gltf
        self.max_texture_size = max_texture_size
        self.textures = []        # (width, height, little-endian RGB565 pixel bytes)
        self.texture_cache = {}   # key -> file texture index
        self.bones = []           # (parent, local[16], inverseBind[16])
        self.skin_bones = {}      # skin index -> list mapping joint slot -> file bone index
        self.meshes = []          # (texture, vertices, indices)

        nodes = gltf.json.get("nodes", [])
        self.parents = [None] * len(nodes)
        for i, node in enumerate(nodes):
            for child in node.get("children", []):
                self.parents[child] = i
        self.locals = [node_local_matrix(n) for n in nodes]
        self.worlds = [None] * len(nodes)

    def world(self, node):
        if self.worlds[node] is None:
            parent = self.parents[node]
            local = self.locals[node]
            self.worlds[node] = local if parent is None else mat_mul(self.world(parent), local)
        return self.worlds[node]

    def scene_nodes(self):
        j = self.gltf.json
        scenes = j.get("scenes", [])
        if scenes:
            roots = scenes[j.get("scene", 0)].get("nodes", [])
        else:
            roots = [i for i, p in enumerate(self.parents) if p is None]
        order, stack = [], list(reversed(roots))
        while stack:
            n = stack.pop()
            order.append(n)
            stack.extend(reversed(j["nodes"][n].get("children", [])))
        return order

    # Textures.

    def add_texture(self, key, width, height, rgb565):
        if key in self.texture_cache:
            return self.texture_cache[key]
        if len(self.textures) >= NONE16:
            raise ValueError("too many textures")
        self.textures.append((width, height, rgb565))
        self.texture_cache[key] = len(self.textures) - 1
        return self.texture_cache[key]

    def color_texture(self, factor):
        rgb = tuple(max(0, min(255, round(c * 255))) for c in factor[:3])
        return self.add_texture(("color", rgb), 1, 1, pack_rgb565([rgb]))

    def image_texture(self, image_index):
        key = ("image", image_index)
        if key in self.texture_cache:
            return self.texture_cache[key]
        try:
            from PIL import Image
        except ImportError:
            sys.exit("error: Pillow is required to decode textures (py -m pip install Pillow)")
        image = Image.open(io.BytesIO(self.gltf.image_bytes(image_index))).convert("RGB")
        if self.max_texture_size and max(image.size) > self.max_texture_size:
            image.thumbnail((self.max_texture_size, self.max_texture_size), Image.LANCZOS)
        width, height = image.size
        if width > NONE16 or height > NONE16:
            raise ValueError(f"image {image_index} is too large ({width}x{height})")
        raw = image.tobytes()
        pixels = zip(raw[0::3], raw[1::3], raw[2::3])
        return self.add_texture(key, width, height, pack_rgb565(pixels))

    def material_texture(self, material_index):
        if material_index is None:
            return self.color_texture((1.0, 1.0, 1.0, 1.0))
        material = self.gltf.json["materials"][material_index]
        pbr = material.get("pbrMetallicRoughness", {})
        tex_info = pbr.get("baseColorTexture")
        if tex_info is not None:
            if tex_info.get("texCoord", 0) != 0:
                warn(f"material {material_index}: baseColorTexture uses TEXCOORD_{tex_info['texCoord']}, using TEXCOORD_0")
            texture = self.gltf.json["textures"][tex_info["index"]]
            if "source" in texture:
                return self.image_texture(texture["source"])
            warn(f"material {material_index}: texture has no standard image source, using baseColorFactor")
        return self.color_texture(pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0]))

    # Bones.

    def skin_bone_map(self, skin_index):
        if skin_index in self.skin_bones:
            return self.skin_bones[skin_index]
        skin = self.gltf.json["skins"][skin_index]
        joints = skin["joints"]
        joint_set = set(joints)

        ibms = [IDENTITY] * len(joints)
        if "inverseBindMatrices" in skin:
            ibms = [list(m) for m in self.gltf.accessor(skin["inverseBindMatrices"])]

        def joint_parent(node):
            p = self.parents[node]
            while p is not None and p not in joint_set:
                p = self.parents[p]
            return p

        def depth(node):
            d, p = 0, joint_parent(node)
            while p is not None:
                d, p = d + 1, joint_parent(p)
            return d

        # Parents must precede children in the file.
        order = sorted(range(len(joints)), key=lambda slot: depth(joints[slot]))
        node_to_bone, slot_to_bone = {}, [NONE16] * len(joints)
        for slot in order:
            node = joints[slot]
            parent_node = joint_parent(node)
            if parent_node is None:
                parent, local = NONE16, self.world(node)
            else:
                parent = node_to_bone[parent_node]
                # Fold any non-joint nodes between the two joints into the local transform.
                local, p = self.locals[node], self.parents[node]
                while p != parent_node:
                    local, p = mat_mul(self.locals[p], local), self.parents[p]
            if len(self.bones) >= NONE16:
                raise ValueError("too many bones")
            self.bones.append((parent, local, ibms[slot]))
            node_to_bone[node] = slot_to_bone[slot] = len(self.bones) - 1

        self.skin_bones[skin_index] = slot_to_bone
        return slot_to_bone

    # Meshes.

    def add_primitive(self, node_index, prim, skin_index):
        g = self.gltf
        mode = prim.get("mode", 4)
        if mode != 4:
            warn(f"node {node_index}: skipping non-triangle-list primitive (mode {mode})")
            return
        attrs = prim["attributes"]
        if "POSITION" not in attrs:
            warn(f"node {node_index}: skipping primitive without POSITION")
            return
        if "JOINTS_1" in attrs:
            warn(f"node {node_index}: only the first 4 bone influences are used")

        positions = g.accessor(attrs["POSITION"])
        count = len(positions)
        normals = g.accessor(attrs["NORMAL"]) if "NORMAL" in attrs else [(0.0, 0.0, 0.0)] * count
        uvs = g.accessor(attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else [(0.0, 0.0)] * count

        skinned = skin_index is not None and "JOINTS_0" in attrs and "WEIGHTS_0" in attrs
        if skinned:
            slot_to_bone = self.skin_bone_map(skin_index)
            joints = g.accessor(attrs["JOINTS_0"])
            weights = g.accessor(attrs["WEIGHTS_0"])
        else:
            world = self.world(node_index)
            nmat = normal_matrix(world)

        vertices = []
        for i in range(count):
            pos, nrm = list(positions[i]), list(normals[i])
            bone_ids, bone_weights = [NONE16] * 4, [0.0] * 4
            if skinned:
                for k in range(4):
                    w = float(weights[i][k])
                    if w > 0.0:
                        bone_ids[k] = slot_to_bone[int(joints[i][k])]
                        bone_weights[k] = w
            else:
                pos = transform_point(world, pos)
                if any(nrm):
                    nrm = transform_normal(nmat, nrm)
            vertices.append((pos, nrm, bone_ids, bone_weights, uvs[i][:2]))

        if "indices" in prim:
            indices = [int(e[0]) for e in g.accessor(prim["indices"])]
        else:
            indices = list(range(count))
        indices = indices[: len(indices) - len(indices) % 3]

        if len(self.meshes) >= NONE16:
            raise ValueError("too many meshes")
        texture = self.material_texture(prim.get("material"))
        self.meshes.append((texture, vertices, indices))

    def convert(self):
        j = self.gltf.json
        for node_index in self.scene_nodes():
            node = j["nodes"][node_index]
            if "mesh" not in node:
                continue
            for prim in j["meshes"][node["mesh"]]["primitives"]:
                self.add_primitive(node_index, prim, node.get("skin"))
        return self.serialize()

    def serialize(self):
        out = bytearray()
        out += MAGIC
        out += struct.pack("<HHHH", VERSION, len(self.textures), len(self.bones), len(self.meshes))
        for width, height, rgb565 in self.textures:
            out += struct.pack("<HH", width, height)
            out += rgb565
        for parent, local, ibm in self.bones:
            out += struct.pack("<H16f16f", parent, *local, *ibm)
        for texture, vertices, indices in self.meshes:
            out += struct.pack("<HII", texture, len(vertices), len(indices))
            for pos, nrm, ids, ws, uv in vertices:
                out += struct.pack("<3f3f4H4f2f", *pos, *nrm, *ids, *ws, *uv)
            out += struct.pack(f"<{len(indices)}I", *indices)
        return bytes(out)


def to_header(data, name, source):
    lines = [
        f"// Generated by tools/glb2bin.py from {os.path.basename(source)}. Do not edit.",
        f"// Load with: package::load({name}, {name}_SIZE);",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        f"alignas(4) inline constexpr uint8_t {name}[] = {{",
    ]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    lines += ["};", f"inline constexpr size_t {name}_SIZE = sizeof({name});", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Convert GLB/glTF to a misha-petite package.")
    parser.add_argument("input", help="input .glb or .gltf file")
    parser.add_argument("-o", "--output", required=True,
                        help="output path; .h/.hpp writes a C++ header, anything else raw binary")
    parser.add_argument("--name", help="array name in the header (default: derived from the input name)")
    parser.add_argument("--max-texture-size", type=int, default=0,
                        help="downscale textures so neither side exceeds this (0 = keep)")
    args = parser.parse_args()

    data = Converter(Gltf(args.input), args.max_texture_size).convert()

    out_dir = os.path.dirname(args.output)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    if os.path.splitext(args.output)[1].lower() in (".h", ".hpp"):
        name = args.name or re.sub(r"\W", "_", os.path.splitext(os.path.basename(args.input))[0]).upper()
        if name[0].isdigit():
            name = "_" + name
        with open(args.output, "w", newline="\n") as f:
            f.write(to_header(data, name, args.input))
    else:
        with open(args.output, "wb") as f:
            f.write(data)

    textures, bones, meshes = struct.unpack_from("<HHH", data, 6)
    print(f"{args.output}: {len(data)} bytes, {textures} textures, {bones} bones, {meshes} meshes")


if __name__ == "__main__":
    main()
