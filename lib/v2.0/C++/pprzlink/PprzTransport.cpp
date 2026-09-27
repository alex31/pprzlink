/*
 * Copyright 2020 garciafa
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

#include "PprzTransport.h"
#include "detail/MessagePayload.h"
#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <utility>

namespace pprzlink {
  namespace {
    constexpr uint8_t startByte = PPRZ_STX;
    constexpr size_t headerSize = 6;
    constexpr size_t checksumSize = 2;
    constexpr size_t minimumFrameSize = headerSize + checksumSize;
    constexpr size_t maximumFrameSize = std::numeric_limits<uint8_t>::max();

    std::array<uint8_t, checksumSize> checksum(std::span<const uint8_t> bytes)
    {
      uint8_t a = 0, b = 0;
      for (const auto byte : bytes) {
        a += byte;
        b += a;
      }
      return {a, b};
    }

    enum class FrameStatus { Incomplete, Invalid, Complete };

    // The span starts at STX. Inspect lengths before accessing header/checksum bytes.
    FrameStatus inspectFrame(std::span<const uint8_t> bytes)
    {
      if (bytes.size() < 2) return FrameStatus::Incomplete;
      const size_t length = bytes[1];
      if (length < minimumFrameSize) return FrameStatus::Invalid;
      if (bytes.size() < length) return FrameStatus::Incomplete;
      const auto expected = checksum(bytes.subspan(1, length - checksumSize - 1));
      if (bytes[length - 2] != expected[0] || bytes[length - 1] != expected[1]) {
        return FrameStatus::Invalid;
      }
      return FrameStatus::Complete;
    }

  }

  PprzTransport::PprzTransport(std::unique_ptr<Device> device, const MessageDictionary &dictionary)
    : Transport(std::move(device), dictionary)
  {
    transportBuffer.reserve(maximumFrameSize);
  }

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

  size_t PprzTransport::sendMessage(const Message &msg)
  {
    constexpr size_t envelopeSize = 4; // STX, length and two checksum bytes.
    const auto payload = detail::encodeMessagePayload(msg, maximumFrameSize - envelopeSize);
    const auto length = static_cast<uint8_t>(envelopeSize + payload.size());
    BytesBuffer buffer{startByte, length};
    buffer.reserve(length);
    buffer.insert(buffer.end(), payload.begin(), payload.end());
    const auto check = checksum(std::span(buffer).subspan(1));
    buffer.insert(buffer.end(), check.begin(), check.end());
    device->writeBuffer(buffer);
    return buffer.size();
  }

  bool PprzTransport::decodeMessage()
  {
    const auto received = device->readAll();
    transportBuffer.insert(transportBuffer.end(), received.begin(), received.end());
    size_t cursor = 0;
    const auto discardThrough = [&](size_t end) {
      transportBuffer.erase(transportBuffer.begin(), transportBuffer.begin() + end);
    };

    while (cursor < transportBuffer.size()) {
      const auto start = std::find(transportBuffer.begin() + cursor, transportBuffer.end(), startByte);
      cursor = static_cast<size_t>(start - transportBuffer.begin());
      const auto remaining = std::span<const uint8_t>(transportBuffer).subspan(cursor);
      switch (inspectFrame(remaining)) {
        case FrameStatus::Incomplete:
          discardThrough(cursor); // Keep only the unfinished frame.
          return false;
        case FrameStatus::Invalid:
          ++cursor; // Search again without recursion or repeated buffer copies.
          continue;
        case FrameStatus::Complete:
          break;
      }

      const size_t length = remaining[1];
      // Consume a checksum-valid frame even if its definition/payload is invalid.
      // A subsequent call can then decode the next frame without exposing a partial message.
      try {
        currentMessage = std::make_unique<Message>(
            detail::decodeMessagePayload(dictionary, remaining.subspan(2, length - 4)));
      } catch (...) {
        discardThrough(cursor + length);
        throw;
      }
      discardThrough(cursor + length);
      return true;
    }
    discardThrough(cursor);
    return false;
  }
}
