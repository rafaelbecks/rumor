#include "engines/paulxstretch/PaulXStretchEngine.h"

#include "PS_Source/StretchSource.h"
#include "PS_Source/ProcessedStretch.h"
#include "PS_Source/ps3_BufferingAudioSource.h"
#include "jcdp_envelope.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>

namespace rumor {
namespace {

int fftSizeFromNorm(float norm01) {
  // Matches PaulXStretch PluginProcessor::setFFTSize mapping (non-huge prebuffer).
  const double raw = std::pow(2.0, 7.0 + static_cast<double>(norm01) * 10.0);
  int size = static_cast<int>(raw);
  int pot = 128;
  while (pot < size && pot < 65536) pot <<= 1;
  return pot;
}

void silence(float* left, float* right, int numFrames) {
  for (int i = 0; i < numFrames; ++i) {
    left[i] = 0.0f;
    if (right != nullptr) right[i] = 0.0f;
  }
}

}  // namespace

PaulXStretchEngine::PaulXStretchEngine() {
  formatManager_ = std::make_unique<juce::AudioFormatManager>();
  formatManager_->registerBasicFormats();
  processPars_ = std::make_unique<ProcessParameters>();
  processBuffer_ = std::make_unique<juce::AudioBuffer<float>>(2, 512);
  bufferThread_ = std::make_unique<juce::TimeSliceThread>("rumor-px-prebuffer");

  const char* names[9] = {
      "enab_harmonics", "enab_octaves",     "enab_freqshift",
      "enab_pitchshift", "enab_spread",     "enab_filter",
      "enab_freefilter", "enab_ratiomix",   "enab_compressor",
  };
  // All spectral modules off by default — enable from the PX panel as needed.
  const bool defaults[9] = {false, false, false, false, false, false, false, false, false};
  for (int i = 0; i < 9; ++i) {
    spectralEnable_[static_cast<size_t>(i)] = defaults[i];
    enablePars_[static_cast<size_t>(i)] = std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{names[i], 1}, names[i], defaults[i]);
    enableParsRaw_[static_cast<size_t>(i)] = enablePars_[static_cast<size_t>(i)].get();
  }
}

PaulXStretchEngine::~PaulXStretchEngine() { release(); }

void PaulXStretchEngine::ensureFreeFilterEnvelopeLocked() {
  if (freeFilterEnvelope_ != nullptr) return;
  freeFilterEnvelope_ = std::make_shared<breakpoint_envelope>();
  freeFilterEnvelope_->SetName("Free filter");
  freeFilterEnvelope_->AddNode({0.0, 0.75});
  freeFilterEnvelope_->AddNode({1.0, 0.75});
  freeFilterEnvelope_->set_reset_nodes(freeFilterEnvelope_->get_all_nodes());
  freeFilterEnvelope_->m_transform_y_random_rate = 0.0;
}

void PaulXStretchEngine::ensureStretchLocked() {
  if (stretch_ != nullptr) return;
  ensureFreeFilterEnvelopeLocked();
  stretch_ = std::make_unique<StretchAudioSource>(2, formatManager_.get(), enableParsRaw_);
  stretch_->setLoopingEnabled(true);
  stretch_->setFFTWindowingType(1);
  stretch_->setOnsetDetection(0.0);
  stretch_->setFreeFilterEnvelope(freeFilterEnvelope_);
  pushProcessParamsLocked();
  applySpectralEnablesLocked();
}

void PaulXStretchEngine::ensureBufferingLocked() {
  if (stretch_ == nullptr) return;
  if (bufferThread_ == nullptr)
    bufferThread_ = std::make_unique<juce::TimeSliceThread>("rumor-px-prebuffer");
  if (!bufferThread_->isThreadRunning())
    bufferThread_->startThread(juce::Thread::Priority::high);

  // Recreate so a new file / prepare gets a fresh ring.
  if (buffered_ != nullptr) {
    buffered_->releaseResources();
    buffered_.reset();
  }
  buffered_ = std::make_unique<MyBufferingAudioSource>(
      stretch_.get(), *bufferThread_, false, prebufferSamples_, 2, false);
  stretch_->m_prebuffersize = prebufferSamples_;
  buffered_->prepareToPlay(maxBlockSize_, sampleRate_);
  buffered_->setNextReadPosition(0);
}

