#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

#include "audio/Oscillator.h"
#include "util/MidiMath.h"
#include "util/Scale.h"

namespace {
glow::ParameterID parameterIdFromSerialDest(glow::SerialDestination dest) {
  glow::ParameterID id = glow::ParameterID::Count;
  if (glow::serialDestinationToParameter(dest, id)) return id;
  return glow::ParameterID::Count;
}

glow::SerialConfig makeDefaultConfig() {
  glow::SerialConfig cfg;
  cfg.baud = 9600;
  cfg.droneNote = 55;
  cfg.waveform = "sine";
  cfg.quantize = "pentatonic";
  cfg.inputLow = 4;
  cfg.inputHigh = 16;
  cfg.destinations = {
      {glow::SerialDestination::ChordSpread, 0.0f, 1.0f, false},
      {glow::SerialDestination::GrainMix, 0.0f, 1.0f, false},
      {glow::SerialDestination::Pitch, 45.0f, 65.0f, false},
  };
  return cfg;
}
}  // namespace

juce::String UmbralAudioProcessor::paramIdFor(glow::ParameterID id) {
  return juce::String(glow::parameterName(id));
}

juce::AudioProcessorValueTreeState::ParameterLayout
UmbralAudioProcessor::createParameterLayout() {
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

  for (size_t i = 0; i < glow::ParameterRegistry::count(); ++i) {
    const auto id = static_cast<glow::ParameterID>(i);
    const auto& desc = glow::parameterDesc(id);
    const auto paramId = juce::ParameterID{paramIdFor(id), 1};

    if (id == glow::ParameterID::Waveform) {
      // Continuous 0..3 so serial can morph between adjacent waves.
      params.push_back(std::make_unique<juce::AudioParameterFloat>(
          paramId, desc.name, juce::NormalisableRange<float>(0.0f, 3.0f, 0.001f),
          desc.defaultValue));
      continue;
    }

    const bool skew =
        id == glow::ParameterID::FilterCutoff || id == glow::ParameterID::GrainDensity;
    auto range = juce::NormalisableRange<float>(desc.minimum, desc.maximum);
    if (skew) range.setSkewForCentre(std::sqrt(desc.minimum * desc.maximum));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramId, desc.name, range, desc.defaultValue));
  }

  // Physical amp outs: L/R as independent mono channels (Sub / Dayton).
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"out.balance", 1}, "Out Balance (Sub / Dayton)",
      juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"out.gainL", 1}, "Out L Gain (Sub)",
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"out.gainR", 1}, "Out R Gain (Dayton)",
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"out.swap", 1}, "Swap L/R outs", false));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"out.monoSum", 1}, "Mono sum to pan", true));
  // Per-engine route into physical L/R (level + balance).
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"route.gran.level", 1}, "Sinte Level",
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"route.gran.balance", 1}, "Sinte Balance",
      juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"route.px.level", 1}, "PaulXStretch Level",
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"route.px.balance", 1}, "PaulXStretch Balance",
      juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));

  return {params.begin(), params.end()};
}

UmbralAudioProcessor::UmbralAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", createParameterLayout()) {
  serialConfig_ = makeDefaultConfig();
  synth_.setParameterRegistry(&registry_);
  synth_.setPerformance(&performance_);
  synth_.setNoteQueue(&noteQueue_);
  serialPitch_.setPerformance(&performance_);
  serialPitch_.setRegistry(&registry_);
  serialPitch_.setOnReading([this](int raw, float smoothed) {
    if (!serialDebugConsole_.load()) return;
    glow::SerialConfig cfg;
    {
      std::lock_guard<std::mutex> lock(configMutex_);
      cfg = serialConfig_;
    }
    // Space-separated key=value tokens (filterable by key, e.g. "chord.spread, px.freq_shift").
    juce::String line;
    line << "raw=" << raw << " smoothed=" << juce::String(smoothed, 1);
    for (const auto& route : cfg.destinations) {
      float lo = route.min, hi = route.max;
      if (route.invert) std::swap(lo, hi);
      const float mapped = glow::serialMap(smoothed, static_cast<float>(cfg.inputLow),
                                           static_cast<float>(cfg.inputHigh), lo, hi);
      juce::String key = glow::serialDestinationName(route.destination);
      if (route.destination == glow::SerialDestination::MidiCc)
        key << "[" << route.midiCc << "]";
      line << " " << key << "=" << juce::String(mapped, 3);
    }
    appendDebugLine(line);
  });

  serialPitch_.setOnMidiCc([this](int cc, float value) {
    if (cc < 0 || cc > 127) return;
    const int rounded = juce::jlimit(0, 127, juce::roundToInt(value));
    const int prev = lastCcSent_[static_cast<size_t>(cc)].exchange(rounded);
    if (prev == rounded) return;
    ccOutQueue_.push({static_cast<uint8_t>(cc), static_cast<uint8_t>(rounded)});
  });

  serialPitch_.setOnEngineParam([this](std::string_view id, float value) {
#if RUMOR_HAS_PAULXSTRETCH
    std::lock_guard<std::mutex> lock(pxMutex_);
    if (pxEngine_ != nullptr) pxEngine_->setParameter(id, value);
#else
    juce::ignoreUnused(id, value);
#endif
  });

