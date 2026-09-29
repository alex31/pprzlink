// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Ivy/ivy.hpp>
#include <Ivy/ivy_thread.hpp>
#include <pprzlink/MessageDictionary.h>
#include <pprzlink/Message.h>
#include <boost/asio/io_context.hpp>
#include <functional>
#include <optional>

namespace link_app {
  /// Own the Ivy thread; deliver validated uplink messages on the agent's Asio loop.
  class IvyBridge {
  public:
    using UplinkHandler = std::function<void(pprzlink::Message, bool broadcast)>;
    IvyBridge(boost::asio::io_context &context, const pprzlink::MessageDictionary &dictionary,
              const std::string &domain, bool uplink, bool aircraftInfo, UplinkHandler handler);
    ~IvyBridge();
    IvyBridge(const IvyBridge &) = delete;
    IvyBridge &operator=(const IvyBridge &) = delete;
    void send(const std::string &text);
    void checkError();

  private:
    ivy::Bus bus;
    std::vector<ivy::Subscription> subscriptions;
    std::optional<ivy::LoopThread> thread; // Stop and join before destroying subscriptions.
  };
}
