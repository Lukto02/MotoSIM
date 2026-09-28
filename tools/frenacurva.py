# Resume una prueba --test frenacurvaN (frenar y doblar a la vez): cuánto frena (de N a 30 km/h o hasta el
# final), cuánto dobla de verdad (cuánto giró el camino a 0.5, 1 y 2 s: rumbo de la moto menos beta, porque
# si la cola sale la moto gira sin que cambie hacia dónde va), cuánto se inclina, cuánto se cruza, si la
# trasera se clavó (uso de agarre > 1.5) y si se cayó. Con frenacurvaNn (sin freno) da la referencia.
# Uso (desde pruebas/): motocross.exe --headless --map prueba/plaza_asfalto --bike base/carrera \
#        --test frenacurva120 --time 12 --telemetry --telemetry-dt 0.05 | python ../tools/frenacurva.py
import math, re, sys
pat = re.compile(r'(\w+)=\s*(-?[\d.]+)')
rows = [dict(pat.findall(l)) for l in sys.stdin if l.startswith('t=')]
f = lambda r, k: float(r.get(k, 0))
start = next((i for i, r in enumerate(rows) if abs(f(r, 'steer')) > 0.5 or f(r, 'fb') > 0.3), None)
if start is None:
    sys.exit('no dobló ni frenó')
t0, v0, beta0 = f(rows[start], 't'), f(rows[start], 'v'), f(rows[start], 'beta')
heading, marks, rollMax, betaMax, rearLocked, t30, v30 = 0.0, {}, 0.0, 0.0, 0.0, None, None
prev = rows[start]
for r in rows[start + 1:]:
    heading += f(r, 'wy') * (f(r, 't') - f(prev, 't'))
    t = f(r, 't') - t0
    path = abs(math.degrees(heading) - (f(r, 'beta') - beta0))   # hacia dónde va, respecto del principio
    for m in (0.5, 1.0, 2.0):
        if m not in marks and t >= m:
            marks[m] = path
    rollMax = max(rollMax, abs(f(r, 'roll')))
    betaMax = max(betaMax, abs(f(r, 'beta')))
    if f(r, 'rUse') > 1.5:
        rearLocked += f(r, 't') - f(prev, 't')
    if t30 is None and f(r, 'v') < 30:
        t30, v30 = t, f(r, 'v')
    if r.get('crash') == '1':
        break
    prev = r
tEnd = t30 if t30 is not None else f(prev, 't') - t0
vEnd = v30 if t30 is not None else f(prev, 'v')
decel = (v0 - vEnd) / 3.6 / max(tEnd, 0.01) / 9.81
crash = any(r.get('crash') == '1' for r in rows[start:])
print(f"{v0:3.0f}->{vEnd:3.0f} km/h {tEnd:5.2f} s ({decel:.2f} g)  camino girado {marks.get(0.5, 0):3.0f}° {marks.get(1.0, 0):3.0f}° "
      f"{marks.get(2.0, 0):3.0f}° (0.5/1/2 s)  incl {rollMax:2.0f}°  beta {betaMax:4.1f}°  trasera clavada {rearLocked:.2f} s  "
      f"{'SE CAYÓ' if crash else 'ok'}")