#if RUMOR_HAS_PAULXSTRETCH
  pxEngine_ = std::make_unique<rumor::PaulXStretchEngine>();
#endif

  for (auto& flag : serialDrives_) flag.store(false);
  for (auto& v : lastCcSent_) v.store(-1);
  syncApvtsToRegistry();
  syncOutputFromApvts();
  registry_.get(glow::ParameterID::Waveform).tickSmoothed();

  juce::String status;
  umbral_presets::applyPreset(*this, umbral_presets::factoryAntenaGranular(), status);
}

UmbralAudioProcessor::~UmbralAudioProcessor() {
  serialPitch_.setOnReading({});
  serialPitch_.setOnMidiCc({});
  serialPitch_.setOnEngineParam({});
  serialPitch_.close();
  {
    std::lock_guard<std::mutex> lock(midiOutMutex_);
    midiOut_.reset();
  }
#if RUMOR_HAS_PAULXSTRETCH
  {
    std::lock_guard<std::mutex> lock(pxMutex_);
    if (pxEngine_ != nullptr) pxEngine_->release();
    pxEngine_.reset();
  }
#endif
}

void UmbralAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  sampleRate_ = sampleRate;
  maxBlockSize_ = juce::jmax(samplesPerBlock, 4096);
  scratchL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
  scratchR_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
  pxScratchL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
  pxScratchR_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
  synth_.prepare(static_cast<float>(sampleRate), maxGrains_);
  prepareActiveEngine();
}

void UmbralAudioProcessor::releaseResources() {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  if (pxEngine_ != nullptr) pxEngine_->release();
#endif
}

void UmbralAudioProcessor::prepareActiveEngine() {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  if (pxEngine_ != nullptr) pxEngine_->prepare(sampleRate_, maxBlockSize_);
#endif
}

bool UmbralAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  const auto& main = layouts.getMainOutputChannelSet();
  return main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo();
}

void UmbralAudioProcessor::handleMidi(const juce::MidiBuffer& midi) {
  for (const auto metadata : midi) {
    const auto msg = metadata.getMessage();
    if (msg.isNoteOn()) {
      if (!isSerialPitchLive()) performance_.clearSerialPitch();
      droneNote_.store(msg.getNoteNumber());
      noteQueue_.push({glow::NoteEvent::Type::NoteOn, static_cast<uint8_t>(msg.getNoteNumber()),
                       static_cast<uint8_t>(
                           juce::jlimit(1, 127, static_cast<int>(msg.getVelocity())))});
      performance_.setBaseRoot(msg.getNoteNumber());
      performance_.setRoot(msg.getNoteNumber());
    } else if (msg.isNoteOff()) {
      noteQueue_.push({glow::NoteEvent::Type::NoteOff, static_cast<uint8_t>(msg.getNoteNumber()), 0});
    }
  }
}

void UmbralAudioProcessor::syncApvtsToRegistry() {
  for (size_t i = 0; i < glow::ParameterRegistry::count(); ++i) {
    const auto id = static_cast<glow::ParameterID>(i);
    if (serialDrives_[i].load()) continue;
    if (auto* p = apvts_.getRawParameterValue(paramIdFor(id))) {
      float v = p->load();
      registry_.setTarget(id, v);
    }
  }
}

