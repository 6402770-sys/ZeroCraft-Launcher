#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# ZERO Launcher - generates the interface sounds. All sounds are original: synthesized here from oscillators and a
# fixed-seed noise source, nothing is sampled from any game or library.
#
# Usage (repo root):  python scripts/zero/gen-ui-sounds.py
# Writes launcher/resources/zero/sounds/<set>/<event>.wav (44.1 kHz, 16-bit, mono, peak about -6 dBFS). The output is
# deterministic; the WAVs are checked in and registered in launcher/resources/zero/zero.qrc, so this only needs to be
# re-run when a sound is changed. Python standard library only.
import math
import os
import struct
import wave

RATE = 44100
PEAK = 0.5  # about -6 dBFS
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "launcher", "resources", "zero", "sounds")


class Noise:
    """Fixed-seed LCG so the output never changes between runs."""

    def __init__(self, seed=12345):
        self.s = seed

    def next(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.s / 0x7FFFFFFF) - 1.0


def wave_sample(kind, phase):
    p = phase % 1.0
    if kind == "square":
        return 1.0 if p < 0.5 else -1.0
    if kind == "pulse":  # 25 % duty, the classic thin 8-bit voice
        return 1.0 if p < 0.25 else -1.0
    if kind == "triangle":
        return 4.0 * abs(p - 0.5) - 1.0
    return math.sin(2 * math.pi * p)


def note(kind, f0, f1, dur, env="lin", attack=0.002, release=0.012, level=1.0, decay=6.0, bits=0):
    """One tone: pitch glides f0 -> f1 over `dur` seconds. env: 'lin' (flat with fades) or 'exp' (exponential decay)."""
    n = int(dur * RATE)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / RATE
        f = f0 + (f1 - f0) * (i / max(1, n - 1))
        phase += f / RATE
        s = wave_sample(kind, phase)
        if bits:  # crude bit-crush for the retro set
            q = 2 ** bits
            s = round(s * q) / q
        e = math.exp(-decay * t / dur) if env == "exp" else 1.0
        a = min(1.0, t / attack) if attack > 0 else 1.0
        r = min(1.0, (dur - t) / release) if release > 0 else 1.0
        out.append(s * e * a * max(0.0, r) * level)
    return out


def tap(dur, level=0.5, seed=7, decay=40.0, lowpass=0.25):
    """A short filtered-noise tap (one-pole low-pass), exponential decay."""
    rng = Noise(seed)
    n = int(dur * RATE)
    out = []
    y = 0.0
    for i in range(n):
        t = i / RATE
        y += lowpass * (rng.next() - y)
        a = min(1.0, t / 0.0008)
        r = min(1.0, (dur - t) / 0.004)
        out.append(y * math.exp(-decay * t / dur * 4) * a * max(0.0, r) * level)
    return out


def mix(*parts):
    n = max(len(p) for p in parts)
    return [sum(p[i] for p in parts if i < len(p)) for i in range(n)]


def seq(*parts, gap=0.0):
    out = []
    for p in parts:
        out.extend(p)
        out.extend([0.0] * int(gap * RATE))
    return out


def write(path, samples):
    peak = max(abs(s) for s in samples) or 1.0
    scale = PEAK / peak
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767)) for s in samples))


# Note frequencies (Hz).
C5, E5, G5, C6, E6, G6 = 523.25, 659.25, 783.99, 1046.5, 1318.5, 1568.0
A3, A4, A5, D5, F5 = 220.0, 440.0, 880.0, 587.33, 698.46


def retro():
    sq = "square"
    return {
        "click": note("pulse", 1760, 1760, 0.025, release=0.008, bits=6),
        "toggle-on": seq(note(sq, C5, C5, 0.05, bits=6), note(sq, G5, G5, 0.07, bits=6)),
        "toggle-off": seq(note(sq, G5, G5, 0.05, bits=6), note(sq, C5, C5, 0.07, bits=6)),
        "navigate": seq(note("pulse", E5, E5, 0.04, bits=6), note("pulse", A5, A5, 0.05, bits=6), gap=0.0),
        "success": seq(note(sq, C5, C5, 0.07, bits=6), note(sq, E5, E5, 0.07, bits=6), note(sq, G5, G5, 0.07, bits=6),
                       note(sq, C6, C6, 0.16, env="exp", decay=3.0, bits=6)),
        "error": mix(note("square", 220, 110, 0.24, env="exp", decay=1.5, bits=5),
                     note("pulse", 165, 82, 0.24, env="exp", decay=1.5, bits=5, level=0.7)),
        "launch": note("pulse", 220, 1320, 0.26, release=0.03, bits=6),
    }


def soft():
    return {
        "click": mix(tap(0.03, level=0.9, seed=3), note("sine", 1200, 900, 0.03, env="exp", decay=7.0, level=0.6)),
        "toggle-on": seq(note("sine", 740, 740, 0.06, env="exp", decay=5.0), note("sine", 1109, 1109, 0.09, env="exp", decay=5.0)),
        "toggle-off": seq(note("sine", 1109, 1109, 0.06, env="exp", decay=5.0), note("sine", 740, 740, 0.09, env="exp", decay=5.0)),
        "navigate": mix(tap(0.05, level=0.5, seed=5, lowpass=0.12), note("triangle", 880, 960, 0.07, env="exp", decay=6.0, level=0.7)),
        "success": seq(note("sine", E5, E5, 0.12, env="exp", decay=4.0), note("sine", G5 * 1.26, G5 * 1.26, 0.30, env="exp", decay=4.0),
                       gap=0.0),
        "error": mix(note("sine", 150, 90, 0.2, env="exp", decay=5.0, attack=0.004, release=0.02),
                     tap(0.06, level=0.5, seed=9, lowpass=0.08)),
        "launch": seq(note("sine", C5, C5, 0.07, env="exp", decay=4.0), note("sine", G5, G5, 0.07, env="exp", decay=4.0),
                      note("sine", C6, C6, 0.15, env="exp", decay=4.0)),
    }


def main():
    for name, events in (("retro", retro()), ("soft", soft())):
        for event, samples in events.items():
            limit = 0.5 if event == "success" else 0.3
            assert len(samples) / RATE <= limit, (name, event, len(samples) / RATE)
            write(os.path.join(OUT, name, event + ".wav"), samples)
            print(name, event, "%d ms" % (len(samples) * 1000 // RATE))


if __name__ == "__main__":
    main()
