#pragma once

#include "parameters/Parameter.h"

#include <array>
#include <optional>

namespace glow {

class ParameterRegistry {
 public:
  ParameterRegistry();

  Parameter& get(ParameterID id);
  const Parameter& get(ParameterID id) const;

  std::optional<ParameterID> findByName(std::string_view name) const;

  /** Audio thread: advance smoothing for all params once per block. */
  void tickAll();

  float value(ParameterID id) const;
  void setTarget(ParameterID id, float value);

  static constexpr size_t count() {
    return static_cast<size_t>(ParameterID::Count);
  }

 private:
  std::array<Parameter, static_cast<size_t>(ParameterID::Count)> params_;
};

}  // namespace glow
