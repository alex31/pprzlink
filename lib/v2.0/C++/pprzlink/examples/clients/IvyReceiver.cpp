// SPDX-License-Identifier: LGPL-3.0-or-later
#include <pprzlink/IvyLink.h>
#include <chrono>
#include <iostream>

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "Usage: ivy_receiver messages.xml ivy-domain\n";
    return 2;
  }
  try {
    const pprzlink::MessageDictionary dictionary(argv[1]);
    pprzlink::IvyLink link(dictionary, "ivy-receiver-example", argv[2]);
    bool received = false;

    // Keep this subscription alive for as long as reception is wanted.
    auto altitude = link.subscribeMessage("CLIENT_ALTITUDE",
      [&](std::string sender, pprzlink::Message message) {
        const double metres = message.getFieldAs<double>("altitude");
        std::cout << "Aircraft " << sender << ": " << metres << " m\n";
        received = true;
        link.stop(); // This one-message example is done. Destruction happens after run().
      });
    // A bounded wait keeps the example useful even when no sender is present.
    auto timeout = link.getBus().bind_event(
      [&](std::chrono::milliseconds) { link.stop(); }, ivy::after(std::chrono::seconds(10)));
    if (!timeout) throw std::system_error(timeout.error(), "Ivy timeout");
    link.run();
    if (!received) {
      std::cerr << "No altitude received within 10 seconds\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
