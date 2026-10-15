#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace glow {

/**
 * Lightweight granular stage. Preallocates grain voices + circular buffer.
 * Phase 1: mix defaults to 0 (dry). Architecture ready for Phase 4 tuning.
 */
class Granular {
 public:
  static constexpr size_t kDefaultMaxGrains = 32;
  static constexpr size_t kBufferSeconds = 2;

  void prepare(float sampleRate, int maxGrains);
  void setSize(float seconds);
  void setDensity(float grainsPerSecond);
  void setPosition(float pos01);
  void setSpray(float spray01);
  void setPitch(float semitones);
  void setMix(float mix01);
  float process(float input);
  void reset();

 private:
  struct Grain {
    bool active{false};
    float readPos{0.0f};
    float rate{1.0f};
    float age{0.0f};
    float duration{0.0f};
    float pan{0.5f};
  };

  void spawnGrain();

  float sampleRate_{48000.0f};
  float size_{0.08f};
  float density_{8.0f};
  float position_{0.5f};
  float spray_{0.1f};
  float pitchSemis_{0.0f};
  float mix_{0.0f};

  std::vector<float> buffer_;
  size_t writePos_{0};
  std::vector<Grain> grains_;
  float spawnAccum_{0.0f};
  uint32_t rng_{0xA341316Cu};
};

}  // namespace glow
