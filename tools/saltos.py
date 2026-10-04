# Mide un salto sin ventana a varias velocidades: aparece en la pista (--spawn S), entra al salto a N
# km/h y resume el vuelo que despega cerca de S_LABIO: velocidad al despegar, tiempo en el aire, altura
# máxima sobre el suelo, velocidad vertical al tocar, cabeceo al tocar, compresión máxima al caer y si
# se cae (y si fue por un golpe fuerte).
#
# Dos pilotos:
#   bajada: --test bajadaN (sostiene N km/h con el gas, sigue la línea y no toca el cuerpo: "sin tocar nada";
#           en el aire sigue con algo de gas);
#   bot:    el bot de pruebas a N km/h (en el aire suelta el gas y busca caer con la trompa 8° abajo, como
#           un piloto que acomoda el cuerpo). El bot toma la velocidad del mapa ("bot_speed"): se usa una
#           copia temporal del mapa con otra velocidad en pruebas/mods/prueba/maps/ (se borra al terminar).
#
# Uso (desde la raíz del proyecto):
#   python tools/saltos.py <exe> <mod/mapa> <spawn> <s_labio> <km/h,km/h,...> [bajada|bot|ambos] [--time T]
#   p. ej. python tools/saltos.py compilaciones/build-msvc/motocross.exe sandbox/medanos 240 446 40,50,60 ambos --time 30
# s_labio: la s de la pista donde despega (--test profile la da; o correr una vez y mirar "despega").
import os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAT = re.compile(r'(\w+)=\s*(-?[\d.]+)')


def find_map(mod_map):
    mod, name = mod_map.split("/", 1)
    for base in (os.path.join(ROOT, "mods"), os.path.join(ROOT, "pruebas", "mods")):
        p = os.path.join(base, mod, "maps", name + ".json")
        if os.path.isfile(p):
            return p
    sys.exit("no encuentro el mapa " + mod_map)


def run(exe, cwd, mapa, spawn, T, test):
    cmd = [exe, "--headless", "--map", mapa, "--spawn", str(spawn), "--time", str(T), "--telemetry", "--telemetry-dt", "0.02"]
    cmd += ["--test", test] if test else ["--bot"]
    out = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors="replace").stdout
    rows, golpes = [], []
    for line in out.splitlines():
        if line.startswith("t="):
            d = {k: float(v) for k, v in PAT.findall(line)}
            d["gnd"] = line.split("gnd=")[1][:2]
            rows.append(d)
        elif line.startswith("GOLPE"):
            golpes.append(float(line.split("t=")[1].split()[0]))
    return rows, golpes


def flights(rows):
    out, cur, prev = [], None, None
    for r in rows:
        air = r["gnd"] == "00"
        if air and cur is None:
            cur = dict(t0=r["t"], v0=prev["v"] if prev else r["v"], s0=r["s"], hmax=r["h"])
        if air:
            cur.update(hmax=max(cur["hmax"], r["h"]), t1=r["t"], vy1=r["vy"], s1=r["s"], p1=r["pitch"])
        elif cur is not None:
            post = [q for q in rows if r["t"] <= q["t"] <= r["t"] + 0.4]
            cur["cmax"] = max(max(q["fC"], q["rC"]) for q in post)
            if cur["t1"] - cur["t0"] > 0.15 and cur["t0"] > 0.5:   # (el primero es la caída al aparecer)
                out.append(cur)
            cur = None
        prev = r
    return out


def main():
    args = sys.argv[1:]
    T = 20.0
    if "--time" in args:
        i = args.index("--time"); T = float(args[i + 1]); del args[i:i + 2]
    exe, mod_map, spawn, lip, speeds = os.path.abspath(args[0]), args[1], float(args[2]), float(args[3]), [int(v) for v in args[4].split(",")]
    who = args[5] if len(args) > 5 else "ambos"
    src = find_map(mod_map)
    text = open(src, encoding="utf-8").read()
    tmpdir = os.path.join(ROOT, "pruebas", "mods", "prueba", "maps")
    cwd = os.path.join(ROOT, "pruebas")                     # junta pruebas/mods y los mods de al lado del exe
    temps, jobs = [], []
    try:
        for v in speeds:
            if who in ("bot", "ambos"):
                name = "_salto_%s_v%d" % (os.getpid(), v)
                body = re.sub(r'"bot_speed"\s*:\s*[\d.]+\s*,?', "", text)
                body = body.replace("{", '{\n  "bot_speed": %.4f,' % (v / 3.6), 1)
                path = os.path.join(tmpdir, name + ".json")
                open(path, "w", encoding="utf-8", newline="\n").write(body)
                temps.append(path)
                jobs.append(("bot %d" % v, "prueba/" + name, None))
            if who in ("bajada", "ambos"):
                # --map con el id de la carpeta de mods de la raíz: desde pruebas/ se ve la de al lado del exe,
                # que puede estar vieja; se usa una copia igual en pruebas.
                name = "_salto_%s_b" % os.getpid()
                path = os.path.join(tmpdir, name + ".json")
                if path not in temps:
                    open(path, "w", encoding="utf-8", newline="\n").write(text)
                    temps.append(path)
                jobs.append(("bajada %d" % v, "prueba/" + name, "bajada%d" % v))
        with ThreadPoolExecutor(6) as ex:
            res = list(ex.map(lambda j: (j[0], run(exe, cwd, j[1], spawn, T, j[2])), jobs))
    finally:
        for p in temps:
            os.remove(p)
    print(f"{mod_map}: salto en s = {lip:g} (aparece en s = {spawn:g})")
    for tag, (rows, golpes) in res:
        fl = [f for f in flights(rows) if abs(f["s0"] - lip) < 12]
        crash = next((r["t"] for r in rows if r["crash"] == 1), None)
        if not fl:
            print(f"  {tag:>10}: no vuela" + (f", se cae a los {crash:.1f} s" if crash else ""))
            continue
        f = fl[0]
        fell = crash is not None and crash < f["t1"] + 1.5
        print(f"  {tag:>10}: despega a {f['v0']:3.0f} km/h, aire {f['t1'] - f['t0'] + 0.02:4.2f} s, alto {f['hmax'] - 0.8:4.1f} m, "
              f"vy al tocar {f['vy1']:6.1f} m/s, cabeceo {f['p1']:4.0f}°, compresión {f['cmax']:.2f}, largo {f['s1'] - f['s0']:4.1f} m"
              + ("  SE CAE" + (" (golpe)" if golpes else "") if fell else ""))


if __name__ == "__main__":
    main()
