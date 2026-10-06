#include "TestSupport.h"
#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/PprzTransport.h>
#include <pprzlink/XbeeTransport.h>
#include <boost/asio/executor_work_guard.hpp>
#include <chrono>
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <thread>
#include <unistd.h>

using namespace pprzlink;
using namespace std::chrono_literals;

namespace {
  class Terminal {
  public:
    Terminal()
    {
      fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
      require(fd >= 0, "Open pseudo-terminal");
      require(grantpt(fd) == 0 && unlockpt(fd) == 0, "Prepare pseudo-terminal");
      name = ptsname(fd);
    }
    ~Terminal() { closeMaster(); }
    void closeMaster() { if (fd >= 0) close(std::exchange(fd, -1)); }
    int fd = -1;
    std::string name;
  };

  template<class Predicate>
  void waitFor(Predicate predicate, const char *description)
  {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
      require(std::chrono::steady_clock::now() < deadline, description);
      std::this_thread::sleep_for(1ms);
    }
  }

  void waitFd(int fd, short events)
  {
    pollfd descriptor{fd, events, 0};
    int result;
    do { result = poll(&descriptor, 1, 5000); } while (result < 0 && errno == EINTR);
    require(result > 0 && (descriptor.revents & events), "Pseudo-terminal I/O timeout/error");
  }

  void writeAll(int fd, const BytesBuffer &data)
  {
    size_t offset = 0;
    while (offset < data.size()) {
      waitFd(fd, POLLOUT);
      const auto count = write(fd, data.data() + offset, data.size() - offset);
      if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      require(count > 0, "Write pseudo-terminal");
      offset += static_cast<size_t>(count);
    }
  }

  BytesBuffer readExactly(int fd, size_t size)
  {
    BytesBuffer data(size);
    size_t offset = 0;
    while (offset < size) {
      waitFd(fd, POLLIN);
      const auto count = read(fd, data.data() + offset, size - offset);
      if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      require(count > 0, "Read pseudo-terminal");
      offset += static_cast<size_t>(count);
    }
    return data;
  }

  // Always stop/join during exception unwinding; keep the context alive for its devices.
  class EventLoop {
  public:
    EventLoop() : thread([this] {
      try { context.run(); } catch (...) { failure = std::current_exception(); }
    }) {}
    ~EventLoop() { finish(); }
    void finish() { context.stop(); if (thread.joinable()) thread.join(); }
    boost::asio::io_context context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work{context.get_executor()};
    std::exception_ptr failure;
  private:
    std::jthread thread;
  };

  void testTraffic()
  {
    Terminal terminal;
    EventLoop loop;
    {
      BoostSerialPortDevice device(loop.context, terminal.name);
      device.setBaudrate(BoostSerialPortDevice::Baudrate(115200));
      require(device.getBaudrate().value() == 115200, "Serial options reflect the port");
      device.startReception();
      device.startReception(); // Must not launch overlapping reads.
      BytesBuffer input(8192);
      for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<uint8_t>(i);
      writeAll(terminal.fd, input);
      waitFor([&] { return device.availableBytes() == input.size(); }, "Receive beyond the scratch buffer");
      require(device.readAll() == input, "Receive every byte exactly once");
      require(device.readAll().empty(), "Drained receive queue");

      for (int i = 0; i < 40; ++i) {
        device.stopReception();
        device.startReception(); // Also exercises restart before the cancellation callback.
        const BytesBuffer bytes{static_cast<uint8_t>(i), 0, 255};
        writeAll(terminal.fd, bytes);
        waitFor([&] { return device.availableBytes() == bytes.size(); }, "Reception after restart");
        require(device.readAll() == bytes, "Cancellation never aliases read buffers");
      }

      expectException<std::invalid_argument>([&] { device.resetBaudrate(0); });
      for (int i = 0; i < 40; ++i) {
        writeAll(terminal.fd, {99, 88});
        waitFor([&] { return device.availableBytes() == 2; }, "Queue old-baud input");
        const auto baudrate = i % 2 == 0 ? 57600u : 9600u;
        device.resetBaudrate(baudrate);
        require(device.getBaudrate().value() == baudrate, "Baud reset changes host UART");
        require(device.availableBytes() == 0, "Baud reset discards queued input");
        const BytesBuffer fresh{static_cast<uint8_t>(i), 0, 255};
        writeAll(terminal.fd, fresh);
        waitFor([&] { return device.availableBytes() == fresh.size(); }, "Reception after baud reset");
        require(device.readAll() == fresh, "No old bytes or aliased read buffer after baud reset");
      }

      BytesBuffer output(128 * 1024);
      for (size_t i = 0; i < output.size(); ++i) output[i] = static_cast<uint8_t>(i * 17);
      BytesBuffer observed;
      std::exception_ptr peerError;
      std::jthread peer([&] {
        try { observed = readExactly(terminal.fd, output.size()); }
        catch (...) { peerError = std::current_exception(); }
      });
      device.writeBuffer(output);
      peer.join();
      if (peerError) std::rethrow_exception(peerError);
      require(observed == output, "Complete synchronous write across partial kernel writes");
      device.writeBuffer({});

      terminal.closeMaster();
      waitFor([&] {
        try { (void)device.readAll(); return false; }
        catch (const boost::system::system_error&) { return true; }
      }, "Peer closure surfaces a receive error");
    }
    loop.finish();
    if (loop.failure) std::rethrow_exception(loop.failure);
  }

  void testDeferredBaudReset()
  {
    Terminal terminal;
    boost::asio::io_context context;
    BoostSerialPortDevice device(context, terminal.name);
    writeAll(terminal.fd, {99, 88});
    device.startReception(); // The immediate read completes, but its handler is still queued.
    device.resetBaudrate(57600);
    context.poll();
    require(device.readAll().empty(), "Old completion cannot restore discarded input");
    writeAll(terminal.fd, {42});
    context.restart();
    context.run_for(10ms);
    require(device.readAll() == BytesBuffer{42}, "Fresh completion uses the new generation");
  }

  void testLifetime()
  {
    tinyxml2::XMLDocument xml;
    xml.Parse(R"(<protocol><msg_class name="test" id="1"><message name="EMPTY" id="1"/></msg_class></protocol>)");
    MessageDictionary dictionary(xml.RootElement());
    Terminal terminal;
    EventLoop loop;
    for (int i = 0; i < 100; ++i) {
      // Destroy each kind of transport while its serial device has a pending read.
      auto device = std::make_unique<BoostSerialPortDevice>(loop.context, terminal.name);
      std::unique_ptr<Transport> transport;
      if (i % 2 == 0) transport = std::make_unique<PprzTransport>(std::move(device), dictionary);
      else transport = std::make_unique<XbeeTransport>(std::move(device), dictionary);
      transport->start();
      writeAll(terminal.fd, {1, 2, 3});
    }
    loop.finish();
    if (loop.failure) std::rethrow_exception(loop.failure);

    boost::asio::io_context deferred;
    {
      PprzTransport transport(std::make_unique<BoostSerialPortDevice>(deferred, terminal.name), dictionary);
      transport.start();
      writeAll(terminal.fd, {4, 5, 6});
    }
    deferred.run(); // Cancellation completion after device destruction must be safe.
  }
}

int main()
{
  try {
    testTraffic();
    testDeferredBaudReset();
    testLifetime();
    std::cout << "Pseudo-terminal reception, complete writes, cancellation and lifetime passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
