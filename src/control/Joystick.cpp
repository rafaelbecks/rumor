#include "control/Joystick.h"

#include "util/MidiMath.h"
#include "util/Scale.h"

#include <cmath>
#include <iostream>

namespace glow {

void Joystick::configure(const JoystickConfig& config) {
  config_ = config;
  if (config_.banks.empty()) {
    activeBank_ = 0;
  } else {
    activeBank_ = activeBank_ % static_cast<int>(config_.banks.size());
  }
}

std::string Joystick::activeBankName() const {
  if (config_.banks.empty()) return "(none)";
  return config_.banks[static_cast<size_t>(activeBank_)].name;
}

void Joystick::setRawX(float bipolar) {
  rawX_ = midi_math::clamp(bipolar, -1.0f, 1.0f);
}

void Joystick::setRawY(float bipolar) {
  rawY_ = midi_math::clamp(bipolar, -1.0f, 1.0f);
}

void Joystick::onButton(bool pressed) {
  if (pressed && !buttonDown_) {
    if (!config_.banks.empty()) {
      activeBank_ = (activeBank_ + 1) % static_cast<int>(config_.banks.size());
      if (verbose_) {
        const auto& bank = config_.banks[static_cast<size_t>(activeBank_)];
        std::cout << "[joy] bank " << activeBank_ << " — " << bank.name
                  << "  X→" << bank.x.parameter << "  Y→" << bank.y.parameter << "\n";
      }
    }
  }
  buttonDown_ = pressed;
}

void Joystick::applyAxis(float bipolar, const AxisMapping& axis, char axisLabel) {
  if (registry_ == nullptr || axis.parameter.empty()) return;
  const auto id = parameterIdFromName(axis.parameter);
  if (id == ParameterID::Count) return;

  float value;
  if (axis.min < 0.0f && axis.max > 0.0f) {
    const float t = bipolar;
    if (t >= 0.0f) {
      value = midi_math::mapNormalized(t, 0.0f, axis.max);
    } else {
      value = midi_math::mapNormalized(-t, 0.0f, -axis.min) * -1.0f;
    }
  } else {
    const float n = (bipolar + 1.0f) * 0.5f;
    const float shaped = midi_math::applyCurve(n, axis.curve.c_str());
    value = midi_math::mapNormalized(shaped, axis.min, axis.max);
  }

  if (id == ParameterID::PitchOffset && performance_ != nullptr) {
    const ScaleId scale = performance_->scale();
    if (scale != ScaleId::Off) {
      value = quantizeCentsToScale(value, performance_->root(), scale);
    }
  }
  registry_->setTarget(id, value);

  if (verbose_) {
    float& last = (axisLabel == 'x') ? lastLoggedX_ : lastLoggedY_;
    const float span = std::abs(axis.max - axis.min);
    const float threshold = span > 0.0f ? span * 0.01f : 0.01f;
    if (std::abs(value - last) >= threshold) {
      last = value;
      std::cout << "[joy] " << axisLabel << " → " << axis.parameter << " = " << value << "\n";
    }
  }
}

void Joystick::update() {
  float x = midi_math::invertBipolar(rawX_, config_.invertX);
  float y = midi_math::invertBipolar(rawY_, config_.invertY);
  x = midi_math::applyDeadzone(x, config_.deadzone);
  y = midi_math::applyDeadzone(y, config_.deadzone);

  const float s = midi_math::clamp(config_.smoothing, 0.0f, 1.0f);
  smoothedX_ += (x - smoothedX_) * s;
  smoothedY_ += (y - smoothedY_) * s;

  if (config_.banks.empty() || registry_ == nullptr) return;
  const auto& bank = config_.banks[static_cast<size_t>(activeBank_)];
  applyAxis(smoothedX_, bank.x, 'x');
  applyAxis(smoothedY_, bank.y, 'y');
}

}  // namespace glow
