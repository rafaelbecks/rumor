#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <Security/SecureTransport.h>
#endif

#include "RemoteTls.h"

#include <cstring>
#include <vector>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if JUCE_LINUX || JUCE_BSD || JUCE_ANDROID
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace {

void setNonBlock(int fd) {
#if JUCE_WINDOWS
  u_long mode = 1;
  ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode);
#else
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

bool waitFd(int fd, bool wantRead, int timeoutMs) {
#if JUCE_WINDOWS
  fd_set set;
  FD_ZERO(&set);
  FD_SET(static_cast<SOCKET>(fd), &set);
  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  const int rc = ::select(0, wantRead ? &set : nullptr, wantRead ? nullptr : &set, nullptr, &tv);
  return rc > 0;
#else
  pollfd p{};
  p.fd = fd;
  p.events = wantRead ? POLLIN : POLLOUT;
  return ::poll(&p, 1, timeoutMs) > 0;
#endif
}

bool waitEither(int fd, int timeoutMs) {
#if JUCE_WINDOWS
  fd_set r, w;
  FD_ZERO(&r);
  FD_ZERO(&w);
  FD_SET(static_cast<SOCKET>(fd), &r);
  FD_SET(static_cast<SOCKET>(fd), &w);
  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  return ::select(0, &r, &w, nullptr, &tv) > 0;
#else
  pollfd p{};
  p.fd = fd;
  p.events = POLLIN | POLLOUT;
  return ::poll(&p, 1, timeoutMs) > 0;
#endif
}

int recvSome(int fd, void* dst, int cap) {
#if JUCE_WINDOWS
  return ::recv(static_cast<SOCKET>(fd), static_cast<char*>(dst), cap, 0);
#else
  return static_cast<int>(::recv(fd, dst, static_cast<size_t>(cap), 0));
#endif
}

int sendSome(int fd, const void* src, int size) {
#if JUCE_WINDOWS
  return ::send(static_cast<SOCKET>(fd), static_cast<const char*>(src), size, 0);
#else
  int flags = 0;
#ifdef MSG_NOSIGNAL
  flags |= MSG_NOSIGNAL;
#endif
  return static_cast<int>(::send(fd, src, static_cast<size_t>(size), flags));
#endif
}

bool wouldBlock() {
#if JUCE_WINDOWS
  const int err = WSAGetLastError();
  return err == WSAEWOULDBLOCK || err == WSAEINTR;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

#if JUCE_WINDOWS
bool sendAllRaw(int fd, const void* data, int size, int timeoutMs) {
  const auto* bytes = static_cast<const char*>(data);
  int sent = 0;
  const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
  while (sent < size) {
    const int n = sendSome(fd, bytes + sent, size - sent);
    if (n > 0) {
      sent += n;
      continue;
    }
    if (n == 0 || !wouldBlock()) return false;
    const int left = static_cast<int>(deadline - juce::Time::getMillisecondCounter());
    if (left <= 0 || !waitFd(fd, false, left)) return false;
  }
  return true;
}
#endif

}  // namespace

struct TlsSession::Impl {
  int fd{-1};
  bool open{false};
#if JUCE_MAC || JUCE_IOS
  SSLContextRef ctx{nullptr};

  static OSStatus readCb(SSLConnectionRef conn, void* data, size_t* len) {
    auto* self = static_cast<Impl*>(const_cast<void*>(conn));
    const int n = recvSome(self->fd, data, static_cast<int>(*len));
    if (n > 0) {
      *len = static_cast<size_t>(n);
      return noErr;
    }
    *len = 0;
    if (n == 0) return errSSLClosedGraceful;
    if (wouldBlock()) return errSSLWouldBlock;
    return errSSLClosedAbort;
  }

  static OSStatus writeCb(SSLConnectionRef conn, const void* data, size_t* len) {
    auto* self = static_cast<Impl*>(const_cast<void*>(conn));
    const int n = sendSome(self->fd, data, static_cast<int>(*len));
    if (n > 0) {
      *len = static_cast<size_t>(n);
      return noErr;
    }
    *len = 0;
    if (wouldBlock()) return errSSLWouldBlock;
    return errSSLClosedAbort;
  }
#elif JUCE_WINDOWS
  CredHandle cred{};
  CtxtHandle ctx{};
  bool haveCred{false};
  bool haveCtx{false};
  SecPkgContext_StreamSizes sizes{};
  std::vector<char> enc;
  std::vector<char> plain;
  std::string host;
#else
  SSL_CTX* ctx{nullptr};
  SSL* ssl{nullptr};
#endif

  void shutdown() {
#if JUCE_MAC || JUCE_IOS
    if (ctx != nullptr) {
      SSLClose(ctx);
      CFRelease(ctx);
      ctx = nullptr;
    }
#elif JUCE_WINDOWS
    if (haveCtx) {
      DeleteSecurityContext(&ctx);
      haveCtx = false;
    }
    if (haveCred) {
      FreeCredentialsHandle(&cred);
      haveCred = false;
    }
    enc.clear();
    plain.clear();
#else
    if (ssl != nullptr) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
      ssl = nullptr;
    }
    if (ctx != nullptr) {
      SSL_CTX_free(ctx);
      ctx = nullptr;
    }
#endif
    open = false;
    fd = -1;
  }
};

