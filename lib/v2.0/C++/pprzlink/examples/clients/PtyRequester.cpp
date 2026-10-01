// SPDX-License-Identifier: LGPL-3.0-or-later
// Ground-side workshop peer: request altitude from aircraft 42 and read its reply.
#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/PprzTransport.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "Usage: pty_requester messages.xml serial-port\n";
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

    pprzlink::Message request(dictionary.getDefinition("GUIDE_ALTITUDE_REQ"));
    request.setSenderId(0);
    request.setReceiverId(42);
    std::cout << "Requester ready on " << argv[2] << std::endl;
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    auto nextRequest = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline) {
      if (std::chrono::steady_clock::now() >= nextRequest) {
        transport.sendMessage(request);
        nextRequest = std::chrono::steady_clock::now() + 500ms;
      }
      if (auto received = transport.tryReceive()) {
        const auto &message = received->message;
        if (message.getDefinition().getName() != "GUIDE_ALTITUDE" ||
            message.getReceiverId() != 0 || std::get<uint8_t>(message.getSenderId()) != 42) continue;
        std::cout << "Aircraft 42: " << message.getField<float>("altitude")
                  << " m (" << received->frameSize << " received bytes)\n";
        return 0;
      }
      context.restart();
      context.run_for(10ms);
    }
    throw std::runtime_error("No altitude reply within 60 seconds");
  } catch (const std::exception &error) {
    std::cerr << argv[0] << ": " << error.what() << '\n';
    return 1;
  }
}
