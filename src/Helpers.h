#ifndef CUSTOMASIOSTREAMS_HELPERS_H
#define CUSTOMASIOSTREAMS_HELPERS_H

#include <iostream>
#include <syncstream>
#include <thread>
#include <boost/asio.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

#include <fmt/format.h>

inline std::osyncstream & tout() {
  // use thread_local to avoid constructing these objects on every function call
  thread_local auto hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
  thread_local auto hashStr = fmt::format("T{:04X} ", hash >> (sizeof(hash) - 2) * 8); // only display 2 bytes
  thread_local auto stream = std::osyncstream(std::cout);

  stream << hashStr;
  return stream;
}

namespace asio = boost::asio;

constexpr auto use_nothrow_awaitable = asio::experimental::as_tuple(asio::use_awaitable);

#endif //CUSTOMASIOSTREAMS_HELPERS_H
