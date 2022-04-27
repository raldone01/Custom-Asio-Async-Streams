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
#include <future>

#include <boost/asio.hpp>
#include <boost/asio/experimental/as_single.hpp>

namespace asio = boost::asio;

/**
 * This global constant can be toggled to simulate an early failure in the simple_async_function.
 * Use it to see how errors are propagated.
 */
const constexpr auto earlyFailureSimulator = false;

/**
 * This class represents some sort of service that allows data to be exchange via async functions.
 */
template <typename Executor>
/**
 * We add this line to indicated that we do not support execution_contexts directly in the constructor.
 * If we were to support contexts we would have to add facilities to unpack execution_contexts.
 * So to use this with an execution_context you just have to call `ctx.get_executor()` before passing it to the constructor.
 */
// TODO: requires std::same_as<asio::is_executor<Executor>, std::true_type> /Boost/libs/asio/include/boost/asio/strand.hpp:445
class AsyncService {
  asio::strand<Executor> strand;
public:
  explicit AsyncService(Executor && exe) : strand{asio::make_strand(exe)} {}

  /**
   * This function typedef indicates the RETURN type of the async_functions.
   * (In this case all functions use the same function typedef because they all return the same values.)
   *
   * Note:  Avoid returning more than two values.
   *        Although it is possible it's rather clunky.
   *
   *        Instead I recommend to only return the error_code/std::exception_ptr and the value you want to return.
   *        The return value MUST be default constructive.
   *        If there is no possible error return value the error_code/ec parameter may be omitted.
   */
  typedef void (async_return_function)(boost::system::error_code ec, double exampleReturnValue1, double exampleReturnValue2);
  //   typedef void (async_return_function)(boost::system::error_code ec, double exampleReturnValue1);  // use this to return one value with    error support
  //   typedef void (async_return_function)(double exampleReturnValue1);                                // use this to return one value without error support
  //   typedef void (async_return_function)(boost::system::error_code ec, struct yourReturnValues);     // use this if you have to return more than one value with error support

  /**
   * This function shows how to implement a async function with completion token using an `asio::awaitable`.
   */
  template<asio::completion_token_for<async_return_function> CompletionToken>
  auto async_coro_function(bool earlyFailure, uint32_t param1, uint32_t param2, CompletionToken && token) {
    return asio::co_spawn(this->strand, [&, earlyFailure, param1, param2] () -> asio::awaitable<std::tuple<double, double>> {
      const constexpr auto TAG = "AsyncCoroFunction";

      tout(TAG) << "Inside";
      if (earlyFailure)
        throw asio::error::bad_descriptor;
      tout(TAG) << "Work";
      co_return { param1 + param2, param2 };
    }, token);
  }

  template<asio::completion_token_for<async_return_function> CompletionToken>
  auto async_simple_function(bool failureSimulator, uint32_t param1, uint32_t param2,
                         CompletionToken &&token) {
    return asio::async_initiate<CompletionToken, async_return_function>(
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
asio::awaitable<int> mainCo(auto & appIO, auto &impl) {
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
  co_return 0;
}

int mainCoroutine() {
  tout() << "========= MAIN COROUTINE START =========" << std::endl;
  asio::io_context appCtx;
  asio::thread_pool srvCtx{1};

  // Print the thread id of the service thread.
  asio::post(srvCtx, std::packaged_task<void()>([]() {
    tout() << "ServiceThread run start" << std::endl;
  })).wait();

  auto service = AsyncService(srvCtx.get_executor());

  auto fut = asio::co_spawn(appCtx, mainCo(appCtx, service), asio::use_future);

  tout() << "MainThread run start" << std::endl;
  appCtx.run();
  tout() << "MainThread run done" << std::endl;

  srvCtx.join(); // the service thread stops here
  tout() << "========= MAIN COROUTINE END   =========" << std::endl;
  return fut.get();
}

int mainCallback() {
  tout() << "========= MAIN CALLBACK START =========" << std::endl;
  asio::io_context appCtx;
  asio::thread_pool srvCtx{1};

  // Print the thread id of the service thread.
  asio::post(srvCtx, std::packaged_task<void()>([]() {
    tout() << "ServiceThread run start" << std::endl;
  })).wait();

  auto service = AsyncService(srvCtx.get_executor());

  asio::post(appCtx, [&] {
    // now we are running in the appIO context
    tout() << "Main run start" << std::endl;
    service.async_simple_function(earlyFailureSimulator, 1, 2, [] (const boost::system::error_code & ec, double exampleReturnValue1, double exampleReturnValue2) {
      // now we should be back running on appIO context again
      tout() << "Main after calling impl"                       << std::endl
             << " EC "                   << ec.message()        << std::endl
             << " ExampleReturnValue1 "  << exampleReturnValue1 << std::endl
             << " ExampleReturnValue2 "  << exampleReturnValue2 << std::endl;
    });
  });

  tout() << "MainThread run start" << std::endl;
  appCtx.run();
  tout() << "MainThread run done" << std::endl;

  srvCtx.join(); // the service thread stops here
  tout() << "========= MAIN CALLBACK END   =========" << std::endl;
  return 0;
}

int mainFuture() {
  tout() << "========= MAIN FUTURE START =========" << std::endl;
  asio::thread_pool srvCtx{1};

  // Print the thread id of the service thread.
  asio::post(srvCtx, std::packaged_task<void()>([]() {
    tout() << "ServiceThread run start" << std::endl;
  })).wait();

  auto service = AsyncService(srvCtx.get_executor());

  tout() << "MainThread run start" << std::endl;
  try {
    auto fut = service.async_simple_function(earlyFailureSimulator, 1, 2, asio::use_future);
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

  srvCtx.join(); // the service thread stops here
  tout() << "========= MAIN FUTURE END   =========" << std::endl;
  return 0;
}

int main() {
  mainCoroutine();
  mainCallback();
  mainFuture();
  return 0;
}
