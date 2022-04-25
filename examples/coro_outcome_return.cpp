/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * See https://stackoverflow.com/questions/71981646/how-to-use-boostoutcomeresult-as-the-return-type-of-completion-token-for-boo/71989368#71989368
 */

#include <iostream>
#include <thread>
#include <optional>

#include <boost/asio.hpp>
#include <boost/outcome.hpp>

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;

auto awaitable_func(bool doThrow) -> asio::awaitable<outcome::result<int>> {
  if (doThrow)
    throw std::runtime_error("Issues");
  co_return outcome::success(666);
}

auto awaitable2_func(bool doThrow) -> asio::awaitable<void> {
  std::cout << "awaitable2 BEGIN doThrow: " << doThrow << std::endl;
  // no issues here calling from another coroutine
  // when an exception is thrown it just propagates it
  // the return value does not have to be default constructed
  try {
    std::cout << "awaitable2 got " << (co_await awaitable_func(doThrow)).value() << std::endl;
  } catch (std::exception &e) {
    std::cout << "What " << e.what() << std::endl;
  }
  std::cout << "awaitable2 END doThrow: " << doThrow << std::endl;
}

/**
 * You can work around the default constructive issue by using a std::optional.
 */
auto awaitable_wrapper_func(bool doThrow) -> asio::awaitable<std::optional<outcome::result<int>>> {
  co_return co_await awaitable_func(doThrow);
}

int main() {
  asio::io_context appCtx;

  // Show that calling awaitable_func from another coroutine is fine
  asio::co_spawn(appCtx, awaitable2_func(false), asio::detached);
  asio::co_spawn(appCtx, awaitable2_func(true), asio::detached);

  // use the workaround like this
  auto fut = asio::co_spawn(appCtx, awaitable_wrapper_func(false), asio::use_future);
  appCtx.run();
  // need to get value twice to unpack the optional first
  std::cout << "From wrapper " << fut.get().value().value() << std::endl;

  std::cout << "Show issues" << std::endl;
  // Now the reason why co_spawn requires default constructive return values on await-ables
  for (bool it : {false, true})
    asio::co_spawn(appCtx, awaitable_wrapper_func(it), [it](
      std::exception_ptr e,
      std::optional<outcome::result<int>> ret // this needs to hold some value even when the awaitable didn't return anything
    ) {
      std::cout << "handler BEGIN doThrow: " << it << std::endl;
      if (e != nullptr) {
        try {
          std::rethrow_exception(e);
        } catch (std::exception &e) {
          std::cout << "What " << e.what() << std::endl;
        }
      }

      // co_spawn had to default construct ret to support this kind of error handling
      if (ret.has_value())
        std::cout << "got val " << ret.value().value() << std::endl;
      std::cout << "handler END doThrow: " << it << std::endl;
    });
  appCtx.restart();
  appCtx.run();

  return 42;
}
