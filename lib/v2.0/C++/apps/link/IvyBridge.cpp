// SPDX-License-Identifier: GPL-2.0-or-later
#include "IvyBridge.h"
#include <pprzlink/IvyMessageCodec.h>
#include <boost/asio/post.hpp>
#include <iostream>
#include <syncstream>
#include <system_error>

namespace link_app {
  namespace {
    template<class T>
    T checked(std::expected<T, std::error_code> result)
    {
      if (!result) throw std::system_error(result.error(), "Ivy");
      if constexpr (!std::is_void_v<T>) return std::move(*result);
    }
  }

  IvyBridge::IvyBridge(boost::asio::io_context &context,
                       const pprzlink::MessageDictionary &dictionary, const std::string &domain,
                       bool uplink, bool aircraftInfo, UplinkHandler handler)
    : bus(checked(ivy::Bus::create("Link", "READY")))
  {
    using Mode = pprzlink::MessageDefinition::LinkMode;
    if (uplink) {
      for (const auto &definition : dictionary.getMsgsForClass("datalink")) {
        auto mode = definition.getLinkMode();
        if (definition.getName() == "ACINFO" || definition.getName() == "ACINFO_LLA")
          mode = aircraftInfo ? Mode::Broadcasted : Mode::None;
        if (mode == Mode::None) continue;
        const auto pattern = "^([^ ]*) +(" + pprzlink::ivy_codec::escapeRegexp(definition.getName()) + "(?: .*|$))$";
        subscriptions.push_back(checked(bus.bind_raw(
          [&context, definition, mode, handler](IvyClientPtr, std::span<const std::string_view> fields) {
            try {
              if (fields.size() != 2) return;
              auto message = pprzlink::ivy_codec::parseLegacyMessageBody(definition, fields[0], fields[1]);
              boost::asio::post(context, [handler, message = std::move(message), mode]() mutable {
                handler(std::move(message), mode == Mode::Broadcasted);
              });
            } catch (const std::exception &error) {
              // A malformed command must not terminate Ivy's event loop.
              std::osyncstream(std::cerr) << "Invalid Ivy command: " << error.what() << '\n';
            }
          }, ivy::runtime_regexp(pattern))));
      }
    }
    checked(bus.start(domain));
    thread.emplace(checked(ivy::LoopThread::create(bus)));
  }

  IvyBridge::~IvyBridge() { thread.reset(); (void)bus.stop(); }
  void IvyBridge::send(const std::string &text) { checked(bus.send(text)); }
  void IvyBridge::checkError() { checked(bus.take_callback_error()); }
}
