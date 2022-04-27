/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CUSTOMASIOSTREAMS_ASYNCFUNCTIONS_H
#define CUSTOMASIOSTREAMS_ASYNCFUNCTIONS_H

#include "Helpers.h"

/**
 * This region contains async functions.
 *
 * async_0_returns_ex_fun   <- Returns void
 *                          <- Throws  boost::system::error_code
 *
 * async_0_returns_ec_fun   <- Returns boost::system::error_code
 *
 * async_1_returns_ex_fun   <- Returns double
 *                          <- Throws  boost::system::error_code
 *
 * async_1_returns_ec_fun   <- Returns [boost::system::error_code, double]
 *
 * async_2_returns_ex_fun   <- Returns [double, double]
 *                          <- Throws  boost::system::error_code
 *
 */
// region async_functions

inline static asio::thread_pool localPool{1};

typedef void (async_0_returns_ex_fun_return_type)();

template<asio::completion_token_for<async_0_returns_ex_fun_return_type> CompletionToken>
auto async_0_returns_ex_fun(bool failure, uint32_t inputParam, CompletionToken && token) {
  return asio::co_spawn(localPool, [&, failure, inputParam] () -> asio::awaitable<void> {
    const constexpr auto TAG = "async_0_returns_ex_fun";

    tout(TAG) << "input " << inputParam << std::endl;
    if (failure)
      throw boost::system::error_code{asio::error::bad_descriptor};
    auto result = inputParam * 2.4;
    tout(TAG) << "computed result " << result << std::endl;
    co_return;
  }, token);
}

typedef void (async_0_returns_ec_fun_return_type)(boost::system::error_code ec);

template<asio::completion_token_for<async_0_returns_ec_fun_return_type> CompletionToken>
auto async_0_returns_ec_fun(bool failure, uint32_t inputParam, CompletionToken && token) {
  return asio::co_spawn(localPool, [&, failure, inputParam] () -> asio::awaitable<boost::system::error_code> {
    const constexpr auto TAG = "async_0_returns_ec_fun";

    tout(TAG) << "input " << inputParam << std::endl;
    if (failure)
      co_return boost::system::error_code{asio::error::bad_descriptor};
    auto result = inputParam * 2.4;
    tout(TAG) << "computed result " << result << std::endl;
    co_return boost::system::error_code{};
  }, token);
}

typedef void (async_1_returns_ex_fun_return_type)(double exampleReturnValue1);

template<asio::completion_token_for<async_1_returns_ex_fun_return_type> CompletionToken>
auto async_1_returns_ex_fun(bool failure, uint32_t inputParam, CompletionToken && token) {
  return asio::co_spawn(localPool, [&, failure, inputParam] () -> asio::awaitable<double> {
    const constexpr auto TAG = "async_1_returns_ex_fun";

    tout(TAG) << "input " << inputParam << std::endl;
    if (failure)
      throw boost::system::error_code{asio::error::bad_descriptor};
    auto result = inputParam * 2.4;
    tout(TAG) << "computed result " << result << std::endl;
    co_return result;
  }, token);
}

typedef void (async_1_returns_ec_fun_return_type)(boost::system::error_code ec, double exampleReturnValue1);

template<asio::completion_token_for<async_1_returns_ec_fun_return_type> CompletionToken>
auto async_1_returns_ec_fun(bool failure, uint32_t inputParam, CompletionToken && token) {
  return asio::co_spawn(localPool, [&, failure, inputParam] () -> asio::awaitable<std::tuple<boost::system::error_code, double>> {
    const constexpr auto TAG = "async_1_returns_ec_fun";

    tout(TAG) << "input " << inputParam << std::endl;
    if (failure)
      co_return std::make_tuple(boost::system::error_code{asio::error::bad_descriptor}, 0.0);
    auto result = inputParam * 2.4;
    tout(TAG) << "computed result " << result << std::endl;
    co_return std::make_tuple(boost::system::error_code{}, result);
  }, token);
}

typedef void (async_2_returns_ex_fun_return_type)(double exampleReturnValue1, double exampleReturnValue2);

template<asio::completion_token_for<async_2_returns_ex_fun_return_type> CompletionToken>
auto async_2_returns_ex_fun(bool failure, uint32_t inputParam, CompletionToken && token) {
  return asio::co_spawn(localPool, [&, failure, inputParam] () -> asio::awaitable<std::tuple<double, double>> {
    const constexpr auto TAG = "async_2_returns_ex_fun";

    tout(TAG) << "input " << inputParam << std::endl;
    if (failure)
      throw boost::system::error_code{asio::error::bad_descriptor};
    auto result1 = inputParam * 2.4;
    auto result2 = inputParam * 3.4;
    tout(TAG) << "computed result 1: " << result1 << " 2: " << result2 << std::endl;
    co_return std::make_tuple(result1, result2);
  }, token);
}

// endregion async_functions

#endif //CUSTOMASIOSTREAMS_ASYNCFUNCTIONS_H
