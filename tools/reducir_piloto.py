# Baja los triángulos del modelo del piloto (glTF con esqueleto) sin tocar el esqueleto, los pesos, las UV
# ni la textura. Ver docs/PILOTO.md ("Menos triángulos: el modelo 2 reducido").
#
# Uso: python tools/reducir_piloto.py entrada.gltf salida.gltf [opciones]
#   --objetivo 0.65    fracción de triángulos que quedan (se frena antes si no hay más que se puedan sacar
#                      sin pasarse de --error)
#   --error 40         desvío máximo de un colapso en mm (raíz del error cuadrático medio de la cuádrica
#                      sobre el área de los triángulos que junta; por la importancia de la zona)
#   --costura 4        cuánto pesa no correr las costuras (en 3D y el borde de la isla en la UV)
#   --uv 2.0           metros por unidad de UV en la cuádrica (1 texel de 2048 ~ 1 mm)
#   --peso-max 0.25    no junta vértices cuyos pesos difieren más que esto (mitad de la suma de |dw|)
#   --cabeza 8 --cuello 2 --pies 6   cuánto más cuesta sacar de cada zona (hueso que más pesa)
#   --manos-fijas 1    no saca ningún vértice con peso de las manos (RiderModel arma los dedos y el agarre
#                      midiendo esos vértices: con menos, la flexión de los dedos cambia)
#
# Cómo: colapsos de medio lado (el vértice que se va se junta con un vecino que queda donde estaba), por
# cuádricas de error en posición + UV (Garland-Heckbert), con las costuras de UV respetadas:
#   - un vértice sin costura se puede juntar con cualquier vecino de su misma isla;
#   - uno en el medio de una costura (dos copias, dos aristas de costura) sólo a lo largo de la costura, y
#     cada copia con la copia del vecino del mismo lado;
#   - los cruces de costuras (tres o más islas) no se tocan;
#   - ninguna isla de UV se queda sin triángulos (la textura de esa isla dejaría de verse).
# Así cada vértice que queda es uno del original con sus mismos bytes (posición, normal, UV, huesos, pesos):
# no se interpola nada. Se rechazan los colapsos que dan vuelta un triángulo (en 3D o en la UV), que dejan
# triángulos muy finos o que rompen la variedad (condición del enlace).
#
# La salida reusa todo lo demás del glTF tal cual (nodos, esqueleto, matrices de bind, animaciones,
# material) y saca del .bin lo que nadie usa. La textura se copia byte a byte a salida_deps/.
import hashlib
import heapq
import json
import os
import shutil
import sys
from collections import defaultdict

import numpy as np

CT = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}

args = sys.argv[1:]
if len(args) < 2:
    sys.exit('uso: python tools/reducir_piloto.py entrada.gltf salida.gltf [opciones]')
IN, OUT = os.path.abspath(args[0]), os.path.abspath(args[1])
opt = {}
for k in range(2, len(args) - 1, 2):
    opt[args[k].lstrip('-')] = float(args[k + 1])
TARGET = opt.get('objetivo', 0.65)
MAXERR = opt.get('error', 40.0) * 1e-3
SEAMW = opt.get('costura', 4.0)
UVS = opt.get('uv', 2.0)
WMAX = opt.get('peso-max', 0.25)
HANDS_LOCKED = opt.get('manos-fijas', 1.0) != 0.0
IMP = {'Hand': opt.get('manos', 10.0), 'Head': opt.get('cabeza', 8.0), 'neck': opt.get('cuello', 2.0),
       'Foot': opt.get('pies', 6.0), 'ToeBase': opt.get('pies', 6.0)}

# ---------------------------------------------------------------------------------------------------
# Leer
# ---------------------------------------------------------------------------------------------------
g = json.load(open(IN, encoding='utf-8'))
base = os.path.dirname(IN)
if len(g['meshes']) != 1 or len(g['meshes'][0]['primitives']) != 1 or len(g['buffers']) != 1:
    sys.exit('se espera una malla con una primitiva y un buffer (como el modelo 2)')
