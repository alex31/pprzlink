// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TestSupport.h"
#include <pprzlink/PprzTransport.h>
#include <pprzlink/TransportPump.h>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <iostream>
#include <thread>

using namespace pprzlink;
using namespace std::chrono_literals;

namespace {
  struct Source {
    std::function<std::optional<ReceivedMessage>()> read;
    std::optional<ReceivedMessage> tryReceive() { return read(); }
  };

  ReceivedMessage packet(const MessageDictionary &dictionary, uint8_t value = 7)
  {
    Message message(dictionary.getDefinition("VALUE"));
    message.setField("value", value);
    return {std::move(message), 9, std::nullopt, UdpEndpoint{"127.0.0.1", 1234}};
  }

  void decodedMessages(const MessageDictionary &dictionary)
  {
    boost::asio::io_context context;
    auto device = std::make_unique<MemoryDevice>();
    for (uint8_t i = 0; i < 4; ++i) {
      const auto bytes = encodePprzFrame(packet(dictionary, i).message);
      device->incoming.insert(device->incoming.end(), bytes.begin(), bytes.end());
    }
    PprzTransport source(std::move(device), dictionary);
    int count = 0;
    TransportPump pump(context, source, [&](ReceivedMessage received) {
      require(received.message.getField<uint8_t>("value") == count, "Frames retain order");
      require(received.frameSize == 9 && !received.udpPeer, "Real decoder metadata retained");
      if (++count == 4) pump.stop();
    }, {}, {1ms, 2});
    pump.start();
    pump.start();
    require(count == 0, "Start never delivers inline");
    context.run_for(100ms);
    require(count == 4 && !pump.isRunning(), "Receive multiple bounded batches and stop inside callback");
  }

  void boundsAndLifetime(const MessageDictionary &dictionary)
  {
    boost::asio::io_context context;
    int calls = 0;
    Source source{[&]() -> std::optional<ReceivedMessage> { ++calls; return packet(dictionary); }};
    TransportPump pump(context, source, [](ReceivedMessage) {}, {}, {1s, 3});
    pump.start();
    context.run_for(10ms);
    require(calls == 3, "An always-ready source cannot monopolize the loop");
    pump.stop();
    pump.start();
    pump.stop();
    pump.start();
    context.restart();
    context.run_for(10ms);
    require(calls == 6, "Canceled generations cannot deliver or reschedule after restart");
    pump.stop();
    context.poll();

    context.restart();
    {
      TransportPump destroyed(context, source, [](ReceivedMessage) {});
      destroyed.start();
    }
    context.run();
    require(calls == 6, "Destruction suppresses pending source access");

    context.restart();
    std::unique_ptr<TransportPump> self;
    self = std::make_unique<TransportPump>(context, source, [&](ReceivedMessage) { self.reset(); });
    self->start();
    context.run();
    require(!self && calls == 7, "A callback may destroy its own pump without another read");
  }

  void errorPolicy(const MessageDictionary &dictionary)
  {
    boost::asio::io_context context;
    int reads = 0, errors = 0, messages = 0;
    Source source{[&]() -> std::optional<ReceivedMessage> {
      if (++reads == 1) throw std::out_of_range("consumed bad frame");
      return packet(dictionary);
    }};
    TransportPump pump(context, source,
      [&](ReceivedMessage received) {
        require(received.udpPeer == UdpEndpoint{"127.0.0.1", 1234}, "Metadata belongs to delivery");
        ++messages;
        pump.stop();
      }, [&](std::exception_ptr error) {
        ++errors;
        require(expectException<std::out_of_range>([&] { std::rethrow_exception(error); }) == "consumed bad frame",
                "Receive exception forwarded intact");
        return PumpErrorAction::Continue;
      });
    pump.start();
    context.run();
    require(errors == 1 && messages == 1, "Explicit recovery continues after consumed malformed input");

    Source failed{[]() -> std::optional<ReceivedMessage> { throw std::runtime_error("I/O failure"); }};
    context.restart();
    TransportPump stop(context, failed, [](ReceivedMessage) {}, [](std::exception_ptr) { return PumpErrorAction::Stop; });
    stop.start();
    context.run();
    require(!stop.isRunning(), "Terminal error stops scheduling");

    context.restart();
    TransportPump unhandled(context, failed, [](ReceivedMessage) {});
    unhandled.start();
    require(expectException<std::runtime_error>([&] { context.run(); }) == "I/O failure", "Unhandled error escapes run");
    require(!unhandled.isRunning(), "Unhandled receive failure stops the pump");

    context.restart();
    errors = 0;
    TransportPump observer(context, source, [](ReceivedMessage) { throw std::logic_error("observer failure"); },
      [&](std::exception_ptr) { ++errors; return PumpErrorAction::Continue; });
    observer.start();
    expectException<std::logic_error>([&] { context.run(); });
    require(errors == 0 && !observer.isRunning(), "Application exceptions are not mislabeled as receive errors");

    context.restart();
    TransportPump handler(context, failed, [](ReceivedMessage) {}, [](std::exception_ptr) -> PumpErrorAction {
      throw std::logic_error("policy failure");
    });
    handler.start();
    expectException<std::logic_error>([&] { context.run(); });
    require(!handler.isRunning(), "Error-handler exceptions also stop scheduling");

    context.restart();
    reads = 0;
    Source alwaysBad{[&]() -> std::optional<ReceivedMessage> { ++reads; throw std::out_of_range("bad"); }};
    TransportPump bounded(context, alwaysBad, [](ReceivedMessage) {},
      [](std::exception_ptr) { return PumpErrorAction::Continue; }, {1s, 4});
    bounded.start();
    context.run_for(10ms);
    require(reads == 4, "Recovery attempts consume the batch budget too");
    bounded.stop();
  }

