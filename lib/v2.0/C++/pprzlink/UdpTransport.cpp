// SPDX-License-Identifier: LGPL-3.0-or-later
#include "UdpTransport.h"
#include <array>

namespace pprzlink {
  namespace {
    boost::asio::ip::udp::endpoint socketEndpoint(const UdpEndpoint &address)
    {
      return {boost::asio::ip::make_address(address.address), address.port};
    }
  }

  UdpTransport::UdpTransport(boost::asio::io_context &context,
                             const MessageDictionary &dictionary, const UdpOptions &options)
    : socket(context), decoder(dictionary)
  {
    const auto local = socketEndpoint(options.local);
    socket.open(local.protocol());
    socket.set_option(boost::asio::socket_base::broadcast(options.broadcast));
    socket.bind(local);
    socket.non_blocking(true);
  }

  std::optional<ReceivedMessage> UdpTransport::tryReceive()
  {
    std::array<uint8_t, 65536> bytes;
    // Bound work even if the socket continuously receives empty or invalid datagrams.
    for (int count = 0; count < 64; ++count) {
      if (pendingPeer) {
        if (auto received = decoder.tryReceive()) {
          received->udpPeer = pendingPeer;
          return received;
        }
        decoder.discardPendingInput();
        pendingPeer.reset();
      }
      boost::asio::ip::udp::endpoint peer;
      boost::system::error_code error;
      const auto size = socket.receive_from(boost::asio::buffer(bytes), peer, 0, error);
      if (error == boost::asio::error::would_block || error == boost::asio::error::try_again)
        return std::nullopt;
      if (error) throw boost::system::system_error(error);
      pendingPeer = UdpEndpoint{peer.address().to_string(), peer.port()};
      decoder.pushBytes(std::span(bytes).first(size));
    }
    return std::nullopt;
  }

  size_t UdpTransport::sendMessage(const Message &message, const UdpEndpoint &destination)
  {
    const auto bytes = encodePprzFrame(message);
    const auto sent = socket.send_to(boost::asio::buffer(bytes), socketEndpoint(destination));
    if (sent != bytes.size()) throw std::runtime_error("Incomplete UDP write");
    return sent;
  }

  UdpEndpoint UdpTransport::localEndpoint() const
  {
    const auto local = socket.local_endpoint();
    return {local.address().to_string(), local.port()};
  }
}
