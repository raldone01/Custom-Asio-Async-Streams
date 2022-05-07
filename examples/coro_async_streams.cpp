/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * This example shows how to implement a modern IO service and a modern IO object.
 * The IO object is an async read write stream.
 * https://stackoverflow.com/questions/72072896/asio-how-to-write-a-custom-asyncstream/72073610?noredirect=1#comment127355216_72073610
 */

#include "Helpers.h"

#include <coroutine>
#include <future>
#include <random>
#include <string>
#include <memory>

namespace asio = boost::asio;

template <typename Executor>
requires my_is_executor<Executor>::value
class ModernIOService : public std::enable_shared_from_this<ModernIOService<Executor>> {
  template<typename CallerExecutor, typename ModernIOService>
  requires my_is_executor<CallerExecutor>::value
  friend class MyAsyncStream;
  /// Data sent to the service
  std::string buffer_in;
  /// Data produced by the service
  std::string buffer_out;
  /// The strand used to avoid concurrent execution if the passed executor is backed by multiple threads.
  asio::strand<Executor> strand;
  /// Used to slow the data consumption and generation
  asio::steady_timer timer;

  /// Used to generate data
  std::mt19937 gen;
  /// https://stackoverflow.com/a/69753502/4479969
  constexpr static const char charset[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";
  template<typename URBG>
  static std::string gen_string(std::size_t length, URBG &&g) {
    std::string result;
    result.resize(length);
    std::sample(std::cbegin(charset),
                std::cend(charset),
                std::begin(result),
                std::intptr_t(length),
                std::forward<URBG>(g));
    return result;
  }

  static const constexpr auto MAX_OPS = 7;

  /**
   * Main loop of the IO service.
   * The shared_ptr parameter ensures that the ModernIOService object stays alive while the main loop is running.
   */
  asio::awaitable<void> main(std::shared_ptr<ModernIOService> /* captured_self */) {
    const constexpr auto TAG = "SrvCo";
    auto exe = co_await asio::this_coro::executor;
    auto use_awaitable = asio::bind_executor(exe, asio::use_awaitable);

    for (size_t ops = 0; ops < MAX_OPS; ops++) {
      timer.expires_after(std::chrono::milliseconds(1000));
      co_await timer.async_wait(use_awaitable);

      tout(TAG) << "Ops " << ops << std::endl;

      buffer_out += gen_string(8, gen);
      tout(TAG) << "Produced: " << buffer_out << std::endl;

      auto consumed = std::string_view(buffer_in).substr(0, 4);
      tout(TAG) << "Consumed: " << consumed << std::endl;
      buffer_in.erase(0, consumed.size());
    }
    tout(TAG) << "Done" << std::endl;
  }
  std::once_flag init_once;

public:

  explicit ModernIOService(Executor && exe) : strand{asio::make_strand(exe)}, timer{exe.context()} {}

  void init() {
    std::call_once(init_once, [this]() {
      asio::co_spawn(strand, main(this->shared_from_this()), asio::detached);
    });
  }
};

/**
 * In case you just want an AsyncReadStream or an AsyncWriteStream just omit either async_read_some or async_write_some.
 * https://www.boost.org/doc/libs/1_66_0/doc/html/boost_asio/reference/AsyncReadStream.html
 */
template<typename CallerExecutor, typename ModernIOService>
requires my_is_executor<CallerExecutor>::value
class MyAsyncStream {
  typedef void async_rw_handler(boost::system::error_code, size_t);
  /// Holds the io objects bound executor.
  CallerExecutor executor;
  /// Use a weak_ptr to behave like a file descriptor.
  std::weak_ptr<ModernIOService> service_ptr;
public:
  explicit MyAsyncStream(std::shared_ptr<ModernIOService> & service, CallerExecutor & exe) : executor{exe}, service_ptr{service} {}

  /// Needed by the stream specification.
  typedef CallerExecutor executor_type;

  /// @return Returns the executor supplied in the constructor.
  auto get_executor() {
    return executor;
  }

  template<typename MutableBufferSequence,
    asio::completion_token_for<async_rw_handler>
    CompletionToken = typename asio::default_completion_token<CallerExecutor>::type>
  requires asio::is_mutable_buffer_sequence<MutableBufferSequence>::value
  auto async_read_some(const MutableBufferSequence &buffer,
                       CompletionToken &&token = typename asio::default_completion_token<CallerExecutor>::type()) {
    return asio::async_initiate<CompletionToken, async_rw_handler>([&](auto completion_handler) {
      BOOST_ASIO_READ_HANDLER_CHECK(CompletionToken, completion_handler) type_check;
      /*
       * Using co_spawn inside this may be too expensive for some cases.
       * If this is the case 'just' don't use it.
       * Consider using callback based or stackless coroutine based code instead.
       * TODO: It might be possible to somehow 'cache' the stack inside the MyAsyncStream class.
       */
      asio::co_spawn(
          asio::get_associated_executor(completion_handler, this->get_executor()), // Use the executor of the completion_handler for the coroutine but fall back to our bound io executor.
          [this,
           buffer, // Pass the buffer by value. Cheap because it only points to memory owned by the caller.
           completion_handler = std::forward<CompletionToken>(completion_handler) // always forward the completion_handler
          ]
          () mutable -> asio::awaitable<void> {
        const constexpr auto TAG = "ARS";
        auto to_caller = asio::bind_executor(co_await asio::this_coro::executor, asio::use_awaitable);

        auto service = service_ptr.lock();
        if (service == nullptr) {
          std::move(completion_handler)(asio::error::bad_descriptor, 0); // move the completion_handler into the 'call'
          co_return;
        }
        auto to_service = asio::bind_executor(service->strand, asio::use_awaitable);

        co_await asio::post(to_service);

        tout(TAG) << "performing read" << std::endl;

        auto buf_begin = asio::buffers_begin(buffer);
        auto buf_end = asio::buffers_end(buffer);
        boost::system::error_code err = asio::error::fault;
        size_t it = 0;
        while (!service->buffer_out.empty()) {
          if (buf_begin == buf_end) {
            // error the buffer is smaller than the request read amount
            err = asio::error::no_buffer_space;
            goto completion;
          }

          *buf_begin++ = service->buffer_out.at(0);
          service->buffer_out.erase(0, 1);
          it++;
        }
        err = asio::stream_errc::eof;
        completion:
        co_await asio::post(to_caller); // without this call the function returns on the wrong thread
        tout(TAG) << "read done returned" << std::endl;
        std::move(completion_handler)(err, it);
      }, asio::detached);
    }, token);
  }

