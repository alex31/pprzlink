// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file PosixFileDevice.h
 * @brief Single-threaded file and FIFO byte-stream adapter.
 * @ingroup transports
 *
 * The owned descriptor is nonblocking and is opened for reading and writing without UART configuration.
 */

#pragma once
#include <pprzlink/Device.h>
#include <string>

namespace pprzlink {
  /// Own an O_RDWR file/FIFO descriptor without applying serial-port settings.
  /// Poll from one thread. EOF and terminal errors are reported after buffered data.
  /// @ingroup transports
  class PosixFileDevice final : public Device {
  public:
    /// @brief Open an owned O_RDWR, O_NONBLOCK, O_CLOEXEC descriptor.
    /// @param[in] path File, FIFO or pseudo-terminal path; no serial options are applied.
    /// @throws std::system_error Opening the descriptor fails.
    explicit PosixFileDevice(const std::string &path);
    /// @brief Close the owned descriptor.
    ~PosixFileDevice() override;
    /// @brief Copying an exclusively owned descriptor is prohibited.
    PosixFileDevice(const PosixFileDevice &) = delete;
    /// @brief Copy assignment is prohibited.
    /// @return This operation is deleted and cannot return.
    PosixFileDevice &operator=(const PosixFileDevice &) = delete;
    /// @brief Poll the descriptor and inspect buffered bytes without consuming them.
    /// @return Number of bytes ready in the internal input buffer.
    /// @throws std::system_error A read failure, after buffered data is exhausted.
    /// @throws std::runtime_error EOF, after buffered data is exhausted.
    size_t availableBytes() override;
    /// @brief Poll and drain the input buffer.
    /// @return Owned currently available input; empty when no new input is ready.
    /// @throws std::exception Deferred EOF or read failure after buffered data is exhausted.
    BytesBuffer readAll() override;
    /// @brief Complete a descriptor write, waiting up to one second per blocked segment.
    /// @param[in] data Bytes delivered in order; a failure may follow a partial external write.
    /// @throws std::system_error Write or readiness polling fails.
    /// @throws std::runtime_error Readiness times out or the descriptor cannot complete a write.
    void writeBuffer(const BytesBuffer &data) override;

  private:
    void readAvailable();
    int descriptor;
    BytesBuffer received;
    bool endOfFile = false;
    int receiveError = 0;
  };
}
