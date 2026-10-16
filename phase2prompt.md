Phase 1 looks great. Now let's proceed to **Phase 2**, where we implement the complete 8-Mode Semi-OBS DSP Effect Processor (`Source/CTK1000Effects.h`) and the main JUCE AudioProcessor (`Source/PluginProcessor.h` and `Source/PluginProcessor.cpp`).

CRITICAL REQUIREMENT: Ensure 100% API, method signature, and struct/enum name compatibility with the exact `IXAOscillator.h`, `CasioSampleBankManager.h`, and `IXAHybridVoice.h` files you wrote in Phase 1. Write complete, production-ready C++20 code with zero placeholders, stubbed functions, or `// TODO` comments.

### 1. `Source/CTK1000Effects.h` — 8-Mode Semi-OBS DSP Effects Unit (`Sanyo LC33832PL-70` Emulation)
Implement a self-contained stereo DSP effects processor class (`CTK1000Effects`) emulating the CTK-1000's 8 hardware DSP modes driven by the 32 KB PSRAM ring buffer (`Sanyo LC33832PL-70`) and the diagnostic service mode post-filter (`PrFit`):
* **8 Selectable Modes:**
  1. `Hall Reverb`: Diffuse stereo reverberator with long decay and high-frequency damping.
  2. `Delay`: Mono/center tapped digital delay line (~22.05 kHz character) with regeneration feedback loop.
  3. `Chorus`: Quadrature stereo LFO-modulated short delay line (~15–25 ms).
  4. `Distortion`: Digital waveshaping overdrive / soft-to-hard clipper followed by a switchable/adjustable post-saturation lowpass tone filter (`PrFit`).
  5. `Room Reverb`: Short early-reflection chamber network with tight decay.
  6. `Stereo Delay`: Cross-coupled ping-pong left/right stereo delay taps.
  7. `Flanger`: Ultra-short LFO-swept delay line (1–7 ms) with regenerative feedback creating metallic comb sweeps.
  8. `Chorus Reverb`: Series cascade routing the stereo Chorus into the Hall Reverb.
* **Continuous Engineer Controls + Hardware Character:**
  * Expose continuous parameters: `dspMode` (0–7), `param1` (Rate / Delay Time / Reverb Size / Drive in `[0, 1]`), `param2` (Depth / Feedback / Damping in `[0, 1]`), `toneFilter` (`PrFit` cutoff frequency / damping in `[0, 1]`), `wetDryMix` (`[0, 1]`), and `bypass`.
  * Include an optional **"32 KB PSRAM Lo-Fi Effect Mode"** toggle that quantizes the internal delay/reverb ring buffer sample stepping or applies a 10 kHz bandwidth roll-off inside the feedback path to emulate the vintage 1993 `LC33832PL-70` memory bandwidth.

