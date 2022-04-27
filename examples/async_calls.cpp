/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "AsyncFunctions.h"

#include <coroutine>
#include <future>

#include <boost/asio/experimental/as_single.hpp>

/**
 * This is the actual main application loop.
 * It uses a new c++20 coroutine.
 */
asio::awaitable<int> mainCo() {
  auto exe = co_await asio::this_coro::executor;
  auto use_awaitable = asio::bind_executor(exe, asio::use_awaitable);
  auto as_single = asio::experimental::as_single(use_awaitable);
  auto as_tuple  = asio::experimental::as_tuple(use_awaitable);
  auto use_future = asio::use_future;

  /**
   * use use_awaitable
   *
   * use_awaitable throws if the underlying function throws
   */
  tout() << std::endl;
  tout() << std::endl;
  tout() << "=== use_awaitable" << std::endl;
  {
    // async_0_returns_ex_fun
    {
      auto TAG = "async_0_returns_ex_fun";

      /* auto ret = */ co_await async_0_returns_ex_fun(false, 12, use_awaitable); // no return value defined

      try {
        co_await async_0_returns_ex_fun(true, 12, use_awaitable);
      } catch(boost::system::error_code const &e) {
        tout(TAG) << "Ec: "  << e.what() << std::endl;
      }

      tout() << std::endl;
    }
    // async_0_returns_ec_fun
    {
      auto TAG = "async_0_returns_ec_fun";

      co_await async_0_returns_ec_fun(false, 12, use_awaitable); // no return value defined

      auto ret = co_await async_0_returns_ec_fun(true, 12, use_awaitable);
      tout(TAG) << "Ec: "  << ret.what() << std::endl;

      tout() << std::endl;
    }
    // async_1_returns_ex_fun
    {
      auto TAG = "async_1_returns_ex_fun";

      auto ret = co_await async_1_returns_ex_fun(false, 12, use_awaitable); // returns double
      tout(TAG) << "Ret: " << ret << std::endl;

      try {
        auto ret1 = co_await async_1_returns_ex_fun(true, 12, use_awaitable);
      } catch(boost::system::error_code const &e) {
        tout(TAG) << "Ec: "  << e.what() << std::endl;
      }

      tout() << std::endl;
    }
    // async_1_returns_ec_fun
    {
      auto TAG = "async_1_returns_ec_fun";

      auto ret = co_await async_1_returns_ec_fun(false, 12, use_awaitable); // returns [boost::system::error_code, double]
      tout(TAG) << "Ec: "  << std::get<0>(ret).what() << std::endl;
      tout(TAG) << "Ret: " << std::get<1>(ret)        << std::endl;

      auto ret1 = co_await async_1_returns_ec_fun(true, 12, use_awaitable);
      tout(TAG) << "Ec: "  << std::get<0>(ret1).what()  << std::endl;
      tout(TAG) << "Ret: " << std::get<1>(ret1)         << std::endl;

      tout() << std::endl;
    }
    // async_2_returns_ex_fun
    {
      auto TAG = "async_2_returns_ex_fun";

      auto ret = co_await async_2_returns_ex_fun(false, 12, use_awaitable); // returns [double, double]
      tout(TAG) << "Ret1: " << std::get<0>(ret)  << std::endl;
      tout(TAG) << "Ret2: " << std::get<1>(ret)  << std::endl;

      try {
        auto ret1 = co_await async_2_returns_ex_fun(true, 12, use_awaitable);
      } catch(boost::system::error_code const &e) {
        tout(TAG) << "Ec: " << e.what() << std::endl;
      }

      tout() << std::endl;
    }
    tout() << std::endl;
  }

  /**
   * use as_single
   *
   * as_single returns a tuple<std::exception_ptr, YOUR_RETURN_TYPE> with two members.
   * The first item is a std::exception_ptr.
   * The second item is the return value of the function.
   *
   * It is useful in -fnoexcept contexts.
   */
  tout() << "=== as_single" << std::endl;
  {
    // async_0_returns_ex_fun
    /**
     * TODO: This seems to behave the same as use_awaitable?!?
     */
    {
      auto TAG = "async_0_returns_ex_fun";

      co_await async_0_returns_ex_fun(false, 12, as_single); // no return value defined

      try {
        co_await async_0_returns_ex_fun(true, 12, as_single); // no return value defined
      } catch(boost::system::error_code const &e) {
        tout(TAG) << "Ec: " << e.what() << std::endl;
      }

      tout() << std::endl;
    }
    // async_0_returns_ec_fun
    {
      auto TAG = "async_0_returns_ec_fun";

      auto [ex, ret] = co_await async_0_returns_ec_fun(false, 12, as_single); // returns [std::exception_ptr, boost::system::error_code]
      assert(ex == nullptr); // no exception thrown
      assert(ret == boost::system::error_code{}); // no error returned

      auto [ex1, ret1] = co_await async_0_returns_ec_fun(true, 12, as_single);
      assert(ex1 == nullptr); // no exception thrown
      tout(TAG) << "Ec: " << ret1 << std::endl;

      tout() << std::endl;
    }
    // async_1_returns_ex_fun
    {
      auto TAG = "async_1_returns_ex_fun";

      auto [ex, ret] = co_await async_1_returns_ex_fun(false, 12, as_single); // returns [std::exception_ptr, double]
      assert(ex == nullptr); // no exception thrown
      tout(TAG) << "Ret: " << ret << std::endl;

      auto [ex1, ret1] = co_await async_1_returns_ex_fun(true, 12, as_single);
      try {
        std::rethrow_exception(ex1);
      } catch(boost::system::error_code const &e) {
        tout(TAG) << "Ec: "  << e.what()          << std::endl;
        tout(TAG) << "Ret: " << ret1 << std::endl; // this value has been default constructed for us
      }

      tout() << std::endl;
    }
    // async_1_returns_ec_fun
    {
      auto TAG = "async_1_returns_ec_fun";

      auto [ex, ret] = co_await async_1_returns_ec_fun(false, 12, as_single); // returns [std::exception_ptr, [ec, double]]
      assert(ex == nullptr); // no exception thrown
      tout(TAG) << "Ec: "  << std::get<0>(ret).what() << std::endl;
      tout(TAG) << "Ret: " << std::get<1>(ret)        << std::endl;

      auto [ex1, ret1] = co_await async_1_returns_ec_fun(true, 12, as_single);
      assert(ex1 == nullptr); // no exception thrown
      tout(TAG) << "Ec: "  << std::get<0>(ret).what() << std::endl;
      tout(TAG) << "Ret: " << std::get<1>(ret)        << std::endl;

      tout() << std::endl;
    }
    // async_2_returns_ex_fun
    {
      auto TAG = "async_2_returns_ex_fun";

      auto [ex, ret] = co_await async_2_returns_ex_fun(false, 12, as_single); // returns [std::exception_ptr, [double, double]]
      assert(ex == nullptr); // no exception thrown
      tout(TAG) << "Ret1: " << std::get<0>(ret) << std::endl;
      tout(TAG) << "Ret2: " << std::get<1>(ret) << std::endl;

      auto [ex1, ret1] = co_await async_2_returns_ex_fun(true, 12, as_single);
      try {
        std::rethrow_exception(ex1);
      } catch(boost::system::error_code const &e) {
        tout(TAG) << "Ec: "   << e.what()          << std::endl;
        tout(TAG) << "Ret1: " << std::get<0>(ret1) << std::endl; // this value has been default constructed for us
        tout(TAG) << "Ret2: " << std::get<1>(ret1) << std::endl; // this value has been default constructed for us
      }

      tout() << std::endl;
    }
    tout() << std::endl;
  }

  // TODO: use as_tuple
  {
    // async_0_returns_ex_fun
    {

    }
    // async_0_returns_ec_fun
    {

    }
    // async_1_returns_ex_fun
    {

    }
    // async_1_returns_ec_fun
    {

    }
    // async_2_returns_ec_fun
    {

    }
  }

  // TODO: use as_future
  {
    // async_0_returns_ex_fun
    {

    }
    // async_0_returns_ec_fun
    {

    }
    // async_1_returns_ex_fun
    {

    }
    // async_1_returns_ec_fun
    {

    }
    // async_2_returns_ec_fun
    {

    }
  }

  tout("MainCo") << "Normal exit" << std::endl;
  co_return 0;
}

int main() {
  asio::io_context appCtx;

  // Print the thread id of the service thread.
  asio::post(asio::bind_executor(localPool, []() {
    tout() << "ServiceThread run start" << std::endl;
  }));

  auto fut = asio::co_spawn(asio::make_strand(appCtx), mainCo(), asio::use_future);

  tout() << "MainThread run start" << std::endl;
  appCtx.run();
  tout() << "MainThread run done" << std::endl;

  localPool.join();
  return fut.get();
}
