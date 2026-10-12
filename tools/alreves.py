# Frenar y doblar: ¿la moto dobla para el otro lado? Corre sin ventana dos barridos y marca los casos malos:
#  - stick: frenacurvaNxS (llega derecho a N km/h, frena a fondo con la S y dobla a la derecha con el manubrio S, 0..1, fijo
#    como un stick): FRENADA resume cuánto giró el camino y cuánto se corrió de costado (+ = para la derecha, hacia la curva).
#    "AL REVES": el camino gira o se corre para la izquierda;
#  - teclas: guiones de teclado (rampas de Game::KeyboardInput) doblando con la D y frenando de varias formas (la S sin soltar
#    la D, soltándola, esperando y clavando, sólo el trasero con Espacio, la D a medias con toques, todo a la vez desde derecho).
#    La frenada va partida en pasos de 0.1 s (líneas TECLAS) y en cada uno se mira cuánto giró el camino y la inclinación al
#    terminar. "CRUZADA": más de 0.3 s inclinada para un lado (5° o más) con el camino girando para el otro (5°/s o más), a más
#    de 30 km/h; "AL REVES": el camino acumulado gira más de 3° para la izquierda.
# Ver docs/FISICA.md, "Frenando y doblando rápido, la moto doblaba para el otro lado".
#
# Uso (desde la carpeta del exe con los mapas de prueba, p. ej. pruebas/):
#   python ../tools/alreves.py <exe> [--bike base/carrera] [--map prueba/plaza_asfalto_ancha] [--vel 100,150,200,250,280]
#                                    [--modo stick|teclas|todo] [--todos]
# --todos: imprime todos los casos (si no, sólo los malos y el total).
import argparse, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

FRENADA = re.compile(r'FRENADA.*?([\d.]+) g \| camino ([-+\d.]+) rumbo ([-+\d.]+) grados, de costado ([-+\d.]+) m \| '
                     r'incl ([-+\d]+) -> ([-+\d]+) \(max (\d+)\) deriva max ([\d.]+).*?%(.*)$')
TECLAS = re.compile(r'^TECLAS (\d+) \S+\s+t=\s*[\d.]+: *(\d+) -> *(\d+) km/h en ([\d.]+) s, [\d.]+ m, -?[\d.]+ g \| camino ([-+\d.]+) '
                    r'rumbo [-+\d.]+ grados, de costado [-+\d.]+ m \| incl ([-+\d]+) -> ([-+\d]+) \(max \d+\) deriva max ([\d.]+).*?(CAIDA)?$')
STICK = (0.06, 0.1, 0.15, 0.2, 0.3, 0.5, 1.0)


def guiones(v):
    """(nombre, antes de frenar, teclas de la frenada). Todas doblan a la derecha con la D."""
    out = []
    for T in (0.3, 0.7, 1.5):                        # entrando, a mitad y ya en la curva
        out += [(f'clavaD_{T}', f'W+{v},WD{T}', 'SD'),           # doblando, clava sin soltar la D
                (f'suelta_{T}', f'W+{v},WD{T}', 'S'),            # suelta la D y clava
                (f'suelta_sin_gas_{T}', f'W+{v},D{T}', 'S'),
                (f'traseroD_{T}', f'W+{v},WD{T}', '_D'),         # sólo el trasero, doblando
                (f'trasero_{T}', f'W+{v},WD{T}', '_'),           # suelta la D y clava el trasero
                (f'ambosD_{T}', f'W+{v},WD{T}', 'S_D'),
                (f'medio_{T}', f'W+{v},WD{T}', 'MEDIO')]         # la D a medias (toques) frenando
        out += [(f'suelta_espera{d}_{T}', f'W+{v},WD{T},W{d}', 'S') for d in (0.15, 0.35, 0.6)]   # al levantar
    out += [('clava_y_dobla', f'W+{v},S0.3', 'SD'), ('juntos', f'W+{v}', 'SD'), ('juntos_S_', f'W+{v}', 'S_D'),
            ('juntos_medio', f'W+{v}', 'MEDIO'), ('juntos_trasero', f'W+{v}', '_D'), ('tirarse_y_clavar', f'W+{v},WD0.15', 'SD')]
    return out


