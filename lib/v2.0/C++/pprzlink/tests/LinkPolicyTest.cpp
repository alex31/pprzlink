// SPDX-License-Identifier: GPL-2.0-or-later
#include "TestSupport.h"
#include "AircraftRegistry.h"
#include "Options.h"
#include "XbeeTransmitter.h"
#include <pprzlink/PprzTransport.h>
#include <iostream>

using namespace std::chrono_literals;

int main()
{
  try {
    link_app::AircraftRegistry registry;
    require(!registry.isLive(42, 0), "Disabling expiry must not make unknown aircraft live");
    registry.received(42, 13, 1, false, 1, std::nullopt);
    registry.transmitted(42);
    registry.pinged(42, 1);
    registry.received(42, 8, 1, true, 1.025, std::nullopt);
    auto report = registry.reports(7, 1000, 2);
    require(report == std::vector<std::string>{"link LINK_REPORT 42 7 2 0 21 2 1 21.0 2.0 1 25.00"}, "Report semantics");
    registry.age(1000);
    require(!registry.isLive(42, 1000) && registry.isLive(42, -1), "Exact expiry boundary");
    registry.broadcast();
    require(registry.all().at(42).transmittedMessages == 2, "Broadcast includes expired known aircraft in accounting");

    tinyxml2::XMLDocument xml;
    xml.Parse("<protocol><msg_class name='datalink' id='2'><message name='PING' id='8'/></msg_class></protocol>");
    pprzlink::MessageDictionary dictionary(xml.RootElement());
    auto device = std::make_unique<MemoryDevice>();
    auto &wire = *device;
    pprzlink::XbeeTransport radio(std::move(device), dictionary);
    link_app::XbeeTransmitter sender(radio, 2);
    pprzlink::Message ping(dictionary.getDefinition("PING"));
    ping.setReceiverId(42);
    const auto start = link_app::XbeeTransmitter::Clock::now();
    sender.send(ping, start);
    const auto original = wire.outgoing;
    sender.status({1, 1}, start);
    sender.status({1, 1}, start); // A duplicated failure does not consume a second attempt.
    wire.outgoing.clear();
    sender.poll(start + 250ms);
    require(wire.outgoing == original, "Retry must preserve frame and ID");
    sender.status({1, 1}, start + 250ms);
    wire.outgoing.clear();
    sender.poll(start + 1s);
    require(wire.outgoing.empty(), "Configured retry limit must be enforced");
    for (int i = 0; i < 255; ++i) sender.send(ping, start);
    expectException<std::runtime_error>([&] { sender.send(ping, start); });
    sender.status({42, 0}, start);
    sender.send(ping, start);
    require(wire.outgoing[4] == 42, "Only released IDs may be reused");
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