void UmbralAudioProcessor::syncOutputFromApvts() {
  if (auto* p = apvts_.getRawParameterValue("out.balance"))
    outputRouter_.setBalance(p->load());
  if (auto* p = apvts_.getRawParameterValue("out.gainL"))
    outputRouter_.setGainL(p->load());
  if (auto* p = apvts_.getRawParameterValue("out.gainR"))
    outputRouter_.setGainR(p->load());
  if (auto* p = apvts_.getRawParameterValue("out.swap"))
    outputRouter_.setSwap(p->load() >= 0.5f);
  if (auto* p = apvts_.getRawParameterValue("out.monoSum"))
    outputRouter_.setMonoSum(p->load() >= 0.5f);
  if (auto* p = apvts_.getRawParameterValue("route.gran.level"))
    outputRouter_.setGranLevel(p->load());
  if (auto* p = apvts_.getRawParameterValue("route.gran.balance"))
    outputRouter_.setGranBalance(p->load());
  if (auto* p = apvts_.getRawParameterValue("route.px.level"))
    outputRouter_.setPxLevel(p->load());
  if (auto* p = apvts_.getRawParameterValue("route.px.balance"))
    outputRouter_.setPxBalance(p->load());
}

void UmbralAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  buffer.clear();
  handleMidi(midi);
  midi.clear();

  MidiCcEvent ccEv;
  while (ccOutQueue_.pop(ccEv)) {
    const auto msg = juce::MidiMessage::controllerEvent(1, ccEv.cc, ccEv.value);
    midi.addEvent(msg, 0);
    std::lock_guard<std::mutex> lock(midiOutMutex_);
    if (midiOut_ != nullptr) midiOut_->sendMessageNow(msg);
  }

  if (!engineOn_.load()) {
    synth_.drainNotes();
    return;
  }

  syncApvtsToRegistry();
  syncOutputFromApvts();

  const int numFrames = buffer.getNumSamples();
  const bool runGranular = granularEnabled_.load();
#if RUMOR_HAS_PAULXSTRETCH
  const bool runPx = pxEnabled_.load() && pxEngine_ != nullptr;
#else
  const bool runPx = false;
#endif

  auto* outL = buffer.getWritePointer(0);
  auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
  const bool hasR = outR != nullptr;

  int offset = 0;
  while (offset < numFrames) {
    const int n = juce::jmin(maxBlockSize_, numFrames - offset);
    juce::FloatVectorOperations::clear(outL + offset, n);
    if (hasR) juce::FloatVectorOperations::clear(outR + offset, n);

    if (runGranular) {
      juce::FloatVectorOperations::clear(scratchL_.data(), n);
      juce::FloatVectorOperations::clear(scratchR_.data(), n);
      synth_.process(scratchL_.data(), scratchR_.data(), n);
      outputRouter_.mixEngine(scratchL_.data(), scratchR_.data(), outL + offset,
                              hasR ? outR + offset : nullptr, n, outputRouter_.granLevel(),
                              outputRouter_.granBalance(), hasR);
    }

#if RUMOR_HAS_PAULXSTRETCH
    if (runPx) {
      juce::FloatVectorOperations::clear(pxScratchL_.data(), n);
      juce::FloatVectorOperations::clear(pxScratchR_.data(), n);
      pxEngine_->process(pxScratchL_.data(), pxScratchR_.data(), n);
      outputRouter_.mixEngine(pxScratchL_.data(), pxScratchR_.data(), outL + offset,
                              hasR ? outR + offset : nullptr, n, outputRouter_.pxLevel(),
                              outputRouter_.pxBalance(), hasR);
    }
#else
    juce::ignoreUnused(runPx);
#endif

    outputRouter_.applyMaster(outL + offset, hasR ? outR + offset : nullptr, n, hasR);
    offset += n;
  }
}

void UmbralAudioProcessor::pushDroneOn() {
  const int note = juce::jlimit(0, 127, droneNote_.load());
  noteQueue_.push({glow::NoteEvent::Type::NoteOn, static_cast<uint8_t>(note), 100});
  performance_.setBaseRoot(note);
  performance_.setRoot(note);
}

void UmbralAudioProcessor::pushDroneOff() {
  const int note = juce::jlimit(0, 127, droneNote_.load());
  noteQueue_.push({glow::NoteEvent::Type::NoteOff, static_cast<uint8_t>(note), 0});
}

void UmbralAudioProcessor::setEngineOn(bool on) {
  const bool was = engineOn_.exchange(on);
  if (on && !was) {
    pushDroneOn();
#if RUMOR_HAS_PAULXSTRETCH
    if (pxEnabled_.load()) setPaulXStretchParameter("px.pause", 0.0f);
#endif
  } else if (!on && was) {
    pushDroneOff();
    disconnectSerial();
  }
}

void UmbralAudioProcessor::setGranularEnabled(bool on) { granularEnabled_.store(on); }

