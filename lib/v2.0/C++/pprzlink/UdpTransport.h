// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <pprzlink/PprzFrameCodec.h>
#include <boost/asio/ip/udp.hpp>

namespace pprzlink {
  struct UdpOptions {
    UdpEndpoint local; // Port zero lets the OS choose a free local port.
    bool broadcast = false;
  };

  /// PPRZ frames over UDP. Sockets are nonblocking; poll from one thread.
  /// Unlike a byte stream, every send needs an explicit destination. Aircraft
  /// discovery, uplink ports and broadcast policy belong to the application.
  /// The supplied context and dictionary must outlive the transport.
  class UdpTransport {
  public:
    UdpTransport(boost::asio::io_context &context, const MessageDictionary &dictionary,
                 const UdpOptions &options = {});
    UdpTransport(const UdpTransport &) = delete;
    UdpTransport &operator=(const UdpTransport &) = delete;
    UdpTransport(UdpTransport &&) = delete;
    UdpTransport &operator=(UdpTransport &&) = delete;

    /// Return one message with the source of its datagram. Malformed payloads
    /// throw after consuming their frame; the next call can continue that datagram.
    /// Incomplete frames are dropped at datagram boundaries, never joined across peers.
    [[nodiscard]] std::optional<ReceivedMessage> tryReceive();
    size_t sendMessage(const Message &message, const UdpEndpoint &destination);
    [[nodiscard]] UdpEndpoint localEndpoint() const;
    [[nodiscard]] const TransportStatistics &getStatistics() const noexcept { return decoder.getStatistics(); }

  private:
    boost::asio::ip::udp::socket socket;
    PprzFrameDecoder decoder;
    std::optional<UdpEndpoint> pendingPeer;
  };
}
