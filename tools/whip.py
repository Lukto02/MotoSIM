# Resume los vuelos de una prueba (whip, giros en el aire): por cada tramo con las dos ruedas en el aire, cuánto se
# acostó la moto (roll, respecto del mundo), cuánto se cruzó respecto de hacia dónde vuela (beta), con cuánto llegó al
# piso y qué pasó después (serpenteo: la cola máxima en el primer segundo apoyada; caída).
#
# Uso:
#   python tools/whip.py <exe> [args del juego...]            corre el exe sin ventana (desde su carpeta) y resume
#   motocross.exe --headless ... --telemetry --telemetry-dt 0.01 | python tools/whip.py -   lee la telemetría de stdin
# El exe corre desde la carpeta actual (los mapas de prueba/: desde pruebas/, con ../tools/whip.py).
# Ejemplos:
#   python tools/whip.py compilaciones/build-msvc-fisica/motocross.exe --flat --drop 1 --test whip --time 5     (desde la raíz)
#   python ../tools/whip.py ../compilaciones/build-msvc-fisica/motocross.exe --map prueba/salto_whip --test whipkick50 --time 14
#                                                                                                    (desde pruebas/)
# Columnas: despegue (t, km/h, roll y su velocidad wr, guiñada wy, velocidad hacia arriba y cuánto sube con eso), en el aire (roll máx, beta máx, si volvió sola:
# |roll| y |beta| al tocar), aterrizaje (roll y beta al tocar, cola máxima en el primer segundo) y caída.
import os, re, subprocess, sys

pat = re.compile(r'(\w+)=\s*(-?[\d.]+)')


def rows_from(lines):
    for line in lines:
        if not line.startswith('t='):
            if 'CAIDA' in line or 'GOLPE' in line:
                yield {'_msg': line.strip()}
            continue
        d = {k: float(v) for k, v in pat.findall(line)}
        g = re.search(r'gnd=(\d)(\d)', line)
        d['gf'], d['gr'] = (int(g.group(1)), int(g.group(2))) if g else (1, 1)
        yield d


def summarize(rows):
    # Un toque de menos de 0.05 s en el medio de un vuelo (la trasera que vuelve a rozar el labio) no lo corta.
    flights = []
    cur = None
    prev = None
    crashT = None
    touch = None
    for d in rows:
        if '_msg' in d:
            continue
        if d.get('crash', 0) >= 1 and crashT is None:
            crashT = d['t']
        air = d['gf'] == 0 and d['gr'] == 0
        if air and cur is None and prev is not None and d['t'] > 0.05:
            cur = {'t0': d['t'], 'v0': prev['v'], 'roll0': prev['roll'], 'wr0': prev.get('wr', 0), 'wy0': prev.get('wy', 0),
                   'beta0': prev.get('beta', 0), 'vy0': prev.get('vy', 0), 'maxRoll': 0.0, 'maxBeta': 0.0, 'samples': 0}
        if air and cur is not None and 'land' not in cur:
            touch = None
            cur['samples'] += 1
            if abs(d['roll']) > abs(cur['maxRoll']):
                cur['maxRoll'] = d['roll']
            if abs(d.get('beta', 0)) > abs(cur['maxBeta']):
                cur['maxBeta'] = d.get('beta', 0)
            cur['last'] = d
        if not air and cur is not None and 'land' not in cur:
            if touch is None:
                touch = d
            if d['t'] - touch['t'] >= 0.05:
                cur['land'] = touch
                cur['tl'] = touch['t']
                cur['after'] = 0.0
                flights.append(cur)
        if cur is not None and 'land' in cur:
            if d['t'] - cur['tl'] <= 1.0:
                cur['after'] = max(cur['after'], abs(d.get('beta', 0)))
                cur['afterRoll'] = max(cur.get('afterRoll', 0.0), abs(d['roll']))
            else:
                cur = None
                touch = None
        prev = d
    out = []
    for f in flights:
        if f['tl'] - f['t0'] < 0.15:
            continue
        L = f['land']
        out.append(f"vuelo t={f['t0']:5.2f}-{f['tl']:5.2f} ({f['tl'] - f['t0']:.2f} s) despega {f['v0']:4.0f} km/h roll {f['roll0']:+5.1f} "
                   f"wr {f['wr0']:+5.2f} wy {f['wy0']:+5.2f} vy {f['vy0']:4.2f} (sube {f['vy0'] ** 2 / 19.62:4.2f} m) | aire: roll max {f['maxRoll']:+6.1f} beta max {f['maxBeta']:+6.1f} | "
                   f"toca: roll {L['roll']:+6.1f} beta {L.get('beta', 0):+6.1f} | 1 s despues: cola max {f['after']:4.1f} roll max {f.get('afterRoll', 0):4.1f}"
                   + (f" | CAIDA t={crashT:.2f}" if crashT is not None and f['t0'] - 0.1 <= crashT <= f['tl'] + 1.5 else ''))
    if crashT is not None:
        out.append(f"caida a los {crashT:.2f} s")
    return out


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == '-':
        lines = sys.stdin
    elif len(sys.argv) >= 2:
        exe = os.path.abspath(sys.argv[1])
        args = sys.argv[2:]
        if '--telemetry-dt' not in args:
            args += ['--telemetry', '--telemetry-dt', '0.01']
        p = subprocess.run([exe, '--headless'] + args, capture_output=True, text=True, errors='replace')
        lines = p.stdout.splitlines()
    else:
        print(__doc__ if __doc__ else 'uso: whip.py <exe> [args] | whip.py -')
        return
    for line in summarize(rows_from(lines)):
        print(line)


if __name__ == '__main__':
    main()
