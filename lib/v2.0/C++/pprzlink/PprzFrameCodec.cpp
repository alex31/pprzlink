// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PprzFrameCodec.h"
#include "detail/MessagePayload.h"
#include <algorithm>
#include <array>
#include <limits>

namespace pprzlink {
  namespace {
    constexpr uint8_t startByte = 0x99;
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

    enum class FrameStatus { Incomplete, BadLength, BadChecksum, Complete };

    // The span starts at STX. Inspect lengths before accessing header/checksum bytes.
    FrameStatus inspectFrame(std::span<const uint8_t> bytes)
    {
      if (bytes.size() < 2) return FrameStatus::Incomplete;
      const size_t length = bytes[1];
      if (length < minimumFrameSize) return FrameStatus::BadLength;
      if (bytes.size() < length) return FrameStatus::Incomplete;
      const auto expected = checksum(bytes.subspan(1, length - checksumSize - 1));
      if (bytes[length - 2] != expected[0] || bytes[length - 1] != expected[1]) {
        return FrameStatus::BadChecksum;
      }
      return FrameStatus::Complete;
    }

  }

  BytesBuffer encodePprzFrame(const Message &msg)
  {
    constexpr size_t envelopeSize = 4; // STX, length and two checksum bytes.
    const auto payload = detail::encodeMessagePayload(msg, maximumFrameSize - envelopeSize);
    const auto length = static_cast<uint8_t>(envelopeSize + payload.size());
    BytesBuffer buffer{startByte, length};
    buffer.reserve(length);
    buffer.insert(buffer.end(), payload.begin(), payload.end());
    const auto check = checksum(std::span(buffer).subspan(1));
    buffer.insert(buffer.end(), check.begin(), check.end());
    return buffer;
  }

  void PprzFrameDecoder::pushBytes(std::span<const uint8_t> bytes)
  {
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
  }

  void PprzFrameDecoder::discardPendingInput()
  {
    statistics.discardedBytes += buffer.size();
    buffer.clear();
  }

  std::optional<Message> PprzFrameDecoder::nextMessage()
  {
    size_t cursor = 0;
    const auto discardThrough = [&](size_t end) {
      buffer.erase(buffer.begin(), buffer.begin() + end);
    };

    while (cursor < buffer.size()) {
      const auto start = std::find(buffer.begin() + cursor, buffer.end(), startByte);
      statistics.discardedBytes += static_cast<size_t>(start - (buffer.begin() + cursor));
      cursor = static_cast<size_t>(start - buffer.begin());
      const auto remaining = std::span<const uint8_t>(buffer).subspan(cursor);
      switch (inspectFrame(remaining)) {
        case FrameStatus::Incomplete:
          discardThrough(cursor); // Keep only the unfinished frame.
          return std::nullopt;
        case FrameStatus::BadLength:
          ++statistics.lengthErrors;
          ++statistics.discardedBytes;
          ++cursor; // Search again without recursion or repeated buffer copies.
          continue;
        case FrameStatus::BadChecksum:
          ++statistics.checksumErrors;
          ++statistics.discardedBytes;
          ++cursor;
          continue;
        case FrameStatus::Complete:
          break;
      }

      const size_t length = remaining[1];
      // Consume a checksum-valid frame even if its definition/payload is invalid.
      // A subsequent call can then decode the next frame without exposing a partial message.
      std::optional<Message> message;
      try {
        message = detail::decodeMessagePayload(dictionary, remaining.subspan(2, length - 4));
      } catch (...) {
        ++statistics.decodingErrors;
        statistics.discardedBytes += length;
        discardThrough(cursor + length);
        throw;
      }
      lastReceivedFrameSize = length;
      ++statistics.receivedMessages;
      statistics.receivedMessageBytes += length;
      discardThrough(cursor + length);
      return message;
    }
    discardThrough(cursor);
    return std::nullopt;
  }
}
