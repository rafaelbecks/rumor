#include "control/SerialPitch.h"

#include "parameters/ParameterRegistry.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>

#if defined(_WIN32)
#include <cstdio>
#else
#include <fcntl.h>
#include <filesystem>
#include <termios.h>
#include <unistd.h>
#endif

namespace glow {
namespace {

#if defined(_WIN32)

DWORD baudToWinBaud(int baud, bool& ok) {
  ok = true;
  switch (baud) {
    case 9600:
      return CBR_9600;
    case 19200:
      return CBR_19200;
    case 38400:
      return CBR_38400;
    case 57600:
      return CBR_57600;
    case 115200:
      return CBR_115200;
    default:
      ok = false;
      return CBR_115200;
  }
}

/** Win32 needs \\.\COMx for CreateFile (required for COM10+). */
std::string winSerialDevicePath(const std::string& path) {
  if (path.rfind("\\\\.\\", 0) == 0) return path;
  return "\\\\.\\" + path;
}

#else

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

#endif

}  // namespace

std::vector<std::string> listSerialPorts() {
  std::vector<std::string> ports;
#if defined(_WIN32)
  char target[256];
  for (int i = 1; i <= 256; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "COM%d", i);
    if (QueryDosDeviceA(name, target, static_cast<DWORD>(sizeof(target))) != 0)
      ports.push_back(name);
  }
#else
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
    if (ec) break;
    const std::string name = entry.path().filename().string();
    if (looksLikeSerialPort(name)) ports.push_back(entry.path().string());
  }
#endif
  std::sort(ports.begin(), ports.end());
  return ports;
}

SerialPitch::~SerialPitch() { close(); }

bool SerialPitch::open(const std::string& path, int baud) {
  close();

#if defined(_WIN32)
  bool baudOk = false;
  const DWORD winBaud = baudToWinBaud(baud, baudOk);
  if (!baudOk) {
    std::cerr << "Unsupported serial baud " << baud
              << " (use 9600, 19200, 38400, 57600, or 115200)\n";
    return false;
  }

  const std::string device = winSerialDevicePath(path);
  HANDLE h = CreateFileA(device.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    std::cerr << "Serial open failed (" << path << "): Win32 error " << GetLastError() << "\n";
    return false;
  }

  DCB dcb{};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(h, &dcb)) {
    std::cerr << "Serial GetCommState failed (" << path << "): " << GetLastError() << "\n";
    CloseHandle(h);
    return false;
  }
  dcb.BaudRate = winBaud;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  if (!SetCommState(h, &dcb)) {
    std::cerr << "Serial SetCommState failed (" << path << "): " << GetLastError() << "\n";
    CloseHandle(h);
    return false;
  }

  COMMTIMEOUTS timeouts{};
  // Non-blocking-ish: return immediately with whatever is available.
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 0;
  SetCommTimeouts(h, &timeouts);
  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

  handle_ = h;
#else
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
#endif

  baud_ = baud;
  path_ = path;
  running_.store(true);
  thread_ = std::thread([this] { readLoop(); });
  return true;
}

void SerialPitch::close() {
  running_.store(false);
  if (thread_.joinable()) thread_.join();
#if defined(_WIN32)
  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
#else
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
#endif
}

void SerialPitch::processIncomingBytes(const char* buf, int n, SerialAdcParser& parser,
                                       float& smoothed, int& lastLoggedNote) {
  for (int i = 0; i < n; ++i) {
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
      const float mapped = serialMap(smoothed, inputLow_, inputHigh_, outLow, outHigh);
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
        if (route.destination == SerialDestination::MidiCc) std::cout << "[" << route.midiCc << "]";
        std::cout << " " << mapped;
      }
    }
    if (debug_) std::cout << std::endl;

    if (!debug_ && verbose_ && havePitch) {
      const int semis = static_cast<int>(pitchNote + 0.5f);
      if (semis != lastLoggedNote) {
        lastLoggedNote = semis;
        std::cout << "[serial] adc " << static_cast<int>(smoothed + 0.5f) << " → MIDI " << semis
                  << "\n";
      }
    }
  }
}

void SerialPitch::readLoop() {
  SerialAdcParser parser;
  float smoothed = -1.0f;
  int lastLoggedNote = -1;
  char buf[64];

  while (running_.load(std::memory_order_relaxed)) {
#if defined(_WIN32)
    DWORD n = 0;
    if (ReadFile(handle_, buf, static_cast<DWORD>(sizeof(buf)), &n, nullptr) && n > 0) {
      processIncomingBytes(buf, static_cast<int>(n), parser, smoothed, lastLoggedNote);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
#else
    const ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n > 0) {
      processIncomingBytes(buf, static_cast<int>(n), parser, smoothed, lastLoggedNote);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
#endif
  }
}

}  // namespace glow
