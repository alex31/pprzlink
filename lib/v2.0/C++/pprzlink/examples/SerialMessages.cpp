#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/IvyMessageCodec.h>
#include <pprzlink/PprzTransport.h>
#include <pprzlink/XbeeTransport.h>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <charconv>
#include <chrono>
#include <concepts>
#include <csignal>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <optional>
#include <string_view>
#include <type_traits>

using namespace pprzlink;
using namespace std::chrono_literals;

namespace {
  constexpr auto usage = R"(Usage: serial_messages -messages FILE -d PORT [options]
  -transport pprz|xbee   Serial framing (default: pprz)
  -s BAUD               Target baud rate (default: 57600), 8 data bits, no parity, 1 stop bit
  -hfc                  Enable RTS/CTS hardware flow control (default: none)
  -send "NAME fields"    Send one message, with XML fields in Ivy text order
  -sender ID            PPRZLINK sender, 0..255 (default: 0)
  -receiver ID          PPRZLINK receiver, 0..255 (default: 255 = broadcast)
  -xbeesan              Validate complete XBee frames before sending; errors go to stderr
  -xbee-dest ADDRESS    Override 16-bit radio destination (decimal or 0x...)
  -xbee-dest64 ADDRESS  Override 64-bit radio destination (decimal or 0x...)
  -xbee_addr ADDRESS   Local XBee MY address (default: 0x100)
  -ch CHANNEL         Set XBee channel, 0x0c..0x17 (default: unchanged)
  -xbee-no-init        Skip AT initialization for an already configured modem
  -xbee-no-autobaud    Use -s directly without detecting/changing the modem baud rate
  -xbee-guard-ms MS    Silence before/after +++ (default: 2000)
  -xbee-timeout-ms MS  Deadline for each AT reply (default: 2000)
  -duration SECONDS     Stop after this duration (default: 0 = until Ctrl-C)
  -h, --help            Show this help

Receive messages and radio statuses on stdout. No Ivy bus is started.
The xbee mode automatically detects the modem baud rate and changes it to -s.
A changed baud rate is verified and saved with ATWR before configuring MY/CH/AP.
ATWR also saves the modem's other existing settings. MY/CH/AP changes made by
this program remain volatile. Legacy 802.15.4 RX16/RX64 API output is required.
Standard rates from 1200 to 115200 are probed, starting with the target rate.
Failed transmissions are not retried by this example. Payloads are limited to 100 bytes including
the PPRZLINK v2 header. XML defines messages, not the transport mode.
)";

  struct Options {
    std::string messages, device, transport = "pprz", send;
    unsigned int baud = 57600, duration = 0;
    uint8_t sender = 0, receiver = 255;
    std::optional<uint16_t> destination16;
    std::optional<uint64_t> destination64;
    XbeeConfiguration xbeeConfiguration;
    bool skipXbeeInitialization = false, hasXbeeConfiguration = false;
    bool autoBaud = true;
    bool hardwareFlowControl = false, help = false, xbeeSanity = false;
  };

  template<class T>
  T number(std::string_view text, std::string_view option)
  {
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) { text.remove_prefix(2); base = 16; }
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() ||
        value > std::numeric_limits<T>::max()) {
      throw std::invalid_argument("Invalid value for " + std::string(option));
    }
    return static_cast<T>(value);
  }

  Options parseOptions(int argc, char **argv)
  {
    Options options;
    for (int i = 1; i < argc; ++i) {
      const std::string_view option(argv[i]);
      if (option == "-h" || option == "--help") { options.help = true; return options; }
      if (option == "-hfc") { options.hardwareFlowControl = true; continue; }
      if (option == "-xbeesan") { options.xbeeSanity = true; continue; }
      if (option == "-xbee-no-init") { options.skipXbeeInitialization = true; continue; }
      if (option == "-xbee-no-autobaud") {
        options.autoBaud = false;
        options.hasXbeeConfiguration = true;
        continue;
      }
      if (i + 1 == argc) throw std::invalid_argument("Missing value for " + std::string(option));
      const std::string_view value(argv[++i]);
      if (option == "-messages") options.messages = value;
      else if (option == "-d") options.device = value;
      else if (option == "-transport") options.transport = value;
      else if (option == "-s") options.baud = number<unsigned int>(value, option);
      else if (option == "-send") options.send = value;
      else if (option == "-sender") options.sender = number<uint8_t>(value, option);
      else if (option == "-receiver") options.receiver = number<uint8_t>(value, option);
      else if (option == "-xbee-dest") options.destination16 = number<uint16_t>(value, option);
      else if (option == "-xbee-dest64") options.destination64 = number<uint64_t>(value, option);
      else if (option == "-xbee_addr") {
        options.xbeeConfiguration.localAddress = number<uint16_t>(value, option);
        options.hasXbeeConfiguration = true;
      } else if (option == "-ch") {
        options.xbeeConfiguration.channel = number<uint8_t>(value, option);
        options.hasXbeeConfiguration = true;
      } else if (option == "-xbee-guard-ms") {
        options.xbeeConfiguration.guardTime = std::chrono::milliseconds(number<unsigned int>(value, option));
        options.hasXbeeConfiguration = true;
      } else if (option == "-xbee-timeout-ms") {
        options.xbeeConfiguration.responseTimeout = std::chrono::milliseconds(number<unsigned int>(value, option));
        options.hasXbeeConfiguration = true;
      } else if (option == "-duration") options.duration = number<unsigned int>(value, option);
      else throw std::invalid_argument("Unknown option: " + std::string(option));
    }
    if (options.transport != "pprz" && options.transport != "xbee") {
      throw std::invalid_argument("-transport must be pprz or xbee");
    }
    if (options.messages.empty() || options.device.empty() || options.baud == 0) {
      throw std::invalid_argument("Supply -messages FILE, -d PORT and a positive baud rate");
    }
    if ((options.destination16 || options.destination64 || options.skipXbeeInitialization ||
         options.hasXbeeConfiguration || options.xbeeSanity) && options.transport != "xbee") {
      throw std::invalid_argument("XBee options require -transport xbee");
    }
    if (options.destination16 && options.destination64) {
      throw std::invalid_argument("Choose either -xbee-dest or -xbee-dest64");
    }
    if (options.skipXbeeInitialization && options.hasXbeeConfiguration) {
      throw std::invalid_argument("XBee configuration options conflict with -xbee-no-init");
    }
    options.xbeeConfiguration.targetBaudrate =
        options.autoBaud && !options.skipXbeeInitialization
        ? std::optional(options.baud) : std::nullopt;
    // Validate baud target, timings and channel before opening the serial port.
    if (options.transport == "xbee") (void)XbeeModem(options.xbeeConfiguration);
    return options;
  }

  void printStatus(const XbeeTransport::RadioStatus &status)
  {
    std::visit([](const auto &event) {
      using T = std::decay_t<decltype(event)>;
      if constexpr (std::same_as<T, XbeeTransport::TransmitStatus>) {
        std::cout << "XBee TX status: frame=" << unsigned(event.frameId)
                  << " status=" << unsigned(event.status) << std::endl;
      } else if constexpr (std::same_as<T, XbeeTransport::ModemStatus>) {
        std::cout << "XBee modem status: " << unsigned(event.status) << std::endl;
      } else {
        std::cout << "XBee AT response: " << event.command[0] << event.command[1]
                  << " frame=" << unsigned(event.frameId) << " status=" << unsigned(event.status) << std::endl;
      }
    }, status);
  }

  void run(const Options &options)
  {
    MessageDictionary dictionary(options.messages);
    std::optional<Message> outgoing;
    if (!options.send.empty()) {
      const auto name = options.send.substr(0, options.send.find(' '));
      outgoing = ivy_codec::parseMessageBody(dictionary.getDefinition(name),
                                             std::to_string(options.sender), options.send);
      outgoing->setReceiverId(options.receiver);
    }

    boost::asio::io_context context;
    auto device = std::make_unique<BoostSerialPortDevice>(context, options.device);
    device->setBaudrate(BoostSerialPortDevice::Baudrate(options.baud));
    device->setDataBits(BoostSerialPortDevice::DataBits(8));
    device->setParity(BoostSerialPortDevice::Parity(BoostSerialPortDevice::Parity::none));
    device->setStopBits(BoostSerialPortDevice::StopBits(BoostSerialPortDevice::StopBits::one));
    device->setFlowcontrol(BoostSerialPortDevice::Flowcontrol(options.hardwareFlowControl
        ? BoostSerialPortDevice::Flowcontrol::hardware : BoostSerialPortDevice::Flowcontrol::none));

    std::unique_ptr<Transport> transport;
    XbeeTransport *xbee = nullptr;
    if (options.transport == "xbee") {
      auto radio = std::make_unique<XbeeTransport>(std::move(device), dictionary);
      radio->setStatusCallback(printStatus);
      radio->setSanityChecksEnabled(options.xbeeSanity);
      xbee = radio.get();
      transport = std::move(radio);
    } else {
      transport = std::make_unique<PprzTransport>(std::move(device), dictionary);
    }

    boost::asio::signal_set signals(context, SIGINT, SIGTERM);
    boost::asio::steady_timer duration(context);
    auto stop = [&] { transport->stop(); signals.cancel(); duration.cancel(); };
    signals.async_wait([&](const boost::system::error_code &error, int) { if (!error) stop(); });
    transport->bind(ALL, [](const Message &message) {
      std::cout << ivy_codec::serializeMessage(message) << std::endl;
    });
    transport->onError([](const ReceiveError &error) {
      if (error.kind == ReceiveError::Kind::Decode) std::cerr << "Discarded message: " << error.message << '\n';
      else std::rethrow_exception(error.exception);
    });
    auto listen = [&] {
      if (xbee && !options.skipXbeeInitialization) {
        if (const auto baud = xbee->getBaudrateInfo()) {
          std::cout << "XBee baud rate: detected " << baud->detected
                    << ", configured " << baud->configured
                    << (baud->savedToFlash ? " (saved to flash)" : " (unchanged)") << std::endl;
        }
        std::cout << "XBee modem ready (AP=1)" << std::endl;
      }
      if (outgoing) {
        size_t written;
        if (options.destination16) written = xbee->sendMessageTo16(*outgoing, *options.destination16);
        else if (options.destination64) written = xbee->sendMessageTo64(*outgoing, *options.destination64);
        else written = transport->sendMessage(*outgoing);
        std::cout << "Wrote " << written << " serial bytes" << std::endl;
      }
      std::cout << "Listening with " << options.transport << " transport" << std::endl;
      if (options.duration) {
        duration.expires_after(std::chrono::seconds(options.duration));
        duration.async_wait([&](const boost::system::error_code &error) { if (!error) stop(); });
      }
    };
    if (xbee) {
      xbee->onReady(listen);
      if (!options.skipXbeeInitialization) {
        xbee->startInitialization(options.xbeeConfiguration);
        std::cout << "Initializing XBee modem" << std::endl;
      }
    }
    transport->start();
    if (!xbee) listen();
    context.run();
  }
}

int main(int argc, char **argv)
{
  try {
    const auto options = parseOptions(argc, argv);
    if (options.help) { std::cout << usage; return 0; }
    run(options);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
