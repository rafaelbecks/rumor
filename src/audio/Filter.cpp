#include "audio/Filter.h"

#include "util/MidiMath.h"

#include <algorithm>
#include <cmath>

namespace glow {
namespace {
constexpr float kPi = 3.14159265358979323846f;

inline float softClip(float x) {
  // Fast tanh-ish; keeps Moog grit without std::tanh cost every stage
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
}  // namespace

void Filter::setSampleRate(float sampleRate) {
  sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
  updateCoeffs();
}

void Filter::setCutoff(float hz) {
  // Musical Moog range — deep enough to gut a saw, open enough to be bright
  const float maxHz = std::min(18000.0f, sampleRate_ * 0.45f);
  cutoff_ = midi_math::clamp(hz, 30.0f, maxHz);
  updateCoeffs();
}

void Filter::setResonance(float amount01) {
  resonance_ = midi_math::clamp(amount01, 0.0f, 1.0f);
}

void Filter::updateCoeffs() {
  // One-pole coefficient from cutoff (Huovilainen / simplified ladder)
  const float fc = cutoff_ / sampleRate_;
  g_ = 1.0f - std::exp(-2.0f * kPi * fc);
  g_ = midi_math::clamp(g_, 0.0f, 0.9f);
}

void Filter::reset() {
  for (int i = 0; i < 4; ++i) {
    stage_[i] = 0.0f;
    delay_[i] = 0.0f;
  }
}

float Filter::process(float input) {
  if (!std::isfinite(input)) {
    reset();
    return 0.0f;
  }
  for (float s : stage_) {
    if (!std::isfinite(s)) {
      reset();
      break;
    }
  }

  // Resonance feedback amount (0 → none, ~4 → self-oscillation edge)
  const float k = resonance_ * 4.0f;

  // Feedback from last stage (comp for passband loss with resonance)
  float x = input - k * stage_[3];
  x = softClip(x * (1.0f + 0.3f * resonance_));

  // Four cascaded one-pole lowpass stages → 24 dB/oct
  const float g = g_;
  for (int i = 0; i < 4; ++i) {
    // Slight per-stage nonlinearity (ladder transistors)
    const float in = softClip(x);
    stage_[i] = delay_[i] + g * (in - delay_[i]);
    delay_[i] = stage_[i];
    x = stage_[i];
  }

  float out = stage_[3];
  if (!std::isfinite(out)) {
    reset();
    return 0.0f;
  }
  return out;
}

}  // namespace glow
