"""Arma la carpeta dist/MotoSim-vX/ y su zip, sin preferencias ni archivos de pruebas.

Uso: python tools/package_release.py <motocross.exe>

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
if len(sys.argv) != 2:
    sys.exit('uso: python tools/package_release.py <motocross.exe>')
exe = Path(sys.argv[1]).resolve()
if not exe.is_file():
    sys.exit(f'no existe el ejecutable: {exe}')
version = re.search(r'MOTOSIM_VERSION "([^"]+)"', (root/'src/Version.h').read_text(encoding='utf-8')).group(1)
name = f'MotoSim-{version}'
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

files = [(exe, 'MotoSim.exe')]
for item in ['tuning.ini', 'MODDING.md', 'Low_Poly_Motorcyclist_3_rigged.gltf']:
    files.append((root/item, item))
for sub in ['mods', 'Low_Poly_Motorcyclist_3_rigged_deps']:
    for path in sorted((root/sub).rglob('*')):
        if wanted(path.relative_to(root)):
            files.append((path, path.relative_to(root).as_posix()))
for source, relative in files:
    if not source.is_file():
        sys.exit(f'falta {source}')

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
with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for source, relative in files:
        archive.write(source, f'{name}/{relative}')
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
    assert data[:3] == b'\xef\xbb\xbf'
    assert data.count(b'\r\n') == data.count(b'\n') > 20, 'el LEEME no es CRLF puro'
assert sorted(p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()) == sorted(n[len(name) + 1:] for n in names)
print(f'{output.name}: {output.stat().st_size} bytes, {len(names)} archivos verificados; carpeta {folder}')
