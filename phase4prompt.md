Phase 3.5 is complete and working great in FL Studio. Now let's proceed to **Phase 4**, where we add a dedicated **per-voice 12 dB/octave (2-pole) Non-Resonant Lowpass Filter** specifically to the **IXA Synthesis Engine** section.

In the CTK-1000 hardware (`NEC uPD939GD` internal digital filter states + `Mitsubishi M5216A` reconstruction filtering), gentle 2-pole lowpass filtering tames the dense high-frequency harmonic "fuzz" produced by the US Patent 5,164,530 triangular wavefolder $\Delta(x)$ without dulling the parallel 16-bit PCM sample layers.

CRITICAL REQUIREMENT: Maintain 100% compatibility with our existing codebase (`IXAHybridVoice.h`, `PluginProcessor.h/.cpp`, `PluginEditor.h/.cpp`). Write complete, production-ready C++20 code with zero placeholders or `// TODO` comments.

### 1. DSP Specification: Per-Voice 12 dB/oct Non-Resonant Lowpass Filter (`Source/IXAHybridVoice.h`)
Implement a smooth, modulation-stable 2-pole (12 dB/octave slope) non-resonant lowpass filter inside `IXAHybridVoice` that processes the summed IXA synthesis signal (`ixaSample`) *before* it is mixed with PCM Elements A and B:
* **Filter Topology:** Use a 2-pole Chamberlin / Cytomic Zero-Delay-Feedback (ZDF) State Variable Filter (`juce::dsp::StateVariableTPTFilter<float>` or a lightweight custom inline 2-pole ZDF trapezoidal SVF) locked to a Butterworth non-resonant damping factor of $Q = \frac{1}{\sqrt{2}} \approx 0.70710678$ ($R = \frac{1}{2Q} = 0.70710678$). Using a trapezoidal/TPT structure guarantees zero zipper noise or instability when the cutoff frequency is modulated per-sample by the envelope or velocity.
* **Dynamic Cutoff Modulation Formula:**
  Provide three controls for the IXA Lowpass Filter so it behaves like a musical synthesizer filter:
  1. `IXA_LPF_CUTOFF`: Base cutoff frequency $f_{\text{cut}} \in [80.0\text{ Hz}, 20000.0\text{ Hz}]$ (logarithmic/skewed range, default `18000.0 Hz`). When `IXA_LPF_CUTOFF >= 19800.0 Hz` and `IXA_LPF_ENV == 0.0`, cleanly bypass the filter math to save CPU and preserve raw 1993 hardware aliasing when desired.
  2. `IXA_LPF_ENV`: Filter Envelope / Velocity Modulation Depth in semitones or octaves ($\in [-4.0, +4.0]$ octaves, default `0.0`), driven by the IXA Modulation ADSR envelope $E_{\text{mod}}[k]$ scaled by note velocity.
  3. `IXA_LPF_KEYTRACK`: Keyboard tracking amount $\in [0.0, 1.0]$ (0% to 100% around middle C, MIDI note 60, default `0.5`).
  * Compute the instantaneous cutoff frequency $f_c[k]$ (clamped to $[40.0\text{ Hz}, 0.48 \cdot f_s]$):
    $$f_{\text{instant}}[k] = f_{\text{cut}} \cdot 2^{\left(\text{keyTrack} \cdot \frac{\text{midiNote} - 60}{12.0}\right) + \left(\text{envDepthOct} \cdot E_{\text{mod}}[k] \cdot (0.3 + 0.7 \tilde{v})\right)}$$
  * Update the TPT coefficients every 8 or 16 samples (control-rate sub-stepping inside `renderNextBlock`) for negligible CPU overhead while maintaining completely smooth sweeps.

### 2. APVTS Parameter Additions (`Source/PluginProcessor.h` & `Source/PluginProcessor.cpp`)
Add the new IXA Lowpass Filter parameters to `createParameterLayout()` in `PluginProcessor.cpp` and pass their atomic values into each `IXAHybridVoice` inside `processBlock()`:
* `"IXA_LPF_CUTOFF"`: `AudioParameterFloat` (`80.0f` to `20000.0f`, skew factor `0.25f` centered around ~1.8 kHz, default `18000.0f`, label `"Hz"`).
* `"IXA_LPF_ENV"`: `AudioParameterFloat` (`-4.0f` to `+4.0f`, step `0.01f`, default `0.0f`, label `"Oct"`).
* `"IXA_LPF_KEYTRACK"`: `AudioParameterFloat` (`0.0f` to `1.0f`, step `0.01f`, default `0.50f`).

### 3. GUI Integration (`Source/PluginEditor.h` & `Source/PluginEditor.cpp`)
* Integrate the 3 new rotary controls (**LPF CUTOFF**, **LPF ENV**, **LPF KEY**) cleanly into the **IXA Synthesis Engine** center panel (or in a dedicated compact **"IXA 12dB LOWPASS FILTER"** sub-group on the right side of the IXA Operator section) using our existing CTK-1000 black/charcoal, vermilion orange-red (`#E84A27`), and amber (`#E58E26`) `LookAndFeel`.
* Connect all 3 knobs to `processorRef.apvts` via `SliderAttachment`.
* Optional visual polish: In the top-right **Live Interactive IXA Wavefolder Oscilloscope**, lightly smooth/filter the displayed preview waveform when `IXA_LPF_CUTOFF` is turned down so the user gets immediate visual feedback of the high-frequency wavefolder corners rounding off.

### Phase 4 Deliverable
1. Update `Source/IXAHybridVoice.h`, `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/PluginEditor.h`, and `Source/PluginEditor.cpp` with the complete 12 dB/oct non-resonant IXA lowpass filter implementation.
2. Output an updated complete project `.zip` archive containing `CMakeLists.txt` and all files in `Source/`.