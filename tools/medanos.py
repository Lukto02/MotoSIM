# Genera el mapa de médanos (mods/sandbox/maps/medanos.json) desde los perfiles de sus saltos, y las
# copias de prueba con otra vuelta guía (pruebas/mods/prueba/maps/medanos_*.json: el terreno es el mismo
# porque la guía no lo toca). Ver docs/MAPAS.md, "Los Médanos".
#
#   python tools/medanos.py            escribe el mapa y las copias de prueba
#   python tools/medanos.py --sim      además, simula cada salto (punto pegado al suelo hasta que la
#                                      curvatura lo despega, vuelo sin aire): despegue, aire, altura y con
#                                      qué velocidad normal cae a cada velocidad. Es una primera idea: lo
#                                      que vale es medirlo en el juego (tools/saltos.py).
#   python tools/medanos.py --labios [exe]
#                                      la s de la vuelta guía donde despega cada salto (con el exe, exacta:
#                                      sale del perfil de la vuelta, --test profile)
#   python tools/medanos.py --medir exe [40,50,60,70] [filtro]
#                                      mide cada salto de la vuelta (o los que tengan "filtro" en el nombre)
#                                      con tools/saltos.py, bot y "sin tocar nada", apareciendo 70 m antes
#
# Los perfiles son tramos rectos (u, alto) como los lee Terrain::PrepareShapes: arcos de radio dado en
# pasos de 3°, rectas y una S suave (smoothstep) para salir de las hoyadas sin labio.
#
# El mapa (v2): una vuelta de ~1.3 km por el borde de la zona de médanos, cargada de saltos, con peraltes
# en las cuatro esquinas; y adentro, lo grande para andar libre, más cerca:
#   la playa (al este, la largada): El Gigante, la mesa para mortales;
#   La Cadena (al norte, x = 140): seis saltos de médano seguidos, de alturas y ángulos distintos;
#   El Serrucho (al oeste, z = 195): diez lomas en tres tamaños (lomitos, Los Lomos y lomazos);
#   La Escalera (al sur, x = -150): las tres mesas, más juntas, y Los Dientes (un serrucho de verdad);
#   adentro: el Médano Grande (sale de la playa), el Cráter, La Ola y Los Montes (médanos redondos).
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


def tabletop(H, kick, R, table, land, Rk, depth, Rb, runout=25.0, end=10.0):
    """Mesa: cara cóncava de radio R hasta 'kick' grados y alto H, mesa de 'table' m, nudillo de radio Rk,
    bajada de 'land' grados que sigue 'depth' m bajo el suelo, fondo de radio Rb, salida en S y 'end' m de llano."""
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
    pts.append((pts[-1][0] + end, 0.0))
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


