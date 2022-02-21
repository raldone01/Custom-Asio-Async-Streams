#include "src/AsyncReadStreamDemo.h"

#include <coroutine>
#include <boost/asio/experimental/as_single.hpp>
#include <boost/bind/bind.hpp>
#include <boost/thread/thread.hpp>

using namespace my;

/**
 * This is the acutal main application loop.
 * It uses a new c++20 coroutine.
 */
boost::asio::awaitable<void> mainCo(asio::io_context &appIO, Producer & prod) {
  try {
    // create strand to use for async operations (might not actually be needed due to the nature of coroutines.)
    // Instead, the appIO may be used directly.
    auto appStrand = asio::io_context::strand{appIO};

    // Create a read stream from our
    auto readStream = MyAsyncReadStream(appStrand, prod, 0, 99);
    std::vector<std::byte> dataBackend;
    std::osyncstream(std::cout) << "BRD: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
    auto dynBuffer = asio::dynamic_buffer(dataBackend, 50);
    auto [ec, n] = co_await asio::async_read(readStream, dynBuffer, asio::experimental::as_single(asio::use_awaitable)); // WARNING after co_await calls your execution_context might have changed
    std::osyncstream(std::cout) << "RD: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << " "
                                << ec.message() << " " << n << std::endl;
  } catch (std::exception &e) {
    std::printf("echo Exception: %s\n", e.what());
  }
}

int main() {
  asio::io_context prodIO;
  boost::thread prodThread;
  {
    // ensure the producer io context doesn't exit
    auto prodWork = asio::make_work_guard(prodIO);

    prodThread = boost::thread{[&prodIO] {
      std::osyncstream(std::cout) << "PROD RUN START: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                  << std::endl;
      prodIO.run();
      std::osyncstream(std::cout) << "PROD RUN ENDED: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                  << std::endl;
    }};
    asio::io_context appIO;

    auto prod = Producer{prodIO};

    asio::co_spawn(appIO, mainCo(appIO, prod), asio::detached);

    std::osyncstream(std::cout) << "Main START: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                << std::endl;

    appIO.run();
    std::osyncstream(std::cout) << "Main DONE: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                << std::endl;
  }
  prodThread.join();
  std::osyncstream(std::cout) << "Main EXIT: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
  return 42;
}
