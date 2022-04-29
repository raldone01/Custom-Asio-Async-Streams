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
#include <boost/asio/experimental/as_tuple.hpp>

namespace asio = boost::asio;

/**
 * This global constant can be toggled to simulate an early failure in the async_functions.
 * Use it to see how errors are propagated.
 */
const constexpr auto earlyFailureSimulator = false;

/**
 * This class represents some sort of service that allows data to be exchanged via async functions.
 */
template <typename Executor>
/**
 * We add this line to indicate that we do not support execution_contexts directly in the constructor.
 * If we were to support contexts we would have to add facilities to unpack executors from execution_contexts.
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
   *        Instead I recommend to only return an error_code and the value you want to return.
   *        The types of the return values MUST be default constructive. (That requirements ironically prevents you from returning boost::outcomes. See the outcome example.)
   *        If there is no possible error return value the error_code/ec parameter may be omitted.
   *        You should only throw when the error is unrecoverable otherwise I advise to use error_codes.
   */
  typedef void (async_return_function)(boost::system::error_code ec, double exampleReturnValue1, double exampleReturnValue2);
  //   typedef void (async_return_function)(boost::system::error_code ec, double exampleReturnValue1);  // use this to return one value with    error support
  //   typedef void (async_return_function)(double exampleReturnValue1);                                // use this to return one value without error support
  //   typedef void (async_return_function)(boost::system::error_code ec, struct yourReturnValues);     // use this if you have to return more than one value with error support

  /**
   * This function shows how to implement a async function with completion token using an `asio::awaitable`.
   *
   * Why don't we just use an awaitable directly?
   * By using co_spawn we make the function look and feel like the stock asio functions as it allows us to take a completion toke parameter.
   *
   * I recommend you to use this approach where possible as it is the easiest.
   * Another advantage of this is how it can propagate exceptions properly.
   */
  template<asio::completion_token_for<async_return_function> CompletionToken>
  auto async_coro_function(bool earlyFailure, uint32_t param1, uint32_t param2, CompletionToken && token) {
    return asio::co_spawn(this->strand, [&, earlyFailure, param1, param2] () -> asio::awaitable<std::tuple<boost::system::error_code, double, double>> {
      const constexpr auto TAG = "async_coro_function";

      tout(TAG) << "Inside" << std::endl;

      if (earlyFailure)
        // throw boost::system::error_code{asio::error::bad_descriptor}; // would propagate correctly
        co_return std::make_tuple( boost::system::error_code{asio::error::bad_descriptor}, 0, 0 );

      tout(TAG) << "Work" << std::endl;
      co_return std::make_tuple( boost::system::error_code{}, param1 + param2, param2 );
    }, token);
  }

  /**
   * This function shows how to implement a async function with completion token using `asio::async_initiate`.
   * This is useful if coroutines aren't available.
   */
  template<asio::completion_token_for<async_return_function> CompletionToken>
  auto async_initiate_function(bool failureSimulator, uint32_t param1, uint32_t param2,
                         CompletionToken &&token) {
    return asio::async_initiate<CompletionToken, async_return_function>(
      // capture failureSimulator by value - See the comments in async_read_some in the AsyncReadStreamDemo.h file for more details.
      [&, failureSimulator, param1, param2](auto completion_handler) mutable {
        const constexpr auto TAG = "async_initiate_function";
        // this will get the executor that asio has already conveniently associated with the completion handler.
        auto assocExe = asio::get_associated_executor(completion_handler);

        tout(TAG) << "Inside" << std::endl;

        if (failureSimulator) { // even if a failure can be detected directly you must not call the completion_handler directly!
          asio::post(assocExe, [completion_handler = std::move(
            completion_handler)]() mutable {
            // throw boost::system::error_code{asio::error::bad_descriptor};  // would propagate out of `appCtx.run();` which is not very helpful.
                                                                              // if necessary you could work around this by using as_tuple
                                                                              // only do this if you have to work with code that throws from non coroutine async implementation
            std::move(completion_handler)(asio::error::bad_descriptor, 0.0, 0.0);
          });
          return;
        }

        asio::post(this->strand, [&TAG, this, workGuard = asio::make_work_guard(assocExe), completion_handler = std::move(
          completion_handler), param1, param2]() mutable {
          tout(TAG) << "Work" << std::endl;
          // now we are on the correct strand to do our operations safely
          double exampleReturnValue1 = param1 + param2;
          double exampleReturnValue2 = param2 * 2;

          // std::move(completion_handler)(std::error_code{}, exampleReturnValue1, exampleReturnValue2); // ILLEGAL!!! Doing this would leak the service executor to the caller.
                                                                                                         // Don't forget to post back to the original calling executor.

          asio::post(workGuard.get_executor(),
                     [exampleReturnValue1, exampleReturnValue2, completion_handler = std::move(
                       completion_handler)]() mutable {
                       std::move(completion_handler)(std::error_code{}, exampleReturnValue1, exampleReturnValue2);
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
template<typename T>
asio::awaitable<int> mainCo(AsyncService<T> &service) {
  const constexpr auto TAG = "MC";
  auto exe = co_await asio::this_coro::executor;
  auto use_awaitable = asio::bind_executor(exe, asio::use_awaitable);

  //async_initiate_function
  {
    tout(TAG) << "before calling" << std::endl;
    auto [ec, ret1, ret2] = co_await service.async_initiate_function(earlyFailureSimulator, 1, 2, asio::experimental::as_tuple(use_awaitable));
    tout(TAG) << "after  calling Ec: " << ec.message() << " Ret1 " << ret1 << " Ret2 " << ret2 << std::endl;
  }
  //async_coro_function
  {
    tout(TAG) << "before calling" << std::endl;
    auto [ec, ret1, ret2] = co_await service.async_coro_function(earlyFailureSimulator, 1, 2, use_awaitable);
    tout(TAG) << "after  calling Ec: " << ec.message() << " Ret1 " << ret1 << " Ret2 " << ret2 << std::endl;
  }

  try {

  } catch (boost::system::system_error const &e) {
    tout() << "MC echo Exception: " << e.what() << std::endl;
    // tout() << "MC echo Exception: " << e.code().message().c_str() << std::endl;
  }
  co_return 0;
}

int main() {
  asio::io_context appCtx;
  asio::thread_pool srvCtx{1};

  // Print the thread id of the service thread.
  asio::post(srvCtx, std::packaged_task<void()>([]() {
    tout() << "ServiceThread run start" << std::endl;
  })).wait();

  auto service = AsyncService(srvCtx.get_executor());

  auto fut = asio::co_spawn(asio::make_strand(appCtx), mainCo(service), asio::use_future);

  tout() << "MainThread run start" << std::endl;
  appCtx.run();
  tout() << "MainThread run done" << std::endl;

  srvCtx.join(); // the service thread stops here
  return fut.get();
}
