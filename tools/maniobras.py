# Maniobras como con el teclado (--test teclas:GUION): frenar derecho, doblar y clavar los frenos (soltando o no la
# dirección), clavar y después doblar, tocar la dirección frenando, soltar una curva sin frenar, una chicana y un toque
# de freno doblando. Corre cada una sin ventana con --telemetry y resume, por maniobra, la frenada (línea FRENADA del
# juego: de cuánto a cuánto, metros, g, cuánto giró el camino, cuánto se corrió de costado, inclinación al empezar,
# deriva máxima y cola en el aire) o el último paso (las que no frenan). "al tirarse" es la deriva máxima mientras se
# tira a la curva (la cola afuera). Camino y costado: + = para el lado de la curva (todas doblan con la D, a la derecha).
#
# Uso (desde la carpeta del exe, o la que tenga los mods que querés):
#   python tools/maniobras.py <exe> [--bike base/carrera] [--map prueba/plaza_asfalto_ancha] [--vel 100,150,200]
#                              [--solo recta,suelta] [--crudo]
# --crudo: imprime las líneas TECLAS / FRENADA del juego tal cual. Necesita un mapa con una recta larga y ancha
# (plaza_asfalto_ancha: 1.5 km derechos; con 250 km/h hacen falta ~350 m para llegar y ~200 para frenar).
import argparse, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

MANIOBRAS = [
    ('recta',            'W+V,S-1'),                 # clavar los frenos derecho
    ('dobla_suelta',     'W+V,D1.5,S-1'),            # sin gas: doblar, soltar la D y clavar
    ('dobla_gas_suelta', 'W+V,WD1.5,S-1'),           # con gas: doblar, soltar todo y clavar
    ('dobla_clava',      'W+V,WD1.5,SD-1'),          # doblando, clavar sin soltar la D
    ('clava_dobla',      'W+V,S0.5,SD-1'),           # clavar y después doblar
    ('clava_dobla_suelta', 'W+V,S0.5,SD0.8,S-1'),    # clavar, doblar un poco y soltar
    ('toque_D',          'W+V,S0.5,SD0.15,S-1'),     # tocar la D frenando
    ('suelta_sin_frenar', 'W+V,WD1.5,W3'),           # doblar y soltar con gas: ¿se endereza y sigue derecho?
    ('chicana',          'W+V,WD1.2,WA1.2,W2'),
    ('toque_freno',      'W+V,WD1.5,SD0.4,WD1.5'),   # tocar el freno doblando
]
PAT = re.compile(r'^(TECLAS \d+ \S+|FRENADA)\s.*?: *(\d+) -> *(\d+) km/h en ([\d.]+) s, ([\d.]+) m, (-?[\d.]+) g \| camino ([-+\d.]+) '
                 r'rumbo ([-+\d.]+) grados, de costado ([-+\d.]+) m \| incl ([-+\d]+) -> ([-+\d]+) \(max (\d+)\) deriva max ([\d.]+) '
                 r'cabeceo min (-?[\d.]+) cola en el aire (\d+)%(.*)$')


def run(exe, bike, mapa, name, script, v):
    s = script.replace('V', str(v))
    out = subprocess.run([exe, '--headless', '--map', mapa, '--bike', bike, '--test', 'teclas:' + s, '--time', '40',
                          '--telemetry', '--telemetry-dt', '1000'], capture_output=True)
    lines = [l.strip() for l in out.stdout.decode('utf-8', 'replace').replace('\0', '').splitlines()]
    return v, name, s, [l for l in lines if l.startswith('TECLAS') or l.startswith('FRENADA')]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('--bike', default='base/carrera')
    ap.add_argument('--map', default='prueba/plaza_asfalto_ancha')
    ap.add_argument('--vel', default='100,150,200')
    ap.add_argument('--solo', default='')
    ap.add_argument('--crudo', action='store_true')
    a = ap.parse_args()
    sel = [m for m in MANIOBRAS if not a.solo or m[0] in a.solo.split(',')]
    jobs = [(a.exe, a.bike, a.map, n, s, int(v)) for v in a.vel.split(',') for n, s in sel]
    with ThreadPoolExecutor(max_workers=8) as ex:
        results = list(ex.map(lambda j: run(*j), jobs))
    for v, name, script, lines in results:
        if a.crudo:
            print(f'--- {v} {name} ({script})')
            print('\n'.join(lines))
            continue
        parsed = {}
        for l in lines:
            m = PAT.match(l)
            if m:
                parsed[m.group(1)] = m.groups()[1:]
        steps = sorted(k for k in parsed if k.startswith('TECLAS'))
        turnIn = [parsed[k][11] for k in steps if k.split()[2].lstrip('W').startswith('D')][:1]
        txt = f'{v:3d} {name:18s}'
        fr = parsed.get('FRENADA')
        if fr and name != 'toque_freno':
            v0, v1, T, dist, g, cam, rum, lat, i0, i1, imax, beta, pmin, air, crash = fr
            txt += f' FRENA {v0:>3}->{v1:>3} {float(dist):5.1f} m {g:>5} g | camino {cam:>6} costado {lat:>6} m | incl {i0:>3} deriva {beta:>4} cola {air:>2}%{crash}'
        elif steps:
            v0, v1, T, dist, g, cam, rum, lat, i0, i1, imax, beta, pmin, air, crash = parsed[steps[-1]]
            txt += f' FINAL {v0:>3}->{v1:>3} {float(dist):5.1f} m | camino {cam:>6} rumbo {rum:>6} costado {lat:>6} m | incl {i0:>3}->{i1:>3} deriva {beta:>4}{crash}'
            if fr:
                txt += f' | toque {fr[4]} g'
        if turnIn:
            txt += f' | al tirarse deriva {turnIn[0]}'
        print(txt)


if __name__ == '__main__':
    sys.exit(main())
