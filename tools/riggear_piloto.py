# Riggea el modelo del piloto: a una malla sin esqueleto (p. ej. un personaje de Tripo en pose A) le
# pone el esqueleto que espera RiderModel (los nombres y la jerarquía de Low_Poly_Motorcyclist_2_rigged,
# estilo Mixamo) más los dedos: tres falanges y la punta de cada dedo, pulgar incluido. Las
# articulaciones se miden de la malla (cortes de las piernas, los brazos y los dedos) y los pesos salen
# del calor de Blender, pieza por pieza, cada una sólo con los huesos que le tocan.
#
# Uso (Blender 4.x o 5.x, sin ventana):
#   blender -b --python tools/riggear_piloto.py -- entrada.gltf salida.gltf [opciones]
# Opciones:
#   --altura 1.70              alto final en metros (pies en z = 0)
#   --adelante auto|+x|-x|+y|-y hacia dónde mira el modelo en Blender al importarlo (auto: por los pies)
#   --articulaciones a.json    corrige a mano posiciones medidas: {"LeftLeg": [x, y, z], ...} en metros,
#                              coordenadas de Blender del modelo ya orientado (mira a -Y, +X es su izquierda)
#   --medidas m.json           guarda las articulaciones medidas (para revisarlas o corregirlas)
#   --malla m.npz              guarda la malla de análisis (vértices unidos, islas, pesos) para inspección
# La salida es salida.gltf + salida_deps/ (el .bin y la textura de color en PNG: raylib no lee JPG).
# Ver docs/PILOTO.md ("Riggear un modelo nuevo").
import bpy, bmesh, sys, os, json, math, heapq, shutil
import numpy as np
from mathutils import Matrix, Vector, kdtree

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
if len(argv) < 2:
    print('uso: blender -b --python tools/riggear_piloto.py -- entrada.gltf salida.gltf [opciones]')
    sys.exit(1)
IN, OUT = os.path.abspath(argv[0]), os.path.abspath(argv[1])
opts = {}
k = 2
while k < len(argv):
    opts[argv[k].lstrip('-')] = argv[k + 1] if k + 1 < len(argv) else ''
    k += 2
H = float(opts.get('altura', 1.70))
FORWARD = opts.get('adelante', 'auto')


def log(*a):
    print('[riggear]', *a, flush=True)


# ----------------------------------------------------------------------------------------------------
# Importar, unir, orientar (mirando a -Y, izquierda del piloto en +X) y escalar (pies en z = 0)
# ----------------------------------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=IN)
scene = bpy.context.scene
vl = bpy.context.view_layer
meshes = [o for o in scene.objects if o.type == 'MESH']
if not meshes:
    raise SystemExit('el archivo no tiene mallas')
for o in scene.objects:
    o.select_set(False)
for o in meshes:
    mw = o.matrix_world.copy()
    o.parent = None
    o.matrix_world = mw
    o.select_set(True)
vl.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
obj = vl.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for o in list(scene.objects):
    if o != obj:
        bpy.data.objects.remove(o, do_unlink=True)
for m in list(obj.modifiers):
    obj.modifiers.remove(m)
obj.vertex_groups.clear()
me = obj.data


def verts_np(mesh):
    a = np.zeros(len(mesh.vertices) * 3)
    mesh.vertices.foreach_get('co', a)
    return a.reshape(-1, 3)


co0 = verts_np(me)
mn, mx = co0.min(0), co0.max(0)
H0 = mx[2] - mn[2]
if FORWARD == 'auto':
    # El eje de profundidad es el más angosto (los brazos abren el otro). Adelante: hacia donde los pies
    # (la suela) se estiran más desde el centro de las canillas.
    depth = 0 if (mx[0] - mn[0]) < (mx[1] - mn[1]) else 1
    z = co0[:, 2] - mn[2]
    shin = co0[(z > 0.12 * H0) & (z < 0.25 * H0), depth].mean()
    sole = co0[z < 0.03 * H0, depth]
    sign = 1 if (sole.max() - shin) > (shin - sole.min()) else -1
    FORWARD = ('+' if sign > 0 else '-') + 'xy'[depth]
    log('mira hacia', FORWARD)
ang = {'-y': 0.0, '+x': -math.pi / 2, '+y': math.pi, '-x': math.pi / 2}[FORWARD.lower()]
R = Matrix.Rotation(ang, 4, 'Z')
me.transform(R)
co0 = verts_np(me)
mn, mx = co0.min(0), co0.max(0)
s = H / (mx[2] - mn[2])
me.transform(Matrix.Diagonal((s, s, s, 1.0)) @ Matrix.Translation((-(mn[0] + mx[0]) / 2, -(mn[1] + mx[1]) / 2, -mn[2])))
me.update()
co_orig = verts_np(me)
log('vértices', len(co_orig), 'escala %.4f' % s)

