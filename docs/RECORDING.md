# Recording your own harmonica

By default the firmware plays the harmonica from the FluidR3_GM SoundFont (MIT licence, see
`LICENSES/FluidR3-MIT.txt`). To replace it with your own harmonica, record one note per file, then convert them
with `app/tools/samples.py`.

## Equipment

- A microphone 20–30 cm from the holes. A phone works; a USB or condenser microphone is better.
- A quiet room that doesn't ring (curtains, a sofa, no bare tiles).
- Hands off the harmonica: no hand effects, the voice adds them (ARP, SEQ).

## The takes

- Format: WAV, 44.1 or 48 kHz, 16 or 24 bits. Mono or stereo (stereo is mixed down to mono).
- One note per file, held **2 to 3 seconds**, steady breath, no vibrato and no bend: the voice adds those.
- A clean attack, then a steady note: the tool keeps 0.25 s of attack and loops what follows.
- The same breath and the same distance for every note. Peaks around −6 dBFS, without clipping.
- Trim the silence before the note if the recorder leaves a lot (the tool finds the start, but noise at the head
  doesn't help).

## Which notes

One harmonica in C is enough: the other keys are made by transposing the nearest samples.

- **Best**: every hole, blown and drawn, 19 notes (the G of 2 draw and 3 blow only once).
- **Fewest**: one note every three or four half steps across the range, for example
  1 blow, 2 blow, 2 draw, 3 draw, 4 draw, 5 blow, 6 draw, 7 blow, 8 draw, 9 blow, 10 draw.

Space matters: the samples go in the FM-1's flash beside the app and must fit in **about 420 KB**. At 32 kHz (the
default) a note takes about 31 KB: 13 notes. With `--rate 22050` a note takes 22 KB: 19 notes fit. The tool prints
the size and warns when it is too much.

## File names

Name each file by the note it plays, with its octave (C4 is middle C, hole 1 blow on a harmonica in C):

| Hole (harmonica in C) | Blow | Draw |
| --- | --- | --- |
| 1 | `C4.wav` | `D4.wav` |
| 2 | `E4.wav` | `G4.wav` |
| 3 | (`G4.wav`, already done) | `B4.wav` |
| 4 | `C5.wav` | `D5.wav` |
| 5 | `E5.wav` | `F5.wav` |
| 6 | `G5.wav` | `A5.wav` |
| 7 | `C6.wav` | `B5.wav` |
| 8 | `E6.wav` | `D6.wav` |
| 9 | `G6.wav` | `F6.wav` |
| 10 | `C7.wav` | `A6.wav` |

Sharps are written `s` (`Fs4.wav`), flats `b` (`Eb5.wav`). The MIDI number works too (`60.wav`).

## Converting

The tool needs numpy, and the build needs Pillow. Homebrew's Python won't install packages for itself, so give
them their own environment once:

```
python3 -m venv ~/.venvs/fm1 && ~/.venvs/fm1/bin/pip install numpy Pillow
```

Then, from the `fm1-harmonica` folder:

```
~/.venvs/fm1/bin/python app/tools/samples.py wavs MY_TAKES --rate 22050 --credit "Samples: my Hohner Special 20 in C, recorded by me."
PYTHON=~/.venvs/fm1/bin/python ./build.sh --web
```

The tool writes `app/dsp/hm_samples.c`, replacing FluidR3_GM's (git keeps the old one). Serve `app/build/emu/` to
listen (`python3 -m http.server -d app/build/emu 8000`, then http://localhost:8000), then `./build.sh` for the
firmware.

To go back to FluidR3_GM's samples:

```
~/.venvs/fm1/bin/python app/tools/samples.py sf2 PATH/FluidR3_GM.sf2 0 22 --credit "..."
```
