/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * TODO: add default_completion_token
 */

#include "Helpers.h"

#include <coroutine>
#include <boost/asio.hpp>
#include <boost/asio/experimental/as_single.hpp>
#include <boost/bind/bind.hpp>
#include <boost/thread/thread.hpp>

namespace asio = boost::asio;

/**
 * This global constant can be toggled to simulate an early failure in the simple_async_function.
 * Use it to see how errors are propagated.
 */
const constexpr auto earlyFailureSimulator = false;

class Impl {
public:
  /**
   * This function indicates the RETURN type of the simple_async_function.
   * As this example shows returning more than two values is possible but rather clunky.
   * Instead I recommend to return a struct as the second return value when more values should be returned.
   * The error_code parameter may be omitted if it's not needed.
   */
  typedef void (async_callbackFunction)(boost::system::error_code ec, double exampleReturnValue1, double exampleReturnValue2);

  asio::io_context::strand strand;

  explicit Impl(asio::io_context &io) : strand{io} {}

  template<asio::completion_token_for<async_callbackFunction> CompletionToken>
  auto async_simple_function(bool failureSimulator, uint32_t param1, uint32_t param2,
                         CompletionToken &&token) {
    return asio::async_initiate<CompletionToken, async_callbackFunction>(
      // capture failureSimulator by value - See the comments in async_read_some in the AsyncReadStreamDemo.h file for more details.
      [&, failureSimulator, param1, param2](auto completion_handler) mutable {
        // this will get the executor that asio has already conveniently associated with the completion handler.
        auto assocExe = asio::get_associated_executor(completion_handler);

        if (failureSimulator) { // even if a failure can be detected directly you must not call the completion_handler directly!
          asio::post(assocExe, [completion_handler = std::move(
            completion_handler)]() mutable {
            completion_handler(asio::error::bad_descriptor, 0.0, 0.0);
          });
          return;
        }
        auto resultWorkGuard = asio::make_work_guard(assocExe);
        asio::post(this->strand, [this, workGuard = std::move(resultWorkGuard), completion_handler = std::move(
          completion_handler), param1, param2]() mutable {
          // now we are on the correct strand to do our operations safely
          double exampleReturnValue1 = param1 + param2;
          double exampleReturnValue2 = param2 * 2;
          tout() << "Inside impl" << std::endl;

          asio::post(workGuard.get_executor(),
                     [exampleReturnValue1, exampleReturnValue2, completion_handler = std::move(
                       completion_handler)]() mutable {
                       completion_handler(std::error_code{}, exampleReturnValue1, exampleReturnValue2);
                     });
        });
      },
      token);
  }
};


/**
 * This is the actual main application loop.
 * It uses a new c++20 coroutine.
 */
asio::awaitable<void> mainCo(asio::io_context &appIO, Impl &impl) {
  try {
    // create strand to use for async operations (might not actually be needed due to the nature of coroutines.)
    // Instead, the appIO may be used directly.
    auto appStrand = asio::io_context::strand{appIO};

    tout() << "MC before calling impl" << std::endl;

    // here you can choose between throwing and non throwing
    const auto throwing = false;
    if (!throwing) {
      auto [ec, exampleReturnValue1, exampleReturnValue2] = co_await impl.async_simple_function(earlyFailureSimulator, 1, 2, asio::experimental::as_single(asio::use_awaitable));

      tout() << "MC after calling impl"                         << std::endl
             << " EC "                   << ec.message()        << std::endl
             << " ExampleReturnValue1 "  << exampleReturnValue1 << std::endl
             << " ExampleReturnValue2 "  << exampleReturnValue2 << std::endl;
    } else {
      auto result = co_await impl.async_simple_function(earlyFailureSimulator, 1, 2, asio::use_awaitable);

      tout() << "MC after calling impl"                         << std::endl
             << " ExampleReturnValue1 "  << std::get<0>(result) << std::endl
             << " ExampleReturnValue2 "  << std::get<1>(result) << std::endl;
    }
  } catch (boost::system::system_error const &e) {
    tout() << "MC echo Exception: " << e.what() << std::endl;
    // tout() << "MC echo Exception: " << e.code().message().c_str() << std::endl;
  }
}

