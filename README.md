# EP-MK2

A physically modelled electric piano in C++ / JUCE.

EP-MK2 starts from the finished EP-MK1 JUCE port and goes after the things that
model **structurally cannot do** — not more knobs on the same skeleton, but a
tine with the modes the measurements actually found, a Q that varies across the
keyboard, a pickup derived from geometry rather than from two dB controls, and
a note that is not identical every time you play it.

## Lineage

* **EP-MK1** — Pure Data physical model by Miguel Moreno (Mike Moreno Audio),
  GPL-3.0.
* **EP-MK1 JUCE port** — hand-written C++ reimplementation, verified against the
  Pd patch and against an hvcc/Heavy build of it. Bands agree within 0.1–0.2 dB.
* **EP-MK2** — this project, by **defensem3ch**. Same licence and the same
  lineage, but the model has changed shape: a hammer with a contact time
  instead of a period, resonators normalised so decay and level are
  independent, a pickup derived from its geometry that senses dPhi/dt, a
  measured Q, and a sub-fundamental. The plugin credits its ancestry in its
  own header.

EP-MK1 stays as it is and remains usable. MK2 is not a replacement for it in
the sense of a bug-fix release; it is where the model changes shape.

## What is inherited, and what already works

Everything in `dsp/` and `plugin/` arrived from the MK1 port in working order:
29 host parameters, sample-accurate MIDI, CC64 sustain, CC120/CC123 handling, a
scaling control panel, VST3 + LV2 + Standalone, and a headless test suite that
drives the `AudioProcessor` the way a host does. All 21 checks pass on the
copy.

So MK2 begins from something playable and at parity, and every change from here
can be measured against that baseline rather than against a memory of it.

```
dsp/          pure C++ DSP, no framework dependency
plugin/       JUCE wrapper: parameters, editor, formats
tests/        headless processor tests, offline MIDI renderer
tests/parity/ verification against the original Pd patch
docs/         model notes and the MK2 roadmap
```

The DSP being framework-free is load-bearing: it is what let the MK1 port be
diffed sample-for-sample against the Heavy build, and it is what will let MK2's
new model be fitted against recorded samples offline.

## Where MK2 is going

See **[docs/ROADMAP.md](docs/ROADMAP.md)** for the full list with
implementation notes. In short:

**Structure the measurements demand**

* Four tine modes (7.1, 20.4, 39.7) instead of two, plus the tone bar's
  sub-fundamental at 0.58–0.83.
* Q that varies 731→2175 across the keyboard instead of one global value.
* A pickup modelled from tine–pickup distance and offset, which is where the
  sidebands around each mode come from.
* Oversampling, so the bottom octave stops aliasing when the pickup is driven.

**Behaviour, not just timbre** — from the GSi argument that a model whose every
note is identical has given up its main advantage over a sample:

* Hammer strike phase relative to the tine's motion, which varies shot to shot.
* Sympathetic resonance between voices when the sustain pedal is down.
* Attack variation, including the occasional unexpectedly hard strike.

**Benchmark** — a 156-sample reference set (13 pitches × 12 velocity layers) is
used to check the model against a real instrument's partial ratios, decay rates
and velocity response. See the roadmap for how it is used, and
`docs/MODEL-NOTES.md` for what each control physically means.

## Building

JUCE is not vendored. Clone it beside the sources, or symlink an existing
checkout (it is gitignored):

```bash
git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE.git
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Artefacts land in `build/EPMK2_artefacts/Release/`:

* `VST3/EP-MK2.vst3` — copy or symlink into `~/.vst3/`
* `LV2/EP-MK2.lv2` — copy or symlink into `~/.lv2/`
* `Standalone/EP-MK2` — runs directly

Linux needs the usual JUCE development packages (ALSA, X11, freetype, GTK).
`JUCE_WEB_BROWSER=0` is set, so webkit2gtk is not required.

### Tests

```bash
cmake --build build --target EPMK2_tests
./build/EPMK2_tests_artefacts/Release/EPMK2_tests
```

Buffer handling, event scheduling, note-off, CC64, CC120/CC123, parameter
exposure and the editor — without needing a DAW. The editor checks need an X
server; use `xvfb-run` on a headless machine.

There is also an offline renderer that plays a MIDI file through the plugin and
reports cost, peak voice count and any discontinuities:

```bash
cmake --build build --target EPMK2_playmidi
./build/EPMK2_playmidi_artefacts/Release/EPMK2_playmidi performance.mid --out render.wav
```

## Licence

GPL-3.0, inherited from EP-MK1. See `LICENSE`.