# ----------------------------------------------------------------------------------------------------
# Malla de análisis: vértices repetidos (costuras de UV) unidos, triángulos e islas
# ----------------------------------------------------------------------------------------------------
bm = bmesh.new()
bm.from_mesh(me)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=2e-5 * H)
bm.verts.ensure_lookup_table()
co = np.array([v.co[:] for v in bm.verts])
tri = []
for f in bm.faces:
    ids = [v.index for v in f.verts]
    for j in range(1, len(ids) - 1):
        tri.append((ids[0], ids[j], ids[j + 1]))
tri = np.array(tri)
nv = len(co)
adj = [set() for _ in range(nv)]
for e in bm.edges:
    a, b = e.verts[0].index, e.verts[1].index
    adj[a].add(b)
    adj[b].add(a)
par = list(range(nv))


def find(a):
    while par[a] != a:
        par[a] = par[par[a]]
        a = par[a]
    return a


for a in range(nv):
    for b in adj[a]:
        ra, rb = find(a), find(b)
        if ra != rb:
            par[ra] = rb
isl = np.array([find(i) for i in range(nv)])
isl_ids, isl_cnt = np.unique(isl, return_counts=True)
triIsl = isl[tri[:, 0]]
log('vértices unidos', nv, 'islas', len(isl_ids))

# Clasificar islas: cuerpo (la más grande), cabeza/casco, manos (guantes), pies (botas) y otras.
body = isl_ids[np.argmax(isl_cnt)]
kind = {}
for k_, c_ in zip(isl_ids, isl_cnt):
    P = co[isl == k_]
    c = P.mean(0)
    if k_ == body:
        kind[k_] = 'cuerpo'
    elif c[2] > 0.78 * H and abs(c[0]) < 0.1 * H:
        kind[k_] = 'cabeza'
    elif c[2] < 0.2 * H and c_ > 20:
        kind[k_] = 'pieI' if c[0] > 0 else 'pieD'
    elif 0.3 * H < c[2] < 0.7 * H and abs(c[0]) > 0.18 * H and c_ > 50:
        kind[k_] = 'manoI' if c[0] > 0 else 'manoD'
    else:
        kind[k_] = 'otra'
for k_, c_ in zip(isl_ids, isl_cnt):
    P = co[isl == k_]
    log('isla %-6s %5d vértices  z %.2f..%.2f  x %.2f..%.2f' % (kind[k_], c_, P[:, 2].min(), P[:, 2].max(), P[:, 0].min(), P[:, 0].max()))


def islands_of(*kinds):
    return [k_ for k_ in isl_ids if kind[k_] in kinds]


def tris_of(*kinds):
    return tri[np.isin(triIsl, islands_of(*kinds))]


# ----------------------------------------------------------------------------------------------------
# Cortes: la malla contra un plano da contornos (lazos de segmentos); de cada uno, el centro y el perímetro
# ----------------------------------------------------------------------------------------------------
def cut(T, p0, n):
    n = np.asarray(n, float)
    n = n / np.linalg.norm(n)
    d = (co - p0) @ n
    dt = d[T]
    neg = dt < 0
    m = neg.any(1) & (~neg).any(1)
    groups = {}
    gpar = {}

    def gf(a):
        while gpar.setdefault(a, a) != a:
            gpar[a] = gpar[gpar[a]]
            a = gpar[a]
        return a
    segs = []
    for t, dd in zip(T[m], dt[m]):
        pts, ks = [], []
        for a, b in ((0, 1), (1, 2), (2, 0)):
            if (dd[a] < 0) != (dd[b] < 0):
                u = dd[a] / (dd[a] - dd[b])
                pts.append(co[t[a]] + u * (co[t[b]] - co[t[a]]))
                ks.append((min(t[a], t[b]), max(t[a], t[b])))
        if len(pts) == 2:
            segs.append((pts, ks))
            ra, rb = gf(ks[0]), gf(ks[1])
            if ra != rb:
                gpar[ra] = rb
    for pts, ks in segs:
        groups.setdefault(gf(ks[0]), []).append(pts)
    out = []
    for ss in groups.values():
        L = np.array([np.linalg.norm(b - a) for a, b in ss])
        M = np.array([(a + b) / 2 for a, b in ss])
        P = np.array([p for sg in ss for p in sg])
        out.append({'c': (M * L[:, None]).sum(0) / max(L.sum(), 1e-9), 'len': L.sum(), 'pts': P})
    out.sort(key=lambda o: -o['len'])
    return out


def nearest_contour(T, p0, n, maxd):
    best = None
    n = np.asarray(n, float) / np.linalg.norm(n)
    for c in cut(T, p0, n):
        dd = np.linalg.norm(np.cross(c['c'] - p0, n))
        if dd < maxd and (best is None or dd < best[0]):
            best = (dd, c)
    return best[1] if best else None


def fit_line(P):
    c = P.mean(0)
    _, _, vt = np.linalg.svd(P - c)
    return c, vt[0]


J = {}          # articulaciones medidas (Blender, m)
Tbody = tris_of('cuerpo')
Tlegs = tris_of('cuerpo', 'pieI', 'pieD', 'otra')

