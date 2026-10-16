You are an expert C++20 Audio DSP Engineer and JUCE 8 Framework Developer. We are building a production-ready VST3 and Standalone synthesizer plugin in C++20 using CMake and JUCE 8 (`https://github.com/juce-framework/JUCE.git`, tag `8.0.4`).

The plugin is an "R&D Engineer's Edition" software reconstruction of the **1993 Casio CTK-1000 IXA ("Integrated Cross-Sound Architecture")** synthesis engine, combining Casio's **US Patent 5,164,530** (Triangular Wave Modulation) with **US Patent 5,319,151** (16-bit 22.05 kHz Dual-Element Algorithmic PCM playback). Rather than restricting the user to the consumer CTK-1000's 3-parameter (`0`–`9`) front-panel Tone Editor, this VST3 exposes the full underlying `NEC uPD939GD` synthesis and hybrid layering architecture in a streamlined, modern synthesizer workflow.

### 1. Core Mathematical & DSP Specifications (US Patent 5,164,530)
Implement the exact mathematical formulation of Triangular Wave Modulation:
* **Phase Accumulators:** Maintain normalized modulo-1.0 phase accumulators $\phi_c[k] \in [0, 1)$ for the carrier and $\phi_m[k] \in [0, 1)$ for the modulator:
  $$\phi_c[k] = \bmod\left(\phi_c[k-1] + \frac{f_c}{f_s},\, 1.0\right), \quad \phi_m[k] = \bmod\left(\phi_m[k-1] + \frac{r \cdot f_c + \Delta f_{\text{detune}}}{f_s},\, 1.0\right)$$
  Use a positive floored modulo helper: `positiveMod(a, b) = a - b * std::floor(a / b)`.
* **Pre-Distorted Monotonous Carrier Phase $W_\alpha(\phi_c)$:** Maps $\phi_c \in [0, 1)$ monotonically onto $[0, 4)$ with zero-slope saddle points at $\phi_c = 0.25$ ($W = 1$) and $\phi_c = 0.75$ ($W = 3$). Use the exact 3-branch piecewise algebraic equivalence:
  $$s_c = \sin(2\pi \phi_c), \quad W(\phi_c) = \begin{cases} s_c & \text{if } 0 \leq \phi_c < 0.25 \\ 2.0 - s_c & \text{if } 0.25 \leq \phi_c < 0.75 \\ 4.0 + s_c & \text{if } 0.75 \leq \phi_c < 1.00 \end{cases}$$
  Blend continuously via the Carrier Warp parameter $\alpha \in [0, 1]$: $W_\alpha(\phi_c) = (1 - \alpha)(4\phi_c) + \alpha W(\phi_c)$.
* **Continuous Engineer-Level Modulation Index $n[k]$:** Given continuous Base Index $n_0 \in [0.0, 6.0]$, Envelope Depth $n_{\text{env}} \in [-5.0, +5.0]$, Velocity Depth $n_{\text{vel}} \in [0.0, 5.0]$, normalized MIDI velocity $\tilde{v} = v / 127.0$, and dedicated Modulation ADSR Envelope $E_{\text{mod}}[k] \in [0, 1]$:
  $$n[k] = \max\left(0.0,\, n_0 + \left(n_{\text{env}} + n_{\text{vel}} \cdot \tilde{v}^{1.8}\right) \cdot E_{\text{mod}}[k]\right)$$
* **Composite Phase Argument $x[k]$ & Wavefolder $\Delta(x[k])$:**
  $$x[k] = W_\alpha(\phi_c[k]) + n[k] \sin(2\pi \phi_m[k]) + \phi_{\text{ext}} + \beta \, y[k-1]$$
  $$u[k] = \bmod(x[k] + 1.0,\, 4.0), \quad \Delta(x[k]) = 1.0 - \big\vert{}u[k] - 2.0\big\vert{} \in [-1, +1]$$
* **Anti-Aliasing & 1993 Hardware Lo-Fi Mode:** Implement switchable First-Order Antiderivative Anti-Aliasing (ADAA) using:
  $$F_1(x) = \begin{cases} 0.5(u - 1.0)^2 - 0.5 & \text{if } 0 \leq u < 2.0 \\ -0.5(3.0 - u)^2 + 0.5 & \text{if } 2.0 \leq u < 4.0 \end{cases}$$
  Include a switchable "1993 DAC Mode" (22,050 Hz internal sample hold with ADAA bypassed).

