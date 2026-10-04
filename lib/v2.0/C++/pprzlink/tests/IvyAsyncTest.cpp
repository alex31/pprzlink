#include <pprzlink/IvyAsync.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
#include <unistd.h>

using namespace std::chrono_literals;
using pprzlink::IvyLink;
using pprzlink::IvyAsyncSubscription;
using pprzlink::Message;
using pprzlink::MessageDictionary;

static_assert(std::is_nothrow_move_constructible_v<IvyAsyncSubscription>);
static_assert(std::is_nothrow_move_assignable_v<IvyAsyncSubscription>);
static_assert(!std::is_copy_constructible_v<IvyAsyncSubscription>);

namespace {
  void require(bool condition, const std::string &description)
  {
    if (!condition) throw std::runtime_error(description);
  }

  template<class Predicate>
  void waitFor(Predicate predicate, const std::string &description)
  {
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (!predicate()) {
      require(std::chrono::steady_clock::now() < deadline, "Timeout: " + description);
      std::this_thread::sleep_for(2ms);
    }
  }

  void waitBindings(IvyLink &sender, const std::string &peerName, size_t count)
  {
    waitFor([&] {
      auto peer = sender.getBus().find_application(peerName);
      if (!peer || !*peer) return false;
      auto regexps = sender.getBus().application_regexps(**peer);
      return regexps && regexps->size() == count;
    }, "bindings of " + peerName);
  }

  void sendRaw(IvyLink &link, const std::string &message)
  {
    require(link.getBus().send(message).has_value(), "Native Ivy send");
  }

  // Messages on one peer TCP connection are dispatched in order. A distinct
  // native marker therefore proves preceding asynchronous messages were posted,
  // without polling or inspecting any adapter implementation detail.
  struct Fence {
    std::atomic<int> seen{0};
    ivy::Subscription token;

    explicit Fence(IvyLink &receiver)
      : token(receiver.subscribeMessage("FENCE", [&](std::string, Message) { ++seen; })) {}

    void wait(IvyLink &sender)
    {
      const int expected = seen + 1;
      sendRaw(sender, "42 FENCE");
      waitFor([&] { return seen == expected; }, "native receive fence");
    }
  };

  void testExecutorDelivery(const MessageDictionary &dict, const std::string &domain)
  {
    boost::asio::io_context context;
    auto strand = boost::asio::make_strand(context);
    std::vector<uint32_t> values;
    const auto expectedThread = std::this_thread::get_id();
    IvyLink receiver(dict, "async-rx", domain, true);
    Fence fence(receiver);
    auto subscription = pprzlink::subscribeMessageOn(strand, receiver, "VALUE",
      [&](std::string sender, Message message) {
        require(std::this_thread::get_id() == expectedThread, "Delivery uses the target event loop");
        require(strand.running_in_this_thread(), "Delivery uses the selected strand");
        require(sender == "plane.1", "Owned sender reaches the target executor");
        require(std::get<std::string>(message.getSenderId()) == sender, "Owned message metadata");
        values.push_back(message.getField<uint32_t>("value"));
      });
    IvyLink sender(dict, "async-tx", domain, true);
    waitBindings(sender, "async-rx", 2);

    context.run_for(10ms);
    require(!context.stopped(), "Active subscription keeps io_context alive while idle");
    for (int i = 1; i <= 3; ++i) sendRaw(sender, "plane.1 VALUE " + std::to_string(i));
    fence.wait(sender);
    require(values.empty(), "Native Ivy callbacks do not invoke the observer");
    context.poll();
    require(values == std::vector<uint32_t>({1, 2, 3}), "Repeated messages preserve order on a strand");
    subscription.reset();
    context.run();
    require(context.stopped(), "Reset releases work so io_context can finish");
    waitBindings(sender, "async-rx", 1);
    require(receiver.getBus().take_callback_error().has_value(), "Native Ivy loop has no callback error");
  }