# ---- línea media y entrepierna
mid = [c for c in cut(Tbody, np.array([0, 0, 0.6 * H]), [0, 0, 1]) if c['pts'][:, 0].min() < 0 < c['pts'][:, 0].max()]
xc = mid[0]['c'][0] if mid else 0.0
zc = None
for z in np.arange(0.30 * H, 0.60 * H, 0.0025 * H):
    for c in cut(Tbody, np.array([0, 0, z]), [0, 0, 1]):
        P = c['pts']
        if P[:, 0].min() < xc - 0.02 * H and P[:, 0].max() > xc + 0.02 * H:
            zc = z
            break
    if zc is not None:
        break
log('línea media x=%.4f  entrepierna z=%.3f' % (xc, zc))

# ---- piernas
zHip = zc + 0.05 * H
zAnkle = 0.058 * H
zKneeRatio = zAnkle + 0.51 * (zHip - zAnkle)


def leg_center(sg, z, T=Tlegs):
    best = None
    for c in cut(T, np.array([0, 0, z]), [0, 0, 1]):
        x = c['c'][0] - xc
        if sg * x > 0.01 * H and abs(x) < 0.14 * H and (best is None or c['len'] > best['len']):
            best = c
    return best


for sg, S in ((1, 'Left'), (-1, 'Right')):
    zs = np.arange(0.01 * H, zc - 0.01 * H, 0.005 * H)
    cs = [(z, leg_center(sg, z)) for z in zs]
    cs = [(z, c) for z, c in cs if c is not None]
    Z = np.array([z for z, _ in cs])
    C = np.array([c['c'] for _, c in cs])
    YMIN = np.array([c['pts'][:, 1].min() for _, c in cs])
    # rodilla: la proporción anatómica, corrida hacia la rodillera si sobresale adelante
    zKnee = zKneeRatio
    band = (Z > 0.20 * H) & (Z < 0.36 * H)
    if band.any():
        zb = Z[band]
        yb = YMIN[band]
        close = zb[yb < yb.min() + 0.004]
        zBulge = close.mean()
        around = YMIN[(np.abs(Z - zBulge) > 0.07) & (np.abs(Z - zBulge) < 0.10)]
        if len(around) and around.mean() - yb.min() > 0.012 and abs(zBulge - zKneeRatio) < 0.05:
            zKnee = 0.5 * (zKneeRatio + zBulge)
    th = (Z > zKnee + 0.06) & (Z < zc - 0.02 * H)
    sh = (Z > zAnkle + 0.10) & (Z < zKnee - 0.06)

    def line_at(mask, z):
        A = np.stack([Z[mask], np.ones(mask.sum())], 1)
        cx = np.linalg.lstsq(A, C[mask, 0], rcond=None)[0]
        cy = np.linalg.lstsq(A, C[mask, 1], rcond=None)[0]
        return np.array([cx[0] * z + cx[1], cy[0] * z + cy[1], z])
    knee = 0.5 * (line_at(th, zKnee) + line_at(sh, zKnee))
    hip = line_at(th, zHip)
    ank = C[(Z >= zAnkle) & (Z <= zAnkle + 0.04)].mean(0)
    ank[2] = zAnkle
    # pie: punta, talón y la base de los dedos (27% del largo desde la punta)
    foot = leg_center(sg, 0.02 * H)
    P = foot['pts']
    yToe, yHeel = P[:, 1].min(), P[:, 1].max()
    yBall = yToe + 0.27 * (yHeel - yToe)
    near = P[np.abs(P[:, 1] - yBall) < 0.03]
    xBall = 0.5 * (near[:, 0].min() + near[:, 0].max()) if len(near) else ank[0]
    J[S + 'UpLeg'] = hip
    J[S + 'Leg'] = knee
    J[S + 'Foot'] = ank
    J[S + 'ToeBase'] = np.array([xBall, yBall, 0.02 * H])
    J[S + 'ToeEnd'] = np.array([xBall, yToe + 0.01, 0.02 * H])
    log('%s: cadera %s rodilla %s (proporción %.3f) tobillo %s' % (S, hip.round(3), knee.round(3), zKneeRatio, ank.round(3)))

# ---- tronco, cuello y cabeza
Tneck = tris_of('cuerpo')


def torso_contour(z, smallest=False):
    cs = [c for c in cut(Tneck, np.array([0, 0, z]), [0, 0, 1]) if c['pts'][:, 0].min() < xc < c['pts'][:, 0].max()]
    if not cs:
        return None
    return min(cs, key=lambda c: c['len']) if smallest else max(cs, key=lambda c: c['len'])


widths = []
for z in np.arange(0.72 * H, 0.86 * H, 0.005 * H):
    c = torso_contour(z)
    if c is not None:
        widths.append((z, c['pts'][:, 0].max() - c['pts'][:, 0].min()))
