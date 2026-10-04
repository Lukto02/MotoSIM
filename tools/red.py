# Par en red en una sola PC, sin ventana: un anfitrión y un cliente, cada uno con su bot y su moto, y un
# resumen de lo que cada uno ve del otro (moto, distancia, ping, corrección). Sirve para probar dos
# versiones entre sí (p. ej. el paquete nuevo contra el anterior). Ver docs/RED.md.
# Uso: python tools/red.py <exe anfitrión> <moto> <exe cliente> <moto>
#   p. ej. python tools/red.py dist/v0.3.5/MotoSim-v0.3.5/MotoSim.exe base/trial compilaciones/build-msvc/motocross.exe base/carrera
# La salida del anfitrión va a un archivo en la carpeta temporal (nunca dentro de un paquete): con un pipe
# que nadie lee mientras corre el cliente, se llena, el anfitrión se traba en el printf y parece un
# problema de red.
import os, re, subprocess, sys, tempfile, time

if len(sys.argv) != 5:
    sys.exit(__doc__ or 'uso: python tools/red.py <exe anfitrión> <moto> <exe cliente> <moto>')
hexe, hbike, cexe, cbike = (os.path.abspath(a) if i % 2 == 0 else a for i, a in enumerate(sys.argv[1:5]))

log = os.path.join(tempfile.gettempdir(), 'motosim_red_anfitrion.txt')
with open(log, 'wb') as hout:
    host = subprocess.Popen([hexe, '--headless', '--host', '--bot', '--bike', hbike, '--test', 'netduel',
                             '--telemetry', '--time', '28'], cwd=os.path.dirname(hexe),
                            stdout=hout, stderr=subprocess.STDOUT)
    code, t0 = None, time.time()
    while code is None and time.time() - t0 < 15:
        time.sleep(0.2)
        m = re.search(r'([0-9A-Z]{5}-[0-9A-Z]{5})', open(log, 'rb').read().decode('utf-8', 'replace'))
        if m:
            code = m.group(1)
    if not code:
        host.kill()
        sys.exit('el anfitrión no dio código de invitación')
    client = subprocess.run([cexe, '--headless', '--join', code, '--bot', '--bike', cbike, '--test', 'netduel',
                             '--telemetry', '--time', '20'], cwd=os.path.dirname(cexe),
                            capture_output=True).stdout.decode('utf-8', 'replace').splitlines()
    host.wait()
hlines = open(log, 'rb').read().decode('utf-8', 'replace').splitlines()
os.remove(log)

def resumen(name, lines):
    net = [l.strip() for l in lines if l.strip().startswith('net ')]
    print(f'== {name}: {len(net)} líneas de red')
    for l in [l for l in lines if re.search(r'(?i)versi|error|no tiene|conectando', l) and not l.startswith('t=')][:4]:
        print('   ' + l.strip())
    for l in net[len(net) // 3::max(1, len(net) // 4)][:4]:
        print('   ' + l)

print('código', code)
resumen('anfitrión', hlines)
resumen('cliente', client)
