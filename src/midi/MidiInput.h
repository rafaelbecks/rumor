#pragma once

#include "midi/MidiMessage.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace glow {

class MidiInput {
 public:
  using Callback = std::function<void(const MidiMessage&)>;

  MidiInput();
  ~MidiInput();

  MidiInput(const MidiInput&) = delete;
  MidiInput& operator=(const MidiInput&) = delete;

  static std::vector<std::string> listDevices();

  /** Open by substring match (empty = first port). Returns false on failure. */
  bool open(const std::string& deviceSubstring, Callback cb);

  void close();
  bool isOpen() const { return open_; }
  const std::string& deviceName() const { return deviceName_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool open_{false};
  std::string deviceName_;
};

}  // namespace glow
