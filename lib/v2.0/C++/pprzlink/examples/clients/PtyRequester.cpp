// SPDX-License-Identifier: LGPL-3.0-or-later
// Ground-side workshop peer: request altitude from aircraft 42 and read its reply.
#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/PprzTransport.h>
#include <boost/asio/steady_timer.hpp>
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
    bool received = false;
    boost::asio::steady_timer timeout(context, 60s), retry(context);
    timeout.async_wait([&](const boost::system::error_code &error) {
      if (!error) { transport.stop(); retry.cancel(); }
    });
    std::function<void()> sendRequest = [&] {
      transport.sendMessage(request);
      retry.expires_after(500ms);
      retry.async_wait([&](const boost::system::error_code &error) { if (!error) sendRequest(); });
    };
    transport.bind("GUIDE_ALTITUDE", {.senderId = 42, .receiverId = 0},
      [&](const pprzlink::Message &message, const pprzlink::ReceiveInfo &info) {
        std::cout << "Aircraft 42: " << message.getFieldSI("altitude")
                  << " m (" << info.frameSize << " received bytes)\n";
        received = true;
        transport.stop(); timeout.cancel(); retry.cancel();
      });
    transport.start();
    sendRequest();
    context.run();
    if (received) return 0;
    throw std::runtime_error("No altitude reply within 60 seconds");
  } catch (const std::exception &error) {
    std::cerr << argv[0] << ": " << error.what() << '\n';
    return 1;
  }
}
