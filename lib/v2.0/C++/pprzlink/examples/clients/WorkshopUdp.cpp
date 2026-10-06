// SPDX-License-Identifier: LGPL-3.0-or-later
// UDP workshop: receive, emit, or exchange a message in one process.
#include <pprzlink/UdpTransport.h>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <cctype>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>

using namespace std::chrono_literals;

namespace {
  pprzlink::Message altitude(const pprzlink::MessageDictionary &dictionary, double metres)
  {
    pprzlink::Message message(dictionary.getDefinition("GUIDE_ALTITUDE"));
    message.setSenderId(42);
    message.setReceiverId(0);
    message.setFieldSI("altitude", metres);
    return message;
  }
  void print(const pprzlink::Message &message, const pprzlink::ReceiveInfo &info)
  {
    const auto &peer = info.udpPeer.value();
    std::cout << "Reçu de " << peer.address << ':' << peer.port
              << " : " << message.toString() << std::endl;
  }
  int both(const pprzlink::MessageDictionary &dictionary)
  {
    boost::asio::io_context context;
    pprzlink::UdpTransport receiver(context, dictionary, {.local = {"127.0.0.1", 0}});
    pprzlink::UdpTransport sender(context, dictionary, {.local = {"127.0.0.1", 0}});
    boost::asio::steady_timer timeout(context, 2s);
    bool received = false;
    timeout.async_wait([&](const boost::system::error_code &error) { if (!error) receiver.stop(); });
    receiver.bind("GUIDE_ALTITUDE", {.senderId = 42, .udpPeer = sender.localEndpoint()},
      [&](const pprzlink::Message &message, const pprzlink::ReceiveInfo &info) {
        print(message, info);
        received = true;
        receiver.stop();
        timeout.cancel();
      });
    receiver.start();
    const auto destination = receiver.localEndpoint();
    const auto sent = sender.sendMessage(altitude(dictionary, 123.5), destination);
    std::cout << "Envoyé : " << sent << " octets vers le port " << destination.port << std::endl;
    context.run();
    if (!received) throw std::runtime_error("Aucun message UDP reçu dans le délai prévu");
    return 0;
  }
  int receive(const pprzlink::MessageDictionary &dictionary)
  {
    boost::asio::io_context context;
    pprzlink::UdpTransport receiver(context, dictionary, {.local = {"127.0.0.1", 4242}});
    boost::asio::steady_timer timeout(context, 30s);
    boost::asio::signal_set signals(context, SIGINT, SIGTERM);
    unsigned int count = 0;
    auto stop = [&] { receiver.stop(); timeout.cancel(); signals.cancel(); };
    timeout.async_wait([&](const boost::system::error_code &error) { if (!error) stop(); });
    signals.async_wait([&](const boost::system::error_code &error, int) { if (!error) stop(); });
    receiver.bind(pprzlink::ALL, [&](const pprzlink::Message &message, const pprzlink::ReceiveInfo &info) {
      print(message, info);
      ++count;
    });
    receiver.start();
    std::cout << "Récepteur prêt sur 127.0.0.1:4242 (30 s, ou Ctrl+C)" << std::endl;
    context.run();
    if (!count) throw std::runtime_error("Aucun message UDP reçu");
    return 0;
  }
  int emit(const pprzlink::MessageDictionary &dictionary)
  {
    boost::asio::io_context context;
    pprzlink::UdpTransport sender(context, dictionary, {.local = {"127.0.0.1", 0}});
    const pprzlink::UdpEndpoint destination{"127.0.0.1", 4242};
    for (const double metres : {100.0, 110.0, 123.5}) {
      const auto sent = sender.sendMessage(altitude(dictionary, metres), destination);
      std::cout << "Envoyé : " << sent << " octets vers le port " << destination.port << std::endl;
    }
    return 0;
  }
}

int main(int argc, char **argv)
{
  if (argc != 3 || !argv[2][0]) {
    std::cerr << "Usage: " << argv[0] << " messages.xml both|emitter|receiver\n";
    return 2;
  }
  try {
    const pprzlink::MessageDictionary dictionary(argv[1]);
    switch (std::toupper(static_cast<unsigned char>(argv[2][0]))) {
      case 'B': return both(dictionary);
      case 'E': return emit(dictionary);
      case 'R': return receive(dictionary);
      default: throw std::invalid_argument("Mode attendu : both, emitter ou receiver");
    }
  } catch (const std::exception &error) {
    std::cerr << "Erreur : " << error.what() << '\n';
    return 1;
  }
}
