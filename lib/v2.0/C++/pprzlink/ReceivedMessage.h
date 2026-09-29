// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <pprzlink/Message.h>
#include <cstdint>
#include <optional>
#include <string>

namespace pprzlink {
  /// An IP address and port. No socket or event-loop ownership is hidden here.
  struct UdpEndpoint {
    std::string address = "0.0.0.0";
    uint16_t port = 0;
    bool operator==(const UdpEndpoint &) const = default;
  };

  struct XbeeReceiveInfo {
    uint64_t sourceAddress;
    bool addressIs64Bit;
    uint8_t rssi; // Magnitude in dBm; valid only when hasRssi is true.
    uint8_t options;
    bool hasRssi = true;
  };

  /// Own a decoded message and the metadata from that same receive operation.
  /// Subsequent receives cannot overwrite this metadata.
  struct ReceivedMessage {
    Message message;
    size_t frameSize = 0; // Complete frame with its envelope; zero if a custom transport does not report it.
    std::optional<XbeeReceiveInfo> xbee;
    std::optional<UdpEndpoint> udpPeer;
  };
}
