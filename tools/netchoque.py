# Choques entre motos en red (prueba netchoque, ver docs/RED.md): para cada caso, un anfitrión que recibe el
# golpe y un invitado que lo embiste, en el terreno plano, y lo que dice cada uno (si se cayó, el choque más
# fuerte que midió su PC y la velocidad al tocarse).
# Uso: python tools/netchoque.py <exe anfitrión> <exe invitado> [opciones] caso...
#   caso: TIPO-KMH[-KMH]: lado-40 (de costado a 40, el otro quieto), atras-40, atras-60-30 (a 60 contra uno
#   a 30), frente-30 (los dos a 30), roce-40 (se le cierra 15°, los dos a 40), ang45-40...
#   --lag MS: cada instancia demora MS ms lo que recibe (como por internet: ida y vuelta ~2*MS).
#   --jitter MS: más una demora al azar de hasta MS (los paquetes llegan en tandas).
#   --bike mod/moto: los dos con esa moto.
#   --ventana: el anfitrión con ventana (el bucle real, por cuadro); sin eso, los dos sin ventana.
#   --telemetria: además, las líneas CHOQUE de cada uno.
#   --guardar PREFIJO: deja la salida de cada uno en PREFIJO_<caso>_anfitrion.txt / _invitado.txt.
# La salida del anfitrión va a un archivo (con un pipe que nadie lee, se llena y el anfitrión se traba).
import os, re, subprocess, sys, tempfile, time

args = sys.argv[1:]
if len(args) < 3:
    sys.exit('uso: python tools/netchoque.py <exe anfitrión> <exe invitado> [--lag MS] [--jitter MS] [--bike mod/moto] [--ventana] caso...')
hexe, cexe = os.path.abspath(args[0]), os.path.abspath(args[1])
extra, cases, show, window, keep = [], [], False, False, None
flags = {'--bike': '--bike', '--lag': '--net-lag', '--jitter': '--net-jitter'}
i = 2
while i < len(args):
    if args[i] in flags:
        extra += [flags[args[i]], args[i + 1]]
        i += 2
    elif args[i] == '--telemetria':
        show = True
        i += 1
    elif args[i] == '--guardar':
        keep = args[i + 1]
        i += 2
    elif args[i] == '--ventana':
        window = True
        i += 1
    else:
        cases.append(args[i])
        i += 1

log = os.path.join(tempfile.gettempdir(), 'motosim_netchoque_anfitrion.txt')
for case in cases:
    test = 'netchoque-' + case
    look = ['--size', '640', '360', '--windowed'] if window else ['--headless']
    with open(log, 'wb') as hout:
        host = subprocess.Popen([hexe] + look + ['--flat', '--host', '--test', test, '--telemetry', '--telemetry-dt', '1'] + extra +
                                ['--time', '14'], cwd=os.path.dirname(hexe), stdout=hout, stderr=subprocess.STDOUT)
        code, t0 = None, time.time()
        while code is None and time.time() - t0 < 20:
            time.sleep(0.2)
            m = re.search(r'([0-9A-Z]{5}-[0-9A-Z]{5})', open(log, 'rb').read().decode('utf-8', 'replace'))
            if m:
                code = m.group(1)
        if not code:
            host.kill()
            sys.exit('el anfitrión no dio código de invitación')
        client = subprocess.run([cexe, '--headless', '--flat', '--join', code, '--test', test, '--telemetry', '--telemetry-dt', '1'] + extra +
                                ['--time', '11'], cwd=os.path.dirname(cexe), capture_output=True).stdout.decode('utf-8', 'replace').splitlines()
        host.wait()
    hlines = open(log, 'rb').read().decode('utf-8', 'replace').splitlines()
    if keep:
        for suffix, lines in (('anfitrion', hlines), ('invitado', client)):
            open(f'{keep}_{case}_{suffix}.txt', 'wb').write(chr(10).join(lines).encode('utf-8'))
    rtt = [re.search(r'rtt=([\d.]+)ms', l) for l in client if ' net ' in l]
    rtt = [float(m.group(1)) for m in rtt if m]
    print('==', case + (f'  (ida y vuelta {rtt[-1]:.0f} ms)' if rtt else ''))
    for name, lines in (('anfitrión', hlines), ('invitado', client)):
        summary = [l for l in lines if l.startswith('NETCHOQUE') and ':' in l and 'desde' not in l]
        if not summary:              # otra versión (sin la prueba, quieta en la largada): lo que imprimió
            hits = [float(m.group(1)) for m in (re.search(r'se acercaban a ([\d.]+)', l) for l in lines) if m]
            fell = any(re.search(r'crash=1', l) for l in lines if l.startswith('t='))
            summary = [f'(sin la prueba) {"SE CAYO" if fell else "no se cayo"}; choque mas fuerte: cierre {max(hits, default=0):.2f} m/s']
        # La regla de hasta la v0.2.6 con los mismos contactos (la velocidad del cuerpo cinemático, entre
        # corchetes): más de 6 m/s y que el otro trajera más del 45%. Si la nueva ya lo tiró, no se ven los
        # contactos de después.
        old = [(float(a), float(b)) for a, b in (re.search(r'\[cuerpo ([\d.]+)/([\d.]+)\]', l).groups() for l in lines
                                                 if l.startswith('CHOQUE') and '[cuerpo' in l)]
        oldFell = any(c > 6.0 and f > 0.45 * c for c, f in old)
        print(f'   {name:9s} {summary[-1]}' + (f'  | regla v0.2.6: {"se caia" if oldFell else "no se caia"}' if old else ''))
        if show:
            for l in [l for l in lines if l.startswith('CHOQUE')][:12]:
                print('      ' + l)
try:
    os.remove(log)
except OSError:
    pass
