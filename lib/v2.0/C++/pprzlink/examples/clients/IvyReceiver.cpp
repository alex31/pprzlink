// SPDX-License-Identifier: LGPL-3.0-or-later
#include <pprzlink/IvyAsync.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
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
    boost::asio::io_context context;
    boost::asio::steady_timer timeout(context);
    bool received = false;
    pprzlink::IvyLink link(dictionary, "ivy-receiver-example", argv[2], true);

    // Ivy owns its receive thread; application callbacks run on this context.
    pprzlink::IvyAsyncSubscription altitude;
    altitude = pprzlink::subscribeMessageOn(context.get_executor(), link, "CLIENT_ALTITUDE",
      [&](std::string sender, pprzlink::Message message) {
        const double metres = message.getFieldSI("altitude");
        std::cout << "Aircraft " << sender << ": " << metres << " m\n";
        received = true;
        altitude.reset(); // Release subscription work and suppress later deliveries.
        timeout.cancel();
      });
    // A bounded wait keeps the example useful even when no sender is present.
    timeout.expires_after(std::chrono::seconds(10));
    timeout.async_wait([&](const boost::system::error_code &error) {
      if (!error) altitude.reset();
    });
    context.run();
    link.stop();
    const auto callbackError = link.getBus().take_callback_error();
    if (!callbackError) throw std::system_error(callbackError.error(), "Ivy receive");
    if (!received) {
      std::cerr << "No altitude received within 10 seconds\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
