// SPDX-License-Identifier: LGPL-3.0-or-later
// The same SDK client runs at either end of a virtual or physical serial link.
#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/PprzTransport.h>
#include <boost/system/system_error.hpp>
#include <charconv>
#include <chrono>
#include <csignal>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
  constexpr auto usage = "Usage: pty_agent messages.xml serial-port agent-id (0..255)\n"
                         "Publishes and receives GUIDE_ALTITUDE until Ctrl+C.\n";
}

int main(int argc, char **argv)
{
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << usage;
    return 0;
  }
  if (argc != 4) { std::cerr << usage; return 2; }
  const std::string_view idText(argv[3]);
  unsigned int id = 0;
  const auto [end, error] = std::from_chars(idText.data(), idText.data() + idText.size(), id);
  if (idText.empty() || error != std::errc{} || end != idText.data() + idText.size() || id > 255) {
    std::cerr << "Agent id must be an integer in [0, 255].\n" << usage;
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

    const double metres = 123.5 + id;
    pprzlink::Message altitude(dictionary.getDefinition("GUIDE_ALTITUDE"));
    altitude.setSenderId(id);
    altitude.setReceiverId(255); // Broadcast: neither port is assigned a ground/aircraft role.
    altitude.setFieldSI("altitude", metres);
    boost::asio::signal_set signals(context, SIGINT, SIGTERM);
    boost::asio::steady_timer transmit(context);
    signals.async_wait([&](const boost::system::error_code &error, int) {
      if (!error) { transport.stop(); transmit.cancel(); }
    });
    transport.bind("GUIDE_ALTITUDE", {.where = [id](const pprzlink::Message &message) {
      return std::get<uint8_t>(message.getSenderId()) != id &&
             (message.getReceiverId() == id || message.getReceiverId() == 255);
    }}, [&](const pprzlink::Message &message) {
      const auto sender = std::get<uint8_t>(message.getSenderId());
      std::cout << "Agent " << id << " RX from " << +sender << ": GUIDE_ALTITUDE "
                << message.getFieldSI("altitude") << " m" << std::endl;
    });
    std::function<void()> send = [&] {
      transport.sendMessage(altitude);
      std::cout << "Agent " << id << " TX GUIDE_ALTITUDE " << metres << " m" << std::endl;
      transmit.expires_after(1s);
      transmit.async_wait([&](const boost::system::error_code &error) { if (!error) send(); });
    };
    transport.start();
    std::cout << "Agent " << id << " ready on " << argv[2] << std::endl;
    send();
    context.run();
    return 0;
  } catch (const boost::system::system_error &error) {
    std::cerr << argv[0] << ": serial port '" << argv[2] << "': " << error.code().message() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << argv[0] << ": " << error.what() << '\n';
    return 1;
  }
}
