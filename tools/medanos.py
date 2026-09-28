# Genera el mapa de médanos (mods/sandbox/maps/medanos.json) desde los perfiles de sus saltos, y las
# copias de prueba con otra vuelta guía (pruebas/mods/prueba/maps/medanos_crater.json y medanos_ola.json:
# el terreno es el mismo porque la guía no lo toca). Ver docs/MAPAS.md, "Los Médanos".
#
#   python tools/medanos.py            escribe el mapa y las copias de prueba
#   python tools/medanos.py --sim      además, simula cada salto (punto pegado al suelo hasta que la
#                                      curvatura lo despega, vuelo sin aire): despegue, aire, altura y con
#                                      qué velocidad normal cae a cada velocidad. Es una primera idea: lo
#                                      que vale es medirlo en el juego (tools/saltos.py).
#
# Los perfiles son tramos rectos (u, alto) como los lee Terrain::PrepareShapes: arcos de radio dado en
# pasos de 3°, rectas y una S suave (smoothstep) para salir de las hoyadas sin labio.
import math, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
G = 9.81


# ------------------------------------------------------------------------------------ geometría
def arc(u, h, a0, a1, R):
    """Arco de radio R desde (u, h) que pasa de pendiente a0 a a1 (grados, + sube)."""
    a0, a1 = math.radians(a0), math.radians(a1)
    n = max(2, int(abs(a1 - a0) / math.radians(3)) + 1)
    sg = 1 if a1 > a0 else -1                        # cóncavo (el centro arriba) o convexo
    cx, cy = u - sg * R * math.sin(a0), h + sg * R * math.cos(a0)
    return [(cx + sg * R * math.sin(a0 + (a1 - a0) * i / n), cy - sg * R * math.cos(a0 + (a1 - a0) * i / n)) for i in range(1, n + 1)]


def line(u, h, ang, length):
    a = math.radians(ang)
    return [(u + length * math.cos(a), h + length * math.sin(a))]


def climb(u, h, L, step=2.0):
    """De (u, h) a (u + L, 0) en una S (smoothstep): pendiente máxima 1.5·|h|/L y sin quiebre arriba."""
    n = max(2, int(L / step))
    return [(u, h)] + [(u + L * i / n, h * (1 - (i / n) ** 2 * (3 - 2 * i / n))) for i in range(1, n + 1)]


def tabletop(H, kick, R, table, land, Rk, depth, Rb, runout=25.0):
    """Mesa: cara cóncava de radio R hasta 'kick' grados y alto H, mesa de 'table' m, nudillo de radio Rk,
    bajada de 'land' grados que sigue 'depth' m bajo el suelo, fondo de radio Rb y salida en S."""
    pts = [(0.0, 0.0)] + arc(0.0, 0.0, 0, kick, R)
    a = math.radians(kick)
    if R * (1 - math.cos(a)) < H:
        pts += line(pts[-1][0], pts[-1][1], kick, (H - R * (1 - math.cos(a))) / math.sin(a))
    lip = pts[-1]
    pts += [(lip[0] + 0.6, lip[1]), (lip[0] + 0.6 + table, lip[1])]
    pts += arc(pts[-1][0], pts[-1][1], 0, -land, Rk)
    ab = math.radians(land)
    u, h = pts[-1]
    drop = h - (-depth + Rb * (1 - math.cos(ab)))
    if drop > 0:
        pts += line(u, h, -land, drop / math.sin(ab))
    pts += arc(pts[-1][0], pts[-1][1], -land, 0, Rb)
    if depth > 0:
        pts += climb(pts[-1][0] + 4, pts[-1][1], max(runout, 11.0 * depth))
    pts.append((pts[-1][0] + 10, 0.0))
    return pts, lip


