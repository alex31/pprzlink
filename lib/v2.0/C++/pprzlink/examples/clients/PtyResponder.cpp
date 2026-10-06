// SPDX-License-Identifier: LGPL-3.0-or-later
// Aircraft-side workshop peer: answer an altitude request from ground station 0.
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
    bool answered = false;
    boost::asio::steady_timer timeout(context, 60s);
    timeout.async_wait([&](const boost::system::error_code &error) { if (!error) transport.stop(); });
    transport.bind("GUIDE_ALTITUDE_REQ", {.senderId = 0, .receiverId = 42}, [&](const pprzlink::Message &) {
      pprzlink::Message altitude(dictionary.getDefinition("GUIDE_ALTITUDE"));
      altitude.setSenderId(42);
      altitude.setReceiverId(0);
      altitude.setFieldSI("altitude", 123.5);
      transport.sendMessage(altitude);
      std::cout << "Answered GUIDE_ALTITUDE_REQ with 123.5 m\n";
      answered = true; transport.stop(); timeout.cancel();
    });
    transport.start();
    context.run();
    if (answered) return 0;
    throw std::runtime_error("No altitude request within 60 seconds");
  } catch (const std::exception &error) {
    std::cerr << argv[0] << ": " << error.what() << '\n';
    return 1;
  }
}
