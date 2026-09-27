// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef PPRZLINKCPP_XBEETRANSPORT_H
#define PPRZLINKCPP_XBEETRANSPORT_H

#include <pprzlink/Transport.h>
#include <pprzlink/XbeeModem.h>
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace pprzlink {
  /** XBee 802.15.4 legacy API transport, without escaping (AP=1).
   * Call startInitialization() or use an already configured modem. XML describes
   * PPRZLINK messages; it does not select or configure the radio transport.
   * Calls must be serialized, as for Transport. No automatic host-side retries.
   */
  class XbeeTransport : public Transport {
  public:
    static constexpr size_t maximumRfPayloadSize = 100;

    struct TransmitStatus {
      uint8_t frameId;
      uint8_t status; // 0: success, 1: no ACK, 2: channel access failure, ...
    };
    struct ModemStatus { uint8_t status; };
    struct AtCommandResponse {
      uint8_t frameId;
      std::array<char, 2> command;
      uint8_t status;
      BytesBuffer value;
    };
    using RadioStatus = std::variant<TransmitStatus, ModemStatus, AtCommandResponse>;
    using StatusCallback = std::function<void(const RadioStatus &)>;

    struct ReceiveInfo {
      uint64_t sourceAddress;
      bool addressIs64Bit;
      uint8_t rssi; // Magnitude of the negative signal strength in dBm.
      uint8_t options;
    };

    XbeeTransport(std::unique_ptr<Device> device, const MessageDictionary &dictionary);

    /// Start/restart AT initialization before exchanging messages. Clears buffered
    /// frames and metadata. Do not access the device directly during initialization.
    void startInitialization(const XbeeConfiguration &configuration = {},
                             XbeeModem::TimePoint now = XbeeModem::Clock::now());
    /// Call regularly while servicing the Device's I/O loop. No sleeping.
    /// Throws on failure and keeps sends blocked until a successful restart.
    bool pollInitialization(XbeeModem::TimePoint now = XbeeModem::Clock::now());
    /// True also for the default constructor's already-configured-modem mode.
    bool isReady() const noexcept { return !initialization || initialization->isReady(); }
    /// Detection and flash-save result, available after successful initialization.
    std::optional<XbeeBaudrateInfo> getBaudrateInfo() const noexcept
    {
      return initialization ? initialization->getBaudrateInfo() : std::nullopt;
    }

    /// Process RX16/RX64 messages and radio statuses; retain incomplete frames.
    /// A checksum-valid malformed message is consumed before its exception is raised.
    bool hasMessage() override;
    std::unique_ptr<Message> getMessage() override;

    /// Send to the PPRZLINK receiver's 16-bit radio address; receiver 255 broadcasts to 0xffff.
    /// Returns bytes written to the serial device, not confirmation of RF delivery.
    size_t sendMessage(const Message &message) override;
    /// Explicit radio addressing, independent of the PPRZLINK header's receiver ID.
    size_t sendMessageTo16(const Message &message, uint16_t destination);
    size_t sendMessageTo64(const Message &message, uint64_t destination);

    /// Extra validation of the complete API frame immediately before Device::writeBuffer.
    /// Failures throw without writing any bytes. Applications choose how to report them.
    void setSanityChecksEnabled(bool enabled) noexcept { sanityChecksEnabled = enabled; }
    /// Validate a captured TX16/TX64 AP=1 frame and its PPRZLINK payload against the XML.
    /// No I/O; throws wrong_message_format with the reason for rejection.
    void validateTransmitFrame(std::span<const uint8_t> frame) const;

    /// IDs cycle through 1..255. Match status events to this ID; do not keep
    /// more than 255 outstanding transmissions. Zero means nothing sent yet.
    uint8_t getLastFrameId() const noexcept { return lastFrameId; }
    /// Metadata of the last successfully decoded message (not its PPRZLINK sender ID).
    const std::optional<ReceiveInfo>& getLastReceiveInfo() const noexcept { return receiveInfo; }

    /// Called synchronously by hasMessage()/getMessage(), never by a background thread.
    /// The argument is borrowed for the call. Do not reenter reception or replace
    /// this callback from inside it. Exceptions propagate after consuming the frame.
    void setStatusCallback(StatusCallback callback) { statusCallback = std::move(callback); }

  private:
    bool decodeMessage();
    void decodeFrame(std::span<const uint8_t> data);
    size_t sendTo(const Message &message, uint64_t destination, bool addressIs64Bit);

    BytesBuffer transportBuffer;
    std::unique_ptr<Message> currentMessage;
    std::optional<ReceiveInfo> receiveInfo;
    StatusCallback statusCallback;
    std::optional<XbeeModem> initialization;
    uint8_t lastFrameId = 0;
    bool sanityChecksEnabled = false;
  };
}
#endif // PPRZLINKCPP_XBEETRANSPORT_H