def medano(H=22.0, stoss=130.0, kick=18.0, Rk=30.0, lee=36.0, Rknuckle=5.0, Rbottom=18.0, dig=8.0):
    """Médano grande: barlovento largo (sube a 12° con un radio de 120 m), labio que patea a 'kick'
    grados en u = 0 a H m, nudillo, sotavento de 'lee' grados que sigue 'dig' m bajo el suelo, fondo y
    salida en S."""
    base = 12.0
    pts = [(-stoss, 0.0)] + arc(-stoss, 0.0, 0, base, 120)
    a = math.radians(base)
    hk = Rk * (math.cos(a) - math.cos(math.radians(kick)))
    u, h = pts[-1]
    pts += line(u, h, base, (H - hk - h) / math.sin(a))
    pts += arc(pts[-1][0], pts[-1][1], base, kick, Rk)
    du = -pts[-1][0]
    pts = [(p[0] + du, p[1]) for p in pts]
    lip = pts[-1]
    pts += arc(lip[0], lip[1], kick, -lee, Rknuckle)
    ab = math.radians(lee)
    u, h = pts[-1]
    pts += line(u, h, -lee, (h - (-dig + Rbottom * (1 - math.cos(ab)))) / math.sin(ab))
    pts += arc(pts[-1][0], pts[-1][1], -lee, 0, Rbottom)
    if dig > 0:
        pts += climb(pts[-1][0] + 6, pts[-1][1], max(50.0, 11.0 * dig))
    pts.append((pts[-1][0] + 20, 0.0))
    return pts, lip


def crater(floor=-2.5, r0=10.0, wall=55.0, R=16.0, top=5.0, outer=18.0, Rout=30.0, rimw=1.5):
    """Perfil radial (u = radio) de un bowl: fondo, pared cóncava hasta 'wall' grados, corona a 'top' m y
    ladera de afuera de 'outer' grados que se apoya en 0."""
    pts = [(0.0, floor), (r0, floor)] + arc(r0, floor, 0, wall, R)
    u, h = pts[-1]
    if h < top:
        pts += line(u, h, wall, (top - h) / math.sin(math.radians(wall)))
    pts.append((pts[-1][0] + rimw, top))
    pts += arc(pts[-1][0], top, 0, -outer, 6.0)
    u, h = pts[-1]
    ao = math.radians(outer)
    pts += line(u, h, -outer, max(0.0, h - Rout * (1 - math.cos(ao))) / math.sin(ao))
    pts += arc(pts[-1][0], pts[-1][1], -outer, 0, Rout)
    pts.append((pts[-1][0] + 5, pts[-1][1]))
    return pts


def spine(H=6.5, wall=55.0, R=10.0, crest=1.2, back=38.0, Rk=4.0, Rb=18.0, approach=40.0, runout=30.0):
    """Pared: cara cóncava hasta 'wall' grados (quarter), cresta corta y espalda de 'back' grados."""
    pts = [(-approach, 0.0), (0.0, 0.0)] + arc(0.0, 0.0, 0, wall, R)
    u, h = pts[-1]
    if h < H:
        pts += line(u, h, wall, (H - h) / math.sin(math.radians(wall)))
    pts.append((pts[-1][0] + crest, pts[-1][1]))
    pts += arc(pts[-1][0], pts[-1][1], 0, -back, Rk)
    ab = math.radians(back)
    u, h = pts[-1]
    pts += line(u, h, -back, (h - Rb * (1 - math.cos(ab))) / math.sin(ab))
    pts += arc(pts[-1][0], pts[-1][1], -back, 0, Rb)
    pts.append((pts[-1][0] + runout, 0.0))
    return pts


# --------------------------------------------------------------------------------------- mapa
def P(pts):
    return "[" + ", ".join(f"[{u:.1f}, {h:.2f}]" for u, h in pts) + "]"


def shape(comment, kind, at, profile, **kw):
    body = {"shape": kind, "at": at}
    body.update(kw)
    txt = ", ".join(f'"{k}": {v if not isinstance(v, str) else chr(34) + v + chr(34)}' for k, v in body.items())
    txt = txt.replace("True", "true").replace("False", "false")
    return f"    // {comment}\n    {{{txt}, \"profile\": {profile}}}"


