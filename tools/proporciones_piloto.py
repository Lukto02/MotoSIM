# Cambia el largo de los brazos del modelo del piloto (glTF con esqueleto estilo Mixamo) sin tocar la textura, las
# UV, los pesos, los triángulos ni la jerarquía. Ver docs/PILOTO.md ("Brazos más largos: tools/proporciones_piloto.py").
#
# Uso:
#   python tools/proporciones_piloto.py entrada.gltf                      mide (largos de cada hueso, alto, envergadura)
#   python tools/proporciones_piloto.py entrada.gltf salida.gltf [--brazo 0.31] [--antebrazo 0.26] [--suave 0.02]
#     --brazo      m del hombro (LeftArm) al codo (LeftForeArm), los dos lados iguales
#     --antebrazo  m del codo a la muñeca (LeftHand), los dos lados iguales
#     --suave      m a cada lado de una articulación en que el estiramiento pasa de 0 al del hueso (sin quiebre)
#
# Cómo (todo en la pose de reposo, en metros; el juego usa las posiciones de reposo de los huesos y los vértices):
#   - Huesos: se alarga la traslación local del codo y de la muñeca (va a lo largo del hueso padre): el codo y la mano
#     se corren a lo largo del brazo y del antebrazo, sin girar nada. Las matrices de bind de esos huesos (y de los que
#     cuelgan de ellos) se corrigen con la misma traslación (IBM' = IBM · T(-d)), y las traslaciones de esos huesos en
#     las animaciones guardadas (Walking, Running; el juego no las usa) se alargan igual.
#   - Vértices: cada hueso tiene un "cambio de reposo" (una función del espacio): el brazo estira a lo largo de su eje
#     lo que está entre el hombro y el codo (afuera de eso, nada o lo que se corrió el codo); el antebrazo se corre con
#     el codo y estira entre el codo y la muñeca; la mano y lo que cuelga de ella se corren enteros; el resto queda
#     quieto. Cada vértice se mueve con el promedio de esas funciones con sus pesos (como el skinning): en el codo y el
#     hombro, donde los pesos se mezclan, el paso es suave. El grosor del brazo no cambia (sólo se estira a lo largo).
#     Las normales, con la inversa transpuesta de cada estiramiento, y normalizadas.
#   - El .bin se reescribe con los mismos accesores y tamaños (se cambian sólo POSITION, NORMAL, las matrices de bind
#     y las traslaciones de esas animaciones; el min/max de POSITION se actualiza). La textura se copia byte a byte.
import hashlib
import json
import os
import shutil
import sys

import numpy as np

CT = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
SIDES = ('Left', 'Right')


def qmat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


class Gltf:
    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.g = json.load(open(self.path, encoding='utf-8'))
        if len(self.g['meshes']) != 1 or len(self.g['meshes'][0]['primitives']) != 1 or len(self.g['buffers']) != 1:
            sys.exit('se espera una malla con una primitiva y un buffer (como el modelo 2)')
        self.base = os.path.dirname(self.path)
        self.buf = bytearray(open(os.path.join(self.base, self.g['buffers'][0]['uri']), 'rb').read())
        self.parent = {}
        for i, n in enumerate(self.g['nodes']):
            for c in n.get('children', []):
                self.parent[c] = i
        self.byname = {n.get('name'): i for i, n in enumerate(self.g['nodes'])}
        self.prim = self.g['meshes'][0]['primitives'][0]
        self.skin = self.g['skins'][0]

    def _view(self, i):
        a = self.g['accessors'][i]
        bv = self.g['bufferViews'][a['bufferView']]
        if bv.get('byteStride') or 'sparse' in a:
            sys.exit('accesor intercalado o disperso: no soportado')
        return a, bv.get('byteOffset', 0) + a.get('byteOffset', 0)

    def read(self, i):
        a, off = self._view(i)
        n = NC[a['type']]
        arr = np.frombuffer(bytes(self.buf), CT[a['componentType']], a['count'] * n, off).copy()
        return arr.reshape(a['count'], n) if n > 1 else arr

    def write(self, i, arr):
        a, off = self._view(i)
        data = np.ascontiguousarray(arr, dtype=CT[a['componentType']]).tobytes()
        assert len(data) == a['count'] * NC[a['type']] * np.dtype(CT[a['componentType']]).itemsize
        self.buf[off:off + len(data)] = data

    def local(self, i):
        n = self.g['nodes'][i]
        if 'matrix' in n:
            return np.array(n['matrix'], float).reshape(4, 4).T
        m = np.eye(4)
        m[:3, :3] = qmat(n.get('rotation', [0, 0, 0, 1])) @ np.diag(n.get('scale', [1, 1, 1]))
        m[:3, 3] = n.get('translation', [0, 0, 0])
        return m

    def world(self, i):
        m = self.local(i)
        return self.world(self.parent[i]) @ m if i in self.parent else m

    def pos(self, name):
        return self.world(self.byname[name])[:3, 3]

    def descendants(self, i):
        out = [i]
        for c in self.g['nodes'][i].get('children', []):
            out += self.descendants(c)
        return out


