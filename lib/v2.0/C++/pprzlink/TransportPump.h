// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file TransportPump.h
 * @brief Deliver polling transports as bounded, asynchronous message callbacks.
 * @ingroup transports
 */
#pragma once

#include <pprzlink/ReceivedMessage.h>
#include <boost/asio/executor.hpp>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

namespace pprzlink {
  /// Scheduling limits for one polling receiver.
  /// @ingroup transports
  struct TransportPumpOptions {
    std::chrono::milliseconds interval{5}; ///< Positive delay between bounded polls.
    size_t maxMessagesPerPoll = 256; ///< Maximum receive attempts, including failures, per poll.
  };

  /// Application decision after a receive failure.
  /// @ingroup transports
  enum class PumpErrorAction {
    Continue, ///< Continue polling; appropriate for a consumed malformed frame.
    Stop ///< Stop this receiver; appropriate for a terminal I/O failure.
  };

  /** Adapt any tryReceive() source to callbacks without creating a thread.
   * @ingroup transports
   * The context and source are borrowed. Serialize start(), stop(), destruction,
   * state queries and all source operations on the selected executor (or before
   * running it). A strand can provide that serialization when multiple threads
   * run the context. Finish executing callbacks before destroying their data or
   * the source. A callback may stop or destroy its own pump.
   *
   * Stop suppresses pending timer deliveries; canceled handlers retain internal
   * state until the context processes or destroys them. Reception remains timer
   * driven. For Asio serial devices, also use this context to service their I/O.
   */
  class TransportPump {
  public:
    /// Observer receiving an owned message and its matching transport metadata.
    using MessageHandler = std::function<void(ReceivedMessage)>;
    /// Receive-error policy, called on the selected executor.
    using ErrorHandler = std::function<PumpErrorAction(std::exception_ptr)>;

    /// @brief Create a stopped receiver on the context's default executor.
    /// @tparam Source Transport or other object exposing tryReceive().
    /// @param[in] context Borrowed context, kept alive through pump destruction.
    /// @param[in] source Borrowed receive source, with no competing reader.
    /// @param[in] onMessage Required observer; exceptions stop the pump and escape run().
    /// @param[in] onError Optional receive-error policy; absent means stop and rethrow.
    /// @param[in] options Positive polling interval and batch limit.
    /// @throws std::invalid_argument Empty observer or invalid scheduling limits.
    template<class Source>
      requires requires(Source &source) { { source.tryReceive() } -> std::same_as<std::optional<ReceivedMessage>>; }
    TransportPump(boost::asio::io_context &context, Source &source, MessageHandler onMessage,
                  ErrorHandler onError = {}, TransportPumpOptions options = {})
      : TransportPump(context, context.get_executor(), source, std::move(onMessage), std::move(onError), options) {}

    /// @brief Create a stopped receiver with explicitly serialized callback delivery.
    /// @tparam Source Transport or other object exposing tryReceive().
    /// @param[in] context Borrowed context owning the polling timer.
    /// @param[in] executor Executor or strand of context on which source and callbacks run.
    /// @param[in] source Borrowed receive source, with no competing reader.
    /// @param[in] onMessage Required message observer.
    /// @param[in] onError Optional receive-error policy; handler exceptions stop and escape run().
    /// @param[in] options Positive polling interval and batch limit.
    /// @throws std::invalid_argument Empty observer, invalid limits or executor from another context.
    template<class Source>
      requires requires(Source &source) { { source.tryReceive() } -> std::same_as<std::optional<ReceivedMessage>>; }
    TransportPump(boost::asio::io_context &context, boost::asio::executor executor, Source &source,
                  MessageHandler onMessage, ErrorHandler onError = {}, TransportPumpOptions options = {})
      : TransportPump(context, std::move(executor), [&source] { return source.tryReceive(); },
                      std::move(onMessage), std::move(onError), options) {}

    /// Cancel pending delivery; call after external users have finished or on the executor.
    ~TransportPump();
    /// Copying a receiver would create competing readers and is prohibited.
    TransportPump(const TransportPump &) = delete;
    /// @brief Copy assignment is prohibited.
    /// @return This operation is deleted and cannot return.
    TransportPump &operator=(const TransportPump &) = delete;
    /// Moving a live receiver is prohibited.
    TransportPump(TransportPump &&) = delete;
    /// @brief Move assignment is prohibited.
    /// @return This operation is deleted and cannot return.
    TransportPump &operator=(TransportPump &&) = delete;

    /// Start asynchronously, or do nothing if already running. Restart after stop is supported.
    /// @throws std::exception Scheduling the first timer fails.
    void start();
    /// Stop polling and invalidate pending deliveries. Safe inside either callback.
    void stop() noexcept;
    /// @return Whether the pump is started; query under the serialization contract.
    [[nodiscard]] bool isRunning() const noexcept;
    /// @return Executor to use for serialized sends and receiver controls.
    [[nodiscard]] boost::asio::executor getExecutor() const;

  private:
    /// Shared state retained by pending timers, never by a raw wrapper pointer.
    struct State;
    /// @brief Construct the type-erased source adapter.
    /// @param[in] context Timer context.
    /// @param[in] executor Source/callback executor.
    /// @param[in] receive Owned callable borrowing the source.
    /// @param[in] onMessage Message observer.
    /// @param[in] onError Receive-error policy.
    /// @param[in] options Polling limits.
    TransportPump(boost::asio::io_context &context, boost::asio::executor executor,
                  std::function<std::optional<ReceivedMessage>()> receive,
                  MessageHandler onMessage, ErrorHandler onError, TransportPumpOptions options);
    std::shared_ptr<State> state; ///< Callback-owned state; source and context remain borrowed.
  };
}
