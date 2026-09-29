// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TestSupport.h"
#include <pprzlink/UdpTransport.h>
#include <chrono>
#include <iostream>
#include <thread>

using namespace pprzlink;
using namespace std::chrono_literals;

namespace {
  ReceivedMessage receive(UdpTransport &transport)
  {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (auto received = transport.tryReceive()) return std::move(*received);
      std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("UDP receive timed out");
  }
}

int main()
{
  try {
    tinyxml2::XMLDocument xml;
    xml.Parse("<protocol><msg_class name='telemetry' id='1'><message name='VALUE' id='1'><field name='value' type='uint8'/></message></msg_class></protocol>");
    MessageDictionary dictionary(xml.RootElement());
    boost::asio::io_context context;
    UdpTransport receiver(context, dictionary, {.local = {"127.0.0.1", 0}});
    UdpTransport sender(context, dictionary, {.local = {"127.0.0.1", 0}});
    require(!receiver.tryReceive(), "Nonblocking receive on an empty socket");
    Message message(dictionary.getDefinition("VALUE"));
    message.setField("value", 7);
    require(sender.sendMessage(message, receiver.localEndpoint()) == 9, "Complete datagram written");
    auto first = receive(receiver);
    require(first.udpPeer == sender.localEndpoint() && first.frameSize == 9 && !first.xbee, "UDP source and frame size");
    receiver.sendMessage(first.message, *first.udpPeer);
    require(receive(sender).message.getFieldAs<int>("value") == 7, "Reply to the actual source port");

    boost::asio::ip::udp::socket raw(context, {boost::asio::ip::address_v4::loopback(), 0});
    const boost::asio::ip::udp::endpoint destination(boost::asio::ip::address_v4::loopback(), receiver.localEndpoint().port);
    const auto bytes = encodePprzFrame(message);
    auto combined = bytes;
    combined.insert(combined.end(), bytes.begin(), bytes.end());
    raw.send_to(boost::asio::buffer(combined), destination);
    const auto a = receive(receiver), b = receive(receiver);
    require(a.udpPeer == b.udpPeer && a.udpPeer->port == raw.local_endpoint().port(), "All frames retain their datagram's peer");
    require(first.udpPeer == sender.localEndpoint(), "A later peer cannot overwrite retained metadata");

    raw.send_to(boost::asio::buffer(bytes.data(), 4), destination);
    raw.send_to(boost::asio::buffer(bytes.data() + 4, bytes.size() - 4), destination);
    require(!receiver.tryReceive(), "Incomplete datagrams must not join into a frame");
    auto bad = bytes;
    bad[5] = 255; // Unknown message, with a repaired checksum, followed by a valid frame.
    uint8_t checkA = 0, checkB = 0;
    for (size_t i = 1; i < bad.size() - 2; ++i) { checkA += bad[i]; checkB += checkA; }
    bad[bad.size() - 2] = checkA;
    bad.back() = checkB;
    bad.insert(bad.end(), bytes.begin(), bytes.end());
    raw.send_to(boost::asio::buffer(bad), destination);
    expectException<no_such_message>([&] { (void)receive(receiver); });
    const auto recovered = receive(receiver);
    require(recovered.udpPeer->port == raw.local_endpoint().port() &&
            recovered.message.getFieldAs<int>("value") == 7, "Malformed frame consumed, next frame retains correct peer");
    require(receiver.getStatistics().decodingErrors == 1, "UDP exposes decoder statistics");
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