class Perfil:
    """Perfil por tramos para encadenar saltos: lleva el punto actual (u, h) y la pendiente (grados)."""
    def __init__(self, u=0.0, h=0.0):
        self.pts = [(u, h)]
        self.ang = 0.0
        self.marks = []                          # (nombre, u del labio)

    @property
    def u(self):
        return self.pts[-1][0]

    @property
    def h(self):
        return self.pts[-1][1]

    def flat(self, L):
        self.pts.append((self.u + L, self.h))
        self.ang = 0.0
        return self

    def arc(self, a1, R):
        self.pts += arc(self.u, self.h, self.ang, a1, R)
        self.ang = a1
        return self

    def line(self, L):
        self.pts += line(self.u, self.h, self.ang, L)
        return self

    def to_h(self, h):
        """Recta con la pendiente actual hasta la altura h."""
        s = math.sin(math.radians(self.ang))
        if abs(s) > 1e-6 and (h - self.h) * s > 0:
            self.line((h - self.h) / s)
        return self

    def s_to(self, h, L, step=2.0):
        """S suave (smoothstep) hasta h en L m (sin quiebres: no patea)."""
        h0, u0, n = self.h, self.u, max(2, int(L / step))
        self.pts += [(u0 + L * i / n, h0 + (h - h0) * (i / n) ** 2 * (3 - 2 * i / n)) for i in range(1, n + 1)]
        self.ang = 0.0
        return self

    def mark(self, name):
        self.marks.append((name, self.u))
        return self

    def cara(self, H, kick, R):
        """Cara cóncava desde la pendiente actual hasta 'kick' grados, recta hasta la altura H (el labio)."""
        self.arc(kick, R)
        if self.h > H:
            raise ValueError("la cara de radio %g pasa el alto del labio (%g m)" % (R, H))
        return self.to_h(H)

    def bajada(self, land, Rk, depth, Rb):
        """Nudillo de radio Rk hasta 'land' grados de bajada, recta hasta el fondo en -depth, fondo de radio Rb."""
        self.arc(-land, Rk)
        target = -depth + Rb * (1 - math.cos(math.radians(land)))
        if self.h > target:
            self.line((self.h - target) / math.sin(math.radians(land)))
        return self.arc(0, Rb)

    def mesa(self, name, H, kick, R, table, land, Rk, depth, Rb):
        """Mesa desde donde esté (el fondo del salto anterior o el suelo): cara, labio, mesa y bajada."""
        self.cara(H, kick, R).mark(name).flat(0.6 + table)
        return self.bajada(land, Rk, depth, Rb)

    def lomas(self, H, lam, n, step=0.5):
        """n lomas de H m cada lam m (cosenos: se bombean o se doblan)."""
        u0, h0, k = self.u, self.h, int(round(lam * n / step))
        self.pts += [(u0 + i * step, h0 + H * 0.5 * (1 - math.cos(2 * math.pi * i * step / lam))) for i in range(1, k + 1)]
        self.ang = 0.0
        return self

    def dientes(self, H, up, down, n, step=0.5):
        """Serrucho: n dientes de H m, subida suave de 'up' m (se curva hasta la cresta, como un médano) y
        bajada de 'down' m (S) hasta el pie del siguiente."""
        for _ in range(n):
            u0, h0 = self.u, self.h
            k = int(round(up / step))
            self.pts += [(u0 + i * step, h0 + H * (i * step / up) ** 1.6) for i in range(1, k + 1)]
            self.mark("diente")
            self.s_to(h0, down, step)
        return self


def berm(floor=28.0, H=3.4, Rc=14.0, wall=36.0, top=1.5, back=24.0):
    """Perfil radial de un peralte (u = radio desde el centro de la curva): piso plano hasta 'floor', pared
    cóncava de radio Rc hasta 'wall' grados y alto H, lomo de 'top' m y espalda de 'back' grados. La vuelta
    guía va a R_CURVA (3 m pasando el piso: 0.3 m de alto, 12° de peralte); más rápido, más arriba."""
    pts = [(0.0, 0.0), (floor, 0.0)] + arc(floor, 0.0, 0, wall, Rc)
    u, h = pts[-1]
    if h < H:
        pts += line(u, h, wall, (H - h) / math.sin(math.radians(wall)))
    pts.append((pts[-1][0] + top, pts[-1][1]))
    pts += arc(pts[-1][0], pts[-1][1], 0, -back, 5.0)
    u, h = pts[-1]
    ab = math.radians(back)
    pts += line(u, h, -back, max(0.0, h - 8.0 * (1 - math.cos(ab))) / math.sin(ab))
    pts += arc(pts[-1][0], pts[-1][1], -back, 0, 8.0)
    pts.append((pts[-1][0] + 2, 0.0))
    return pts


def monte(H, flank, top=5.0, Rtop=8.0, Rfoot=14.0):
    """Perfil radial de un médano redondo (un monte, una mesa redonda): cima plana de 'top' m de radio,
    nudillo, ladera de 'flank' grados y pie cóncavo. Se sube por cualquier lado, se sale por la cima y se cae
    en la ladera de enfrente o en la mesa."""
    pts = [(0.0, H), (top, H)] + arc(top, H, 0, -flank, Rtop)
    u, h = pts[-1]
    af = math.radians(flank)
    pts += line(u, h, -flank, max(0.0, h - Rfoot * (1 - math.cos(af))) / math.sin(af))
    pts += arc(pts[-1][0], pts[-1][1], -flank, 0, Rfoot)
    pts.append((pts[-1][0] + 3, 0.0))
    return pts


