Phase 2 is complete. Now let's proceed to **Phase 3 (Final Phase)**, where we implement the custom JUCE 8 graphical user interface (`Source/PluginEditor.h` and `Source/PluginEditor.cpp`) and ensure `CMakeLists.txt` and `Source/PluginProcessor.cpp` compile and return the custom editor.

CRITICAL REQUIREMENTS:
1. Ensure 100% API, parameter ID, and method signature compatibility with the exact `PluginProcessor.h` and `PluginProcessor.cpp` you wrote in Phase 2.
2. Use strict **JUCE 8** API conventions (for example, use `juce::FontOptions` when constructing `juce::Font` objects so it compiles cleanly on JUCE 8.0.4 without deprecation warnings).
3. Write complete, production-ready C++20 code with zero placeholders or `// TODO` comments. To prevent response timeouts, keep your conversational text minimal and focus on writing the complete `PluginEditor.h`, `PluginEditor.cpp`, updated `CMakeLists.txt`, and generating the final complete project `.zip`.

### 1. Authentic 1993 Casio CTK-1000 Hardware Color Palette & Aesthetic (`CTK1000LookAndFeel`)
Design a custom `juce::LookAndFeel_V4` subclass (`CTK1000LookAndFeel`) that faithfully matches the physical 1993 Casio CTK-1000 hardware photo while expanding it into a professional "R&D Engineer's Workstation" (`1180 x 760` window):
* **Outer Chassis & Molded Panels:** Matte dark charcoal-black outer body (`#1B1C1E`) with subtle diagonal vent-grille line detailing in the top corners (nodding to the CTK-1000 speaker grilles).
* **Recessed Glossy Console Windows:** Deep jet-black recessed inner panels (`#0D0E10`) with subtle dark grey bevel borders (`#2E3035`) housing the display, oscilloscope, and parameter matrices.
* **Signature CTK-1000 Silkscreen & LED Colors (No Turquoise):**
  * **Section Headers, Horizontal Rule Outlines & Knob Arcs:** Casio Vermilion / Orange-Red (`#E84A27`). Draw crisp 1px orange-red rectangular outlines and horizontal header divider rules around each functional group just like the silkscreened matrix on the CTK-1000 console.
  * **Sub-Labels & Parameter Text:** Warm Amber-Gold (`#D9822B`) and crisp Off-White (`#EAEAEA`) for high contrast on dark panels.
  * **7-Segment Display & Discrete LED Indicator Dots:** Bright glowing crimson-red (`#FF1E1E`) on a deep maroon-black bezel (`#1A0505`). Draw small circular red LED indicator dots next to active toggles and mode selectors just like the hardware's red panel LEDs.
  * **Hardware Accent Buttons:** Dark matte grey rectangular buttons (`#2A2C30`) for standard controls, plus the CTK-1000's signature **Royal Blue (`#2B4C8C`)** and **Crimson Red (`#A62639`)** accent buttons for high-priority actions (like the SA-51 Folder Loader and Legacy Macro Override).

### 2. Single-Screen Workstation Layout (`1180 x 760` — 5 Hardware Regions)
Organize the GUI cleanly so every control from Phase 2 is immediately accessible without hidden tabs:

1. **Top Center Console Bezel (Header, Red LED Readout, SA-51 Browser & Oscilloscope):**
   * **Left Branding:** `CASIO CTK-1000` in bold white synthesis typography with `IXA SOUND SOURCE // R&D ENGINEER'S WORKSTATION` in orange-red (`#E84A27`).
   * **Center 7-Segment LED Window & Architecture Matrix:** Glowing red LED status screen displaying active mode/patch info, flanked by `CROSS_MODE` (`Pure IXA`, `Hybrid PCM+IXA`, `Dual PCM`), `ROUTING_MODE` (`Parallel`, `Cascade`, `Single`), `HARDWARE_DAC_MODE` (`1993 22.05kHz DAC` toggle with red LED indicator dot), and `MASTER_GAIN` rotary knob.
   * **SA-51 Sample Bank Loader Bar:** A royal-blue hardware button (`SELECT SA-51 WAV FOLDER...`) that launches an async `juce::FileChooser`, passes the chosen directory of 761 `.wav` files to `PluginProcessor` / `CasioSampleBankManager`, saves it persistently, displays the active folder path/status in a red LED text readout, and immediately repopulates the Slot A and Slot B instrument `ComboBox` menus.
   * **Top-Right Live IXA Wavefolder Oscilloscope:** A recessed dark screen plotting one cycle of the active $\Delta(W_\alpha(\phi) + n \sin(2\pi r \phi))$ transfer waveform in glowing orange-red (`#E84A27`) with an amber grid, updating smoothly as the user adjusts Operator 1/2 Ratio, Index, and Carrier Warp knobs.