void UmbralAudioProcessor::setPaulXStretchEnabled(bool on) {
#if RUMOR_HAS_PAULXSTRETCH
  pxEnabled_.store(on);
  if (on) {
    prepareActiveEngine();
    if (engineOn_.load()) setPaulXStretchParameter("px.pause", 0.0f);
  }
#else
  juce::ignoreUnused(on);
  pxEnabled_.store(false);
#endif
}

void UmbralAudioProcessor::setEngineMix(float mix01) {
  // Legacy: bias PX vs granular levels (kept for old UI / presets).
  mix01 = juce::jlimit(0.0f, 1.0f, mix01);
  if (auto* g = apvts_.getParameter("route.gran.level"))
    g->setValueNotifyingHost(g->convertTo0to1(1.0f - mix01));
  if (auto* p = apvts_.getParameter("route.px.level"))
    p->setValueNotifyingHost(p->convertTo0to1(mix01));
}

float UmbralAudioProcessor::getEngineMix() const {
  const float g = apvts_.getRawParameterValue("route.gran.level")
                      ? apvts_.getRawParameterValue("route.gran.level")->load()
                      : 1.0f;
  const float p = apvts_.getRawParameterValue("route.px.level")
                      ? apvts_.getRawParameterValue("route.px.level")->load()
                      : 0.0f;
  const float sum = g + p;
  return sum > 1.0e-6f ? (p / sum) : 0.0f;
}

void UmbralAudioProcessor::setDroneNote(int note) {
  note = juce::jlimit(0, 127, note);

  // Manual drone always owns pitch unless serial is live-driving it.
  if (!isSerialPitchLive()) performance_.clearSerialPitch();

  const int prev = droneNote_.exchange(note);
  {
    std::lock_guard<std::mutex> lock(configMutex_);
    serialConfig_.droneNote = note;
  }

  if (!engineOn_.load()) return;
  if (isSerialPitchLive()) return;  // serial owns frequency; slider is display-only

  if (prev != note) {
    noteQueue_.push({glow::NoteEvent::Type::NoteOff, static_cast<uint8_t>(prev), 0});
  }
  // Always (re)trigger so the voice locks to this MIDI note even if prev == note.
  pushDroneOn();
}

glow::SerialConfig UmbralAudioProcessor::getSerialConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return serialConfig_;
}

void UmbralAudioProcessor::appendDebugLine(const juce::String& line) {
  std::lock_guard<std::mutex> lock(debugMutex_);
  debugLines_.push_back(line);
  while (debugLines_.size() > 200) debugLines_.pop_front();
}

juce::String UmbralAudioProcessor::consumeDebugLog() {
  std::lock_guard<std::mutex> lock(debugMutex_);
  if (debugLines_.empty()) return {};
  juce::String out;
  for (const auto& line : debugLines_) {
    out << line << "\n";
  }
  debugLines_.clear();
  return out;
}

void UmbralAudioProcessor::setSerialDebugConsole(bool enabled) {
  serialDebugConsole_.store(enabled);
  serialPitch_.setDebug(enabled);
}

juce::String UmbralAudioProcessor::serialConfigToJson(const glow::SerialConfig& cfg) const {
  juce::String json;
  json << "{\n";
  json << "  \"drone\": " << cfg.droneNote << ",\n";
  json << "  \"port\": " << juce::JSON::toString(juce::var(cfg.port)) << ",\n";
  json << "  \"baud\": " << cfg.baud << ",\n";
  json << "  \"debug\": " << (cfg.debug ? "true" : "false") << ",\n";
  if (cfg.waveform) json << "  \"waveform\": \"" << *cfg.waveform << "\",\n";
  if (cfg.quantize) json << "  \"quantize\": \"" << *cfg.quantize << "\",\n";
  json << "  \"input\": [" << cfg.inputLow << ", " << cfg.inputHigh << "],\n";
  json << "  \"destinations\": [\n";
  for (size_t i = 0; i < cfg.destinations.size(); ++i) {
    const auto& d = cfg.destinations[i];
    json << "    { \"parameter\": \"" << glow::serialDestinationName(d.destination) << "\"";
    if (d.destination == glow::SerialDestination::MidiCc)
      json << ", \"cc\": " << d.midiCc;
    json << ", \"min\": " << juce::String(d.min, 4) << ", \"max\": " << juce::String(d.max, 4)
         << ", \"invert\": " << (d.invert ? "true" : "false") << " }";
    if (i + 1 < cfg.destinations.size()) json << ",";
    json << "\n";
  }
  json << "  ]\n}\n";
  return json;
}

