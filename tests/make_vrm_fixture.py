"""Generate an original skinned VRM test figure; no third-party artwork."""
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


positions, joint_values, index_values = [], [], []


def quad(x0, y0, x1, y1, bone):
    start = len(positions) // 3
    positions.extend([x0, y0, 0, x1, y0, 0, x0, y1, 0, x1, y1, 0])
    joint_values.extend([bone, 0, 0, 0] * 4)
    index_values.extend([start, start+2, start+1, start+1, start+2, start+3])


quad(-.24, .65, .24, 1.15, 0)  # torso
quad(-.18, 1.15, .18, 1.50, 9)  # head
quad(-.70, 1.02, -.24, 1.15, 1)  # left arm, exported in T pose
quad(.24, 1.02, .70, 1.15, 3)  # right arm
quad(-.23, 0, -.03, .65, 5)  # left leg
quad(.03, 0, .23, .65, 7)  # right leg
vertex_count = len(positions) // 3
pos = accessor(positions, 'f', 'VEC3', 5126)
normal = accessor([0, 0, -1] * vertex_count, 'f', 'VEC3', 5126)
uv = accessor([0, 1, 1, 1, 0, 0, 1, 0] * 6, 'f', 'VEC2', 5126)
joints = accessor(joint_values, 'H', 'VEC4', 5123)
weights = accessor([1, 0, 0, 0] * vertex_count, 'f', 'VEC4', 5126)
indices = accessor(index_values, 'H', 'SCALAR', 5123)
rest_positions = [(0, .65), (-.24, 1.08), (-.48, 1.08), (.24, 1.08), (.48, 1.08),
                  (-.13, .65), (-.13, .30), (.13, .65), (.13, .30), (0, 1.25)]
inverse_values = []
for x, y in rest_positions:
    inverse_values.extend([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, 0, 1])
bind = accessor(inverse_values, 'f', 'MAT4', 5126)
morph = accessor([0, .01, 0] * vertex_count, 'f', 'VEC3', 5126)


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(b'\x00' + bytes([255, 100, 180, 255, 100, 220, 255, 255]) + b'\x00' + bytes([100, 220, 255, 255, 255, 100, 180, 255])))
png += chunk(b'IEND', b'')
image = view(png)
j = dict(asset=dict(version='2.0'), scene=0, scenes=[dict(nodes=[0])],
         nodes=[dict(children=[1, 2]), dict(mesh=0, skin=0),
                dict(name='Hips', translation=[0, .65, 0], children=[3, 5, 7, 9, 11]),
                dict(name='UpperArm.L', translation=[-.24, .43, 0], children=[4]),
                dict(name='LowerArm.L', translation=[-.24, 0, 0]),
                dict(name='UpperArm.R', translation=[.24, .43, 0], children=[6]),
                dict(name='LowerArm.R', translation=[.24, 0, 0]),
                dict(name='UpperLeg.L', translation=[-.13, 0, 0], children=[8]),
                dict(name='LowerLeg.L', translation=[0, -.35, 0]),
                dict(name='UpperLeg.R', translation=[.13, 0, 0], children=[10]),
                dict(name='LowerLeg.R', translation=[0, -.35, 0]),
                dict(name='Head', translation=[0, .60, 0])],
         meshes=[dict(weights=[1], primitives=[dict(attributes=dict(POSITION=pos, NORMAL=normal, TEXCOORD_0=uv, JOINTS_0=joints, WEIGHTS_0=weights), indices=indices, material=0, targets=[dict(POSITION=morph)])])],
         skins=[dict(joints=list(range(2, 12)), inverseBindMatrices=bind)],
         materials=[dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0))],
         textures=[dict(source=0)], images=[dict(bufferView=image, mimeType='image/png')],
         accessors=accessors, bufferViews=views, buffers=[dict(byteLength=len(binary))],
         extensionsUsed=['VRM'], extensions=dict(VRM=dict(specVersion='0.0', meta=dict(title='Original animated VRM test figure'),
            humanoid=dict(humanBones=[dict(bone=name, node=node) for name, node in [
                ('hips', 2), ('head', 11), ('leftUpperArm', 3), ('rightUpperArm', 5),
                ('leftUpperLeg', 7), ('rightUpperLeg', 9), ('leftLowerLeg', 8), ('rightLowerLeg', 10)]]))))
document = json.dumps(j, separators=(',', ':')).encode()
document += b' ' * (-len(document) % 4)
binary += b'\0' * (-len(binary) % 4)
glb = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(document) + 8 + len(binary))
glb += struct.pack('<II', len(document), 0x4E4F534A) + document
glb += struct.pack('<II', len(binary), 0x004E4942) + binary
destination = pathlib.Path(sys.argv[1])
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_bytes(glb)
print(f'VRM fixture: {len(glb)} bytes / {vertex_count} vertices / 10 joints')
