// SPDX-License-Identifier: LGPL-3.0-or-later

#include "XbeeTransport.h"
#include "detail/MessagePayload.h"
#include <algorithm>
#include <format>
#include <utility>

namespace pprzlink {
  namespace {
    constexpr uint8_t startByte = 0x7e;
    constexpr uint8_t tx64 = 0x00, tx16 = 0x01;
    constexpr uint8_t rx64 = 0x80, rx16 = 0x81;
    constexpr uint8_t atResponse = 0x88, txStatus = 0x89, modemStatus = 0x8a;
    constexpr size_t envelopeSize = 4; // Delimiter, two length bytes, checksum.
    constexpr size_t maximumFrameDataSize = 11 + XbeeTransport::maximumRfPayloadSize;

    uint8_t checksum(std::span<const uint8_t> data)
    {
      uint8_t sum = 0;
      for (const auto byte : data) sum += byte;
      return static_cast<uint8_t>(0xff - sum);
    }

    size_t frameDataSize(std::span<const uint8_t> frame)
    {
      return (static_cast<size_t>(frame[1]) << 8) | frame[2];
    }

    enum class FrameStatus { Incomplete, Invalid, Complete };

    FrameStatus inspectFrame(std::span<const uint8_t> frame)
    {
      if (frame.size() < 3) return FrameStatus::Incomplete;
      const auto size = frameDataSize(frame);
      // This transport handles legacy 802.15.4 frames with at most 100 RF bytes.
      // Reject corrupt lengths before waiting for/allocating a 64 KiB frame.
      if (size == 0 || size > maximumFrameDataSize) return FrameStatus::Invalid;
      if (frame.size() < size + envelopeSize) return FrameStatus::Incomplete;
      if (checksum(frame.subspan(3, size)) != frame[3 + size]) return FrameStatus::Invalid;
      return FrameStatus::Complete;
    }

    uint64_t readAddress(std::span<const uint8_t> bytes)
    {
      uint64_t address = 0;
      for (const auto byte : bytes) address = (address << 8) | byte;
      return address;
    }
  }

  XbeeTransport::XbeeTransport(std::unique_ptr<Device> device, const MessageDictionary &dictionary)
    : Transport(std::move(device), dictionary)
  {
    transportBuffer.reserve(maximumFrameDataSize + envelopeSize);
  }

  bool XbeeTransport::hasMessage()
  {
    if (!pollInitialization()) return false;
    if (!currentMessage) decodeMessage();
    return static_cast<bool>(currentMessage);
  }

  std::unique_ptr<Message> XbeeTransport::getMessage()
  {
    if (!pollInitialization()) return nullptr;
    if (!currentMessage) decodeMessage();
    return std::move(currentMessage);
  }

  size_t XbeeTransport::sendMessage(const Message &message)
  {
    const uint16_t destination = message.getReceiverId() == 255 ? 0xffff : message.getReceiverId();
    return sendMessageTo16(message, destination);
  }

  size_t XbeeTransport::sendMessageTo16(const Message &message, uint16_t destination)
  {
    return sendTo(message, destination, false);
  }

  size_t XbeeTransport::sendMessageTo64(const Message &message, uint64_t destination)
  {
    return sendTo(message, destination, true);
  }

  size_t XbeeTransport::sendTo(const Message &message, uint64_t destination, bool addressIs64Bit)
  {
    if (!isReady()) throw std::logic_error("XBee initialization must succeed before sending messages");
    const auto payload = detail::encodeMessagePayload(message, maximumRfPayloadSize);
    const size_t addressSize = addressIs64Bit ? 8 : 2;
    const size_t dataSize = 3 + addressSize + payload.size();
    const uint8_t frameId = lastFrameId == 255 ? 1 : static_cast<uint8_t>(lastFrameId + 1);
    BytesBuffer frame{startByte, static_cast<uint8_t>(dataSize >> 8), static_cast<uint8_t>(dataSize),
                      addressIs64Bit ? tx64 : tx16, frameId};
    frame.reserve(dataSize + envelopeSize);
    for (size_t i = addressSize; i > 0; --i) frame.push_back(static_cast<uint8_t>(destination >> (8 * (i - 1))));
    frame.push_back(0); // Normal transmission: enable the modem's acknowledgement/retry mechanism.
    // No 0x99/length/PPRZ checksum inside the XBee RF data.
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(checksum(std::span(frame).subspan(3)));
    if (sanityChecksEnabled) validateTransmitFrame(frame);
    device->writeBuffer(frame);
    lastFrameId = frameId;
    return frame.size();
  }

