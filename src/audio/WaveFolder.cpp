#include "audio/WaveFolder.h"

#include "util/MidiMath.h"

#include <cmath>

namespace glow {
namespace {

float foldOnce(float x) {
  // Reflect into [-1, 1]
  constexpr int kMaxIters = 8;
  for (int i = 0; i < kMaxIters; ++i) {
    if (x > 1.0f) {
      x = 2.0f - x;
    } else if (x < -1.0f) {
      x = -2.0f - x;
    } else {
      break;
    }
  }
  return midi_math::clamp(x, -1.0f, 1.0f);
}

}  // namespace

void WaveFolder::setDrive(float drive01) {
  drive_ = midi_math::clamp(drive01, 0.0f, 1.0f);
}

void WaveFolder::setSymmetry(float bipolar) {
  symmetry_ = midi_math::clamp(bipolar, -1.0f, 1.0f);
}

void WaveFolder::setMix(float mix01) {
  mix_ = midi_math::clamp(mix01, 0.0f, 1.0f);
}

float WaveFolder::process(float input) {
  // Gain 1 → ~9: quiet at 0 drive, aggressive folds when pushed
  const float gain = 1.0f + drive_ * 8.0f;
  const float shaped = foldOnce(input * gain + symmetry_ * 0.85f);
  // Soft compensate so louder drive doesn't always explode level
  const float wet = shaped / std::sqrt(gain);
  return input * (1.0f - mix_) + wet * mix_;
}

}  // namespace glow
