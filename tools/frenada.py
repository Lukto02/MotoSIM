# Resume una prueba --test frenadaN: de cuánto a 30 km/h y en cuánto, desaceleración media, cuánto se cruzó
# la moto (beta), cuánto giró el manubrio, qué fracción del tiempo la cola estuvo en el aire y si se cayó.
# Uso: motocross.exe --headless --map base/circuito --bike base/carrera --test frenada250 --time 18 \
#        --telemetry --telemetry-dt 0.05 | python tools/frenada.py
import re, sys
pat = re.compile(r'(\w+)=\s*(-?[\d.]+)')
rows = [dict(pat.findall(l)) for l in sys.stdin if l.startswith('t=')]
b = [r for r in rows if float(r.get('fb', 0)) > 0.5]
if not b:
    sys.exit('no frenó')
t0, v0 = float(b[0]['t']), float(b[0]['v'])
stop = [r for r in b if float(r['v']) < 30]
t1 = float(stop[0]['t']) if stop else float(b[-1]['t'])
upTo = [r for r in b if float(r['t']) <= t1]
print(f"de {v0:.0f} a 30 km/h en {t1 - t0:.2f} s ({(v0 - 30) / 3.6 / max(t1 - t0, 0.01):.1f} m/s²)  "
      f"beta máx {max(abs(float(r['beta'])) for r in upTo):.1f}°  manubrio máx {max(abs(float(r['steer'])) for r in upTo):.1f}°  "
      f"cola en el aire {100 * sum(1 for r in upTo if r['gnd'] == '10') / len(upTo):.0f}%  "
      f"{'SE CAYÓ' if any(r['crash'] == '1' for r in b) else 'sin caída'}")