buf = open(os.path.join(base, g['buffers'][0]['uri']), 'rb').read()


def raw(i):
    a = g['accessors'][i]
    bv = g['bufferViews'][a['bufferView']]
    if bv.get('byteStride'):
        sys.exit('bufferView intercalado: no soportado')
    n = a['count'] * NC[a['type']]
    arr = np.frombuffer(buf, CT[a['componentType']], n, bv.get('byteOffset', 0) + a.get('byteOffset', 0))
    return arr.reshape(a['count'], NC[a['type']]) if NC[a['type']] > 1 else arr


prim = g['meshes'][0]['primitives'][0]
attrs = prim['attributes']
POS32 = raw(attrs['POSITION'])
P = POS32.astype(np.float64)
UV = raw(attrs['TEXCOORD_0']).astype(np.float64)
JO = raw(attrs['JOINTS_0']).astype(np.int64)
WE = raw(attrs['WEIGHTS_0']).astype(np.float64)
T0 = raw(prim['indices']).astype(np.int64).reshape(-1, 3)
names = [g['nodes'][j]['name'] for j in g['skins'][0]['joints']]
nv = len(P)
dense = np.zeros((nv, len(names)))
np.add.at(dense, (np.repeat(np.arange(nv), 4), JO.ravel()), WE.ravel())
dominant = dense.argmax(1)


hand_cols = [j for j, n in enumerate(names) if n.endswith('Hand')]


def importance(v):
    n = names[dominant[v]]
    for key, f in IMP.items():
        if n.endswith(key):
            return f
    return 1.0


# Vértices unidos por posición exacta (los bytes del float).
key = raw(attrs['POSITION']).view(np.uint8).reshape(nv, 12)
_, wel = np.unique(key, axis=0, return_inverse=True)
wel = wel.ravel()
nw = wel.max() + 1
WP = np.zeros((nw, 3))
WP[wel] = P

locked = np.zeros(nw, bool)
if HANDS_LOCKED:
    locked[wel[dense[:, hand_cols].sum(1) > 0.0]] = True

tris = [list(t) for t in T0]            # índices sin unir (copias)
alive = [True] * len(tris)
vt = defaultdict(set)                   # vértice unido -> triángulos vivos
for ti, t in enumerate(tris):
    for c in t:
        vt[wel[c]].add(ti)


# Islas de UV: triángulos unidos por copias compartidas (no cambian al colapsar).
par = list(range(nv))


def find(x):
    while par[x] != x:
        par[x] = par[par[x]]
        x = par[x]
    return x


for t in T0:
    par[find(t[1])] = find(t[0])
    par[find(t[2])] = find(t[0])
island = [find(t[0]) for t in T0]
island_tris = defaultdict(int)
for i in island:
    island_tris[i] += 1


def wtri(ti):
    return [wel[c] for c in tris[ti]]


def neighbors(W):
    s = set()
    for ti in vt[W]:
        s.update(wtri(ti))
    s.discard(W)
    return s


# ---------------------------------------------------------------------------------------------------
# Cuádricas (6x6 sobre [x, y, z, s·u, s·v, 1]) por copia
# ---------------------------------------------------------------------------------------------------
K = np.zeros((nv, 6, 6))
KA = np.zeros(nv)                       # área que suma cada cuádrica


def x5(c):
    return np.array([P[c, 0], P[c, 1], P[c, 2], UVS * UV[c, 0], UVS * UV[c, 1]])


def tri_area(a, b, c):
    return 0.5 * np.linalg.norm(np.cross(b - a, c - a))


