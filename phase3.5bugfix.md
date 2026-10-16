The VST3 compiled cleanly and loaded in FL Studio 2026! Both the IXA synth engine and the custom GUI look and sound awesome. We just need a quick **Phase 3.5 Bugfix & Optimization Pass** to fix three specific issues visible in testing:

### 1. Fix Mojibake / Garbled Characters in PCM Element A & B `ComboBox` Items
* **Symptom:** In the top LED display, the instrument names render cleanly in ASCII (`A:EPIANOONE B:STRINGS`), but inside the PCM Element A and Element B `ComboBox` dropdowns, the middle of the instrument name is corrupted into CJK/mojibake characters (e.g., `24: 靉靆潮湯et (7 zn)` and `71: 瑳槭杙s髐a琀 (10 zn)`).
* **Root Cause & Fix:** This is caused by dangling `std::string` / `std::smatch`views, non-UTF8 `char*` pointer conversions, or corrupt `juce::String` concatenation when building the display name or parsing filenames in `CasioSampleBankManager.h` / `PluginEditor.cpp`.
  * Remove any `std::regex` / `std::smatch` or raw `const char*` pointer conversions in `CasioSampleBankManager.h`.
  * Parse filenames strictly using safe, native `juce::String` methods! Specifically, given `juce::String stem = file.getFileNameWithoutExtension();`:
    1. Walk backwards from the end of `stem` while `juce::CharacterFunctions::isDigit(stem[i])` to split the trailing MIDI root note digits from the leading instrument name.
    2. Strip any trailing underscores, hyphens, or spaces from the instrument name using `.trimCharactersAtEnd("_- ")`, and sanitize it with `.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_- ")` so it is guaranteed 100% clean ASCII.
  * When populating the `ComboBox` items in `PluginEditor.cpp`, construct the label cleanly using `juce::String`:
    `juce::String itemText = juce::String(i + 1) + ": " + groupName.toStdString().c_str() + " (" + juce::String(numZones) + " zn)";` (or pure `juce::String` concatenation without raw pointer casts).

### 2. Fix `juce::MidiKeyboardComponent` Blank White Space on the Right Side
* **Symptom:** The bottom `MidiKeyboardComponent` sets its playable range to 5 octaves (61 keys, `C1` to `C6`, MIDI notes `36` to `96`), which equals 36 white keys. Because `MidiKeyboardComponent` uses a narrow default `keyWidth`, the keys stop two-thirds of the way across the window at `C6` and leave a huge blank white rectangle on the right side.
* **Fix in `PluginEditor.cpp`:**
  * Set the keyboard range to `36` through `96` (`36.0f` white keys total), or full `24` (`C0`) through `108` (`C8`).
  * Inside `resized()`, after setting the bounds of the `MidiKeyboardComponent`, dynamically calculate and set `setKeyWidth()` so the white keys fill 100% of the component's width with zero empty space on the right:
    ```cpp
    // For MIDI range 36 (C1) to 96 (C6), there are exactly 36 white keys:
    midiKeyboard.setAvailableRange(36, 96);
    midiKeyboard.setKeyWidth(static_cast<float>(keyboardArea.getWidth()) / 36.0f);
    ```
  * Also set `midiKeyboard.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, juce::Colour(0xff1a1d21));` and `setScrollButtonsVisible(false);` so no scroll arrows or white backgrounds ever appear.

### 3. Eliminate Startup Stall via On-Demand (Lazy) WAV Loading in `CasioSampleBankManager.h`
* **Symptom:** Opening the VST3 in FL Studio stalls for several seconds because `CasioSampleBankManager` reads the audio data of all 761 `.wav` files from disk during directory scanning.
* **Fix in `CasioSampleBankManager.h`:**
  * During `scanDirectory()`, **only inspect the filenames** (`juce::File` objects) to build the 92 instrument groups and calculate their key-split zones (`lowNote`, `highNote`, `rootNote`, and `juce::File sourceFile`). Do NOT open `AudioFormatReader` or allocate `AudioBuffer<float>` for all 761 files during the folder scan! Scanning filenames alone takes less than 2 milliseconds.
  * Only load the actual `.wav` audio samples (`AudioFormatReader` + `smpl` loop metadata) on-demand when an instrument group is actually selected for **PCM Slot A** or **PCM Slot B** (and cache loaded groups in memory once read).

### 4. Fix Minor Header Clipping on Top-Right Knobs & Title
* In the top header bar, slightly widen the label bounds for the two small top-right knobs (`IX...` and `M...` next to `1993 22.05kHz DAC`) and the sub-header (`IXA SOUND SOURCE // R&D ENGINEER'S EDITIO`) by a few pixels so the text doesn't clip with `...`.

### Phase 3.5 Deliverable
1. Update `Source/CasioSampleBankManager.h`, `Source/PluginEditor.h`, and `Source/PluginEditor.cpp` (plus `Source/PluginProcessor.cpp` if needed for lazy loading).
2. Output an updated complete project `.zip` archive containing `CMakeLists.txt` and all files in `Source/`.