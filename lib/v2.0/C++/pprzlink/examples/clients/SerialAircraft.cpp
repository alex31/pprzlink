// SPDX-License-Identifier: LGPL-3.0-or-later
// A minimal aircraft peer: publish an altitude, then answer one PING with PONG.
#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/PprzTransport.h>
#include <pprzlink/TransportPump.h>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <iostream>
#include <memory>

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "Usage: serial_aircraft messages.xml serial-port\n";
    return 2;
  }
  try {
    using namespace std::chrono_literals;
    const pprzlink::MessageDictionary dictionary(argv[1]);
    boost::asio::io_context context; // Outlives the serial device.
    auto device = std::make_unique<pprzlink::BoostSerialPortDevice>(context, argv[2]);
    device->setBaudrate(pprzlink::BoostSerialPortDevice::Baudrate(57600));
    pprzlink::PprzTransport transport(std::move(device), dictionary);

    pprzlink::Message altitude(dictionary.getDefinition("CLIENT_ALTITUDE"));
    altitude.setSenderId(42);
    altitude.setReceiverId(0);
    altitude.setFieldSI("altitude", 123.5); // Metres; the XML chooses the stored unit/type.
    transport.sendMessage(altitude);

    bool answered = false;
    boost::asio::steady_timer deadline(context, 10s);
    pprzlink::TransportPump receiver(context, transport,
      [&](pprzlink::ReceivedMessage received) {
        const auto &message = received.message;
        if (message.getDefinition().getName() != "PING" || message.getReceiverId() != 42) return;
        pprzlink::Message pong(dictionary.getDefinition("PONG"));
        pong.setSenderId(42);
        pong.setReceiverId(std::get<uint8_t>(message.getSenderId()));
        transport.sendMessage(pong);
        std::cout << "Answered PING (" << received.frameSize << " received bytes)\n";
        answered = true;
        receiver.stop();
        context.stop(); // Also leaves the device's outstanding asynchronous read.
      });
    deadline.async_wait([&](const boost::system::error_code &error) {
      if (!error) { receiver.stop(); context.stop(); }
    });
    receiver.start();
    context.run();
    if (answered) return 0;
    std::cerr << "No PING received within 10 seconds\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
