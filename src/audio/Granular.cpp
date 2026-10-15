#include "audio/Granular.h"

#include "util/MidiMath.h"

#include <cmath>

namespace glow {
namespace {

float nextRand(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

}  // namespace

void Granular::prepare(float sampleRate, int maxGrains) {
  sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
  const size_t buf = static_cast<size_t>(sampleRate_ * static_cast<float>(kBufferSeconds));
  buffer_.assign(buf > 64 ? buf : 64, 0.0f);
  writePos_ = 0;
  const int n = maxGrains > 0 ? maxGrains : static_cast<int>(kDefaultMaxGrains);
  grains_.assign(static_cast<size_t>(n), Grain{});
  spawnAccum_ = 0.0f;
}

void Granular::setSize(float seconds) {
  size_ = midi_math::clamp(seconds, 0.005f, 1.0f);
}

void Granular::setDensity(float grainsPerSecond) {
  density_ = midi_math::clamp(grainsPerSecond, 0.1f, 200.0f);
}

void Granular::setPosition(float pos01) {
  position_ = midi_math::clamp(pos01, 0.0f, 1.0f);
}

void Granular::setSpray(float spray01) {
  spray_ = midi_math::clamp(spray01, 0.0f, 1.0f);
}

void Granular::setPitch(float semitones) {
  pitchSemis_ = midi_math::clamp(semitones, -24.0f, 24.0f);
}

void Granular::setMix(float mix01) {
  mix_ = midi_math::clamp(mix01, 0.0f, 1.0f);
}

void Granular::reset() {
  std::fill(buffer_.begin(), buffer_.end(), 0.0f);
  writePos_ = 0;
  for (auto& g : grains_) g.active = false;
  spawnAccum_ = 0.0f;
}

void Granular::spawnGrain() {
  for (auto& g : grains_) {
    if (g.active) continue;
    const float sprayOffset = (nextRand(rng_) * 2.0f - 1.0f) * spray_ * 0.25f;
    float pos = midi_math::clamp(position_ + sprayOffset, 0.0f, 1.0f);
    const float bufLen = static_cast<float>(buffer_.size());
    g.readPos = pos * (bufLen - 1.0f);
    g.rate = std::pow(2.0f, pitchSemis_ / 12.0f);
    g.duration = size_ * sampleRate_;
    g.age = 0.0f;
    g.active = true;
    return;
  }
}

float Granular::process(float input) {
  if (buffer_.empty()) return input;

  buffer_[writePos_] = input;
  writePos_ = (writePos_ + 1) % buffer_.size();

  spawnAccum_ += density_ / sampleRate_;
  while (spawnAccum_ >= 1.0f) {
    spawnAccum_ -= 1.0f;
    spawnGrain();
  }

  float wet = 0.0f;
  int active = 0;
  for (auto& g : grains_) {
    if (!g.active) continue;
    const size_t i0 = static_cast<size_t>(g.readPos) % buffer_.size();
    const size_t i1 = (i0 + 1) % buffer_.size();
    const float frac = g.readPos - std::floor(g.readPos);
    const float s = buffer_[i0] * (1.0f - frac) + buffer_[i1] * frac;
    const float env = std::sin(3.14159265f * (g.age / g.duration));  // Hann-ish
    wet += s * env;
    ++active;

    g.readPos += g.rate;
    if (g.readPos >= static_cast<float>(buffer_.size())) {
      g.readPos -= static_cast<float>(buffer_.size());
    }
    g.age += 1.0f;
    if (g.age >= g.duration) g.active = false;
  }

  if (active > 0) wet /= static_cast<float>(active);
  return input * (1.0f - mix_) + wet * mix_;
}

}  // namespace glow
