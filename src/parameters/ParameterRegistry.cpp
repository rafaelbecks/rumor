#include "parameters/ParameterRegistry.h"

namespace glow {

ParameterRegistry::ParameterRegistry() {
  for (size_t i = 0; i < params_.size(); ++i) {
    const auto id = static_cast<ParameterID>(i);
    const auto& d = parameterDesc(id);
    params_[i].configure(id, d.name, d.minimum, d.maximum, d.defaultValue, d.smoothing);
  }
}

Parameter& ParameterRegistry::get(ParameterID id) {
  return params_[static_cast<size_t>(id)];
}

const Parameter& ParameterRegistry::get(ParameterID id) const {
  return params_[static_cast<size_t>(id)];
}

std::optional<ParameterID> ParameterRegistry::findByName(std::string_view name) const {
  const auto id = parameterIdFromName(name);
  if (id == ParameterID::Count) return std::nullopt;
  return id;
}

void ParameterRegistry::tickAll() {
  for (auto& p : params_) {
    p.tickSmoothed();
  }
}

float ParameterRegistry::value(ParameterID id) const {
  return get(id).value();
}

void ParameterRegistry::setTarget(ParameterID id, float value) {
  get(id).setTarget(value);
}

}  // namespace glow
