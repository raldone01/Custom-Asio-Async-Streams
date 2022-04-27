/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "AsyncWriteStreamDemo.h"

#include <coroutine>
#include <thread>

#include <boost/asio/experimental/as_single.hpp>
#include <boost/bind/bind.hpp>

using namespace my;

/**
 * This is the actual main application loop.
 * It uses a new c++20 coroutine.
 */
asio::awaitable<void> mainCo(asio::io_context &appIO, Consumer & prod) {
  try {
    // create strand to use for async operations (might not actually be needed due to the nature of coroutines.)
    // Instead, the appIO may be used directly.
    auto appStrand = asio::io_context::strand{appIO};

    // Create a write stream from our consumer
    auto writeStream = MyAsyncWriteStream(appStrand, prod);
    std::vector<std::byte> dataBackend;

    dataBackend.push_back((std::byte)'H');
    dataBackend.push_back((std::byte)'i');

    auto dynBuffer = asio::dynamic_buffer(dataBackend, 50);
    auto [ec, n] = co_await asio::async_write(writeStream, dynBuffer, asio::experimental::as_single(asio::use_awaitable));
    // WARNING after co_await calls your execution_context might have changed
    // that's why the MyAsyncWriteStream takes an executor as an argument in its constructor to ensure that the
    // execution_context doesn't change accidentally

    tout() << "MC write done: bytes: "
           << n
           << " ec: "
           << ec.message()
           << std::endl;
  } catch (std::exception &e) {
    tout() << "MC echo Exception: " << e.what() << std::endl;
  }
}

int main() {
  asio::io_context prodIO;
  std::thread prodThread;
  {
    // ensure the producer io context doesn't exit
    auto prodWork = asio::make_work_guard(prodIO);

    prodThread = std::thread{[&prodIO] {
      tout() << "ProdThread run start" << std::endl;
      prodIO.run();
      tout() << "ProdThread run done" << std::endl;
    }};
    asio::io_context appIO;

    auto prod = Consumer{prodIO};

    asio::co_spawn(appIO, mainCo(appIO, prod), asio::detached);

    tout() << "MainThread run start" << std::endl;
    appIO.run();
    tout() << "MainThread run done" << std::endl;
  }
  prodThread.join();
  tout() << "MainFunc exit" << std::endl;
  return 0;
}