TlsSession::TlsSession() : impl_(std::make_unique<Impl>()) {}

TlsSession::~TlsSession() { close(); }

void TlsSession::close() {
  if (impl_ != nullptr) impl_->shutdown();
}

bool TlsSession::handshake(int fd, const juce::String& host, int timeoutMs) {
  close();
  impl_->fd = fd;
  setNonBlock(fd);
  const auto hostUtf8 = host.toStdString();
  const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);

#if JUCE_MAC || JUCE_IOS
  impl_->ctx = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
  if (impl_->ctx == nullptr) return false;
  SSLSetIOFuncs(impl_->ctx, Impl::readCb, Impl::writeCb);
  SSLSetConnection(impl_->ctx, impl_.get());
  SSLSetPeerDomainName(impl_->ctx, hostUtf8.c_str(), hostUtf8.size());
  for (;;) {
    const OSStatus st = SSLHandshake(impl_->ctx);
    if (st == noErr) {
      impl_->open = true;
      return true;
    }
    if (st != errSSLWouldBlock) return false;
    const int left = static_cast<int>(deadline - juce::Time::getMillisecondCounter());
    if (left <= 0 || !waitEither(fd, left)) return false;
  }

#elif JUCE_WINDOWS
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  SCHANNEL_CRED cred{};
  cred.dwVersion = SCHANNEL_CRED_VERSION;
  cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
#ifdef SP_PROT_TLS1_3_CLIENT
  cred.grbitEnabledProtocols |= SP_PROT_TLS1_3_CLIENT;
#endif
  cred.dwFlags = SCH_USE_STRONG_CRYPTO | SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;
  TimeStamp expiry{};
  if (AcquireCredentialsHandleA(nullptr, const_cast<char*>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr,
                                &cred, nullptr, nullptr, &impl_->cred, &expiry) != SEC_E_OK)
    return false;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  impl_->haveCred = true;
  impl_->host = hostUtf8;

  const DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                      ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
  SecBuffer outBuf{};
  outBuf.BufferType = SECBUFFER_TOKEN;
  SecBufferDesc outDesc{SECBUFFER_VERSION, 1, &outBuf};
  DWORD outFlags = 0;
  auto sendToken = [&]() {
    bool ok = true;
    if (outBuf.pvBuffer != nullptr && outBuf.cbBuffer > 0)
      ok = sendAllRaw(fd, outBuf.pvBuffer, static_cast<int>(outBuf.cbBuffer), timeoutMs);
    if (outBuf.pvBuffer != nullptr) FreeContextBuffer(outBuf.pvBuffer);
    outBuf.pvBuffer = nullptr;
    outBuf.cbBuffer = 0;
    return ok;
  };

  std::vector<char> incoming;
  SECURITY_STATUS ss = InitializeSecurityContextA(&impl_->cred, nullptr, impl_->host.data(), flags, 0,
                                                  SECURITY_NATIVE_DREP, nullptr, 0, &impl_->ctx, &outDesc,
                                                  &outFlags, &expiry);
  if (ss != SEC_I_CONTINUE_NEEDED && ss != SEC_E_OK) return false;
  impl_->haveCtx = true;
  if (!sendToken()) return false;

  while (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE) {
    const int left = static_cast<int>(deadline - juce::Time::getMillisecondCounter());
    if (left <= 0) return false;
    if (ss == SEC_E_INCOMPLETE_MESSAGE || incoming.empty()) {
      if (!waitFd(fd, true, left)) return false;
      char tmp[8192];
      const int n = recvSome(fd, tmp, static_cast<int>(sizeof(tmp)));
      if (n <= 0) return false;
      incoming.insert(incoming.end(), tmp, tmp + n);
    }

    SecBuffer inBufs[2]{};
    inBufs[0].BufferType = SECBUFFER_TOKEN;
    inBufs[0].pvBuffer = incoming.data();
    inBufs[0].cbBuffer = static_cast<unsigned long>(incoming.size());
    inBufs[1].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc inDesc{SECBUFFER_VERSION, 2, inBufs};
    outBuf.BufferType = SECBUFFER_TOKEN;
    ss = InitializeSecurityContextA(&impl_->cred, &impl_->ctx, impl_->host.data(), flags, 0,
                                    SECURITY_NATIVE_DREP, &inDesc, 0, nullptr, &outDesc, &outFlags, &expiry);
    if (inBufs[1].BufferType == SECBUFFER_EXTRA && inBufs[1].cbBuffer > 0) {
      const auto keep = static_cast<size_t>(inBufs[1].cbBuffer);
      if (keep < incoming.size()) incoming.erase(incoming.begin(), incoming.end() - static_cast<std::ptrdiff_t>(keep));
    } else if (ss != SEC_E_INCOMPLETE_MESSAGE) {
      incoming.clear();
    }
    if (!sendToken()) return false;
  }
  if (ss != SEC_E_OK) return false;
  if (QueryContextAttributes(&impl_->ctx, SECPKG_ATTR_STREAM_SIZES, &impl_->sizes) != SEC_E_OK) return false;
  impl_->open = true;
  return true;

