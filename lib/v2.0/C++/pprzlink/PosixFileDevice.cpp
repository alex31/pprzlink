// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file PosixFileDevice.cpp
 * @brief Bounded file/FIFO polling and complete descriptor writes.
 * @ingroup transports
 *
 * Buffered input is delivered before EOF or read errors. Nonblocking writes wait for readiness with a bounded timeout and fail on incomplete delivery.
 */

#include "PosixFileDevice.h"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace pprzlink {
  PosixFileDevice::PosixFileDevice(const std::string &path)
    : descriptor(::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC))
  {
    if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "open " + path);
  }

  PosixFileDevice::~PosixFileDevice() { ::close(descriptor); }

  void PosixFileDevice::readAvailable()
  {
    if (receiveError) {
      if (received.empty()) throw std::system_error(receiveError, std::generic_category(), "read link device");
      return;
    }
    if (endOfFile) {
      if (received.empty()) throw std::runtime_error("End of file on link device");
      return;
    }
    std::array<uint8_t, 4096> bytes;
    // Bound each poll so a continuously writing peer cannot starve timers.
    while (received.size() < 65536) {
      const auto count = ::read(descriptor, bytes.data(), bytes.size());
      if (count > 0) received.insert(received.end(), bytes.begin(), bytes.begin() + count);
      else if (count == 0) { endOfFile = true; break; }
      else if (errno == EINTR) continue;
      else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      else {
        receiveError = errno;
        if (received.empty()) throw std::system_error(receiveError, std::generic_category(), "read link device");
        break;
      }
    }
  }

  size_t PosixFileDevice::availableBytes() { readAvailable(); return received.size(); }
  BytesBuffer PosixFileDevice::readAll() { readAvailable(); return std::exchange(received, {}); }

  void PosixFileDevice::writeBuffer(const BytesBuffer &bytes)
  {
    size_t offset = 0;
    while (offset < bytes.size()) {
      const auto count = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
      if (count > 0) offset += static_cast<size_t>(count);
      else if (count < 0 && errno == EINTR) continue;
      else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        pollfd waiting{descriptor, POLLOUT, 0};
        int ready;
        do { ready = ::poll(&waiting, 1, 1000); } while (ready < 0 && errno == EINTR);
        if (ready < 0) throw std::system_error(errno, std::generic_category(), "poll link device");
        if (ready == 0 || (waiting.revents & (POLLERR | POLLHUP | POLLNVAL)))
          throw std::runtime_error("Link device cannot complete the write");
      } else throw std::system_error(count == 0 ? EIO : errno, std::generic_category(), "write link device");
    }
  }
}