for t in T0:
    p1, p2, p3 = x5(t[0]), x5(t[1]), x5(t[2])
    area = tri_area(P[t[0]], P[t[1]], P[t[2]])
    e1 = p2 - p1
    if np.linalg.norm(e1) < 1e-12:
        continue
    e1 /= np.linalg.norm(e1)
    e2 = p3 - p1 - np.dot(p3 - p1, e1) * e1
    if np.linalg.norm(e2) < 1e-12:
        continue
    e2 /= np.linalg.norm(e2)
    A = np.eye(5) - np.outer(e1, e1) - np.outer(e2, e2)
    b = np.dot(p1, e1) * e1 + np.dot(p1, e2) * e2 - p1
    c = np.dot(p1, p1) - np.dot(p1, e1) ** 2 - np.dot(p1, e2) ** 2
    Q = np.zeros((6, 6))
    Q[:5, :5] = A
    Q[:5, 5] = b
    Q[5, :5] = b
    Q[5, 5] = c
    for v in t:
        K[v] += area * Q
        KA[v] += area

# Costuras: plano por la arista, perpendicular al triángulo, para que la costura no se deforme.
edge_tris = defaultdict(list)
for ti, t in enumerate(T0):
    for i in range(3):
        a, b = t[i], t[(i + 1) % 3]
        edge_tris[tuple(sorted((wel[a], wel[b])))].append((ti, a, b))
seam_edges = 0
for e, lst in edge_tris.items():
    if len(lst) != 2 or {lst[0][1], lst[0][2]} == {lst[1][1], lst[1][2]}:
        continue
    seam_edges += 1
    for ti, a, b in lst:
        t = T0[ti]
        n = np.cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]])
        d = P[b] - P[a]
        m = np.cross(d, n)
        if np.linalg.norm(m) < 1e-12:
            continue
        m /= np.linalg.norm(m)
        h = np.array([m[0], m[1], m[2], 0.0, 0.0, -np.dot(m, P[a])])
        Q = np.outer(h, h) * np.dot(d, d) * SEAMW
        # y el borde de la isla en la UV, para que no se coma textura de los costados
        du = UV[b] - UV[a]
        if np.linalg.norm(du) > 1e-9:
            mu = np.array([-du[1], du[0]]) / np.linalg.norm(du)
            hu = np.array([0.0, 0.0, 0.0, mu[0], mu[1], -UVS * np.dot(mu, UV[a])])
            Q += np.outer(hu, hu) * np.dot(d, d) * SEAMW
        K[a] += Q
        K[b] += Q


def qerr(c, x):
    h = np.append(x, 1.0)
    return float(h @ K[c] @ h)


# ---------------------------------------------------------------------------------------------------
# Colapsos
# ---------------------------------------------------------------------------------------------------
def quality(a, b, c):
    l = np.dot(b - a, b - a) + np.dot(c - b, c - b) + np.dot(a - c, a - c)
    return 4.0 * np.sqrt(3.0) * tri_area(a, b, c) / l if l > 0 else 0.0


def uv_area(a, b, c):
    return (UV[b, 0] - UV[a, 0]) * (UV[c, 1] - UV[a, 1]) - (UV[c, 0] - UV[a, 0]) * (UV[b, 1] - UV[a, 1])


def plan(U, V):
    """Mapeo copia de U -> copia de V si U se puede juntar con V; si no, None."""
    shared = [ti for ti in vt[U] if V in wtri(ti)]
    if len(shared) != 2:
        return None
    if island[shared[0]] == island[shared[1]]:
        if island_tris[island[shared[0]]] <= 2:
            return None
    elif island_tris[island[shared[0]]] <= 1 or island_tris[island[shared[1]]] <= 1:
        return None
    wedges = defaultdict(set)
    for ti in vt[U]:
        uc = next(c for c in tris[ti] if wel[c] == U)
        wedges[uc]
        if ti in shared:
            wedges[uc].add(next(c for c in tris[ti] if wel[c] == V))
    mapping = {}
    for uc, s in wedges.items():
        if len(s) != 1:
            return None
        mapping[uc] = next(iter(s))
    # Enlace: los vecinos comunes son justo los dos opuestos de la arista.
    opp = set()
    for ti in shared:
        opp.update(w for w in wtri(ti) if w != U and w != V)
    if neighbors(U) & neighbors(V) != opp:
        return None
    for o in opp:
        if len(neighbors(o)) <= 3:
            return None
    # Pesos parecidos.
    for uc, vc in mapping.items():
        if 0.5 * np.abs(dense[uc] - dense[vc]).sum() > WMAX:
            return None
    # Geometría: sin dar vuelta triángulos ni dejarlos muy finos, en 3D y en la UV.
    for ti in vt[U]:
        if ti in shared:
            continue
        t = tris[ti]
        old = [WP[wel[c]] for c in t]
        new = [WP[V] if wel[c] == U else WP[wel[c]] for c in t]
        n0 = np.cross(old[1] - old[0], old[2] - old[0])
        n1 = np.cross(new[1] - new[0], new[2] - new[0])
        l0, l1 = np.linalg.norm(n0), np.linalg.norm(n1)
        if l1 < 1e-12 or np.dot(n0, n1) < 0.5 * l0 * l1:          # más de 60° de giro
            return None
        if quality(*new) < min(0.08, 0.5 * quality(*old)):
            return None
        tc = [mapping[c] if wel[c] == U else c for c in t]
        a0, a1 = uv_area(*t), uv_area(*tc)
        if a0 * a1 <= 0.0 or abs(a1) < 1e-10:
            return None
    return mapping


