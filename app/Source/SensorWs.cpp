#include "SensorWs.h"
#include "RemoteTls.h"

#include <cstring>
#include <vector>

#if JUCE_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace {

struct WsUrl {
  juce::String host;
  int port{80};
  juce::String path{"/"};
  bool secure{false};
};

bool parseSensorUrl(juce::String text, WsUrl& out, juce::String& error) {
  text = text.trim();
  if (text.startsWithIgnoreCase("http://")) text = "ws://" + text.substring(7);
  else if (text.startsWithIgnoreCase("https://")) text = "wss://" + text.substring(8);
  if (!text.contains("://")) text = "ws://" + text;
  const bool secure = text.startsWithIgnoreCase("wss://");
  if (!secure && !text.startsWithIgnoreCase("ws://")) {
    error = "websocket: url invalida";
    return false;
  }

  auto rest = text.fromFirstOccurrenceOf("://", false, false);
  const int slash = rest.indexOfChar('/');
  const int query = rest.indexOfChar('?');
  juce::String hostport;
  juce::String path;
  if (slash < 0 && query < 0) {
    hostport = rest;
    path = "/";
  } else if (query >= 0 && (slash < 0 || query < slash)) {
    hostport = rest.substring(0, query);
    path = "/" + rest.substring(query);
  } else {
    hostport = rest.substring(0, slash);
    path = rest.substring(slash);
    if (path.isEmpty()) path = "/";
  }

  const int colon = hostport.lastIndexOfChar(':');
  if (colon > 0) {
    out.host = hostport.substring(0, colon);
    out.port = hostport.substring(colon + 1).getIntValue();
  } else {
    out.host = hostport;
    out.port = secure ? 443 : 80;
  }
  out.secure = secure;
  if (out.host.isEmpty() || out.port <= 0 || out.port > 65535) {
    error = "websocket: url invalida";
    return false;
  }
  out.path = path;
  return true;
}

int sendRaw(juce::StreamingSocket& socket, const void* data, int size) {
  const auto* bytes = static_cast<const char*>(data);
  int sent = 0;
  while (sent < size) {
#if JUCE_WINDOWS
    const int wrote = socket.write(bytes + sent, size - sent);
#else
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    const int wrote = static_cast<int>(
        ::send(socket.getRawSocketHandle(), bytes + sent, static_cast<size_t>(size - sent), flags));
#endif
    if (wrote <= 0) return -1;
    sent += wrote;
  }
  return sent;
}

void harden(juce::StreamingSocket& socket) {
  const int fd = socket.getRawSocketHandle();
  if (fd < 0) return;
  const int one = 1;
  const auto* opt = reinterpret_cast<const char*>(&one);
#ifdef SO_NOSIGPIPE
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, opt, sizeof(one));
#endif
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, opt, static_cast<socklen_t>(sizeof(one)));
}

struct Io {
  juce::StreamingSocket* tcp{nullptr};
  TlsSession* tls{nullptr};

  int write(const void* data, int size) const {
    if (tls != nullptr) return tls->write(data, size);
    return sendRaw(*tcp, data, size);
  }

  int read(void* dst, int size, int timeoutMs) const {
    if (tls != nullptr) return tls->read(dst, size, timeoutMs);
    const int ready = tcp->waitUntilReady(true, timeoutMs);
    if (ready < 0) return -1;
    if (ready == 0) return 0;
    const int got = tcp->read(dst, size, false);
    return got <= 0 ? -1 : got;
  }
};

