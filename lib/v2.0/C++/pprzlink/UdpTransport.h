// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file UdpTransport.h
 * @brief Nonblocking PPRZLINK datagrams with explicit destinations.
 * @ingroup transports
 *
 * Every send names a destination. Source endpoint metadata accompanies each received message; application discovery and broadcast policy remain outside the transport.
 */

#pragma once
#include <pprzlink/PprzFrameCodec.h>
#include <boost/asio/ip/udp.hpp>

namespace pprzlink {
  /// @brief Local socket binding and broadcast permission for a UDP transport.
  /// @ingroup transports
  struct UdpOptions {
    UdpEndpoint local; ///< Bind address and port; zero selects an ephemeral port.
    bool broadcast = false; ///< Enable the socket's broadcast option without choosing destinations.
  };

  /// PPRZ frames over UDP. Sockets are nonblocking; poll from one thread.
  /// Unlike a byte stream, every send needs an explicit destination. Aircraft
  /// discovery, uplink ports and broadcast policy belong to the application.
  /// The supplied context and dictionary must outlive the transport.
  /// @ingroup transports
  class UdpTransport {
  public:
    /// @brief Open and bind a nonblocking UDP socket.
    /// @param[in] context Asio context that must outlive this transport.
    /// @param[in] dictionary Borrowed, unchanged schemas that must outlive this transport.
    /// @param[in] options Numeric local endpoint and broadcast permission.
    /// @throws boost::system::system_error Invalid address, socket option or bind failure.
    UdpTransport(boost::asio::io_context &context, const MessageDictionary &dictionary,
                 const UdpOptions &options = {});
    /// @brief Copying the socket/decoder owner is prohibited.
    UdpTransport(const UdpTransport &) = delete;
    /// @brief Copy assignment is prohibited.
    /// @return This operation is deleted and cannot return.
    UdpTransport &operator=(const UdpTransport &) = delete;
    /// @brief Moving the socket/decoder owner is prohibited.
    UdpTransport(UdpTransport &&) = delete;
    /// @brief Move assignment is prohibited.
    /// @return This operation is deleted and cannot return.
    UdpTransport &operator=(UdpTransport &&) = delete;

    /// Return one message with the source of its datagram. Malformed payloads
    /// throw after consuming their frame; the next call can continue that datagram.
    /// Incomplete frames are dropped at datagram boundaries, never joined across peers.
    /// @return Owned message and source endpoint, or std::nullopt after bounded polling.
    /// @throws std::exception Socket I/O or payload decoding fails.
    [[nodiscard]] std::optional<ReceivedMessage> tryReceive();
    /// @brief Encode one message and synchronously send its complete frame as one datagram.
    /// @param[in] message Fully populated message compatible with the destination's XML.
    /// @param[in] destination Numeric IPv4/IPv6 address and explicit remote UDP port.
    /// @return Complete frame bytes sent.
    /// @throws std::exception Encoding, address parsing, socket I/O or incomplete write failure.
    size_t sendMessage(const Message &message, const UdpEndpoint &destination);
    /// @brief Query the bound local endpoint, including an OS-selected port.
    /// @return Address and port as a value independent of the socket's lifetime.
    /// @throws boost::system::system_error Querying the socket fails.
    [[nodiscard]] UdpEndpoint localEndpoint() const;
    /// @brief Borrow cumulative frame validation counters.
    /// @return Decoder statistics updated by subsequent receive calls.
    [[nodiscard]] const TransportStatistics &getStatistics() const noexcept { return decoder.getStatistics(); }

  private:
    boost::asio::ip::udp::socket socket;
    PprzFrameDecoder decoder;
    std::optional<UdpEndpoint> pendingPeer;
  };
}