  void executorDelivery(const MessageDictionary &dictionary)
  {
    boost::asio::io_context context;
    auto strand = boost::asio::make_strand(context);
    int messages = 0;
    bool command = false;
    Source source{[&]() -> std::optional<ReceivedMessage> {
      require(strand.running_in_this_thread(), "Source access uses the selected strand");
      return packet(dictionary);
    }};
    TransportPump pump(context, strand, source, [&](ReceivedMessage) {
      require(strand.running_in_this_thread(), "Message callbacks use the selected strand");
      if (++messages == 10) pump.stop();
    }, {}, {1ms, 2});
    boost::asio::post(pump.getExecutor(), [&] {
      require(strand.running_in_this_thread(), "Commands share the receiver executor");
      command = true;
    });
    pump.start();
    std::exception_ptr firstFailure, secondFailure;
    std::jthread first([&] { try { context.run(); } catch (...) { firstFailure = std::current_exception(); context.stop(); } });
    std::jthread second([&] { try { context.run(); } catch (...) { secondFailure = std::current_exception(); context.stop(); } });
    first.join();
    second.join();
    if (firstFailure) std::rethrow_exception(firstFailure);
    if (secondFailure) std::rethrow_exception(secondFailure);
    require(messages == 10 && command, "Two runner threads preserve serialized reception and commands");
  }

  void callbackLifecycle(const MessageDictionary &dictionary)
  {
    boost::asio::io_context context;
    int reads = 0, messages = 0;
    Source source{[&]() -> std::optional<ReceivedMessage> { ++reads; return packet(dictionary); }};
    TransportPump pump(context, source, [&](ReceivedMessage) {
      pump.stop();
      if (++messages == 1) pump.start();
    }, {}, {1s, 8});
    pump.start();
    context.run_for(100ms);
    require(reads == 2 && messages == 2 && !pump.isRunning(),
            "Restart inside a message callback invalidates the old batch and schedules a new one");

    context.restart();
    reads = 0;
    Source failed{[&]() -> std::optional<ReceivedMessage> { ++reads; throw std::runtime_error("failure"); }};
    std::unique_ptr<TransportPump> self;
    auto capture = std::make_shared<int>(17);
    std::weak_ptr<int> released = capture;
    self = std::make_unique<TransportPump>(context, failed, [](ReceivedMessage) {},
      [&, capture](std::exception_ptr) {
        self.reset();
        require(*capture == 17, "Error-handler captures survive destruction of their pump");
        return PumpErrorAction::Continue;
      });
    capture.reset();
    self->start();
    context.run();
    require(!self && reads == 1 && released.expired(),
            "Destruction inside an error callback cancels recovery without another source access");
  }

  void invalidOptions()
  {
    boost::asio::io_context context;
    Source source{[] { return std::optional<ReceivedMessage>{}; }};
    expectException<std::invalid_argument>([&] { TransportPump pump(context, source, {}); });
    expectException<std::invalid_argument>([&] { TransportPump pump(context, source, [](auto) {}, {}, {0ms, 1}); });
    expectException<std::invalid_argument>([&] { TransportPump pump(context, source, [](auto) {}, {}, {1ms, 0}); });
    expectException<std::invalid_argument>([&] {
      TransportPump pump(context, boost::asio::executor{}, source, [](auto) {});
    });
    boost::asio::io_context other;
    expectException<std::invalid_argument>([&] {
      TransportPump pump(context, other.get_executor(), source, [](auto) {});
    });
  }
}

int main()
{
  try {
    tinyxml2::XMLDocument xml;
    xml.Parse("<protocol><msg_class name='telemetry' id='1'><message name='VALUE' id='1'><field name='value' type='uint8'/></message></msg_class></protocol>");
    MessageDictionary dictionary(xml.RootElement());
    invalidOptions();
    decodedMessages(dictionary);
    boundsAndLifetime(dictionary);
    errorPolicy(dictionary);
    callbackLifecycle(dictionary);
    executorDelivery(dictionary);
    std::cout << "Transport pump tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