void UmbralAudioProcessor::applySerialConfig(const glow::SerialConfig& cfg, bool connectSerial,
                                             juce::String& statusOut) {
  {
    std::lock_guard<std::mutex> lock(configMutex_);
    serialConfig_ = cfg;
  }

  if (cfg.droneNote >= 0) {
    droneNote_.store(juce::jlimit(0, 127, cfg.droneNote));
  }

  if (cfg.waveform) {
    glow::Waveform wave = glow::Waveform::Sine;
    if (glow::waveformFromName(*cfg.waveform, wave)) {
      const float v = glow::waveformToParam(wave);
      registry_.setTarget(glow::ParameterID::Waveform, v);
      if (auto* p = apvts_.getParameter(paramIdFor(glow::ParameterID::Waveform)))
        p->setValueNotifyingHost(p->convertTo0to1(v));
    }
  }

  if (cfg.quantize) {
    glow::ScaleId scale = glow::ScaleId::Off;
    if (glow::scaleFromName(*cfg.quantize, scale)) performance_.setScale(scale);
  } else {
    performance_.setScale(glow::ScaleId::Off);
  }

  // Hot-update mapping without dropping the port unless Connect asks to (re)open.
  serialPitch_.setInputRange(static_cast<float>(cfg.inputLow), static_cast<float>(cfg.inputHigh));
  serialPitch_.setRoutes(cfg.destinations);
  serialPitch_.setDebug(serialDebugConsole_.load() || cfg.debug);

  auto refreshSerialDrives = [this, &cfg]() {
    for (auto& flag : serialDrives_) flag.store(false);
    serialDrivesPitch_.store(false);
    // Only lock UI→engine params while the port is actually open.
    if (!serialPitch_.isOpen()) {
      performance_.clearSerialPitch();
      return;
    }
    for (const auto& route : cfg.destinations) {
      if (route.destination == glow::SerialDestination::Pitch) {
        serialDrivesPitch_.store(true);
        continue;
      }
      if (route.destination == glow::SerialDestination::MidiCc) continue;
      if (glow::serialDestinationIsPaulXStretch(route.destination)) continue;
      const auto id = parameterIdFromSerialDest(route.destination);
      if (id != glow::ParameterID::Count)
        serialDrives_[static_cast<size_t>(id)].store(true);
    }
    if (!serialDrivesPitch_.load()) performance_.clearSerialPitch();
  };

  if (!connectSerial) {
    refreshSerialDrives();
    statusOut = serialPitch_.isOpen()
                    ? juce::String::fromUTF8("Ajustes aplicados (serial sigue conectado).")
                    : juce::String::fromUTF8(
                          "Ajustes aplicados. Conectar para abrir el puerto serial.");
    return;
  }

  if (cfg.port.empty()) {
    refreshSerialDrives();
    statusOut = juce::String::fromUTF8("Ajustes aplicados. Eleg\xc3\xad un puerto y Conectar.");
    return;
  }

  const bool samePort =
      serialPitch_.isOpen() && serialPitch_.path() == cfg.port && serialPitch_.baud() == cfg.baud;
  if (samePort) {
    refreshSerialDrives();
    statusOut = juce::String::fromUTF8("Ajustes aplicados \xe2\x80\x94 ya conectado a ") +
                juce::String(cfg.port);
    return;
  }

  serialPitch_.close();
  if (!serialPitch_.open(cfg.port, cfg.baud)) {
    refreshSerialDrives();
    statusOut = juce::String::fromUTF8("Ajustes aplicados, pero fall\xc3\xb3 al abrir: ") +
                juce::String(cfg.port);
    return;
  }
  refreshSerialDrives();
  statusOut = juce::String::fromUTF8("Serial conectado: ") + juce::String(serialPitch_.path()) +
              " @ " + juce::String(serialPitch_.baud());
}

bool UmbralAudioProcessor::applyConfig(glow::SerialConfig cfg, juce::String& statusOut,
                                       bool connectSerial) {
  if (cfg.destinations.size() > static_cast<size_t>(glow::kSerialMaxDestinations)) {
    statusOut = "At most " + juce::String(glow::kSerialMaxDestinations) + " destinations.";
    return false;
  }
  for (const auto& route : cfg.destinations) {
    float limLo = 0, limHi = 1;
    glow::serialDestinationLimits(route.destination, limLo, limHi);
    if (route.min == route.max || route.min < limLo || route.min > limHi || route.max < limLo ||
        route.max > limHi) {
      statusOut = juce::String("Invalid range for ") + glow::serialDestinationName(route.destination);
      return false;
    }
  }

  applySerialConfig(cfg, connectSerial, statusOut);
  if (engineOn_.load()) {
    pushDroneOff();
    pushDroneOn();
  }
  return true;
}

