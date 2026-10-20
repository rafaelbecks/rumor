#pragma once

#include <JuceHeader.h>

/** TLS session over an already-connected TCP socket. */
class TlsSession {
 public:
  TlsSession();
  ~TlsSession();

  bool handshake(int fd, const juce::String& host, int timeoutMs);
  /** >0 bytes, 0 timeout, -1 closed or error. */
  int read(void* dst, int size, int timeoutMs);
  /** Writes the whole buffer, or -1. */
  int write(const void* data, int size);
  void close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TlsSession)
};
