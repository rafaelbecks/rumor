#pragma once

#include <JuceHeader.h>

#include "audio/Synth.h"
#include "config/ConfigLoader.h"
#include "control/PerformanceState.h"
#include "control/SerialPitch.h"
#include "engines/IAudioEngine.h"
#include "engines/OutputRouter.h"
#include "parameters/ParameterRegistry.h"
#include "util/LockFreeQueue.h"
#include "RemoteLink.h"
#include "SensorWs.h"

#if RUMOR_HAS_PAULXSTRETCH
#include "engines/paulxstretch/PaulXStretchEngine.h"
#endif

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class UmbralAudioProcessor : public juce::AudioProcessor {
 public:
  UmbralAudioProcessor();
  ~UmbralAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using juce::AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.5; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  bool isEngineOn() const { return engineOn_.load(); }
  void setEngineOn(bool on);

  glow::SerialConfig getSerialConfig() const;
  bool applyConfig(glow::SerialConfig cfg, juce::String& statusOut, bool connectSerial);

  void disconnectSerial();
  bool isSerialConnected() const;
  juce::String getSerialStatus() const;
  std::vector<juce::String> listSerialPorts() const;

  /** Port, baud, serial window, websocket host, and per-key windows. Does not change listen mode. */
  bool saveConnectionSettings(glow::SerialConfig incoming, juce::String& statusOut);
  bool setSensorListen(glow::SensorListen mode, juce::String& statusOut);
  glow::SensorListen getSensorListen() const;
  bool isWsConnected() const;
  juce::String getWsStatus() const;
  std::vector<std::string> getWsSeenKeys() const;
  std::vector<std::pair<std::string, float>> getWsLatest() const;
  juce::String getWsPretty() const;
  juce::String consumeWsDebugLog();
  void setRemoteUrl(const juce::String& url);

  struct MidiOutDevice {
    juce::String identifier;
    juce::String name;
  };
  std::vector<MidiOutDevice> listMidiOutputDevices() const;
  bool setMidiOutputDevice(const juce::String& identifier, juce::String& statusOut);
  juce::String getMidiOutputIdentifier() const;
  juce::String getMidiOutputName() const;
  bool hasMidiOutputDevice() const;

  glow::ParameterRegistry& registry() { return registry_; }
  const glow::ParameterRegistry& registry() const { return registry_; }

  void setParameterTarget(glow::ParameterID id, float value);
  float getParameterTarget(glow::ParameterID id) const;

  bool isSerialDriving(glow::ParameterID id) const;
  /** True when an open serial route owns this PaulXStretch param id (e.g. "px.stretch"). */
  bool isSerialDrivingPx(const juce::String& paramId) const;
  /** True when an open serial route owns this destination id (levels, px.*, …). */
  bool isSerialDrivingId(const juce::String& paramId) const;
  bool isSerialDrivingPitch() const;
  bool isSerialPitchLive() const;
  float getDisplayedPitchMidi() const;
  float getSoundingFrequencyHz() const;

  int getDroneNote() const { return droneNote_.load(); }
  void setDroneNote(int note);

  static constexpr int kMaxPitches = 3;
  int getPitchCount() const { return pitchCount_.load(); }
  int getPitchNote(int index) const;
  float getPitchFrequencyHz(int index) const;
  void setPitchNote(int index, int note);
  void addPitch();
  void removePitch(int index);

  void setSerialDebugConsole(bool enabled);
  bool isSerialDebugConsole() const { return serialDebugConsole_.load(); }
  juce::String consumeDebugLog();
  int lastSerialRaw() const { return serialPitch_.lastRawAdc(); }
  float lastSerialSmoothed() const { return serialPitch_.lastSmoothedAdc(); }

  /** Dual engines: both can run together; mix 0 = granular, 1 = PaulXStretch. */
  void setGranularEnabled(bool on);
  void setPaulXStretchEnabled(bool on);
  bool isGranularEnabled() const { return granularEnabled_.load(); }
  bool isPaulXStretchEnabled() const { return pxEnabled_.load(); }
  void setEngineMix(float mix01);
  float getEngineMix() const;

  /** Legacy single-kind API (maps onto enables). */
  void setAudioEngineKind(rumor::EngineKind kind);
  rumor::EngineKind getAudioEngineKind() const;
  bool hasPaulXStretch() const;
  bool loadPaulXStretchSource(const juce::File& file, juce::String& statusOut);
  void clearPaulXStretchSource();
  juce::String paulXStretchSourceName() const;
  juce::File paulXStretchSourceFile() const;
  double paulXStretchPlayheadPercent() const;
  double paulXStretchPrebufferReady() const;
  bool setPaulXStretchParameter(const juce::String& id, float value);
  bool getPaulXStretchParameter(const juce::String& id, float& valueOut) const;

  /** Physical L/R outs as mono amp channels (Sub / Dayton). */
  rumor::OutputRouter& outputRouter() { return outputRouter_; }
  const rumor::OutputRouter& outputRouter() const { return outputRouter_; }
  void setOutputBalance(float balanceMinus1To1);
  void setOutputGainL(float gain01);
  void setOutputGainR(float gain01);
  void setOutputSwap(bool swap);
  void setOutputMonoSum(bool mono);

  juce::AudioProcessorValueTreeState& apvts() { return apvts_; }

  /** Outbound link to the remote calibration hub. Empty URL is ignored. */
  void connectRemote(const juce::String& url);
  void disconnectRemote();
  bool isRemoteEnabled() const { return remoteEnabled_; }
  bool isRemoteConnected() const;
  juce::String getRemoteUrl() const { return remoteUrl_; }
  juce::String getRemoteStatus() const;
  uint32_t remoteUiRevision() const { return remoteUiRevision_.load(); }
  uint32_t remotePxRevision() const { return remotePxRevision_.load(); }

  static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  static juce::String paramIdFor(glow::ParameterID id);

 private:
  void handleMidi(const juce::MidiBuffer& midi);
  void syncApvtsToRegistry();
  void syncOutputFromApvts();
  void pushDroneOn();
  void pushDroneOff();
  void syncSynthPitches();
  void applySerialConfig(const glow::SerialConfig& cfg, bool connectSerial, juce::String& statusOut);
  void refreshSensorDrives();
  void handleWsValues(const std::vector<std::pair<std::string, float>>& values, const juce::String& pretty);
  bool routeIsLive(const glow::SerialRoute& route) const;
  void appendDebugLine(const juce::String& line);
  void appendWsDebug(const juce::String& block);
  juce::String serialConfigToJson(const glow::SerialConfig& cfg) const;
  void applyRemoteCommand(const juce::var& msg);
  void applyRemoteParam(const juce::String& id, float value);
  void applyRemoteConfig(const juce::var& config);
  juce::String buildRemoteHelloJson() const;
  juce::String buildRemoteStateJson() const;
  void bumpRemoteUi();
  void bumpRemotePx();
  void refreshRemoteDevices();
  void prepareActiveEngine();

  juce::AudioProcessorValueTreeState apvts_;

  glow::ParameterRegistry registry_;
  glow::PerformanceState performance_;
  struct MidiCcEvent {
    uint8_t cc{0};
    uint8_t value{0};
  };

  glow::LockFreeQueue<glow::NoteEvent, 256> noteQueue_;
  glow::LockFreeQueue<MidiCcEvent, 128> ccOutQueue_;
  glow::Synth synth_;
  glow::SerialPitch serialPitch_;
  glow::SerialConfig serialConfig_{};
  rumor::OutputRouter outputRouter_;

#if RUMOR_HAS_PAULXSTRETCH
  std::unique_ptr<rumor::PaulXStretchEngine> pxEngine_;
#endif
  std::atomic<bool> granularEnabled_{true};
  std::atomic<bool> pxEnabled_{false};
  double sampleRate_{44100.0};

  mutable std::mutex configMutex_;
  mutable std::mutex midiOutMutex_;
  mutable std::mutex pxMutex_;
  std::mutex debugMutex_;
  std::deque<juce::String> debugLines_;

  std::unique_ptr<juce::MidiOutput> midiOut_;
  juce::String midiOutIdentifier_;
  juce::String midiOutName_;

  std::unique_ptr<RemoteLink> remote_;
  struct RemoteDevicePoll;
  std::unique_ptr<RemoteDevicePoll> remotePoll_;
  mutable juce::CriticalSection remoteDeviceLock_;
  juce::Array<juce::var> remotePorts_;
  juce::Array<juce::var> remoteMidi_;
  juce::Array<juce::var> remotePresets_;
  juce::String remoteUrl_;
  bool remoteEnabled_{false};
  std::atomic<uint32_t> remoteUiRevision_{0};
  std::atomic<uint32_t> remotePxRevision_{0};

  std::atomic<bool> engineOn_{false};
  std::atomic<int> droneNote_{55};
  std::atomic<int> pitchCount_{1};
  std::array<std::atomic<int>, kMaxPitches - 1> extraNote_{};
  std::atomic<bool> serialDrivesPitch_{false};
  std::atomic<bool> serialDebugConsole_{false};
  std::array<std::atomic<bool>, static_cast<size_t>(glow::ParameterID::Count)> serialDrives_{};
  std::array<std::atomic<int>, 128> lastCcSent_{};

  std::vector<float> scratchL_;
  std::vector<float> scratchR_;
  std::vector<float> pxScratchL_;
  std::vector<float> pxScratchR_;
  int maxBlockSize_{512};
  int maxGrains_{32};

  struct Lifetime {
    std::atomic<bool> alive{true};
  };
  std::shared_ptr<Lifetime> life_ = std::make_shared<Lifetime>();
  mutable std::mutex wsMutex_;
  std::deque<juce::String> wsDebugLines_;
  std::vector<std::string> wsSeenKeys_;
  std::vector<std::pair<std::string, float>> wsLatest_;
  juce::String wsPretty_;
  std::atomic<int> sensorListen_{static_cast<int>(glow::SensorListen::Off)};
  std::unique_ptr<SensorWs> sensorWs_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UmbralAudioProcessor)
};
