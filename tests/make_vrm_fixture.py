"""Generate an original, four-vertex VRM 0.x fixture; no third-party artwork."""
import json
import pathlib
import struct
import sys
import zlib

binary = bytearray()
views = []
accessors = []


def view(data):
    while len(binary) % 4:
        binary.append(0)
    i = len(views)
    views.append(dict(buffer=0, byteOffset=len(binary), byteLength=len(data)))
    binary.extend(data)
    return i


def accessor(values, fmt, kind, component):
    i = len(accessors)
    accessors.append(dict(bufferView=view(struct.pack('<' + fmt * len(values), *values)),
                          componentType=component, count=len(values) // dict(VEC2=2, VEC3=3, VEC4=4, MAT4=16, SCALAR=1)[kind], type=kind))
    return i


pos = accessor([-.3, 0, 0, .3, 0, 0, -.3, 1, 0, .3, 1, 0], 'f', 'VEC3', 5126)
normal = accessor([0, 0, 1] * 4, 'f', 'VEC3', 5126)
uv = accessor([0, 1, 1, 1, 0, 0, 1, 0], 'f', 'VEC2', 5126)
joints = accessor([0, 0, 0, 0] * 4, 'H', 'VEC4', 5123)
weights = accessor([1, 0, 0, 0] * 4, 'f', 'VEC4', 5126)
indices = accessor([0, 2, 1, 1, 2, 3], 'H', 'SCALAR', 5123)
bind = accessor([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 'f', 'MAT4', 5126)
morph = accessor([0, .1, 0] * 4, 'f', 'VEC3', 5126)


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(b'\x00' + bytes([255, 100, 180, 255, 100, 220, 255, 255]) + b'\x00' + bytes([100, 220, 255, 255, 255, 100, 180, 255])))
png += chunk(b'IEND', b'')
image = view(png)
j = dict(asset=dict(version='2.0'), scene=0, scenes=[dict(nodes=[0])],
         nodes=[dict(children=[1, 2], translation=[0, .5, 0]), dict(mesh=0, skin=0), dict(translation=[0, .2, 0])],
         meshes=[dict(weights=[1], primitives=[dict(attributes=dict(POSITION=pos, NORMAL=normal, TEXCOORD_0=uv, JOINTS_0=joints, WEIGHTS_0=weights), indices=indices, material=0, targets=[dict(POSITION=morph)])])],
         skins=[dict(joints=[2], inverseBindMatrices=bind)],
         materials=[dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0))],
         textures=[dict(source=0)], images=[dict(bufferView=image, mimeType='image/png')],
         accessors=accessors, bufferViews=views, buffers=[dict(byteLength=len(binary))],
         extensionsUsed=['VRM'], extensions=dict(VRM=dict(specVersion='0.0', meta=dict(title='Original VRM test card'))))
document = json.dumps(j, separators=(',', ':')).encode()
document += b' ' * (-len(document) % 4)
binary += b'\0' * (-len(binary) % 4)
glb = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(document) + 8 + len(binary))
glb += struct.pack('<II', len(document), 0x4E4F534A) + document
glb += struct.pack('<II', len(binary), 0x004E4942) + binary
destination = pathlib.Path(sys.argv[1])
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_bytes(glb)
print(f'VRM fixture: {len(glb)} bytes / 4 vertices')