  void testCancellationAndMoves(const MessageDictionary &dict, const std::string &domain)
  {
    boost::asio::io_context context;
    int canceled = 0, delivered = 0;
    IvyLink receiver(dict, "cancel-rx", domain, true);
    Fence fence(receiver);
    IvyLink sender(dict, "cancel-tx", domain, true);

    IvyAsyncSubscription empty;
    require(!empty, "Default token is empty");
    empty.reset();
    auto capture = std::make_shared<int>(0);
    std::weak_ptr<int> released = capture;
    auto canceledSubscription = pprzlink::subscribeMessageOn(context.get_executor(), receiver,
      dict.getDefinition("VALUE"), [&, capture](std::string, Message) { ++canceled; });
    capture.reset();
    auto liveSubscription = pprzlink::subscribeMessageOn(context.get_executor(), receiver, "VALUE",
      [&](std::string, Message) { ++delivered; });
    waitBindings(sender, "cancel-rx", 3);
    sendRaw(sender, "42 VALUE 1");
    fence.wait(sender);

    canceledSubscription = std::move(liveSubscription);
    require(!liveSubscription && canceledSubscription, "Move assignment transfers ownership");
    require(released.expired(), "Cancel releases captures even while suppressed deliveries remain queued");
    auto moved = std::move(canceledSubscription);
    require(!canceledSubscription && moved, "Move construction empties the source token");
    context.poll();
    require(canceled == 0 && delivered == 1, "Move assignment suppresses old deliveries and retains new ones");

    sendRaw(sender, "42 VALUE 2");
    fence.wait(sender);
    moved.reset();
    moved.reset();
    context.run();
    require(delivered == 1, "Explicit reset suppresses a message already posted");
    require(context.stopped(), "Idempotent reset releases outstanding work");

    context.restart();
    {
      auto scoped = pprzlink::subscribeMessageOn(context.get_executor(), receiver, "VALUE",
        [&](std::string, Message) { ++canceled; });
      waitBindings(sender, "cancel-rx", 2);
      sendRaw(sender, "42 VALUE 3");
      fence.wait(sender);
    }
    context.run();
    require(canceled == 0, "Destruction suppresses queued deliveries");
    waitBindings(sender, "cancel-rx", 1);
  }

  void testSelfCancellationAndSender(const MessageDictionary &dict, const std::string &domain)
  {
    boost::asio::io_context context;
    int delivered = 0;
    IvyLink receiver(dict, "sender-rx", domain, true);
    Fence fence(receiver);
    IvyLink sender(dict, "sender-tx", domain, true);
    auto capture = std::make_shared<int>(19);
    std::weak_ptr<int> released = capture;
    auto subscription = std::make_unique<IvyAsyncSubscription>();
    *subscription = pprzlink::subscribeSenderOn(context.get_executor(), receiver, "plane.1",
      [&, capture](std::string source, Message message) {
        require(source == "plane.1", "Sender selector is literal");
        require(message.getField<uint32_t>("value") == 7, "Sender selector delivers decoded fields");
        subscription.reset();
        require(*capture == 19, "Callback captures survive destruction of their subscription");
        ++delivered;
      });
    capture.reset();
    waitBindings(sender, "sender-rx", 2);
    sendRaw(sender, "planeX1 VALUE 7");
    sendRaw(sender, "plane.1 VALUE 7");
    sendRaw(sender, "plane.1 VALUE 8");
    fence.wait(sender);
    context.run();
    require(delivered == 1 && released.expired(), "Self-destruction suppresses later queued callbacks");
    waitBindings(sender, "sender-rx", 1);
  }

