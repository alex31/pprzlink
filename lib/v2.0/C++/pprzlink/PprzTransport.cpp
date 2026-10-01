// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file PprzTransport.cpp
 * @brief Polling transport wrapper around the shared PPRZ frame codec.
 * @ingroup transports
 *
 * Reception drains the device, caches at most one complete message and copies decoder statistics even when payload decoding throws.
 */

#include "PprzTransport.h"

namespace pprzlink {
  PprzTransport::PprzTransport(std::unique_ptr<Device> device, const MessageDictionary &dictionary)
    : Transport(std::move(device), dictionary), decoder(dictionary) {}

  bool PprzTransport::hasMessage()
  {
    if (!currentMessage) decodeMessage();
    return static_cast<bool>(currentMessage);
  }

  std::unique_ptr<Message> PprzTransport::getMessage()
  {
    if (!currentMessage) decodeMessage();
    return std::move(currentMessage);
  }

  size_t PprzTransport::sendMessage(const Message &message)
  {
    const auto bytes = encodePprzFrame(message);
    device->writeBuffer(bytes);
    return bytes.size();
  }

  bool PprzTransport::decodeMessage()
  {
    decoder.pushBytes(device->readAll());
    try {
      if (auto message = decoder.nextMessage()) {
        currentMessage = std::make_unique<Message>(std::move(*message));
      }
    } catch (...) {
      statistics = decoder.getStatistics();
      throw;
    }
    statistics = decoder.getStatistics();
    lastReceivedFrameSize = decoder.getLastReceivedFrameSize();
    return static_cast<bool>(currentMessage);
  }
}
