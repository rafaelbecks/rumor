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

#if RUMOR_HAS_PAULXSTRETCH
#include "engines/paulxstretch/PaulXStretchEngine.h"
#endif

#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class UmbralAudioProcessor : public juce::AudioProcessor {
 public:
  UmbralAudioProcessor();
  ~UmbralAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
  bool isSerialDrivingPitch() const;
  bool isSerialPitchLive() const;
  float getDisplayedPitchMidi() const;
  float getSoundingFrequencyHz() const;

  int getDroneNote() const { return droneNote_.load(); }
  void setDroneNote(int note);

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

  static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  static juce::String paramIdFor(glow::ParameterID id);

 private:
  void handleMidi(const juce::MidiBuffer& midi);
  void syncApvtsToRegistry();
  void syncOutputFromApvts();
  void pushDroneOn();
  void pushDroneOff();
  void applySerialConfig(const glow::SerialConfig& cfg, bool connectSerial, juce::String& statusOut);
  void appendDebugLine(const juce::String& line);
  juce::String serialConfigToJson(const glow::SerialConfig& cfg) const;
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

  std::atomic<bool> engineOn_{false};
  std::atomic<int> droneNote_{55};
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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UmbralAudioProcessor)
};
