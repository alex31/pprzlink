/*
 * This file is part of PprzLinkCPP.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

/**
 * @file IvyAsync.h
 * @brief Scoped Ivy subscriptions delivering messages on an Asio executor.
 * @ingroup ivy
 */

#ifndef PPRZLINKCPP_IVYASYNC_H
#define PPRZLINKCPP_IVYASYNC_H

#include <pprzlink/IvyLink.h>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace pprzlink {

/// @cond INTERNAL
namespace detail {
  struct IvyAsyncAccess;

  struct IvyAsyncState {
    explicit IvyAsyncState(messageCallback_t cb)
      : callback(std::make_shared<messageCallback_t>(std::move(cb))) {}
    virtual ~IvyAsyncState() = default;

    void deactivate() noexcept
    {
      std::shared_ptr<messageCallback_t> retired;
      {
        std::lock_guard lock(mutex);
        retired = std::move(callback);
        resetWork();
      }
      // Destroy user captures outside the lock: their destructors can reenter.
    }

    void deliver(std::string sender, Message message)
    {
      std::shared_ptr<messageCallback_t> current;
      {
        std::lock_guard lock(mutex);
        current = callback;
      }
      // Admission above may race with reset. Keep admitted callback storage
      // alive, including when that callback destroys its own subscription.
      if (current) (*current)(std::move(sender), std::move(message));
    }

    virtual void resetWork() noexcept = 0;
    std::mutex mutex;
    std::shared_ptr<messageCallback_t> callback;
  };

  template<class Executor>
  struct IvyAsyncExecutorState final : IvyAsyncState,
                                       std::enable_shared_from_this<IvyAsyncExecutorState<Executor>> {
    IvyAsyncExecutorState(const Executor &executor, messageCallback_t callback)
      : IvyAsyncState(std::move(callback)), work(executor) {}

    void enqueue(std::string sender, Message message)
    {
      std::lock_guard lock(mutex);
      if (!callback) return;
      // post never invokes the observer inline. Holding the lock through post
      // ensures no new work can be submitted after deactivate returns.
      boost::asio::post(work.get_executor(),
        [self = this->shared_from_this(), sender = std::move(sender), message = std::move(message)]() mutable {
          self->deliver(std::move(sender), std::move(message));
        });
    }

    void resetWork() noexcept override { work.reset(); }
    boost::asio::executor_work_guard<Executor> work;
  };

  template<class State>
  struct IvyAsyncSource {
    explicit IvyAsyncSource(std::shared_ptr<State> state) : state(std::move(state)) {}
    ~IvyAsyncSource() { state->deactivate(); }
    std::shared_ptr<State> state;
  };
}
/// @endcond

/**
 * @brief Move-only owner of an Ivy subscription forwarded to an Asio executor.
 * @ingroup ivy
 *
 * Keep this token alive to receive messages. Reset/destruction disables queued
 * deliveries and unregisters the Ivy binding. A callback already admitted for
 * execution may finish; reset never waits for it. Reset from that callback is
 * supported. Operations on this token itself must not race with one another.
 *
 * The executor's context must outlive the subscription and pending handlers.
 * Active subscriptions keep executor work outstanding. Reset the token when
 * shutting down, then drain/join the executor before destroying captured state.
 * Moving transfers ownership and leaves the source empty. A token may outlive
 * its IvyLink; destruction of the native binding cancels queued deliveries.
 */
class IvyAsyncSubscription {
public:
  /// @brief Construct an empty subscription with no outstanding executor work.
  IvyAsyncSubscription() noexcept = default;
  /// @brief Cancel the subscription and any deliveries not yet admitted.
  ~IvyAsyncSubscription() { reset(); }
  /// @brief Transfer ownership and leave the source empty.
  IvyAsyncSubscription(IvyAsyncSubscription &&) noexcept = default;
  /** @brief Cancel this subscription and take ownership from another token.
   * @param[in] other Token to move; self-assignment leaves this token unchanged.
   * @return This token.
   */
  IvyAsyncSubscription &operator=(IvyAsyncSubscription &&other) noexcept
  {
    if (this != &other) {
      reset();
      state = std::move(other.state);
      subscription = std::move(other.subscription);
    }
    return *this;
  }
  /// @brief Subscription ownership cannot be copied.
  IvyAsyncSubscription(const IvyAsyncSubscription &) = delete;
  /// @brief Subscription ownership cannot be copy-assigned.
  /// @return This operation is deleted and cannot return.
  IvyAsyncSubscription &operator=(const IvyAsyncSubscription &) = delete;

  /** @brief Cancel queued deliveries, release executor work and unbind Ivy.
   * Repeated calls are harmless. Already-admitted callbacks may finish after
   * return. Native unbind errors are ignored, as by ivy::Subscription's destructor.
   */
  void reset() noexcept
  {
    auto retired = std::move(state);
    if (retired) retired->deactivate();
    subscription = ivy::Subscription{};
  }

