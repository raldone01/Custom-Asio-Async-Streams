/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CUSTOMASIOSTREAMS_HELPERS_H
#define CUSTOMASIOSTREAMS_HELPERS_H

#include <iostream>
#include <syncstream>
#include <thread>
#include <boost/asio.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

#include <fmt/format.h>

inline std::osyncstream tout(const std::string & tag = "") {
  auto hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
  auto hashStr = fmt::format("T{:04X} ", hash >> (sizeof(hash) - 2) * 8); // only display 2 bytes
  auto stream = std::osyncstream(std::cout);

  stream << hashStr;
  if (not tag.empty())
    stream << tag << " ";
  return stream;
}

namespace asio = boost::asio;

constexpr auto use_nothrow_awaitable = asio::experimental::as_tuple(asio::use_awaitable);

/**
 * Helper that checks if a type is an executor.
 * @tparam T The type to check for executor compatability.
 */
template <typename T>
struct my_is_executor
  : std::integral_constant<bool,
    (asio::is_executor<T>::value ||
     asio::execution::is_executor<T>::value)> {};

#endif //CUSTOMASIOSTREAMS_HELPERS_H