def jumps():
    """Los perfiles de todo lo esculpido: (nombre, puntos, u del labio)."""
    esc, u0, marks = [(-60.0, 0.0)], 0.0, []
    for name, (p, lip) in (("chica", tabletop(1.4, 24, 9, 6, 22, 6, 0.0, 10)), ("mediana", tabletop(2.6, 30, 14, 10, 26, 8, 1.0, 16)),
                           ("grande", tabletop(4.0, 34, 18, 14, 30, 10, 2.0, 20))):
        esc += [(u + u0, h) for u, h in p]
        marks.append((name, u0 + lip[0]))
        u0 = esc[-1][0] + 18.0
    esc.append((u0 + 60.0, 0.0))
    gp, glip = tabletop(6.0, 42, 18, 16, 36, 12, 5.0, 26, runout=30.0)
    mp, mlip = medano()
    lomos = [(-40.0, 0.0)] + [(u * 0.5, 0.65 * (1 - math.cos(2 * math.pi * u * 0.5 / 14.0))) for u in range(0, 113)] + [(150.0, 0.0)]
    return dict(escalera=(esc, marks), gigante=([(-100.0, 0.0)] + gp, glip[0]), medano=([(-220.0, 0.0)] + [q for q in mp if q[0] > -219], mlip[0]),
                lomos=(lomos, 0.0), crater=(crater(), 0.0), ola=(spine(), 0.0))


def build():
    J = jumps()
    s = [shape("The beach: a flat strip along the south edge (start line). Levels the dunes to 0.",
               "line", [0, -205], "[[-300, 0], [300, 0]]", yaw=90, width=40, edge=26, mode="level", base=0, smooth=0)]
    s.append(shape("El Médano Grande: a 22 m dune ridden north. Long windward climb from the beach, lip at z = 40, 36° lee to land "
                   "on that goes on 8 m below the ground, then a long gentle climb out. 40-55 km/h at the lip (lean back in the air: "
                   "hands off, the nose drops).", "line", [75, 40], P(J["medano"][0]), yaw=0, width=34, edge=34, bend=22, mode="level", base=0, smooth=1.0))
    for at, r in (([-165, 182], 34), ([58, 186], 30)):
        s.append(shape("A flat corner for the guide lap.", "round", at, f"[[0, 0], [{r}, 0]]", edge=18, mode="level", base=0, smooth=0))
    s.append(shape("El Gigante: 6 m table with a 42° kick on the beach, ridden east from the start. 2.4-3 s of air at 55-65 km/h: room "
                   "for a backflip (70 km/h overshoots the landing). Landing slope 36° down into a hollow dug 5 m into the beach.",
                   "line", [-60, -205], P(J["gigante"][0]), yaw=90, width=20, edge=14, mode="level", base=0, smooth=1.0))
    s.append(shape("Los Lomos: four 1.3 m sand rollers, 14 m apart, on the north strip (ridden west): pump them or double them.",
                   "line", [10, 200], P(J["lomos"][0]), yaw=270, width=16, edge=12, mode="level", base=0, smooth=0.5))
    s.append(shape("La Escalera: three tables ridden south along the west side, small to big (1.4 m, 2.6 m and 4 m). Any speed from "
                   "40 to 75 km/h lands on a slope or a table.", "line", [-180, 110], P(J["escalera"][0]), yaw=180, width=22, edge=16,
                   mode="level", base=0, smooth=1.0))
    s.append(shape("El Cráter: a bowl 50 m across. 55° walls (quarter pipes) 7.5 m tall from the floor; ride in over the rim, carve the "
                   "walls, or launch out over the far rim.", "round", [172, 55], P(J["crater"][0]), edge=16, mode="level", base=0, smooth=1.0))
    s.append(shape("La Ola: a 6.5 m wall facing the beach, 55° at the top (quarter pipe) with a 38° back side to land on (spine).",
                   "line", [165, -150], P(J["ola"][0]), yaw=0, width=40, edge=12, mode="level", base=0, smooth=1.0))
    return s


