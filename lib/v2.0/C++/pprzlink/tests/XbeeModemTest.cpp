#include "TestSupport.h"
#include <pprzlink/XbeeTransport.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <utility>

using namespace pprzlink;
using namespace std::chrono_literals;

namespace {
  struct ModemDevice : MemoryDevice {
    std::vector<std::string> writes;
    bool failRead = false, failWrite = false;
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
    const auto start = XbeeModem::Clock::now();
    transport.startInitialization(quickConfiguration(), start);
    require(!transport.isReady(), "Starting initialization blocks transport");
    expectException<std::logic_error>([&] { transport.sendMessage(message); });
    require(device.writes.empty(), "Blocked send writes no serial bytes");
    transport.pollInitialization(start + 10ms);
    device.reply("OK\r");
    transport.pollInitialization(start + 20ms);
    for (int i = 0; i < 2; ++i) {
      device.reply("OK\r");
      require(!transport.pollInitialization(start + 21ms + i * 1ms), "Not ready before exit acknowledgement");
      expectException<std::logic_error>([&] { transport.sendMessageTo64(message, 42); });
    }
    device.reply("OK\r\n");
    device.incoming.insert(device.incoming.end(), {0x7e, 0, 9, 0x81, 0, 42, 50, 0, 42, 0, 1, 1, 0xf6});
    require(transport.pollInitialization(start + 24ms) && transport.isReady(), "Transport initialized");
    const auto received = transport.getMessage();
    require(received && received->getDefinition().getName() == "EMPTY", "API frame after ATCN survives initialization");
    transport.sendMessage(message);
    require(device.outgoing.front() == 0x7e, "Binary send allowed after initialization");

    transport.startInitialization(quickConfiguration(), start);
    transport.pollInitialization(start + 10ms);
    expectException<std::runtime_error>([&] { transport.pollInitialization(start + 40ms); });
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
