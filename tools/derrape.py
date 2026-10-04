# Freno de mano (Espacio / botón A): doblando con la D, clavar el freno trasero y ver cuánto sale la cola. Corre sin
# ventana, con las rampas del teclado (--test teclas:), cada maniobra a cada velocidad, y resume el tramo con el
# freno trasero: cola (beta, máxima y mediana mientras va a más de 10 km/h, y la mediana a 10-25 / 25-45 / más de
# 45 km/h), cuánto giró el camino y el rumbo de la
# moto, cuánto frenó (g), cuánto anduvo la trasera trabada y si se cayó. "sale" es lo que pasa después de soltar
# (con gas y la D): la cola al final y si se cayó.
#
# Maniobras (todas a la derecha, con la D):
#   clava:        acelera hasta V, dobla con gas 1 s y clava el trasero sin soltar la D hasta 5 km/h
#   clava_sale:   lo mismo, pero suelta el freno a los 1.2 s y sale con gas doblando (¿sale del derrape?)
#   clava_suelta: clava el trasero y suelta la D a la vez (la moto se endereza frenando atrás)
#   recto:        derecho, el trasero solo hasta 5 km/h (no tiene que cruzarse más que antes)
#
# Uso (desde pruebas/, para los mapas de prueba):
#   python ../tools/derrape.py <exe> [--bike base/motocross] [--map prueba/plaza_tierra] [--vel 30,50,70]
#                              [--solo clava,recto] [--crudo]
import argparse, math, re, subprocess
from concurrent.futures import ThreadPoolExecutor

MANIOBRAS = [
    ('clava',        'W+V,D1,D_-5,4'),
    ('clava_sale',   'W+V,D1,D_1.2,WD2.5'),
    ('clava_suelta', 'W+V,D1,_-5,4'),
    ('recto',        'W+V,_-5,4'),
]
PAT = re.compile(r'(\w+)=\s*(-?[\d.]+)')
TEC = re.compile(r'^TECLAS (\d+) (\S+)\s.*?: *(\d+) -> *(\d+) km/h en ([\d.]+) s, ([\d.]+) m, (-?[\d.]+) g \| camino ([-+\d.]+) '
                 r'rumbo ([-+\d.]+) grados.*?incl ([-+\d]+) -> ([-+\d]+) \(max (\d+)\)(.*)$')


def run(exe, bike, mapa, name, script, v):
    s = script.replace('V', str(v))
    out = subprocess.run([exe, '--headless', '--map', mapa, '--bike', bike, '--test', 'teclas:' + s, '--time', '30',
                          '--telemetry', '--telemetry-dt', '0.02'], capture_output=True)
    lines = out.stdout.decode('utf-8', 'replace').replace('\0', '').splitlines()
    return v, name, s, lines


def resume(lines, step):
    # el tramo del freno trasero es el paso 3 del guion (W+V, WD1, el del freno)
    tec = {}
    for l in lines:
        m = TEC.match(l.strip())
        if m: tec[int(m.group(1))] = m
    rows = [dict(PAT.findall(l)) for l in lines if l.startswith('t=')]
    f = lambda r, k: float(r.get(k, 0))
    m = tec.get(step)
    if not m: return None
    # tiempos del paso: desde la suma de las duraciones de los anteriores
    t0 = float(m.group(0).split('t=')[1].split(':')[0])
    t1 = t0 + float(m.group(5))
    seg = [r for r in rows if t0 <= f(r, 't') <= t1]
    fast = [abs(f(r, 'beta')) for r in seg if f(r, 'v') > 10]
    locked = sum(0.02 for r in seg if f(r, 'rSlip') < -0.9)
    crash = any(r.get('crash') == '1' for r in rows if f(r, 't') >= t0)
    med = sorted(fast)[len(fast) // 2] if fast else 0.0
    def band(lo, hi):
        b = sorted(abs(f(r, 'beta')) for r in seg if lo < f(r, 'v') <= hi)
        return f"{b[len(b) // 2]:3.0f}" if b else '  -'
    bands = f"{band(10, 25)}/{band(25, 45)}/{band(45, 200)}"
    nxt = tec.get(step + 1)
    after = ''
    if nxt:
        t2 = float(nxt.group(0).split('t=')[1].split(':')[0]) + float(nxt.group(5))
        end = [r for r in rows if f(r, 't') <= t2][-1]
        after = f"sale: cola {abs(f(end, 'beta')):4.0f} incl {f(end, 'roll'):+4.0f} {float(nxt.group(4)):3.0f} km/h"
    return (f"{float(m.group(3)):3.0f}->{float(m.group(4)):3.0f} km/h {float(m.group(5)):4.2f} s {float(m.group(7)):4.2f} g | "
            f"cola max {max(fast) if fast else 0:4.0f} med {med:4.0f} ({bands}) | camino {float(m.group(8)):+5.0f} rumbo {float(m.group(9)):+5.0f} | "
            f"incl max {m.group(12):>2} | trabada {locked:4.2f} s | {after} {'CAIDA' if crash else ''}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('--bike', default='base/motocross')
    ap.add_argument('--map', default='prueba/plaza_tierra')
    ap.add_argument('--vel', default='30,50,70')
    ap.add_argument('--solo', default='')
    ap.add_argument('--crudo', action='store_true')
    a = ap.parse_args()
    sel = [x for x in MANIOBRAS if not a.solo or x[0] in a.solo.split(',')]
    jobs = [(a.exe, a.bike, a.map, n, s, int(v)) for n, s in sel for v in a.vel.split(',')]
    with ThreadPoolExecutor(max_workers=8) as ex:
        res = list(ex.map(lambda j: run(*j), jobs))
    for v, name, s, lines in res:
        if a.crudo:
            print(f'--- {v} {name} ({s})')
            print('\n'.join(l for l in lines if l.startswith('TECLAS')))
            continue
        step = 2 if name == 'recto' else 3
        print(f'{name:12} {v:3}  {resume(lines, step)}')


if __name__ == '__main__':
    main()
