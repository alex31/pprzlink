// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file UdpTransport.cpp
 * @brief UDP framing and per-datagram source tracking.
 * @ingroup transports
 *
 * Multiple frames in one datagram retain its source. An incomplete trailing frame is discarded before receiving another datagram or peer.
 */

#include "UdpTransport.h"
#include <array>

namespace pprzlink {
  /// @brief Internal conversion between value endpoints and native Asio endpoints.
  /// @ingroup internals
  namespace {
    /// @brief Parse a numeric endpoint without performing DNS lookup.
    /// @param[in] address Numeric IP address and UDP port.
    /// @return Asio endpoint selecting IPv4 or IPv6 from the parsed address.
    /// @throws boost::system::system_error The address is not a supported numeric IP string.
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

  /// @details Drain all decodable frames belonging to pendingPeer before reading
  /// another datagram. Discard an incomplete datagram tail and bound each polling
  /// call to 64 receive attempts, even under a stream of empty/invalid datagrams.
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