#else
  static bool ready = false;
  if (!ready) {
    SSL_library_init();
    SSL_load_error_strings();
    ready = true;
  }
  impl_->ctx = SSL_CTX_new(TLS_client_method());
  if (impl_->ctx == nullptr) return false;
  SSL_CTX_set_default_verify_paths(impl_->ctx);
  SSL_CTX_set_verify(impl_->ctx, SSL_VERIFY_PEER, nullptr);
  impl_->ssl = SSL_new(impl_->ctx);
  if (impl_->ssl == nullptr) return false;
  SSL_set_fd(impl_->ssl, fd);
  SSL_set_tlsext_host_name(impl_->ssl, hostUtf8.c_str());
  SSL_set1_host(impl_->ssl, hostUtf8.c_str());
  SSL_set_connect_state(impl_->ssl);
  for (;;) {
    const int rc = SSL_connect(impl_->ssl);
    if (rc == 1) {
      impl_->open = true;
      return true;
    }
    const int err = SSL_get_error(impl_->ssl, rc);
    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) return false;
    const int left = static_cast<int>(deadline - juce::Time::getMillisecondCounter());
    if (left <= 0 || !waitFd(fd, err == SSL_ERROR_WANT_READ, left)) return false;
  }
#endif
}

int TlsSession::read(void* dst, int size, int timeoutMs) {
  if (impl_ == nullptr || !impl_->open || size <= 0) return -1;
#if JUCE_MAC || JUCE_IOS
  size_t got = 0;
  const OSStatus st = SSLRead(impl_->ctx, dst, static_cast<size_t>(size), &got);
  if (got > 0) return static_cast<int>(got);
  if (st == errSSLWouldBlock) {
    if (timeoutMs <= 0 || !waitFd(impl_->fd, true, timeoutMs)) return 0;
    got = 0;
    const OSStatus again = SSLRead(impl_->ctx, dst, static_cast<size_t>(size), &got);
    if (got > 0) return static_cast<int>(got);
    if (again == errSSLWouldBlock) return 0;
    return -1;
  }
  return -1;
#elif JUCE_WINDOWS
  auto takePlain = [&]() -> int {
    if (impl_->plain.empty()) return 0;
    const int n = std::min(size, static_cast<int>(impl_->plain.size()));
    std::memcpy(dst, impl_->plain.data(), static_cast<size_t>(n));
    impl_->plain.erase(impl_->plain.begin(), impl_->plain.begin() + n);
    return n;
  };
  if (const int n = takePlain()) return n;

  const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
  for (;;) {
    if (!impl_->enc.empty()) {
      SecBuffer bufs[4]{};
      bufs[0].BufferType = SECBUFFER_DATA;
      bufs[0].pvBuffer = impl_->enc.data();
      bufs[0].cbBuffer = static_cast<unsigned long>(impl_->enc.size());
      for (int i = 1; i < 4; ++i) bufs[i].BufferType = SECBUFFER_EMPTY;
      SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
      const SECURITY_STATUS ss = DecryptMessage(&impl_->ctx, &desc, 0, nullptr);
      if (ss == SEC_E_INCOMPLETE_MESSAGE) {
        // Need another TCP chunk.
      } else if (ss != SEC_E_OK) {
        return -1;
      } else {
        std::vector<char> extra;
        for (auto& buf : bufs) {
          if (buf.BufferType == SECBUFFER_DATA && buf.cbBuffer > 0 && buf.pvBuffer != nullptr)
            impl_->plain.insert(impl_->plain.end(), static_cast<char*>(buf.pvBuffer),
                                static_cast<char*>(buf.pvBuffer) + buf.cbBuffer);
          if (buf.BufferType == SECBUFFER_EXTRA && buf.cbBuffer > 0 && buf.pvBuffer != nullptr)
            extra.assign(static_cast<char*>(buf.pvBuffer), static_cast<char*>(buf.pvBuffer) + buf.cbBuffer);
        }
        impl_->enc.swap(extra);
        if (const int n = takePlain()) return n;
        continue;
      }
    }
    const int left = static_cast<int>(deadline - juce::Time::getMillisecondCounter());
    if (left <= 0) return 0;
    if (!waitFd(impl_->fd, true, left)) return 0;
    char tmp[8192];
    const int n = recvSome(impl_->fd, tmp, static_cast<int>(sizeof(tmp)));
    if (n == 0) return -1;
    if (n < 0) return wouldBlock() ? 0 : -1;
    impl_->enc.insert(impl_->enc.end(), tmp, tmp + n);
  }
#else
  const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
  for (;;) {
    const int n = SSL_read(impl_->ssl, dst, size);
    if (n > 0) return n;
    const int err = SSL_get_error(impl_->ssl, n);
    if (err == SSL_ERROR_ZERO_RETURN) return -1;
    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) return -1;
    const int left = static_cast<int>(deadline - juce::Time::getMillisecondCounter());
    if (left <= 0) return 0;
    if (!waitFd(impl_->fd, err != SSL_ERROR_WANT_WRITE, left)) return 0;
  }