wmax = max(w for _, w in widths)
zTop = max(z for z, _ in widths)
zNeck = None
for z in np.arange(0.76 * H, 0.92 * H, 0.0025 * H):
    c = torso_contour(z)
    if c is None or c['pts'][:, 0].max() - c['pts'][:, 0].min() < 0.4 * wmax:
        zNeck = z
        break
zNeck -= 0.005
# el cuello: de los contornos que cruzan la línea media, el más angosto (no el cuello de la campera); puede
# ser parte de la pieza de la cabeza (en el modelo 3 el cuello baja del casco adentro de la campera)
ncs = [c for c in cut(tris_of('cuerpo', 'cabeza'), np.array([0, 0, zNeck + 0.015]), [0, 0, 1]) if c['pts'][:, 0].min() < xc < c['pts'][:, 0].max()]
nc = min(ncs, key=lambda c: c['pts'][:, 0].max() - c['pts'][:, 0].min()) if ncs else None
yNeck = nc['c'][1] if nc is not None else 0.0
zHips = zHip + 0.04 * H
yOf = lambda z: torso_contour(z)['c'][1]
J['Hips'] = np.array([xc, yOf(zHips), zHips])
for f, name in ((0.27, 'Spine02'), (0.54, 'Spine01'), (0.81, 'Spine')):
    z = zHips + f * (zNeck - zHips)
    J[name] = np.array([xc, yOf(z), z])
J['neck'] = np.array([xc, yNeck, zNeck])
zHead = zNeck + 0.25 * (H - zNeck)
J['Head'] = np.array([xc, yNeck, zHead])
J['head_end'] = np.array([xc, yNeck, H])
Thead = tris_of('cabeza') if islands_of('cabeza') else Tneck
hc = cut(Thead, np.array([0, 0, zHead]), [0, 0, 1])
yFront = min(c['pts'][:, 1].min() for c in hc) if hc else yNeck - 0.1
J['headfront'] = np.array([xc, yFront + 0.02, zHead])
log('cuello z=%.3f  cabeza z=%.3f  cadera (Hips) z=%.3f' % (zNeck, zHead, zHips))

