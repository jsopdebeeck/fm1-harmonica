#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The harmonica's samples: from a SoundFont 2 preset, or from your own WAV recordings, to a C file the voice plays
(dsp/hm_samples.c: 16-bit mono at RATE Hz, each with its root note, its key range and its loop).

  samples.py list FILE.sf2 [NAME]                      the presets (whose name has NAME)
  samples.py show FILE.sf2 BANK PROGRAM                one preset's zones: keys, sample, root, loop
  samples.py sf2 FILE.sf2 BANK PROGRAM [--rate HZ] [--credit TEXT] [--out dsp/hm_samples.c]
  samples.py wavs DIR [--rate HZ] [--credit TEXT] [--out dsp/hm_samples.c]
                                                       DIR/*.wav named by the note they play: 60.wav or C4.wav,
                                                       Eb5.wav, Fs4.wav (s for sharp)...; a sustained note each,
                                                       2 s or more: the loop is found in its sustain

The samples go in the firmware's flash, beside the app: keep them under about 420 KB (the build says how much). A
recorded note keeps 0.25 s of attack and a loop of about 0.25 s; --rate 22050 halves the size (the default: 44100
for a SoundFont, 32000 for WAVs). Needs numpy. Each sample is resampled to RATE, cut a little after its loop's end (the loop is all that plays then),
normalised to the loudest of the set (so their levels stay as recorded), and its loop's ends crossfaded."""
import argparse
import re
import struct
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 44100                              # the samples' rate, the FM-1's own
TAIL = 64                                 # samples kept past a loop's end (the interpolation reads one past it)

# ------------------------------------------------------------------ SoundFont 2 ---

GEN = {0: "startAddrsOffset", 1: "endAddrsOffset", 2: "startloopAddrsOffset", 3: "endloopAddrsOffset",
       4: "startAddrsCoarseOffset", 12: "endAddrsCoarseOffset", 45: "startloopAddrsCoarseOffset",
       50: "endloopAddrsCoarseOffset", 41: "instrument", 43: "keyRange", 44: "velRange", 48: "initialAttenuation",
       51: "coarseTune", 52: "fineTune", 53: "sampleID", 54: "sampleModes", 58: "overridingRootKey",
       34: "attackVolEnv", 38: "releaseVolEnv", 37: "sustainVolEnv", 36: "holdVolEnv", 35: "decayVolEnv",
       8: "initialFilterFc"}


def chunks(data, off, end):
    while off + 8 <= end:
        cid, size = data[off:off + 4].decode("latin-1"), struct.unpack_from("<I", data, off + 4)[0]
        yield cid, off + 8, size
        off += 8 + size + (size & 1)


class SF2:
    def __init__(self, path):
        d = Path(path).read_bytes()
        if d[0:4] != b"RIFF" or d[8:12] != b"sfbk":
            raise SystemExit(f"{path}: not a SoundFont 2 file")
        self.data = d
        pd = {}
        for cid, o, n in chunks(d, 12, len(d)):
            if cid != "LIST":
                continue
            kind = d[o:o + 4].decode()
            for sid, so, sn in chunks(d, o + 4, o + n):
                if kind == "sdta" and sid == "smpl":
                    self.smpl = np.frombuffer(d, dtype="<i2", count=sn // 2, offset=so)
                elif kind == "pdta":
                    pd[sid] = (so, sn)
        rec = lambda name, size: [d[pd[name][0] + i:pd[name][0] + i + size] for i in range(0, pd[name][1], size)]
        cstr = lambda b: b.split(b"\0")[0].decode("latin-1").strip()
        self.phdr = [(cstr(r[:20]),) + struct.unpack("<HHH", r[20:26]) for r in rec("phdr", 38)]
        self.pbag = [struct.unpack("<HH", r) for r in rec("pbag", 4)]
        self.pgen = [struct.unpack("<HH", r) for r in rec("pgen", 4)]
        self.inst = [(cstr(r[:20]), struct.unpack("<H", r[20:22])[0]) for r in rec("inst", 22)]
        self.ibag = [struct.unpack("<HH", r) for r in rec("ibag", 4)]
        self.igen = [struct.unpack("<HH", r) for r in rec("igen", 4)]
        self.shdr = [(cstr(r[:20]),) + struct.unpack("<IIIIIBbHH", r[20:46]) for r in rec("shdr", 46)]

    @staticmethod
    def gens(bags, gens, i):
        out = {}
        for g in range(bags[i][0], bags[i + 1][0]):
            op, amt = gens[g]
            if op in (43, 44):
                out[op] = (amt & 0xFF, amt >> 8)
            else:
                out[op] = amt - 65536 if amt >= 32768 and op not in (41, 53, 58, 54) else amt
        return out

    def presets(self):
        return [(n, b, p) for n, p, b, _ in self.phdr[:-1]]

    def zones(self, bank, prog):
        """the preset's instrument zones, merged with its own zones (SF2 2.04 8.5: instrument values, preset
        values added to them) and their global zones"""
        idx = [i for i, (n, p, b, _) in enumerate(self.phdr[:-1]) if b == bank and p == prog]
        if not idx:
            raise SystemExit(f"no preset {bank}:{prog}")
        i = idx[0]
        pz = [self.gens(self.pbag, self.pgen, z) for z in range(self.phdr[i][3], self.phdr[i + 1][3])]
        pglob = pz.pop(0) if pz and 41 not in pz[0] else {}
        out = []
        for p in pz:
            p = {**pglob, **p}
            ins = p[41]
            iz = [self.gens(self.ibag, self.igen, z) for z in range(self.inst[ins][1], self.inst[ins + 1][1])]
            iglob = iz.pop(0) if iz and 53 not in iz[0] else {}
            for z in iz:
                z = {**iglob, **z}
                for op, v in p.items():   # the preset's ranges narrow, its other values add
                    if op in (43, 44):
                        lo, hi = z.get(op, (0, 127))
                        z[op] = (max(lo, v[0]), min(hi, v[1]))
                    elif op not in (41, 53, 54, 58):
                        z[op] = z.get(op, 0) + v
                if z.get(43, (0, 127))[0] <= z.get(43, (0, 127))[1]:
                    out.append(z)
        return out

    def sample(self, z):
        name, start, end, ls, le, rate, pitch, corr, _, stype = self.shdr[z[53]]
        g = lambda f, c: z.get(f, 0) + 32768 * z.get(c, 0)
        start += g(0, 4)
        end += g(1, 12)
        ls += g(2, 45)
        le += g(3, 50)
        root = z.get(58, pitch)
        tune = z.get(51, 0) * 100 + z.get(52, 0) - corr          # cents
        return dict(name=name, pcm=self.smpl[start:end].astype(np.float64) / 32768.0, rate=rate, root=root,
                    tune=tune, loop=(ls - start, le - start) if z.get(54, 0) in (1, 3) else None,
                    keys=z.get(43, (0, 127)), vel=z.get(44, (0, 127)), att=z.get(48, 0) / 10.0)


# --------------------------------------------------------------------- the WAVs ---

NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note_of(stem):
    if re.fullmatch(r"\d+", stem):
        return int(stem)
    m = re.fullmatch(r"([A-Ga-g])([sb#]?)(-?\d)", stem)
    if not m:
        raise SystemExit(f"{stem}.wav: name it by its note (60.wav, C4.wav, Eb5.wav, Fs4.wav)")
    n = NOTE[m[1].upper()] + {"s": 1, "#": 1, "b": -1, "": 0}[m[2]]
    return 12 * (int(m[3]) + 1) + n


def read_wav(path):
    with wave.open(str(path)) as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    elif width == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = ((b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 | b[:, 2].astype(np.int32) << 16) << 8 >> 8) / 8388608.0
    elif width == 4:
        x = np.frombuffer(raw, "<i4").astype(np.float64) / 2147483648.0
    else:
        raise SystemExit(f"{path}: 16, 24 or 32-bit PCM, please")
    return x.reshape(-1, ch).mean(axis=1), rate


def find_loop(x, rate, root):
    """a loop in the sustain: whole periods of the note, the pair of ends that match best"""
    period = rate / (440.0 * 2 ** ((root - 69) / 12))
    env = np.abs(x)
    on = int(np.argmax(env > 0.1 * env.max()))
    a = on + int(0.25 * rate)                 # past the attack
    want = int(0.25 * rate)                   # about 0.25 s of loop
    if a + want + 2048 > len(x):
        raise SystemExit(f"note {root}: too short: hold each note 2 s or more")
    n = max(1, round(want / period))
    best = None
    w = int(period * 2)
    for k in range(-24, 25):                  # ends on whole periods, the start nudged for the best match
        b = a + int(round(n * period)) + k
        e = np.sum((x[a:a + w] - x[b:b + w]) ** 2)
        if best is None or e < best[0]:
            best = (e, b)
    return on, a, best[1]


# ---------------------------------------------------------------------- output ---

def resample(x, r0, r1):
    """windowed-sinc resampling (Kaiser, 32 taps a side), the cut-off below the lower Nyquist"""
    if r0 == r1:
        return x.copy()
    ratio = r1 / r0
    n = int(len(x) * ratio)
    t = np.arange(n) / ratio
    fc = min(1.0, ratio) * 0.94
    taps = 32
    i0 = np.floor(t).astype(np.int64)
    out = np.zeros(n)
    win = np.kaiser(2 * taps + 1, 8.6)
    for k in range(-taps, taps + 1):
        j = i0 + k
        ok = (j >= 0) & (j < len(x))
        d = t - j
        h = fc * np.sinc(fc * d) * np.interp(d, np.arange(-taps, taps + 1), win)
        out[ok] += x[j[ok]] * h[ok]
    return out


def crossfade(x, ls, le, n=256):
    """the loop's end blended into what precedes its start: no click at the jump"""
    n = min(n, ls, le - ls)
    if n <= 0:
        return x
    f = np.linspace(0.0, 1.0, n)
    x = x.copy()
    x[le - n:le] = x[le - n:le] * (1 - f) + x[ls - n:ls] * f
    return x


def write_c(zones, out, credit):
    peak = max(np.abs(z["pcm"]).max() for z in zones) or 1.0
    top = max(z["gain"] for z in zones)
    zones = [{**z, "gain": z["gain"] / top} for z in zones]   # the loudest at 1: the voice sets the level
    lines = ["/* SPDX-License-Identifier: MPL-2.0 (this file's code); the samples: see below */",
             "/* generated by tools/samples.py: the harmonica's samples (dsp/harmo.h hm_sample_t) */",
             *[f"/* {l} */" for l in credit.splitlines() if l.strip()],
             '#include "harmo.h"', ""]
    total = 0
    names = []
    for i, z in enumerate(zones):
        s = np.clip(np.round(z["pcm"] / peak * 32000.0), -32768, 32767).astype(np.int16)
        total += s.nbytes
        names.append(f"S{i}")
        lines.append(f"static const int16_t S{i}[{len(s)}] = {{   /* {z['name']}: {len(s) / RATE:.2f} s */")
        for k in range(0, len(s), 16):
            lines.append("    " + ", ".join(str(int(v)) for v in s[k:k + 16]) + ",")
        lines.append("};")
    lines.append("")
    lines.append(f"const hm_sample_t HM_SAMPLES[{len(zones)}] = {{")
    for i, z in enumerate(zones):
        ls, le = z["loop"]
        lines.append(f"    {{S{i}, {len(z['pcm'])}u, {ls}u, {le}u, {z['root']}, {z['tune']}, {z['keys'][0]}, "
                     f"{z['keys'][1]}, {z['gain']:.4f}f}},   /* {z['name']} */")
    lines.append("};")
    lines.append(f"const uint32_t HM_NSAMPLES = {len(zones)}u;")
    lines.append(f"const float HM_SAMPLE_RATE = {RATE}.0f;")
    Path(out).write_text("\n".join(lines) + "\n")
    print(f"{out}: {len(zones)} samples, {total // 1024} KB at {RATE} Hz")
    if total > 420 * 1024:
        print(f"samples: {total // 1024} KB will not fit beside the app (about 420 KB): fewer notes, or --rate 22050")


