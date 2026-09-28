# Resume el paso por un tramo de la pista (p. ej. la escadaria de la favela: --s 74 90): fuerza máxima en
# cada rueda, cabeceo extremo, fracción en el aire, el mayor salto de velocidad entre muestras y caídas.
# Uso: ... --telemetry --telemetry-dt 0.025 | python tools/escalera.py 74 90
import re, sys
s0, s1 = float(sys.argv[1]), float(sys.argv[2])
pat = re.compile(r'(\w+)=\s*(-?[\d.]+)')
rows = [dict(pat.findall(l)) for l in sys.stdin if l.startswith('t=')]
seg = [r for r in rows if s0 <= float(r['s']) <= s1]
if not seg:
    sys.exit('no pasó por el tramo')
f = lambda k: [float(r[k]) for r in seg]
v = f('v')
jump = max((abs(b - a) for a, b in zip(v, v[1:])), default=0)
air = sum(1 for r in seg if r['gnd'] == '00') / len(seg)
print(f"v {min(v):4.0f}-{max(v):4.0f} km/h  fN máx {max(f('fN')):6.0f}  rN máx {max(f('rN')):6.0f}  "
      f"cabeceo {min(f('pitch')):6.1f}..{max(f('pitch')):5.1f}°  en el aire {air * 100:3.0f}%  salto de v {jump:4.1f} km/h  "
      f"{'CAYÓ' if any(r['crash'] == '1' for r in seg) else 'ok'}  ({float(seg[-1]['t']) - float(seg[0]['t']):.1f} s)")