# ---- brazos, muñecas y dedos
HAND = {}
for sg, S in ((1, 'Left'), (-1, 'Right')):
    hk = islands_of('manoI' if sg > 0 else 'manoD')
    if hk:
        Vh = np.where(np.isin(isl, hk))[0]
    else:
        lat = sg * (co[:, 0] - xc)
        Vh = np.where((isl == body) & (lat > 0.85 * lat.max()))[0]
    Th = tri[np.isin(tri[:, 0], Vh)]
    a = np.array([xc + sg * 0.1 * H, 0.0, 0.78 * H])
    b = co[Vh].mean(0)
    for it in range(4):
        L = np.linalg.norm(b - a)
        ax = (b - a) / L
        pts = []
        for s_ in np.arange(0.2 * L, 0.8 * L, 0.01):
            c = nearest_contour(Tbody, a + ax * s_, ax, 0.05 * H)
            if c is not None:
                pts.append(c['c'])
        cen, d_ = fit_line(np.array(pts))
        if d_ @ ax < 0:
            d_ = -d_
        a = cen + d_ * ((a - cen) @ d_)
        b = cen + d_ * ((b - cen) @ d_)
    ax = d_
    L = np.linalg.norm(b - a)
    # muñeca: lo más angosto entre el puño del guante (o el antebrazo) y la palma
    prof = []
    for s_ in np.arange(0.6 * L, L + 0.1 * H, 0.005):
        c = nearest_contour(Th if hk else Tbody, a + ax * s_, ax, 0.05 * H)
        if c is not None:
            prof.append((s_, c['len'], c['c']))
    per = np.array([p[1] for p in prof])
    i0 = int(np.argmax(per[:max(3, len(per) // 3)]))
    iw = i0
    for i in range(i0, len(per)):
        if per[i] < per[iw]:
            iw = i
        if per[i] > 1.15 * per[iw]:
            break
    sW, wrist = prof[iw][0], prof[iw][2]
    # hombro: sobre el eje del brazo, 3.5% del alto debajo de lo más alto del hombro
    xs = xc + sg * 0.11 * H
    top = max(c['pts'][:, 2].max() for c in cut(Tbody, np.array([xs, 0, 0]), [1, 0, 0]) if c['pts'][:, 2].max() > 0.7 * H)
    zSh = top - 0.035 * H
    sSh = (zSh - a[2]) / ax[2]
    shoulder = a + ax * sSh
    sE = sSh + 0.555 * (sW - sSh)
    ce = nearest_contour(Tbody, a + ax * sE, ax, 0.05 * H)
    elbow = ce['c'] if ce is not None else a + ax * sE
    J[S + 'Arm'] = shoulder
    J[S + 'ForeArm'] = elbow
    J[S + 'Hand'] = wrist
    J[S + 'Shoulder'] = np.array([xc + sg * 0.035 * H / 1.7, 0.5 * (shoulder[1] + yNeck), shoulder[2] + 0.005])
    HAND[S] = dict(V=Vh, T=Th, ax=ax, sW=sW, a=a)
    log('%s: hombro %s codo %s muñeca %s (eje a %.0f° de la vertical)' % (S, shoulder.round(3), elbow.round(3), wrist.round(3),
                                                                       math.degrees(math.acos(-ax[2]))))

FINGERS = ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky']


def measure_fingers(S):
    h = HAND[S]
    Vh, Th, ax = h['V'], h['T'], h['ax']
    inV = np.zeros(nv, bool)
    inV[Vh] = True
    sv = (co[Vh] - h['a']) @ ax
    src = Vh[sv < sv.min() + 0.015]
    dist = np.full(nv, 1e9)
    hq = []
    for v in src:
        dist[v] = 0.0
        heapq.heappush(hq, (0.0, v))
    while hq:
        dv, v = heapq.heappop(hq)
        if dv > dist[v]:
            continue
        for w in adj[v]:
            if inV[w]:
                nd = dv + np.linalg.norm(co[v] - co[w])
                if nd < dist[w]:
                    dist[w] = nd
                    heapq.heappush(hq, (nd, w))
    # Ramas del grafo de Reeb: de lo más lejano a lo más cercano; cuando dos componentes se juntan, la
    # de punta más cercana termina ahí (su horqueta).
    rp = {}

    def rf(a):
        while rp[a] != a:
            rp[a] = rp[rp[a]]
            a = rp[a]
        return a
    topv, mem, merges = {}, {}, []
    for v in sorted(Vh, key=lambda v: -dist[v]):
        rp[v] = v
        topv[v] = v
        mem[v] = [v]
        for w in adj[v]:
            if w in rp:
                ra, rb = rf(v), rf(w)
                if ra == rb:
                    continue
                small, big = (ra, rb) if dist[topv[ra]] < dist[topv[rb]] else (rb, ra)
                if len(mem[small]) >= 6 and dist[topv[small]] - dist[v] > 0.012:
                    merges.append(dict(tip=topv[small], saddle=v, mem=list(mem[small])))
                rp[small] = big
                mem[big] += mem[small]
    root = rf(Vh[0])
    merges.sort(key=lambda m: -(dist[m['tip']] - dist[m['saddle']]))
    if len(merges) < 4:
        log('%s: no se separan los dedos (%d ramas): la mano queda sin dedos' % (S, len(merges)))
        return None
    br = merges[:4] + [dict(tip=topv[root], saddle=None, mem=None)]
    # el pulgar se separa primero (horqueta más cerca de la muñeca)
    th = min(br[:4], key=lambda m: dist[m['saddle']])
    fing = [m for m in br if m is not th]
    sadd = [m['saddle'] for m in fing if m['saddle'] is not None]
    dS = np.mean([dist[v] for v in sadd])
    for m in fing:
        if m['saddle'] is None:
            # el dedo más largo no tiene horqueta propia: su base a la altura de las de los otros
            base = [v for v in Vh if abs(dist[v] - dS) < 0.004 and np.linalg.norm(co[v] - co[m['tip']]) < 1.3 * (dist[m['tip']] - dS)]
            m['base'] = co[base].mean(0) if base else co[m['tip']]
        else:
            m['base'] = co[m['saddle']]
    th['base'] = co[th['saddle']]
    # índice, medio, anular, meñique: por la distancia de su base a la del pulgar
    fing.sort(key=lambda m: np.linalg.norm(m['base'] - th['base']))
    out = {}
    for name, m in zip(FINGERS, [th] + fing):
        tip = co[m['tip']]
        d_ = tip - m['base']
        d_ /= np.linalg.norm(d_)
        p0 = tip
        for it in range(3):
            vis = (tip - m['base']) @ d_
            pts = []
            for s_ in np.arange(0.004, vis, 0.004):
                c = nearest_contour(Th, tip - d_ * s_, d_, 0.02)
                if c is not None:
                    pts.append(c['c'])
            if len(pts) >= 3:
                cen, nd = fit_line(np.array(pts))
                if nd @ d_ < 0:
                    nd = -nd
                d_ = nd
                p0 = cen
        tipC = p0 + d_ * ((tip - p0) @ d_)            # punta sobre el eje del dedo
        baseC = p0 + d_ * ((m['base'] - p0) @ d_)     # horqueta sobre el eje
        vis = np.linalg.norm(tipC - baseC)
        if name == 'Thumb':
            mcp = baseC - d_ * 0.1 * vis
            ip = mcp + (tipC - mcp) * 0.52
            cmc = J[S + 'Hand'] + 0.35 * (mcp - J[S + 'Hand'])
            out[name] = [cmc, mcp, ip, tipC - d_ * 0.003]
        else:
            mcp = baseC - d_ * 0.28 * vis                # nudillo: adentro de la palma
            Lf = np.linalg.norm(tipC - mcp)
            out[name] = [mcp, mcp + d_ * 0.46 * Lf, mcp + d_ * 0.74 * Lf, tipC - d_ * 0.003]
        log('%s %-6s punta %s base %s visible %.3f' % (S, name, tipC.round(3), baseC.round(3), vis))
    h['branches'] = {name: m for name, m in zip(FINGERS, [th] + fing)}
    return out


for S in ('Left', 'Right'):
    fj = measure_fingers(S)
    if fj:
        for name, pts in fj.items():
            for i, p in enumerate(pts):
                J['%sHand%s%d' % (S, name, i + 1)] = p

# correcciones a mano
if opts.get('articulaciones'):
    for kk, v in json.load(open(opts['articulaciones'])).items():
        J[kk] = np.array(v, float)
        log('corregida', kk, v)
if opts.get('medidas'):
    json.dump({kk: [round(float(x), 5) for x in v] for kk, v in J.items()}, open(opts['medidas'], 'w'), indent=1)

# ----------------------------------------------------------------------------------------------------
# Esqueleto (nombres y jerarquía de Low_Poly_Motorcyclist_2_rigged + dedos estilo Mixamo)
# ----------------------------------------------------------------------------------------------------
BONES = [  # nombre, padre, hacia dónde apunta la cola, deforma
    ('Hips', None, 'Spine02', True),
    ('Spine02', 'Hips', 'Spine01', True),
    ('Spine01', 'Spine02', 'Spine', True),
    ('Spine', 'Spine01', 'neck', True),
    ('neck', 'Spine', 'Head', True),
    ('Head', 'neck', 'head_end', True),
    ('head_end', 'Head', None, False),
    ('headfront', 'Head', None, False),
]
for S in ('Left', 'Right'):
    BONES += [
        (S + 'Shoulder', 'Spine', S + 'Arm', True),
        (S + 'Arm', S + 'Shoulder', S + 'ForeArm', True),
        (S + 'ForeArm', S + 'Arm', S + 'Hand', True),
        (S + 'Hand', S + 'ForeArm', S + 'HandMiddle1', True),
        (S + 'UpLeg', 'Hips', S + 'Leg', True),
        (S + 'Leg', S + 'UpLeg', S + 'Foot', True),
        (S + 'Foot', S + 'Leg', S + 'ToeBase', True),
        (S + 'ToeBase', S + 'Foot', S + 'ToeEnd', True),
    ]
    if S + 'HandMiddle1' in J:
        for f in FINGERS:
            for i in range(1, 5):
                BONES.append(('%sHand%s%d' % (S, f, i), S + 'Hand' if i == 1 else '%sHand%s%d' % (S, f, i - 1),
                              '%sHand%s%d' % (S, f, i + 1) if i < 4 else None, i < 4))
    else:
        BONES = [b if b[0] != S + 'Hand' else (S + 'Hand', S + 'ForeArm', None, True) for b in BONES]

arm_data = bpy.data.armatures.new('Armature')
arm = bpy.data.objects.new('Armature', arm_data)
scene.collection.objects.link(arm)
for o in scene.objects:
    o.select_set(False)
vl.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
eb = {}
for name, parent, tail, deform in BONES:
    b = arm_data.edit_bones.new(name)
    hd = Vector(J[name])
    if tail and tail in J:
        tl = Vector(J[tail])
    elif parent:
        d_ = hd - Vector(J[parent])
        tl = hd + d_.normalized() * max(0.01, 0.3 * d_.length)
    else:
        tl = hd + Vector((0, 0, 0.1))
    if (tl - hd).length < 1e-3:
        tl = hd + Vector((0, 0, 0.01))
    b.head, b.tail = hd, tl
    b.use_deform = deform
    b.use_connect = False
    if parent:
        b.parent = eb[parent]
    eb[name] = b
bpy.ops.object.mode_set(mode='OBJECT')
deform_bones = [b[0] for b in BONES if b[3]]
log('huesos', len(BONES), 'que deforman', len(deform_bones))

# ----------------------------------------------------------------------------------------------------
# Pesos: calor de Blender por pieza, con los huesos que le tocan a cada una
# ----------------------------------------------------------------------------------------------------
def side_bones(S, parts):
    return [S + p for p in parts]


FING_BONES = {S: ['%sHand%s%d' % (S, f, i) for f in FINGERS for i in (1, 2, 3) if '%sHand%s%d' % (S, f, i) in J] for S in ('Left', 'Right')}
BODY_BONES = ['Hips', 'Spine02', 'Spine01', 'Spine', 'neck', 'Head'] + \
    [b for S in ('Left', 'Right') for b in side_bones(S, ['Shoulder', 'Arm', 'ForeArm', 'UpLeg', 'Leg'])]
ALLOWED = {
    'cuerpo': BODY_BONES + ([] if islands_of('manoI') else ['LeftHand'] + FING_BONES['Left'])
    + ([] if islands_of('manoD') else ['RightHand'] + FING_BONES['Right'])
    + ([] if islands_of('pieI') else ['LeftFoot', 'LeftToeBase']) + ([] if islands_of('pieD') else ['RightFoot', 'RightToeBase']),
    'manoI': ['LeftForeArm', 'LeftHand'] + FING_BONES['Left'],
    'manoD': ['RightForeArm', 'RightHand'] + FING_BONES['Right'],
    'pieI': ['LeftLeg', 'LeftFoot', 'LeftToeBase'],
    'pieD': ['RightLeg', 'RightFoot', 'RightToeBase'],
    'otra': deform_bones,
}
bidx = {n: i for i, n in enumerate(deform_bones)}
W = np.zeros((nv, len(deform_bones)))

for k_ in isl_ids:
    V = np.where(isl == k_)[0]
    if kind[k_] == 'cabeza':
        # El casco es rígido; si la pieza trae el cuello (un tubo angosto que baja adentro del cuello de la
        # campera), éste pasa de la columna al cuello y del cuello a la cabeza según la altura: rígido con
        # la cabeza, al girarla el tubo salía por el cuello de la campera.
        J0 = J['neck']
        r = np.hypot(co[V, 0] - J0[0], co[V, 1] - J0[1])
        zN, zH = J['neck'][2], J['Head'][2]
        isNeck = (r < 0.036 * H) & (co[V, 2] < zH + 0.015 * H)

        def sstep(e0, e1, x):
            t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
            return t * t * (3 - 2 * t)
        s1 = sstep(zN - 0.04, zN, co[V, 2])
        s2 = sstep(zN + 0.01, zH + 0.01, co[V, 2])
        W[V, bidx['Head']] = np.where(isNeck, s1 * s2, 1.0)
        W[V, bidx['neck']] = np.where(isNeck, s1 * (1 - s2), 0.0)
        W[V, bidx['Spine']] = np.where(isNeck, 1 - s1, 0.0)
        log('pesos cabeza %5d vértices, %d del cuello' % (len(V), isNeck.sum()))
        continue
    allowed = ALLOWED[kind[k_]]
    remap = -np.ones(nv, int)
    remap[V] = np.arange(len(V))
    T = tri[triIsl == k_]
    m = bpy.data.meshes.new('isla')
    m.from_pydata(co[V].tolist(), [], remap[T].tolist())
    o = bpy.data.objects.new('isla', m)
    scene.collection.objects.link(o)
    for b in arm_data.bones:
        b.use_deform = b.name in allowed
    for oo in scene.objects:
        oo.select_set(False)
    o.select_set(True)
    arm.select_set(True)
    vl.objects.active = arm
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    got = 0
    for g in o.vertex_groups:
        if g.name not in bidx:
            continue
        for i in range(len(V)):
            try:
                w = g.weight(i)
            except RuntimeError:
                continue
            W[V[i], bidx[g.name]] = w
            got += 1
    # vértices sin peso (el calor no encontró solución): al hueso permitido más cercano
    empty = V[W[V].sum(1) < 1e-6]
    if len(empty):
        segs = [(bidx[n], np.array(arm_data.bones[n].head_local), np.array(arm_data.bones[n].tail_local)) for n in allowed]
        for v in empty:
            best = None
            for bi, h0, t0 in segs:
                d_ = t0 - h0
                u = np.clip((co[v] - h0) @ d_ / max(d_ @ d_, 1e-12), 0, 1)
                dd = np.linalg.norm(co[v] - (h0 + u * d_))
                if best is None or dd < best[0]:
                    best = (dd, bi)
            W[v, best[1]] = 1.0
    log('pesos %-6s %5d vértices, %d sin solución del calor' % (kind[k_], len(V), len(empty)))
    bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.meshes.remove(m)
for b in arm_data.bones:
    b.use_deform = b.name in bidx

# Hasta 4 huesos por vértice (raylib lee JOINTS_0/WEIGHTS_0), sin pesos chicos, normalizado.
order = np.argsort(-W, axis=1)
W4 = np.zeros_like(W)
for v in range(nv):
    top = order[v, :4]
    w = W[v, top]
    w[w < 0.02] = 0.0
    if w.sum() < 1e-6:
        w[0] = 1.0
    W4[v, top] = w / w.sum()
W = W4

# Revisión de los dedos: ningún vértice de un dedo con peso de otro dedo.
for S in ('Left', 'Right'):
    h = HAND.get(S)
    if not h or 'branches' not in h:
        continue
    for f, m in h['branches'].items():
        if not m['mem']:
            continue
        others = [bidx['%sHand%s%d' % (S, g, i)] for g in FINGERS if g != f for i in (1, 2, 3)]
        bleed = W[m['mem']][:, others].sum(1).max()
        own = [bidx['%sHand%s%d' % (S, f, i)] for i in (1, 2, 3)]
        mine = W[m['mem']][:, own].sum(1).min()
        log('%s %-6s: peso de otros dedos máx %.3f, propio mín %.3f' % (S, f, bleed, mine))
        if bleed > 0.0:    # el calor se pasa entre dedos vecinos: ese peso va a la falange propia del mismo nivel
            M = np.array(m['mem'])
            for g in FINGERS:
                if g == f:
                    continue
                for i in (1, 2, 3):
                    W[M, bidx['%sHand%s%d' % (S, f, i)]] += W[M, bidx['%sHand%s%d' % (S, g, i)]]
                    W[M, bidx['%sHand%s%d' % (S, g, i)]] = 0.0

if opts.get('malla'):
    np.savez(opts['malla'], co=co, tri=tri, isl=isl, W=W, bones=np.array(deform_bones),
             kinds=np.array([kind[k_] for k_ in isl]))

# ----------------------------------------------------------------------------------------------------
# Pasar los pesos a la malla original (vértices repetidos en las costuras: mismo lugar, mismos pesos)
# ----------------------------------------------------------------------------------------------------
kd = kdtree.KDTree(nv)
for i, p in enumerate(co):
    kd.insert(p, i)
kd.balance()
groups = {n: obj.vertex_groups.new(name=n) for n in deform_bones}
maxd = 0.0
for vi, p in enumerate(co_orig):
    _, j, dd = kd.find(p)
    maxd = max(maxd, dd)
    for bi in np.nonzero(W[j])[0]:
        groups[deform_bones[bi]].add([vi], float(W[j, bi]), 'REPLACE')
log('pesos pasados a la malla original (distancia máx %.2g)' % maxd)
obj.parent = arm
mod = obj.modifiers.new('Armature', 'ARMATURE')
mod.object = arm
obj.name = os.path.splitext(os.path.basename(OUT))[0]
me.name = obj.name

# ----------------------------------------------------------------------------------------------------
# Material: sólo la textura de color, en PNG (raylib no lee JPG y el juego no usa las otras)
# ----------------------------------------------------------------------------------------------------
base = os.path.splitext(OUT)[0]
deps = base + '_deps'
tmp = base + '_tmp'
os.makedirs(tmp, exist_ok=True)
for mat in me.materials:
    if not mat or not mat.use_nodes:
        continue
    nt = mat.node_tree
    bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if not bsdf:
        continue
    color_img = None
    for ln in list(nt.links):
        if ln.to_node == bsdf and ln.to_socket.name == 'Base Color' and ln.from_node.type == 'TEX_IMAGE':
            color_img = ln.from_node.image
    for ln in list(nt.links):
        if ln.to_node == bsdf and ln.to_socket.name != 'Base Color':
            nt.links.remove(ln)
    for n in list(nt.nodes):
        if n.type in ('NORMAL_MAP', 'SEPARATE_COLOR', 'SEPRGB') or (n.type == 'TEX_IMAGE' and n.image != color_img):
            nt.nodes.remove(n)
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = 0.8
    if color_img:
        color_img.filepath_raw = os.path.join(tmp, obj.name + '.png')
        color_img.file_format = 'PNG'
        color_img.save()
        color_img.name = obj.name
        log('textura de color:', color_img.size[0], 'x', color_img.size[1])

# ----------------------------------------------------------------------------------------------------
# Exportar y acomodar como el modelo anterior: salida.gltf + salida_deps/(.bin, .png)
# ----------------------------------------------------------------------------------------------------
for oo in scene.objects:
    oo.select_set(oo in (obj, arm))
gl = os.path.join(tmp, os.path.basename(OUT))
bpy.ops.export_scene.gltf(filepath=gl, export_format='GLTF_SEPARATE', use_selection=True, export_yup=True,
                          export_skins=True, export_animations=False, export_def_bones=False, export_influence_nb=4,
                          export_rest_position_armature=True, export_normals=True, export_texcoords=True,
                          export_tangents=False, export_image_format='AUTO', export_materials='EXPORT',
                          export_morph=False)
g = json.load(open(gl, encoding='utf-8'))
os.makedirs(deps, exist_ok=True)
dname = os.path.basename(deps)
for bf in g.get('buffers', []):
    src = os.path.join(tmp, bf['uri'])
    shutil.move(src, os.path.join(deps, os.path.basename(bf['uri'])))
    bf['uri'] = dname + '/' + os.path.basename(bf['uri'])
for im in g.get('images', []):
    src = os.path.join(tmp, im['uri'])
    dst = obj.name + '.image-0.png' if len(g['images']) == 1 else os.path.basename(im['uri'])
    shutil.move(src, os.path.join(deps, dst))
    im['uri'] = dname + '/' + dst
g['asset']['generator'] = 'MotoSim tools/riggear_piloto.py (Blender %s)' % bpy.app.version_string
json.dump(g, open(OUT, 'w', encoding='utf-8'), indent=1)
shutil.rmtree(tmp, ignore_errors=True)
skin = g['skins'][0]
log('listo:', OUT, '| huesos', len(skin['joints']), '| vértices', g['accessors'][g['meshes'][0]['primitives'][0]['attributes']['POSITION']]['count'])