def cost(U, V, mapping):
    e = 0.0
    for uc, vc in mapping.items():
        e += qerr(uc, np.array([WP[V][0], WP[V][1], WP[V][2], UVS * UV[vc, 0], UVS * UV[vc, 1]]))
    # Orden: el error de la cuádrica pesado por área (primero lo chico y plano). Tope: el desvío medio (m).
    imp = max(importance(uc) for uc in mapping)
    area = sum(KA[uc] for uc in mapping)
    return max(e, 0.0) * imp, np.sqrt(max(e, 0.0) / max(area, 1e-12)) * imp


version = [0] * nw
removed = [False] * nw
heap = []


def push(U):
    version[U] += 1
    if locked[U]:
        return
    best = None
    for V in neighbors(U):
        m = plan(U, V)
        if m is None:
            continue
        c, dev = cost(U, V, m)
        if dev > MAXERR:
            continue
        if best is None or c < best[0]:
            best = (c, V)
    if best is not None:
        heapq.heappush(heap, (best[0], U, best[1], version[U]))


for U in range(nw):
    push(U)
count = len(tris)
target = int(TARGET * count)
done = 0
while heap and count > target:
    c, U, V, ver = heapq.heappop(heap)
    if removed[U] or ver != version[U] or removed[V]:
        continue
    mapping = plan(U, V)
    if mapping is None or abs(cost(U, V, mapping)[0] - c) > 1e-9 * c + 1e-18:
        push(U)
        continue
    ring = neighbors(U)
    for ti in list(vt[U]):
        t = tris[ti]
        if V in wtri(ti):
            alive[ti] = False
            island_tris[island[ti]] -= 1
            for cc in t:
                vt[wel[cc]].discard(ti)
            count -= 1
        else:
            tris[ti] = [mapping[cc] if wel[cc] == U else cc for cc in t]
            vt[V].add(ti)
    vt[U].clear()
    removed[U] = True
    for uc, vc in mapping.items():
        K[vc] += K[uc]
        KA[vc] += KA[uc]
    done += 1
    again = set(ring)
    for X in ring:
        again |= neighbors(X)
    again.discard(U)
    for X in again:
        if not removed[X]:
            push(X)

# ---------------------------------------------------------------------------------------------------
# Escribir
# ---------------------------------------------------------------------------------------------------
T1 = np.array([tris[ti] for ti in range(len(tris)) if alive[ti]], dtype=np.int64)
used = np.unique(T1)
remap = np.full(nv, -1, dtype=np.int64)
remap[used] = np.arange(len(used))
T1 = remap[T1]

used_views = set()
for a in g['accessors']:
    if 'bufferView' in a:
        used_views.add(a['bufferView'])
for im in g.get('images', []):
    if 'bufferView' in im:
        used_views.add(im['bufferView'])
mesh_acc = set(attrs.values()) | {prim['indices']}
new_data = {}
for name, ai in attrs.items():
    new_data[ai] = np.ascontiguousarray(raw(ai)[used]).tobytes()
