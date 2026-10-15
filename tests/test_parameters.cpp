#include <catch_amalgamated.hpp>

#include "parameters/ParameterRegistry.h"

TEST_CASE("Parameter registry defaults and clamp", "[params]") {
  glow::ParameterRegistry reg;
  REQUIRE(reg.findByName("filter.cutoff").has_value());
  REQUIRE_FALSE(reg.findByName("does.not.exist").has_value());

  reg.setTarget(glow::ParameterID::FilterCutoff, 999999.0f);
  REQUIRE(reg.get(glow::ParameterID::FilterCutoff).target() ==
          Catch::Approx(16000.0f));

  reg.setTarget(glow::ParameterID::PitchOffset, -50.0f);
  REQUIRE(reg.get(glow::ParameterID::PitchOffset).target() == Catch::Approx(-50.0f));
}

TEST_CASE("Parameter name resolution", "[params]") {
  REQUIRE(glow::parameterIdFromName("granular.size") == glow::ParameterID::GrainSize);
  REQUIRE(glow::parameterIdFromName("granular.pitch") == glow::ParameterID::GrainPitch);
  REQUIRE(glow::parameterIdFromName("amplitude") == glow::ParameterID::Amplitude);
  REQUIRE(glow::parameterIdFromName("nope") == glow::ParameterID::Count);
}
