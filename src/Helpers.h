#ifndef CUSTOMASIOSTREAMS_HELPERS_H
#define CUSTOMASIOSTREAMS_HELPERS_H

#include <iostream>
#include <syncstream>
#include <thread>

#include <fmt/format.h>

inline std::osyncstream tout() {
  auto hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
  auto hashStr = fmt::format("{:0X}", hash);
  return std::osyncstream(std::cout) << "T" << hashStr.substr(hashStr.size() / 2) << " ";
}

#endif //CUSTOMASIOSTREAMS_HELPERS_H