# --------------------------------------------------------------------------------------- mapa
# La vuelta: esquinas con peralte de radio R_CURVA; líneas en x = X_ESTE (La Cadena, al norte), z = Z_NORTE
# (El Serrucho, al oeste), x = X_OESTE (La Escalera, al sur) y la playa en z = Z_PLAYA (al este).
R_CURVA = 31.0
X_ESTE, X_OESTE, Z_NORTE, Z_PLAYA = 140.0, -150.0, 195.0, -205.0
CORNERS = {  # centro de cada esquina y el sector del peralte (rumbos desde el centro, en el sentido del reloj)
    "SE": ((X_ESTE - R_CURVA, Z_PLAYA + R_CURVA), (90, 180)),
    "NE": ((X_ESTE - R_CURVA, Z_NORTE - R_CURVA), (0, 90)),
    "NO": ((X_OESTE + R_CURVA, Z_NORTE - R_CURVA), (270, 360)),
    "SO": ((X_OESTE + R_CURVA, Z_PLAYA + R_CURVA), (180, 270)),
}


def P(pts):
    return "[" + ", ".join(f"[{u:.1f}, {h:.2f}]" for u, h in pts) + "]"


def shape(comment, kind, at, profile, **kw):
    body = {"shape": kind, "at": at}
    body.update(kw)
    txt = ", ".join(f'"{k}": {v if not isinstance(v, str) else chr(34) + v + chr(34)}' for k, v in body.items())
    txt = txt.replace("True", "true").replace("False", "false")
    return f"    // {comment}\n    {{{txt}, \"profile\": {profile}}}"


def jumps():
    """Los perfiles de todo lo esculpido: nombre -> (puntos, [(nombre del salto, u del labio)])."""
    J = {}
    # La Escalera (medida en la v1): tres mesas, ahora con 8 m entre la salida de una y la cara de la otra, y
    # Los Dientes al final (un serrucho: subida larga, bajada corta, como médanos chiquitos).
    esc, u0, marks = [(-30.0, 0.0)], 0.0, []
    for name, (p, lip) in (("Escalera chica", tabletop(1.4, 24, 9, 6, 22, 6, 0.0, 10)),
                           ("Escalera mediana", tabletop(2.6, 30, 14, 10, 26, 8, 1.0, 16)),
                           ("Escalera grande", tabletop(4.0, 34, 18, 14, 30, 10, 2.0, 20))):
        esc += [(u + u0, h) for u, h in p]
        marks.append((name, u0 + lip[0]))
        u0 = esc[-1][0] + 8.0
    d = Perfil(u0 - 8.0, 0.0).flat(20.0).dientes(1.1, 8.0, 3.5, 6)
    esc += d.pts[1:]
    marks += [("Diente %d" % (i + 1), u) for i, (_, u) in enumerate(d.marks)]
    esc.append((esc[-1][0] + 40.0, 0.0))
    J["escalera"] = (esc, marks)

    # La Cadena: seis saltos de médano seguidos (ver docs/MAPAS.md para lo medido de cada uno).
    c = Perfil(-40.0, 0.0).flat(40.0)
    for name, H, kick, R, table, land, Rk, depth, Rb, out, flat in CADENA:
        c.mesa(name, H, kick, R, table, land, Rk, depth, Rb)
        if out:
            c.s_to(0.0, out)
        c.flat(flat)
    J["cadena"] = (c.pts, c.marks)

    # El Serrucho: lomitos, Los Lomos (los de la v1) y lomazos.
    s = Perfil(-30.0, 0.0).flat(30.0)
    s.mark("Lomitos").lomas(0.7, 10.0, 4).flat(20)
    s.mark("Los Lomos").lomas(1.3, 14.0, 4).flat(14)
    s.mark("Lomazos").lomas(2.0, 20.0, 2).flat(40)
    J["serrucho"] = (s.pts, s.marks)

    gp, glip = tabletop(6.0, 42, 18, 16, 36, 12, 5.0, 26, runout=30.0)
    J["gigante"] = ([(-60.0, 0.0)] + gp, [("Gigante", glip[0])])
    mp, mlip = medano()
    J["medano"] = ([(-150.0, 0.0)] + [q for q in mp if q[0] > -149], [("Médano Grande", mlip[0])])
    J["crater"] = (crater(), [])
    J["ola"] = (spine(approach=30.0), [("La Ola", 0.0)])
    J["berm"] = (berm(), [])
    return J


