#include "audio/Synth.h"

#include "util/MidiMath.h"
#include "util/Scale.h"

namespace glow {
namespace {

constexpr int kChordDegreeOrder[Synth::kMaxChordExtras] = {2, -2, 4, -4, 6, -6};
/** Harmonic partial multipliers as chord spread opens (2f … 7f). */
constexpr int kHarmonicPartials[Synth::kMaxChordExtras] = {2, 3, 4, 5, 6, 7};

}  // namespace

void Synth::prepare(float sampleRate, int maxGrains) {
  sampleRate_ = sampleRate;
  osc_.setSampleRate(sampleRate);
  for (auto& o : chordOsc_) o.setSampleRate(sampleRate);
  env_.setSampleRate(sampleRate);
  filter_.setSampleRate(sampleRate);
  granular_.prepare(sampleRate, maxGrains);
  limiter_.setCeiling(0.95f);
}

void Synth::noteOn(uint8_t note, uint8_t velocity) {
  currentNote_ = note;
  velocity_ = velocity / 127.0f;
  fundamentalHz_ = midi_math::midiNoteToFrequency(static_cast<float>(note));
  osc_.reset();
  for (auto& o : chordOsc_) o.reset();
  env_.noteOn(velocity_);
  if (performance_) performance_->setRoot(static_cast<int>(note));
}

void Synth::noteOff(uint8_t note) {
  if (currentNote_ == static_cast<int>(note) || currentNote_ < 0) {
    env_.noteOff();
    if (currentNote_ == static_cast<int>(note)) currentNote_ = -1;
  }
}

void Synth::drainNotes() {
  if (!noteQueue_) return;
  NoteEvent ev;
  while (noteQueue_->pop(ev)) {
    if (ev.type == NoteEvent::Type::NoteOn) {
      noteOn(ev.note, ev.velocity);
    } else {
      noteOff(ev.note);
    }
  }
}

void Synth::pullParameters() {
  if (!registry_) return;
  registry_->tickAll();

  if (performance_ != nullptr && performance_->serialPitchActive()) {
    float note = performance_->serialMidi();
    const ScaleId serialScale = performance_->scale();
    if (serialScale != ScaleId::Off && serialScale != ScaleId::Harmonics) {
      note = quantizeMidiToScale(note, performance_->baseRoot(), serialScale);
    }
    fundamentalHz_ = midi_math::midiNoteToFrequency(note);
    performance_->setRoot(static_cast<int>(note + 0.5f));
  }

  const float cents = registry_->value(ParameterID::PitchOffset);
  float freq = midi_math::frequencyWithCentsOffset(fundamentalHz_, cents);
  freq = midi_math::clamp(freq, 20.0f, 8000.0f);
  centerFreq_ = freq;
  osc_.setFrequency(freq);

  const float wavePos = registry_->value(ParameterID::Waveform);
  const float sineTimbre = registry_->value(ParameterID::SineTimbre);
  waveform_ = waveformFromParam(wavePos);
  osc_.setWaveformPosition(wavePos);
  osc_.setSineTimbre(sineTimbre);
  for (auto& o : chordOsc_) {
    o.setWaveformPosition(wavePos);
    o.setSineTimbre(sineTimbre);
  }

  chordSpread_ = registry_->value(ParameterID::ChordSpread);

  ScaleId scale = ScaleId::Major;
  if (performance_ != nullptr) {
    const ScaleId s = performance_->scale();
    if (s != ScaleId::Off) scale = s;
  }
  const bool harmonics = (scale == ScaleId::Harmonics);

  const float richness = chordSpread_ * static_cast<float>(kMaxChordExtras);
  float activeWeight = 1.0f;
  for (int i = 0; i < kMaxChordExtras; ++i) {
    const float amp = midi_math::clamp(richness - static_cast<float>(i), 0.0f, 1.0f);
    chordAmp_[static_cast<size_t>(i)] = amp;
    activeWeight += amp;

    float voiceHz = centerFreq_;
    if (harmonics) {
      voiceHz = centerFreq_ * static_cast<float>(kHarmonicPartials[i]);
    } else {
      const int semis = scaleDegreeToSemitones(kChordDegreeOrder[i], scale);
      voiceHz =
          midi_math::frequencyWithCentsOffset(centerFreq_, static_cast<float>(semis) * 100.0f);
    }
    voiceHz = midi_math::clamp(voiceHz, 20.0f, 8000.0f);
    chordOsc_[static_cast<size_t>(i)].setFrequency(voiceHz);
  }
  chordNorm_ = 1.0f / activeWeight;

  filter_.setCutoff(registry_->value(ParameterID::FilterCutoff));
  filter_.setResonance(registry_->value(ParameterID::FilterResonance));

  folder_.setDrive(registry_->value(ParameterID::FolderDrive));
  folder_.setSymmetry(registry_->value(ParameterID::FolderSymmetry));
  folder_.setMix(registry_->value(ParameterID::FolderMix));

  env_.setAttack(registry_->value(ParameterID::Attack));
  env_.setRelease(registry_->value(ParameterID::Release));

  granular_.setSize(registry_->value(ParameterID::GrainSize));
  granular_.setDensity(registry_->value(ParameterID::GrainDensity));
  granular_.setPosition(registry_->value(ParameterID::GrainPosition));
  granular_.setSpray(registry_->value(ParameterID::GrainSpray));
  granular_.setPitch(registry_->value(ParameterID::GrainPitch));
  granular_.setMix(registry_->value(ParameterID::GrainMix));
  amplitude_ = registry_->value(ParameterID::Amplitude);
}

void Synth::process(float* outL, float* outR, int numFrames) {
  drainNotes();
  pullParameters();

  for (int i = 0; i < numFrames; ++i) {
    const float env = env_.process();
    float sample = osc_.process();
    for (int v = 0; v < kMaxChordExtras; ++v) {
      const float a = chordAmp_[static_cast<size_t>(v)];
      if (a > 0.0001f) {
        sample += chordOsc_[static_cast<size_t>(v)].process() * a;
      }
    }
    sample *= chordNorm_;
    sample *= env * 0.55f;

    sample = folder_.process(sample);
    sample = filter_.process(sample);
    sample = granular_.process(sample);

    const float out = limiter_.process(sample) * amplitude_;
    outL[i] = out;
    outR[i] = out;
  }
}

}  // namespace glow
