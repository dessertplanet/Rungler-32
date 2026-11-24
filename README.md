# Rungler-32

Alternative firmware for the Moog Mother-32 that turns its sequencer into a Rungler-style chaotic shift
register and adds a tape-style digital delay on the TEMPO CV input.

> [!CAUTION]
> **Unofficial.** Rungler-32 is not made, endorsed or supported by Moog Music. Installing it replaces the
> factory firmware (you can put that back at any time, see below). Use it at your own risk: it comes with
> no warranty (see sections 15 and 16 of the [license](LICENSE)), and installing third-party firmware may
> affect your Moog warranty.

## What it does

### Shift register (KB out, GATE out)

An 8-bit shift register in the spirit of Rob Hordijk's Rungler, clocked by the GATE input and/or an
internal clock.

- **Clock:** the rising edge of the GATE input, plus the internal clock while **RUN/STOP** is on
  (0.25 Hz to 1 kHz on **TEMPO**). While the internal clock is on and MIDI clock arrives, the MIDI clock
  (16th notes) replaces it.
- **Each step** rotates the active bits and XORs the first bit with the **RUN**, **RESET** and **HOLD**
  input jacks and a comparator: is the **TEMPO CV** input above the **GLIDE** knob? With nothing patched
  into TEMPO CV, internal noise is compared instead. The two ends of GLIDE lock the pattern.
- **White keys** set the length (low C = 1 bit, high C = 8 bits); their LEDs show the bits.
- **KB out:** the active bits as a number, scaled to 0 to +2 V and quantised to C major by default.
  Tap **SHIFT** on its own to toggle quantisation (TEMPO LED green = on, red = off). The **arrows**
  transpose by octaves, shown as one red octave LED.
- **GATE out and envelope:** high while the last active bit is set, while a panel key is held, or while
  a MIDI note is held. The envelope retriggers on each step that leaves the last bit set (SUSTAIN off = a
  trigger, on = held). The **HOLD input** also fires the envelope.
- **HOLD button:** loop lock. The pattern keeps cycling unchanged; the other octave LEDs light yellow.
- While the sequencer runs, key presses are queued to the next clock.

### MIDI

Notes on any channel play KB, the gate and the envelope normally (last-note priority, pitch bend ±2
semitones, velocity above 95 fires the accent). After the last note-off, KB holds that note until the
next shift-register step.

### Delay (TEMPO CV in, ASSIGN out)

A 24 kHz tape-style delay, up to about 0.52 s, stored as 4-bit ADPCM.

- **GLIDE:** delay time. Changes glide like tape, so turning it bends the pitch.
- **Clock sync:** a clock on the **RESET** input sets the delay to its period times a fraction picked by
  GLIDE (1/4 to 2). Modulating that clock's rate with a slow LFO gives chorus or vibrato.
- **SHIFT + arrows:** feedback, up to 100%, shown on the octave LEDs (green, then yellow, then red).
- **Delay buffer freeze:** while the **HOLD input** is high, recording stops and the last delay-time of
  sound loops. GLIDE and the RESET clock repitch the loop (1/4x to 4x). The other octave LEDs light green
  while HOLD is high, over a yellow background instead of a dark one when loop lock is on.
- ASSIGN carries only the delayed signal: patch it into EXT AUDIO and use MIX for dry + wet.

If all eight octave LEDs ever light red together for a second, the firmware ran out of processing time
for a moment. Please report it with what you were doing.

## Panel reference

Everything the firmware reads or drives. The rest of the Mother-32 (VCO, VCF, VCA, LFO, mixer, ATTACK,
DECAY, VOLUME and their patch points) is analog and works as usual.

### Input jacks

| Jack | Rungler-32 |
|---|---|
| **GATE** | Rising edges clock the shift register (along with the internal clock). Edges in the last 2 s count as "sequencer running", so key presses wait for the next clock. Does not drive GATE out or the envelope. |
| **TEMPO** (CV) | Audio input of the delay (about -6 to +6 V). Also the comparator input. Within about ±120 mV for 2 s, or never patched: the comparator uses internal noise instead. |
| **RUN** | One of the XOR inputs on each shift-register clock. It does not start or stop the clock. |
| **RESET** | One of the XOR inputs. A clock here (2.5 ms to 4 s period) also syncs the delay time; sync ends when no edge arrives for twice the period plus 0.25 s. |
| **HOLD** | One of the XOR inputs. While high: delay buffer freeze, and the envelope opens (each rising edge retriggers it). Does not drive GATE out. |

