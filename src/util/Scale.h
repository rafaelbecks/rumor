#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace glow {

enum class ScaleId : uint8_t {
  Off = 0,
  Chromatic,
  Major,
  Minor,
  HarmonicMinor,
  MelodicMinor,
  Pentatonic,
  MinorPentatonic,
  Mixolydian,
  Dorian,
  Phrygian,
  Lydian,
  Locrian,
  Blues,
  WholeTone,
  Harmonics,  // chord spread = integer partials (2f, 3f, …); pitch unquantized
  Count
};

struct ScaleDef {
  ScaleId id;
  const char* name;
  const char* alias;
  const int* degrees;
  int degreeCount;
};

inline const int kDegChromatic[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
inline const int kDegMajor[] = {0, 2, 4, 5, 7, 9, 11};
inline const int kDegMinor[] = {0, 2, 3, 5, 7, 8, 10};
inline const int kDegHarmMinor[] = {0, 2, 3, 5, 7, 8, 11};
inline const int kDegMelMinor[] = {0, 2, 3, 5, 7, 9, 11};
inline const int kDegPenta[] = {0, 2, 4, 7, 9};
inline const int kDegMinPenta[] = {0, 3, 5, 7, 10};
inline const int kDegMixo[] = {0, 2, 4, 5, 7, 9, 10};
inline const int kDegDorian[] = {0, 2, 3, 5, 7, 9, 10};
inline const int kDegPhryg[] = {0, 1, 3, 5, 7, 8, 10};
inline const int kDegLydian[] = {0, 2, 4, 6, 7, 9, 11};
inline const int kDegLocrian[] = {0, 1, 3, 5, 6, 8, 10};
inline const int kDegBlues[] = {0, 3, 5, 6, 7, 10};
inline const int kDegWhole[] = {0, 2, 4, 6, 8, 10};

inline const ScaleDef kScales[] = {
    {ScaleId::Chromatic, "chromatic", "chroma", kDegChromatic, 12},
    {ScaleId::Major, "major", "ionian", kDegMajor, 7},
    {ScaleId::Minor, "minor", "aeolian", kDegMinor, 7},
    {ScaleId::HarmonicMinor, "harmonic-minor", "harmminor", kDegHarmMinor, 7},
    {ScaleId::MelodicMinor, "melodic-minor", "melminor", kDegMelMinor, 7},
    {ScaleId::Pentatonic, "pentatonic", "penta", kDegPenta, 5},
    {ScaleId::MinorPentatonic, "minor-pentatonic", "minpenta", kDegMinPenta, 5},
    {ScaleId::Mixolydian, "mixolydian", "mixo", kDegMixo, 7},
    {ScaleId::Dorian, "dorian", nullptr, kDegDorian, 7},
    {ScaleId::Phrygian, "phrygian", nullptr, kDegPhryg, 7},
    {ScaleId::Lydian, "lydian", nullptr, kDegLydian, 7},
    {ScaleId::Locrian, "locrian", nullptr, kDegLocrian, 7},
    {ScaleId::Blues, "blues", nullptr, kDegBlues, 6},
    {ScaleId::WholeTone, "whole-tone", "wholetone", kDegWhole, 6},
};

inline constexpr size_t kScaleCount = sizeof(kScales) / sizeof(kScales[0]);

inline const char* scaleName(ScaleId id) {
  if (id == ScaleId::Off) return "off";
  if (id == ScaleId::Harmonics) return "harmonics";
  for (const auto& s : kScales) {
    if (s.id == id) return s.name;
  }
  return "off";
}

inline bool scaleFromName(std::string_view name, ScaleId& out) {
  if (name.empty() || name == "off" || name == "none") {
    out = ScaleId::Off;
    return true;
  }
  if (name == "harmonics" || name == "harmonic" || name == "partials") {
    out = ScaleId::Harmonics;
    return true;
  }
  if (name == "mixyolidian" || name == "mixolidian") {
    out = ScaleId::Mixolydian;
    return true;
  }
  if (name == "pent" || name == "majpent") {
    out = ScaleId::Pentatonic;
    return true;
  }
  for (const auto& s : kScales) {
    if (name == s.name) {
      out = s.id;
      return true;
    }
    if (s.alias != nullptr && name == s.alias) {
      out = s.id;
      return true;
    }
  }
  return false;
}

inline ScaleId nextScale(ScaleId current) {
  // Cycle musical scales (skip Off): … → last → Harmonics → Major.
  if (current == ScaleId::Off || current == ScaleId::Count) {
    return ScaleId::Major;
  }
  if (current == ScaleId::Harmonics) {
    return ScaleId::Major;
  }
  int idx = -1;
  for (size_t i = 0; i < kScaleCount; ++i) {
    if (kScales[i].id == current) {
      idx = static_cast<int>(i);
      break;
    }
  }
  if (idx < 0) return ScaleId::Major;
  if (idx + 1 >= static_cast<int>(kScaleCount)) return ScaleId::Harmonics;
  return kScales[static_cast<size_t>(idx + 1)].id;
}

inline const ScaleDef* scaleDef(ScaleId id) {
  for (const auto& s : kScales) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

/**
 * Map a scale-degree index (0 = root, ±1 = next degree up/down, can span octaves)
 * to a semitone offset from the root.
 */
inline int scaleDegreeToSemitones(int degreeIndex, ScaleId scale) {
  const ScaleDef* def = scaleDef(scale);
  // Fall back to major when quantize is off so chords still make sense
  if (def == nullptr) def = scaleDef(ScaleId::Major);
  if (def == nullptr || def->degreeCount <= 0) return 0;

  const int n = def->degreeCount;
  int oct = static_cast<int>(std::floor(static_cast<double>(degreeIndex) / static_cast<double>(n)));
  int idx = degreeIndex - oct * n;
  while (idx < 0) {
    idx += n;
    --oct;
  }
  while (idx >= n) {
    idx -= n;
    ++oct;
  }
  return oct * 12 + def->degrees[idx];
}

/**
 * Snap cents offset (relative to rootMidi) onto the nearest scale degree.
 * Root is the drone / last MIDI note; degrees are semitone steps from that root.
 */
inline float quantizeCentsToScale(float cents, int rootMidi, ScaleId scale) {
  if (scale == ScaleId::Off || scale == ScaleId::Harmonics) return cents;
  const ScaleDef* def = scaleDef(scale);
  if (def == nullptr || def->degreeCount <= 0) return cents;

  const float absolute = static_cast<float>(rootMidi) + cents / 100.0f;
  float best = absolute;
  float bestDist = 1e9f;

  for (int oct = -2; oct <= 2; ++oct) {
    for (int i = 0; i < def->degreeCount; ++i) {
      const float candidate =
          static_cast<float>(rootMidi + def->degrees[i] + oct * 12);
      const float dist = std::fabs(candidate - absolute);
      if (dist < bestDist) {
        bestDist = dist;
        best = candidate;
      }
    }
  }

  return (best - static_cast<float>(rootMidi)) * 100.0f;
}

/**
 * Snap an absolute MIDI note onto the nearest degree of `scale`,
 * using `rootMidi` as the scale root (any octave).
 */
inline float quantizeMidiToScale(float midiNote, int rootMidi, ScaleId scale) {
  if (scale == ScaleId::Off || scale == ScaleId::Harmonics) return midiNote;
  const ScaleDef* def = scaleDef(scale);
  if (def == nullptr || def->degreeCount <= 0) return midiNote;

  const int rootClass = ((rootMidi % 12) + 12) % 12;
  float best = midiNote;
  float bestDist = 1e9f;
  for (int oct = -1; oct <= 10; ++oct) {
    for (int i = 0; i < def->degreeCount; ++i) {
      const float candidate =
          static_cast<float>(oct * 12 + rootClass + def->degrees[i]);
      if (candidate < 0.0f || candidate > 127.0f) continue;
      const float dist = std::fabs(candidate - midiNote);
      if (dist <= bestDist) {
        bestDist = dist;
        best = candidate;
      }
    }
  }
  return best;
}

}  // namespace glow
