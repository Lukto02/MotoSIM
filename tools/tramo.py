# Un tramo entero de la vuelta a N km/h: todos los vuelos entre s0 y s1 (dónde despega, a qué velocidad,
# aire, largo, vy al tocar y cabeceo), y si se cae (dónde, y si fue por un golpe fuerte). Para líneas de
# saltos seguidos y serruchos, donde lo que pasa en un salto depende de cómo se llegó del anterior
# (tools/saltos.py mide uno solo).
#
# Dos pilotos, como saltos.py: "bajada" (--test bajadaN: sostiene N km/h con el gas y no toca el cuerpo) y
# "bot" (el bot a N km/h: en el aire busca caer con la trompa 8° abajo), con una copia temporal del mapa en
# pruebas/mods/prueba/maps/ con otro bot_speed (se borra al terminar). Corre desde pruebas/.
#
# Uso (desde la raíz del proyecto):
#   python tools/tramo.py <exe> <mod/mapa> <spawn> <s0> <s1> <km/h,km/h,...> [bot|bajada|ambos] [--time T]
#   p. ej. python tools/tramo.py build-msvc/motocross.exe sandbox/medanos 600 650 850 40,50,60,70 ambos --time 30
# Ojo: "bajada" no frena en las curvas; una caída pasando s1 suele ser la curva que viene, no el tramo.
import os, re, subprocess, sys
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


def run(exe, job, spawn, T):
    tag, m, test = job
    cmd = [exe, "--headless", "--map", m, "--spawn", str(spawn), "--time", str(T), "--telemetry", "--telemetry-dt", "0.02"]
    cmd += ["--test", test] if test else ["--bot"]
    out = subprocess.run(cmd, cwd=os.path.join(ROOT, "pruebas"), capture_output=True, text=True, errors="replace").stdout
    rows, golpes = [], []
    for line in out.splitlines():
        if line.startswith("t="):
            d = {k: float(x) for k, x in PAT.findall(line)}
            d["gnd"] = line.split("gnd=")[1][:2]
            rows.append(d)
        elif line.startswith("GOLPE"):
            golpes.append(line.strip())
    return tag, rows, golpes


def flights(rows, s0, s1):
    fl, cur, crash = [], None, None
    for r in rows:
        if r["crash"] == 1:
            crash = r
            break
        if r["gnd"] == "00" and cur is None and s0 <= r["s"] <= s1:
            cur = dict(t0=r["t"], s0=r["s"], v0=r["v"])
        if cur is not None:
            if r["gnd"] == "00":
                cur.update(t1=r["t"], s1=r["s"], vy=r["vy"], p=r["pitch"])
            else:
                if cur.get("t1", 0) - cur["t0"] > 0.12:
                    fl.append(cur)
                cur = None
    return fl, crash


def main():
    args = sys.argv[1:]
    T = 30.0
    if "--time" in args:
        i = args.index("--time"); T = float(args[i + 1]); del args[i:i + 2]
    exe, mod_map = os.path.abspath(args[0]), args[1]
    spawn, s0, s1 = float(args[2]), float(args[3]), float(args[4])
    speeds = [int(v) for v in args[5].split(",")]
    who = args[6] if len(args) > 6 else "ambos"
    text = open(find_map(mod_map), encoding="utf-8").read()
    tmpdir = os.path.join(ROOT, "pruebas", "mods", "prueba", "maps")
    jobs, temps = [], []
    try:
        for v in speeds:
            if who in ("bot", "ambos"):
                n = "_tramo_%d_v%d" % (os.getpid(), v)
                body = re.sub(r'"bot_speed"\s*:\s*[\d.]+\s*,?', "", text).replace("{", '{\n  "bot_speed": %.4f,' % (v / 3.6), 1)
                p = os.path.join(tmpdir, n + ".json")
                open(p, "w", encoding="utf-8", newline="\n").write(body)
                temps.append(p)
                jobs.append(("bot %d" % v, "prueba/" + n, None))
            if who in ("bajada", "ambos"):
                n = "_tramo_%d_b" % os.getpid()     # copia igual en pruebas (la de al lado del exe puede estar vieja)
                p = os.path.join(tmpdir, n + ".json")
                if p not in temps:
                    open(p, "w", encoding="utf-8", newline="\n").write(text)
                    temps.append(p)
                jobs.append(("bajada %d" % v, "prueba/" + n, "bajada%d" % v))
        with ThreadPoolExecutor(6) as ex:
            res = list(ex.map(lambda j: run(exe, j, spawn, T), jobs))
    finally:
        for p in temps:
            os.remove(p)
    print(f"{mod_map}: vuelos entre s = {s0:g} y {s1:g} (aparece en s = {spawn:g})")
    for tag, rows, golpes in res:
        fl, crash = flights(rows, s0, s1)
        txt = " | ".join(f"s{f['s0']:.0f} {f['v0']:.0f}km/h {f['t1'] - f['t0'] + 0.02:.2f}s {f['s1'] - f['s0']:.0f}m vy{f['vy']:.0f} p{f['p']:.0f}"
                         for f in fl)
        extra = ""
        if crash is not None:
            extra = f"  SE CAE en s={crash['s']:.0f} t={crash['t']:.1f}" + (" (" + golpes[0] + ")" if golpes else "")
        print(f"{tag:>10}: {len(fl)} vuelos: {txt}{extra}")


if __name__ == "__main__":
    main()
