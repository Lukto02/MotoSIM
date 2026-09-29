# Genera el Parque de física (mods/sandbox/maps/park.json, el mod de ejemplo) y sus copias de prueba
# (pruebas/mods/prueba/maps/park_*.json, con una vuelta guía por cada zona para medirla con saltos.py;
# prueba/park es el parque con la trial). Ver docs/MAPAS.md, "Parque de física".
#
#   python tools/parque.py
#
# El parque (v2): la pista de siempre por el borde (la del bot) y adentro un parque de saltos con zonas.
# Todas las líneas se andan hacia el norte: se entra desde la recta de largada (doblando a la izquierda) y
# se sale a la recta de enfrente (otra vez a la izquierda), que es para donde va la vuelta.
#   oeste:  Línea de tierra, tres mesas de tierra (chica, mediana y la de mortales) y al final la pirámide
#           de cajas para atravesar;
#   centro: Línea de madera, tres cajones de madera (rampa, mesa y bajada: se pueden saltar o pasar
#           rodando, para los dos lados) y al final la bolera;
#   este:   el pump track, un óvalo de lomitos con dos peraltes, y al salir el muro de tambores;
#   norte:  la plaza, con el bowl (y las pelotas gigantes adentro: siempre vuelven al fondo) y La Pared,
#           un quarter de tierra con espalda para volver o pasarse.
# Las rampas de madera son cuñas ("ramp") de 10-20°, siempre con su mesa detrás (sin paredes verticales
# a la vista); lo de tierra son formas del terreno ("terrain.shapes"), con curvas.
import math, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True                             # (sin tools/__pycache__ al importar medanos)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from medanos import arc, line, tabletop, berm, P  # noqa: E402  (los mismos perfiles que Los Médanos)


# ------------------------------------------------------------------------------------ zonas
X_TIERRA, X_MADERA = -50.0, 0.0
PUMP = dict(x0=40.0, x1=66.0, z0=-58.0, z1=-8.0)          # rectas del óvalo (oeste sube, este baja) y centros de los peraltes
BOWL_AT, PARED_AT = (-20.0, 58.0), (42.0, 52.0)

# Línea de tierra: (nombre, alto, patada, radio de la cara, mesa, bajada, nudillo, fondo bajo el suelo, radio del fondo,
# m de la S que sale del fondo, llano hasta la cara del siguiente)
TIERRA = [
    ("Mesa chica", 1.2, 20, 8, 4, 18, 6, 0.0, 10, 0, 8),
    ("Mesa mediana", 2.0, 26, 10, 6, 22, 8, 0.0, 12, 0, 8),
    ("La de mortales", 3.6, 34, 16, 10, 30, 8, 1.5, 16, 14, 6),
]
Z_TIERRA = -72.0                                          # donde empieza la cara de la mesa chica


def linea_tierra():
    pts, u0, marks = [(-12.0, 0.0)], 0.0, []
    for name, H, kick, R, table, land, Rk, depth, Rb, out, end in TIERRA:
        p, lip = tabletop(H, kick, R, table, land, Rk, depth, Rb, runout=out, end=end)
        pts += [(u + u0, h) for u, h in p[1:]]          # (el primer punto de la mesa es el último del llano anterior)
        marks.append((name, u0 + lip[0]))
        u0 = pts[-1][0]
    return pts, marks


def bowl(floor=-2.4, r0=5.0, wall=50.0, R=6.0, rim=0.8, lip=1.0, outer=20.0):
    """Perfil radial del bowl: fondo, pared cóncava hasta 'wall' grados, borde a 'rim' m y ladera de afuera."""
    pts = [(0.0, floor), (r0, floor)] + arc(r0, floor, 0, wall, R)
    u, h = pts[-1]
    if h < rim:
        pts += line(u, h, wall, (rim - h) / math.sin(math.radians(wall)))
    pts.append((pts[-1][0] + lip, rim))
    pts += arc(pts[-1][0], rim, 0, -outer, 3.0)
    u, h = pts[-1]
    pts += line(u, h, -outer, max(0.0, h - 4.0 * (1 - math.cos(math.radians(outer)))) / math.sin(math.radians(outer)))
    pts += arc(pts[-1][0], pts[-1][1], -outer, 0, 4.0)
    pts.append((pts[-1][0] + 2.0, 0.0))
    return pts