### 2. `Source/PluginProcessor.h` & `Source/PluginProcessor.cpp` — Complete APVTS & Audio Pipeline
Implement the full `AudioProcessor` class managing the 16-voice `juce::Synthesiser`, `CasioSampleBankManager`, oversampling, and `CTK1000Effects`:
* **Complete `AudioProcessorValueTreeState` (APVTS) Parameter Layout:**
  Create a comprehensive, cleanly organized parameter layout matching every control needed by `IXAHybridVoice` and `CTK1000Effects`:
  * **Global / Architecture:** `CROSS_MODE` (Pure IXA, Hybrid PCM+IXA, Dual PCM), `ROUTING_MODE` (Parallel Detuned, 2-Op Series Cascade, Single Op), `HARDWARE_DAC_MODE` (1993 22.05 kHz Lo-Fi vs Modern Oversampled ADAA), `LEGACY_MACRO_ENABLE` (Toggle for classic CTK-1000 `0`–`9` `WAVE`, `ATTACK`, `RELEASE` overrides), `MACRO_WAVE` (`0`–`9`), `MACRO_ATTACK` (`0`–`9`), `MACRO_RELEASE` (`0`–`9`), and `MASTER_GAIN`.
  * **IXA Operator 1 & Operator 2 (Continuous Engineer Controls for each Op):** Coarse Ratio (`0.5` to `16.0`), Fine Tune / Detune Hz (`-50.0` to `+50.0` Hz), Base Index $n_0$ (`0.0` to `6.0`), Mod Env Depth $n_{\text{env}}$ (`-5.0` to `+5.0`), Vel-to-Index $n_{\text{vel}}$ (`0.0` to `5.0`), Carrier Warp $\alpha$ (`0.0` to `1.0`), Feedback $\beta$ (`0.0` to `0.5`), Op Level (`0.0` to `1.0`), Mod ADSR (`A`, `D`, `S`, `R`), and IXA Amp ADSR (`A`, `D`, `S`, `R`).
  * **Dual PCM Slots (Slot A & Slot B):** Level (`0.0` to `1.0`), Pan (`-1.0` to `+1.0`), Coarse Semitones (`-24` to `+24`), Fine Cents (`-100` to `+100`), Vel-to-Level (`0.0` to `1.0`), Loop Mode (`One-Shot`, `Forward`, `Ping-Pong`), Loop Start Override (`0.0` to `1.0`), Loop End Override (`0.0` to `1.0`), and per-slot Amp ADSR (`A`, `D`, `S`, `R`).
  * **8-Mode DSP Effects Section:** `DSP_MODE` (8 choices), `DSP_PARAM1`, `DSP_PARAM2`, `DSP_TONE`, `DSP_MIX`, `DSP_LOFI_PSRAM`, and `DSP_BYPASS`.
* **Sample Bank Integration & Thread-Safe Instrument Switching:**
  * Own the `CasioSampleBankManager` instance inside `PluginProcessor`.
  * On `PluginProcessor` construction, automatically trigger `CasioSampleBankManager` to check its persistent `PropertiesFile` (`"CasioCTK1000_IXA/settings.xml"`). If a valid saved folder of the 761 SA-51 `.wav` files exists, scan the bank and load default instruments into Slot A and Slot B automatically; otherwise fall back cleanly to the procedural default PCM wave so the synth makes sound immediately out of the box.
  * Provide public methods on `PluginProcessor` for the GUI (`PluginEditor`) to:
    1. Prompt/set a new sample directory path and refresh the instrument list.
    2. Get the `juce::StringArray` of discovered instrument group names from the 761 `.wav` files.
    3. Select an instrument group by name or index for **Slot A** and **Slot B** safely without audio-thread race conditions.
* **Audio Processing & Oversampling (`processBlock`):**
  * Push updated parameter states to all 16 `IXAHybridVoice` instances at the start of each block.
  * When `HARDWARE_DAC_MODE` is off (Modern Mode), run the `juce::Synthesiser` through $2\times$ polyphase IIR oversampling (`juce::dsp::Oversampling<float>`) with ADAA enabled. When `HARDWARE_DAC_MODE` is on (1993 Mode), render at native rate with the 22,050 Hz sample-and-hold emulation active and ADAA bypassed.
  * Process the stereo output through `CTK1000Effects` and apply `MASTER_GAIN`.
* **State Serialization (`getStateInformation` / `setStateInformation`):**
  * Save and restore the full APVTS state PLUS the currently selected Slot A and Slot B instrument group names and custom sample folder path as properties on the state `ValueTree` so DAW projects recall the exact SA-51 multi-samples automatically.

### Phase 2 Deliverable
1. Write the complete code for `Source/CTK1000Effects.h`, `Source/PluginProcessor.h`, and `Source/PluginProcessor.cpp` (and if any small helper method needs to be added to `CMakeLists.txt` or the Phase 1 headers to connect them cleanly, update those files as well).
2. Package all project files created so far (`CMakeLists.txt` + all Phase 1 and Phase 2 files inside `Source/`) into an updated downloadable `.zip` archive using your file generation tool.
3. Wait for my confirmation before moving on to Phase 3 (`PluginEditor.h` and `PluginEditor.cpp`).