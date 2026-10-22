Phase 6 is complete and the Dual LFO engine is working great in FL Studio. Now let's proceed to **Phase 6.5**, where we implement the classic late-80s/early-90s **Casio SA-Series / Yamaha Portasound "Fake Sustain & Envelope Reverb"** effect.

On vintage keyboards without dedicated reverb DSP (or when layering pseudo-reverb tails on the Casio SA-series and Yamaha PSS-480), "Sustain" and "Reverb" were created inside the digital VCA amplitude envelope upon key-up (`noteOff`). Because standard `juce::ADSR` uses a purely linear release ramp, a long release in `juce::ADSR` does not sound like hardware fake sustain. Instead, hardware envelope counters use a **sharp-curve / two-stage concave exponential release** (an immediate steep drop upon key-up down to an adjustable "echo/reverb shelf", followed by a long exponential tail, optionally quantized to 8-bit envelope steps).

CRITICAL REQUIREMENT:
1. Maintain 100% compatibility with our Phase 6 codebase (`IXAOscillator.h`, `CasioSampleBankManager.h`, `IXAHybridVoice.h`, `CTK1000Effects.h`, `PluginProcessor.h/.cpp`, `PluginEditor.h/.cpp`), including our `mainCanvas` + `AffineTransform` rescaling architecture.
2. Write complete, production-ready C++20 code with zero placeholders or `// TODO` comments.

### 1. DSP Specification: Dual-Slope Concave "Fake Sustain / Env-Reverb" (`Source/IXAHybridVoice.h`)
Create a dedicated helper struct or envelope post-processor inside `IXAHybridVoice` that transforms the post-`noteOff` release behavior of our voices (applicable to IXA Synth, PCM Element A, and PCM Element B):
* **4 Selectable Fake Sustain Modes (`FakeSustainMode` enum):**
  0. `Off (Standard ADSR)`: Uses standard `juce::ADSR` release behavior.
  1. `SA-Series Sustain (Concave Exp)`: Upon `noteOff`, replaces the linear release with a sharp concave exponential decay curve $E_{\text{rel}}(t) = E_{\text{noteOff}} \cdot \exp(-t / \tau_{\text{tail}})^p$ (where curvature power $p \in [1.5, 4.0]$ creates an immediate snappy drop followed by a lingering sustain tail).
  2. `PSS-480 Fake Reverb (2-Stage Shelf)`: Upon `noteOff`, drops rapidly over ~`45 ms` from $E_{\text{noteOff}}$ down to an adjustable **Reverb Shelf Level** $L_{\text{shelf}} \in [0.10, 0.50] \times E_{\text{noteOff}}$, and then decays exponentially over `FAKE_SUS_TIME` ($\tau_{\text{tail}} \in [0.3\text{ s}, 6.0\text{ s}]$). This creates the exact psychoacoustic illusion of a dry note stopping while a room reverb tail rings out!
  3. `Stepped 8-Bit Echo Tail`: Identical to Mode 2 (`2-Stage Shelf`), but quantizes the decaying envelope multiplier to 64 or 128 discrete digital attenuation steps and optionally applies a subtle $12\text{ Hz}$ sample-hold envelope clock jitter to emulate late-80s SA-1 / Portasound digital VCA zipper tail character.
* **Controls & Parameters:**
  * `FAKE_SUS_MODE`: Choice (`0`: Off, `1`: SA-Series Concave, `2`: PSS-480 Reverb Shelf, `3`: 8-Bit Stepped Echo).
  * `FAKE_SUS_TIME`: Tail duration $\in [0.20\text{ s}, 6.00\text{ s}]$ (default `1.80 s`).
  * `FAKE_SUS_CURVE`: Shelf level / Concave steepness $\in [0.05, 0.85]$ (default `0.30`). In Mode 1, controls how steeply the initial drop curves; in Modes 2 & 3, sets the initial drop-down shelf ratio $L_{\text{shelf}}$ where the fake reverb tail begins.
  * `FAKE_SUS_ROUTING`: Choice (`0`: Global All Layers, `1`: IXA Synth Only, `2`: PCM A+B Only) so the user can apply fake sustain globally or only to the PCM or IXA layer.
* **Voice Lifecycle Handling:**
  * When `FAKE_SUS_MODE != Off`, ensure `IXAHybridVoice::renderNextBlock` keeps the voice active until the custom Fake Sustain tail envelope drops below `0.0005f` (calling `clearCurrentNote()` only when both the active layers and the fake sustain tail have finished decaying).
  * If a voice is re-triggered while in the fake sustain release phase, smoothly reset or capture the current envelope level to prevent clicks.

### 2. APVTS Parameter Additions (`Source/PluginProcessor.h` & `Source/PluginProcessor.cpp`)
Add the 4 new parameters to `createParameterLayout()` in `PluginProcessor.cpp` and pass them into each `IXAHybridVoice` inside `processBlock()`:
* `"FAKE_SUS_MODE"`: `AudioParameterChoice` (`"Off (Std ADSR)"`, `"SA-Series Sustain"`, `"PSS-480 Fake Reverb"`, `"8-Bit Stepped Echo"`, default `0`)
* `"FAKE_SUS_TIME"`: `AudioParameterFloat` (`0.20f` to `6.00f`, skew `0.45f`, default `1.80f`, `"s"`)
* `"FAKE_SUS_CURVE"`: `AudioParameterFloat` (`0.05f` to `0.85f`, step `0.01f`, default `0.30f`)
* `"FAKE_SUS_ROUTING"`: `AudioParameterChoice` (`"All Layers (Global)"`, `"IXA Synth Only"`, `"PCM Elements Only"`, default `0`)

### 3. GUI Integration inside `mainCanvas` (`Source/PluginEditor.h` & `Source/PluginEditor.cpp`)
Integrate a compact **"SA / PSS ENVELOPE SUSTAIN & FAKE REVERB"** control group cleanly into `mainCanvas`:
* **Placement:** Place it either right alongside the **Legacy 1993 Tone Editor Override** panel OR in the **Modulation / LFO strip** added in Phase 6 so no existing controls are crowded.
* Include:
  * `FAKE_SUS_MODE` dropdown selector (with a glowing red hardware indicator LED that lights up whenever Mode > 0).
  * `FAKE_SUS_ROUTING` dropdown selector.
  * Rotary knobs for `TAIL TIME` (`FAKE_SUS_TIME`) and `SHELF / CURVE` (`FAKE_SUS_CURVE`).
* Connect all 4 controls to `processorRef.apvts` via `ComboBoxAttachment` and `SliderAttachment`.

### Phase 6.5 Deliverable
1. Update `Source/IXAHybridVoice.h`, `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/PluginEditor.h`, and `Source/PluginEditor.cpp` with the complete Fake Sustain & Envelope Reverb engine.
2. Output an updated complete project `.zip` archive containing `CMakeLists.txt` and all files in `Source/`.