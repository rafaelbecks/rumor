#include "engines/granular/GranularEngine.h"

#include "parameters/Parameter.h"

namespace rumor {

GranularEngine::GranularEngine() {
  synth_.setParameterRegistry(&registry_);
  synth_.setPerformance(&performance_);
  synth_.setNoteQueue(&noteQueue_);
}

GranularEngine::~GranularEngine() { release(); }

void GranularEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
  synth_.prepare(static_cast<float>(sampleRate), maxGrains_);
}

void GranularEngine::process(float* left, float* right, int numFrames) {
  synth_.process(left, right, numFrames);
}

void GranularEngine::release() { synth_.drainNotes(); }

void GranularEngine::reset() {
  synth_.drainNotes();
  performance_.clearSerialPitch();
}

std::vector<EngineParameterInfo> GranularEngine::parameters() const {
  std::vector<EngineParameterInfo> out;
  out.reserve(glow::ParameterRegistry::count());
  for (size_t i = 0; i < glow::ParameterRegistry::count(); ++i) {
    const auto id = static_cast<glow::ParameterID>(i);
    const auto& d = glow::parameterDesc(id);
    out.push_back({d.name, d.name, d.minimum, d.maximum, d.defaultValue, true});
  }
  return out;
}

bool GranularEngine::setParameter(std::string_view id, float value) {
  for (size_t i = 0; i < glow::ParameterRegistry::count(); ++i) {
    const auto pid = static_cast<glow::ParameterID>(i);
    if (glow::parameterName(pid) != id) continue;
    registry_.setTarget(pid, value);
    return true;
  }
  return false;
}

bool GranularEngine::getParameter(std::string_view id, float& valueOut) const {
  for (size_t i = 0; i < glow::ParameterRegistry::count(); ++i) {
    const auto pid = static_cast<glow::ParameterID>(i);
    if (glow::parameterName(pid) != id) continue;
    valueOut = registry_.get(pid).target();
    return true;
  }
  return false;
}

}  // namespace rumor
