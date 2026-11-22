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
- **Freeze:** while the **HOLD input** is high, recording stops and the last delay-time of sound loops.
  GLIDE and the RESET clock repitch the loop (1/4x to 4x). The other octave LEDs flash green.
- ASSIGN carries only the delayed signal: patch it into EXT AUDIO and use MIX for dry + wet.

If all eight octave LEDs ever light red together for a second, the firmware ran out of processing time
for a moment. Please report it with what you were doing.

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
- The delay's ADPCM storage follows the approach of my
  [MLRws card](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/15_MLRws) for the
  Music Thing Modular Workshop System Computer; the codec is standard IMA ADPCM.

## License

Copyright (C) 2026 Dune Desormeaux (@Dessertplanet). GPL-3.0-or-later; see [LICENSE](LICENSE).

Moog and Mother-32 are trademarks of Moog Music Inc. They are used here only to say which instrument
this firmware runs on.
