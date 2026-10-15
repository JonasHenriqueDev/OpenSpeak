"""Sintetiza os efeitos sonoros do OpenSpeak (originais, inspirados no estilo Discord/Skype).

Uso: python scripts/gen_sounds.py
Gera public/sounds/{suave,bolha}/*.wav e native/client/assets/sounds/{suave,bolha}/*.wav.
"""
import sys, wave
from pathlib import Path
import numpy as np

SR = 48000
ROOT = Path(__file__).resolve().parent.parent
DESTS = [ROOT / "public" / "sounds", ROOT / "native" / "client" / "assets" / "sounds"]
THEMES = {"A": "suave", "B": "bolha"}


def env(n, attack=0.004, decay=6.0):
    t = np.arange(n) / SR
    a = np.clip(t / attack, 0, 1)
    return a * np.exp(-t * decay)


def glide_tone(f0, f1, dur, decay=6.0, harm=0.15, glide=0.06):
    """Seno com glissando curto (estilo 'bloop' do Discord)."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    k = np.clip(t / glide, 0, 1)
    k = 1 - (1 - k) ** 3  # ease-out
    f = f0 + (f1 - f0) * k
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = np.sin(ph) + harm * np.sin(2 * ph) + 0.05 * np.sin(3 * ph)
    return s * env(n, 0.006, decay)


def mallet(f, dur, decay=9.0, bright=0.5):
    """Tom de marimba/bolha (estilo Skype): fundamental + parcial 4x que morre rápido."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    fund = np.sin(2 * np.pi * f * t) * np.exp(-t * decay)
    over = bright * np.sin(2 * np.pi * f * 3.98 * t) * np.exp(-t * decay * 5)
    # leve "pop" de pitch no ataque, dá a sensação de bolha
    pop = 0.35 * np.sin(2 * np.pi * f * (1 + 0.6 * np.exp(-t * 90)) * t) * np.exp(-t * 60)
    a = np.clip(t / 0.002, 0, 1)
    return (fund + over + pop) * a


def place(events, total):
    out = np.zeros(int(total * SR))
    for start, sig, gain in events:
        i = int(start * SR)
        j = min(len(out), i + len(sig))
        out[i:j] += gain * sig[: j - i]
    return out


def reverb(x, mix=0.18):
    """Reverb simples (combs em paralelo) para dar ar sem embolar."""
    y = np.zeros(len(x) + int(0.6 * SR))
    y[: len(x)] = x
    wet = np.zeros_like(y)
    for d_ms, g in ((29.7, 0.55), (37.1, 0.5), (41.1, 0.47), (43.7, 0.43)):
        d = int(d_ms / 1000 * SR)
        buf = y.copy()
        for i in range(d, len(buf), d):
            seg = buf[i - d : i]
            buf[i : i + d][: len(seg)] += g * seg[: len(buf[i : i + d])]
        wet += buf - y
    return y + mix * wet / 4


