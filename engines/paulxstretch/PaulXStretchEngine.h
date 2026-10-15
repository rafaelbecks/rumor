#pragma once

#include "engines/IAudioEngine.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

class StretchAudioSource;
class MyBufferingAudioSource;
struct ProcessParameters;
class breakpoint_envelope;

namespace juce {
class AudioFormatManager;
class AudioParameterBool;
class TimeSliceThread;
template <typename T>
class AudioBuffer;
}  // namespace juce

namespace rumor {

/**
 * Native PaulXStretch adapter (StretchAudioSource + background prebuffer).
 * Same OutputRouter path as granular: process() fills L/R, host pans via router.
 */
class PaulXStretchEngine final : public IAudioEngine {
 public:
  PaulXStretchEngine();
  ~PaulXStretchEngine() override;

  EngineKind kind() const override { return EngineKind::PaulXStretch; }
  const char* name() const override { return "PaulXStretch"; }

  void prepare(double sampleRate, int maxBlockSize) override;
  void process(float* left, float* right, int numFrames) override;
  void release() override;
  void reset() override;

  bool loadSourceFile(const std::string& path, std::string& errorOut) override;
  void clearSource() override;
  std::string sourceFilePath() const override;
  bool hasSource() const override;

  std::vector<EngineParameterInfo> parameters() const override;
  bool setParameter(std::string_view id, float value) override;
  bool getParameter(std::string_view id, float& valueOut) const override;

  bool isAvailable() const { return stretch_ != nullptr; }
  double playheadPercent() const;
  /** 0..1 how full the PX prebuffer is (UI). */
  double prebufferReady() const;

 private:
  void ensureStretchLocked();
  void ensureFreeFilterEnvelopeLocked();
  void ensureBufferingLocked();
  void pushProcessParamsLocked();
  void applySpectralEnablesLocked();
  void unpauseLocked();

  mutable std::mutex mutex_;
  std::unique_ptr<juce::AudioFormatManager> formatManager_;
  std::array<std::unique_ptr<juce::AudioParameterBool>, 9> enablePars_;
  std::array<juce::AudioParameterBool*, 9> enableParsRaw_{};
  std::unique_ptr<StretchAudioSource> stretch_;
  std::unique_ptr<juce::TimeSliceThread> bufferThread_;
  std::unique_ptr<MyBufferingAudioSource> buffered_;
  std::unique_ptr<ProcessParameters> processPars_;
  std::shared_ptr<breakpoint_envelope> freeFilterEnvelope_;
  std::unique_ptr<juce::AudioBuffer<float>> processBuffer_;

  double sampleRate_{44100.0};
  int maxBlockSize_{512};
  int prebufferSamples_{65536};
  std::string sourcePath_;
  std::atomic<bool> prepared_{false};

  float mainVolumeDb_{0.0f};
  float stretchAmount_{2.0f};
  float pitchShiftCents_{0.0f};
  float freqShiftHz_{0.0f};
  float fftSizeNorm_{0.35f};  // ~2k FFT — audible without huge latency
  float spread_{0.0f};
  float filterLow_{0.0f};
  float filterHigh_{22000.0f};
  float onset_{0.0f};
  float soundStart_{0.0f};
  float soundEnd_{1.0f};
  bool freeze_{false};
  bool paused_{false};
  std::array<bool, 9> spectralEnable_{
      false, false, false, false, false, false, false, false, false};
};

}  // namespace rumor
