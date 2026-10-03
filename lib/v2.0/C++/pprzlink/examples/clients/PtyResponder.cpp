// SPDX-License-Identifier: LGPL-3.0-or-later
// Aircraft-side workshop peer: answer an altitude request from ground station 0.
#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/PprzTransport.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "Usage: pty_responder messages.xml serial-port\n";
    return 2;
  }
  try {
    using namespace std::chrono_literals;
    const pprzlink::MessageDictionary dictionary(argv[1]);
    boost::asio::io_context context;
    using Serial = pprzlink::BoostSerialPortDevice;
    auto device = std::make_unique<Serial>(context, argv[2]);
    device->setBaudrate(Serial::Baudrate(57600));
    device->setDataBits(Serial::DataBits(8));
    device->setParity(Serial::Parity(Serial::Parity::none));
    device->setStopBits(Serial::StopBits(Serial::StopBits::one));
    device->setFlowcontrol(Serial::Flowcontrol(Serial::Flowcontrol::none));
    pprzlink::PprzTransport transport(std::move(device), dictionary);

    std::cout << "Responder ready on " << argv[2] << std::endl;
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (auto received = transport.tryReceive()) {
        const auto &request = received->message;
        if (request.getDefinition().getName() != "GUIDE_ALTITUDE_REQ" ||
            request.getReceiverId() != 42 || std::get<uint8_t>(request.getSenderId()) != 0) continue;
        pprzlink::Message altitude(dictionary.getDefinition("GUIDE_ALTITUDE"));
        altitude.setSenderId(42);
        altitude.setReceiverId(0);
        altitude.setFieldSI("altitude", 123.5); // Metres, independently of the XML representation.
        transport.sendMessage(altitude);
        std::cout << "Answered GUIDE_ALTITUDE_REQ with 123.5 m\n";
        return 0;
      }
      context.restart();
      context.run_for(10ms);
    }
    throw std::runtime_error("No altitude request within 60 seconds");
  } catch (const std::exception &error) {
    std::cerr << argv[0] << ": " << error.what() << '\n';
    return 1;
  }
}