  template<typename ConstBufferSequence,
    asio::completion_token_for <async_rw_handler>
    CompletionToken = typename asio::default_completion_token<CallerExecutor>::type>
  requires asio::is_const_buffer_sequence<ConstBufferSequence>::value
  auto async_write_some(const ConstBufferSequence &buffer,
                        CompletionToken &&token = typename asio::default_completion_token<CallerExecutor>::type()) {
    return asio::async_initiate<CompletionToken, async_rw_handler>([&](auto completion_handler) {
      BOOST_ASIO_WRITE_HANDLER_CHECK(CompletionToken, completion_handler) type_check;
      asio::co_spawn(
          asio::get_associated_executor(completion_handler, this->get_executor()),
          [this,
           buffer,
           completion_handler = std::forward<CompletionToken>(completion_handler)
          ]
          () mutable -> asio::awaitable<void> {
        const constexpr auto TAG = "AWS";
        auto to_caller = asio::bind_executor(co_await asio::this_coro::executor, asio::use_awaitable);

        auto service = service_ptr.lock();
        if (service == nullptr) {
          std::move(completion_handler)(asio::error::bad_descriptor, 0);
          co_return;
        }
        auto to_service = asio::bind_executor(service->strand, asio::use_awaitable);

        co_await asio::post(to_service);

        tout(TAG) << "performing write" << std::endl;

        auto buf_begin = asio::buffers_begin(buffer);
        auto buf_end = asio::buffers_end(buffer);
        boost::system::error_code err = asio::error::fault;
        size_t it = 0;
        while (buf_begin != buf_end) {
          service->buffer_in.push_back(static_cast<char>(*buf_begin++));
          it++;
        }
        err = asio::stream_errc::eof;
        completion:
        co_await asio::post(to_caller); // without this call the function returns on the wrong thread
        tout(TAG) << "write done returned" << std::endl;
        std::move(completion_handler)(err, it);
      }, asio::detached);
    }, token);
  }
};

asio::awaitable<int> mainCo() {
  const constexpr auto TAG = "MainCo";
  auto exe = co_await asio::this_coro::executor;
  auto use_awaitable = asio::bind_executor(exe, asio::use_awaitable);
  auto as_tuple  = asio::experimental::as_tuple(use_awaitable);
  auto use_future = asio::use_future;
  auto timer = asio::steady_timer(exe);

  asio::thread_pool service_pool{1};

  co_await asio::post(asio::bind_executor(service_pool, asio::use_awaitable));
  tout() << "ModernIOServiceThread run start" << std::endl;
  co_await asio::post(use_awaitable);

  auto service = std::make_shared<ModernIOService<boost::asio::thread_pool::basic_executor_type<std::allocator<void>, 0> >>(service_pool.get_executor());
  service->init();
  auto stream = MyAsyncStream{service, exe};

  for (size_t it = 0; it < 4; it++) {
    try {
      std::vector<char> data_owner;
      auto dyn_buf = asio::dynamic_buffer(data_owner, 50);
      auto [ec, n] = co_await asio::async_read(stream, dyn_buf, as_tuple); // Using as_tuple here avoids raising exceptions. Which is always good.

      tout(TAG) << "read done: " << std::endl
                << "n:   " << n  << std::endl
                << "msg: " << std::string{data_owner.begin(), data_owner.end()} << std::endl
                << "ec:  " << ec.message()
                << std::endl;
    } catch(boost::system::error_code &e) {
      tout(TAG) << "W: " << e.what() << std::endl;
    }

    try {
      auto const constexpr str = std::string_view{"HelloW"};
      std::vector<char> data_owner{str.begin(), str.end()};

      auto dyn_buf = asio::dynamic_buffer(data_owner, 50);
      auto [ec, n] = co_await asio::async_write(stream, dyn_buf, as_tuple);

      tout(TAG) << "write done: " << std::endl
                << "n:   " << n   << std::endl
                << "msg: " << str << std::endl
                << "ec:  " << ec.message()
                << std::endl;
    } catch(boost::system::error_code &e) {
      tout(TAG) << "W: " << e.what() << std::endl;
    }


    timer.expires_after(std::chrono::milliseconds(2500));
    co_await timer.async_wait(use_awaitable);
  }

  service_pool.join();
  tout(TAG) << "Normal exit" << std::endl;
  co_return 0;
}

int main() {
  asio::io_context app_ctx;

  auto fut = asio::co_spawn(asio::make_strand(app_ctx), mainCo(), asio::use_future);

  tout() << "MainThread run start" << std::endl;
  app_ctx.run();
  tout() << "MainThread run done" << std::endl;

  return fut.get();
}
