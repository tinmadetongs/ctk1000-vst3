Phase 6.5 is complete and the SA/PSS Fake Sustain engine works great. Now let's proceed to **Phase 7**, where we implement a **Real-Time Dual-Readhead Loop Crossfade & De-Click Engine** for **PCM Element A** and **PCM Element B** to eliminate loop-boundary clicks in the extracted SA-51 `.wav` samples (aligning with the loop crossfading techniques in Casio's US Patent 5,319,151).

CRITICAL REQUIREMENT:
1. Maintain 100% compatibility with our Phase 6.5 codebase (`IXAOscillator.h`, `CasioSampleBankManager.h`, `IXAHybridVoice.h`, `CTK1000Effects.h`, `PluginProcessor.h/.cpp`, `PluginEditor.h/.cpp`), including our `mainCanvas` + `AffineTransform` rescalable GUI.
2. Write complete, production-ready C++20 code with zero placeholders or `// TODO` comments.

### 1. DSP Specification: Real-Time Loop Boundary Crossfading (`Source/IXAHybridVoice.h`)
Upgrade the PCM sample playback engine inside `IXAHybridVoice` for both **PCM Element A** and **PCM Element B** so each slot supports continuous **Loop Crossfade Amount** (`PCM_A_XFADE`, `PCM_B_XFADE` $\in [0.0, 0.50]$, representing $0\%$ to $50\%$ of the active loop length $L_{\text{loop}} = \text{loopEnd} - \text{loopStart}$) and a **Crossfade Curve** toggle (`Linear` vs `Equal-Power Cosine`):

* **Crossfade Window Calculation:**
  Given effective `loopStart` and `loopEnd` (with $L_{\text{loop}} = \max(1, \text{loopEnd} - \text{loopStart})$) and normalized crossfade parameter $x_{\text{fade}} \in [0.0, 0.50]$:
  * If $x_{\text{fade}} \leq 0.001$ or `loopMode == OneShot`, bypass crossfade math for zero extra CPU overhead.
  * Otherwise, define the crossfade window length in samples as:
    $$W_{\text{xfade}} = \text{clamp}\left(x_{\text{fade}} \cdot L_{\text{loop}},\, 4.0,\, 0.5 \cdot L_{\text{loop}}\right)$$

* **Case A — Forward Loop Mode (`Forward`):**
  When the current read position $p \in [\text{loopEnd} - W_{\text{xfade}},\, \text{loopEnd})$:
  1. Compute normalized crossfade progress $\alpha = \frac{p - (\text{loopEnd} - W_{\text{xfade}})}{W_{\text{xfade}}} \in [0.0, 1.0)$.
  2. Read the **outgoing sample** $s_{\text{out}}$ at position $p$.
  3. Compute the **incoming shadow readhead position** $p_{\text{in}}$:
     * Ideally, read from the pre-loop lead-in before `loopStart`: $p_{\text{lead}} = \text{loopStart} - W_{\text{xfade}} + (p - (\text{loopEnd} - W_{\text{xfade}}))$.
     * Safety clamp for short attack samples where $\text{loopStart} < W_{\text{xfade}}$: if $p_{\text{lead}} < 0$, wrap smoothly inside the loop region or clamp/mirror safely so `p_in` is always strictly within $[0, \text{numFrames} - 1]$ and never reads out-of-bounds memory!
  4. Blend $s_{\text{out}}$ and $s_{\text{in}}$ using either **Linear Crossfade** ($(1 - \alpha) s_{\text{out}} + \alpha s_{\text{in}}$) or **Equal-Power Crossfade** ($\cos(\frac{\pi}{2}\alpha) s_{\text{out}} + \sin(\frac{\pi}{2}\alpha) s_{\text{in}}$).

* **Case B — Bidirectional Loop Mode (`Ping-Pong`, US Patent 5,319,151):**
  In Ping-Pong mode, phase/slope discontinuities occur at the turnaround vertices (`loopEnd` when moving forward, `loopStart` when moving backward).
  * When approaching `loopEnd` moving forward ($p \in [\text{loopEnd} - W_{\text{xfade}}, \text{loopEnd}]$) or approaching `loopStart` moving backward ($p \in [\text{loopStart}, \text{loopStart} + W_{\text{xfade}}]$), apply a smooth turnaround window or crossfade between the forward readhead and the reflected backward readhead across $W_{\text{xfade}}$ (plus a subtle zero-crossing snap when initializing default loop bounds if `LOOP_OVR` is off) so turnaround reversals are 100% click-free.

* **Automatic Zero-Crossing Snap Helper (Optional Bonus in `CasioSampleBankManager.h`):**
  When loading a `.wav` sample into memory, nudge the default `loopStart` and `loopEnd` indices by up to $\pm 64$ samples to the nearest positive-slope zero crossing so even with `XFADE = 0.0`, default loops start as cleanly as possible.

### 2. APVTS Parameter Additions (`Source/PluginProcessor.h` & `Source/PluginProcessor.cpp`)
Add per-slot Loop Crossfade parameters to `createParameterLayout()` in `PluginProcessor.cpp` and pass them to all 16 voices inside `processBlock()`:
* `"PCM_A_XFADE"`: `AudioParameterFloat` (`0.0f` to `0.50f`, step `0.005f`, default `0.05f` [5% crossfade by default so SA-51 loops are immediately de-clicked out of the box!])
* `"PCM_B_XFADE"`: `AudioParameterFloat` (`0.0f` to `0.50f`, step `0.005f`, default `0.05f`)
* `"PCM_XFADE_POWER"`: `AudioParameterBool` (`"Equal-Power Loop XFade"`, default `true` — switches between Equal-Power Cosine/Sine crossfade and Linear phase-locked crossfade).

### 3. GUI Integration inside `mainCanvas` (`Source/PluginEditor.h` & `Source/PluginEditor.cpp`)
Integrate the Loop Crossfade controls directly into the **PCM Element A** and **PCM Element B** panels on the left side of `mainCanvas`:
* In **PCM Element A**, add a `LP XFADE` rotary knob (`PCM_A_XFADE`) right next to `LP START` and `LP END`.
* In **PCM Element B**, add a `LP XFADE` rotary knob (`PCM_B_XFADE`) right next to `LP START` and `LP END`.
* Add a compact `EQ-PWR` toggle button (`PCM_XFADE_POWER`) in the PCM Element header or next to `LOOP OVR` so the user can toggle between Linear (best for phase-aligned single-cycle waves) and Equal-Power (best for ensemble/pad multi-cycle samples).
* Connect all 3 controls to `processorRef.apvts` via `SliderAttachment` and `ButtonAttachment`.

### Phase 7 Deliverable
1. Update `Source/CasioSampleBankManager.h`, `Source/IXAHybridVoice.h`, `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/PluginEditor.h`, and `Source/PluginEditor.cpp` with the complete Loop Crossfading & De-Clicking Engine.
2. Output an updated complete project `.zip` archive containing `CMakeLists.txt` and all files in `Source/`.