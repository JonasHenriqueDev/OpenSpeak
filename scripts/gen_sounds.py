"""Sintetiza os efeitos sonoros do OpenSpeak (originais, inspirados no estilo Discord/Skype).

Tudo é gerado do zero (osciladores, FM, envelopes); nenhum som de terceiros é usado.
Uso: python scripts/gen_sounds.py
Gera public/sounds/<tema>/*.wav e native/client/assets/sounds/<tema>/*.wav.
"""
import sys, wave
from pathlib import Path
import numpy as np

SR = 48000
ROOT = Path(__file__).resolve().parent.parent
DESTS = [ROOT / "public" / "sounds", ROOT / "native" / "client" / "assets" / "sounds"]
THEMES = {"A": "suave", "B": "bolha", "C": "cristal", "D": "retro", "E": "kalimba", "F": "espacial",
          "G": "discreto"}


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


# ======================= temas extras (mesmos 8 eventos) =======================

def fm_bell(f, dur, decay=5.0, ratio=3.5, index=2.2):
    """Sino de vidro (FM): modulador inarmônico cujo brilho some antes do tom."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    mod = index * np.exp(-t * decay * 1.8) * np.sin(2 * np.pi * f * ratio * t)
    s = np.sin(2 * np.pi * f * t + mod) + 0.25 * np.sin(2 * np.pi * f * 2.01 * t) * np.exp(-t * decay * 2)
    return s * env(n, 0.002, decay)


def chip(f, dur, decay=10.0):
    """Onda quadrada de console 8-bit (só harmônicos ímpares, para não ficar áspera)."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    s = sum(np.sin(2 * np.pi * f * k * t) / k for k in range(1, 10, 2))
    # envelope em degraus, como o volume de 4 bits dos consoles
    return 0.8 * s * (np.round(env(n, 0.001, decay) * 15) / 15)


def chip_slide(f0, f1, dur, decay=10.0):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = f0 * (f1 / f0) ** np.clip(t / dur, 0, 1)
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = sum(np.sin(k * ph) / k for k in range(1, 10, 2))
    return 0.8 * s * (np.round(env(n, 0.001, decay) * 15) / 15)


def kalimba(f, dur, decay=7.0):
    """Lâmina dedilhada: fundamental + parcial inarmônico (~5.4x) curto + estalo do dedo."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    fund = np.sin(2 * np.pi * f * t) * np.exp(-t * decay)
    tine = 0.45 * np.sin(2 * np.pi * f * 5.43 * t) * np.exp(-t * decay * 6)
    click = 0.3 * np.sin(2 * np.pi * 2400 * t) * np.exp(-t * 400)
    return (fund + tine + click) * np.clip(t / 0.0015, 0, 1)


def space(f0, f1, dur, decay=4.0, attack=0.03):
    """Synth etéreo: dois osciladores desafinados com vibrato e glissando lento."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    k = np.clip(t / (dur * 0.6), 0, 1)
    f = (f0 + (f1 - f0) * (1 - (1 - k) ** 2)) * (1 + 0.006 * np.sin(2 * np.pi * 5.5 * t))
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = np.sin(ph) + 0.7 * np.sin(ph * 1.004) + 0.2 * np.sin(2 * ph)
    return s * env(n, attack, decay)