CADENA = [  # La Cadena: nombre, alto del labio, patada (°), radio de la cara, mesa, bajada (°), nudillo, fondo bajo el
    # suelo, radio del fondo, m de la S de salida (0 = sin S) y llano hasta la cara del siguiente (pasando donde cae a 70)
    ("Cadena 1: el primero", 2.0, 22, 9, 6, 24, 8, 0.8, 12, 12, 3),
    ("Cadena 2: el pozo", 2.6, 28, 10, 10, 28, 8, 1.0, 14, 14, 3),
    ("Cadena 3: el trampolín", 3.0, 34, 8, 12, 28, 8, 1.0, 14, 14, 3),
    ("Cadena 4: el largo", 1.8, 18, 14, 6, 20, 10, 0.3, 14, 0, 12.5),
    ("Cadena 5: el escalón", 3.2, 26, 12, 12, 24, 8, 1.0, 14, 14, 3),
    ("Cadena 6: la grande", 4.4, 32, 14, 14, 30, 10, 1.5, 18, 18, 30),
]
MONTES = [  # Los Montes: mesas redondas para saltar para cualquier lado (x, z, alto, ladera en grados, radio de la cima)
    (65, 45, 3.0, 22, 5), (65, 80, 4.5, 24, 6), (65, 115, 3.5, 22, 5), (98, 62, 2.5, 20, 4), (98, 98, 3.5, 22, 5),
    (35, 62, 2.5, 18, 4), (35, 98, 3.0, 20, 5),
]
PLAZA = (66.0, 80.0, 50.0)                # donde están Los Montes: arena nivelada (centro y radio)
MEDANO_X, MEDANO_LIP_Z = -30.0, -70.0     # el Médano Grande: sale de la playa y patea en z = -70
LINEAS = {  # dónde empieza (u = 0) cada línea de la vuelta y para dónde va (yaw): la usan build() y lips()
    "gigante": ((-60.0, Z_PLAYA), 90), "cadena": ((X_ESTE, -155.0), 0), "serrucho": ((90.0, Z_NORTE), 270),
    "escalera": ((X_OESTE, 145.0), 180),
}
CRATER_AT, OLA_AT = (-30.0, 112.0), (40.0, -118.0)


