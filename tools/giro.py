# Resume una prueba --test giroN[k][xS] (dobla a velocidad constante): cuánto tarda en tirarse (la inclinación llega
# al 63% y al 90% de la final), cuánto tarda el camino en girar 90°, y el giro sostenido (promedio de los últimos
# segundos): velocidad, inclinación, guiñada del mundo (°/s), radio, g lateral, deriva y uso de agarre de las cubiertas.
# La guiñada del mundo es wy / cos(roll) (wy es la del eje de la moto). El camino es el rumbo menos la deriva.
# Uso: motocross.exe --headless --map prueba/plaza_tierra --bike base/carrera --test giro60 --time 14 \
#        --telemetry --telemetry-dt 0.02 | python tools/giro.py [--ultimos 3] [--tabla]
import math, re, sys

args = sys.argv[1:]
last = 3.0
if '--ultimos' in args:
    last = float(args[args.index('--ultimos') + 1])
pat = re.compile(r'(\w+)=\s*(-?[\d.]+)')
rows, t0 = [], None
for l in sys.stdin:
    l = l.replace('\0', '').strip()
    if l.startswith('giro: inicio'):
        t0 = float(dict(pat.findall(l))['t'])
    elif l.startswith('t='):
        rows.append({k: float(v) for k, v in pat.findall(l)})
if t0 is None or not rows:
    sys.exit('no empezó a doblar (no llegó a la velocidad)')
turn = [r for r in rows if r['t'] >= t0]
crash = next((r['t'] - t0 for r in turn if r.get('crash') == 1), None)
if crash is not None:
    turn = [r for r in turn if r['t'] - t0 < crash]
tEnd = turn[-1]['t']
steady = [r for r in turn if r['t'] >= tEnd - last]
avg = lambda k, rs=steady: sum(r[k] for r in rs) / len(rs)
v = avg('v') / 3.6
roll = avg('roll')
yawW = sum(r['wy'] / max(math.cos(math.radians(r['roll'])), 0.2) for r in steady) / len(steady)   # rad/s (+ = izquierda)
radius = v / abs(yawW) if abs(yawW) > 1e-3 else float('inf')
latG = v * abs(yawW) / 9.81
beta = avg('beta')
fUse, rUse = avg('fUse'), avg('rUse')
rollStd = math.sqrt(sum((r['roll'] - roll) ** 2 for r in steady) / len(steady))
# Entrada: inclinación al 63% / 90% de la final; camino girado 90°.
t63 = next((r['t'] - t0 for r in turn if abs(r['roll']) >= 0.63 * abs(roll)), None)
t90 = next((r['t'] - t0 for r in turn if abs(r['roll']) >= 0.90 * abs(roll)), None)
heading, prev, tPath90 = 0.0, turn[0], None
beta0 = turn[0]['beta']
for r in turn[1:]:
    heading += r['wy'] / max(math.cos(math.radians(r['roll'])), 0.2) * (r['t'] - prev['t'])
    if tPath90 is None and abs(math.degrees(heading) - (r['beta'] - beta0)) >= 90.0:
        tPath90 = r['t'] - t0
    prev = r
fmt = lambda x: '  -  ' if x is None else f'{x:5.2f}'
out = (f"v {avg('v'):5.1f} km/h  incl {abs(roll):4.1f}° (±{rollStd:3.1f})  guiñada {math.degrees(abs(yawW)):5.1f}°/s  "
       f"R {radius:6.1f} m  lat {latG:4.2f} g (tan incl {math.tan(math.radians(abs(roll))):4.2f})  beta {beta:5.1f}°  "
       f"uso D/T {fUse:4.2f}/{rUse:4.2f}  entrada 63% {fmt(t63)} s 90% {fmt(t90)} s  camino 90° {fmt(tPath90)} s"
       + (f"  SE CAYÓ a los {crash:.2f} s" if crash is not None else ''))
if '--tabla' in args:
    out = (f"{avg('v'):.0f}|{abs(roll):.0f}|{math.degrees(abs(yawW)):.0f}|{radius:.1f}|{latG:.2f}|{beta:.1f}|"
           f"{fUse:.2f}/{rUse:.2f}|{fmt(t63).strip()}|{fmt(t90).strip()}|{fmt(tPath90).strip()}|{'caída' if crash is not None else 'ok'}")
print(out)
