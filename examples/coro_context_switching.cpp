/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "Helpers.h"

#include <coroutine>
#include <boost/asio.hpp>
#include <boost/asio/experimental/as_single.hpp>
#include <boost/bind/bind.hpp>
#include <boost/thread/thread.hpp>

namespace asio = boost::asio;

/**
 * This is the actual main application loop.
 * It uses a new c++20 coroutine.
 */
asio::awaitable<void> mainCo(asio::io_context &appIO, asio::io_context &prodIO) {
  auto astrand = asio::io_context::strand{appIO};
  auto pstrand = asio::io_context::strand{prodIO};
  tout() << "MC on APPIO" << std::endl;
  co_await asio::post(pstrand, asio::use_awaitable);
  tout() << "MC on PRODIO" << std::endl;
  co_await asio::post(astrand, asio::use_awaitable);
  tout() << "MC on APPIO" << std::endl;
  co_await asio::post(pstrand, asio::use_awaitable);
  tout() << "MC on PRODIO" << std::endl;
  co_await asio::post(pstrand, asio::use_awaitable); // nop - no operation because we are already on the correct execution_context
  tout() << "MC on PRODIO" << std::endl;
  co_await asio::post(astrand, asio::use_awaitable);
  tout() << "MC on APPIO" << std::endl;
  /*
   * As you can see the execution context can change after calling co_await!!!!
   * When dealing with badly written async function this CAN ba a BIG issue!!!!
   * If you come across a badly behaved function you can just `co_await asio::post(yourCorrectExecutor, asio::use_awaitable);` to fix the issue.
   */
}

int main() {
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

    asio::co_spawn(appIO, mainCo(appIO, prodIO), asio::detached);

    tout() << "MainThread run start" << std::endl;
    appIO.run();
    tout() << "MainThread run done" << std::endl;
  }
  prodThread.join();
  return 42;
}