def build():
    J = jumps()
    s = [shape("The beach: a flat strip along the south edge (start line). Levels the dunes to 0.",
               "line", [0, Z_PLAYA], "[[-300, 0], [300, 0]]", yaw=90, width=40, edge=26, mode="level", base=0, smooth=0)]
    s.append(shape("El Médano Grande: a 22 m dune ridden north from the beach. Windward climb from the beach, lip at z = -70, "
                   "36° lee to land on that goes on 8 m below the ground, then a long gentle climb out. 40-55 km/h at the lip "
                   "(lean back in the air: hands off, the nose drops).", "line", [MEDANO_X, MEDANO_LIP_Z], P(J["medano"][0]),
                   yaw=0, width=34, edge=30, bend=22, mode="level", base=0, smooth=1.0))
    s.append(shape("El Cráter: a bowl 50 m across. 55° walls (quarter pipes) 7.5 m tall from the floor; ride in over the rim, carve "
                   "the walls, or launch out over the far rim.", "round", list(CRATER_AT), P(J["crater"][0]), edge=16, mode="level",
                   base=0, smooth=1.0))
    s.append(shape("La Ola: a 6.5 m wall facing the beach, 55° at the top (quarter pipe) with a 38° back side to land on (spine).",
                   "line", list(OLA_AT), P(J["ola"][0]), yaw=0, width=36, edge=12, mode="level", base=0, smooth=1.0))
    s.append(shape("Los Montes: a flat sand plain for the round dunes (levels the dune field).", "round", [PLAZA[0], PLAZA[1]],
                   "[[0, 0], [%g, 0]]" % PLAZA[2], edge=20, mode="level", base=0, smooth=0))
    for x, z, H, fl, top in MONTES:
        s.append(shape("Los Montes: a round table %.1f m tall with %d° flanks: ride up any side and jump off the top any way you like."
                       % (H, fl), "round", [x, z], P(monte(H, fl, top)), edge=8, mode="add", smooth=1.0))
    s.append(shape("El Gigante: 6 m table with a 42° kick on the beach, ridden east from the start. 2.4-3 s of air at 55-65 km/h: "
                   "room for a backflip (70 km/h overshoots the landing). Landing slope 36° down into a hollow dug 5 m into the beach.",
                   "line", list(LINEAS["gigante"][0]), P(J["gigante"][0]), yaw=LINEAS["gigante"][1], width=20, edge=14, mode="level", base=0, smooth=1.0))
    s.append(shape("La Cadena: six dune jumps in a row along the east side, ridden north, all different: a low one, a deep landing, "
                   "a steep kicker (34°) for whips, a long low one, a step-up table and a big one (4.4 m). Each lands on a slope or "
                   "its table from 40 to 65 km/h, with the next face right after the hollow.", "line", list(LINEAS["cadena"][0]),
                   P(J["cadena"][0]), yaw=LINEAS["cadena"][1], width=16, edge=14, mode="level", base=0, smooth=1.0))
    s.append(shape("El Serrucho: ten sand rollers ridden west along the north side: four small ones (0.7 m, 10 m apart) to pump, "
                   "Los Lomos (1.3 m, 14 m apart) and two big ones (2 m, 20 m apart) to double.", "line", list(LINEAS["serrucho"][0]),
                   P(J["serrucho"][0]), yaw=LINEAS["serrucho"][1], width=16, edge=12, mode="level", base=0, smooth=0.5))
    s.append(shape("La Escalera: three tables ridden south along the west side, small to big (1.4 m, 2.6 m and 4 m), and then Los "
                   "Dientes, six sawtooth dunes (1.1 m: long face, short steep back).", "line", list(LINEAS["escalera"][0]), P(J["escalera"][0]),
                   yaw=LINEAS["escalera"][1], width=18, edge=14, mode="level", base=0, smooth=1.0))
    for name, (at, sector) in CORNERS.items():
        s.append(shape("Corner %s: a sand berm (banked turn) %d m in radius; the guide lap runs low on it." % (name, R_CURVA), "round",
                       [at[0], at[1]], P(J["berm"][0]), arc=list(sector), edge=16, mode="level", base=0, smooth=1.0))
    return s


def guide():
    """La vuelta guía: la playa al este, La Cadena al norte, El Serrucho al oeste, La Escalera al sur; en
    cada esquina tres puntos sobre el arco del peralte (a R_CURVA del centro)."""
    def corner(name, angles):
        (cx, cz), _ = CORNERS[name]
        return [(round(cx + R_CURVA * math.sin(math.radians(a)), 1), round(cz + R_CURVA * math.cos(math.radians(a)), 1)) for a in angles]
    pts = [(x, Z_PLAYA) for x in range(-100, 81, 30)] + corner("SE", (165, 135, 105))
    pts += [(X_ESTE, z) for z in range(-160, 151, 30)] + corner("NE", (75, 45, 15))
    pts += [(x, Z_NORTE) for x in range(90, -111, -30)] + corner("NO", (345, 315, 285))
    pts += [(X_OESTE, z) for z in range(160, -171, -30)] + corner("SO", (255, 225, 195))
    return pts


