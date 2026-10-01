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

/**
 * @file Device.h
 * @brief Byte-stream and reconfigurable UART contracts.
 * @ingroup transports
 *
 * A transport owns its device. Writes must complete or throw; availableBytes and readAll expose only data already available to the implementation.
 */

#ifndef PPRZLINKCPP_DEVICE_H
#define PPRZLINKCPP_DEVICE_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace pprzlink {

  /// @brief Owned sequence of raw bytes used by devices and codecs.
  /// @ingroup transports
  using BytesBuffer = std::vector<uint8_t>;

  /// Byte stream used by a transport. Implementations define their threading contract.
  /// @ingroup transports
  class Device {
  public:
    /// @brief Release the device and any implementation-owned I/O resources.
    virtual ~Device() = default;
    /// @brief Inspect already buffered input without consuming it.
    /// @return Number of bytes available to read without waiting for new input.
    /// @throws std::exception An implementation-specific I/O failure.
    virtual size_t availableBytes() = 0;

    /// Consume the bytes currently available; an empty buffer means no data yet.
    /// @return Owned bytes removed from the device's input buffer.
    /// @throws std::exception An implementation-specific I/O failure.
    virtual BytesBuffer readAll() = 0;

    /// Write the entire buffer or throw. On failure some bytes may already be sent.
    /// @param[in] data Bytes to deliver in their original order.
    /// @throws std::exception An implementation-specific I/O failure.
    virtual void writeBuffer(const BytesBuffer &data) = 0;
  };

  /// Byte stream whose UART rate can be changed during modem initialization.
  /// @ingroup transports
  class SerialDevice : public Device {
  public:
    /// Set the host baud rate and discard buffered/in-flight input from the old rate.
    /// The caller must own the dialogue and drain any stale input before sending commands.
    /// @param[in] baudrate New host UART rate, in bits per second.
    /// @throws std::exception An unsupported setting or I/O failure.
    virtual void resetBaudrate(unsigned int baudrate) = 0;
  };
}
#endif // PPRZLINKCPP_DEVICE_H
