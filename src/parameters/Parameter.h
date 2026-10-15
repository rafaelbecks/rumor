#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace glow {

enum class ParameterID : uint16_t {
  PitchOffset = 0,
  FilterCutoff,
  FilterResonance,
  ChordSpread,
  GrainSize,
  GrainDensity,
  GrainPosition,
  GrainSpray,
  GrainPitch,
  GrainMix,
  FolderDrive,
  FolderSymmetry,
  FolderMix,
  Waveform,       // 0=sine, 1=saw, 2=square, 3=triangle
  Attack,
  Release,
  Amplitude,  // output level, 0 = silent, 1 = full
  SineTimbre, // sine phase-mod depth (GRM Gen-style timbre), 0..1
  Count
};

struct ParameterDesc {
  ParameterID id;
  const char* name;
  float minimum;
  float maximum;
  float defaultValue;
  float smoothing;  // 0..1 one-pole coefficient toward target (per sample-ish block)
};

class Parameter {
 public:
  Parameter() = default;

  void configure(ParameterID id, std::string name, float minV, float maxV, float def,
                 float smoothing);

  ParameterID id() const { return id_; }
  const std::string& name() const { return name_; }
  float minimum() const { return min_; }
  float maximum() const { return max_; }
  float defaultValue() const { return default_; }
  float smoothing() const { return smoothing_; }

  /** Control thread: set target value (clamped). */
  void setTarget(float value);

  /** Audio thread: pull smoothed value. */
  float tickSmoothed();

  /** Instant read of current smoothed value (audio-safe atomic load). */
  float value() const;

  /** Instant read of target. */
  float target() const;

 private:
  ParameterID id_{ParameterID::PitchOffset};
  std::string name_;
  float min_{0.0f};
  float max_{1.0f};
  float default_{0.0f};
  float smoothing_{0.05f};
  std::atomic<float> target_{0.0f};
  std::atomic<float> current_{0.0f};
};

ParameterID parameterIdFromName(std::string_view name);
const char* parameterName(ParameterID id);
const ParameterDesc& parameterDesc(ParameterID id);

}  // namespace glow