### 2. Automatic Sample Library Auto-Mapper & Persistent Folder Memory (`CasioSampleBankManager.h`)
I have attached `sa51_filelist.txt`, which lists all 761 `.wav` files extracted from the Casio SA-51 SoundFont. Inspect the exact naming conventions in `sa51_filelist.txt` (e.g., `<instrumentName><midiNoteNumber>.wav` such as `accordion60.wav`, plus any multi-part or drum/percussion naming quirks present in the list).
* **Exact Filename Parser:** Build `CasioSampleBankManager` so its string/regex parser cleanly handles 100% of the filenames in `sa51_filelist.txt`, extracting the **Instrument Group Name** and the **Root MIDI Note** (`0`–`127`).
* **Automatic Key-Zone Splits:** When an instrument group has multiple `.wav` files across different root notes, automatically sort them by root note and compute contiguous key-split zones (`lowNote` to `highNote` midpoints between adjacent root notes) across `0`–`127` so each voice automatically selects and pitch-tracks the appropriate sample for any played MIDI note.
* **Persistent Sample Folder Memory:** Use `juce::ApplicationProperties` / `juce::PropertiesFile` (saving to the user's application data directory under `"CasioCTK1000_IXA/settings.xml"`) to automatically save and remember the local directory path of the 761 `.wav` files across all DAW sessions and plugin instances. On plugin startup, `CasioSampleBankManager` must automatically check if the saved directory path exists and auto-scan the bank immediately without requiring the user to re-locate the folder. Also expose helper methods to serialize/deserialize the active path and selected Slot A / Slot B instrument names inside the processor's `ValueTree` state.
* **Loop Extraction & Ping-Pong Playback:** Read `smpl` chunk loop points (`Loop0Start`, `Loop0End`) from the `.wav` headers via `juce::AudioFormatReader::metadataValues` if present, with a fallback default loop range (e.g., 40% to 95% of sample length) and continuous GUI parameter overrides for Loop Start (0–100%), Loop End (0–100%), and Loop Mode (One-Shot, Forward Loop, and US Patent 5,319,151 Bidirectional Ping-Pong Loop).

### 3. Dual-Element "Engineer's Workstation" Voice Architecture (16-Voice Polyphonic)
Expose full continuous JUCE `AudioProcessorValueTreeState` parameters for:
* **Dual PCM Sample Slots (Element A & Element B):** Allow loading any parsed instrument group from `CasioSampleBankManager` into Slot A and Slot B independently, with per-slot Level, Pan, Coarse/Fine Tune, Velocity-to-Level, Loop Mode, Loop Start/End overrides, and dedicated Amp ADSR.
* **Dual-Operator IXA Synth Engine (Operators 1 & 2):**
  * Routing Mode: `Parallel Detuned`, `2-Op Series Cascade (Op 2 -> Op 1)`, or `Single Op`.
  * Per-Operator Continuous Controls: Coarse Ratio ($0.5$ to $16.0$), Fine Ratio / Linear Detune ($\text{Hz}$), Base Modulation Index $n_0$, Mod Env Depth $n_{\text{env}}$, Velocity-to-Index $n_{\text{vel}}$, Carrier Warp $\alpha \in [0, 1]$, Self-Feedback $\beta \in [0, 0.5]$, Mod ADSR, and Amp ADSR.
  * Include a "Legacy CTK-1000 Macro Override" toggle if the user wants to audition the classic `0`–`9` stepped table.
* **8-Mode Semi-OBS DSP Effect Processor:** Emulate the `Sanyo LC33832PL-70` 32 KB PSRAM effects unit (`Hall Reverb`, `Delay`, `Chorus`, `Distortion`, `Room Reverb`, `Stereo Delay`, `Flanger`, `Chorus Reverb`) with continuous controls for Rate/Time, Depth/Feedback, Tone Filter, and Wet/Dry Mix.

### 4. Project Structure & Phase 1 Deliverable
We will build this modularly with zero placeholders or omitted code. Our target file structure is:
* `CMakeLists.txt`
* `Source/IXAOscillator.h`
* `Source/CasioSampleBankManager.h`
* `Source/IXAHybridVoice.h`
* `Source/CTK1000Effects.h`
* `Source/PluginProcessor.h`
* `Source/PluginProcessor.cpp`
* `Source/PluginEditor.h`
* `Source/PluginEditor.cpp`

**For your first response (Phase 1):**
1. Analyze the attached `sa51_filelist.txt` to verify all filename patterns.
2. Write the complete, production-ready code for `CMakeLists.txt`, `Source/IXAOscillator.h`, `Source/CasioSampleBankManager.h` (with persistent `PropertiesFile` folder memory), and `Source/IXAHybridVoice.h`.
3. Package these Phase 1 files into their proper folder hierarchy (`CMakeLists.txt` at root, headers inside `Source/`) and output a downloadable `.zip` file using your file generation tool.
4. Wait for my confirmation before moving on to Phase 2 (`CTK1000Effects.h` and `PluginProcessor.h/.cpp`).