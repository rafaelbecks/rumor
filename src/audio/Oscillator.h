#pragma once

#include <cmath>
#include <string_view>

namespace glow {

enum class Waveform {
  Sine = 0,
  Saw = 1,
  Square = 2,
  Triangle = 3
};

inline constexpr int kWaveformCount = 4;

/** Map parameter float (0..N-1) to nearest waveform (UI / discrete). */
inline Waveform waveformFromParam(float value) {
  int i = static_cast<int>(value + 0.5f);
  if (i < 0) i = 0;
  if (i >= kWaveformCount) i = kWaveformCount - 1;
  return static_cast<Waveform>(i);
}

inline float waveformToParam(Waveform w) {
  return static_cast<float>(static_cast<int>(w));
}

inline const char* waveformName(Waveform w) {
  switch (w) {
    case Waveform::Sine: return "sine";
    case Waveform::Saw: return "saw";
    case Waveform::Square: return "square";
    case Waveform::Triangle: return "triangle";
  }
  return "saw";
}

/** Parse "sine"|"sin"|"saw"|"square"|"sqr"|"triangle"|"tri". Returns false if unknown. */
inline bool waveformFromName(std::string_view name, Waveform& out) {
  if (name == "sine" || name == "sin") {
    out = Waveform::Sine;
    return true;
  }
  if (name == "saw" || name == "sawtooth") {
    out = Waveform::Saw;
    return true;
  }
  if (name == "square" || name == "sqr") {
    out = Waveform::Square;
    return true;
  }
  if (name == "triangle" || name == "tri") {
    out = Waveform::Triangle;
    return true;
  }
  return false;
}

/** Wrap phase into [0, 1). */
inline float wrapPhase01(float phase) {
  phase -= std::floor(phase);
  return phase;
}

/**
 * Render one cycle sample for a discrete waveform at phase in [0, 1).
 * `timbre` (0..1) applies self phase-mod (GRM Gen-style) to every shape:
 * phase' = phase + (k/2π)·sin(2π·phase), with k up to ~17.5.
 */
inline float waveformSample(Waveform waveform, float phase, float timbre = 0.0f) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  constexpr float kMaxIndex = 17.5f;  // ~5× prior sine-only depth
  const float index = timbre * kMaxIndex;
  const float phi = phase * kTwoPi;
  const float modPhase = wrapPhase01(phase + (index / kTwoPi) * std::sin(phi));

  switch (waveform) {
    case Waveform::Sine:
      return std::sin(phi + index * std::sin(phi));
    case Waveform::Saw:
      return 2.0f * modPhase - 1.0f;
    case Waveform::Square:
      return modPhase < 0.5f ? 1.0f : -1.0f;
    case Waveform::Triangle: {
      float sample = 4.0f * std::fabs(modPhase - 0.5f) - 1.0f;
      return -sample;
    }
  }
  return 2.0f * modPhase - 1.0f;
}

/** Morph between adjacent waveforms at phase in [0, 1). */
inline float morphWaveformSample(float waveformPos, float phase, float sineTimbre = 0.0f) {
  float pos = waveformPos;
  if (pos < 0.0f) pos = 0.0f;
  if (pos > static_cast<float>(kWaveformCount - 1)) pos = static_cast<float>(kWaveformCount - 1);
  const int i0 = static_cast<int>(pos);
  const int i1 = i0 < (kWaveformCount - 1) ? i0 + 1 : i0;
  const float t = pos - static_cast<float>(i0);
  const float a = waveformSample(static_cast<Waveform>(i0), phase, sineTimbre);
  const float b = waveformSample(static_cast<Waveform>(i1), phase, sineTimbre);
  return a + (b - a) * t;
}

/**
 * Allocation-free oscillator. Fractional waveformPosition morphs between adjacent waves
 * (0=sine … 3=triangle).
 */
class Oscillator {
 public:
  void setSampleRate(float sampleRate);
  void setFrequency(float frequencyHz);
  void setWaveform(Waveform waveform);
  /** Continuous morph position in [0, kWaveformCount-1]. */
  void setWaveformPosition(float position);
  /** Phase-mod depth 0..1 (all waveforms / morph). */
  void setSineTimbre(float timbre);
  float process();
  void reset();

 private:
  float sampleRate_{48000.0f};
  float frequency_{440.0f};
  float phase_{0.0f};
  float waveformPos_{1.0f};  // default saw
  float sineTimbre_{0.0f};
};

}  // namespace glow
