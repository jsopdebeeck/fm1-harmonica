# HARMONICA: a 10-hole harmonica for the M-VAVE FM-1

A firmware that turns the M-VAVE FM-1 into a 10-hole diatonic harmonica. The keyboard is the harmonica: ten keys are
its ten holes, one key is the breath turning from blow to draw, three keys bend the note. Its USB name is
"HARMONICA FM-1", its identity FM-1_47 and the version (FM-1_47000500 is 0.5).

## How it plays

- **White keys 1 to 10 are holes 1 to 10.** A key blows its hole.
- **White key 11 is DRAW.** Held, the holes are drawn instead of blown; the notes sounding turn with it.
- **White keys 12 to 14 bend**, held: a half step, a whole step, a whole step and a half, as far as the hole allows
  (draw bends on the low holes, blow bends on the high ones, as on the instrument).
- **Neighbouring keys together are a chord.** One mouth, one breath: 4 neighbouring holes at most; a hole out of the
  mouth's reach leaves it.
- **The lights show it.** The ten holes glow at rest; the holes sounding are bright; DRAW and the bend in use are
  bright while they are, dark at rest.

## The controls

One control per setting:

| Control | What it does |
| --- | --- |
| PRESET | the harmonica's key |
| ALGORITHM | the tuning: Major Diatonic, Melody Maker, Natural Minor, Harmonic Minor |
| SELECT | the lick |
| PLAY | loops the lick, its keys lit as it plays; PLAY again, or a hole, stops it |
| EDIT | the next sound: BLUES, FOLK, CHICAGO, FM |
| KNOB 1 to 4 | TONE, BREATH, VIBRATO, DRIVE |
| ARP, held | the hands closed (wah) |
| SEQ, held | the hand's tremolo |
| HOME | the harmonica; the battery, top right, for 3 s |
| GLO | SETUP: KNOB 1 the note names, KNOB 2 the keys' lights |
| SAVE | keeps the settings |
| MASTER | the volume |

Turning KNOB 1 to 4, PRESET, ALGORITHM or SELECT says its name and its value large for a second and a half, then the
harmonica's screen is back; a hole brings it back at once.

## The licks

29 licks, each written for its tuning, in `app/licks.tab`: scales, bends, blues and boogie choruses, the train, and
tunes in the public domain (Oh! Susanna, When the Saints, Amazing Grace, Ode to Joy...). `app/tools/gen_licks.py`
turns the file into the firmware's table and refuses what a real harmonica cannot do: a chord that is not 1 to 4
neighbouring holes on one breath, a bend the hole does not have.

## Build

```sh
git clone --recursive https://github.com/jsopdebeeck/fm1-harmonica
cd fm1-harmonica
./build.sh --host            # the simulator, and the scripted tests in app/tests/
./build.sh --web             # the browser emulator (Emscripten)
./build.sh --release 0.5     # the firmware, a .fwsc (JieLi toolchain)
```

The build is fm1-base's (`base/`, a git submodule); its README says what each target needs. `PYTHON` names a Python
with Pillow and libraqm.

The firmware keeps only its own settings (SAVE), in its own place in the FM-1's flash.

## The sound

The harmonica's samples come from the FluidR3_GM SoundFont by Frank Wen (MIT, `LICENSES/FluidR3-MIT.txt`).
`docs/RECORDING.md` says how to record your own harmonica and convert it with `app/tools/samples.py`.

## Licence

The code is under the Mozilla Public License 2.0 ([LICENSE](LICENSE)), as fm1-base is. The samples keep their own
licence (above).

It is built on fm1-base by Charles Vestal and on felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments).
M-VAVE and FM-1 are trademarks of their owners; this firmware is not affiliated with M-VAVE. Major Diatonic, Melody
Maker, Natural Minor and Harmonic Minor name Lee Oskar Harmonicas' tunings; this firmware is not affiliated with Lee
Oskar Harmonicas.
