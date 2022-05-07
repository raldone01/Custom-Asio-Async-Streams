/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * This file explores the new <iterator> concepts.
 * https://stackoverflow.com/questions/71135323/how-to-properly-constrain-an-iterator-based-function-using-concepts/71135490#71135490
 */

#include <iterator>
#include <array>
#include <forward_list>
#include <iostream>
#include <vector>
#include <sstream>

template <std::random_access_iterator I, std::output_iterator<std::iter_reference_t<I>> O>
auto random_assign(I first, I last, O out) -> O {
  // you can use distance, but we know it's random access
  size_t const dist = last - first;

  for (size_t i = 0; i != dist; ++i) {
    // output_iterator requires this work
    *out++ = first[rand() % dist];
  }

  // law of useful return
  return out;
}

template<std::ranges::range Range>
void printOut(Range output) {
  std::stringstream buf {};
  buf << "Out ";
  for (auto item : output)
    buf << " " << item;
  std::cout << buf.str() << std::endl;
}

int main() {
  {
    std::array<uint32_t, 9> input = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::forward_list<uint32_t> output;
    random_assign(input.begin(), input.end(), std::front_insert_iterator(output));
    printOut(output);
    random_assign(input.begin(), input.end(), output.begin());
    printOut(output);
  }
  return 0;
}
