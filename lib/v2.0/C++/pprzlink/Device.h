/*
 * Copyright 2019 garciafa
 * This file is part of PprzLinkCPP
 *
 * PprzLinkCPP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PprzLinkCPP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with ModemTester.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef PPRZLINKCPP_DEVICE_H
#define PPRZLINKCPP_DEVICE_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace pprzlink {

  using BytesBuffer = std::vector<uint8_t>;

  /// Byte stream used by a transport. Implementations define their threading contract.
  class Device {
  public:
    virtual ~Device() = default;
    virtual size_t availableBytes() = 0;

    /// Consume the bytes currently available; an empty buffer means no data yet.
    virtual BytesBuffer readAll() = 0;

    /// Write the entire buffer or throw. On failure some bytes may already be sent.
    virtual void writeBuffer(const BytesBuffer &data) = 0;
  };

  /// Byte stream whose UART rate can be changed during modem initialization.
  class SerialDevice : public Device {
  public:
    /// Set the host baud rate and discard buffered/in-flight input from the old rate.
    /// The caller must own the dialogue and drain any stale input before sending commands.
    virtual void resetBaudrate(unsigned int baudrate) = 0;
  };
}
#endif // PPRZLINKCPP_DEVICE_H