2. **Left Panel — Dual PCM Sample Engine (Element A & Element B):**
   * Framed with orange-red silkscreen header bars (`PCM ELEMENT A` and `PCM ELEMENT B`).
   * Each slot has an **Instrument Group `ComboBox`** populated from `CasioSampleBankManager` (e.g., `"accordion"`, `"RainDrop"`, etc.), plus controls for Level, Pan, Coarse Semitones, Fine Cents, Vel-to-Level, Loop Mode (`One-Shot`, `Forward`, `Ping-Pong`), Loop Start Override, Loop End Override, and vertical sliders or compact rotary knobs for Amp `A / D / S / R`.

3. **Center Panel — Dual-Operator IXA Synthesis Engine (Operators 1 & 2):**
   * Framed with orange-red silkscreen headers (`IXA OPERATOR 1` and `IXA OPERATOR 2`).
   * Complete continuous engineer controls for each operator: Coarse Ratio, Fine Detune Hz, Base Index $n_0$, Mod Env Depth $n_{\text{env}}$, Vel-to-Index $n_{\text{vel}}$, Carrier Warp $\alpha$, Self-Feedback $\beta$, Op Level, Mod `A / D / S / R`, and IXA Amp `A / D / S / R`.

4. **Bottom-Left Panel — Legacy 1993 CTK-1000 Tone Editor Override:**
   * Features the `LEGACY_MACRO_ENABLE` toggle button (styled in crimson `#A62639` with a red LED dot) alongside a 7-segment numeric readout and controls for `MACRO_WAVE` (`0`–`9`), `MACRO_ATTACK` (`0`–`9`), and `MACRO_RELEASE` (`0`–`9`).

5. **Bottom-Right Panel — 8-Mode Semi-OBS DSP Effect Unit (`Sanyo LC33832PL-70`):**
   * Framed with orange-red silkscreen borders (`SEMI-OBS DSP EFFECT PROCESSOR`).
   * Includes `DSP_MODE` selector (`Hall Reverb`, `Delay`, `Chorus`, `Distortion`, `Room Reverb`, `Stereo Delay`, `Flanger`, `Chorus Reverb`) with simulated red LED mode indicator dots, plus rotary controls for `DSP_PARAM1`, `DSP_PARAM2`, `DSP_TONE` (`PrFit`), `DSP_MIX`, `DSP_LOFI_PSRAM` toggle, and `DSP_BYPASS` toggle.

### 3. Parameter Attachments & Thread Safety
* Connect all sliders, combo boxes, and toggles to `processorRef.apvts` using `juce::AudioProcessorValueTreeState::SliderAttachment`, `ComboBoxAttachment`, and `ButtonAttachment` matching the exact parameter IDs defined in `PluginProcessor.cpp`.
* Ensure `PluginProcessor::createEditor()` in `PluginProcessor.cpp` returns `new CasioCTK1000AudioProcessorEditor(*this);` (update `PluginProcessor.cpp` and `CMakeLists.txt` accordingly).

### Phase 3 Deliverable
1. Write the complete `Source/PluginEditor.h` and `Source/PluginEditor.cpp`, and update `CMakeLists.txt` and `Source/PluginProcessor.cpp` (`createEditor()`) so the custom editor is compiled and linked.
2. Package the **entire completed project** (`CMakeLists.txt` at root + all `.h` and `.cpp` files in `Source/`) into a final downloadable `.zip` archive using your file generation tool.