  /// @brief Inspect the underlying native registration; this is a snapshot.
  /// @return False for empty/moved/canceled tokens or a stopped/destroyed Ivy bus.
  explicit operator bool() const noexcept { return subscription.is_bound(); }

private:
  friend struct detail::IvyAsyncAccess;
  IvyAsyncSubscription(std::shared_ptr<detail::IvyAsyncState> state, ivy::Subscription subscription) noexcept
    : state(std::move(state)), subscription(std::move(subscription)) {}
  std::shared_ptr<detail::IvyAsyncState> state;
  ivy::Subscription subscription;
};

/// @cond INTERNAL
namespace detail {
  struct IvyAsyncAccess {
    template<class Executor, class Bind>
    static IvyAsyncSubscription subscribe(const Executor &executor, messageCallback_t callback, Bind bind)
    {
      if (!callback) throw std::invalid_argument("Ivy asynchronous callback must not be empty");
      using State = IvyAsyncExecutorState<Executor>;
      auto state = std::make_shared<State>(executor, std::move(callback));
      auto source = std::make_shared<IvyAsyncSource<State>>(state);
      auto subscription = bind([source = std::move(source)](std::string sender, Message message) {
        source->state->enqueue(std::move(sender), std::move(message));
      });
      return IvyAsyncSubscription(std::move(state), std::move(subscription));
    }
  };
}
/// @endcond

/**
 * @brief Subscribe by definition and always post decoded messages to an executor.
 * @ingroup ivy
 *
 * The native Ivy loop still performs reception and decoding. Sender and Message
 * are owned values in the posted handler. No user callback runs under an adapter
 * lock. Use a strand if callbacks must be serialized on a multi-threaded context.
 * Callback exceptions propagate through the executor's run()/poll(), without
 * stopping Ivy. Decode/post failures retain IvyLink's native callback error path.
 * Queued messages are not bounded; callbacks must keep pace with reception.
 *
 * @tparam Executor Asio executor type, including io_context and strand executors.
 * @param[in] executor Executor used for every application callback.
 * @param[in] link Ivy bus whose native event loop must be serviced independently.
 * @param[in] definition Schema copied into the native subscription.
 * @param[in] callback Observer receiving owned sender text and decoded message.
 * @return Scoped token controlling reception and queued deliveries.
 * @throws std::invalid_argument The callback is empty.
 * @throws std::exception Allocation, schema or native Ivy binding fails.
 */
template<class Executor>
[[nodiscard]] IvyAsyncSubscription subscribeMessageOn(const Executor &executor, IvyLink &link,
    const MessageDefinition &definition, messageCallback_t callback)
{
  return detail::IvyAsyncAccess::subscribe(executor, std::move(callback),
    [&](messageCallback_t forwarded) { return link.subscribeMessage(definition, std::move(forwarded)); });
}

/** @brief Subscribe by schema name, with the same delivery rules as the definition overload.
 * @ingroup ivy
 * @tparam Executor Asio executor type.
 * @param[in] executor Executor used for every application callback.
 * @param[in] link Ivy bus whose event loop is serviced independently.
 * @param[in] name Exact message name in the link's dictionary.
 * @param[in] callback Observer receiving owned sender text and decoded message.
 * @return Scoped token controlling reception and queued deliveries.
 * @throws std::invalid_argument The callback is empty.
 * @throws std::exception Dictionary lookup, allocation or native Ivy binding fails.
 */
template<class Executor>
[[nodiscard]] IvyAsyncSubscription subscribeMessageOn(const Executor &executor, IvyLink &link,
    std::string_view name, messageCallback_t callback)
{
  return detail::IvyAsyncAccess::subscribe(executor, std::move(callback),
    [&](messageCallback_t forwarded) { return link.subscribeMessage(name, std::move(forwarded)); });
}

/** @brief Subscribe to one literal sender and post its messages to an executor.
 * @ingroup ivy
 * Delivery, error and cancellation rules match subscribeMessageOn().
 * @tparam Executor Asio executor type.
 * @param[in] executor Executor used for every application callback.
 * @param[in] link Ivy bus whose event loop is serviced independently.
 * @param[in] sender Literal sender selector, not a regular expression.
 * @param[in] callback Observer receiving owned sender text and decoded message.
 * @return Scoped token controlling reception and queued deliveries.
 * @throws std::invalid_argument The callback is empty.
 * @throws std::exception Allocation or native Ivy binding fails.
 */
template<class Executor>
[[nodiscard]] IvyAsyncSubscription subscribeSenderOn(const Executor &executor, IvyLink &link,
    std::string sender, messageCallback_t callback)
{
  return detail::IvyAsyncAccess::subscribe(executor, std::move(callback),
    [&](messageCallback_t forwarded) { return link.subscribeSender(std::move(sender), std::move(forwarded)); });
}

}
#endif // PPRZLINKCPP_IVYASYNC_H
