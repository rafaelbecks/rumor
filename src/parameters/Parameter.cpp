#include "parameters/Parameter.h"

#include "util/MidiMath.h"

#include <array>
#include <cstring>

namespace glow {
namespace {

constexpr std::array<ParameterDesc, static_cast<size_t>(ParameterID::Count)> kDescs{{
  {ParameterID::PitchOffset, "pitch.offset", -1200.0f, 1200.0f, 0.0f, 0.08f},
  {ParameterID::FilterCutoff, "filter.cutoff", 40.0f, 16000.0f, 6000.0f, 0.04f},
  {ParameterID::FilterResonance, "filter.resonance", 0.0f, 1.0f, 0.45f, 0.05f},
  {ParameterID::ChordSpread, "chord.spread", 0.0f, 1.0f, 0.0f, 0.05f},
  {ParameterID::GrainSize, "granular.size", 0.01f, 0.5f, 0.08f, 0.05f},
  {ParameterID::GrainDensity, "granular.density", 1.0f, 100.0f, 8.0f, 0.05f},
  {ParameterID::GrainPosition, "granular.position", 0.0f, 1.0f, 0.5f, 0.05f},
  {ParameterID::GrainSpray, "granular.spray", 0.0f, 1.0f, 0.1f, 0.05f},
  {ParameterID::GrainPitch, "granular.pitch", -12.0f, 12.0f, 0.0f, 0.05f},
  {ParameterID::GrainMix, "granular.mix", 0.0f, 1.0f, 0.0f, 0.05f},
  {ParameterID::FolderDrive, "folder.drive", 0.0f, 1.0f, 0.0f, 0.05f},
  {ParameterID::FolderSymmetry, "folder.symmetry", -1.0f, 1.0f, 0.0f, 0.05f},
  {ParameterID::FolderMix, "folder.mix", 0.0f, 1.0f, 1.0f, 0.05f},
  {ParameterID::Waveform, "osc.waveform", 0.0f, 3.0f, 1.0f, 0.08f},
  {ParameterID::Attack, "env.attack", 0.001f, 2.0f, 0.01f, 0.1f},
  {ParameterID::Release, "env.release", 0.001f, 4.0f, 0.2f, 0.1f},
  {ParameterID::Amplitude, "amplitude", 0.0f, 1.0f, 1.0f, 0.05f},
  {ParameterID::SineTimbre, "osc.timbre", 0.0f, 1.0f, 0.0f, 0.05f},
}};

}  // namespace

void Parameter::configure(ParameterID id, std::string name, float minV, float maxV, float def,
                          float smoothing) {
  id_ = id;
  name_ = std::move(name);
  min_ = minV;
  max_ = maxV;
  default_ = def;
  smoothing_ = smoothing;
  target_.store(def, std::memory_order_relaxed);
  current_.store(def, std::memory_order_relaxed);
}

void Parameter::setTarget(float value) {
  target_.store(midi_math::clamp(value, min_, max_), std::memory_order_relaxed);
}

float Parameter::tickSmoothed() {
  const float target = target_.load(std::memory_order_relaxed);
  float current = current_.load(std::memory_order_relaxed);
  const float a = midi_math::clamp(smoothing_, 0.0f, 1.0f);
  current += (target - current) * a;
  current_.store(current, std::memory_order_relaxed);
  return current;
}

float Parameter::value() const {
  return current_.load(std::memory_order_relaxed);
}

float Parameter::target() const {
  return target_.load(std::memory_order_relaxed);
}

ParameterID parameterIdFromName(std::string_view name) {
  for (const auto& d : kDescs) {
    if (name == d.name) return d.id;
  }
  return ParameterID::Count;
}

const char* parameterName(ParameterID id) {
  const auto i = static_cast<size_t>(id);
  if (i >= kDescs.size()) return "unknown";
  return kDescs[i].name;
}

const ParameterDesc& parameterDesc(ParameterID id) {
  const auto i = static_cast<size_t>(id);
  return kDescs[i < kDescs.size() ? i : 0];
}

}  // namespace glow
