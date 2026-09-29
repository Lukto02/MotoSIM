"""Arma la carpeta dist/MotoSim-vX/ y su zip, sin preferencias ni archivos de pruebas.

Uso: python tools/package_release.py [--mac] <motocross.exe | motocross>

- Con --mac arma dist/MotoSim-vX-mac/ y su zip con el binario de Mac (se llama MotoSim, con permiso de
  ejecución dentro del zip) y un LEEME con los pasos para abrirlo en Mac arriba; sin --mac, el de Windows.
- La versión sale de src/Version.h.
- El LEEME.txt del paquete sale de LEEME.txt (en la raíz del repo, con las novedades arriba): se escribe
  en UTF-8 con BOM y CRLF. Si no dice la versión de Version.h (no se actualizó), el script se detiene.
- Nada depende de esta PC: sólo el exe que se le pasa y archivos del repo (sirve igual en el workflow
  de GitHub, con el exe en build-release/Release/, y en local con el de MinGW).
- Rehace la carpeta y el zip cada vez (borra los de la misma versión).
"""
from pathlib import Path
import hashlib
import re
import shutil
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
args = [a for a in sys.argv[1:] if a != '--mac']
mac = len(args) != len(sys.argv) - 1
if len(args) != 1:
    sys.exit('uso: python tools/package_release.py [--mac] <motocross.exe | motocross>')
exe = Path(args[0]).resolve()
if not exe.is_file():
    sys.exit(f'no existe el ejecutable: {exe}')
version = re.search(r'MOTOSIM_VERSION "([^"]+)"', (root/'src/Version.h').read_text(encoding='utf-8')).group(1)
name = f'MotoSim-{version}-mac' if mac else f'MotoSim-{version}'
folder = root/'dist'/name
output = root/'dist'/f'{name}.zip'

# El LEEME tiene que ser el de esta versión.
leeme = (root/'LEEME.txt').read_text(encoding='utf-8-sig').replace('\r\n', '\n')
first = leeme.split('\n', 1)[0]
if f'MotoSim {version} ' not in first + ' ' or f'NOVEDADES DE LA {version}\n' not in leeme:
    sys.exit(f'LEEME.txt no es el de la {version}: la primera línea tiene que empezar con "MotoSim {version}" '
             f'y tiene que tener el bloque "NOVEDADES DE LA {version}"')
skip_names = {'preferencias.ini', 'preferencias.ini.v1.bak'}
def wanted(path):
    parts = set(path.parts)
    return path.is_file() and path.name not in skip_names and '__pycache__' not in parts and 'pruebas' not in parts

exe_name = 'MotoSim' if mac else 'MotoSim.exe'
files = [(exe, exe_name)]
for item in ['tuning.ini', 'MODDING.md', 'Low_Poly_Motorcyclist_3_rigged.gltf']:
    files.append((root/item, item))
for sub in ['mods', 'Low_Poly_Motorcyclist_3_rigged_deps']:
    for path in sorted((root/sub).rglob('*')):
        if wanted(path.relative_to(root)):
            files.append((path, path.relative_to(root).as_posix()))
for source, relative in files:
    if not source.is_file():
        sys.exit(f'falta {source}')

mac_intro = f'''MotoSim {version} para Mac (chip Apple: M1 o más nuevo)
=====================================================

CÓMO ABRIRLO EN MAC
1. Descomprimí el zip y dejá la carpeta entera (no lo abras desde adentro del zip).
2. Abrí la Terminal y ejecutá (cambiá la ruta por donde la dejaste):
     cd ~/Downloads/{name}
     xattr -dr com.apple.quarantine .
     ./MotoSim
   El xattr saca la marca de "descargado de internet": el juego no está firmado por Apple y sin eso macOS
   no lo deja abrir. (Si preferís, intentá abrirlo una vez y después Ajustes del Sistema > Privacidad y
   seguridad > "Abrir de todas formas".)
3. Si el firewall de macOS está activado, la primera vez que creás una partida en red pregunta si deja que
   MotoSim reciba conexiones: hay que permitirlo.
4. En un teclado de Mac, las teclas F (F3, F8, F11...) se usan con la tecla fn.

Es la versión de Mac del mismo juego: lo de abajo (controles, red, novedades) vale igual, salvo que donde
dice MotoSim.exe es ./MotoSim. Las pruebas automáticas de GitHub lo compilan y lo corren sin ventana; si algo
anda raro en tu Mac, avisá.

---------------------------------------------------------------------------------------------------------

'''
if mac:
    readme = (mac_intro + leeme).encode('utf-8')                   # en Mac: UTF-8 y LF, sin BOM
else:
    readme = b'\xef\xbb\xbf' + leeme.replace('\n', '\r\n').encode('utf-8')

# Carpeta (se prueba desde ahí) y zip (sale de la misma lista).
(root/'dist').mkdir(exist_ok=True)
if folder.exists():
    shutil.rmtree(folder)
for source, relative in files:
    target = folder/relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
(folder/'LEEME.txt').write_bytes(readme)

if output.exists():
    output.unlink()
def add_file(archive, source, arcname):
    if not mac:
        archive.write(source, arcname)
        return
    # En Mac el binario tiene que salir del zip ejecutable: el permiso va en los atributos del archivo.
    info = zipfile.ZipInfo.from_file(source, arcname)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = (0o755 if arcname == f'{name}/{exe_name}' else 0o644) << 16
    archive.writestr(info, Path(source).read_bytes(), compresslevel=9)

with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for source, relative in files:
        add_file(archive, source, f'{name}/{relative}')
    if mac:
        info = zipfile.ZipInfo(f'{name}/LEEME.txt', date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        archive.writestr(info, readme)
    else:
        archive.writestr(f'{name}/LEEME.txt', readme)

# Verificación: el zip sano, sin preferencias ni pruebas, igual a la carpeta, y el LEEME con BOM y CRLF.
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    names = archive.namelist()
    assert not any(n.endswith('preferencias.ini') or '/pruebas/' in n for n in names), 'sobra un archivo'
    assert all(n.startswith(name + '/') for n in names)
    for n in names:
        rel = n[len(name) + 1:]
        assert hashlib.sha256(archive.read(n)).digest() == hashlib.sha256((folder/rel).read_bytes()).digest(), rel
    data = archive.read(f'{name}/LEEME.txt')
    if mac:
        assert archive.getinfo(f'{name}/{exe_name}').external_attr >> 16 & 0o111 == 0o111, 'el binario no es ejecutable'
        assert data[:3] != b'\xef\xbb\xbf' and b'\r' not in data and data.count(b'\n') > 20, 'el LEEME de Mac es LF sin BOM'
    else:
        assert data[:3] == b'\xef\xbb\xbf'
        assert data.count(b'\r\n') == data.count(b'\n') > 20, 'el LEEME no es CRLF puro'
assert sorted(p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()) == sorted(n[len(name) + 1:] for n in names)
print(f'{output.name}: {output.stat().st_size} bytes, {len(names)} archivos verificados; carpeta {folder}')
