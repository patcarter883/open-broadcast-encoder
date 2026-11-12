// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

// One vocabulary for the raw-OBS listener's sockets, which are the only sockets
// this application opens. POSIX on Unix, Winsock on Windows: the names differ
// (close/closesocket, errno/WSA error code, poll/WSAPoll) but the shape does
// not.

#if defined(_WIN32)

#  include <winsock2.h>
#  include <ws2tcpip.h>

#  ifndef POLLIN
#    define POLLIN 0x0100
#  endif

namespace obc_socket
{
using handle = SOCKET;
using poll_descriptor = WSAPOLLFD;
inline constexpr handle invalid_handle = INVALID_SOCKET;

inline void start_up()
{
  static const int started = []() -> int
  {
    WSADATA data {};
    return ::WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 1 : 0;
  }();
  (void)started;
}

inline int poll(poll_descriptor* fds, unsigned long count, int timeout_ms)
{
  return ::WSAPoll(fds, count, timeout_ms);
}

// A send that never raises SIGPIPE: a dropped client goes into the return
// value, everywhere, instead of killing the process on one platform.
inline constexpr int no_signal = 0;

inline void set_send_timeout_ms(handle h, int ms)
{
  const DWORD timeout = static_cast<DWORD>(ms);
  ::setsockopt(h,
               SOL_SOCKET,
               SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
}

inline void close_handle(handle h)
{
  ::closesocket(h);
}

// EINTR has no Windows equivalent: a Winsock call is not interrupted by a
// signal.
inline bool interrupted()
{
  return false;
}

inline int last_error()
{
  return ::WSAGetLastError();
}
}  // namespace obc_socket

#else

#  include <cerrno>

#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>

namespace obc_socket
{
using handle = int;
using poll_descriptor = pollfd;
inline constexpr handle invalid_handle = -1;

inline void start_up() {}

inline int poll(poll_descriptor* fds, unsigned long count, int timeout_ms)
{
  return ::poll(fds, static_cast<nfds_t>(count), timeout_ms);
}

// A send that never raises SIGPIPE: a dropped client goes into the return
// value, everywhere, instead of killing the process on one platform.
inline constexpr int no_signal = MSG_NOSIGNAL;

inline void set_send_timeout_ms(handle h, int ms)
{
  timeval timeout {};
  timeout.tv_sec = ms / 1000;
  timeout.tv_usec = (ms % 1000) * 1000;
  ::setsockopt(h, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

inline void close_handle(handle h)
{
  ::close(h);
}

inline bool interrupted()
{
  return errno == EINTR;
}

inline int last_error()
{
  return errno;
}
}  // namespace obc_socket

#endif
