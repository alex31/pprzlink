/*
 * Copyright 2019 garciafa
 * This file is part of PprzLinkCPP
 *
 * PprzLinkCPP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PprzLinkCPP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with ModemTester.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef PPRZLINKCPP_IVYLINK_H
#define PPRZLINKCPP_IVYLINK_H

#include <Ivy/ivy.hpp>
#include <Ivy/ivy_thread.hpp>
#include <pprzlink/Message.h>
#include <pprzlink/MessageDictionary.h>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace pprzlink {

  using messageCallback_t = std::function<void(std::string, Message)>;
  using answererCallback_t = std::function<Message(std::string, Message)>;

  /** Owns an independent Ivy bus. The dictionary must outlive the link.
   * Sends and subscription operations may be called from multiple threads.
   * Callbacks run on the bus's event loop; synchronize shared application data.
   * Destroy the link after external callers/loops have finished, never from a callback.
   */
  class IvyLink {
  public:
    /** Start a bus, optionally running its event loop on an owned thread.
     * With threadedIvy=false, call run() to service this bus.
     */
    IvyLink(const MessageDictionary &dict, std::string appName,
            std::string domain = "127.255.255.255:2010", bool threadedIvy = false);
    ~IvyLink();

    IvyLink(const IvyLink&) = delete;
    IvyLink& operator=(const IvyLink&) = delete;
    IvyLink(IvyLink&&) = delete;
    IvyLink& operator=(IvyLink&&) = delete;

    long BindMessage(const MessageDefinition &def, messageCallback_t cb);

    /// Bind all dictionary messages from this literal sender ID.
    long BindOnSrcAc(std::string ac_id, messageCallback_t cb);
    /// Cancel a binding. An already executing callback may finish after return.
    void UnbindMessage(long bindId);
    void sendMessage(const Message &msg);
    long sendRequest(const Message &msg, messageCallback_t cb);
    long registerRequestAnswerer(const MessageDefinition &def, answererCallback_t cb);

    /// Run the loop on this thread (only when constructed with threadedIvy=false).
    void run();
    /// Request stop; safe inside callbacks. Destruction joins the owned thread.
    void stop();
    /// Borrow the bus for native API access and take_callback_error() inspection.
    /// Do not move/destroy it or run a second loop on it.
    ivy::Bus& getBus() noexcept { return bus; }

  private:
    const MessageDictionary &dictionary;
    ivy::Bus bus;
    std::mutex subscriptionsMutex;
    std::map<long, ivy::Subscription> subscriptions;
    std::atomic<long> nextBindId{1};
    // Declared last: stop/join before destroying subscriptions or the bus.
    std::optional<ivy::LoopThread> loop;

    long storeSubscription(ivy::Bus::BindResult result);
    void storeSubscription(long id, ivy::Bus::BindResult result);
  };
}
#endif // PPRZLINKCPP_IVYLINK_H