def pared(H=3.2, wall=62.0, R=5.0, top=1.0, back=24.0, approach=20.0):
    """La Pared: quarter de tierra (cara cóncava hasta 'wall' grados), lomo corto y espalda de 'back' grados."""
    pts = [(-approach, 0.0), (0.0, 0.0)] + arc(0.0, 0.0, 0, wall, R)
    u, h = pts[-1]
    if h < H:
        pts += line(u, h, wall, (H - h) / math.sin(math.radians(wall)))
    pts.append((pts[-1][0] + top, H))
    pts += arc(pts[-1][0], H, 0, -back, 4.0)
    u, h = pts[-1]
    pts += line(u, h, -back, max(0.0, h - 8.0 * (1 - math.cos(math.radians(back)))) / math.sin(math.radians(back)))
    pts += arc(pts[-1][0], pts[-1][1], -back, 0, 8.0)
    pts.append((pts[-1][0] + 4.0, 0.0))
    return pts


def lomitos(H=0.6, lam=6.25, n=8, lead=4.0):
    L = lam * n
    pts = [(-lead, 0.0), (0.0, 0.0)] + [(i * 0.5, H * 0.5 * (1 - math.cos(2 * math.pi * i * 0.5 / lam))) for i in range(1, int(L / 0.5) + 1)]
    return pts + [(L + lead, 0.0)]


def shape(comment, kind, at, profile, **kw):
    body = {"shape": kind, "at": at}
    body.update(kw)
    def val(v):
        return ("true" if v else "false") if isinstance(v, bool) else ('"%s"' % v if isinstance(v, str) else str(v))
    txt = ", ".join('"%s": %s' % (k, val(v)) for k, v in body.items())
    return f"      // {comment}\n      {{{txt},\n       \"profile\": {profile}}}"


def shapes():
    tp, _ = linea_tierra()
    s = [shape("Línea de tierra (west): three dirt tables ridden north, small, medium and the big one for backflips (3.6 m, "
               "34° kick, landing dug 1.5 m into the ground).", "line", [X_TIERRA, Z_TIERRA], P(tp), yaw=0, width=8, edge=6, mode="level",
               base=0, smooth=1.0, dirt=True)]
    for x, yaw, name in ((PUMP["x0"], 0, "west side, ridden north"), (PUMP["x1"], 180, "east side, ridden south")):
        z = PUMP["z0"] if yaw == 0 else PUMP["z1"]
        s.append(shape("Pump track (%s): eight 0.6 m rollers, 6.25 m apart: pump them without throttle." % name, "line",
                       [x, z], P(lomitos()), yaw=yaw, width=5, edge=3, mode="level", base=0, smooth=0.5, dirt=True))
    bp = berm(floor=11.5, H=1.8, Rc=5.0, wall=40.0, top=1.0, back=30.0)
    cx = (PUMP["x0"] + PUMP["x1"]) / 2
    s.append(shape("Pump track, north berm (a round shape with an \"arc\": only the half from west to east).", "round",
                   [cx, PUMP["z1"]], P(bp), arc=[270, 90], edge=4, mode="level", base=0, smooth=0.5, dirt=True))
    s.append(shape("Pump track, south berm.", "round", [cx, PUMP["z0"]], P(bp), arc=[90, 270], edge=4, mode="level", base=0, smooth=0.5,
                   dirt=True))
    s.append(shape("The bowl (north-west): 2.4 m deep, 50° walls, a 0.8 m rim all around. The giant balls live in it.", "round",
                   list(BOWL_AT), P(bowl()), edge=5, mode="level", base=0, smooth=0.8, dirt=True))
    s.append(shape("La Pared (north-east): a 3.2 m dirt quarter pipe (62° at the top) facing south, with a 24° back side to "
                   "land on if you go over.", "line", list(PARED_AT), P(pared()), yaw=0, width=22, edge=6, mode="level", base=0,
                   smooth=0.8, dirt=True))
    return s


# --------------------------------------------------------------------------------------- objetos
def obj(shape_, at, size, **kw):
    body = {"shape": shape_, "at": at, "size": size}
    body.update(kw)
    parts = []
    for k, v in body.items():
        if isinstance(v, bool):
            v = "true" if v else "false"
        elif isinstance(v, str):
            v = '"%s"' % v
        elif isinstance(v, list):
            v = "[" + ", ".join("%g" % round(q, 3) for q in v) + "]"
        else:
            v = "%g" % round(v, 3)
        parts.append('"%s": %s' % (k, v))
    return "    { " + ", ".join(parts) + " }"


MADERA = [  # cajones de la Línea de madera: (nombre, alto, largo de la rampa, largo de la mesa, largo de la bajada)
    ("chico", 0.8, 4.0, 3.0, 5.5),
    ("mediano", 1.2, 5.0, 4.5, 7.0),
    ("grande", 1.8, 5.0, 5.5, 9.0),
]
WOOD, WOOD_DARK = "#c9a36b", "#a8834f"


