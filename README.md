# Whysho EQ1

A modern, feature-complete equalizer plugin — part of the Whysho suite, but fully independent.
Built on [JUCE](https://juce.com) (C++), shipping as **VST3** (Windows + macOS) and **AU** (macOS).

![status](https://img.shields.io/badge/stage-alpha-orange) ![formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU-blue)

---

## Features

**Filtering**
- Up to **16 bands**, each: Bell, Low/High Shelf, High/Low Pass, Notch, Band Pass, Tilt Shelf, All Pass
- Variable **slopes** 6 → 96 dB/oct (Butterworth cascades) on pass filters
- Per-band **channel mode**: Stereo · Left · Right · **Mid** · **Side**
- Per-band **solo** (listen) and enable
- **Dynamic EQ** on any band — band-localized detector, threshold / range / attack / release, upward *or* downward

**Sidechain**
- Dedicated **sidechain input bus** — route any track or plugin output into it from your DAW
- Per-band **"SC Key"** makes that band's dynamics follow the sidechain instead of the internal signal
- **Sidechain Listen** to audition the key signal
- Works with Whysho plugins *and* any host-routed source (FL Studio, Ableton, Reaper, Cubase, Logic …)

**Processing & quality**
- Phase modes: **Zero Latency** (IIR) · **Natural** · **Linear Phase** (FFT, latency-compensated)
- **Oversampling**: off / 2× / 4× / 8× / 16×
- **Auto-gain** compensation, global In/Out trim, dry/wet **Mix**, global EQ-gain **Scale**

**Presets & workflow**
- **Factory bank** (Vocal Presence, Bass Tighten, De-Ess, Mix Glue, Air Lift, Telephone, Warmth, …) + **user presets** saved to disk
- Preset bar with prev/next, **Save**, and **A/B compare** + Copy

**Interface**
- Real-time **FFT spectrum analyzer** (Pre / Post / Pre+Post / Sidechain) with musical tilt
- **Draggable band nodes** on the curve — drag to set freq/gain, wheel for Q, right-click for type/channel/dynamics
- **Themes** — switchable palettes, persisted with the project. **Paper** (the Whysho editorial/paper brand look) is the default; **Midnight** and **Graphite** are dark alternatives
- Resizable UI

**Theming / brand** — The visual system lives in `src/gui/Theme.h`: one `Theme` struct (colors + per-band node palette + style flags) and a small catalogue. "Paper" is the shared Whysho brand look intended across the whole suite (and the website). To add a theme, append one entry to the catalogue — every component reads `theme().<token>` live, so no other code changes. The selector is in the top bar; the choice is saved in the plugin state (`uiTheme`).

**Licensing** — see [`docs/LICENSING.md`](docs/LICENSING.md)
- Online **activation** → machine-bound, **Ed25519-signed** license, verified **offline** on every load
- 14-day local trial, offline grace, periodic re-check
- Layered **anti-tamper** (obfuscated strings, redundant gate, anti-debug)

---

## Repository layout

```
CMakeLists.txt        Root build (fetches JUCE + Monocypher)
src/                  Plugin: DSP, GUI, licensing
server/               License activation server (Node/TypeScript)  ← independent
packaging/            Inno Setup (.exe) + macOS .pkg installer configs
.github/workflows/    CI: build + package + release on Win & macOS
tools/                Offscreen GUI snapshot renderer (dev only)
docs/                 Licensing + architecture notes
```

## Building

**Requirements:** CMake ≥ 3.22, a C++17 compiler. First configure downloads JUCE 8 + Monocypher.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artefacts land in `build/WhyshoEQ1_artefacts/Release/` (`VST3/`, `AU/`, `Standalone/`).

**Installers** (CI builds these automatically; locally):
```bash
# Windows (needs Inno Setup):  powershell scripts/build-windows-installer.ps1
# macOS   (needs Xcode tools):  scripts/build-macos-installer.sh
```
Install locations and code-signing/notarization setup are documented in [`packaging/README.md`](packaging/README.md).

## Using the sidechain (quick guide)

1. Add **Whysho EQ1** on the track you want to process.
2. Enable a band and turn on **Dynamic** + **SC Key** for it.
3. In your DAW, route the *source* track/plugin to EQ1's **Sidechain** input:
   - **FL Studio:** right-click the source channel → *Sidechain to this track*, then pick the sidechain input in EQ1's wrapper.
   - **Ableton/Reaper/Cubase/Logic:** select EQ1's Sidechain bus as the send/aux input.
4. That band now ducks/boosts in response to the sidechain signal.

## Security / "non-crackable" — honest note

No locally-run plugin can be made truly uncrackable; anything that executes on a user's
machine can, in principle, be bypassed. Whysho EQ1 instead uses a **layered deterrent**:
signed machine-bound licenses verified offline, obfuscated secrets, a redundant audio-path
gate, and anti-debug probes. The goal is to make cracking expensive and casual sharing
impractical — not to promise the impossible. See [`docs/LICENSING.md`](docs/LICENSING.md).

## Roadmap

- Spectrum **grab**/collision detection, per-band gain-match
- Linear-phase IR regeneration off the audio thread (background)
- Preset browser + factory presets, A/B compare, undo/redo
- CLAP format, AAX (Pro Tools)
- Inter-plugin shared sidechain bus across the Whysho suite