bool sendFrame(Io& io, uint8_t opcode, const void* data, size_t size) {
  uint8_t header[14];
  size_t n = 0;
  header[n++] = static_cast<uint8_t>(0x80 | opcode);
  if (size < 126) {
    header[n++] = static_cast<uint8_t>(0x80 | size);
  } else if (size <= 65535) {
    header[n++] = 0x80 | 126;
    header[n++] = static_cast<uint8_t>((size >> 8) & 0xff);
    header[n++] = static_cast<uint8_t>(size & 0xff);
  } else {
    header[n++] = 0x80 | 127;
    for (int shift = 56; shift >= 0; shift -= 8)
      header[n++] = static_cast<uint8_t>((size >> shift) & 0xff);
  }
  uint8_t mask[4];
  auto& rng = juce::Random::getSystemRandom();
  for (auto& byte : mask) byte = static_cast<uint8_t>(rng.nextInt(256));
  std::memcpy(header + n, mask, 4);
  n += 4;
  if (io.write(header, static_cast<int>(n)) < 0) return false;

  std::vector<uint8_t> masked(size);
  const auto* src = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) masked[i] = static_cast<uint8_t>(src[i] ^ mask[i & 3]);
  return size == 0 || io.write(masked.data(), static_cast<int>(size)) >= 0;
}

bool readSome(Io& io, std::vector<uint8_t>& buf, int timeoutMs) {
  uint8_t chunk[4096];
  const int got = io.read(chunk, static_cast<int>(sizeof(chunk)), timeoutMs);
  if (got < 0) return false;
  if (got == 0) return true;
  buf.insert(buf.end(), chunk, chunk + got);
  return buf.size() <= (1u << 20);
}

int takeFrame(std::vector<uint8_t>& buf, uint8_t& opcode, bool& fin, std::vector<uint8_t>& payload) {
  if (buf.size() < 2) return 0;
  fin = (buf[0] & 0x80) != 0;
  opcode = static_cast<uint8_t>(buf[0] & 0x0f);
  const bool masked = (buf[1] & 0x80) != 0;
  uint64_t len = buf[1] & 0x7f;
  size_t header = 2;
  if (len == 126) {
    if (buf.size() < 4) return 0;
    len = (static_cast<uint64_t>(buf[2]) << 8) | buf[3];
    header = 4;
  } else if (len == 127) {
    if (buf.size() < 10) return 0;
    len = 0;
    for (int i = 0; i < 8; ++i) len = (len << 8) | buf[static_cast<size_t>(2 + i)];
    header = 10;
  }
  if (len > (1u << 20)) return -1;
  const size_t maskLen = masked ? 4 : 0;
  if (buf.size() < header + maskLen + static_cast<size_t>(len)) return 0;
  payload.resize(static_cast<size_t>(len));
  const uint8_t* src = buf.data() + header + maskLen;
  if (masked) {
    const uint8_t* mask = buf.data() + header;
    for (size_t i = 0; i < payload.size(); ++i)
      payload[i] = static_cast<uint8_t>(src[i] ^ mask[i & 3]);
  } else if (!payload.empty()) {
    std::memcpy(payload.data(), src, payload.size());
  }
  buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(header + maskLen + payload.size()));
  return 1;
}

bool handshake(Io& io, const WsUrl& url, std::vector<uint8_t>& leftover) {
  uint8_t key[16];
  auto& rng = juce::Random::getSystemRandom();
  for (auto& byte : key) byte = static_cast<uint8_t>(rng.nextInt(256));
  const bool defaultPort = url.secure ? url.port == 443 : url.port == 80;
  const auto hostHeader = defaultPort ? url.host : url.host + ":" + juce::String(url.port);
  juce::String request;
  request << "GET " << url.path << " HTTP/1.1\r\n"
          << "Host: " << hostHeader << "\r\n"
          << "Upgrade: websocket\r\n"
          << "Connection: Upgrade\r\n"
          << "Sec-WebSocket-Key: " << juce::Base64::toBase64(key, sizeof(key)) << "\r\n"
          << "Sec-WebSocket-Version: 13\r\n\r\n";
  if (io.write(request.toRawUTF8(), static_cast<int>(request.getNumBytesAsUTF8())) < 0) return false;

  std::vector<uint8_t> buf;
  const auto deadline = juce::Time::getMillisecondCounter() + 3000;
  while (juce::Time::getMillisecondCounter() < deadline) {
    if (!readSome(io, buf, 200)) return false;
    const std::string text(buf.begin(), buf.end());
    const auto end = text.find("\r\n\r\n");
    if (end == std::string::npos) continue;
    const auto statusLine = text.substr(0, text.find("\r\n"));
    const size_t headerBytes = end + 4;
    if (headerBytes < buf.size()) leftover.assign(buf.begin() + static_cast<std::ptrdiff_t>(headerBytes), buf.end());
    return statusLine.find(" 101 ") != std::string::npos;
  }
  return false;
}

