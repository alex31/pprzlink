#include "TestSupport.h"
#include <pprzlink/XbeeTransport.h>
#include <boost/asio/post.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <utility>

using namespace pprzlink;
using namespace std::chrono_literals;

namespace {
  struct ModemDevice : MemoryDevice {
    std::vector<std::string> writes;
    bool failRead = false, failWrite = false, autoReplies = false;
    BytesBuffer readAll() override
    {
      if (failRead) throw std::runtime_error("read failed");
      return MemoryDevice::readAll();
    }
    void writeBuffer(const BytesBuffer &bytes) override
    {
      if (failWrite) throw std::runtime_error("write failed");
      writes.emplace_back(bytes.begin(), bytes.end());
      MemoryDevice::writeBuffer(bytes);
      if (autoReplies) {
        reply("OK\r");
        if (writes.back() == "ATCN\r") incoming.insert(incoming.end(), {0x7e, 0, 9, 0x81, 0, 42, 50, 0, 42, 0, 1, 1, 0xf6});
        boost::asio::post(context, [this] { notify(); });
      }
    }
    void reply(std::string_view text) { incoming.assign(text.begin(), text.end()); }
  };

  XbeeConfiguration quickConfiguration()
  {
    XbeeConfiguration options;
    options.targetBaudrate.reset(); // Exercise the explicit fixed-baud mode here.
    options.guardTime = 10ms;
    options.responseTimeout = 20ms;
    return options;
  }

  auto enter(XbeeModem &modem, ModemDevice &device, XbeeModem::TimePoint start)
  {
    device.reply("OK\r"); // A stale reply must not acknowledge the new escape sequence.
    require(!modem.poll(device, start) && device.writes.empty(), "Initial guard is silent");
    require(!modem.poll(device, start + 10ms) && device.writes.back() == "+++", "Escape without CR");
    device.reply("O");
    require(!modem.poll(device, start + 11ms), "Fragmented entry reply");
    device.reply("K\r\n");
    require(!modem.poll(device, start + 12ms) && device.writes.size() == 1, "Early OK preserves post-escape guard");
    require(!modem.poll(device, start + 20ms) && device.writes.back().starts_with("ATMY"), "Guard expires before first command");
    return start + 20ms;
  }

  void testSequence()
  {
    const auto start = XbeeModem::Clock::now();
    auto options = quickConfiguration();
    options.localAddress = 0x234;
    options.channel = 0x15;
    XbeeModem modem(options, start);
    ModemDevice device;
    auto now = enter(modem, device, start);
    for (const auto expected : {"ATCH15\r", "ATAP1\r", "ATCN\r"}) {
      device.reply("OK\rOK\r"); // A duplicate in the same read cannot ACK the next command.
      require(!modem.poll(device, now += 1ms) && device.writes.back() == expected, "One verified AT command at a time");
      require(!modem.poll(device, now += 1ms), "No premature success from duplicate OK");
    }
    device.reply("OK\r");
    device.incoming.insert(device.incoming.end(), {0x7e, 0, 2, 0x8a, 0, 0x75});
    require(modem.poll(device, now += 1ms) && modem.isReady(), "Ready only after ATCN OK");
    require(device.writes == std::vector<std::string>{"+++", "ATMY0234\r", "ATCH15\r", "ATAP1\r", "ATCN\r"},
            "Exact sequence and no ATWR or baud/PAN changes");
    require(modem.takeRemainingBytes() == BytesBuffer{0x7e, 0, 2, 0x8a, 0, 0x75}, "Preserve final-read API bytes");
    require(modem.takeRemainingBytes().empty() && modem.poll(device, now), "Completion is idempotent");
  }

