# Local patches applied for RUMOR integration

Pinned upstream: see `PINNED_COMMIT.txt` (essej/paulxstretch).

1. Replaced `#include "../JuceLibraryCode/JuceHeader.h"` with `#include <JuceHeader.h>`
   and provide a JUCE 8 module shim under `include/JuceHeader.h`.
2. Omitted `Player.*` and `PaulStretchControl.*` from the DSP library (app-level /
   missing `Mutex.h` dependency); RUMOR uses `StretchAudioSource` directly.
3. Build with `PS_USE_PFFFT=1` (bundled pffft) — no FFTW required.
4. Algorithm sources are otherwise unmodified copies of upstream DSP files.

5. Include `juce_cryptography` in JuceHeader shim (MD5 used by jcdp_envelope.h).
6. JUCE 8: `getArrayOfWritePointers()` returns `float* const*` — use `auto*`
   in AInputS.h and StretchSource.cpp.
7. Null-check `m_free_filter_envelope` before random-rate update in
   `StretchSource::getNextAudioBlock` (RUMOR may start before envelope is set).
8. Null-check free-filter envelope before `spectrum_do_free_filter` in
   `ProcessedStretch`.
9. Vendored `ps3_BufferingAudioSource` (upstream prebuffer) for realtime PX.
10. `AInputS::openAudioFile`: set `info.nsamples` before computing
    `m_currentsample` from the active range.
11. Windows/MSVC: define `WINDOWS`, `WIN32`, `_USE_MATH_DEFINES`, and `NOMINMAX`
    so `globals.cpp` skips `unistd.h` and `M_PI` is available in FreeEdit /
    BinauralBeats.