void UmbralAudioProcessor::disconnectSerial() {
  serialPitch_.close();
  for (auto& flag : serialDrives_) flag.store(false);
  serialDrivesPitch_.store(false);
  performance_.clearSerialPitch();
  // Re-lock voice to the drone MIDI note after serial releases pitch.
  if (engineOn_.load()) pushDroneOn();
}

bool UmbralAudioProcessor::isSerialConnected() const { return serialPitch_.isOpen(); }

juce::String UmbralAudioProcessor::getSerialStatus() const {
  if (!serialPitch_.isOpen()) return "Serial: disconnected";
  return "Serial: " + juce::String(serialPitch_.path()) + " @ " + juce::String(serialPitch_.baud());
}

std::vector<juce::String> UmbralAudioProcessor::listSerialPorts() const {
  std::vector<juce::String> out;
  for (const auto& p : glow::listSerialPorts()) out.emplace_back(p);
  return out;
}

std::vector<UmbralAudioProcessor::MidiOutDevice> UmbralAudioProcessor::listMidiOutputDevices() const {
  std::vector<MidiOutDevice> out;
  for (const auto& d : juce::MidiOutput::getAvailableDevices())
    out.push_back({d.identifier, d.name});
  return out;
}

bool UmbralAudioProcessor::setMidiOutputDevice(const juce::String& identifier,
                                               juce::String& statusOut) {
  std::lock_guard<std::mutex> lock(midiOutMutex_);
  midiOut_.reset();
  midiOutIdentifier_ = identifier;
  midiOutName_ = {};

  if (identifier.isEmpty()) {
    statusOut = "MIDI CC out: host only (no system device).";
    return true;
  }

  midiOut_ = juce::MidiOutput::openDevice(identifier);
  if (midiOut_ == nullptr) {
    midiOutIdentifier_.clear();
    statusOut = "Failed to open MIDI output device.";
    return false;
  }

  midiOutName_ = midiOut_->getName();
  statusOut = juce::String::fromUTF8("MIDI CC out \xe2\x86\x92 ") + midiOutName_;
  return true;
}

juce::String UmbralAudioProcessor::getMidiOutputIdentifier() const {
  std::lock_guard<std::mutex> lock(midiOutMutex_);
  return midiOutIdentifier_;
}

juce::String UmbralAudioProcessor::getMidiOutputName() const {
  std::lock_guard<std::mutex> lock(midiOutMutex_);
  return midiOutName_;
}

bool UmbralAudioProcessor::hasMidiOutputDevice() const {
  std::lock_guard<std::mutex> lock(midiOutMutex_);
  return midiOut_ != nullptr;
}

void UmbralAudioProcessor::setParameterTarget(glow::ParameterID id, float value) {
  registry_.setTarget(id, value);
  if (auto* p = apvts_.getParameter(paramIdFor(id))) {
    if (id == glow::ParameterID::Waveform) value = std::round(value);
    p->setValueNotifyingHost(p->convertTo0to1(value));
  }
}

float UmbralAudioProcessor::getParameterTarget(glow::ParameterID id) const {
  return registry_.get(id).target();
}

bool UmbralAudioProcessor::isSerialDriving(glow::ParameterID id) const {
  return serialDrives_[static_cast<size_t>(id)].load();
}

bool UmbralAudioProcessor::isSerialDrivingPx(const juce::String& paramId) const {
  if (!serialPitch_.isOpen() || paramId.isEmpty()) return false;
  glow::SerialConfig cfg;
  {
    std::lock_guard<std::mutex> lock(configMutex_);
    cfg = serialConfig_;
  }
  for (const auto& route : cfg.destinations) {
    if (!glow::serialDestinationIsPaulXStretch(route.destination)) continue;
    if (paramId == glow::serialDestinationName(route.destination)) return true;
  }
  return false;
}

bool UmbralAudioProcessor::isSerialDrivingPitch() const { return serialDrivesPitch_.load(); }