#endif
}

int TlsSession::write(const void* data, int size) {
  if (impl_ == nullptr || !impl_->open || size < 0) return -1;
  if (size == 0) return 0;
  const auto* bytes = static_cast<const uint8_t*>(data);
  int sent = 0;
  const auto deadline = juce::Time::getMillisecondCounter() + 8000u;

#if JUCE_MAC || JUCE_IOS
  while (sent < size) {
    size_t n = 0;
    const OSStatus st = SSLWrite(impl_->ctx, bytes + sent, static_cast<size_t>(size - sent), &n);
    n = (st == noErr || st == errSSLWouldBlock) ? n : 0;
    sent += static_cast<int>(n);
    if (st == noErr) continue;
    if (st != errSSLWouldBlock) return -1;
    const int left = static_cast<int>(deadline - juce::Time::getMillisecondCounter());
    if (left <= 0 || !waitFd(impl_->fd, false, left)) return -1;
  }
  return sent;
#elif JUCE_WINDOWS
  while (sent < size) {
    const int chunk = std::min(size - sent, static_cast<int>(impl_->sizes.cbMaximumMessage));
    std::vector<char> buf(impl_->sizes.cbHeader + static_cast<size_t>(chunk) + impl_->sizes.cbTrailer);
    std::memcpy(buf.data() + impl_->sizes.cbHeader, bytes + sent, static_cast<size_t>(chunk));
    SecBuffer bufs[4]{};
    bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
    bufs[0].pvBuffer = buf.data();
    bufs[0].cbBuffer = impl_->sizes.cbHeader;
    bufs[1].BufferType = SECBUFFER_DATA;
    bufs[1].pvBuffer = buf.data() + impl_->sizes.cbHeader;
    bufs[1].cbBuffer = static_cast<unsigned long>(chunk);
    bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
    bufs[2].pvBuffer = buf.data() + impl_->sizes.cbHeader + chunk;
    bufs[2].cbBuffer = impl_->sizes.cbTrailer;
    bufs[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
    if (EncryptMessage(&impl_->ctx, 0, &desc, 0) != SEC_E_OK) return -1;
    const int packet = static_cast<int>(bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer);
    if (!sendAllRaw(impl_->fd, buf.data(), packet, 8000)) return -1;
    sent += chunk;
  }
  return sent;
#else
  while (sent < size) {
    const int n = SSL_write(impl_->ssl, bytes + sent, size - sent);
    if (n > 0) {
      sent += n;
      continue;
    }
    const int err = SSL_get_error(impl_->ssl, n);
    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) return -1;
    const int left = static_cast<int>(deadline - juce::Time::getMillisecondCounter());
    if (left <= 0 || !waitFd(impl_->fd, err != SSL_ERROR_WANT_WRITE, left)) return -1;
  }
  return sent;
#endif
}

#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif
