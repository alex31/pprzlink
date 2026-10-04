// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file TransportPump.cpp
 * @brief Timer scheduling and cancellation for callback-driven polling sources.
 * @ingroup transports
 */
#include "TransportPump.h"
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/system_error.hpp>
#include <stdexcept>

namespace pprzlink {
  /// Serialized receive state, retained until canceled handlers have completed.
  struct TransportPump::State : std::enable_shared_from_this<State> {
    boost::asio::steady_timer timer; ///< Bounded polling scheduler.
    boost::asio::executor executor; ///< Shared serialization domain for callbacks and source calls.
    std::function<std::optional<ReceivedMessage>()> receive; ///< Borrowed source operation.
    MessageHandler onMessage; ///< Required message observer.
    ErrorHandler onError; ///< Optional receive-error policy.
    TransportPumpOptions options; ///< Positive interval and batch budget.
    bool running = false; ///< Only accessed under the executor contract.
    size_t generation = 0; ///< Invalidates handlers from earlier start/stop cycles.

    /// @brief Store the source and callbacks without initiating I/O.
    /// @param[in] context Timer context.
    /// @param[in] executor Serialization domain for source calls and observers.
    /// @param[in] receive Callable borrowing the receive source.
    /// @param[in] onMessage Required message observer.
    /// @param[in] onError Optional receive-error policy.
    /// @param[in] options Validated polling limits.
    State(boost::asio::io_context &context, boost::asio::executor executor,
          std::function<std::optional<ReceivedMessage>()> receive, MessageHandler onMessage,
          ErrorHandler onError, TransportPumpOptions options)
      : timer(context), executor(std::move(executor)), receive(std::move(receive)),
        onMessage(std::move(onMessage)), onError(std::move(onError)), options(options) {}

    /// @brief Invalidate older handlers and cancel the timer without throwing.
    void stop() noexcept
    {
      running = false;
      ++generation;
      boost::system::error_code ignored;
      timer.cancel(ignored);
    }

    /// @brief Schedule one callback, retaining state through completion or cancellation.
    /// @param[in] delay Delay before this batch; zero for the initial poll.
    void schedule(std::chrono::milliseconds delay)
    {
      timer.expires_after(delay);
      timer.async_wait(boost::asio::bind_executor(executor,
        [self = shared_from_this(), epoch = generation](const boost::system::error_code &error) {
          if (!self->running || epoch != self->generation) return;
          try {
            if (error) throw boost::system::system_error(error);
            self->poll(epoch);
          } catch (...) {
            self->stop();
            throw;
          }
        }));
    }

    /// @brief Drain a bounded batch and schedule the next eligible poll.
    /// @param[in] epoch Start/stop generation captured when the timer was scheduled.
    void poll(size_t epoch)
    {
      for (size_t count = 0; count < options.maxMessagesPerPoll; ++count) {
        if (!running || epoch != generation) return;
        std::optional<ReceivedMessage> message;
        try {
          message = receive();
        } catch (...) {
          if (!onError) throw;
          const auto action = onError(std::current_exception());
          if (!running || epoch != generation) return;
          if (action == PumpErrorAction::Stop) { stop(); return; }
          continue;
        }
        if (!running || epoch != generation) return;
        if (!message) break;
        // Observer exceptions are not transport errors: stop and propagate them.
        onMessage(std::move(*message));
      }
      if (running && epoch == generation) schedule(options.interval);
    }
  };

  TransportPump::TransportPump(boost::asio::io_context &context, boost::asio::executor executor,
                              std::function<std::optional<ReceivedMessage>()> receive,
                              MessageHandler onMessage, ErrorHandler onError, TransportPumpOptions options)
  {
    if (!onMessage) throw std::invalid_argument("TransportPump requires a message handler");
    if (options.interval <= std::chrono::milliseconds::zero() || options.maxMessagesPerPoll == 0)
      throw std::invalid_argument("TransportPump requires a positive interval and batch limit");
    if (!executor) throw std::invalid_argument("TransportPump requires an executor");
    if (&executor.context() != &context)
      throw std::invalid_argument("TransportPump executor must belong to its timer context");
    state = std::make_shared<State>(context, std::move(executor), std::move(receive),
                                    std::move(onMessage), std::move(onError), options);
  }

  TransportPump::~TransportPump() { stop(); }
  void TransportPump::stop() noexcept { state->stop(); }
  bool TransportPump::isRunning() const noexcept { return state->running; }
  boost::asio::executor TransportPump::getExecutor() const { return state->executor; }

  void TransportPump::start()
  {
    if (state->running) return;
    state->running = true;
    ++state->generation;
    try { state->schedule(std::chrono::milliseconds::zero()); }
    catch (...) { state->stop(); throw; }
  }
}
