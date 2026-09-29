// SPDX-License-Identifier: LGPL-3.0-or-later
// A minimal aircraft peer: publish an altitude, then answer one PING with PONG.
#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/PprzTransport.h>
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
    altitude.addField("altitude", 123.5); // Checked conversion to the XML float type.
    transport.sendMessage(altitude);

    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (auto received = transport.tryReceive()) {
        const auto &message = received->message;
        if (message.getDefinition().getName() != "PING" || message.getReceiverId() != 42) continue;
        pprzlink::Message pong(dictionary.getDefinition("PONG"));
        pong.setSenderId(42);
        pong.setReceiverId(std::get<uint8_t>(message.getSenderId()));
        transport.sendMessage(pong);
        std::cout << "Answered PING (" << received->frameSize << " received bytes)\n";
        return 0;
      }
      context.restart();
      context.run_for(10ms);
    }
    std::cerr << "No PING received within 10 seconds\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