def tick(f, dur=0.09, decay=45.0):
    """Toque curtinho e abafado (tema discreto)."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    return (np.sin(2 * np.pi * f * t) + 0.2 * np.sin(2 * np.pi * f * 2 * t)) * env(n, 0.001, decay)


# ---------- C: "Cristal" (sinos de vidro) ----------
finish(place([(0.00, fm_bell(E5, 0.8), 0.6), (0.10, fm_bell(B5, 1.0, decay=4), 0.7)], 1.2), "C_entrar_sala")
finish(place([(0.00, fm_bell(B5, 0.8), 0.7), (0.11, fm_bell(E5, 1.0, decay=4), 0.6)], 1.2), "C_sair_sala")
finish(place([(0.00, fm_bell(A5, 0.6, decay=7), 0.7)], 0.7), "C_alguem_entrou")
finish(place([(0.00, fm_bell(E5, 0.6, decay=7), 0.7)], 0.7), "C_alguem_saiu")
finish(place([(0.00, fm_bell(C5, 0.6, decay=7), 0.5), (0.08, fm_bell(E5, 0.6, decay=7), 0.5),
              (0.16, fm_bell(G5, 0.6, decay=7), 0.55), (0.24, fm_bell(C6, 1.2, decay=3.5), 0.65)], 1.5),
       "C_iniciar_transmissao")
finish(place([(0.00, fm_bell(G5, 0.6, decay=7), 0.6), (0.12, fm_bell(D5, 0.9, decay=5), 0.6)], 1.1),
       "C_pausar_transmissao")
finish(place([(0.00, fm_bell(G6, 0.4, decay=12, index=1.2), 0.5)], 0.45), "C_espectador_entrou", peak_db=-7)
finish(place([(0.00, fm_bell(E6, 0.4, decay=12, index=1.2), 0.5)], 0.45), "C_espectador_saiu", peak_db=-7)

# ---------- D: "Retrô" (chiptune 8-bit) ----------
finish(place([(0.00, chip(C5, 0.09), 0.5), (0.07, chip(E5, 0.09), 0.5), (0.14, chip(G5, 0.25, decay=7), 0.55)], 0.45),
       "D_entrar_sala", peak_db=-8)
finish(place([(0.00, chip(G5, 0.09), 0.55), (0.07, chip(E5, 0.09), 0.5), (0.14, chip(C5, 0.25, decay=7), 0.5)], 0.45),
       "D_sair_sala", peak_db=-8)
finish(place([(0.00, chip_slide(E5, A5, 0.14, decay=14), 0.55)], 0.2), "D_alguem_entrou", peak_db=-8)
finish(place([(0.00, chip_slide(A5, E5, 0.14, decay=14), 0.55)], 0.2), "D_alguem_saiu", peak_db=-8)
finish(place([(0.00, chip(C5, 0.07), 0.45), (0.06, chip(E5, 0.07), 0.45), (0.12, chip(G5, 0.07), 0.45),
              (0.18, chip(C6, 0.07), 0.45), (0.24, chip(E6, 0.07), 0.45), (0.30, chip(G6, 0.35, decay=6), 0.5)], 0.7),
       "D_iniciar_transmissao", peak_db=-8)
finish(place([(0.00, chip(E5, 0.1), 0.5), (0.1, chip(C5, 0.25, decay=8), 0.5)], 0.4), "D_pausar_transmissao", peak_db=-8)
finish(place([(0.00, chip(C6, 0.05, decay=30), 0.45), (0.05, chip(G6, 0.08, decay=25), 0.45)], 0.16),
       "D_espectador_entrou", peak_db=-12)
finish(place([(0.00, chip(G6, 0.05, decay=30), 0.45), (0.05, chip(C6, 0.08, decay=25), 0.45)], 0.16),
       "D_espectador_saiu", peak_db=-12)

# ---------- E: "Kalimba" (lâminas dedilhadas) ----------
finish(place([(0.00, kalimba(D5, 0.7), 0.65), (0.09, kalimba(A5, 0.9, decay=5), 0.7)], 1.0), "E_entrar_sala")
finish(place([(0.00, kalimba(A5, 0.7), 0.7), (0.10, kalimba(D5, 0.9, decay=5), 0.65)], 1.0), "E_sair_sala")
finish(place([(0.00, kalimba(B5, 0.5, decay=9), 0.7)], 0.55), "E_alguem_entrou")
finish(place([(0.00, kalimba(E5, 0.5, decay=9), 0.7)], 0.55), "E_alguem_saiu")
finish(place([(0.00, kalimba(D5, 0.5, decay=9), 0.55), (0.07, kalimba(E5, 0.5, decay=9), 0.55),
              (0.14, kalimba(A5, 0.5, decay=9), 0.6), (0.21, kalimba(D5 * 2, 1.0, decay=4), 0.65)], 1.3),
       "E_iniciar_transmissao")
finish(place([(0.00, kalimba(A5, 0.5, decay=9), 0.6), (0.11, kalimba(E5, 0.8, decay=6), 0.6)], 0.9),
       "E_pausar_transmissao")
finish(place([(0.00, kalimba(E6, 0.35, decay=13), 0.6)], 0.4), "E_espectador_entrou", peak_db=-7)
finish(place([(0.00, kalimba(B5, 0.35, decay=13), 0.6)], 0.4), "E_espectador_saiu", peak_db=-7)

# ---------- F: "Espacial" (synth etéreo com glissandos) ----------
finish(place([(0.00, space(C5, G5, 0.9), 0.7)], 1.0), "F_entrar_sala")
finish(place([(0.00, space(G5, C5, 0.9), 0.7)], 1.0), "F_sair_sala")
finish(place([(0.00, space(E5, A5, 0.5, decay=7, attack=0.015), 0.7)], 0.55), "F_alguem_entrou")
finish(place([(0.00, space(A5, E5, 0.5, decay=7, attack=0.015), 0.7)], 0.55), "F_alguem_saiu")
finish(place([(0.00, space(C5, C5, 0.5, decay=6, attack=0.02), 0.45), (0.12, space(G5, G5, 0.5, decay=6, attack=0.02), 0.45),
              (0.24, space(C6 * 0.9, C6, 1.2, decay=3), 0.6)], 1.5), "F_iniciar_transmissao")
finish(place([(0.00, space(G5, D5, 0.8, decay=4.5), 0.7)], 0.9), "F_pausar_transmissao")
finish(place([(0.00, space(G6 * 0.9, G6, 0.3, decay=12, attack=0.01), 0.5)], 0.35), "F_espectador_entrou", peak_db=-8)
finish(place([(0.00, space(G6, G6 * 0.9, 0.3, decay=12, attack=0.01), 0.5)], 0.35), "F_espectador_saiu", peak_db=-8)

# ---------- G: "Discreto" (toques curtos e baixos) ----------
finish(place([(0.00, tick(G5), 0.6), (0.08, tick(C6), 0.6)], 0.2), "G_entrar_sala", peak_db=-8)
finish(place([(0.00, tick(C6), 0.6), (0.08, tick(G5), 0.6)], 0.2), "G_sair_sala", peak_db=-8)
finish(place([(0.00, tick(A5), 0.6)], 0.12), "G_alguem_entrou", peak_db=-9)
finish(place([(0.00, tick(E5), 0.6)], 0.12), "G_alguem_saiu", peak_db=-9)
finish(place([(0.00, tick(C6), 0.6), (0.07, tick(E6), 0.6), (0.14, tick(G6, dur=0.15, decay=25), 0.6)], 0.3),
       "G_iniciar_transmissao", peak_db=-8)
finish(place([(0.00, tick(E6), 0.6), (0.08, tick(C6, dur=0.15, decay=25), 0.6)], 0.25), "G_pausar_transmissao", peak_db=-8)
finish(place([(0.00, tick(E6, dur=0.06, decay=60), 0.6)], 0.08), "G_espectador_entrou", peak_db=-11)
finish(place([(0.00, tick(C6, dur=0.06, decay=60), 0.6)], 0.08), "G_espectador_saiu", peak_db=-11)
