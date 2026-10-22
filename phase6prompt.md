Phase 5 is complete and the rescalable GUI is working great in FL Studio. Now let's proceed to **Phase 6**, where we implement a **Dual Assignable Multi-Target LFO Engine (LFO 1 & LFO 2)** to bring authentic 1993 Casio CTK-1000 delayed vibrato, tremolo, filter sweeps, and animated IXA wavefolder movement to the synthesizer.

CRITICAL REQUIREMENT:
1. Maintain 100% compatibility with our Phase 5 codebase (`IXAOscillator.h`, `CasioSampleBankManager.h`, `IXAHybridVoice.h`, `CTK1000Effects.h`, `PluginProcessor.h/.cpp`, `PluginEditor.h/.cpp`), including our Phase 5 `mainCanvas` + `AffineTransform` rescaling architecture.
2. Write complete, production-ready C++20 code with zero placeholders or `// TODO` comments.

### 1. DSP Specification: Dual Per-Voice LFOs with Fade-In Delay (`Source/IXAHybridVoice.h`)
Implement two independent LFO generators (`LFO 1` and `LFO 2`) inside `IXAHybridVoice` (replacing or upgrading the basic fixed CC#1 vibrato LFO so CC#1 Mod Wheel smoothly boosts LFO 1 depth while both LFOs can run autonomously per patch):
* **5 Selectable LFO Waveforms (`LfoWaveform` enum):**
  1. `Triangle` (Authentic Casio hardware stepped digital triangle LFO)
  2. `Sine` (Smooth continuous sine)
  3. `SawUp` (Rising ramp for rhythmic/arpeggiated wavefolder sweeps)
  4. `Square` (Bipolar square for trills and hard-gated tremolos)
  5. `SampleAndHold` (Pseudo-random stepped value latched once per LFO cycle for classic 90s Casio sci-fi / special FX patches)
* **Per-LFO Controls:**
  * `RATE`: $0.05\text{ Hz}$ to $30.0\text{ Hz}$ (skewed toward $0.5\text{–}8.0\text{ Hz}$, default `5.0 Hz` for LFO 1, `1.2 Hz` for LFO 2).
  * `DELAY` (Fade-In Ramp Time): $0.0\text{ s}$ to $5.0\text{ s}$ (default `0.0 s`). On `startNote()`, reset a per-voice linear/smooth ramp envelope $E_{\text{delay}}[k] \in [0, 1]$ that holds at or ramps smoothly from $0.0$ to $1.0$ over `DELAY` seconds so patches can have authentic delayed vibrato or delayed wavefolder bloom.
  * `DEPTH`: Bipolar modulation depth $\in [-1.0, +1.0]$ (default `0.0`). For `LFO 1`, add MIDI CC#1 (`modWheelDepth`) to the effective depth so the Mod Wheel always works out of the box even when `LFO1_DEPTH` is at `0.0`.
  * `KEY_SYNC`: Boolean toggle (default `true`). When `true`, reset LFO phase to `0.0` (and reset the `DELAY` fade-in ramp) on `startNote()`.
  * `TARGET` (11 Assignable Modulation Destinations):
    0. `None / Off`
    1. `Master Pitch (Vibrato)` ($\pm 2.0$ semitones max at full depth, applied to both IXA Ops and PCM Elements A & B)
    2. `Master Amplitude (Tremolo)` (Modulates voice output VCA gain)
    3. `IXA 12dB LPF Cutoff` ($\pm 3.5$ octaves added to the Phase 4 filter cutoff exponent)
    4. `IXA Op 1 Modulation Index (n)` ($\pm 3.0$ added to Op 1 instantaneous index $n[k]$)
    5. `IXA Op 2 Modulation Index (n)` ($\pm 3.0$ added to Op 2 instantaneous index $n[k]$)
    6. `IXA Both Ops Index (n1 + n2)` (Simultaneous wavefolder sweep across Op 1 and Op 2)
    7. `IXA Op 1 Carrier Warp (alpha)` ($\pm 0.8$ added to Op 1 $\alpha \in [0, 1]$)
    8. `IXA Op 1 Self-Feedback (beta)` ($\pm 0.35$ added to Op 1 $\beta \in [0, 0.5]$)
    9. `IXA Op 2 Fine Frequency / Ratio` (Creates dynamic FM/IXA harmonic clang and metallic shifting)
    10. `PCM A/B Crossfade Balance` (Dynamically pans/morphs between PCM Element A and PCM Element B levels)

### 2. APVTS Parameter Additions (`Source/PluginProcessor.h` & `Source/PluginProcessor.cpp`)
Add the 12 new LFO parameters (6 for `LFO1`, 6 for `LFO2`) to `createParameterLayout()` in `PluginProcessor.cpp` and pass their state into each `IXAHybridVoice` inside `processBlock()`:
* For `i` in `{1, 2}`:
  * `"LFO" + i + "_WAVE"`: `AudioParameterChoice` (`"Triangle"`, `"Sine"`, `"Saw Up"`, `"Square"`, `"Sample & Hold"`, default `0`)
  * `"LFO" + i + "_TARGET"`: `AudioParameterChoice` (the 11 targets listed above, default `1` (`Master Pitch`) for LFO 1, `6` (`IXA Both Ops Index`) for LFO 2)
  * `"LFO" + i + "_RATE"`: `AudioParameterFloat` (`0.05f` to `30.0f`, skew `0.35f`, default `5.2f` for LFO 1, `1.2f` for LFO 2, `"Hz"`)
  * `"LFO" + i + "_DELAY"`: `AudioParameterFloat` (`0.0f` to `5.0f`, skew `0.5f`, default `0.35f` for LFO 1, `0.0f` for LFO 2, `"s"`)
  * `"LFO" + i + "_DEPTH"`: `AudioParameterFloat` (`-1.0f` to `+1.0f`, step `0.01f`, default `0.0f`)
  * `"LFO" + i + "_SYNC"`: `AudioParameterBool` (default `true`)

### 3. GUI Integration inside `mainCanvas` (`Source/PluginEditor.h` & `Source/PluginEditor.cpp`)
Integrate a clean **"DUAL ASSIGNABLE MODULATION LFOs (LFO 1 & LFO 2)"** panel into our existing `mainCanvas` layout without breaking the Phase 5 aspect-ratio rescaling:
* **Layout Placement:** Either convert the bottom-left **1993 CTK-1000 Front-Panel Tone Editor Override** panel into a split panel (compact Legacy `0–9` Override on the left + **LFO 1 & LFO 2** side-by-side on the right), OR expand `kBaseHeight` slightly by ~`95` pixels so a full-width **"MODULATION MATRIX // DUAL LFO ENGINE (LFO 1 & LFO 2)"** strip sits cleanly above the MIDI keyboard while preserving the exact `0.60f` default scale factor. (Expanding `kBaseHeight` by ~`95` px is super clean because nothing existing gets cramped!).
* For both **LFO 1** and **LFO 2**, provide:
  * `WAVE` dropdown, `TARGET` dropdown, `KEY SYNC` LED button, and rotary knobs for `RATE`, `DELAY`, and `DEPTH`.
  * A small glowing red LED indicator dot next to each LFO header that pulses in real time at the active LFO rate!
* Connect all 12 controls to `processorRef.apvts` via `ComboBoxAttachment`, `SliderAttachment`, and `ButtonAttachment`.

### Phase 6 Deliverable
1. Update `Source/IXAHybridVoice.h`, `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/PluginEditor.h`, and `Source/PluginEditor.cpp` with the complete Dual LFO Engine.
2. Output an updated complete project `.zip` archive containing `CMakeLists.txt` and all files in `Source/`.