void PaulXStretchEngine::applySpectralEnablesLocked() {
  if (stretch_ == nullptr) return;
  for (int i = 0; i < 9; ++i) {
    const bool on = spectralEnable_[static_cast<size_t>(i)];
    if (auto* p = enableParsRaw_[static_cast<size_t>(i)]) *p = on;
  }
}

void PaulXStretchEngine::unpauseLocked() {
  paused_ = false;
  if (stretch_ == nullptr) return;
  // Stretch starts paused (state 2). Drive the 2→3→0 unpause machine.
  if (stretch_->isPaused()) stretch_->setPaused(false);
}

void PaulXStretchEngine::pushProcessParamsLocked() {
  if (stretch_ == nullptr || processPars_ == nullptr) return;
  processPars_->pitch_shift.cents = static_cast<int>(std::lround(pitchShiftCents_));
  processPars_->freq_shift.Hz = static_cast<int>(std::lround(freqShiftHz_));
  processPars_->spread.bandwidth = spread_;
  processPars_->filter.low = filterLow_;
  processPars_->filter.high = filterHigh_;
  stretch_->setProcessParameters(processPars_.get(), nullptr);
  stretch_->setRate(static_cast<double>(stretchAmount_));
  stretch_->setMainVolume(static_cast<double>(mainVolumeDb_));
  stretch_->setFreezing(freeze_);
  stretch_->setOnsetDetection(static_cast<double>(onset_));
  stretch_->setPlayRange({static_cast<double>(soundStart_), static_cast<double>(soundEnd_)});
  stretch_->setFFTSize(fftSizeFromNorm(fftSizeNorm_));
  if (freeFilterEnvelope_ != nullptr)
    stretch_->setFreeFilterEnvelope(freeFilterEnvelope_);
  if (paused_)
    stretch_->setPaused(true);
  else
    unpauseLocked();
}

void PaulXStretchEngine::prepare(double sampleRate, int maxBlockSize) {
  std::lock_guard<std::mutex> lock(mutex_);
  sampleRate_ = sampleRate;
  maxBlockSize_ = maxBlockSize;
  if (processBuffer_ == nullptr || processBuffer_->getNumSamples() < maxBlockSize)
    processBuffer_ = std::make_unique<juce::AudioBuffer<float>>(2, juce::jmax(512, maxBlockSize));
  ensureStretchLocked();
  // Stretch prepare happens inside buffering prepareToPlay.
  ensureBufferingLocked();
  prepared_.store(true);
}

void PaulXStretchEngine::process(float* left, float* right, int numFrames) {
  if (!prepared_.load() || left == nullptr) return;

  std::unique_lock<std::mutex> lock(mutex_);
  if (buffered_ == nullptr || stretch_ == nullptr || sourcePath_.empty()) {
    lock.unlock();
    silence(left, right, numFrames);
    return;
  }

  if (processBuffer_ == nullptr || processBuffer_->getNumSamples() < numFrames)
    processBuffer_ = std::make_unique<juce::AudioBuffer<float>>(2, numFrames);

  processBuffer_->setSize(2, numFrames, false, false, true);
  processBuffer_->clear();
  juce::AudioSourceChannelInfo info(processBuffer_.get(), 0, numFrames);
  // Pull from prebuffer (cheap); FFT stretch runs on bufferThread_.
  buffered_->getNextAudioBlock(info);
  lock.unlock();

  for (int i = 0; i < numFrames; ++i) {
    left[i] = processBuffer_->getSample(0, i);
    if (right != nullptr)
      right[i] = processBuffer_->getNumChannels() > 1 ? processBuffer_->getSample(1, i) : left[i];
  }
}

void PaulXStretchEngine::release() {
  std::lock_guard<std::mutex> lock(mutex_);
  prepared_.store(false);
  if (buffered_ != nullptr) {
    buffered_->releaseResources();
    buffered_.reset();
  }
  if (bufferThread_ != nullptr && bufferThread_->isThreadRunning())
    bufferThread_->stopThread(3000);
  if (stretch_ != nullptr) stretch_->releaseResources();
}

