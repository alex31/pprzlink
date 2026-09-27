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

#include "BoostSerialPortDevice.h"
#include <boost/asio/write.hpp>
#include <array>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace pprzlink {
  struct BoostSerialPortDevice::State : std::enable_shared_from_this<State> {
    State(boost::asio::io_context &context, const std::string &name) : port(context, name) {}

    std::mutex mutex;
    boost::asio::serial_port port;
    std::array<uint8_t, 1024> readBuffer;
    BytesBuffer received;
    boost::system::error_code receiveError;
    bool receiving = false;
    bool readPending = false;
    uint64_t inputGeneration = 0;

    void checkReceiveError() const
    {
      if (receiveError) throw boost::system::system_error(receiveError, "Serial reception");
    }

    // Caller holds mutex. At most one operation uses readBuffer at any time.
    void startRead()
    {
      if (!receiving || readPending || receiveError) return;
      port.async_read_some(boost::asio::buffer(readBuffer),
        [self = shared_from_this(), generation = inputGeneration](const boost::system::error_code &error, size_t size) {
          self->finishRead(error, size, generation);
        });
      readPending = true;
    }

    void finishRead(const boost::system::error_code &error, size_t size, uint64_t generation)
    {
      std::lock_guard lock(mutex);
      readPending = false;
      if (generation == inputGeneration) {
        received.insert(received.end(), readBuffer.begin(), readBuffer.begin() + size);
        if (error && error != boost::asio::error::operation_aborted) {
          receiveError = error;
          receiving = false;
        }
      }
      startRead(); // Includes a restart requested while cancellation was pending.
    }

    template<class Option>
    Option getOption()
    {
      std::lock_guard lock(mutex);
      Option value;
      port.get_option(value);
      return value;
    }

    template<class Option>
    void setOption(const Option &value)
    {
      std::lock_guard lock(mutex);
      port.set_option(value);
    }
  };

  BoostSerialPortDevice::BoostSerialPortDevice(boost::asio::io_context &context, std::string name)
    : state(std::make_shared<State>(context, name)) {}

  BoostSerialPortDevice::~BoostSerialPortDevice()
  {
    std::lock_guard lock(state->mutex);
    state->receiving = false;
    boost::system::error_code ignored;
    state->port.close(ignored);
    // Canceled handlers own State, never this device. They can safely finish later.
  }

  size_t BoostSerialPortDevice::availableBytes()
  {
    std::lock_guard lock(state->mutex);
    if (state->received.empty()) state->checkReceiveError();
    return state->received.size();
  }

  BytesBuffer BoostSerialPortDevice::readAll()
  {
    std::lock_guard lock(state->mutex);
    if (state->received.empty()) state->checkReceiveError();
    state->receiving = !state->receiveError;
    state->startRead();
    return std::exchange(state->received, {});
  }

  void BoostSerialPortDevice::writeBuffer(const BytesBuffer &data)
  {
    std::lock_guard lock(state->mutex);
    boost::asio::write(state->port, boost::asio::buffer(data));
  }

  void BoostSerialPortDevice::resetBaudrate(unsigned int baudrate)
  {
    if (baudrate == 0) throw std::invalid_argument("Serial baud rate must be positive");
    std::lock_guard lock(state->mutex);
    state->checkReceiveError();
    state->port.cancel();
    ++state->inputGeneration; // Canceled completions must not reinsert old-rate bytes.
    state->received.clear();
    state->port.set_option(Baudrate(baudrate));
    state->receiving = true;
    state->startRead(); // Waits for a pending cancellation before reusing readBuffer.
  }

  void BoostSerialPortDevice::startReception()
  {
    std::lock_guard lock(state->mutex);
    state->checkReceiveError();
    state->receiving = true;
    state->startRead();
  }

  void BoostSerialPortDevice::stopReception()
  {
    std::lock_guard lock(state->mutex);
    state->receiving = false;
    state->port.cancel();
  }

  BoostSerialPortDevice::Baudrate BoostSerialPortDevice::getBaudrate() const
  {
    return state->getOption<Baudrate>();
  }

  void BoostSerialPortDevice::setBaudrate(const Baudrate &value)
  {
    state->setOption(value);
  }

  BoostSerialPortDevice::DataBits BoostSerialPortDevice::getDataBits() const
  {
    return state->getOption<DataBits>();
  }

  void BoostSerialPortDevice::setDataBits(const DataBits &value)
  {
    state->setOption(value);
  }

  BoostSerialPortDevice::Parity BoostSerialPortDevice::getParity() const
  {
    return state->getOption<Parity>();
  }

  void BoostSerialPortDevice::setParity(const Parity &value)
  {
    state->setOption(value);
  }

  BoostSerialPortDevice::StopBits BoostSerialPortDevice::getStopBits() const
  {
    return state->getOption<StopBits>();
  }

  void BoostSerialPortDevice::setStopBits(const StopBits &value)
  {
    state->setOption(value);
  }

  BoostSerialPortDevice::Flowcontrol BoostSerialPortDevice::getFlowcontrol() const
  {
    return state->getOption<Flowcontrol>();
  }

  void BoostSerialPortDevice::setFlowcontrol(const Flowcontrol &value)
  {
    state->setOption(value);
  }

}
