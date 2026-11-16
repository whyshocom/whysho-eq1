# whysho EQ1

A modern, feature-complete equalizer plugin — part of the whysho suite, but fully independent.
Built on [JUCE](https://juce.com) (C++), shipping as **VST3** (Windows + macOS) and **AU** (macOS).

![status](https://img.shields.io/badge/stage-alpha-orange) ![formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU-blue)

---

## Features

**Filtering**
- Up to **16 bands**, each: Bell, Low/High Shelf, High/Low Pass, Notch, Band Pass, Tilt Shelf, All Pass
- Variable **slopes** 6 → 96 dB/oct (Butterworth cascades) on pass filters
- Per-band **channel mode**: Stereo · Left · Right · **Mid** · **Side**
- Per-band **solo** (listen) and enable
- **Dynamic EQ** on any band and any filter type: gain bands move by the Range; pass, notch, band-pass and all-pass filters fade in or out. Threshold / range / attack / release, upward *or* downward, shown live on the graph

**Sidechain**
- Dedicated **sidechain input bus** — route any track or plugin output into it from your DAW
- Per-band **"SC Key"** makes that band's dynamics follow the sidechain instead of the internal signal
- **Sidechain Listen** to audition the key signal
- Works with whysho plugins *and* any host-routed source (FL Studio, Ableton, Reaper, Cubase, Logic …)

**Processing & quality**
- Phase modes:
  - **Zero Latency**: plain IIR at the host rate (the curve squeezes toward Nyquist, as with any digital EQ)
  - **Natural Phase**: the analog curve and phase across the audible band, via internal oversampling with linear-phase filters (about 60 samples of latency at 48 kHz)
  - **Linear Phase**: no phase shift at all; FIR designed in the background, per-channel, 4096 samples of latency at 48 kHz. Bands with Dynamic on run as normal filters after it, and very narrow bands below ~100 Hz come out shallower than drawn
- **Oversampling**: off / 2× / 4× / 8× / 16×
- **Auto-gain** compensation, global In/Out trim, dry/wet **Mix**, global EQ-gain **Scale**

**Presets & workflow**
- **Factory bank** (Vocal Presence, Bass Tighten, De-Ess, Mix Glue, Air Lift, Telephone, Warmth, …) + **user presets** saved to disk
- Preset bar with prev/next, **Save**, and **A/B compare** + Copy

**Interface**
- Real-time **spectrum analyzer** (Pre / Post / Pre+Post / Sidechain): 8192-point FFT with overlap, 1/6-octave smoothing, musical tilt
- **Zoomable** graph (±6 / ±12 / ±30 dB), band power switch, **undo / redo**, preset rename / delete, remembered window size
- **Draggable band nodes** on the curve — drag to set freq/gain, wheel for Q, right-click for type/channel/dynamics
- **Themes** — switchable palettes, persisted with the project. **Paper** (the whysho editorial/paper brand look) is the default; **Midnight** and **Graphite** are dark alternatives
- Resizable UI

**Theming / brand** — The visual system lives in `src/gui/Theme.h`: one `Theme` struct (colors + per-band node palette + style flags) and a small catalogue. "Paper" is the shared whysho brand look intended across the whole suite (and the website). To add a theme, append one entry to the catalogue — every component reads `theme().<token>` live, so no other code changes. The selector sits next to the presets; the choice is one preference per computer (not part of a project or preset) and every open EQ1 window follows it.

**Licensing** — see [`docs/LICENSING.md`](docs/LICENSING.md)
- Account-based, like the rest of the whysho suite: sign in with the browser to a whysho.com account that owns EQ1
- 14-day trial; after it, EQ1 switches off on that computer (audio passes through unchanged) until it is signed in
- The site's answer is signed (Ed25519) and verified in the plugin, so it can't be faked by redirecting whysho.com
- Works offline for 30 days between checks; one sign-in unlocks every open instance

---

## Repository layout

```
CMakeLists.txt        Root build (fetches JUCE + Monocypher); the version lives here
src/                  Plugin: DSP, GUI, licensing
tests/                Unit tests: licensing rules, DSP stress
packaging/            Inno Setup (.exe) + macOS .pkg installer configs
.github/workflows/    CI: build + package + release on Win & macOS
tools/                Offscreen GUI snapshot renderer (dev only)
docs/                 Licensing notes
```

## Building

**Requirements:** CMake ≥ 3.22, a C++17 compiler. First configure downloads JUCE 8.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artefacts land in `build/WhyshoEQ1_artefacts/Release/` (`VST3/`, `AU/`, `Standalone/`).

**Tests:**
```bash
ctest --test-dir build -C Release --output-on-failure
```

**Installers** (CI builds these automatically; locally):
```bash
# Windows (needs Inno Setup):  powershell scripts/build-windows-installer.ps1
# macOS   (needs Xcode tools):  scripts/build-macos-installer.sh
```
Install locations and code-signing/notarization setup are documented in [`packaging/README.md`](packaging/README.md).

## Using the sidechain (quick guide)

1. Add **whysho EQ1** on the track you want to process.
2. Enable a band and turn on **Dynamic** + **SC Key** for it.
3. In your DAW, route the *source* track/plugin to EQ1's **Sidechain** input:
   - **FL Studio:** right-click the source channel → *Sidechain to this track*, then pick the sidechain input in EQ1's wrapper.
   - **Ableton/Reaper/Cubase/Logic:** select EQ1's Sidechain bus as the send/aux input.
4. That band now ducks/boosts in response to the sidechain signal.

## Copy protection: honest note

No plugin that runs on the user's own machine can be made uncrackable, and this code is
public. The trial and the account check are there to keep honest use simple and casual
sharing inconvenient, not to promise the impossible. What they do and don't stop is
spelled out in [`docs/LICENSING.md`](docs/LICENSING.md).

## Roadmap

- Spectrum **grab**/collision detection, per-band gain-match
- Longer linear-phase kernels (optional, more latency) for narrow low bands
- Server-side trial and a seat limit (need website changes)
- CLAP format, AAX (Pro Tools)
- Inter-plugin shared sidechain bus across the whysho suite