int mainCoroutine() {
  tout() << "========= MAIN COROUTINE START =========" << std::endl;
  asio::io_context prodIO;
  boost::thread prodThread;
  {
    // ensure the producer io context doesn't exit
    auto prodWork = asio::make_work_guard(prodIO);

    prodThread = boost::thread{[&prodIO] {
      tout() << "ProdThread run start" << std::endl;
      prodIO.run();
      tout() << "ProdThread run done" << std::endl;
    }};
    asio::io_context appIO;

    auto impl = Impl(prodIO);

    asio::co_spawn(appIO, mainCo(appIO, impl), asio::detached);

    tout() << "MainThread run start" << std::endl;
    appIO.run();
    tout() << "MainThread run done" << std::endl;
  }
  prodThread.join();
  tout() << "========= MAIN COROUTINE END   =========" << std::endl;
  return 42;
}

int mainCallback() {
  tout() << "========= MAIN CALLBACK START =========" << std::endl;
  asio::io_context prodIO;
  boost::thread prodThread;
  {
    // ensure the producer io context doesn't exit
    auto prodWork = asio::make_work_guard(prodIO);

    prodThread = boost::thread{[&prodIO] {
      tout() << "ProdThread run start" << std::endl;
      prodIO.run();
      tout() << "ProdThread run done" << std::endl;
    }};
    asio::io_context appIO;

    auto impl = Impl(prodIO);

    asio::post(appIO, [&] {
      // now we are running in the appIO context
      tout() << "Main run start" << std::endl;
      impl.async_simple_function(earlyFailureSimulator, 1, 2, [] (const boost::system::error_code & ec, double exampleReturnValue1, double exampleReturnValue2) {
        // now we should be back running on appIO context again
        tout() << "Main after calling impl"                       << std::endl
               << " EC "                   << ec.message()        << std::endl
               << " ExampleReturnValue1 "  << exampleReturnValue1 << std::endl
               << " ExampleReturnValue2 "  << exampleReturnValue2 << std::endl;
      });
    });

    tout() << "MainThread run start" << std::endl;
    appIO.run();
    tout() << "MainThread run done" << std::endl;
  }
  prodThread.join();
  tout() << "========= MAIN CALLBACK END   =========" << std::endl;
  return 42;
}

int mainFuture() {
  tout() << "========= MAIN FUTURE START =========" << std::endl;
  asio::io_context prodIO;
  boost::thread prodThread;
  {
    // ensure the producer io context doesn't exit
    auto prodWork = asio::make_work_guard(prodIO);

    prodThread = boost::thread{[&prodIO] {
      tout() << "ProdThread run start" << std::endl;
      prodIO.run();
      tout() << "ProdThread run done" << std::endl;
    }};

    auto impl = Impl(prodIO);

    tout() << "MainThread run start" << std::endl;
    try {
      auto fut = impl.async_simple_function(earlyFailureSimulator, 1, 2, asio::use_future);
      fut.wait();
      auto ret = fut.get();
      tout() << "MainThread after calling impl"                         << std::endl
             << " ExampleReturnValue1 "  << std::get<0>(ret) << std::endl
             << " ExampleReturnValue2 "  << std::get<1>(ret) << std::endl;
    } catch (boost::system::system_error const &e) {
      tout() << "MainThread echo Exception: " << e.what() << std::endl;
      // tout() << "MainThread echo Exception: " << e.code().message().c_str() << std::endl;
    }
    tout() << "MainThread run done" << std::endl;
  }
  prodThread.join();
  tout() << "========= MAIN FUTURE END   =========" << std::endl;
  return 42;
}

int main() {
  mainCoroutine();
  mainCallback();
  mainFuture();
  return 42;
}
