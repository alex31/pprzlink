// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <pprzlink/Device.h>
#include <string>

namespace pprzlink {
  /// Own an O_RDWR file/FIFO descriptor without applying serial-port settings.
  /// Poll from one thread. EOF and terminal errors are reported after buffered data.
  class PosixFileDevice final : public Device {
  public:
    explicit PosixFileDevice(const std::string &path);
    ~PosixFileDevice() override;
    PosixFileDevice(const PosixFileDevice &) = delete;
    PosixFileDevice &operator=(const PosixFileDevice &) = delete;
    size_t availableBytes() override;
    BytesBuffer readAll() override;
    void writeBuffer(const BytesBuffer &data) override;

  private:
    void readAvailable();
    int descriptor;
    BytesBuffer received;
    bool endOfFile = false;
    int receiveError = 0;
  };
}
