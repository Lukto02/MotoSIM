# Resume una prueba --test slalomN[fF][xS][cC][k] (la dirección de lado a lado a velocidad constante): por cada cambio,
# cuánto rola la moto y cómo: rolido máximo (°/s), aceleración de rolido máxima (°/s², derivada de wr suavizada 25 ms),
# tiempo hasta cruzar el 0 y hasta el 90% del recorrido, cuánto se pasa de la inclinación a la que llega y cuánto se
# mueve el piso de las ruedas de costado (fLat/rLat máximos). Al final, el peor de cada cosa.
# Uso: motocross.exe --headless --map prueba/plaza_asfalto_ancha --bike base/naked --test slalom100f0.25c2 --time 20 \
#        --telemetry --telemetry-dt 0.008 | python tools/slalom.py [--tabla]
# Con --telemetry-dt menor que el paso (1/120 s) sale una fila por paso: hace falta para la aceleración de rolido.
import math, re, sys

sys.stdout.reconfigure(encoding='utf-8')

pat = re.compile(r'(\w+)=\s*(-?[\d.]+)')
rows, changes = [], []
for l in sys.stdin:
    l = l.replace('\0', '').strip()
    if l.startswith('slalom: cambio'):
        d = dict(pat.findall(l))
        changes.append((float(d['t']), int(float(d['dir']))))
    elif l.startswith('t='):
        rows.append({k: float(v) for k, v in pat.findall(l)})
if not changes or not rows:
    sys.exit('no empezó el slalom (no llegó a la velocidad)')
crash = next((r['t'] for r in rows if r.get('crash') == 1), None)
# Aceleración de rolido: derivada central de wr (rad/s) sobre +-1 fila, suavizada con 3 filas.
acc = [0.0] * len(rows)
for i in range(1, len(rows) - 1):
    dt = rows[i + 1]['t'] - rows[i - 1]['t']
    acc[i] = (rows[i + 1]['wr'] - rows[i - 1]['wr']) / dt if dt > 0 else 0.0
acc = [sum(acc[max(0, i - 1):i + 2]) / len(acc[max(0, i - 1):i + 2]) for i in range(len(acc))]

out = []
worst = {'rate': 0.0, 'acc': 0.0, 'over': 0.0, 'lat': 0.0}
swing90, swing0, amps = None, None, []   # el primer cambio de un lado al otro; lo que llega en cada uno
for n, (tc, d) in enumerate(changes):
    tn = changes[n + 1][0] if n + 1 < len(changes) else rows[-1]['t']
    seg = [(i, r) for i, r in enumerate(rows) if tc <= r['t'] < tn]
    if crash is not None and crash < tn:
        seg = [(i, r) for i, r in seg if r['t'] < crash]
    if len(seg) < 4:
        continue
    r0 = seg[0][1]['roll']
    r1 = seg[-1][1]['roll']                      # adonde llegó al final del medio ciclo
    sw = r1 - r0
    sgn = 1.0 if sw >= 0 else -1.0
    rate = max(abs(math.degrees(r['wr'])) for _, r in seg)
    inner = seg[:-2]                             # las últimas filas ya ven el cambio siguiente
    a = max(abs(math.degrees(acc[i])) for i, _ in inner)
    tAcc = next(r['t'] - tc for i, r in inner if abs(math.degrees(acc[i])) >= a - 1e-6)
    t0 = next((r['t'] - tc for _, r in seg if (r['roll'] - 0.0) * sgn >= 0 and r0 * sgn < 0), None)
    t90 = next((r['t'] - tc for _, r in seg if abs(sw) > 2 and (r['roll'] - r0) / sw >= 0.9), None)
    peak = max((r['roll'] - r1) * sgn for _, r in seg)
    lat = max(max(abs(r.get('fLat', 0)), abs(r.get('rLat', 0))) for _, r in seg)
    if d != 0 and abs(r0) > 2 and r0 * d < 0:
        amps.append(abs(r1))
        if swing90 is None:
            swing90, swing0 = t90, t0
    worst['rate'] = max(worst['rate'], rate)
    worst['acc'] = max(worst['acc'], a)
    worst['over'] = max(worst['over'], peak)
    worst['lat'] = max(worst['lat'], lat)
    f = lambda x: '  -  ' if x is None else f'{x:5.2f}'
    out.append(f"t={tc:6.2f} dir={d:+d}  {r0:6.1f}° -> {r1:6.1f}°  rolido máx {rate:5.0f}°/s  acel máx {a:6.0f}°/s² (a los {tAcc:4.2f} s)  "
               f"cruza 0 {f(t0)} s  90% {f(t90)} s  pasada {peak:4.1f}°  lat máx {lat:5.0f} N")
if '--tabla' in sys.argv:
    g = lambda x: '-' if x is None else f'{x:.2f}'
    amp = f'{sum(amps) / len(amps):.0f}' if amps else '-'
    # El rolido entre el segundo cambio y el último (la oscilación ya armada): ± cuánto y alrededor de qué (centrada = 0).
    tA = changes[1][0] if len(changes) > 2 else changes[0][0]
    tB = next((tc for tc, d in changes if d == 0 and tc > tA), rows[-1]['t'])
    osc = [r['roll'] for r in rows if tA <= r['t'] < tB and (crash is None or r['t'] < crash)]
    osc = f"±{(max(osc) - min(osc)) / 2:.0f} c {(max(osc) + min(osc)) / 2:+.0f}" if osc else '-'
    print(f"{worst['rate']:.0f}|{worst['acc']:.0f}|{g(swing0)}|{g(swing90)}|{worst['over']:.1f}|{amp}|{osc}|{'caída %.1f' % crash if crash is not None else 'ok'}")
else:
    print('\n'.join(out))
    print(f"peor: rolido {worst['rate']:.0f}°/s  aceleración {worst['acc']:.0f}°/s²  pasada {worst['over']:.1f}°  lat {worst['lat']:.0f} N"
          + (f"  SE CAYÓ a los {crash:.2f} s" if crash is not None else ''))
