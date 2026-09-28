"""Resume los CSV del barrido de holgura (barrido.sh): por moto y grupo de poses, cuánto se mete cada zona.

Uso: python tools/holgura/tabla.py <carpeta> <etiqueta> [<etiqueta2>] [--p 90]
Cada celda: lo más adentro (cm) / el percentil p de las poses del grupo. Con dos etiquetas: "antes -> después".
No cuentan el agarre (la mano en el puño) ni la bota en la estribera: son el contacto que tiene que haber.
"""
import csv
import os
import sys

BIKES = ['motocross', 'carrera', 'trial', 'trilheira', 'dostiempos']
GROUPS = ['suelo', 'pata afuera', 'aire', 'cola', 'aterrizaje', 'rapido']
ZONES = [('casco', ['casco']), ('torso', ['torso']), ('cadera', ['cadera']), ('brazo', ['brazoI', 'brazoD']),
         ('antebrazo', ['antebrI', 'antebrD']), ('mano', ['manoI', 'manoD']), ('muslo', ['musloI', 'musloD']),
         ('canilla', ['canillaI', 'canillaD']), ('bota', ['botaI', 'botaD'])]


def load(folder, tag, bike):
    path = os.path.join(folder, '%s_%s.csv' % (tag, bike))
    if not os.path.exists(path):
        return None
    return [r for r in csv.DictReader(open(path, encoding='utf-8', errors='replace')) if None not in r.values() and r.get('botaD_donde')]


def pct(values, p):
    v = sorted(values)
    return v[min(len(v) - 1, int(round(p / 100.0 * (len(v) - 1))))] if v else 0.0


def summary(rows, group, p):
    rs = rows if group == 'todo' else [r for r in rows if r['grupo'] == group]
    if not rs:
        return None
    out = {}
    for name, cols in ZONES:
        vals = [max(float(r[c]) for c in cols) for r in rs]
        out[name] = (max(vals), pct(vals, p))
    return out, len(rs)


def main():
    args = sys.argv[1:]
    p = 90.0
    if '--p' in args:
        k = args.index('--p')
        p = float(args[k + 1])
        del args[k:k + 2]
    folder, tags = args[0], args[1:]
    for bike in BIKES:
        data = [load(folder, t, bike) for t in tags]
        if not data[0]:
            continue
        print('\n### %s  (cm: máximo / percentil %d)' % (bike, p))
        print('| grupo | poses | ' + ' | '.join(z for z, _ in ZONES) + ' |')
        print('|---|---|' + '---|' * len(ZONES))
        for g in GROUPS + ['todo']:
            sums = [summary(d, g, p) if d else None for d in data]
            if not sums[0]:
                continue
            cells = [' -> '.join('?' if s is None else '%.1f/%.1f' % s[0][z] for s in sums) for z, _ in ZONES]
            print('| %s | %d | %s |' % (g, sums[0][1], ' | '.join(cells)))


if __name__ == '__main__':
    main()
