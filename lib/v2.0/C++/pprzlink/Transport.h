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

#ifndef PPRZLINKCPP_TRANSPORT_H
#define PPRZLINKCPP_TRANSPORT_H

#include "Message.h"
#include "Device.h"
#include "MessageDictionary.h"
#include <memory>
#include <stdexcept>
#include <utility>

namespace pprzlink {
  /// Owns its device exclusively; the borrowed dictionary must outlive the transport.
  /// Calls on the same transport must be serialized by the application.
  class Transport {
  public:
    explicit Transport(std::unique_ptr<Device> device, const MessageDictionary &dictionary)
      : device(std::move(device)), dictionary(dictionary)
    {
      if (!this->device) throw std::invalid_argument("Transport requires a device");
    }

    virtual ~Transport() = default;
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;

    virtual bool hasMessage() = 0;

    virtual std::unique_ptr<Message> getMessage() = 0;

    /// Write a complete message and return the number of bytes sent, or throw.
    virtual size_t sendMessage(const Message &msg) = 0;

    /// Borrow the device until transport destruction; never delete it or transfer ownership.
    [[nodiscard]] Device& getDevice() noexcept { return *device; }
    [[nodiscard]] const Device& getDevice() const noexcept { return *device; }

  protected:
    std::unique_ptr<Device> device;
    const MessageDictionary &dictionary;
  };
}
#endif // PPRZLINKCPP_TRANSPORT_H