  void testExceptions(const MessageDictionary &dict, const std::string &domain)
  {
    boost::asio::io_context context;
    IvyLink receiver(dict, "exception-rx", domain, true);
    Fence fence(receiver);
    IvyLink sender(dict, "exception-tx", domain, true);
    int delivered = 0;
    auto subscription = pprzlink::subscribeMessageOn(context.get_executor(), receiver, "VALUE",
      [&](std::string, Message) {
        ++delivered;
        if (delivered == 1) throw std::runtime_error("observer failure");
      });
    waitBindings(sender, "exception-rx", 2);
    sendRaw(sender, "42 VALUE 1");
    sendRaw(sender, "42 VALUE 2");
    fence.wait(sender);
    bool propagated = false;
    try {
      context.poll();
    } catch (const std::runtime_error &error) {
      propagated = std::string(error.what()) == "observer failure";
    }
    require(propagated, "Application callback exceptions propagate through Asio");
    context.poll();
    require(delivered == 2, "Delivery can continue after the caller handles an observer exception");
    require(receiver.getBus().take_callback_error().has_value(), "Observer exception does not fail Ivy");
    subscription.reset();
    context.run();

    context.restart();
    bool rejected = false;
    try {
      auto unused = pprzlink::subscribeMessageOn(context.get_executor(), receiver, "VALUE", {});
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "Empty callback rejected at registration");
    bool lookupFailed = false;
    try {
      auto unused = pprzlink::subscribeMessageOn(context.get_executor(), receiver, "UNKNOWN",
        [](std::string, Message) {});
    } catch (const std::exception &) {
      lookupFailed = true;
    }
    require(lookupFailed, "Dictionary binding failure reaches the caller");
    context.run();
    require(context.stopped(), "Failed subscription creation releases executor work");
  }

  void testConcurrentReset(const MessageDictionary &dict, const std::string &domain)
  {
    boost::asio::io_context context;
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto releaseFuture = release.get_future().share();
    int delivered = 0;
    auto capture = std::make_shared<int>(23);
    std::weak_ptr<int> released = capture;
    IvyLink receiver(dict, "concurrent-rx", domain, true);
    Fence fence(receiver);
    IvyLink sender(dict, "concurrent-tx", domain, true);
    auto subscription = pprzlink::subscribeMessageOn(context.get_executor(), receiver, "VALUE",
      [&, capture](std::string, Message) {
        entered.set_value();
        releaseFuture.wait();
        require(*capture == 23, "Running callback keeps captures alive across concurrent reset");
        ++delivered;
      });
    capture.reset();
    waitBindings(sender, "concurrent-rx", 2);
    sendRaw(sender, "42 VALUE 1");
    sendRaw(sender, "42 VALUE 2");
    fence.wait(sender);

    auto worker = std::async(std::launch::async, [&] { context.run(); });
    const bool started = enteredFuture.wait_for(4s) == std::future_status::ready;
    subscription.reset(); // Must not wait for the callback blocked on release.
    const bool keptAlive = !released.expired();
    release.set_value();
    worker.get();
    require(started && keptAlive, "Reset can run concurrently with an admitted callback");
    require(delivered == 1 && released.expired(), "Concurrent reset suppresses pending delivery and cleans up");
  }

  void testLinkLifetime(const MessageDictionary &dict, const std::string &domain)
  {
    boost::asio::io_context context;
    int delivered = 0;
    IvyAsyncSubscription subscription;
    {
      IvyLink receiver(dict, "lifetime-rx", domain, true);
      Fence fence(receiver);
      IvyLink sender(dict, "lifetime-tx", domain, true);
      subscription = pprzlink::subscribeMessageOn(context.get_executor(), receiver, "VALUE",
        [&](std::string, Message) { ++delivered; });
      waitBindings(sender, "lifetime-rx", 2);
      sendRaw(sender, "42 VALUE 1");
      fence.wait(sender);
    }
    require(!subscription, "Token becomes inactive after link destruction");
    context.run_for(100ms);
    require(context.stopped(), "Link destruction releases subscription executor work");
    require(delivered == 0, "Link destruction cancels queued observer calls");
    subscription.reset();
  }
}

int main()
{
  try {
    tinyxml2::XMLDocument xml;
    require(xml.Parse(R"(<protocol><msg_class name="test" id="1">
      <message name="VALUE" id="1"><field name="value" type="uint32"/></message>
      <message name="FENCE" id="2"/>
    </msg_class></protocol>)") == tinyxml2::XML_SUCCESS, "Test dictionary XML");
    const MessageDictionary dict(xml.RootElement());
    const auto domain = "127.255.255.255:" + std::to_string(25000 + (getpid() % 10000) * 2);
    testExecutorDelivery(dict, domain);
    testCancellationAndMoves(dict, domain);
    testSelfCancellationAndSender(dict, domain);
    testExceptions(dict, domain);
    testConcurrentReset(dict, domain);
    testLinkLifetime(dict, domain);
    std::cout << "Ivy executor delivery, cancellation, lifetime and errors passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