def guide():
    pts = [(-150, -205), (-110, -205), (-70, -205), (-30, -205), (10, -205), (40, -201), (62, -186), (73, -160)]
    pts += [(75, z) for z in range(-120, 151, 40)]
    pts += [(69, 178), (51, 196), (20, 200), (-20, 200), (-60, 200), (-100, 200), (-130, 199), (-158, 190), (-175, 170), (-180, 140)]
    pts += [(-180, z) for z in range(100, -141, -40)]
    return pts + [(-178, -170), (-170, -192)]


HEAD = """// Los Médanos: free-roam dune map. The terrain is a dune field ("type": "dunes") with hand-made
// shapes on top ("terrain.shapes", see MODDING.md): big jumps with landings that follow the arc, a bowl
// and a quarter pipe. The track is only a guide lap ("style": "guide"): it does not touch the ground
// and has no stakes; the bot and the respawn follow it, and it runs over the best jumps.
// Generated by tools/medanos.py from the profiles designed for it (docs/MAPAS.md, "Los Médanos").
{
  "name": "Los Médanos",
  "kind": "sandbox · dunas para andar libre",
  "description": "Médanos al atardecer para andar libre: saltos de todos los tamaños, un médano de 22 m, una mesa para mortales, un cráter y una pared.",
  "hint": "Andá libre · Médano Grande: 40-55 km/h, cuerpo atrás · Gigante: 55-65, da para un mortal",
  "color": "#f2a65a",
  "bike": "base/motocross",
  "bot_speed": 15,
  "bot_lateral": 5,

  "terrain": {
    // Transverse dunes blowing north: gentle windward side to the south, steep face to the north.
    "type": "dunes", "height": 8, "wavelength": 64, "wind": 0, "meander": 0.6, "detail": 0.12, "border": 1.2,
    "size": 512, "resolution": 0.5, "edge": 12,
    "shapes": [
"""

TAIL = """
    ]
  },

  "track": {
    "style": "guide",
    "width": 10,
    "points": [%s]
  },
  "start": [-140, -205],

  "look": {
    "preset": "sunset",
    "ground_textures": "sand",
    "sun_dir": [-0.85, 0.42, -0.25],
    "ground": [214, 164, 112],
    "zenith": [78, 112, 180],
    "fog": 0.0019
  }
}
"""


def write_map():
    text = HEAD + ",\n\n".join(build()) + TAIL % ", ".join(f"[{x}, {z}]" for x, z in guide())
    path = os.path.join(ROOT, "mods", "sandbox", "maps", "medanos.json")
    open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("escrito", path)
    # Copias con la guía por lo que la vuelta no pasa (para tools/saltos.py y capturas).
    def loop(x, z0, z1, back=28, step=20):
        pts = [(x, z) for z in range(z0, z1 + 1, step)] + [(x + back / 2, z1 + 15)]
        pts += [(x + back, z) for z in range(z1, z0 - 1, -step)] + [(x + back / 2, z0 - 15)]
        return "[" + ", ".join("[%g, %g]" % p for p in pts) + "]"
    for name, pts, start in (("crater", loop(172, -60, 170), (172, -40)), ("ola", loop(165, -228, -40), (165, -225))):
        t = re.sub(r'"points": \[.*?\]\]', lambda m: '"points": ' + pts, text, flags=re.S)
        t = re.sub(r'"start": \[[^\]]*\]', '"start": [%g, %g]' % start, t)
        t = t.replace('"name": "Los Médanos"', '"name": "Los Médanos (guía por %s)"' % ("el cráter" if name == "crater" else "la Ola"))
        p = os.path.join(ROOT, "pruebas", "mods", "prueba", "maps", "medanos_%s.json" % name)
        open(p, "w", encoding="utf-8", newline="\n").write(
            "// Copy of mods/sandbox/maps/medanos.json with the guide lap over %s (same terrain). Written by tools/medanos.py.\n" % name + t)
        print("escrito", p)


