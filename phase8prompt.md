Phase 7 is complete and the real-time loop crossfading engine works great. Now let's proceed to **Phase 8 (Final Feature Stage)**, where we add a **"LOAD WAVE..." custom `.WAV` file loader** to both **PCM Element A** and **PCM Element B** so users can load their own individual `.WAV` files (such as Yamaha PSR-500 or Korg M1 waveforms) directly from disk into either PCM slot alongside the SA-51 bank.

CRITICAL REQUIREMENT:
1. Maintain 100% compatibility with our Phase 7 codebase (`IXAOscillator.h`, `CasioSampleBankManager.h`, `IXAHybridVoice.h`, `CTK1000Effects.h`, `PluginProcessor.h/.cpp`, `PluginEditor.h/.cpp`), including our Phase 5 `mainCanvas` + `AffineTransform` rescalable GUI and Phase 7 Loop Crossfading.
2. User `.WAV` files must NOT be stored or embedded inside the plugin binary or APVTS XML blob; store only the external file path strings (`customWavPathA`, `customWavPathB`) in the processor state so keeping track of custom `.WAV` files on disk remains lightweight and entirely up to the user.
3. Write complete, production-ready C++20 code with zero placeholders or `// TODO` comments.

### 1. Single-File Custom `.WAV` Loader in `CasioSampleBankManager.h` & `PluginProcessor`
Extend `CasioSampleBankManager` and `CasioCTK1000AudioProcessor` to support loading an arbitrary external `.wav` file into **PCM Slot A** or **PCM Slot B**:
* **Custom `.WAV` Loading Method (`loadCustomWaveFileForSlot(int slotIndex, const juce::File& wavFile)`):**
  * Verify `wavFile.existsAsFile()` and open it using `juce::AudioFormatManager` (`WavAudioFormat`).
  * Read the audio samples into a single-zone (full keyboard range `lowNote = 0` to `highNote = 127`) `InstrumentGroup` / sample structure for that slot.
  * **Root MIDI Note Detection:**
    1. First check if `reader->metadataValues` contains `"MidiUnityNote"` (from the RIFF `smpl` chunk) and use it if valid (`0`–`127`).
    2. Otherwise, check if the filename ends with trailing digits (using our existing safe ASCII digit parser from Phase 3.5) within `0`–`127`.
    3. Otherwise, default `rootNote` to `60` (Middle C, C4).
  * **Loop Point & Zero-Crossing Detection:**
    * Extract `Loop0Start` and `Loop0End` from `reader->metadataValues` if present; otherwise apply our default loop window (40% to 95% of sample length) with our Phase 7 $\pm 64$-sample positive-slope zero-crossing snap.
    * All existing slot controls (`COARSE`, `FINE`, `LOOP MODE`, `LOOP OVR`, `LP START`, `LP END`, and Phase 7 `LP XFADE`) must work identically on custom loaded `.WAV` files!
* **Interaction Between SA-51 Dropdown & Custom `"LOAD WAVE..."`:**
  * Track per-slot state in `PluginProcessor`:
    * `bool isUsingCustomWavA = false;` / `juce::String customWavPathA;`
    * `bool isUsingCustomWavB = false;` / `juce::String customWavPathB;`
  * When a user loads a file via `"LOAD WAVE..."` on Slot A (or Slot B), set `isUsingCustomWavA = true`, store `customWavPathA = wavFile.getFullPathName()`, save `wavFile.getParentDirectory()` as `"lastCustomWavDir"` in our persistent `settings.xml` (`PropertiesFile`), and swap the active sample data for that slot thread-safely.
  * Conversely, if the user subsequently selects a preset from the **SA-51 Instrument `ComboBox`** for that slot, automatically set `isUsingCustomWav = false`, clear `customWavPath` for that slot, and load the selected SA-51 multi-sample group.
* **DAW Project State Serialization (`getStateInformation` / `setStateInformation`):**
  * Save `customWavPathA` and `customWavPathB` as string attributes on the processor's state `ValueTree` / XML.
  * On `setStateInformation()`, if `customWavPathA` (or `B`) is non-empty and the `juce::File` exists on disk, automatically reload that custom `.wav` file into Slot A (or B). If the file was moved or deleted by the user, fall back gracefully without crashing and display `"MISSING WAV"` in the slot status.

### 2. GUI Integration inside `mainCanvas` (`Source/PluginEditor.h` & `Source/PluginEditor.cpp`)
Add the `"LOAD WAVE..."` button to both **PCM Element A** and **PCM Element B** panels:
* **Button Placement & Styling:**
  * In both the **PCM Element A** and **PCM Element B** sub-panels, right next to the SA-51 Instrument `ComboBox` (and `ON` button), place a compact hardware-styled button labeled `"LOAD WAVE..."` (using our CTK-1000 royal blue `#2B4C8C` or crimson/vermilion accent styling).
* **Async FileBrowser Workflow:**
  * Clicking `"LOAD WAVE..."` for Slot A (or Slot B) launches an asynchronous `juce::FileChooser` (`"Select Custom PCM .WAV File..."`, starting in the saved `"lastCustomWavDir"` folder, filtering for `"*.wav;*.WAV"`).
  * When a `.wav` file is chosen:
    1. Call `processorRef.loadCustomWaveFileForSlot(slotIndex, chosenFile)`.
    2. Update the slot's `ComboBox` text (via `setTextWhenNothingSelected` or a custom display text override like `"USR: " + chosenFile.getFileNameWithoutExtension()`) AND update the top red 7-segment/LED status readout (`A:USR_<NAME>` or `B:USR_<NAME>`) so the user clearly sees which custom `.wav` is active in that slot.

### Phase 8 Deliverable
1. Update `Source/CasioSampleBankManager.h`, `Source/IXAHybridVoice.h` (if needed), `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/PluginEditor.h`, and `Source/PluginEditor.cpp` with the complete per-slot custom `.WAV` file loader.
2. Output the final updated project `.zip` archive containing `CMakeLists.txt` and all files in `Source/`.