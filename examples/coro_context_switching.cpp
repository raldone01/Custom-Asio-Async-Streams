/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * https://stackoverflow.com/a/71991876/4479969
 * Thanks sehe for improving my code substantially.
 */

#include "Helpers.h"

#include <coroutine>

#include <boost/asio.hpp>

/**
 * As you can see the executor can change after calling co_await!!!!
 * When dealing with badly written async function this CAN ba a BIG issue!!!!
 * If you come across a badly behaved function you can just `co_await asio::post(bind_executor(correctExecutor, asio::use_awaitable));` to return to the correct executor.
 *
 * @param app Actually this parameter is redundant as it is the same as `co_await asio::this_coro::executor`.
 */
asio::awaitable<int> mainCo(auto app, auto srv) {
  // bind the completion tokens to the appropriate executors
  auto to_app = bind_executor(app, asio::use_awaitable); // this line is technically unnecessary as per default `asio::use_awaitable` binds to `co_await asio::this_coro::executor`
  auto to_srv = bind_executor(srv, asio::use_awaitable);

  // swap executors a few time
  // NOTE:  In this example the thread_id changes when the executor changes.
  //        However, if both strands were part of the same thread_pool with one thread.
  //        The executor would still change but the thread_id would stay the same.
  tout() << "MC on appCtx"  << std::endl;
  co_await asio::post(to_srv); tout() << "MC on srvCtx" << std::endl;
  co_await asio::post(to_app); tout() << "MC on appCtx" << std::endl;
  co_await asio::post(to_srv); tout() << "MC on srvCtx" << std::endl;
  co_await asio::post(to_srv); tout() << "MC on srvCtx" << std::endl; // the post in this line is a nop - no operation because we are already on the correct executor
  co_await asio::post(to_app); tout() << "MC on appCtx" << std::endl;

  co_return 42;
}

/**
 * What is the difference between executors and execution_contexts?
 */
int main() {
  /**
   * This is an execution_context.
   * This execution_context can mostly be implicitly be converted to an executor by asio.
   */
  asio::io_context appCtx;
  /**
   * This is also an execution_context.
   * This execution_context can mostly be implicitly be converted to an executor by asio.
   */
  asio::thread_pool srvCtx{1};
  // Actually execution_contexts have a default executor.
  // In this case it enforces no execution order.
  // If srvCtx had more threads than its default executor might execute work concurrently without restrictions.

  // To ensure our program behaves consistently, and we have no race conditions,
  // we create strands as soon as possible to avoid unnecessary complexities.
  // Only ever consider adding more threads to an execution_context if performance demands it.

  // Strands essentially wrap an executor and add a queue on top.
  // They enforce that work is executed in the order it is queued.
  // It doesn't matter how many threads the underlying execution_context has
  // - a strand executes its work 'synchronously'. (You can compare it to a javascript promise chain.)
  // However, if the execution_context has the ability to concurrently execute it might still execute multiple DIFFERENT strands concurrently.
  auto appStrand = make_strand(appCtx);
  auto srvStrand = make_strand(srvCtx);

  // Print the thread id of the service thread.
  asio::post(asio::bind_executor(srvStrand, []() {
    tout() << "ServiceThread run start" << std::endl;
  }));

  auto fut = asio::co_spawn(appStrand, // the first argument sets the coroutines default executer. It can be accessed by using `co_await asio::this_coro::executor`.
                 mainCo(appStrand, srvStrand), // Initializes the coroutine to run
                 asio::use_future // - Indicates that the coroutine returns a value and we would like it to be returned in the form of a std::future
                 // asio::detached - Indicates that the coroutine is not expected to return any value
                 );

  tout() << "MainThread run start" << std::endl;
  appCtx.run();
  tout() << "MainThread run done" << std::endl;

  srvCtx.join(); // the service thread stops here
  return fut.get(); // gets the value or exception result from the coroutine
}
