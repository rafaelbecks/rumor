#pragma once

#include <atomic>
#include <cmath>

namespace rumor {

/**
 * Physical L/R outs as mono amp channels (Sub / Dayton), plus per-engine
 * level/balance so Granular and PaulXStretch can be routed independently.
 */
class OutputRouter {
 public:
  void setBalance(float v) { balance_.store(clamp(v, -1.0f, 1.0f)); }
  float balance() const { return balance_.load(); }

  void setGainL(float v) { gainL_.store(clamp(v, 0.0f, 1.0f)); }
  void setGainR(float v) { gainR_.store(clamp(v, 0.0f, 1.0f)); }
  float gainL() const { return gainL_.load(); }
  float gainR() const { return gainR_.load(); }

  void setSwap(bool s) { swap_.store(s); }
  bool swap() const { return swap_.load(); }

  void setMonoSum(bool m) { monoSum_.store(m); }
  bool monoSum() const { return monoSum_.load(); }

  void setGranLevel(float v) { granLevel_.store(clamp(v, 0.0f, 1.0f)); }
  void setGranBalance(float v) { granBalance_.store(clamp(v, -1.0f, 1.0f)); }
  void setPxLevel(float v) { pxLevel_.store(clamp(v, 0.0f, 1.0f)); }
  void setPxBalance(float v) { pxBalance_.store(clamp(v, -1.0f, 1.0f)); }

  float granLevel() const { return granLevel_.load(); }
  float granBalance() const { return granBalance_.load(); }
  float pxLevel() const { return pxLevel_.load(); }
  float pxBalance() const { return pxBalance_.load(); }

  /** Constant-power pan gains for balance -1..+1. */
  static void panGains(float balanceMinus1To1, float& outL, float& outR) {
    constexpr float kPiOver4 = 0.7853981633974483f;
    const float t = (clamp(balanceMinus1To1, -1.0f, 1.0f) + 1.0f) * kPiOver4;
    outL = std::cos(t);
    outR = std::sin(t);
  }

  /** Mix one engine bus into accumulating L/R with level + balance. */
  void mixEngine(const float* inL, const float* inR, float* accL, float* accR, int numFrames,
                 float level, float balance, bool hasRightOut) const {
    if (accL == nullptr || inL == nullptr || numFrames <= 0 || level <= 0.0f) return;
    float panL = 0.0f, panR = 0.0f;
    panGains(balance, panL, panR);
    const float gL = level * panL;
    const float gR = level * panR;
    const bool mono = monoSum_.load();

    for (int i = 0; i < numFrames; ++i) {
      const float l = inL[i];
      const float r = inR != nullptr ? inR[i] : l;
      float src = mono ? (0.5f * (l + r)) : l;
      float srcR = mono ? src : r;
      accL[i] += src * gL;
      if (hasRightOut && accR != nullptr) accR[i] += srcR * gR;
    }
  }

  /** Apply master gain L/R (+ optional swap) in place. */
  void applyMaster(float* ioL, float* ioR, int numFrames, bool hasRightOut) const {
    if (ioL == nullptr || numFrames <= 0) return;
    float gL = gainL_.load();
    float gR = gainR_.load();
    if (swap_.load()) {
      const float tmp = gL;
      gL = gR;
      gR = tmp;
    }
    // Master balance tilts the summed bus.
    float panL = 1.0f, panR = 1.0f;
    panGains(balance_.load(), panL, panR);
    gL *= panL;
    gR *= panR;

    for (int i = 0; i < numFrames; ++i) {
      ioL[i] *= gL;
      if (hasRightOut && ioR != nullptr) ioR[i] *= gR;
    }
  }

  /** Legacy single-bus apply (kept for tests / simple paths). */
  void apply(const float* inL, const float* inR, float* outL, float* outR, int numFrames,
             bool hasRightOut) const {
    if (outL == nullptr || numFrames <= 0) return;
    for (int i = 0; i < numFrames; ++i) {
      outL[i] = 0.0f;
      if (hasRightOut && outR != nullptr) outR[i] = 0.0f;
    }
    mixEngine(inL, inR, outL, outR, numFrames, 1.0f, 0.0f, hasRightOut);
    applyMaster(outL, outR, numFrames, hasRightOut);
  }

 private:
  static float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }

  std::atomic<float> balance_{0.0f};
  std::atomic<float> gainL_{1.0f};
  std::atomic<float> gainR_{1.0f};
  std::atomic<bool> swap_{false};
  std::atomic<bool> monoSum_{true};
  std::atomic<float> granLevel_{1.0f};
  std::atomic<float> granBalance_{0.0f};
  std::atomic<float> pxLevel_{1.0f};
  std::atomic<float> pxBalance_{0.0f};
};

}  // namespace rumor
