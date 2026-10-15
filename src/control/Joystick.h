#pragma once

#include "config/ConfigLoader.h"
#include "control/PerformanceState.h"
#include "parameters/ParameterRegistry.h"
#include "util/Scale.h"

#include <string>

namespace glow {

/**
 * Joystick abstraction — no DSP knowledge.
 * Banks are defined entirely by JSON via JoystickConfig.
 */
class Joystick {
 public:
  void configure(const JoystickConfig& config);
  void setRegistry(ParameterRegistry* registry) { registry_ = registry; }
  void setPerformance(PerformanceState* performance) { performance_ = performance; }
  void setVerbose(bool verbose) { verbose_ = verbose; }

  /** Raw bipolar inputs in [-1, 1] (typically from MIDI CC mapping). */
  void setRawX(float bipolar);
  void setRawY(float bipolar);

  /** Button edge: press advances bank. */
  void onButton(bool pressed);

  /** Process smoothing + apply active bank mapping into ParameterRegistry. */
  void update();

  int activeBank() const { return activeBank_; }
  size_t bankCount() const { return config_.banks.size(); }
  float processedX() const { return smoothedX_; }
  float processedY() const { return smoothedY_; }
  std::string activeBankName() const;

 private:
  void applyAxis(float bipolar, const AxisMapping& axis, char axisLabel);

  JoystickConfig config_{};
  ParameterRegistry* registry_{nullptr};
  PerformanceState* performance_{nullptr};
  float rawX_{0.0f};
  float rawY_{0.0f};
  float smoothedX_{0.0f};
  float smoothedY_{0.0f};
  float lastLoggedX_{1e9f};
  float lastLoggedY_{1e9f};
  int activeBank_{0};
  bool buttonDown_{false};
  bool verbose_{false};
};

}  // namespace glow
