"""Genera mods/base/maps/autopista.json: la Autopista del Atlántico (generador "highway" con tráfico).

El trazado se arma como rectas y arcos (radios de 600 a 1100 m, curvas en S suaves) y la vuelta se cierra sola
(se resuelven los largos de dos rectas). De ahí salen los puntos de control cada ~40 m, la largada, los puentes
de los cruces (en rectas, lejos de las curvas y entre sí) y sus terraplenes como formas del terreno ("line" a lo
ancho de la autopista: la calzada los corta y el generador pone estribos, pila y tablero). Ver docs/MAPAS.md,
"Autopista".

Uso: python tools/autopista.py [--info]
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'mods', 'base', 'maps', 'autopista.json')

# yaw: 0 = +z (norte), 90 = +x. Un arco con grados > 0 hace crecer el yaw.
SEGS = [
    ('S', 250),
    ('A', 900, -22), ('S', 250), ('A', 900, 22),
    ('S', 300),
    ('A', 650, -90),
    ('S', 450),
    ('A', 1100, 18), ('S', 300), ('A', 1100, -18),
    ('S', 400),
    ('A', 600, -90),
    ('S', 300),
    ('A', 1000, -28), ('A', 1000, 28),
    ('S', 600),
    ('A', 750, -90),
    ('S', 1000),
    ('A', 700, -45), ('S', 250), ('A', 700, -45),
    ('S', 350),
]
CLOSE = (15, 17)          # rectas cuyo largo se ajusta para cerrar la vuelta
YAW0 = 90.0


def build(segs, start=(0.0, 0.0), step=2.0):
    """Polilínea cada ~step m: lista de (x, z, yaw en radianes, curvatura con signo del yaw)."""
    x, z = start
    yaw = math.radians(YAW0)
    pts = [(x, z, yaw, 0.0)]
    for sg in segs:
        if sg[0] == 'S':
            n = max(1, int(round(sg[1] / step)))
            d = sg[1] / n
            for _ in range(n):
                x += math.sin(yaw) * d
                z += math.cos(yaw) * d
                pts.append((x, z, yaw, 0.0))
        else:
            R, deg = sg[1], sg[2]
            L = R * math.radians(abs(deg))
            n = max(1, int(round(L / step)))
            dth = math.radians(deg) / n
            d = 2 * R * math.sin(abs(dth) / 2)
            for _ in range(n):
                yaw += dth / 2
                x += math.sin(yaw) * d
                z += math.cos(yaw) * d
                yaw += dth / 2
                pts.append((x, z, yaw, (1 if deg > 0 else -1) / R))
    return pts


def close(segs):
    i1, i2 = CLOSE

    def end(a, b):
        s = list(segs)
        s[i1] = ('S', a)
        s[i2] = ('S', b)
        p = build(s)
        return p[-1][0], p[-1][1]
    a0, b0 = segs[i1][1], segs[i2][1]
    e0 = end(a0, b0)
    ea = end(a0 + 100, b0)
    eb = end(a0, b0 + 100)
    m = [[(ea[0] - e0[0]) / 100, (eb[0] - e0[0]) / 100], [(ea[1] - e0[1]) / 100, (eb[1] - e0[1]) / 100]]
    det = m[0][0] * m[1][1] - m[0][1] * m[1][0]
    da = (-e0[0] * m[1][1] + e0[1] * m[0][1]) / det
    db = (-e0[1] * m[0][0] + e0[0] * m[1][0]) / det
    s = list(segs)
    s[i1] = ('S', a0 + da)
    s[i2] = ('S', b0 + db)
    return s


def main():
    segs = close(SEGS)
    pts = build(segs)
    xs = [p[0] for p in pts]
    zs = [p[1] for p in pts]
    cx, cz = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2
    pts = [(p[0] - cx, p[1] - cz, p[2], p[3]) for p in pts]
    cum = [0.0]
    for a, b in zip(pts, pts[1:]):
        cum.append(cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = cum[-1]
    # Puntos de control cada ~40 m (la Catmull-Rom del juego los une casi igual que los arcos).
    ctrl = []
    nxt = 0.0
    for p, s in zip(pts[:-1], cum[:-1]):
        if s >= nxt:
            ctrl.append([round(p[0], 1), round(p[1], 1)])
            nxt = s + 40.0
    ctrl[0].append('asphalt')

    # Rectas (curvatura 0 en ±250 m) para los puentes y la largada.
    def straight_at(i, half=250.0):
        s0 = cum[i]
        j = i
        while j > 0 and s0 - cum[j] < half:
            if pts[j][3] != 0.0:
                return False
            j -= 1
        j = i
        while j < len(pts) - 1 and cum[j] - s0 < half:
            if pts[j][3] != 0.0:
                return False
            j += 1
        return True
    # Puentes: cada ~2 km de vuelta, el punto de recta más cercano a esa marca.
    bridges, shapes = [], []
    want = [1300 + k * 2100 for k in range(6)]
    for w in want:
        best = None
        for i in range(0, len(pts), 5):
            if not straight_at(i, 120):
                continue
            d = abs(cum[i] - w)
            if best is None or d < best[0]:
                best = (d, i)
        if best is None or best[0] > 700:
            continue
        i = best[1]
        if any(abs(cum[i] - cum[b]) < 1200 for b in [bb[2] for bb in bridges]):
            continue
        x, z, yaw = pts[i][0], pts[i][1], pts[i][2]
        tx, tz = math.sin(yaw), math.cos(yaw)
        # Perpendicular (la derecha del trazado en el juego: (-tz, tx)), como rumbo de la forma: atan2(x, z).
        rx, rz = -tz, tx
        yaw_deg = math.degrees(math.atan2(rx, rz))
        bridges.append([round(x, 1), round(z, 1), i])
        shapes.append({"shape": "line", "at": [round(x, 1), round(z, 1)], "yaw": round(yaw_deg, 2), "width": 11, "edge": 16, "smooth": 8,
                       "profile": [[-210, 0], [-45, 8.6], [45, 8.6], [210, 0]]})

    # Largada: en una recta, a más de 600 m de los puentes (la grilla de autos de la vuelta no la tapa nada).
    def far_from_bridges(i):
        return all(min(abs(cum[i] - cum[b[2]]), total - abs(cum[i] - cum[b[2]])) > 600 for b in bridges)
    start_i = next(i for i in range(len(pts)) if cum[i] > 100 and straight_at(i, 150) and far_from_bridges(i))
    start = [round(pts[start_i][0], 1), round(pts[start_i][1], 1)]

    doc = {
        "name": "Autopista del Atlántico",
        "kind": "ruta · tráfico",
        "description": "Doce kilómetros de autopista, tres carriles por mano y tráfico de verdad: pasá entre los autos a fondo.",
        "hint": "Los autos frenan si te ven adelante y ponen el guiño antes de cambiar de carril  ·  chocar o rozar un auto te tira  ·  cada 2 km hay un cruce del separador para pasarse a la otra mano",
        "bike": "base/carrera",
        "color": "#43a047",
        "order": 4,
        "terrain": {"type": "hills", "height": 26, "scale": 0.0011, "detail": 0.2, "size": 4096, "resolution": 2.5, "edge": 10,
                    "shapes": shapes},
        "generator": "highway",
        "markers": False,
        "racing_line": False,
        "track": {"style": "street", "width": 32.6, "banking": False, "smooth": 160, "shoulder": 9, "superelevation": 0.05,
                  "superelevation_radius": 700, "points": ctrl},
        "start": start,
        "highway": {
            "name": "AUTOPISTA DEL ATLÁNTICO",
            "lanes": 3, "lane_width": 3.6, "median": 3.0, "inner_shoulder": 1.0, "outer_shoulder": 3.0,
            "lights": 50, "gantries": 2400, "crossovers": 2000, "crossover_length": 40, "trees": 1.0,
            "bridges": [[b[0], b[1]] for b in bridges],
            "signs": ["MAR DEL PLATA  312", "SALIDA 23  CHASCOMÚS", "PINAMAR  341", "SALIDA 31  RANCHOS", "DOLORES  158", "SALIDA 12  BRANDSEN",
                      "LA PLATA  42", "SALIDA 40  LEZAMA", "BUENOS AIRES  87", "SALIDA 47  CASTELLI", "TANDIL  360", "SALIDA 58  GRAL. GUIDO"],
        },
        "traffic": {"density": 11, "speed": [90, 140], "lane_change": 14, "trucks": 0.12, "seed": 7},
        "bot_speed": 45,
        "bot_lateral": 3.5,
        "bot_brake": 5,
        "look": {"preset": "day", "ground_textures": "circuit", "fog": 0.0017},
    }
    if '--info' in sys.argv:
        print('largo %.0f m, %d puntos, %d puentes, largada %s (s=%.0f), x %.0f..%.0f z %.0f..%.0f' % (
            total, len(ctrl), len(bridges), start, cum[start_i], min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts),
            max(p[1] for p in pts)))
        print('rectas:', [round(s[1]) for s in segs if s[0] == 'S'])
        for b in bridges:
            print('puente en s=%.0f (%.0f, %.0f)' % (cum[b[2]], b[0], b[1]))
    header = ("// Autopista del Atlántico: a 12 km dual carriageway loop with traffic, for the race bike. Written by\n"
              "// tools/autopista.py (the layout is straights and arcs; edit the script, not this file). The \"highway\"\n"
              "// generator builds the median barrier, guardrails, lane lines, lights, gantries, signs and the overpasses\n"
              "// (their embankments are the terrain shapes below); the \"traffic\" section drives the cars. See MODDING.md.\n")
    text = json.dumps(doc, ensure_ascii=False, indent=1)
    # Los puntos de a varios por renglón (si no, el archivo tiene miles de renglones).
    import re
    text = re.sub(r'\[\n\s+(-?[\d.]+),\n\s+(-?[\d.]+)(,\n\s+"asphalt")?\n\s+\]',
                  lambda m: '[' + m.group(1) + ', ' + m.group(2) + (', "asphalt"' if m.group(3) else '') + ']', text)
    with open(OUT, 'wb') as f:
        f.write((header + text + '\n').encode('utf-8'))
    print('escrito', os.path.normpath(OUT))


if __name__ == '__main__':
    main()
