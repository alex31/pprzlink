#include <pprzlink/IvyLink.h>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {
  // Ivy discovers peers and their subscriptions asynchronously. Send only when
  // the receiver's subscription is visible, so the first message is not lost.
  void waitForReceiver(pprzlink::IvyLink &sender, const std::string &receiverName)
  {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
      auto peer = sender.getBus().find_application(receiverName);
      if (!peer) throw std::system_error(peer.error(), "Find Ivy receiver");
      if (*peer) {
        auto bindings = sender.getBus().application_regexps(**peer);
        if (!bindings) throw std::system_error(bindings.error(), "Read Ivy subscriptions");
        if (!bindings->empty()) return;
      }
      std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error("Timed out waiting for the Ivy receiver's subscription");
  }
}

int main(int argc, char **argv)
{
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " messages.xml [Ivy-domain]\n";
    return 1;
  }

  try {
    // 1. Load field definitions. The dictionary outlives both Ivy links.
    const pprzlink::MessageDictionary dictionary(argv[1]);
    const auto &definition = dictionary.getDefinition("EXAMPLE_ALTITUDE");
    const auto suffix = std::to_string(getpid());
    const auto domain = argc == 3 ? std::string(argv[2])
      : "127.255.255.255:" + std::to_string(40000 + getpid() % 10000);

    // 2. Register a receiver. The promise transfers the message from Ivy's
    // callback thread to main; it is declared first to outlive those callbacks.
    std::promise<pprzlink::Message> received;
    auto reception = received.get_future();
    const auto receiverName = "example-receiver-" + suffix;
    pprzlink::IvyLink receiver(dictionary, receiverName, domain, true);
    receiver.BindMessage(definition, [&](std::string, pprzlink::Message message) {
      received.set_value(std::move(message));
    });

    // 3. Create a sender and populate a message according to the XML types.
    pprzlink::IvyLink sender(dictionary, "example-sender-" + suffix, domain, true);
    pprzlink::Message outgoing(definition);
    outgoing.setSenderId(uint8_t{42});
    outgoing.addField("altitude", 123.5f);

    // 4. Send through the local Ivy bus, then read the received field.
    waitForReceiver(sender, receiverName);
    sender.sendMessage(outgoing);
    std::cout << "Sent: " << outgoing.toString() << '\n';
    if (reception.wait_for(5s) != std::future_status::ready) {
      throw std::runtime_error("Timed out waiting for the altitude message");
    }
    const auto incoming = reception.get();
    const auto altitude = incoming.getField<float>("altitude");
    const auto &source = std::get<std::string>(incoming.getSenderId());
    if (source != "42" || altitude != 123.5f) {
      throw std::runtime_error("Received message differs from the sent message");
    }
    std::cout << "Received from " << source << ": altitude = " << altitude << " m\n";
    // Destruction stops and joins the two owned Ivy threads.
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
