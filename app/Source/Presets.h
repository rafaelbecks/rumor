#pragma once

#include "PluginProcessor.h"

#include <optional>

/** Factory + user presets (JSON). Serial port is never stored. */
namespace umbral_presets {

inline constexpr const char* kFactoryAntenaGranular = "antena granular";

juce::File presetsDirectory();
juce::StringArray listPresetNames();  // factory first, then user files A–Z

/** Built-in default matching the antena granular session. */
juce::var factoryAntenaGranular();

/** Snapshot current engine state (no port). */
juce::var capturePreset(const UmbralAudioProcessor& processor, const juce::String& name);

bool savePreset(const juce::String& name, const juce::var& preset, juce::String& errorOut);
std::optional<juce::var> loadPreset(const juce::String& name, juce::String& errorOut);

/** Apply preset into processor. Keeps the current serial port. Does not open serial. */
bool applyPreset(UmbralAudioProcessor& processor, const juce::var& preset, juce::String& statusOut);

}  // namespace umbral_presets