bool UmbralAudioProcessor::isSerialPitchLive() const {
  return serialPitch_.isOpen() && serialDrivesPitch_.load() && performance_.serialPitchActive();
}

float UmbralAudioProcessor::getDisplayedPitchMidi() const {
  if (isSerialPitchLive()) {
    float note = performance_.serialMidi();
    const auto scale = performance_.scale();
    if (scale != glow::ScaleId::Off)
      note = glow::quantizeMidiToScale(note, performance_.baseRoot(), scale);
    return note;
  }
  return static_cast<float>(droneNote_.load());
}

float UmbralAudioProcessor::getSoundingFrequencyHz() const {
  float midi = getDisplayedPitchMidi();
  const float cents = registry_.get(glow::ParameterID::PitchOffset).value();
  return glow::midi_math::frequencyWithCentsOffset(glow::midi_math::midiNoteToFrequency(midi),
                                                   cents);
}

void UmbralAudioProcessor::setAudioEngineKind(rumor::EngineKind kind) {
#if !RUMOR_HAS_PAULXSTRETCH
  kind = rumor::EngineKind::Granular;
#endif
  // Legacy exclusive switch → dual enables with full bias.
  if (kind == rumor::EngineKind::PaulXStretch) {
    setGranularEnabled(false);
    setPaulXStretchEnabled(true);
    setEngineMix(1.0f);
  } else {
    setGranularEnabled(true);
    setPaulXStretchEnabled(false);
    setEngineMix(0.0f);
  }
}

rumor::EngineKind UmbralAudioProcessor::getAudioEngineKind() const {
  if (pxEnabled_.load() && !granularEnabled_.load()) return rumor::EngineKind::PaulXStretch;
  return rumor::EngineKind::Granular;
}

bool UmbralAudioProcessor::hasPaulXStretch() const {
#if RUMOR_HAS_PAULXSTRETCH
  return pxEngine_ != nullptr;
#else
  return false;
#endif
}

bool UmbralAudioProcessor::loadPaulXStretchSource(const juce::File& file, juce::String& statusOut) {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  if (pxEngine_ == nullptr) {
    statusOut = juce::String::fromUTF8("PaulXStretch no disponible.");
    return false;
  }
  std::string err;
  if (!pxEngine_->loadSourceFile(file.getFullPathName().toStdString(), err)) {
    statusOut = err.empty() ? juce::String::fromUTF8("No se pudo cargar el audio.")
                            : juce::String(err);
    return false;
  }
  statusOut = juce::String::fromUTF8("Fuente PX: ") + file.getFileName();
  return true;
#else
  juce::ignoreUnused(file);
  statusOut = juce::String::fromUTF8("Compilado sin PaulXStretch.");
  return false;
#endif
}

void UmbralAudioProcessor::clearPaulXStretchSource() {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  if (pxEngine_ != nullptr) pxEngine_->clearSource();
#endif
}

juce::String UmbralAudioProcessor::paulXStretchSourceName() const {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  if (pxEngine_ == nullptr || !pxEngine_->hasSource()) return {};
  return juce::File(pxEngine_->sourceFilePath()).getFileName();
#else
  return {};
#endif
}

juce::File UmbralAudioProcessor::paulXStretchSourceFile() const {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  if (pxEngine_ == nullptr || !pxEngine_->hasSource()) return {};
  return juce::File(pxEngine_->sourceFilePath());
#else
  return {};
#endif
}

double UmbralAudioProcessor::paulXStretchPlayheadPercent() const {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  if (pxEngine_ == nullptr) return 0.0;
  return pxEngine_->playheadPercent();
#else
  return 0.0;
#endif
}

double UmbralAudioProcessor::paulXStretchPrebufferReady() const {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  if (pxEngine_ == nullptr) return 0.0;
  return pxEngine_->prebufferReady();
#else
  return 0.0;
#endif
}

bool UmbralAudioProcessor::setPaulXStretchParameter(const juce::String& id, float value) {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  return pxEngine_ != nullptr && pxEngine_->setParameter(id.toStdString(), value);
#else
  juce::ignoreUnused(id, value);
  return false;
#endif
}

bool UmbralAudioProcessor::getPaulXStretchParameter(const juce::String& id, float& valueOut) const {
#if RUMOR_HAS_PAULXSTRETCH
  std::lock_guard<std::mutex> lock(pxMutex_);
  return pxEngine_ != nullptr && pxEngine_->getParameter(id.toStdString(), valueOut);
#else
  juce::ignoreUnused(id, valueOut);
  return false;
#endif
}