void appendNumber(const juce::String& key, float value, const juce::String& shown,
                  std::vector<SensorWs::Sample>& values, juce::String& pretty) {
  values.emplace_back(key.toStdString(), value);
  pretty << key << ": " << shown << "\n";
}

void flattenJson(const juce::var& value, const juce::String& prefix, std::vector<SensorWs::Sample>& values,
                 juce::String& pretty) {
  if (auto* obj = value.getDynamicObject()) {
    const auto& props = obj->getProperties();
    for (int i = 0; i < props.size(); ++i) {
      const auto name = props.getName(i).toString();
      const auto key = prefix.isEmpty() ? name : prefix + "." + name;
      flattenJson(props.getValueAt(i), key, values, pretty);
    }
    return;
  }
  if (auto* arr = value.getArray()) {
    bool objects = !arr->isEmpty();
    for (const auto& item : *arr)
      if (!item.isObject()) objects = false;
    if (objects) {
      for (int i = 0; i < arr->size(); ++i)
        flattenJson(arr->getReference(i), prefix + "." + juce::String(i), values, pretty);
      return;
    }
    if (prefix.isNotEmpty()) pretty << prefix << ": " << juce::JSON::toString(value) << "\n";
    return;
  }
  if (prefix.isEmpty()) return;
  if (value.isBool()) {
    const bool on = static_cast<bool>(value);
    appendNumber(prefix, on ? 1.0f : 0.0f, on ? "true" : "false", values, pretty);
    return;
  }
  if (value.isDouble() || value.isInt() || value.isInt64()) {
    const float number = static_cast<float>(static_cast<double>(value));
    appendNumber(prefix, number, juce::String(number, 4), values, pretty);
    return;
  }
  pretty << prefix << ": " << value.toString() << "\n";
}

}  // namespace

SensorWs::SensorWs() : juce::Thread("RumorSensorWs") {}

SensorWs::~SensorWs() { stop(); }

void SensorWs::start(ValuesFn onValues, ConnectFn onConnection) {
  onValues_ = std::move(onValues);
  onConnection_ = std::move(onConnection);
  if (!isThreadRunning()) startThread();
}

void SensorWs::stop() {
  signalThreadShouldExit();
  epoch_.fetch_add(1);
  stopThread(4000);
  connected_.store(false);
}

void SensorWs::setUrl(const juce::String& url) {
  {
    const juce::ScopedLock lock(urlLock_);
    url_ = url.trim();
  }
  epoch_.fetch_add(1);
  notify();
}

juce::String SensorWs::status() const {
  const juce::ScopedLock lock(statusLock_);
  return status_;
}

