#include "control/SerialPitch.h"

#include "parameters/ParameterRegistry.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace glow {
namespace {

speed_t baudToSpeed(int baud, bool& ok) {
  ok = true;
  switch (baud) {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    default:
      ok = false;
      return B115200;
  }
}

bool looksLikeSerialPort(const std::string& name) {
  return name.rfind("cu.usb", 0) == 0 || name.rfind("cu.wch", 0) == 0 ||
         name.rfind("cu.SLAB", 0) == 0 || name.rfind("ttyACM", 0) == 0 ||
         name.rfind("ttyUSB", 0) == 0;
}

}  // namespace

std::vector<std::string> listSerialPorts() {
  std::vector<std::string> ports;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
    if (ec) break;
    const std::string name = entry.path().filename().string();
    if (looksLikeSerialPort(name)) ports.push_back(entry.path().string());
  }
  std::sort(ports.begin(), ports.end());
  return ports;
}

SerialPitch::~SerialPitch() { close(); }

bool SerialPitch::open(const std::string& path, int baud) {
  close();

  bool baudOk = false;
  const speed_t speed = baudToSpeed(baud, baudOk);
  if (!baudOk) {
    std::cerr << "Unsupported serial baud " << baud
              << " (use 9600, 19200, 38400, 57600, or 115200)\n";
    return false;
  }

  const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    std::cerr << "Serial open failed (" << path << "): " << std::strerror(errno) << "\n";
    return false;
  }

  termios tty{};
  if (tcgetattr(fd, &tty) != 0) {
    std::cerr << "Serial tcgetattr failed (" << path << "): " << std::strerror(errno) << "\n";
    ::close(fd);
    return false;
  }

  cfmakeraw(&tty);
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
  tty.c_cflag &= ~CRTSCTS;
#endif
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    std::cerr << "Serial tcsetattr failed (" << path << "): " << std::strerror(errno) << "\n";
    ::close(fd);
    return false;
  }
  tcflush(fd, TCIOFLUSH);

  fd_ = fd;
  baud_ = baud;
  path_ = path;
  running_.store(true);
  thread_ = std::thread([this] { readLoop(); });
  return true;
}

void SerialPitch::close() {
  running_.store(false);
  if (thread_.joinable()) thread_.join();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void SerialPitch::readLoop() {
  SerialAdcParser parser;
  float smoothed = -1.0f;
  int lastLoggedNote = -1;
  char buf[64];

  while (running_.load(std::memory_order_relaxed)) {
    const ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n > 0) {
      for (ssize_t i = 0; i < n; ++i) {
        int adc = 0;
        if (!parser.feed(buf[i], adc)) continue;

        if (smoothed < 0.0f) {
          smoothed = static_cast<float>(adc);
        } else {
          smoothed += 0.2f * (static_cast<float>(adc) - smoothed);
        }

        float pitchNote = 0.0f;
        bool havePitch = false;
        lastRawAdc_.store(adc, std::memory_order_relaxed);
        lastSmoothedAdc_.store(smoothed, std::memory_order_relaxed);
        readingSeq_.fetch_add(1, std::memory_order_relaxed);
        if (onReading_) onReading_(adc, smoothed);

        if (debug_) std::cout << "[serial] " << adc << " →";
        for (const SerialRoute& route : routes_) {
          float outLow = route.min;
          float outHigh = route.max;
          if (route.invert) std::swap(outLow, outHigh);
          const float mapped =
              serialMap(smoothed, inputLow_, inputHigh_, outLow, outHigh);
          if (route.destination == SerialDestination::Pitch) {
            if (performance_ != nullptr) performance_->setSerialMidi(mapped);
            pitchNote = mapped;
            havePitch = true;
          } else if (route.destination == SerialDestination::MidiCc) {
            if (onMidiCc_) onMidiCc_(route.midiCc, mapped);
          } else if (serialDestinationIsPaulXStretch(route.destination)) {
            if (onEngineParam_) onEngineParam_(serialDestinationName(route.destination), mapped);
          } else {
            ParameterID paramId = ParameterID::Count;
            if (serialDestinationToParameter(route.destination, paramId) && registry_ != nullptr)
              registry_->setTarget(paramId, mapped);
          }
          if (debug_) {
            std::cout << " " << serialDestinationName(route.destination);
            if (route.destination == SerialDestination::MidiCc)
              std::cout << "[" << route.midiCc << "]";
            std::cout << " " << mapped;
          }
        }
        if (debug_) std::cout << std::endl;

        if (!debug_ && verbose_ && havePitch) {
          const int semis = static_cast<int>(pitchNote + 0.5f);
          if (semis != lastLoggedNote) {
            lastLoggedNote = semis;
            std::cout << "[serial] adc " << static_cast<int>(smoothed + 0.5f) << " → MIDI "
                      << semis << "\n";
          }
        }
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
}

}  // namespace glow
