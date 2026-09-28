# Mide un WAV del sintetizador (--sound-test): nivel (RMS, pico), brillo (centroide del espectro) y la
# frecuencia de las explosiones (el pico de la envolvente) por tramos de tiempo.
# Uso: python tools/sonido.py archivo.wav [t0-t1 ...]      p. ej.  python tools/sonido.py 2t.wav 0.2-0.9 3.2-4.4
import sys, wave
import numpy as np

path = sys.argv[1]
with wave.open(path) as w:
    rate = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
ranges = [tuple(float(v) for v in a.split('-')) for a in sys.argv[2:]] or [(0, len(x) / rate)]
print(f"{path}: {len(x) / rate:.2f} s")
for t0, t1 in ranges:
    seg = x[int(t0 * rate):int(t1 * rate)]
    if len(seg) < 1024:
        continue
    rms, peak = np.sqrt(np.mean(seg ** 2)), np.max(np.abs(seg))
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    freqs = np.fft.rfftfreq(len(seg), 1 / rate)
    centroid = np.sum(freqs * spec) / np.sum(spec)
    # Explosiones: el pico del espectro de la envolvente entre 10 y 600 Hz.
    env = np.abs(seg)
    k = int(rate / 1500)
    env = np.convolve(env, np.ones(k) / k, mode='same')
    es = np.abs(np.fft.rfft((env - env.mean()) * np.hanning(len(env))))
    ef = np.fft.rfftfreq(len(env), 1 / rate)
    band = (ef > 10) & (ef < 600)
    fire = ef[band][np.argmax(es[band])]
    hi = np.sum(spec[freqs > 2000]) / np.sum(spec)
    print(f"  {t0:4.1f}-{t1:4.1f} s  rms {20 * np.log10(rms + 1e-9):6.1f} dB  pico {20 * np.log10(peak + 1e-9):6.1f} dB  "
          f"centroide {centroid:6.0f} Hz  >2 kHz {hi * 100:4.1f}%  explosiones ~{fire:5.0f} Hz")