void SensorWs::run() {
  auto setStatus = [this](const juce::String& text) {
    const juce::ScopedLock lock(statusLock_);
    status_ = text;
  };

  while (!threadShouldExit()) {
    juce::String url;
    {
      const juce::ScopedLock lock(urlLock_);
      url = url_;
    }
    if (url.isEmpty()) {
      if (connected_.exchange(false) && onConnection_) onConnection_(false);
      setStatus({});
      wait(400);
      continue;
    }

    const auto epoch = epoch_.load();
    WsUrl parsed;
    juce::String error;
    if (!parseSensorUrl(url, parsed, error)) {
      if (connected_.exchange(false) && onConnection_) onConnection_(false);
      setStatus(error);
      while (!threadShouldExit() && epoch_.load() == epoch) wait(400);
      continue;
    }

    setStatus("websocket: conectando");
    juce::StreamingSocket socket;
    const bool opened = socket.connect(parsed.host, parsed.port, 3000);
    if (!opened || threadShouldExit() || epoch_.load() != epoch) {
      if (connected_.exchange(false) && onConnection_) onConnection_(false);
      socket.close();
      if (epoch_.load() == epoch) {
        setStatus("websocket: no se pudo conectar");
        wait(1000);
      }
      continue;
    }

    harden(socket);
    TlsSession tls;
    Io io{&socket, nullptr};
    if (parsed.secure) {
      if (!tls.handshake(socket.getRawSocketHandle(), parsed.host, 8000) || epoch_.load() != epoch) {
        tls.close();
        socket.close();
        if (connected_.exchange(false) && onConnection_) onConnection_(false);
        if (epoch_.load() == epoch) {
          setStatus("websocket: tls fallo");
          wait(1000);
        }
        continue;
      }
      io.tls = &tls;
    }

    std::vector<uint8_t> buf;
    if (!handshake(io, parsed, buf) || epoch_.load() != epoch) {
      tls.close();
      socket.close();
      if (connected_.exchange(false) && onConnection_) onConnection_(false);
      if (epoch_.load() == epoch) {
        setStatus("websocket: no se pudo conectar");
        wait(1000);
      }
      continue;
    }

    connected_.store(true);
    setStatus("websocket: en vivo");
    if (onConnection_) onConnection_(true);

    std::vector<uint8_t> frag;
    uint8_t fragOp = 0;
    auto nextPing = juce::Time::getMillisecondCounter() + 15000;
    bool drop = false;

    while (!drop && !threadShouldExit() && epoch_.load() == epoch && socket.isConnected()) {
      if (!readSome(io, buf, 80)) break;

      uint8_t opcode = 0;
      bool fin = false;
      std::vector<uint8_t> payload;
      for (;;) {
        const int got = takeFrame(buf, opcode, fin, payload);
        if (got == 0) break;
        if (got < 0) {
          drop = true;
          break;
        }
        if (opcode == 0x9) {
          if (!sendFrame(io, 0xA, payload.data(), payload.size())) drop = true;
          continue;
        }
        if (opcode == 0x8) {
          drop = true;
          break;
        }
        if (opcode != 0x1 && opcode != 0x0) continue;
        if (opcode == 0x1) {
          frag.clear();
          fragOp = 0x1;
        }
        frag.insert(frag.end(), payload.begin(), payload.end());
        if (frag.size() > (1u << 20)) {
          drop = true;
          break;
        }
        if (!fin || fragOp != 0x1 || !onValues_) continue;
        const juce::String text(reinterpret_cast<const char*>(frag.data()), frag.size());
        frag.clear();
        const auto parsedJson = juce::JSON::parse(text);
        std::vector<Sample> values;
        juce::String pretty;
        if (parsedJson.isObject() || parsedJson.isArray())
          flattenJson(parsedJson, {}, values, pretty);
        if (pretty.isEmpty()) pretty = text.substring(0, 2000);
        onValues_(values, pretty);
      }

      const auto now = juce::Time::getMillisecondCounter();
      if (static_cast<uint32_t>(now - nextPing) < 60000 && now >= nextPing) {
        if (!sendFrame(io, 0x9, nullptr, 0)) break;
        nextPing = now + 15000;
      }
    }

    const bool was = connected_.exchange(false);
    tls.close();
    socket.close();
    if (was && onConnection_) onConnection_(false);
    if (!threadShouldExit() && epoch_.load() == epoch) {
      setStatus("websocket: reconectando");
      wait(500);
    }
  }
  connected_.store(false);
}