new_data[prim['indices']] = T1.astype(np.uint16 if len(used) < 65536 else np.uint32).ravel().tobytes()

out_name = os.path.splitext(os.path.basename(OUT))[0]
deps = out_name + '_deps'
os.makedirs(os.path.join(os.path.dirname(OUT), deps), exist_ok=True)
view_of_acc = {ai: g['accessors'][ai]['bufferView'] for ai in mesh_acc}
blob = bytearray()
new_views = []
view_map = {}
for vi, bv in enumerate(g['bufferViews']):
    if vi not in used_views:
        print(f'bufferView {vi} ({bv["byteLength"]} bytes) no lo usa nadie: se saca')
        continue
    accs = [ai for ai, v in view_of_acc.items() if v == vi]
    if accs:
        data = new_data[accs[0]]
    else:
        off = bv.get('byteOffset', 0)
        data = buf[off:off + bv['byteLength']]
    while len(blob) % 4:
        blob.append(0)
    nb = {k: v for k, v in bv.items() if k not in ('byteOffset', 'byteLength')}
    nb['byteOffset'] = len(blob)
    nb['byteLength'] = len(data)
    view_map[vi] = len(new_views)
    new_views.append(nb)
    blob += data
g['bufferViews'] = new_views
for ai, a in enumerate(g['accessors']):
    if 'bufferView' in a:
        a['bufferView'] = view_map[a['bufferView']]
    if ai in mesh_acc:
        a['count'] = len(T1) * 3 if ai == prim['indices'] else len(used)
        if ai == prim['indices']:
            a['componentType'] = 5123 if len(used) < 65536 else 5125
    if ai == attrs['POSITION']:
        pp = POS32[used]
        a['min'] = [float(x) for x in pp.min(0)]
        a['max'] = [float(x) for x in pp.max(0)]
for im in g.get('images', []):
    if 'bufferView' in im:
        im['bufferView'] = view_map[im['bufferView']]
bin_name = f'{deps}/{out_name}.bin'
open(os.path.join(os.path.dirname(OUT), bin_name), 'wb').write(bytes(blob))
g['buffers'][0] = {'byteLength': len(blob), 'uri': bin_name}
for k, im in enumerate(g.get('images', [])):
    if 'uri' in im:
        src = os.path.join(base, im['uri'])
        dst_rel = f'{deps}/{out_name}.image-{k}{os.path.splitext(im["uri"])[1]}'
        dst = os.path.join(os.path.dirname(OUT), dst_rel)
        shutil.copyfile(src, dst)
        h = [hashlib.sha256(open(f, 'rb').read()).hexdigest() for f in (src, dst)]
        assert h[0] == h[1]
        im['uri'] = dst_rel
g['asset']['extras'] = {'reducido': f'tools/reducir_piloto.py desde {os.path.basename(IN)}: '
                                    f'{len(T0)} -> {len(T1)} triángulos, {nv} -> {len(used)} vértices'}
with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
    json.dump(g, f, indent=1, ensure_ascii=False)

# ---------------------------------------------------------------------------------------------------
# Informe por zona (hueso que más pesa)
# ---------------------------------------------------------------------------------------------------
before = np.bincount(dominant[T0[:, 0]], minlength=len(names))
after = np.bincount(dominant[used[T1[:, 0]]], minlength=len(names))
print(f'{len(T0)} -> {len(T1)} triángulos ({100.0 * (1 - len(T1) / len(T0)):.1f}% menos), '
      f'{nv} -> {len(used)} vértices, {done} colapsos, {seam_edges} aristas de costura')
for j in np.argsort(-before):
    if before[j]:
        print(f'  {names[j]:14s} {before[j]:5d} -> {after[j]:5d}')
for hname in ('LeftHand', 'RightHand'):
    j = names.index(hname)
    print(f'  vértices con peso >= 0.5 de {hname}: {(dense[:, j] >= 0.5).sum()} -> {(dense[used, j] >= 0.5).sum()}')
print(f'{OUT}: .bin {len(buf)} -> {len(blob)} bytes')
