Phase 4 is complete and the 12 dB/oct IXA lowpass filter works great. Now let's proceed to **Phase 5 (Final Polish)**, where we make the plugin window **freely rescalable with a fixed aspect ratio** and set a **compact default opening size (~60% of the native canvas resolution)** so it fits comfortably inside FL Studio and standard 1080p/1440p DAW workspaces.

CRITICAL REQUIREMENT:
1. Do NOT rewrite or alter the internal coordinates of our controls! Instead, use the rock-solid JUCE `mainCanvas` + `juce::AffineTransform::scale()` pattern so all custom `LookAndFeel` drawing, fonts, knobs, LED readouts, and the MIDI keyboard scale proportionally as a crisp vector UI without any text clipping.
2. Maintain 100% compatibility with our Phase 4 codebase (`PluginEditor.h`, `PluginEditor.cpp`, `CasioSampleBankManager.h`, `PluginProcessor.h/.cpp`). Write complete, production-ready C++20 code with zero placeholders or `// TODO` comments.

### 1. Architecture: `mainCanvas` Wrapper + `AffineTransform` Scaling (`Source/PluginEditor.h` & `Source/PluginEditor.cpp`)
Right now, all child components and background panel drawing live directly on `CasioCTK1000AudioProcessorEditor` at its large base design dimensions (`kBaseWidth` $\times$ `kBaseHeight`, e.g., `1100 x 720` or whatever exact base width/height constants are currently used in `PluginEditor`). Refactor the Editor cleanly as follows:
* **Create an Inner `MainCanvasComponent` (or `juce::Component mainCanvas`):**
  * Move all existing child components (`addAndMakeVisible(...)` for all sliders, combo boxes, buttons, the live IXA oscilloscope, and `midiKeyboard`) and the custom background/panel `paint(juce::Graphics&)` code into a child component owned by the Editor (`mainCanvas`), OR keep them inside a dedicated canvas container class whose internal bounds are always locked to `(0, 0, kBaseWidth, kBaseHeight)`.
  * Inside `mainCanvas.resized()`, execute the exact `kBaseWidth` $\times$ `kBaseHeight` layout math that currently works perfectly.
* **Configure Aspect-Ratio-Locked Window Resizing on `CasioCTK1000AudioProcessorEditor`:**
  * Define the native unscaled constants `static constexpr int kBaseWidth = ...;` and `static constexpr int kBaseHeight = ...;` matching our current layout.
  * Set the **default initial scale factor** to `0.60f` (approx $1 / 1.70$ of the original size, e.g., `660 x 432` for an `1100 x 720` base canvas) so the plugin opens at an ergonomic, standard VST size in FL Studio out of the box.
  * Enable native corner resizing with locked aspect ratio:
    ```cpp
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(kBaseWidth) / static_cast<double>(kBaseHeight));
    setResizeLimits( static_cast<int>(kBaseWidth * 0.45f), static_cast<int>(kBaseHeight * 0.45f),
                     static_cast<int>(kBaseWidth * 1.35f), static_cast<int>(kBaseHeight * 1.35f) );
    ```
  * In `CasioCTK1000AudioProcessorEditor::resized()`, compute the current scale factor and apply `juce::AffineTransform::scale`:
    ```cpp
    const float scaleX = static_cast<float>(getWidth())  / static_cast<float>(kBaseWidth);
    const float scaleY = static_cast<float>(getHeight()) / static_cast<float>(kBaseHeight);
    const float scale  = std::min(scaleX, scaleY);
    mainCanvas.setTransform(juce::AffineTransform::scale(scale));
    mainCanvas.setBounds(0, 0, kBaseWidth, kBaseHeight);
    ```

### 2. Quick-Scale Header Dropdown (`50%` to `125%`) & Persistent Scale Memory
* **UI Scale Selector in Top Header Bar:**
  * Add a compact `juce::ComboBox uiScaleBox` (labeled `"UI:"` or showing `"60%"`) in the top header bar with preset scale options:
    * `50% Compact` (`0.50f`)
    * `60% Default (FL Studio)` (`0.60f`)
    * `70% Medium` (`0.70f`)
    * `80% Large` (`0.80f`)
    * `100% Native 1:1` (`1.00f`)
    * `125% HiDPI` (`1.25f`)
  * Selecting an item from `uiScaleBox` immediately calls `setSize(std::roundToInt(kBaseWidth * selectedScale), std::roundToInt(kBaseHeight * selectedScale))` and saves the new scale factor.
* **Persistent Window Size / Scale Memory (`settings.xml`):**
  * Save and load the user's last-used `uiScaleFactor` (defaulting to `0.60f` on first launch) in the same persistent `PropertiesFile` (`"CasioCTK1000_IXA/settings.xml"`) managed by `CasioSampleBankManager` / `PluginProcessor`.
  * Whenever the user drags the bottom-right corner resizer OR chooses a percentage from `uiScaleBox`, persist the updated scale factor so every new instance and DAW session remembers their exact preferred window size.

### Phase 5 Deliverable
1. Update `Source/PluginEditor.h` and `Source/PluginEditor.cpp` (and `Source/CasioSampleBankManager.h` or `Source/PluginProcessor.h/.cpp` if adding the `getSavedUIScale()` / `setSavedUIScale(float)` helper methods to `settings.xml`).
2. Package the **entire final project** (`CMakeLists.txt` at root + all files inside `Source/`) into a downloadable `.zip` archive using your file generation tool.