def measure(m, title):
    d = lambda a, b: float(np.linalg.norm(m.pos(a) - m.pos(b)))
    P = m.read(m.prim['attributes']['POSITION']).astype(float)
    J = m.read(m.prim['attributes']['JOINTS_0']).astype(int)
    W = m.read(m.prim['attributes']['WEIGHTS_0']).astype(float)
    jn = [m.g['nodes'][j]['name'] for j in m.skin['joints']]
    top = P[:, 1].max()
    print(f'{title}: alto {top:.3f} m, {len(m.read(m.prim["indices"])) // 3} triángulos, {len(P)} vértices')
    print(f'  cadera (Hips) {m.pos("Hips")[1]:.3f}, cuello {m.pos("neck")[1]:.3f}, cabeza {m.pos("Head")[1]:.3f}')
    for s in SIDES:
        hv = P[np.array(jn)[J[np.arange(len(J)), np.argmax(W, 1)]] == s + 'Hand']          # el hueso que más pesa: la mano
        fore = m.pos(s + 'Hand') - m.pos(s + 'ForeArm')
        hand = float(((hv - m.pos(s + 'Hand')) @ (fore / np.linalg.norm(fore))).max()) if len(hv) else 0.0
        arm, forearm = d(s + 'Arm', s + 'ForeArm'), d(s + 'ForeArm', s + 'Hand')
        print(f'  {s:5s}: hombro a {abs(m.pos(s + "Arm")[0]):.3f} del centro y {m.pos(s + "Arm")[1]:.3f} de alto; brazo {arm:.3f} + '
              f'antebrazo {forearm:.3f} = {arm + forearm:.3f} ({100 * (arm + forearm) / top:.1f}% del alto); mano {hand:.3f}; '
              f'colgando, la punta de los dedos a {m.pos(s + "Arm")[1] - arm - forearm - hand:.3f}; '
              f'envergadura (los dos así) {2 * (abs(m.pos(s + "Arm")[0]) + arm + forearm + hand):.3f}')
        print(f'         cadera a {abs(m.pos(s + "UpLeg")[0]):.3f} y {m.pos(s + "UpLeg")[1]:.3f} de alto; muslo {d(s + "UpLeg", s + "Leg"):.3f} + '
              f'canilla {d(s + "Leg", s + "Foot"):.3f} (rodilla a {m.pos(s + "Leg")[1]:.3f}, tobillo a {m.pos(s + "Foot")[1]:.3f})')


