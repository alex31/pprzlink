// SPDX-License-Identifier: LGPL-3.0-or-later
// Prepare all command fields together and send one datagram to a local test peer.
#include <pprzlink/UdpTransport.h>
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "Usage: udp_setting_sender messages.xml destination-port\n";
    return 2;
  }
  try {
    const std::string_view portText(argv[2]);
    uint16_t port = 0;
    const auto [end, error] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
    if (portText.empty() || error != std::errc{} || end != portText.data() + portText.size() || port == 0)
      throw std::invalid_argument("Destination port must be an integer in [1, 65535]");

    const pprzlink::MessageDictionary dictionary(argv[1]);
    boost::asio::io_context context;
    pprzlink::UdpTransport transport(context, dictionary);

    pprzlink::Message command(dictionary.getDefinition("CLIENT_SETTING"));
    command.setSenderId(0);
    command.setReceiverId(42);

    const auto bytes = transport.sendMessage(
      command.setField("ac_id", 42, "value", 12.5), {"127.0.0.1", port});
    std::cout << "Sent " << command.toString() << " (" << bytes << " bytes)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
