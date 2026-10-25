#include "Presets.h"

#include "util/Scale.h"

namespace umbral_presets {
namespace {

juce::var makeDest(const char* param, double minV, double maxV, bool invert = false,
                   int midiCc = -1, const juce::String& source = {}) {
  auto* o = new juce::DynamicObject();
  o->setProperty("parameter", param);
  o->setProperty("min", minV);
  o->setProperty("max", maxV);
  o->setProperty("invert", invert);
  if (midiCc >= 0) o->setProperty("cc", midiCc);
  if (source.isNotEmpty()) o->setProperty("source", source);
  return juce::var(o);
}

}  // namespace

juce::File presetsDirectory() {
  auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                 .getChildFile("UMBRAL")
                 .getChildFile("presets");
  dir.createDirectory();
  return dir;
}

juce::var factoryAntenaGranular() {
  auto* root = new juce::DynamicObject();
  root->setProperty("name", kFactoryAntenaGranular);
  root->setProperty("baud", 9600);
  root->setProperty("drone", 55);
  root->setProperty("waveform", "sine");
  root->setProperty("quantize", "pentatonic");
  root->setProperty("debug", false);
  root->setProperty("input", juce::Array<juce::var>{4, 16});

  juce::Array<juce::var> destinations;
  destinations.add(makeDest("chord.spread", 0.0, 1.0));
  destinations.add(makeDest("granular.mix", 0.0, 1.0));
  destinations.add(makeDest("pitch", 45.0, 65.0));
  root->setProperty("destinations", destinations);

  auto* params = new juce::DynamicObject();
  params->setProperty("amplitude", 1.0);
  params->setProperty("filter.cutoff", 6000.0);
  params->setProperty("filter.resonance", 0.45);
  params->setProperty("chord.spread", 0.0);
  params->setProperty("granular.size", 0.08);
  params->setProperty("granular.density", 8.0);
  params->setProperty("granular.position", 0.5);
  params->setProperty("granular.spray", 0.1);
  params->setProperty("granular.pitch", 0.0);
  params->setProperty("granular.mix", 0.0);
  params->setProperty("folder.drive", 0.0);
  params->setProperty("folder.symmetry", 0.0);
  params->setProperty("folder.mix", 1.0);
  params->setProperty("osc.waveform", 0.0);  // sine
  params->setProperty("env.attack", 0.01);
  params->setProperty("env.release", 0.2);
  params->setProperty("pitch.offset", 0.0);
  root->setProperty("parameters", juce::var(params));

  return juce::var(root);
}

juce::StringArray listPresetNames() {
  juce::StringArray names;
  names.add(kFactoryAntenaGranular);

  for (const auto& entry : presetsDirectory().findChildFiles(juce::File::findFiles, false, "*.json")) {
    const auto stem = entry.getFileNameWithoutExtension();
    if (stem.isNotEmpty() && !names.contains(stem)) names.add(stem);
  }
  // Keep factory first; sort the rest
  if (names.size() > 1) {
    juce::StringArray rest;
    for (int i = 1; i < names.size(); ++i) rest.add(names[i]);
    rest.sort(true);
    names.clear();
    names.add(kFactoryAntenaGranular);
    names.addArray(rest);
  }
  return names;
}

juce::var capturePreset(const UmbralAudioProcessor& processor, const juce::String& name) {
  const auto cfg = processor.getSerialConfig();
  auto* root = new juce::DynamicObject();
  root->setProperty("name", name);
  root->setProperty("baud", cfg.baud);
  root->setProperty("drone", cfg.droneNote >= 0 ? cfg.droneNote : processor.getDroneNote());
  if (cfg.waveform) root->setProperty("waveform", juce::String(*cfg.waveform));
  if (cfg.quantize) root->setProperty("quantize", juce::String(*cfg.quantize));
  root->setProperty("debug", cfg.debug);
  root->setProperty("input", juce::Array<juce::var>{cfg.inputLow, cfg.inputHigh});

  juce::Array<juce::var> destinations;
  for (const auto& d : cfg.destinations) {
    const int cc = d.destination == glow::SerialDestination::MidiCc ? d.midiCc : -1;
    destinations.add(makeDest(glow::serialDestinationName(d.destination), d.min, d.max, d.invert, cc,
                              d.source.empty() ? juce::String() : juce::String(d.source)));
  }
  root->setProperty("destinations", destinations);
  root->setProperty("wsUrl", juce::String(cfg.wsUrl));
  juce::Array<juce::var> wsKeys;
  for (const auto& key : cfg.wsKeys) {
    auto* item = new juce::DynamicObject();
    item->setProperty("key", juce::String(key.key));
    item->setProperty("low", key.low);
    item->setProperty("high", key.high);
    item->setProperty("enabled", key.enabled);
    wsKeys.add(juce::var(item));
  }
  root->setProperty("wsKeys", wsKeys);

  auto* params = new juce::DynamicObject();
  for (size_t i = 0; i < glow::ParameterRegistry::count(); ++i) {
    const auto id = static_cast<glow::ParameterID>(i);
    const auto key = UmbralAudioProcessor::paramIdFor(id);
    if (auto* p = const_cast<UmbralAudioProcessor&>(processor).apvts().getRawParameterValue(key))
      params->setProperty(key, static_cast<double>(p->load()));
  }
  root->setProperty("parameters", juce::var(params));
  return juce::var(root);
}

bool savePreset(const juce::String& name, const juce::var& preset, juce::String& errorOut) {
  if (name.isEmpty()) {
    errorOut = "Preset name is empty.";
    return false;
  }

  juce::String safe;
  for (int i = 0; i < name.length(); ++i) {
    const auto c = name[i];
    if (juce::CharacterFunctions::isLetterOrDigit(c) || c == ' ' || c == '-' || c == '_')
      safe += c;
    else
      safe += '_';
  }
  safe = safe.trim();
  if (safe.isEmpty()) {
    errorOut = "Invalid preset name.";
    return false;
  }

  const auto file = presetsDirectory().getChildFile(safe + ".json");
  const auto text = juce::JSON::toString(preset, true);
  if (!file.replaceWithText(text)) {
    errorOut = "Could not write " + file.getFullPathName();
    return false;
  }
  return true;
}

std::optional<juce::var> loadPreset(const juce::String& name, juce::String& errorOut) {
  if (name.isEmpty()) {
    errorOut = "No preset selected.";
    return std::nullopt;
  }

  const auto file = presetsDirectory().getChildFile(name + ".json");
  if (file.existsAsFile()) {
    const auto parsed = juce::JSON::parse(file);
    if (parsed.isVoid()) {
      errorOut = "Invalid JSON in " + file.getFileName();
      return std::nullopt;
    }
    return parsed;
  }

  if (name == kFactoryAntenaGranular) return factoryAntenaGranular();

  errorOut = "Preset not found: " + name;
  return std::nullopt;
}

bool applyPreset(UmbralAudioProcessor& processor, const juce::var& preset, juce::String& statusOut) {
  if (!preset.isObject()) {
    statusOut = "Invalid preset.";
    return false;
  }

  // Build serial JSON text for ConfigLoader (comments not needed).
  juce::String serialJson;
  serialJson << "{\n";
  serialJson << "  \"baud\": " << static_cast<int>(preset.getProperty("baud", 9600)) << ",\n";
  serialJson << "  \"drone\": " << static_cast<int>(preset.getProperty("drone", 55)) << ",\n";
  if (preset.hasProperty("waveform"))
    serialJson << "  \"waveform\": \"" << preset["waveform"].toString() << "\",\n";
  if (preset.hasProperty("quantize"))
    serialJson << "  \"quantize\": \"" << preset["quantize"].toString() << "\",\n";
  serialJson << "  \"debug\": " << (static_cast<bool>(preset.getProperty("debug", false)) ? "true" : "false")
             << ",\n";

  const auto input = preset["input"];
  int inLo = 0, inHi = 1023;
  if (input.isArray() && input.size() >= 2) {
    inLo = static_cast<int>(input[0]);
    inHi = static_cast<int>(input[1]);
  }
  serialJson << "  \"input\": [" << inLo << ", " << inHi << "],\n";
  if (preset.hasProperty("wsUrl"))
    serialJson << "  \"wsUrl\": " << juce::JSON::toString(preset["wsUrl"]) << ",\n";
  if (preset.hasProperty("wsKeys"))
    serialJson << "  \"wsKeys\": " << juce::JSON::toString(preset["wsKeys"]) << ",\n";
  serialJson << "  \"destinations\": [\n";

  const auto destinations = preset["destinations"];
  if (destinations.isArray()) {
    for (int i = 0; i < destinations.size(); ++i) {
      const auto d = destinations[i];
      serialJson << "    { \"parameter\": \"" << d.getProperty("parameter", "pitch").toString()
                 << "\"";
      if (d.hasProperty("cc")) serialJson << ", \"cc\": " << static_cast<int>(d["cc"]);
      if (d.hasProperty("source"))
        serialJson << ", \"source\": " << juce::JSON::toString(d["source"]);
      serialJson << ", \"min\": " << static_cast<double>(d.getProperty("min", 0.0))
                 << ", \"max\": " << static_cast<double>(d.getProperty("max", 1.0))
                 << ", \"invert\": "
                 << (static_cast<bool>(d.getProperty("invert", false)) ? "true" : "false") << " }";
      if (i + 1 < destinations.size()) serialJson << ",";
      serialJson << "\n";
    }
  }
  serialJson << "  ]\n}\n";

  glow::SerialConfig cfg;
  std::string err;
  if (!glow::ConfigLoader::parseSerialText(serialJson.toStdString(), cfg, err)) {
    statusOut = err;
    return false;
  }

  const auto previous = processor.getSerialConfig();
  cfg.port = previous.port;
  if (!preset.hasProperty("wsUrl")) {
    cfg.wsUrl = previous.wsUrl;
    cfg.wsKeys = previous.wsKeys;
  }

  if (!processor.applyConfig(cfg, statusOut, false)) return false;

  if (auto* params = preset["parameters"].getDynamicObject()) {
    for (size_t i = 0; i < glow::ParameterRegistry::count(); ++i) {
      const auto id = static_cast<glow::ParameterID>(i);
      const auto key = UmbralAudioProcessor::paramIdFor(id);
      if (!params->hasProperty(key)) continue;
      const float value = static_cast<float>(params->getProperty(key));
      if (auto* p = processor.apvts().getParameter(key))
        p->setValueNotifyingHost(p->convertTo0to1(value));
      processor.registry().setTarget(id, value);
    }
  }

  statusOut = "Loaded preset \"" + preset.getProperty("name", "preset").toString() + "\".";
  return true;
}

}  // namespace umbral_presets
