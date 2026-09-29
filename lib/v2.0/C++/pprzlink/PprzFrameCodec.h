// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <pprzlink/MessageDictionary.h>
#include <pprzlink/Message.h>
#include <pprzlink/TransportStatistics.h>
#include <pprzlink/ReceivedMessage.h>
#include <optional>
#include <span>

namespace pprzlink {
  /// Encode one PPRZLINK v2 serial frame without performing I/O.
  BytesBuffer encodePprzFrame(const Message &message);

  /// Incremental framing shared by serial streams and UDP. The dictionary is borrowed.
  class PprzFrameDecoder {
  public:
    explicit PprzFrameDecoder(const MessageDictionary &dictionary) : dictionary(dictionary) {}
    void pushBytes(std::span<const uint8_t> bytes);
    /// Invalid payloads throw after consuming their frame; a later call can continue.
    std::optional<Message> nextMessage();
    [[nodiscard]] std::optional<ReceivedMessage> tryReceive()
    {
      auto message = nextMessage();
      if (!message) return std::nullopt;
      return ReceivedMessage{std::move(*message), lastReceivedFrameSize, std::nullopt, std::nullopt};
    }
    /// End a datagram; do not concatenate it with bytes from the next datagram.
    void discardPendingInput();
    const TransportStatistics &getStatistics() const noexcept { return statistics; }
    size_t getLastReceivedFrameSize() const noexcept { return lastReceivedFrameSize; }

  private:
    const MessageDictionary &dictionary;
    BytesBuffer buffer;
    TransportStatistics statistics;
    size_t lastReceivedFrameSize = 0;
  };
}