# ------------------------------------------------------------------------------ simulación
class Prof:
    """El perfil como lo arma el juego: tabla cada 0.1 m y dos pasadas de promedio de 'smooth' m."""
    def __init__(self, knots, smooth=1.0):
        self.u0 = knots[0][0] - smooth * 1.5 - 0.2
        n = int(math.ceil((knots[-1][0] + smooth * 1.5 + 0.2 - self.u0) / 0.1)) + 1
        us = [k[0] for k in knots]
        def lin(u):
            if u <= us[0]:
                return knots[0][1]
            for (a, ha), (b, hb) in zip(knots, knots[1:]):
                if u <= b:
                    return ha + (hb - ha) * (u - a) / (b - a)
            return knots[-1][1]
        t = [lin(self.u0 + i * 0.1) for i in range(n)]
        r = int(round(smooth * 0.5 / 0.1))
        for _ in range(2 if r else 0):
            pre = [0.0]
            for v in t:
                pre.append(pre[-1] + v)
            t = [(pre[min(n - 1, i + r) + 1] - pre[max(0, i - r)]) / (min(n - 1, i + r) - max(0, i - r) + 1) for i in range(n)]
        self.t, self.end = t, knots[-1][0]

    def h(self, u):
        f = min(max((u - self.u0) / 0.1, 0.0), len(self.t) - 1.001)
        i = int(f)
        return self.t[i] + (self.t[i + 1] - self.t[i]) * (f - i)

    def s(self, u):
        return (self.h(u + 0.1) - self.h(u - 0.1)) / 0.2


def simulate(p, start, kmh):
    """Velocidad constante (con gas) pegado al suelo hasta que la curvatura lo despega; después, vuelo."""
    v, u = kmh / 3.6, start
    while u < p.end:
        h1, h2 = p.s(u), (p.s(u + 0.1) - p.s(u - 0.1)) / 0.2
        th, kap = math.atan(h1), -h2 / (1 + h1 * h1) ** 1.5
        if kap > 0 and v * v * kap > G * math.cos(th):
            break
        u += 0.02 * math.cos(th)
    else:
        return None
    x, y, vx, vy, t, top = u, p.h(u), v * math.cos(th), v * math.sin(th), 0.0, 0.0
    while t < 8:
        t += 0.002; x += vx * 0.002; vy -= G * 0.002; y += vy * 0.002
        top = max(top, y - p.h(x))
        if y <= p.h(x) and t > 0.02:
            break
    sl = p.s(x)
    vn = -(vx * -sl + vy) / math.hypot(1, sl)
    return math.degrees(th), t, top, x - u, math.degrees(math.atan(-sl)), vn


def sim_all():
    J = jumps()
    esc, marks = J["escalera"]
    cases = [("Gigante", J["gigante"][0], -20), ("Médano Grande", J["medano"][0], -30)]
    cases += [("Escalera " + n, esc, lip - 12) for n, lip in marks]
    for name, pts, start in cases:
        print("---", name)
        for kmh in (40, 45, 50, 55, 60, 65, 70):
            r = simulate(Prof(pts), start, kmh)
            if r and r[1] > 0.1:
                print(f"  {kmh} km/h: sale a {r[0]:4.1f}°, aire {r[1]:4.2f} s, alto {r[2]:4.1f} m, {r[3]:4.1f} m más allá; "
                      f"cae en {r[4]:5.1f}° de bajada con {r[5]:4.1f} m/s contra el suelo")


if __name__ == "__main__":
    write_map()
    if "--sim" in sys.argv:
        sim_all()