  void testFailures()
  {
    const auto start = XbeeModem::Clock::now();
    for (const bool timeout : {false, true}) {
      // Every awaited reply, including entry and ATCN, must be checked.
      for (int step = 0; step < 4; ++step) {
        XbeeModem modem(quickConfiguration(), start);
        ModemDevice device;
        auto now = start + 10ms;
        modem.poll(device, now);
        if (step != 0) {
          device.reply("OK\r");
          now += 10ms;
          modem.poll(device, now);
          for (int i = 1; i < step; ++i) {
            device.reply("OK\r");
            modem.poll(device, now += 1ms);
          }
        }
        const auto count = device.writes.size();
        if (timeout) now += 40ms;
        else device.reply("ERROR\r");
        const auto error = expectException<std::runtime_error>([&] { modem.poll(device, now); });
        require(error.find(timeout ? "timeout" : "ERROR") != std::string::npos && !modem.isReady(), "Actionable AT failure");
        device.reply("OK\r");
        expectException<std::runtime_error>([&] { modem.poll(device, now + 1ms); });
        require(device.writes.size() == count, "No further writes after failed initialization");
      }
    }

    for (const auto &response : {std::string("NOK\r"), std::string(65, 'x')}) {
      XbeeModem modem(quickConfiguration(), start);
      ModemDevice device;
      const auto now = enter(modem, device, start);
      device.reply(response);
      expectException<std::runtime_error>([&] { modem.poll(device, now + 1ms); });
      require(device.writes.size() == 2, "Unexpected/overlong AT response blocks next command");
    }

    XbeeModem modem(quickConfiguration(), start);
    ModemDevice device;
    modem.poll(device, start + 10ms);
    device.reply("NOK\r");
    modem.poll(device, start + 20ms);
    require(device.writes.size() == 1, "NOK is not an entry acknowledgement");
    expectException<std::runtime_error>([&] { modem.poll(device, start + 40ms); });

    for (const bool reading : {false, true}) {
      XbeeModem failed(quickConfiguration(), start);
      ModemDevice io;
      io.failRead = reading;
      io.failWrite = !reading;
      expectException<std::runtime_error>([&] { failed.poll(io, start + 10ms); });
      io.failRead = io.failWrite = false;
      expectException<std::runtime_error>([&] { failed.poll(io, start + 20ms); });
      require(io.writes.empty() && !failed.isReady(), "I/O failures are terminal");
    }
  }

  void testTransportGating()
  {
    tinyxml2::XMLDocument xml;
    xml.Parse(R"(<protocol><msg_class name="test" id="1"><message name="EMPTY" id="1"/></msg_class></protocol>)");
    MessageDictionary dictionary(xml.RootElement());
    Message message(dictionary.getDefinition("EMPTY"));
    auto deviceOwner = std::make_unique<ModemDevice>();
    auto &device = *deviceOwner;
    XbeeTransport transport(std::move(deviceOwner), dictionary);
    device.autoReplies = true;
    transport.startInitialization(quickConfiguration());
    require(!transport.isReady(), "Starting initialization blocks transport");
    expectException<std::logic_error>([&] { transport.sendMessage(message); });
    require(device.writes.empty(), "Blocked send writes no serial bytes");
    int received = 0, ready = 0;
    transport.bind("EMPTY", [&](const Message &) { ++received; });
    transport.onReady([&] { ++ready; });
    transport.start();
    device.context.run_for(100ms);
    require(transport.isReady() && ready == 1 && received == 1,
            "Input notifications and guard deadlines initialize the modem and preserve post-ATCN frames");
    transport.sendMessage(message);
    require(device.outgoing.front() == 0x7e, "Binary send allowed after initialization");
    device.autoReplies = false;
    std::vector<ReceiveError::Kind> failures;
    transport.onError([&](const ReceiveError &error) { failures.push_back(error.kind); });
    transport.startInitialization(quickConfiguration());
    device.context.restart();
    device.context.run_for(100ms);
    require(failures == std::vector<ReceiveError::Kind>{ReceiveError::Kind::Initialization} && !transport.isRunning(),
            "A reply deadline reports initialization failure and stops reception without polling");
    const auto count = device.writes.size();
    expectException<std::logic_error>([&] { transport.sendMessageTo16(message, 42); });
    require(device.writes.size() == count, "Failed initialization never enables binary sends");
  }

  void testValidation()
  {
    for (const int invalid : {0, 11, 24, 255}) {
      auto options = quickConfiguration();
      options.channel = static_cast<uint8_t>(invalid);
      expectException<std::invalid_argument>([&] { XbeeModem modem(options); });
    }
    for (const auto duration : {0ms, -1ms, 60001ms}) {
      auto options = quickConfiguration();
      options.guardTime = duration;
      expectException<std::invalid_argument>([&] { XbeeModem modem(options); });
      options = quickConfiguration();
      options.responseTimeout = duration;
      expectException<std::invalid_argument>([&] { XbeeModem modem(options); });
    }
  }
}

int main()
{
  try {
    testSequence();
    testFailures();
    testTransportGating();
    testValidation();
    std::cout << "AT guards, acknowledgements, deadlines, failures and transport gating passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
