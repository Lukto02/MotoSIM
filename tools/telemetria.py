# Filtra la telemetría del juego (líneas "t= ...") y muestra sólo las columnas pedidas, opcionalmente en
# un rango de s (distancia sobre la pista) o de t.
# Uso: motocross.exe --headless ... --telemetry --telemetry-dt 0.05 | python tools/telemetria.py t v pitch fN rN gnd s [--s 70 95] [--t 5 9]
import re, sys

args = sys.argv[1:]
rng = {}
for key in ('--s', '--t'):
    if key in args:
        i = args.index(key)
        rng[key[2:]] = (float(args[i + 1]), float(args[i + 2]))
        del args[i:i + 3]
cols = args
pat = re.compile(r'(\w+)=\s*(-?[\d.]+)')
print('  '.join(f'{c:>8}' for c in cols))
# Las líneas de detalle que siguen a una de telemetría ("   suelo psi=...", "   whip ...") se suman a esa fila.
row = None


def emit(vals):
    if vals is None or any(k in vals and not (lo <= float(vals[k]) <= hi) for k, (lo, hi) in rng.items()):
        return
    print('  '.join(f'{vals.get(c, "-"):>8}' for c in cols))


for line in sys.stdin:
    if line.startswith('t='):
        emit(row)
        row = dict(pat.findall(line))
    elif row is not None and line.startswith(('   suelo ', '   whip ')):
        row.update(pat.findall(line))
emit(row)
