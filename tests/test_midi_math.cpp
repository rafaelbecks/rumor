#include <catch_amalgamated.hpp>

#include "util/MidiMath.h"

TEST_CASE("MIDI note to frequency", "[midi]") {
  REQUIRE_THAT(glow::midi_math::midiNoteToFrequency(69.0f),
               Catch::Matchers::WithinAbs(440.0f, 0.01f));
  REQUIRE_THAT(glow::midi_math::midiNoteToFrequency(57.0f),
               Catch::Matchers::WithinAbs(220.0f, 0.01f));
  REQUIRE_THAT(glow::midi_math::midiNoteToFrequency(81.0f),
               Catch::Matchers::WithinAbs(880.0f, 0.01f));
}

TEST_CASE("Cents offset", "[midi]") {
  const float base = 440.0f;
  const float up = glow::midi_math::frequencyWithCentsOffset(base, 100.0f);
  REQUIRE_THAT(up, Catch::Matchers::WithinAbs(466.16f, 0.5f));
  const float none = glow::midi_math::frequencyWithCentsOffset(base, 0.0f);
  REQUIRE_THAT(none, Catch::Matchers::WithinAbs(440.0f, 0.001f));
}

TEST_CASE("MIDI CC to normalized", "[midi]") {
  REQUIRE(glow::midi_math::ccToNormalized(0) == Catch::Approx(0.0f));
  REQUIRE(glow::midi_math::ccToNormalized(127) == Catch::Approx(1.0f));
  REQUIRE(glow::midi_math::ccToNormalized(64) == Catch::Approx(64.0f / 127.0f));
}

TEST_CASE("Normalized parameter mapping", "[midi]") {
  REQUIRE(glow::midi_math::mapNormalized(0.0f, 80.0f, 12000.0f) == Catch::Approx(80.0f));
  REQUIRE(glow::midi_math::mapNormalized(1.0f, 80.0f, 12000.0f) == Catch::Approx(12000.0f));
  REQUIRE(glow::midi_math::mapNormalized(0.5f, 0.0f, 100.0f) == Catch::Approx(50.0f));
}