def finish(x, name, peak_db=-4.0):
    x = reverb(x)
    # corta cauda silenciosa
    thr = np.max(np.abs(x)) * 10 ** (-60 / 20)
    last = np.nonzero(np.abs(x) > thr)[0][-1] + int(0.02 * SR)
    x = x[:last]
    fade = min(len(x), int(0.03 * SR))
    x[-fade:] *= np.linspace(1, 0, fade)
    x = x / np.max(np.abs(x)) * 10 ** (peak_db / 20)
    pcm = (x * 32767).astype(np.int16)
    theme, event = name.split("_", 1)
    for dest in DESTS:
        folder = dest / THEMES[theme]
        folder.mkdir(parents=True, exist_ok=True)
        with wave.open(str(folder / f"{event}.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes(pcm.tobytes())
    print(f"{THEMES[theme]}/{event}.wav  {len(x)/SR:.2f}s")


# notas
P = 2 ** (-5 / 12)  # transpõe uma quarta abaixo (mais grave)
C5, D5, E5, G5, A5, B5 = (x * P for x in (523.25, 587.33, 659.25, 783.99, 880.0, 987.77))
C6, E6, G6 = (x * P for x in (1046.5, 1318.5, 1568.0))
G4, A4, E4 = (x * P for x in (392.0, 440.0, 329.63))

# ---------- Variante A: "Suave" (glides arredondados, estilo Discord) ----------
finish(place([(0.00, glide_tone(C5 * 0.92, C5, 0.35), 0.8),
              (0.11, glide_tone(G5 * 0.92, G5, 0.5), 0.9)], 0.7), "A_entrar_sala")
finish(place([(0.00, glide_tone(G5 * 1.06, G5, 0.35), 0.9),
              (0.12, glide_tone(C5 * 1.06, C5, 0.5, decay=7), 0.8)], 0.7), "A_sair_sala")
finish(place([(0.00, glide_tone(E5, A5, 0.35, decay=9, glide=0.08), 0.8)], 0.4), "A_alguem_entrou")
finish(place([(0.00, glide_tone(A5, E5, 0.35, decay=9, glide=0.08), 0.8)], 0.4), "A_alguem_saiu")
finish(place([(0.00, glide_tone(C5, C5, 0.3, decay=10), 0.6),
              (0.07, glide_tone(E5, E5, 0.3, decay=10), 0.65),
              (0.14, glide_tone(G5, G5, 0.3, decay=10), 0.7),
              (0.21, glide_tone(C6 * 0.95, C6, 0.6, decay=5), 0.8)], 0.9), "A_iniciar_transmissao")
finish(place([(0.00, glide_tone(G5, G5, 0.3, decay=11), 0.7),
              (0.10, glide_tone(D5 * 1.03, D5, 0.45, decay=8), 0.65)], 0.6), "A_pausar_transmissao")

# ---------- Variante B: "Bolha" (marimba/bolhas, estilo Skype) ----------
finish(place([(0.00, mallet(G4, 0.5), 0.7),
              (0.09, mallet(C5, 0.5), 0.75),
              (0.18, mallet(E5, 0.7, decay=6), 0.8)], 0.9), "B_entrar_sala")
finish(place([(0.00, mallet(E5, 0.5), 0.8),
              (0.10, mallet(C5, 0.5), 0.75),
              (0.20, mallet(G4, 0.7, decay=6), 0.7)], 0.9), "B_sair_sala")
finish(place([(0.00, mallet(A5, 0.35, decay=12), 0.7),
              (0.07, mallet(E6, 0.45, decay=10), 0.6)], 0.5), "B_alguem_entrou")
finish(place([(0.00, mallet(E6, 0.35, decay=12), 0.6),
              (0.07, mallet(A5, 0.45, decay=10), 0.7)], 0.5), "B_alguem_saiu")
finish(place([(0.00, mallet(C5, 0.4, decay=10), 0.6),
              (0.06, mallet(G5, 0.4, decay=10), 0.6),
              (0.12, mallet(C6, 0.4, decay=10), 0.6),
              (0.18, mallet(E6, 0.4, decay=10), 0.55),
              (0.26, mallet(G6, 0.8, decay=5, bright=0.3), 0.6)], 1.1), "B_iniciar_transmissao")
finish(place([(0.00, mallet(C6, 0.4, decay=11), 0.6),
              (0.09, mallet(A5, 0.4, decay=11), 0.6),
              (0.18, mallet(E5, 0.6, decay=8, bright=0.25), 0.65)], 0.8), "B_pausar_transmissao")

# ---------- Espectadores da sua transmissão (mais curtos e discretos) ----------
# "bip-bip" curto, para não confundir com alguém entrando no canal.
finish(place([(0.00, glide_tone(G5, G5, 0.18, decay=18), 0.7),
              (0.075, glide_tone(C6 * 0.97, C6, 0.3, decay=12), 0.75)], 0.45), "A_espectador_entrou", peak_db=-7)
finish(place([(0.00, glide_tone(C6, C6, 0.18, decay=18), 0.7),
              (0.075, glide_tone(G5 * 1.03, G5, 0.3, decay=12), 0.7)], 0.45), "A_espectador_saiu", peak_db=-7)
finish(place([(0.00, mallet(E6, 0.25, decay=16, bright=0.3), 0.6),
              (0.06, mallet(G6, 0.35, decay=13, bright=0.3), 0.55)], 0.45), "B_espectador_entrou", peak_db=-7)
finish(place([(0.00, mallet(G6, 0.25, decay=16, bright=0.3), 0.55),
              (0.06, mallet(E6, 0.35, decay=13, bright=0.3), 0.6)], 0.45), "B_espectador_saiu", peak_db=-7)