def correr(exe, bike, mapa, test):
    out = subprocess.run([exe, '--headless', '--map', mapa, '--bike', bike, '--test', test, '--time', '45',
                          '--telemetry', '--telemetry-dt', '1000'], capture_output=True)
    return out.stdout.decode('utf-8', 'replace').replace('\0', '').splitlines()


def stick(exe, bike, mapa, v, s):
    for l in correr(exe, bike, mapa, f'frenacurva{v}x{s}'):
        m = FRENADA.search(l)
        if m:
            g, cam, rum, lat, i0, i1, imax, beta, rest = m.groups()
            malo = float(cam) < -3.0 or float(lat) < -3.0
            return malo, (f'{v:3d} stick x{s:<4} {g} g  camino {cam:>7}  costado {lat:>6} m  incl max {imax:>2}  deriva {beta:>4}'
                          f'{rest}{"  <-- AL REVES" if malo else ""}')
    return True, f'{v:3d} stick x{s:<4} sin FRENADA (no llego a {v} km/h?)'


def teclas(exe, bike, mapa, v, nombre, antes, frena):
    pasos = (','.join(('SD0.05' if i % 2 == 0 else 'S0.05') for i in range(160)) if frena == 'MEDIO'
             else ','.join(f'{frena}0.1' for _ in range(80)))
    nAntes = antes.count(',') + 1
    filas, caida = [], False
    for l in correr(exe, bike, mapa, 'teclas:' + antes + ',' + pasos):
        m = TECLAS.match(l.strip())
        if not m:
            continue
        caida = caida or bool(m.group(9))
        if int(m.group(1)) > nAntes and float(m.group(2)) > 10.0:
            filas.append((float(m.group(2)), float(m.group(4)), float(m.group(5)), int(m.group(7)), float(m.group(8))))
    acum = minAcum = cruz = 0.0
    peor = None
    for v0, T, cam, incl, beta in filas:
        acum += cam
        minAcum = min(minAcum, acum)
        tasa = cam / max(T, 1e-3)
        if v0 > 30 and abs(incl) >= 5 and abs(tasa) >= 5 and incl * tasa < 0:
            cruz += T
            if peor is None or abs(tasa) > abs(peor[1]):
                peor = (incl, tasa, v0)
    malo = minAcum < -3.0 or cruz >= 0.3
    txt = (f'{v:3d} {nombre:20s} camino {acum:+7.1f}  min {minAcum:+6.1f}  incl min {min((f[3] for f in filas), default=0):+3d}  '
           f'deriva {max((f[4] for f in filas), default=0):4.1f}{"  CAIDA" if caida else ""}')
    if minAcum < -3.0:
        txt += '  <-- AL REVES'
    if cruz >= 0.3:
        txt += f'  <-- CRUZADA {cruz:.1f} s (inclinada {peor[0]:+d}, camino {peor[1]:+.0f} grados/s a {peor[2]:.0f} km/h)'
    return malo, txt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('--bike', default='base/carrera')
    ap.add_argument('--map', default='prueba/plaza_asfalto_ancha')
    ap.add_argument('--vel', default='100,150,200,250,280')
    ap.add_argument('--modo', default='todo', choices=('stick', 'teclas', 'todo'))
    ap.add_argument('--todos', action='store_true')
    a = ap.parse_args()
    trabajos = []
    for v in (int(x) for x in a.vel.split(',')):
        if a.modo in ('stick', 'todo'):
            trabajos += [(stick, (a.exe, a.bike, a.map, v, s)) for s in STICK]
        if a.modo in ('teclas', 'todo'):
            trabajos += [(teclas, (a.exe, a.bike, a.map, v) + g) for g in guiones(v)]
    with ThreadPoolExecutor(max_workers=12) as ex:
        res = list(ex.map(lambda j: j[0](*j[1]), trabajos))
    malos = 0
    for malo, txt in res:
        malos += malo
        if malo or a.todos:
            print(txt)
    print(f'{a.bike}: {malos} de {len(res)} casos doblan al reves o quedan cruzados')


if __name__ == '__main__':
    sys.exit(main())
