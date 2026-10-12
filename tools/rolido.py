# Barrido de cambios de dirección (--test slalomN...) con varias motos, velocidades y modos, una fila por corrida:
# rolido máximo (°/s), aceleración de rolido máxima (°/s²), de un lado al otro (cruza 0 / 90% del recorrido, s), cuánto
# se pasa (°), inclinación a la que llega en cada cambio (°, promedio) y si se cayó. Ver tools/slalom.py.
# Uso (desde pruebas/): python ../tools/rolido.py <exe> [--motos base/naked,base/carrera] [--vel 30,60,100] \
#        [--modos cambio,cambiok,s1,s1k,s2] [--tierra base/motocross,...]
# Los modos: cambio = un cambio de lado a lado entero cada 2 s con el stick (f0.25c2); cambiok, con el teclado; s1 / s2 =
# slalom a 1 o 2 Hz con el stick (3 ciclos); s1k, con el teclado; medio = el stick a medias (x0.5) cada 2 s.
import subprocess, sys

sys.stdout.reconfigure(encoding='utf-8')
args = sys.argv[1:]
exe = args[0]
def opt(name, default):
    return args[args.index(name) + 1] if name in args else default
calle = {'base/carrera', 'base/naked'}
motos = opt('--motos', 'base/motocross,base/motocross600,sandbox/dostiempos,base/trilheira,base/trial,base/carrera,base/naked').split(',')
vels = [int(v) for v in opt('--vel', '30,60,100,150,200').split(',')]
modos = opt('--modos', 'cambio,cambiok,s1,s1k,s2').split(',')
suf = {'cambio': 'f0.25c2', 'cambiok': 'f0.25c2k', 's1': 'f1c3', 's1k': 'f1c3k', 's2': 'f2c3', 'medio': 'f0.25c2x0.5', 'mediok': 'f0.25c2x0.5k'}
print('moto|km/h|modo|°/s máx|°/s² máx|cruza 0|90%|pasada|llega a|oscila|')
for m in motos:
    mapa = 'prueba/plaza_asfalto_ancha' if m in calle else 'prueba/plaza_tierra'
    for v in vels:
        if m not in calle and v > 100: continue
        if m == 'base/trial' and v > 70: v = 70
        if m == 'base/naked' and v > 180: v = 180
        for mo in modos:
            test = f'slalom{v}{suf[mo]}'
            p = subprocess.run([exe, '--headless', '--map', mapa, '--bike', m, '--test', test, '--time', '22',
                                '--telemetry', '--telemetry-dt', '0.008'], capture_output=True)
            r = subprocess.run([sys.executable, __file__.replace('rolido.py', 'slalom.py'), '--tabla'], input=p.stdout, capture_output=True)
            print(f"{m.split('/')[1]}|{v}|{mo}|{r.stdout.decode('utf-8', 'replace').strip() or r.stderr.decode('utf-8', 'replace').strip()}", flush=True)
