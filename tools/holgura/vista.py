"""Mira un volcado de la medición de holgura (RiderClearance, MOTOSIM_HOLGURA_VOLCAR).

Uso: python tools/holgura/vista.py volcado.bin salida.png [--vistas izq,der,frente,arriba] [--zoom cx cy cz tam] [--ppm 400] [--titulo txt]
       [--corte eje valor]  (sólo lo que queda de un lado: x<valor, etc.)

Dibuja la moto (gris) y el piloto (colores por zona) con el algoritmo del pintor, en proyección ortogonal y
en espacio de la moto (+X izquierda, +Y arriba, +Z adelante). Encima, en rayos X: en rojo los vértices del
piloto adentro de la moto (A) y en amarillo los puntos de la moto adentro del piloto (B), más grandes cuanto
más adentro.
"""
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

REGION_COLORS = [
    (200, 200, 210),  # casco
    (200, 60, 70),    # torso
    (160, 50, 120),   # cadera
    (220, 120, 60), (220, 120, 60),    # brazos
    (230, 170, 60), (230, 170, 60),    # antebrazos
    (90, 90, 100), (90, 90, 100),      # manos
    (60, 120, 200), (60, 120, 200),    # muslos
    (70, 170, 170), (70, 170, 170),    # canillas
    (60, 60, 60), (60, 60, 60),        # botas
]
PART_COLORS = {0: (150, 155, 165), 1: (120, 120, 120), 5: (170, 170, 175), 6: (110, 110, 118), 7: (140, 150, 160),
               8: (175, 160, 150), 11: (70, 70, 70), 12: (70, 70, 70)}


def load(path):
    b = open(path, 'rb').read()
    nv, nt, nbt, nb = struct.unpack_from('4i', b, 0)
    o = 16
    def take(dtype, n):
        nonlocal o
        a = np.frombuffer(b, dtype=dtype, count=n, offset=o)
        o += a.nbytes
        return a
    rv = take(np.float32, nv * 3).reshape(nv, 3)
    rt = take(np.int32, nt * 3).reshape(nt, 3)
    reg = take(np.int32, nv)
    dA = take(np.float32, nv)
    bt = take(np.float32, nbt * 9).reshape(nbt, 3, 3)
    bp = take(np.int32, nbt)
    B = take(np.float32, nb * 4).reshape(nb, 4)
    return rv, rt, reg, dA, bt, bp, B


VIEWS = {
    # nombre: (eje x de pantalla, eje y de pantalla, profundidad hacia la cámara)
    'izq': (np.array([0, 0, -1.0]), np.array([0, 1.0, 0]), np.array([1.0, 0, 0])),
    'der': (np.array([0, 0, 1.0]), np.array([0, 1.0, 0]), np.array([-1.0, 0, 0])),
    'frente': (np.array([1.0, 0, 0]), np.array([0, 1.0, 0]), np.array([0, 0, 1.0])),
    'atras': (np.array([-1.0, 0, 0]), np.array([0, 1.0, 0]), np.array([0, 0, -1.0])),
    'arriba': (np.array([-1.0, 0, 0]), np.array([0, 0, 1.0]), np.array([0, 1.0, 0])),
}


def render(data, view, center, size, ppm, cut=None):
    rv, rt, reg, dA, bt, bp, B = data
    ex, ey, ez = VIEWS[view]
    W = H = int(size * ppm)
    img = Image.new('RGB', (W, H), (235, 238, 242))
    dr = ImageDraw.Draw(img)
    light = np.array([0.3, 0.8, 0.5]); light /= np.linalg.norm(light)

    def scr(p):
        d = p - center
        return (W / 2 + (d @ ex) * ppm, H / 2 - (d @ ey) * ppm, d @ ez)

    tris = []
    def keep(pts):
        if cut is None:
            return True
        axis, val, side = cut
        c = pts.mean(axis=0)[axis]
        return c < val if side < 0 else c > val
    for t in range(len(bt)):
        pts = bt[t]
        if not keep(pts):
            continue
        n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
        ln = np.linalg.norm(n)
        if ln < 1e-12:
            continue
        shade = 0.45 + 0.55 * abs(n @ light) / ln
        col = PART_COLORS.get(int(bp[t]), (150, 150, 150))
        tris.append((pts, tuple(int(c * shade) for c in col)))
    for t in range(len(rt)):
        pts = rv[rt[t]]
        if not keep(pts):
            continue
        n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
        ln = np.linalg.norm(n)
        if ln < 1e-12:
            continue
        shade = 0.45 + 0.55 * abs(n @ light) / ln
        col = REGION_COLORS[min(int(reg[rt[t][0]]), len(REGION_COLORS) - 1)]
        tris.append((pts, tuple(int(c * shade) for c in col)))
    proj = []
    for pts, col in tris:
        s = [scr(p) for p in pts]
        depth = sum(q[2] for q in s) / 3
        proj.append((depth, [(q[0], q[1]) for q in s], col))
    proj.sort(key=lambda x: x[0])
    for depth, poly, col in proj:
        dr.polygon(poly, fill=col)
    # Rayos X: lo que se mete.
    for i in np.nonzero(dA > 0.003)[0]:
        if cut is not None and not keep(rv[i][None]):
            continue
        x, y, _ = scr(rv[i])
        r = 1.5 + 60 * float(dA[i])
        dr.ellipse([x - r, y - r, x + r, y + r], fill=(230, 20, 20))
    for p in B:
        if cut is not None and not keep(p[None, :3]):
            continue
        if p[3] < 0.003:
            continue
        x, y, _ = scr(p[:3])
        r = 1.0 + 40 * float(p[3])
        dr.ellipse([x - r, y - r, x + r, y + r], fill=(250, 210, 0))
    dr.text((6, 4), view, fill=(0, 0, 0))
    return img


def main():
    args = sys.argv[1:]
    src, out = args[0], args[1]
    views = ['izq', 'der', 'frente', 'arriba']
    center = None
    size = 2.0
    ppm = 400
    title = ''
    cut = None
    i = 2
    while i < len(args):
        if args[i] == '--vistas':
            views = args[i + 1].split(','); i += 2
        elif args[i] == '--zoom':
            center = np.array([float(v) for v in args[i + 1:i + 4]]); size = float(args[i + 4]); i += 5
        elif args[i] == '--ppm':
            ppm = float(args[i + 1]); i += 2
        elif args[i] == '--titulo':
            title = args[i + 1]; i += 2
        elif args[i] == '--corte':
            ax = 'xyz'.index(args[i + 1][-1]); side = -1 if args[i + 1][0] == '-' else 1
            cut = (ax, float(args[i + 2]), side); i += 3
        else:
            raise SystemExit('argumento desconocido: ' + args[i])
    data = load(src)
    if center is None:
        center = np.array([0.0, 0.35, 0.05])
    imgs = [render(data, v, center, size, ppm, cut) for v in views]
    cols = 2 if len(imgs) > 1 else 1
    rows = (len(imgs) + cols - 1) // cols
    w, h = imgs[0].size
    sheet = Image.new('RGB', (w * cols, h * rows + (24 if title else 0)), (255, 255, 255))
    for k, im in enumerate(imgs):
        sheet.paste(im, ((k % cols) * w, (k // cols) * h + (24 if title else 0)))
    if title:
        ImageDraw.Draw(sheet).text((8, 4), title, fill=(0, 0, 0))
    sheet.save(out)


if __name__ == '__main__':
    main()
