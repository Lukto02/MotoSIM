import sys, glob
from PIL import Image, ImageDraw
# Grilla numerada de capturas (para mirar una serie de golpe), con recorte opcional.
# Uso: python tools/grilla.py salida.png columnas escala [crop x0 y0 x1 y1] archivos-o-comodines...
out, cols, scale = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
args = sys.argv[4:]
box = None
if args and args[0] == 'crop':
    box = tuple(int(v) for v in args[1:5])
    args = args[5:]
files = []
for a in args:
    files += sorted(glob.glob(a))
ims = []
for f in files:
    im = Image.open(f).convert('RGB')
    if box: im = im.crop(box)
    ims.append(im)
w, h = int(ims[0].width * scale), int(ims[0].height * scale)
rows = (len(ims) + cols - 1) // cols
g = Image.new('RGB', (w * cols, h * rows), (0, 0, 0))
d = ImageDraw.Draw(g)
for i, im in enumerate(ims):
    x, y = (i % cols) * w, (i // cols) * h
    g.paste(im.resize((w, h)), (x, y))
    d.text((x + 4, y + 4), str(i), fill=(255, 255, 0))
g.save(out)
print(len(ims), g.size)
