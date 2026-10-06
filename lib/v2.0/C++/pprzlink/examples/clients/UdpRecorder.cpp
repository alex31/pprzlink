// SPDX-License-Identifier: LGPL-3.0-or-later
// Record three messages with the source endpoint belonging to each message.
#include <pprzlink/UdpTransport.h>
#include <pprzlink/IvyMessageCodec.h>
#include <charconv>
#include <chrono>
#include <iostream>
#include <boost/asio/steady_timer.hpp>

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "Usage: udp_recorder messages.xml local-port (0 chooses a free port)\n";
    return 2;
  }
  try {
    using namespace std::chrono_literals;
    const std::string_view text(argv[2]);
    uint16_t port = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (error != std::errc{} || end != text.data() + text.size())
      throw std::invalid_argument("Invalid UDP port");
    const pprzlink::MessageDictionary dictionary(argv[1]);
    boost::asio::io_context context;
    pprzlink::UdpTransport transport(context, dictionary,
      {.local = {"127.0.0.1", port}});
    std::cout << "Listening on " << transport.localEndpoint().port << std::endl;

    boost::asio::steady_timer timeout(context, 10s);
    int count = 0;
    timeout.async_wait([&](const boost::system::error_code &error) { if (!error) transport.stop(); });
    transport.bind(pprzlink::ALL, [&](const pprzlink::Message &message, const pprzlink::ReceiveInfo &info) {
      std::cout << info.udpPeer->address << ':' << info.udpPeer->port
                << " [" << info.frameSize << " bytes] "
                << pprzlink::ivy_codec::serializeMessage(message) << std::endl;
      if (++count == 3) { transport.stop(); timeout.cancel(); }
    });
    transport.start();
    context.run();
    return count == 3 ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
