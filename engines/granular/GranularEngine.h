#pragma once

#include "engines/IAudioEngine.h"

#include "audio/Synth.h"
#include "control/PerformanceState.h"
#include "parameters/ParameterRegistry.h"
#include "util/LockFreeQueue.h"

#include <memory>

namespace rumor {

/** Wraps the existing UMBRAL / glow Synth::process path. */
class GranularEngine final : public IAudioEngine {
 public:
  GranularEngine();
  ~GranularEngine() override;

  EngineKind kind() const override { return EngineKind::Granular; }
  const char* name() const override { return "Granular Synth"; }

  void prepare(double sampleRate, int maxBlockSize) override;
  void process(float* left, float* right, int numFrames) override;
  void release() override;
  void reset() override;

  std::vector<EngineParameterInfo> parameters() const override;
  bool setParameter(std::string_view id, float value) override;
  bool getParameter(std::string_view id, float& valueOut) const override;

  glow::ParameterRegistry& registry() { return registry_; }
  glow::PerformanceState& performance() { return performance_; }
  glow::LockFreeQueue<glow::NoteEvent, 256>& noteQueue() { return noteQueue_; }
  glow::Synth& synth() { return synth_; }

 private:
  glow::ParameterRegistry registry_;
  glow::PerformanceState performance_;
  glow::LockFreeQueue<glow::NoteEvent, 256> noteQueue_;
  glow::Synth synth_;
  int maxGrains_{32};
};

}  // namespace rumor