The XOR inputs: on each clock, if an odd number of RUN, RESET, HOLD and the comparator are high, the
first bit flips. A jack held high therefore reverses the decision of the others. Loop lock ignores all
of them.

### Outputs

| Output | Rungler-32 |
|---|---|
| **KB** | The active bits as a number, 0 to +2 V (two octaves), quantised to C major by default, plus the octave transpose. A held MIDI note replaces it (1 V/oct, MIDI note 60 = 0 V) until the next clock with no note held. Uses the unit's factory calibration. |
| **GATE** | High while the last active bit is set (from the first clock on), while any panel key is held, or while a MIDI note is held. A key tapped while the sequencer runs gives a 10 ms gate on the next clock. |
| **Envelope** (EG) | Opens with GATE out or the HOLD jack. Retriggered by a clock that leaves the last bit set, a new key press, a HOLD rising edge, or a new MIDI note (SUSTAIN off). SUSTAIN off = trigger (attack/decay), on = held. |
| **ASSIGN** | The delayed signal only (wet). Patch into EXT AUDIO and use MIX for dry + wet. |
| Accent | MIDI notes with velocity above 95 fire the Mother-32's accent. |

### Knobs and switches

| Control | Rungler-32 |
|---|---|
| **TEMPO** | Internal clock rate, 0.25 Hz to 1 kHz (exponential). |
| **GLIDE** | Three jobs at once. **Delay time**: 0.33 ms to about 0.52 s, changing smoothly like tape (bends the pitch); with a clock on RESET, the fraction of its period instead (1/4, 1/3, 3/8, 1/2, 2/3, 3/4, 1, 3/2, 2, halved until it fits). **Comparator threshold**: the comparator is high when the TEMPO CV input (or noise) is above it. **Pattern lock** at the ends: fully anticlockwise the comparator is always high, fully clockwise never. GLIDE no longer glides the pitch. |
| **SUSTAIN** | Envelope held (on) or triggered (off). With MIDI: legato (on) or retrigger per note (off). |

### Buttons and keys

| Control | Rungler-32 |
|---|---|
| **White keys** | Shift-register length: low C = 1 bit ... high C = 8 bits. Also hold GATE out and fire the envelope. |
| **Black keys** | GATE out and envelope only. |
| **Left / right arrows** | Transpose KB by octaves, -4 to +3. |
| **SHIFT** (tapped alone) | Quantisation on/off (TEMPO LED green = on, red = off). |
| **SHIFT + arrows** | Delay feedback, 25 levels: 0 = a single repeat, top = 100% (the loop holds forever and new input piles up into saturation). |
| **RUN/STOP** | Internal clock on/off. While on, incoming MIDI clock replaces it. |
| **HOLD** | Loop lock on/off: each clock only rotates the bits, so the pattern repeats unchanged. |
| **RESET** | Not used while running. Held at power-up (with TEMPO fully anticlockwise and SUSTAIN off): update mode, see below. |
| **PATTERN** | Not used. |

While the sequencer runs (RUN/STOP on, or GATE edges in the last 2 s), key presses take effect on the
next clock; releases act at once. Otherwise presses act at once.

### LEDs

| LED | Rungler-32 |
|---|---|
| **Key LEDs** | The active bits. |
| **Octave LEDs** | One red LED: the octave (the fifth LED = no transpose). The other seven: dark, yellow with loop lock on, green while the HOLD jack is high (delay buffer freeze). While SHIFT is held, and for 1.5 s after a change: the feedback level as a bar (green 1-8, yellow 9-16, red 17-24). All eight red for 1 s: the firmware ran out of processing time. |
| **TEMPO LED** | Quantisation: green = on, red = off. While the sequencer runs, it flashes with the clock. |

### MIDI IN