void PaulXStretchEngine::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stretch_ != nullptr) stretch_->seekPercent(static_cast<double>(soundStart_));
  if (buffered_ != nullptr) buffered_->setNextReadPosition(0);
}

bool PaulXStretchEngine::loadSourceFile(const std::string& path, std::string& errorOut) {
  std::lock_guard<std::mutex> lock(mutex_);
  ensureStretchLocked();
  const juce::String err = stretch_->setAudioFile(juce::URL{juce::File{path}});
  if (err.isNotEmpty()) {
    errorOut = err.toStdString();
    sourcePath_.clear();
    return false;
  }
  sourcePath_ = path;
  stretch_->setLoopingEnabled(true);
  pushProcessParamsLocked();
  unpauseLocked();
  // Rebuild prebuffer so the new file starts filling immediately.
  if (prepared_.load() || sampleRate_ > 0.0) ensureBufferingLocked();
  prepared_.store(true);
  errorOut.clear();
  return true;
}

void PaulXStretchEngine::clearSource() {
  std::lock_guard<std::mutex> lock(mutex_);
  sourcePath_.clear();
  paused_ = true;
  if (stretch_ != nullptr) stretch_->setPaused(true);
  if (buffered_ != nullptr) buffered_->setNextReadPosition(0);
}

std::string PaulXStretchEngine::sourceFilePath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sourcePath_;
}

bool PaulXStretchEngine::hasSource() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !sourcePath_.empty();
}

double PaulXStretchEngine::playheadPercent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stretch_ == nullptr) return 0.0;
  return stretch_->getInfilePositionPercent();
}

double PaulXStretchEngine::prebufferReady() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (buffered_ == nullptr) return 0.0;
  return buffered_->getPercentReady();
}

std::vector<EngineParameterInfo> PaulXStretchEngine::parameters() const {
  return {
      {"px.main_volume", "Main volume (dB)", -24.0f, 12.0f, 0.0f, true},
      {"px.stretch", "Stretch amount", 0.1f, 1024.0f, 2.0f, true},
      {"px.fft_size", "FFT size (norm)", 0.0f, 1.0f, 0.35f, true},
      {"px.pitch_shift", "Pitch shift (cents)", -2400.0f, 2400.0f, 0.0f, true},
      {"px.freq_shift", "Frequency shift (Hz)", -1000.0f, 1000.0f, 0.0f, true},
      {"px.spread", "Frequency spread", 0.0f, 1.0f, 0.0f, true},
      {"px.filter_low", "Filter low (Hz)", 0.0f, 22000.0f, 0.0f, true},
      {"px.filter_high", "Filter high (Hz)", 0.0f, 22000.0f, 22000.0f, true},
      {"px.onset", "Onset detection", 0.0f, 1.0f, 0.0f, true},
      {"px.sound_start", "Sound start", 0.0f, 1.0f, 0.0f, true},
      {"px.sound_end", "Sound end", 0.0f, 1.0f, 1.0f, true},
      {"px.freeze", "Freeze", 0.0f, 1.0f, 0.0f, false},
      {"px.pause", "Pause", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_harmonics", "Harmonics", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_octaves", "Octaves", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_freqshift", "Freq shift", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_pitchshift", "Pitch shift", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_spread", "Spread", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_filter", "Filter", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_freefilter", "Free filter", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_ratiomix", "Ratio mix", 0.0f, 1.0f, 0.0f, false},
      {"px.enab_compressor", "Compressor", 0.0f, 1.0f, 0.0f, false},
  };
}