def prepare(z):
    """resampled to RATE, cut after the loop, crossfaded; the level the SoundFont gives it kept as a gain"""
    r = RATE / z["rate"]
    x = resample(z["pcm"], z["rate"], RATE)
    ls, le = int(round(z["loop"][0] * r)), int(round(z["loop"][1] * r))
    x = crossfade(x[:le + TAIL], ls, le)
    x[le:le + TAIL] = x[ls:ls + TAIL][:len(x) - le]           # past the end: the loop's start again
    return {**z, "pcm": x, "loop": (ls, le), "gain": 10 ** (-z.get("att", 0) / 20)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("list"); a.add_argument("sf2"); a.add_argument("name", nargs="?", default="")
    a = sub.add_parser("show"); a.add_argument("sf2"); a.add_argument("bank", type=int); a.add_argument("prog", type=int)
    for c in ("sf2", "wavs"):
        a = sub.add_parser(c)
        a.add_argument("src")
        if c == "sf2":
            a.add_argument("bank", type=int); a.add_argument("prog", type=int)
        a.add_argument("--rate", type=int, default=44100 if c == "sf2" else 32000)
        a.add_argument("--credit", default="")
        a.add_argument("--out", default=str(Path(__file__).resolve().parents[1] / "dsp" / "hm_samples.c"))
    a = ap.parse_args()
    global RATE
    RATE = getattr(a, "rate", RATE)
    if a.cmd == "list":
        for n, b, p in sorted(SF2(a.sf2).presets(), key=lambda t: (t[1], t[2])):
            if a.name.lower() in n.lower():
                print(f"{b:3d}:{p:3d}  {n}")
    elif a.cmd == "show":
        f = SF2(a.sf2)
        for z in f.zones(a.bank, a.prog):
            s = f.sample(z)
            print(f"keys {s['keys'][0]:3d}-{s['keys'][1]:3d} vel {s['vel'][0]:3d}-{s['vel'][1]:3d}  {s['name']:20s} "
                  f"root {s['root']:3d} tune {s['tune']:+4d}c rate {s['rate']} len {len(s['pcm']) / s['rate']:.2f}s "
                  f"loop {s['loop']} att {s['att']:.1f} dB")
    elif a.cmd == "sf2":
        f = SF2(a.src)
        zs = [f.sample(z) for z in f.zones(a.bank, a.prog)]
        top = max(z["vel"][1] for z in zs)
        zs = [z for z in zs if z["vel"][1] == top and z["loop"]]     # one layer: the loudest, looped
        mono = {}
        for z in zs:                      # a stereo pair, (L) and (R) on the same keys: one mono sample
            m = mono.setdefault(z["keys"], {**z, "name": re.sub(r"\((L|R)\)$", "", z["name"]), "n": 0})
            if m["n"]:
                k = min(len(m["pcm"]), len(z["pcm"]))
                m["pcm"] = m["pcm"][:k] + z["pcm"][:k]
            m["n"] += 1
        zs = sorted(({**m, "pcm": m["pcm"] / m["n"]} for m in mono.values()), key=lambda z: z["keys"][0])
        write_c([prepare(z) for z in zs], a.out, a.credit)
    else:
        files = sorted(Path(a.src).glob("*.wav"), key=lambda p: note_of(p.stem))
        if not files:
            raise SystemExit(f"{a.src}: no .wav")
        zs = []
        for p in files:
            x, rate = read_wav(p)
            root = note_of(p.stem)
            on, ls, le = find_loop(x, rate, root)
            zs.append(dict(name=p.name, pcm=x[on:], rate=rate, root=root, tune=0, loop=(ls - on, le - on), att=0.0))
        # each note's keys: from halfway to the one below, to halfway to the one above
        roots = [z["root"] for z in zs]
        for i, z in enumerate(zs):
            lo = 0 if i == 0 else (roots[i - 1] + roots[i]) // 2 + 1
            hi = 127 if i == len(zs) - 1 else (roots[i] + roots[i + 1]) // 2
            z["keys"] = (lo, hi)
        write_c([prepare(z) for z in zs], a.out, a.credit)


if __name__ == "__main__":
    main()
