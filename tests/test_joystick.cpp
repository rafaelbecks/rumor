#include <catch_amalgamated.hpp>

#include "control/Joystick.h"
#include "control/PerformanceState.h"
#include "parameters/ParameterRegistry.h"
#include "util/MidiMath.h"
#include "util/Scale.h"

TEST_CASE("Joystick deadzone", "[joystick]") {
  REQUIRE(glow::midi_math::applyDeadzone(0.05f, 0.08f) == Catch::Approx(0.0f));
  REQUIRE(glow::midi_math::applyDeadzone(-0.05f, 0.08f) == Catch::Approx(0.0f));
  REQUIRE(glow::midi_math::applyDeadzone(1.0f, 0.08f) == Catch::Approx(1.0f));
  REQUIRE(glow::midi_math::applyDeadzone(0.0f, 0.08f) == Catch::Approx(0.0f));
}

TEST_CASE("Joystick inversion", "[joystick]") {
  REQUIRE(glow::midi_math::invertBipolar(0.5f, true) == Catch::Approx(-0.5f));
  REQUIRE(glow::midi_math::invertBipolar(0.5f, false) == Catch::Approx(0.5f));
}

TEST_CASE("Bank switching", "[joystick]") {
  glow::ParameterRegistry reg;
  glow::JoystickConfig cfg;
  cfg.deadzone = 0.0f;
  cfg.smoothing = 1.0f;
  cfg.buttonCC = 26;
  cfg.banks = {
      {"synth", {"pitch.offset", -1200, 1200, "linear"}, {"filter.cutoff", 80, 12000, "linear"}},
      {"granular", {"granular.density", 1, 100, "linear"}, {"granular.pitch", -12, 12, "linear"}},
  };

  glow::Joystick joy;
  joy.configure(cfg);
  joy.setRegistry(&reg);
  REQUIRE(joy.activeBank() == 0);

  joy.onButton(true);
  joy.onButton(false);
  REQUIRE(joy.activeBank() == 1);

  joy.onButton(true);
  joy.onButton(false);
  REQUIRE(joy.activeBank() == 0);

  joy.onButton(true);
  joy.onButton(false);
  joy.setRawX(1.0f);
  joy.setRawY(-1.0f);
  joy.update();
  REQUIRE(reg.get(glow::ParameterID::GrainDensity).target() == Catch::Approx(100.0f));
  REQUIRE(reg.get(glow::ParameterID::GrainPitch).target() == Catch::Approx(-12.0f));
}

TEST_CASE("Scale name parsing", "[scale]") {
  glow::ScaleId id = glow::ScaleId::Off;
  REQUIRE(glow::scaleFromName("major", id));
  REQUIRE(id == glow::ScaleId::Major);
  REQUIRE(glow::scaleFromName("mixo", id));
  REQUIRE(id == glow::ScaleId::Mixolydian);
  REQUIRE(glow::scaleFromName("mixyolidian", id));
  REQUIRE(id == glow::ScaleId::Mixolydian);
  REQUIRE(glow::scaleFromName("pentatonic", id));
  REQUIRE(id == glow::ScaleId::Pentatonic);
  REQUIRE(glow::scaleFromName("minor", id));
  REQUIRE(id == glow::ScaleId::Minor);
}

TEST_CASE("Scale degree to semitones", "[scale]") {
  REQUIRE(glow::scaleDegreeToSemitones(0, glow::ScaleId::Major) == 0);
  REQUIRE(glow::scaleDegreeToSemitones(2, glow::ScaleId::Major) == 4);   // 3rd
  REQUIRE(glow::scaleDegreeToSemitones(4, glow::ScaleId::Major) == 7);   // 5th
  REQUIRE(glow::scaleDegreeToSemitones(-2, glow::ScaleId::Major) == -3); // 3rd down
}

TEST_CASE("Pitch quantize to major scale", "[joystick]") {
  REQUIRE(glow::quantizeCentsToScale(700.0f, 60, glow::ScaleId::Major) ==
          Catch::Approx(700.0f));
  const float nearE = glow::quantizeCentsToScale(420.0f, 60, glow::ScaleId::Major);
  REQUIRE(nearE == Catch::Approx(400.0f));
  const float nearF = glow::quantizeCentsToScale(480.0f, 60, glow::ScaleId::Major);
  REQUIRE(nearF == Catch::Approx(500.0f));
  const float cs = glow::quantizeCentsToScale(100.0f, 60, glow::ScaleId::Major);
  REQUIRE((cs == Catch::Approx(0.0f).margin(0.01f) || cs == Catch::Approx(200.0f).margin(0.01f)));
}

TEST_CASE("Pitch quantize via joystick pentatonic", "[joystick]") {
  glow::ParameterRegistry reg;
  glow::PerformanceState perf;
  perf.setRoot(60);
  perf.setScale(glow::ScaleId::Pentatonic);

  glow::JoystickConfig cfg;
  cfg.deadzone = 0.0f;
  cfg.smoothing = 1.0f;
  cfg.banks = {
      {"synth", {"pitch.offset", -1200, 1200, "linear"}, {"filter.cutoff", 80, 8000, "linear"}},
  };

  glow::Joystick joy;
  joy.configure(cfg);
  joy.setRegistry(&reg);
  joy.setPerformance(&perf);

  joy.setRawX(0.5f);
  joy.setRawY(0.0f);
  joy.update();
  const float cents = reg.get(glow::ParameterID::PitchOffset).target();
  const bool ok = cents == Catch::Approx(400.0f).margin(0.01f) ||
                  cents == Catch::Approx(700.0f).margin(0.01f);
  REQUIRE(ok);
}