bool PaulXStretchEngine::setParameter(std::string_view id, float value) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto setBool = [&](std::string_view key, size_t index) -> bool {
    if (id != key) return false;
    spectralEnable_[index] = value >= 0.5f;
    applySpectralEnablesLocked();
    return true;
  };

  if (id == "px.main_volume")
    mainVolumeDb_ = juce::jlimit(-24.0f, 12.0f, value);
  else if (id == "px.stretch")
    stretchAmount_ = juce::jlimit(0.1f, 1024.0f, value);
  else if (id == "px.fft_size")
    fftSizeNorm_ = juce::jlimit(0.0f, 1.0f, value);
  else if (id == "px.pitch_shift")
    pitchShiftCents_ = juce::jlimit(-2400.0f, 2400.0f, value);
  else if (id == "px.freq_shift")
    freqShiftHz_ = juce::jlimit(-1000.0f, 1000.0f, value);
  else if (id == "px.spread")
    spread_ = juce::jlimit(0.0f, 1.0f, value);
  else if (id == "px.filter_low")
    filterLow_ = juce::jlimit(0.0f, 22000.0f, value);
  else if (id == "px.filter_high")
    filterHigh_ = juce::jlimit(0.0f, 22000.0f, value);
  else if (id == "px.onset")
    onset_ = juce::jlimit(0.0f, 1.0f, value);
  else if (id == "px.sound_start")
    soundStart_ = juce::jlimit(0.0f, 1.0f, value);
  else if (id == "px.sound_end")
    soundEnd_ = juce::jlimit(0.0f, 1.0f, value);
  else if (id == "px.freeze")
    freeze_ = value >= 0.5f;
  else if (id == "px.pause") {
    paused_ = value >= 0.5f;
    if (stretch_ != nullptr) {
      if (paused_)
        stretch_->setPaused(true);
      else
        unpauseLocked();
    }
    return true;
  } else if (setBool("px.enab_harmonics", 0) || setBool("px.enab_octaves", 1) ||
             setBool("px.enab_freqshift", 2) || setBool("px.enab_pitchshift", 3) ||
             setBool("px.enab_spread", 4) || setBool("px.enab_filter", 5) ||
             setBool("px.enab_freefilter", 6) || setBool("px.enab_ratiomix", 7) ||
             setBool("px.enab_compressor", 8))
    return true;
  else
    return false;

  if (soundEnd_ < soundStart_) std::swap(soundEnd_, soundStart_);
  if (stretch_ != nullptr) pushProcessParamsLocked();
  return true;
}

bool PaulXStretchEngine::getParameter(std::string_view id, float& valueOut) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (id == "px.main_volume")
    valueOut = mainVolumeDb_;
  else if (id == "px.stretch")
    valueOut = stretchAmount_;
  else if (id == "px.fft_size")
    valueOut = fftSizeNorm_;
  else if (id == "px.pitch_shift")
    valueOut = pitchShiftCents_;
  else if (id == "px.freq_shift")
    valueOut = freqShiftHz_;
  else if (id == "px.spread")
    valueOut = spread_;
  else if (id == "px.filter_low")
    valueOut = filterLow_;
  else if (id == "px.filter_high")
    valueOut = filterHigh_;
  else if (id == "px.onset")
    valueOut = onset_;
  else if (id == "px.sound_start")
    valueOut = soundStart_;
  else if (id == "px.sound_end")
    valueOut = soundEnd_;
  else if (id == "px.freeze")
    valueOut = freeze_ ? 1.0f : 0.0f;
  else if (id == "px.pause")
    valueOut = paused_ ? 1.0f : 0.0f;
  else if (id == "px.enab_harmonics")
    valueOut = spectralEnable_[0] ? 1.0f : 0.0f;
  else if (id == "px.enab_octaves")
    valueOut = spectralEnable_[1] ? 1.0f : 0.0f;
  else if (id == "px.enab_freqshift")
    valueOut = spectralEnable_[2] ? 1.0f : 0.0f;
  else if (id == "px.enab_pitchshift")
    valueOut = spectralEnable_[3] ? 1.0f : 0.0f;
  else if (id == "px.enab_spread")
    valueOut = spectralEnable_[4] ? 1.0f : 0.0f;
  else if (id == "px.enab_filter")
    valueOut = spectralEnable_[5] ? 1.0f : 0.0f;
  else if (id == "px.enab_freefilter")
    valueOut = spectralEnable_[6] ? 1.0f : 0.0f;
  else if (id == "px.enab_ratiomix")
    valueOut = spectralEnable_[7] ? 1.0f : 0.0f;
  else if (id == "px.enab_compressor")
    valueOut = spectralEnable_[8] ? 1.0f : 0.0f;
  else
    return false;
  return true;
}

}  // namespace rumor