  void XbeeTransport::validateTransmitFrame(std::span<const uint8_t> frame) const
  {
    const auto reject = [](const std::string &reason) {
      throw wrong_message_format("XBee sanity check: " + reason);
    };
    if (frame.size() < envelopeSize) reject("truncated API envelope");
    if (frame[0] != startByte) reject("expected start delimiter 0x7e");
    const auto dataSize = frameDataSize(frame);
    if (dataSize != frame.size() - envelopeSize) {
      reject(std::format("length declares {} bytes, actual frame data has {}", dataSize, frame.size() - envelopeSize));
    }
    if (dataSize == 0) reject("missing API frame type");
    if (frame[3] != tx16 && frame[3] != tx64) {
      reject(std::format("unsupported transmit frame type 0x{:02x} (expected 0x00 or 0x01)", frame[3]));
    }
    const size_t headerSize = frame[3] == tx16 ? 5 : 11;
    if (dataSize < headerSize) reject("truncated radio address/transmit header");
    const size_t payloadSize = dataSize - headerSize;
    if (payloadSize < detail::messageHeaderSize || payloadSize > maximumRfPayloadSize) {
      reject(std::format("RF payload has {} bytes; expected 4..100 including the PPRZLINK header", payloadSize));
    }
    if ((frame[3 + headerSize - 1] & ~0x05) != 0) reject("reserved transmit option bits are set");
    // Check the sum including the checksum, independently of the encoder's helper.
    uint8_t sum = 0;
    for (const auto byte : frame.subspan(3)) sum += byte;
    if (sum != 0xff) reject("invalid API checksum");
    try {
      (void)detail::decodeMessagePayload(dictionary, frame.subspan(3 + headerSize, payloadSize));
    } catch (const std::exception &error) {
      reject("PPRZLINK payload does not match the XML: " + std::string(error.what()));
    }
  }

  void XbeeTransport::startInitialization(const XbeeConfiguration &configuration, XbeeModem::TimePoint now)
  {
    // Validate before replacing a previous initialization or discarding received data.
    auto modem = XbeeModem(configuration, now);
    initialization = std::move(modem);
    transportBuffer.clear();
    currentMessage.reset();
    receiveInfo.reset();
    lastFrameId = 0;
  }

  bool XbeeTransport::pollInitialization(XbeeModem::TimePoint now)
  {
    if (!initialization) return true;
    if (!initialization->poll(*device, now)) return false;
    const auto remaining = initialization->takeRemainingBytes();
    transportBuffer.insert(transportBuffer.end(), remaining.begin(), remaining.end());
    return true;
  }

  void XbeeTransport::decodeFrame(std::span<const uint8_t> data)
  {
    const auto requireSize = [&](size_t size, bool exact = false) {
      if (data.size() < size || (exact && data.size() != size)) {
        throw wrong_message_format("Invalid XBee API frame size for type " + std::to_string(data[0]));
      }
    };
    const auto notify = [&](const RadioStatus &status) {
      if (statusCallback) statusCallback(status);
    };

    switch (data[0]) {
      case rx16:
      case rx64: {
        const bool addressIs64Bit = data[0] == rx64;
        const size_t addressSize = addressIs64Bit ? 8 : 2;
        const size_t headerSize = addressSize + 3;
        requireSize(headerSize + detail::messageHeaderSize);
        const auto payload = data.subspan(headerSize);
        if (payload.size() > maximumRfPayloadSize) throw wrong_message_format("XBee RF payload exceeds 100 bytes");
        auto message = std::make_unique<Message>(detail::decodeMessagePayload(dictionary, payload));
        receiveInfo = ReceiveInfo{readAddress(data.subspan(1, addressSize)), addressIs64Bit,
                                  data[1 + addressSize], data[2 + addressSize]};
        currentMessage = std::move(message);
        break;
      }
      case txStatus:
        requireSize(3, true);
        notify(TransmitStatus{data[1], data[2]});
        break;
      case modemStatus:
        requireSize(2, true);
        notify(ModemStatus{data[1]});
        break;
      case atResponse:
        requireSize(5);
        notify(AtCommandResponse{data[1], {static_cast<char>(data[2]), static_cast<char>(data[3])},
                                 data[4], BytesBuffer(data.begin() + 5, data.end())});
        break;
      default:
        break; // Other API indications (e.g. I/O samples) are not PPRZLINK messages.
    }
  }

  bool XbeeTransport::decodeMessage()
  {
    const auto received = device->readAll();
    transportBuffer.insert(transportBuffer.end(), received.begin(), received.end());
    size_t cursor = 0;
    const auto discardThrough = [&](size_t end) {
      transportBuffer.erase(transportBuffer.begin(), transportBuffer.begin() + end);
    };
    while (cursor < transportBuffer.size()) {
      const auto start = std::find(transportBuffer.begin() + cursor, transportBuffer.end(), startByte);
      cursor = static_cast<size_t>(start - transportBuffer.begin());
      const auto remaining = std::span<const uint8_t>(transportBuffer).subspan(cursor);
      switch (inspectFrame(remaining)) {
        case FrameStatus::Incomplete:
          discardThrough(cursor);
          return false;
        case FrameStatus::Invalid:
          ++cursor;
          continue;
        case FrameStatus::Complete:
          break;
      }
      const size_t dataSize = frameDataSize(remaining);
      cursor += dataSize + envelopeSize;
      try {
        decodeFrame(remaining.subspan(3, dataSize));
      } catch (...) {
        discardThrough(cursor);
        throw;
      }
      if (currentMessage) {
        discardThrough(cursor);
        return true;
      }
    }
    discardThrough(cursor);
    return false;
  }
}