HEAD = """// Los Médanos: free-roam dune map. The terrain is a dune field ("type": "dunes") with hand-made
// shapes on top ("terrain.shapes", see MODDING.md): lines of jumps with landings that follow the arc, sand
// rollers, berms in the corners (round shapes with an "arc"), round dunes, a big dune, a bowl and a
// quarter pipe. The track is only a guide lap ("style": "guide"): it does not touch the ground and has no
// stakes; the bot and the respawn follow it, and it runs over the lines of jumps.
// Generated by tools/medanos.py from the profiles designed for it (docs/MAPAS.md, "Los Médanos").
{
  "name": "Los Médanos",
  "kind": "sandbox · dunas para andar libre",
  "description": "Médanos al atardecer para andar libre: líneas de saltos seguidos, un serrucho de lomas, peraltes, una mesa para mortales, un médano de 22 m, un cráter y una pared.",
  "hint": "Andá libre · La Cadena y El Serrucho: 45-65 km/h · Gigante: 55-65, da para un mortal · Médano Grande: 40-55, cuerpo atrás",
  "color": "#f2a65a",
  "bike": "base/motocross",
  "bot_speed": 15,
  "bot_lateral": 5,

  "terrain": {
    // Transverse dunes blowing north: gentle windward side to the south, steep face to the north.
    "type": "dunes", "height": %(height)s, "wavelength": %(wavelength)s, "wind": 0, "meander": 0.6, "detail": 0.12, "border": 1.2,
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
  "start": [-100, -205],

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

DUNES = {"height": 8, "wavelength": 52}


def loop(x, z0, z1, back=28, step=20):
    """Vuelta chica de ida y vuelta por x (para medir con saltos.py lo que la vuelta no pisa)."""
    pts = [(x, z) for z in range(z0, z1 + 1, step)] + [(x + back / 2, z1 + 15)]
    pts += [(x + back, z) for z in range(z1, z0 - 1, -step)] + [(x + back / 2, z0 - 15)]
    return pts


TESTS = {  # copias de prueba: nombre -> (guía, largada, qué recorre)
    "crater": (loop(CRATER_AT[0], 20, 200), (CRATER_AT[0], 30), "el cráter"),
    "ola": (loop(OLA_AT[0], -228, -40), (OLA_AT[0], -225), "la Ola"),
    "grande": (loop(MEDANO_X, -200, 60), (MEDANO_X, -195), "el Médano Grande"),
    "montes": (loop(65, 0, 150, back=33), (65, 5), "Los Montes"),
}


def write_map():
    text = (HEAD % DUNES) + ",\n\n".join(build()) + TAIL % ", ".join(f"[{x:g}, {z:g}]" for x, z in guide())
    path = os.path.join(ROOT, "mods", "sandbox", "maps", "medanos.json")
    open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("escrito", path)
    # Copias con la guía por lo que la vuelta no pasa (para tools/saltos.py y capturas).
    for name, (pts, start, what) in TESTS.items():
        t = re.sub(r'"points": \[.*?\]\]', lambda m: '"points": [' + ", ".join("[%g, %g]" % p for p in pts) + "]", text, flags=re.S)
        t = re.sub(r'"start": \[[^\]]*\]', '"start": [%g, %g]' % start, t)
        t = t.replace('"name": "Los Médanos"', '"name": "Los Médanos (guía por %s)"' % what)
        p = os.path.join(ROOT, "pruebas", "mods", "prueba", "maps", "medanos_%s.json" % name)
        open(p, "w", encoding="utf-8", newline="\n").write(
            "// Copy of mods/sandbox/maps/medanos.json with the guide lap over %s (same terrain). Written by tools/medanos.py.\n" % what + t)
        print("escrito", p)


# ------------------------------------------------------------------------------ simulación
class Prof:
    """El perfil como lo arma el juego: tabla cada 0.1 m y dos pasadas de promedio de 'smooth' m."""
    def __init__(self, knots, smooth=1.0):
        self.u0 = knots[0][0] - smooth * 1.5 - 0.2
        n = int(math.ceil((knots[-1][0] + smooth * 1.5 + 0.2 - self.u0) / 0.1)) + 1
        us = [k[0] for k in knots]
        import bisect
        def lin(u):
            if u <= us[0]:
                return knots[0][1]
            k = bisect.bisect_left(us, u)
            if k >= len(us):
                return knots[-1][1]
            (a, ha), (b, hb) = knots[k - 1], knots[k]
            return ha + (hb - ha) * (u - a) / (b - a)
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
    """Velocidad constante (con gas) pegado al suelo hasta que la curvatura lo despega; después, vuelo.
    Devuelve (ángulo al despegar, aire, altura sobre el suelo, largo, pendiente donde cae, vn, u del despegue)."""
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
    return math.degrees(th), t, top, x - u, math.degrees(math.atan(-sl)), vn, u


def sim_all(speeds=(40, 45, 50, 55, 60, 65, 70)):
    J = jumps()
    for key in ("gigante", "cadena", "serrucho", "escalera", "medano"):
        pts, marks = J[key]
        p = Prof(pts, 0.5 if key == "serrucho" else 1.0)
        for name, lip in marks:
            print("---", name)
            for kmh in speeds:
                r = simulate(p, lip - 12, kmh)
                if r and r[1] > 0.1:
                    print(f"  {kmh} km/h: sale a {r[0]:4.1f}°, aire {r[1]:4.2f} s, alto {r[2]:4.1f} m, {r[3]:4.1f} m más allá; "
                          f"cae en {r[4]:5.1f}° de bajada con {r[5]:4.1f} m/s contra el suelo")
                else:
                    print(f"  {kmh} km/h: no vuela")


def lip_points():
    """Cada salto de la vuelta: (nombre, x, z del labio), en el orden de la vuelta."""
    J, out = jumps(), []
    for key, ((x0, z0), yaw) in LINEAS.items():
        dx, dz = round(math.sin(math.radians(yaw))), round(math.cos(math.radians(yaw)))
        out += [(name, x0 + dx * u, z0 + dz * u) for name, u in J[key][1]]
    return out


def lips(exe=None):
    """s de la vuelta guía donde despega cada salto: aprox. por la poligonal de los puntos o, con el exe, la del
    punto más cercano del perfil de la vuelta (--test profile, cada 2 m). Devuelve [(nombre, s)]."""
    out = []
    if exe:
        import subprocess
        prof = []
        txt = subprocess.run([os.path.abspath(exe), "--headless", "--map", "sandbox/medanos", "--test", "profile"], cwd=ROOT,
                             capture_output=True, text=True, errors="replace").stdout
        for ln in txt.splitlines():
            m = re.match(r"perfil s=\s*([\d.]+) x=\s*(-?[\d.]+) z=\s*(-?[\d.]+)", ln)
            if m:
                prof.append(tuple(float(v) for v in m.groups()))
        for name, x, z in lip_points():
            out.append((name, min(prof, key=lambda p: math.hypot(p[1] - x, p[2] - z))[0], prof[-1][0] + 2.0))
    else:
        pts = guide()
        s, acc = [0.0], 0.0
        for a, b in zip(pts, pts[1:] + pts[:1]):
            acc += math.hypot(b[0] - a[0], b[1] - a[1])
            s.append(acc)
        for name, x, z in lip_points():
            best = min(range(len(pts)), key=lambda i: math.hypot(pts[i][0] - x, pts[i][1] - z))
            out.append((name, s[best], acc))
    return out


def medir(exe, speeds="40,50,60,70", filt="", antes=70.0):
    """Cada salto de la vuelta con tools/saltos.py (bot y sin tocar nada), apareciendo 'antes' m antes."""
    import subprocess
    for name, s, L in lips(exe):
        if filt and filt.lower() not in name.lower():
            continue
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "saltos.py"), os.path.abspath(exe), "sandbox/medanos",
                            "%.1f" % ((s - antes) % L), "%.1f" % s, speeds, "ambos", "--time", "16"], cwd=ROOT, capture_output=True,
                           text=True, errors="replace").stdout
        print("#", name)
        print(r.strip())
        sys.stdout.flush()


if __name__ == "__main__":
    write_map()
    if "--sim" in sys.argv:
        sim_all()
    if "--labios" in sys.argv:
        i = sys.argv.index("--labios")
        exe = sys.argv[i + 1] if i + 1 < len(sys.argv) and not sys.argv[i + 1].startswith("--") else None
        for name, s, _ in lips(exe):
            print(f"{name:28s} s = {s:6.1f}")
    if "--medir" in sys.argv:
        a = sys.argv[sys.argv.index("--medir") + 1:]
        medir(a[0], a[1] if len(a) > 1 else "40,50,60,70", a[2] if len(a) > 2 else "")