void UmbralAudioProcessor::setOutputBalance(float balanceMinus1To1) {
  outputRouter_.setBalance(balanceMinus1To1);
  if (auto* p = apvts_.getParameter("out.balance"))
    p->setValueNotifyingHost(p->convertTo0to1(balanceMinus1To1));
}

void UmbralAudioProcessor::setOutputGainL(float gain01) {
  outputRouter_.setGainL(gain01);
  if (auto* p = apvts_.getParameter("out.gainL"))
    p->setValueNotifyingHost(p->convertTo0to1(gain01));
}

void UmbralAudioProcessor::setOutputGainR(float gain01) {
  outputRouter_.setGainR(gain01);
  if (auto* p = apvts_.getParameter("out.gainR"))
    p->setValueNotifyingHost(p->convertTo0to1(gain01));
}

void UmbralAudioProcessor::setOutputSwap(bool swap) {
  outputRouter_.setSwap(swap);
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts_.getParameter("out.swap")))
    p->setValueNotifyingHost(swap ? 1.0f : 0.0f);
}

void UmbralAudioProcessor::setOutputMonoSum(bool mono) {
  outputRouter_.setMonoSum(mono);
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts_.getParameter("out.monoSum")))
    p->setValueNotifyingHost(mono ? 1.0f : 0.0f);
}

void UmbralAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
  auto state = apvts_.copyState();
  state.setProperty("engineOn", engineOn_.load(), nullptr);
  state.setProperty("droneNote", droneNote_.load(), nullptr);
  state.setProperty("serialDebug", serialDebugConsole_.load(), nullptr);
  state.setProperty("patchJson", serialConfigToJson(getSerialConfig()), nullptr);
  state.setProperty("midiOutId", getMidiOutputIdentifier(), nullptr);
  state.setProperty("granularOn", granularEnabled_.load(), nullptr);
  state.setProperty("pxOn", pxEnabled_.load(), nullptr);
  state.setProperty("audioEngine", rumor::engineKindName(getAudioEngineKind()), nullptr);
  state.setProperty("pxSource", [&]() -> juce::String {
#if RUMOR_HAS_PAULXSTRETCH
    std::lock_guard<std::mutex> lock(pxMutex_);
    return pxEngine_ != nullptr ? juce::String(pxEngine_->sourceFilePath()) : juce::String();
#else
    return {};
#endif
  }(), nullptr);
  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  copyXmlToBinary(*xml, destData);
}

void UmbralAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
  if (xml == nullptr || !xml->hasTagName(apvts_.state.getType())) return;

  auto tree = juce::ValueTree::fromXml(*xml);
  apvts_.replaceState(tree);
  if (tree.hasProperty("droneNote")) droneNote_.store(static_cast<int>(tree.getProperty("droneNote")));
  setSerialDebugConsole(static_cast<bool>(tree.getProperty("serialDebug", false)));
  syncApvtsToRegistry();
  syncOutputFromApvts();

  if (tree.hasProperty("patchJson")) {
    glow::SerialConfig cfg;
    std::string err;
    if (glow::ConfigLoader::parseSerialText(tree.getProperty("patchJson").toString().toStdString(),
                                            cfg, err)) {
      juce::String status;
      applyConfig(cfg, status, false);
    }
  }

  if (tree.hasProperty("midiOutId")) {
    juce::String status;
    setMidiOutputDevice(tree.getProperty("midiOutId").toString(), status);
  }

  if (tree.hasProperty("granularOn") || tree.hasProperty("pxOn")) {
    setGranularEnabled(static_cast<bool>(tree.getProperty("granularOn", true)));
    setPaulXStretchEnabled(static_cast<bool>(tree.getProperty("pxOn", false)));
  } else if (tree.hasProperty("audioEngine")) {
    const auto name = tree.getProperty("audioEngine").toString();
    setAudioEngineKind(name == "paulxstretch" ? rumor::EngineKind::PaulXStretch
                                              : rumor::EngineKind::Granular);
  }

  if (tree.hasProperty("pxSource")) {
    const auto path = tree.getProperty("pxSource").toString();
    if (path.isNotEmpty()) {
      juce::String status;
      loadPaulXStretchSource(juce::File(path), status);
    }
  }

  setEngineOn(static_cast<bool>(tree.getProperty("engineOn", false)));
}

juce::AudioProcessorEditor* UmbralAudioProcessor::createEditor() {
  return new UmbralAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UmbralAudioProcessor(); }
