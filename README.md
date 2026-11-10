# S5 Sampler

An emulation of the **Roland S-550** sampler as a plug-in (VST3, Audio Unit) and standalone app for macOS.
The screen is a pixel-exact copy of the S-550 monitor, so you operate it the way you would operate the real machine
(manual in hand, pages and menus in the same places).

*Not a Roland product. It contains no Roland code and no Roland samples.*

## Please read first

This is an **early, experimental version** and it was **written with the help of Claude, an AI by Anthropic**.
It will contain bugs and incomplete features. It may crash your DAW, play wrong sounds, or damage or overwrite data
(for example hard disk or floppy images, and project files), so **always work on copies and keep backups**.

**Use it at your own risk.** The software is provided "as is", without warranty of any kind, and the authors are not liable
for any damage or loss. It has not been tested on a wide range of Macs, DAWs or real S-550 hardware.
Bug reports and feedback are welcome.

## What it does

- Plays S-550 sound data: 16 voices, 8 parts (multitimbral), 64 patches, 32 tones, Wave Banks A/B and C/D
- Edits everything on the screen: Patch, Tone, Split, TVF, TVA, LFO, Loop, Wave Draw, Key Map, Function and MIDI pages
- Samples through the plug-in input and shows the signal in the Wave Scope
- Imports WAV, AIFF and MP3 as tones (too long files are cut automatically)
- Loads and saves S-550 data from floppy images (IMG) and hard disk images (SCSI), and talks to a real S-550 over MIDI (SysEx)
- 8 individual stereo outputs (Output Assign), master VOLUME and REC LEVEL in the strip under the screen

## Install

**Easiest:** open `S5Sampler_<date>.dmg`, right-click `S5 Sampler installieren.pkg` > **Open**, choose VST3 and/or Audio Unit.
macOS asks once for confirmation because the file is not notarized. Then restart your DAW (rescan plug-ins).

Plug-ins are installed to `/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/Components`.

## Use in a DAW

1. Create an instrument track and load **S5 Sampler**. It starts empty.
2. Send MIDI to it. Each part listens to its own MIDI channel (Play page).
3. Load sounds: **Disk** menu > hard disk image or floppy image, or import a wave file (**Load Wave**).

**Mouse:** click or hover a value and click to change it; right click closes a menu; the wheel changes values;
Shift = bigger steps. **Keyboard:** type into name fields.

**Sampling input:** switch on the plug-in's *Sidechain* in your DAW and route the track you want to sample to it
(Cubase: send to the sidechain of S5 Sampler). Then use UTIL-Sampling or UTIL-Wave Scope (press the red bar to start).

**Individual outputs:** enable *Out 1 ... Out 8* in your DAW's output routing. Each patch or tone picks its output with *Output Assign*.
An output the DAW does not use plays in the main mix.

**Strip under the screen:** REC LEVEL (input level for sampling), VOLUME (master), CPU. Double-click a slider = 100.
The **Options** button opens version, disk image settings, MIDI link, and display size (1x / 2x).

## Build it yourself

Needs Xcode command line tools and CMake (`xcode-select --install`, `brew install cmake`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target S5Sampler_VST3 -j 8
```

JUCE 8 is downloaded automatically on the first run. Targets: `S5Sampler_VST3`, `S5Sampler_AU`, `S5Sampler_Standalone`.
To make the installer (VST3 + AU, Apple Silicon + Intel): `bash Tools/make_dmg.sh`

## Good to know

- The plug-in keeps its data in the DAW project. The standalone app always starts empty.
- Samples play at 30 kHz or 15 kHz like the original; the plug-in resamples to the project rate.
- Not yet finished: Func / MIDI save and load on disk, the MIDI program change address on a real S-550, some Wave Draw details.

## License

Free and non-commercial. The source code is released under the **GNU Affero General Public License v3 (AGPL-3.0)**,
see `LICENSE`. This is required because the plug-in is built with [JUCE](https://juce.com), which is available under AGPLv3.
Roland and S-550 are trademarks of Roland Corporation; this project is not affiliated with or endorsed by Roland.
Sound disks and manuals are not included and must come from your own copies.

## Folders

`Source/Screen` screen and pages, `Source/Engine` sound engine, `Source/Data` S-550 data formats and disk images,
`Source/Midi` SysEx, `Tests` automatic tests, `Tools` helper scripts.
