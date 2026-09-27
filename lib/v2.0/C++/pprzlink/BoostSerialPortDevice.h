/*
 * Copyright 2020 garciafa
 * This file is part of PprzLinkCPP
 *
 * PprzLinkCPP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PprzLinkCPP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with ModemTester.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef PPRZLINKCPP_BOOSTSERIALPORTDEVICE_H
#define PPRZLINKCPP_BOOSTSERIALPORTDEVICE_H

#include "Device.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/serial_port.hpp>
#include <memory>
#include <string>

namespace pprzlink {
  /** Serial byte stream with asynchronous reception and complete synchronous writes.
   * Run the supplied io_context to service reception; it must outlive this device.
   * I/O and option calls are serialized internally, including completion handlers.
   * Finish external calls before destruction. Pending handlers retain their own state.
   */
  class BoostSerialPortDevice : public SerialDevice {
  public:
    using Baudrate = boost::asio::serial_port_base::baud_rate;
    using Parity = boost::asio::serial_port_base::parity;
    using StopBits = boost::asio::serial_port_base::stop_bits;
    using DataBits = boost::asio::serial_port_base::character_size;
    using Flowcontrol = boost::asio::serial_port_base::flow_control;

    BoostSerialPortDevice(boost::asio::io_context &context, std::string serialPortName);
    ~BoostSerialPortDevice() override;
    BoostSerialPortDevice(const BoostSerialPortDevice&) = delete;
    BoostSerialPortDevice& operator=(const BoostSerialPortDevice&) = delete;
    BoostSerialPortDevice(BoostSerialPortDevice&&) = delete;
    BoostSerialPortDevice& operator=(BoostSerialPortDevice&&) = delete;

    size_t availableBytes() override;
    /// Drain received bytes and start/resume reception (also works with polling transports).
    /// A receive error is reported after any already buffered bytes have been drained.
    BytesBuffer readAll() override;
    void writeBuffer(const BytesBuffer &data) override;
    void resetBaudrate(unsigned int baudrate) override;

    [[nodiscard]] Baudrate getBaudrate() const;
    void setBaudrate(const Baudrate &value);
    [[nodiscard]] DataBits getDataBits() const;
    void setDataBits(const DataBits &value);
    [[nodiscard]] Parity getParity() const;
    void setParity(const Parity &value);
    [[nodiscard]] StopBits getStopBits() const;
    void setStopBits(const StopBits &value);
    [[nodiscard]] Flowcontrol getFlowcontrol() const;
    void setFlowcontrol(const Flowcontrol &value);

    /// Idempotent. A stopped reception can be restarted even before cancellation completes.
    void startReception();
    /// Cancel reception, keeping the port open and the buffered bytes available.
    void stopReception();

  private:
    struct State;
    std::shared_ptr<State> state;
  };
}
#endif // PPRZLINKCPP_BOOSTSERIALPORTDEVICE_H