def objects():
    out = []
    z = -66.0
    lips = []
    out.append("    // --- Línea de madera (middle), ridden north: three wooden jump boxes (ramp, flat deck, ramp down).")
    out.append("    // Jump from ramp to ramp or just roll over them, either way. The pieces overlap by 5 cm (no gaps).")
    for name, H, up, deck, down in MADERA:
        out.append(obj("ramp", [X_MADERA, 0, z + up / 2], [4, H, up], color=WOOD))
        lips.append(("Cajón " + name, z + up))
        # (la mesa 4 cm más angosta que las rampas: sus costados no quedan en el mismo plano y no titilan)
        out.append(obj("box", [X_MADERA, 0, z + up + deck / 2 - 0.05], [3.96, H, deck + 0.1], color=WOOD_DARK))
        out.append(obj("ramp", [X_MADERA, 0, z + up + deck + down / 2 - 0.1], [4, H, down], yaw=180, color=WOOD))
        z += up + deck + down + 14.0
    zb = z + 12.0
    out.append("")
    out.append("    // --- La bolera: ten bowling pins at the end of the wooden line.")
    k = 0
    for row in range(4):
        for i in range(row + 1):
            x = X_MADERA + (i - row / 2) * 1.2
            out.append(obj("cylinder", [x, 0, zb + row * 1.1], [0.45, 1.5], dynamic=True, mass=6,
                           color="#d62c2c" if (row, i) == (2, 1) else "#f4f4f4"))
            k += 1
    out.append("")
    out.append("    // --- Pirámide de cajas (4 + 3 + 2 + 1) at the end of the dirt line: ride through it.")
    zp = 52.0
    for row, n in enumerate((4, 3, 2, 1)):
        for i in range(n):
            x = X_TIERRA + (i - (n - 1) / 2) * 1.1
            out.append(obj("box", [x, row * 1.0, zp], 1, dynamic=True, mass=12, color="#a0703c" if (row + i) % 2 else "#b07c44"))
    out.append("")
    out.append("    // --- Muro de tambores: two rows of oil drums where you leave the pump track.")
    cols = ["#c0392b", "#2e86c1", "#f1c40f", "#e67e22"]
    zt = PUMP["z1"] + 30.0
    for r in range(2):
        for i in range(6):
            x = (PUMP["x0"] + PUMP["x1"]) / 2 + (i - 2.5) * 0.9
            out.append(obj("cylinder", [x, 0, zt + r * 0.9], [0.6, 0.9], dynamic=True, mass=20, color=cols[(i + r) % 4]))
    out.append("")
    out.append("    // --- Pelotas gigantes, in the bowl: they always roll back to the bottom.")
    for (dx, dz), col in zip(((0, 0), (2.6, 1.2), (-1.4, 2.4)), ("#e84393", "#00b894", "#fdcb6e")):
        out.append(obj("sphere", [BOWL_AT[0] + dx, 0, BOWL_AT[1] + dz], 2.2, dynamic=True, mass=45, friction=0.6, color=col))
    return out, lips, zb


HEAD = """// Example mod: a flat physics park, laid out like a small bike park. The lap track runs around the
// edge; inside, every line is ridden north, from the start straight to the far one (both left turns):
//   west:   Línea de tierra, three dirt tables (small, medium and one for backflips), then a crate pyramid;
//   middle: Línea de madera, three wooden jump boxes (ramp, deck, ramp down), then the bowling pins;
//   east:   a pump track (an oval of rollers with two berms), then a wall of oil drums;
//   north:  the plaza, with a bowl (the giant balls live in it) and La Pared, a dirt quarter pipe.
// Dirt jumps are shapes sculpted into the ground ("terrain.shapes"); wooden ones are objects ("objects").
// Copy this folder to start your own mod; every field is in MODDING.md. Save this file while the game is
// running on this map and it rebuilds right away. (Written by tools/parque.py.)
{
  "name": "Parque de física",
  "kind": "sandbox · parque de saltos y objetos sueltos",
  "description": "Parque de saltos: línea de tierra, línea de madera, pump track, bowl y quarter, y cosas para voltear al final de cada línea.",
  "hint": "Las líneas se andan hacia el norte, desde la recta de largada · Al final de cada una hay algo para voltear",
  "color": "#4fc3f7",
  "bike": "sandbox/dostiempos",

  "terrain": {
    "type": "flat", "edge": 10,
    // shape: line | round. "profile": [[u, height], ...] in meters; "line" runs it along "yaw" (0 = north),
    // "round" spins it around "at" (u = radius; "arc" keeps only a slice, from one heading to another).
    // "mode": "level" flattens the ground to "base" + profile; "dirt": true paints it with the track's dirt
    // (see MODDING.md, terrain.shapes).
    "shapes": [
"""

