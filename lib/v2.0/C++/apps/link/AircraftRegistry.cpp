// SPDX-License-Identifier: GPL-2.0-or-later
#include "AircraftRegistry.h"
#include <format>

namespace link_app {
  void AircraftRegistry::received(uint8_t id, size_t bytes, uint64_t errors, bool pong,
                                  double now, std::optional<pprzlink::UdpEndpoint> peer)
  {
    auto [entry, inserted] = aircraft.try_emplace(id);
    auto &state = entry->second;
    if (inserted) state.udpPeer = std::move(peer);
    state.receivedBytes += bytes;
    ++state.receivedMessages;
    state.receiveErrors = errors;
    state.millisecondsSinceReceive = 0;
    if (pong) state.lastPong = now;
  }

  bool AircraftRegistry::isLive(uint8_t id, int timeout) const
  {
    const auto entry = aircraft.find(id);
    return entry != aircraft.end() &&
           (timeout <= 0 || entry->second.millisecondsSinceReceive < timeout);
  }

  void AircraftRegistry::transmitted(uint8_t id) { ++aircraft.at(id).transmittedMessages; }
  void AircraftRegistry::pinged(uint8_t id, double now) { aircraft.at(id).lastPing = now; }
  void AircraftRegistry::broadcast()
  {
    // Preserve OCaml's accounting: broadcasts count for every known aircraft.
    for (auto &[id, state] : aircraft) { (void)id; ++state.transmittedMessages; }
  }
  void AircraftRegistry::age(int milliseconds)
  {
    for (auto &[id, state] : aircraft) { (void)id; state.millisecondsSinceReceive += milliseconds; }
  }

  std::vector<std::string> AircraftRegistry::reports(int linkId, int periodMilliseconds, uint64_t runTime)
  {
    std::vector<std::string> messages;
    const double seconds = periodMilliseconds / 1000.0;
    for (auto &[id, state] : aircraft) {
      const double byteRate = (state.receivedBytes - state.previousBytes) / seconds;
      const double messageRate = (state.receivedMessages - state.previousMessages) / seconds;
      // LINK_REPORT uses double precision on the ground, although its XML fields
      // are floats. Avoid rounding the rates/timestamps to a binary float first.
      messages.push_back(std::format("link LINK_REPORT {} {} {} {} {} {} {} {:.1f} {:.1f} {} {:.2f}",
        id, linkId, runTime, state.millisecondsSinceReceive / 1000,
        state.receivedBytes, state.receivedMessages, state.receiveErrors,
        byteRate, messageRate, state.transmittedMessages, 1000 * (state.lastPong - state.lastPing)));
      state.previousBytes = state.receivedBytes;
      state.previousMessages = state.receivedMessages;
    }
    return messages;
  }
}