| Message | Rungler-32 |
|---|---|
| Note on/off (any channel) | KB, GATE out and the envelope; last-note priority, up to 8 notes held. |
| Velocity above 95 | Accent. |
| Pitch bend | ±2 semitones on a MIDI note. |
| Clock | Replaces the internal clock while RUN/STOP is on: one step per 16th note. Start, Stop and Continue are ignored. |
| CC 120 / 123 | All notes off. |
| SysEx `F0 04 14 11 F7` | ERASE: enters update mode (see below). |

### At power-up

Nothing is saved between power cycles. Rungler-32 starts with the internal clock off, length 8 with
all bits set, quantisation on, no transpose, loop lock off and feedback at a single repeat.

## Installing

You need a MIDI interface with a 5-pin DIN output and a SysEx sender, such as SysEx Librarian (macOS) or
Bome SendSX (Windows). This is the same procedure as an official Mother-32 firmware update:

1. Download `rungler32.syx` from the releases page.
2. Connect the computer's MIDI out to the Mother-32's MIDI IN.
3. Send the ERASE file from Moog's official firmware package (`Mother-32_ERASE_firmware.syx`). The TEMPO
   LED flashes green/red: the synth is in update mode.
4. Send the ERASE file a second time. The TEMPO LED blinks red twice, then flashes green.
5. Send `rungler32.syx`. When it finishes, Rungler-32 starts by itself.

## Going back to the factory firmware

Rungler-32 answers the ERASE message the same way the factory firmware does, so follow Moog's official
update instructions with Moog's firmware file: ERASE twice, then the firmware.

**If the synth doesn't respond to ERASE:** power it up while holding **RESET**, with the **TEMPO** knob
fully anticlockwise and the **SUSTAIN** switch off. It starts in update mode; continue from step 4.

## Building

Requirements:

- TI C2000 Code Generation Tools 22.6.1 LTS (free from TI), installed in `~/ti/ti-cgt-c2000_22.6.1.LTS`,
  or set `CGT` to its folder.
- Python 3.

```sh
tools/build.sh
```

This writes `build/rungler32.syx`.

Options go in `EXTRA_CFLAGS`, for example `EXTRA_CFLAGS="-DDELAY_NO_FEEDBACK" tools/build.sh`:

- `-DDELAY_NO_FEEDBACK` removes the internal feedback.
- `-DM32_NO_EEPROM_CAL` ignores the unit's factory calibration of the KB and ASSIGN outputs.

### Layout

| Path | Contents |
|---|---|
| `firmware/Mother32.h` | A small single-header framework for the Mother-32: panel, CV outputs, gates, MIDI in, oversampled TEMPO CV input, update-mode handling |
| `firmware/rungler32/main.cpp` | Rungler-32 itself |
| `firmware/link/flash.cmd` | Linker command file |
| `tools/` | Build script, SysEx packager, and the pre-commit check |

### Contributing

Run `tools/install_hooks.sh` once after cloning. It installs a pre-commit check (`tools/check_release.sh`)
that keeps build output and stray files out of the repository.

## Credits

- Moog Music, for the Mother-32: an inspiring instrument with a classic sound that I love to bits.
  (Rungler-32 is still an unofficial project; see the note at the top.)
- The Rungler is Rob Hordijk's design (Benjolin, Blippoo Box). This is a homage, not a recreation of his
  circuit.
- Tom Whitwell's [Turing Machine](https://musicthing.co.uk/Turing-Machine/) (Music Thing Modular), a
  looping random shift register, also helped inspire the sequencer.
- Chris Johnson's [ComputerCard.h](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard)
  framework for the Workshop System Computer, which much of the hardware API in `firmware/Mother32.h` is
  based on.
- The delay's ADPCM storage follows the approach of my
  [MLRws card](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/15_MLRws) for the
  Music Thing Modular Workshop System Computer; the codec is standard IMA ADPCM.

## License

Copyright (C) 2026 Dune Desormeaux (@Dessertplanet). GPL-3.0-or-later; see [LICENSE](LICENSE).

Moog and Mother-32 are trademarks of Moog Music Inc. They are used here only to say which instrument
this firmware runs on.