def ramp_integral(s, L, r):
    """phi(s): cuánto del hueso quedó atrás de s, con el estiramiento entrando y saliendo suave (ancho 2r) en 0 y L.
    phi = 0 antes del hueso, L después; su derivada (rho) es la fracción de estiramiento en s."""
    def ss_int(x):            # integral de smoothstep(-r, r, t) desde -inf hasta x
        u = np.clip((x + r) / (2 * r), 0.0, 1.0)
        return np.where(x >= r, x, 2 * r * (u ** 3 - 0.5 * u ** 4))
    def ss(x):
        u = np.clip((x + r) / (2 * r), 0.0, 1.0)
        return u * u * (3 - 2 * u)
    if r <= 0:
        return np.clip(s, 0, L), ((s > 0) & (s < L)).astype(float)
    phi = ss_int(s) - ss_int(s - L)
    rho = ss(s) - ss(s - L)
    return phi, rho


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit('uso: python tools/proporciones_piloto.py entrada.gltf [salida.gltf] [--brazo m] [--antebrazo m] [--suave m]')
    pos_args = [a for i, a in enumerate(args) if not a.startswith('--') and (i == 0 or not args[i - 1].startswith('--'))]
    opt = {args[i].lstrip('-'): float(args[i + 1]) for i in range(len(args) - 1) if args[i].startswith('--')}
    m = Gltf(pos_args[0])
    measure(m, 'antes' if len(pos_args) > 1 else 'modelo')
    if len(pos_args) < 2:
        return
    OUT = os.path.abspath(pos_args[1])
    target = {'Arm': opt.get('brazo', 0.31), 'ForeArm': opt.get('antebrazo', 0.26)}
    soft = opt.get('suave', 0.02)

    attrs = m.prim['attributes']
    P = m.read(attrs['POSITION']).astype(np.float64)
    N = m.read(attrs['NORMAL']).astype(np.float64)
    J = m.read(attrs['JOINTS_0']).astype(int)
    W = m.read(attrs['WEIGHTS_0']).astype(np.float64)
    W = W / np.maximum(W.sum(1, keepdims=True), 1e-9)
    joints = m.skin['joints']
    nV = len(P)

    # Cambio de reposo de cada articulación (índice de nodo): desplazamiento y matriz lineal por vértice.
    disp = {j: np.zeros((nV, 3)) for j in joints}
    lin = {j: np.broadcast_to(np.eye(3), (nV, 3, 3)).copy() for j in joints}
    node_shift = {}                         # nodo -> cuánto se corre en el mundo (m)
    scale_local = {}                        # nodo -> factor de su traslación local
    for s in SIDES:
        arm, fore, hand = (m.byname[s + k] for k in ('Arm', 'ForeArm', 'Hand'))
        S, E, Wr = m.pos(s + 'Arm'), m.pos(s + 'ForeArm'), m.pos(s + 'Hand')
        L1, L2 = np.linalg.norm(E - S), np.linalg.norm(Wr - E)
        a1, a2 = (E - S) / L1, (Wr - E) / L2
        k1, k2 = target['Arm'] / L1, target['ForeArm'] / L2
        dE = (k1 - 1) * L1 * a1
        dW = dE + (k2 - 1) * L2 * a2
        scale_local[fore], scale_local[hand] = k1, k2
        for b in m.descendants(fore):
            node_shift[b] = dE
        for b in m.descendants(hand):
            node_shift[b] = dW
        # brazo: estira entre hombro y codo
        phi, rho = ramp_integral((P - S) @ a1, L1, soft)
        if arm in disp:
            disp[arm] = (k1 - 1) * phi[:, None] * a1
            lin[arm] = np.eye(3) + (k1 - 1) * rho[:, None, None] * np.outer(a1, a1)
        # antebrazo: se corre con el codo y estira entre codo y muñeca
        phi, rho = ramp_integral((P - E) @ a2, L2, soft)
        if fore in disp:
            disp[fore] = dE + (k2 - 1) * phi[:, None] * a2
            lin[fore] = np.eye(3) + (k2 - 1) * rho[:, None, None] * np.outer(a2, a2)
        for b in m.descendants(hand):
            if b in disp:
                disp[b] = np.broadcast_to(dW, (nV, 3)).copy()
        print(f'  {s}: brazo x{k1:.3f} (codo {np.linalg.norm(dE) * 100:.1f} cm), antebrazo x{k2:.3f} (muñeca {np.linalg.norm(dW) * 100:.1f} cm)')

    newP = P.copy()
    newN = np.zeros_like(N)
    acc_d = np.zeros((nV, 3))
    acc_n = np.zeros((nV, 3))
    for k in range(4):
        w = W[:, k]
        jid = np.array(joints)[J[:, k]]
        for j in np.unique(jid[w > 0]):
            sel = (jid == j) & (w > 0)
            acc_d[sel] += w[sel, None] * disp[j][sel]
            Ainv_T = np.linalg.inv(lin[j][sel]).transpose(0, 2, 1)
            acc_n[sel] += w[sel, None] * np.einsum('nij,nj->ni', Ainv_T, N[sel])
    newP = P + acc_d
    newN = acc_n / np.maximum(np.linalg.norm(acc_n, axis=1, keepdims=True), 1e-12)

    # Nodos: traslación local de codo y muñeca más larga.
    for b, f in scale_local.items():
        t = np.array(m.g['nodes'][b]['translation'], float)
        m.g['nodes'][b]['translation'] = [float(x) for x in t * f]
    # Matrices de bind: IBM' = IBM · T(-d) (los vértices están en el mismo espacio que el mundo de los huesos).
    ibm = m.read(m.skin['inverseBindMatrices']).astype(np.float64).reshape(-1, 4, 4).transpose(0, 2, 1)
    for k, j in enumerate(joints):
        if j in node_shift:
            T = np.eye(4)
            T[:3, 3] = -node_shift[j]
            ibm[k] = ibm[k] @ T
    m.write(m.skin['inverseBindMatrices'], ibm.transpose(0, 2, 1).reshape(-1, 16))
    # Animaciones guardadas: las traslaciones de esos huesos, con el mismo factor.
    for anim in m.g.get('animations', []):
        for ch in anim['channels']:
            b = ch['target'].get('node')
            if ch['target']['path'] == 'translation' and b in scale_local:
                acc = anim['samplers'][ch['sampler']]['output']
                m.write(acc, m.read(acc).astype(np.float64) * scale_local[b])
    # Malla.
    m.write(attrs['POSITION'], newP)
    m.write(attrs['NORMAL'], newN)
    pa = m.g['accessors'][attrs['POSITION']]
    p32 = newP.astype(np.float32)
    pa['min'] = [float(x) for x in p32.min(0)]
    pa['max'] = [float(x) for x in p32.max(0)]

    # Comprobaciones: el reposo nuevo es coherente (IBM = inversa del mundo) y nada más cambió.
    err = max(np.abs(ibm[k] @ m.world(j) - np.eye(4)).max() for k, j in enumerate(joints))
    moved = np.linalg.norm(newP - P, axis=1)
    print(f'  matrices de bind contra el reposo nuevo: error {err:.1e}; vértices movidos {int((moved > 1e-6).sum())} de {nV} '
          f'(hasta {moved.max() * 100:.1f} cm); normales que giraron más de 5°: '
          f'{int((np.einsum("ij,ij->i", newN, N / np.linalg.norm(N, axis=1, keepdims=True)) < np.cos(np.radians(5))).sum())}')

    m.g['asset'].setdefault('extras', {})['proporciones'] = (
        f"tools/proporciones_piloto.py: brazo {target['Arm']:.3f} m y antebrazo {target['ForeArm']:.3f} m de cada lado (suave {soft:.3f})")
    # Escribir: salida.gltf + salida_deps/ (el .bin y la textura copiada tal cual).
    stem = os.path.splitext(os.path.basename(OUT))[0]
    deps = stem + '_deps'
    out_dir = os.path.dirname(OUT)
    os.makedirs(os.path.join(out_dir, deps), exist_ok=True)
    bin_name = f'{deps}/{stem}.bin'
    m.g['buffers'][0]['uri'] = bin_name
    for k, img in enumerate(m.g.get('images', [])):
        if 'uri' in img:
            src = os.path.join(m.base, img['uri'])
            dst_rel = f'{deps}/{stem}.image-{k}{os.path.splitext(img["uri"])[1]}'
            dst = os.path.join(out_dir, dst_rel)
            if os.path.abspath(src) != os.path.abspath(dst):
                shutil.copyfile(src, dst)
            img['uri'] = dst_rel
            print(f'  textura {dst_rel}: sha256 {hashlib.sha256(open(dst, "rb").read()).hexdigest()[:16]} '
                  f'(original {hashlib.sha256(open(src, "rb").read()).hexdigest()[:16]})')
    open(os.path.join(out_dir, bin_name), 'wb').write(bytes(m.buf))
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(m.g, f, ensure_ascii=False, indent=1)
    measure(Gltf(OUT), 'después')


if __name__ == '__main__':
    main()
