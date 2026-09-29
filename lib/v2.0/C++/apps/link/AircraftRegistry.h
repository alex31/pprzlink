// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <pprzlink/ReceivedMessage.h>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace link_app {
  struct AircraftState {
    uint64_t receivedBytes = 0;
    uint64_t receivedMessages = 0;
    uint64_t receiveErrors = 0;
    uint64_t transmittedMessages = 0;
    uint64_t previousBytes = 0;
    uint64_t previousMessages = 0;
    int64_t millisecondsSinceReceive = 0;
    double lastPing = 0;
    double lastPong = 0;
    std::optional<pprzlink::UdpEndpoint> udpPeer;
  };

  /// Link policy and accounting. All methods run on the agent's event loop.
  class AircraftRegistry {
  public:
    void received(uint8_t id, size_t bytes, uint64_t errors, bool pong, double now,
                  std::optional<pprzlink::UdpEndpoint> peer);
    bool isLive(uint8_t id, int timeout) const;
    void transmitted(uint8_t id);
    void broadcast();
    void pinged(uint8_t id, double now);
    void age(int milliseconds);
    const std::map<uint8_t, AircraftState> &all() const noexcept { return aircraft; }
    std::vector<std::string> reports(int linkId, int periodMilliseconds, uint64_t runTime);

  private:
    std::map<uint8_t, AircraftState> aircraft;
  };
}