MID = """
    ]
  },

  // A rounded rectangle around the park (x, z in meters; x = east, z = north).
  "track": {
    "style": "track",
    "width": 12,
    "points": [
      [-80, -95], [0, -96], [80, -95], [97, -80],
      [98, 0], [97, 80], [80, 97], [0, 98],
      [-80, 97], [-97, 80], [-98, 0], [-97, -80]
    ]
  },
  "start": [-40, -95],
  "auto_features": [
    ["kicker", "tabletop"],   // south straight (the start)
    ["rollers"],              // east
    ["double"],               // north
    ["whoops"]                // west
  ],

  "look": { "preset": "day", "sun_dir": [-0.35, 0.8, -0.5], "fog": 0.0026 },

  // shape: box | ramp | cylinder | sphere. "at": [x, y, z] where y is the gap between the ground and the
  // lowest point of the object (negative = buried). size: [width, height, length] in meters
  // (cylinder: [diameter, height], sphere: diameter). yaw / pitch / roll in degrees. A ramp rises
  // towards its "yaw" direction (0 = north, 90 = east). dynamic: true makes it a loose object with
  // "mass" in kg. Fixed objects can be ridden on; loose ones get pushed by the bike's body.
  "objects": [
"""

TAIL = """
  ]
}
"""


def loop(x, z0, z1, back, step=20):
    pts = [(x, z) for z in range(int(z0), int(z1) + 1, step)] + [(x + back / 2, z1 + 12)]
    pts += [(x + back, z) for z in range(int(z1), int(z0) - 1, -step)] + [(x + back / 2, z0 - 12)]
    return pts


def write():
    obs, lips, zb = objects()
    body = []
    for o in obs:
        body.append(o)
    # comas: todas las líneas de objeto menos la última llevan coma
    idx = [i for i, o in enumerate(body) if o.startswith("    {")]
    for i in idx[:-1]:
        body[i] += ","
    text = HEAD + ",\n\n".join(shapes()) + MID + "\n".join(body) + TAIL
    path = os.path.join(ROOT, "mods", "sandbox", "maps", "park.json")
    open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("escrito", path)
    # Copias de prueba: el parque con la trial (prueba/park) y una vuelta guía por cada zona.
    tests = {
        "park": (None, None, "Parque de física (trial)", "base/trial"),
        "park_tierra": (loop(X_TIERRA, -90, 30, -24), (X_TIERRA, -85), "Parque (guía por la línea de tierra)", None),
        "park_madera": (loop(X_MADERA, -90, zb - 6, -24), (X_MADERA, -85), "Parque (guía por la línea de madera)", None),
        "park_pump": ([(PUMP["x0"], z) for z in range(int(PUMP["z0"]), int(PUMP["z1"]) + 1, 10)]
                      + [((PUMP["x0"] + PUMP["x1"]) / 2 + 13 * math.sin(math.radians(a)), PUMP["z1"] + 13 * math.cos(math.radians(a)))
                         for a in (300, 330, 0, 30, 60)]
                      + [(PUMP["x1"], z) for z in range(int(PUMP["z1"]), int(PUMP["z0"]) - 1, -10)]
                      + [((PUMP["x0"] + PUMP["x1"]) / 2 + 13 * math.sin(math.radians(a)), PUMP["z0"] + 13 * math.cos(math.radians(a)))
                         for a in (120, 150, 180, 210, 240)], (PUMP["x0"], -40), "Parque (guía por el pump track)", None),
        "park_plaza": (loop(46, 18, 78, 26), (46, 22), "Parque (guía por La Pared)", None),
        "park_bowl": (loop(BOWL_AT[0], 10, 100, 30), (BOWL_AT[0], 14), "Parque (guía por el bowl)", None),
    }
    for name, (pts, start, title, bike) in tests.items():
        t = text.replace('"name": "Parque de física"', '"name": "%s"' % title)
        if bike:
            t = t.replace('"bike": "sandbox/dostiempos"', '"bike": "%s"' % bike)
        if pts:
            t = re.sub(r'"points": \[.*?\]\s*\]', lambda m: '"points": [' + ", ".join("[%g, %g]" % (round(a, 1), round(b, 1)) for a, b in pts) + "]",
                       t, flags=re.S)
            t = t.replace('"style": "track"', '"style": "guide"')
            t = re.sub(r'"start": \[[^\]]*\]', '"start": [%g, %g]' % start, t)
        p = os.path.join(ROOT, "pruebas", "mods", "prueba", "maps", name + ".json")
        open(p, "w", encoding="utf-8", newline="\n").write("// Copy of mods/sandbox/maps/park.json (%s). Written by tools/parque.py.\n" % title + t)
        print("escrito", p)
    print("labios de madera:", ", ".join("%s z=%.1f" % l for l in lips), "| bolos en z=%.1f" % zb)
    tp, marks = linea_tierra()
    print("labios de tierra:", ", ".join("%s z=%.1f" % (n, Z_TIERRA + u) for n, u in marks), "| termina en z=%.1f" % (Z_TIERRA + tp[-1][0]))


if __name__ == "__main__":
    write()
