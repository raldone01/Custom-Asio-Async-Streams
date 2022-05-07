/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * TODO: add default_completion_token
 */

#include "Helpers.h"

#include <coroutine>
#include <future>
#include <random>
#include <string>
#include <memory>

namespace asio = boost::asio;

namespace ModernIOService {
  namespace {
    template<typename Executor> requires my_is_executor<Executor>::value
    class ModernIOServiceImpl : public std::enable_shared_from_this<ModernIOServiceImpl<Executor>> {
      template<typename CallerExecutor, typename ModernIOService> requires my_is_executor<CallerExecutor>::value
      friend
      class MyAsyncStream;

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
      asio::awaitable<void> main(std::shared_ptr<ModernIOServiceImpl> /* captured_self */) {
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

    public:

      /**
       * NOTE: The constructor of the service is called from a foreign executor!
       *       When you want to init executor specific things do it in the init function.
       */
      explicit ModernIOServiceImpl(Executor &&exe) : strand{asio::make_strand(exe)}, timer{exe.context()} {}

      /**
       * This function is called by the wrapper from a foreign executor!
       * However, as it invoked after the constructor `shared_from_this()` is available.
       */
      void init() {
        // if we wanted to init things on OUR executor
        asio::post(asio::bind_executor(strand, [this, captured_self = this->shared_from_this()]() {
          tout() << "ModernIOServiceImpl init" << std::endl;
        }));

        // start the main io service loop
        asio::co_spawn(strand, main(this->shared_from_this()), asio::detached);
      }

      auto make_destructor_work_guard() {
        return asio::make_work_guard(strand);
      }

      /// The service wrapper ensures that this destructor is called on the destructor_work_guards executor.
      ~ModernIOServiceImpl() {
        tout() << "ModernIOServiceImpl destructor" << std::endl;
      }

      // region direct async functions

      /**
       * This function typedef indicates the RETURN type of the async_functions.
       * (In this case all functions use the same function typedef because they all return the same values.)
       *
       * Note:  Avoid returning more than two values.
       *        Although it is possible it's rather clunky.
       *
       *        Instead I recommend to only return an error_code and the value you want to return.
       *        The types of the return values MUST be default constructive. (That requirements ironically prevents you from returning boost::outcomes. See the outcome example.)
       *        If there is no possible error return value the error_code/ec parameter may be omitted.
       *        You should only throw when the error is unrecoverable otherwise I advise to use error_codes.
       */
      typedef void (async_return_function)(boost::system::error_code ec, size_t buffer_in_size, size_t buffer_out_size);
      //   typedef void (async_return_function)(boost::system::error_code ec, size_t exampleReturnValue1);  // use this to return one value with    error support
      //   typedef void (async_return_function)(size_t exampleReturnValue1);                                // use this to return one value without error support
      //   typedef void (async_return_function)(boost::system::error_code ec, struct yourReturnValues);     // use this if you have to return more than one value with error support

      /**
       * This function shows how to implement a async function with a completion token using `asio::async_initiate`.
       * This is useful if coroutines aren't available or for performance issues.
       */
      template<asio::completion_token_for<async_return_function> CompletionToken>
      auto async_buffer_op_initiate(bool buffer_in_clear, bool buffer_out_clear,
                                    CompletionToken &&token) {
        return asio::async_initiate<CompletionToken, async_return_function>(
          [this, buffer_in_clear, buffer_out_clear] // It is imperative to capture any parameters BY VALUE or to forward/move them.
            (auto completion_handler) mutable {
            const constexpr auto TAG = "async_initiate_function";
            // this gets the executor that asio has already conveniently associated with the completion handler.
            auto assoc_executor = asio::get_associated_executor(completion_handler);

            tout(TAG) << "Inside" << std::endl;

            asio::post(this->strand, [&TAG, this, workGuard = asio::make_work_guard(assoc_executor),
                                      completion_handler = std::move(completion_handler),
                                      buffer_in_clear, buffer_out_clear]() mutable {
              tout(TAG) << "Work" << std::endl;

              auto buffer_in_size = buffer_in.size(), buffer_out_size = buffer_out.size();
              if (buffer_in_clear)
                buffer_in = "";
              if (buffer_out_clear)
                buffer_out = "";

              // std::move(completion_handler)(std::error_code{}, buffer_in_size, buffer_out_size); // ILLEGAL!!! Doing this would leak the service executor to the caller.
              // Don't forget to post back to the original calling executor.

              asio::post(workGuard.get_executor(),
                         [buffer_in_size, buffer_out_size,
                          completion_handler = std::move(completion_handler)]() mutable {
                           std::move(completion_handler)(std::error_code{}, buffer_in_size, buffer_out_size);
                         });
            });
          },
          token);
      }

      /**
       * This function shows how to implement a async function with a completion token using an `asio::awaitable`.
       *
       * Why don't we just use an awaitable directly?
       * By using co_spawn we make the function look and feel like the stock asio functions as it allows us to take ANY completion token parameter.
       *
       * I recommend you to use this approach where possible as it is the easiest.
       */
      template<asio::completion_token_for<async_return_function> CompletionToken>
      auto async_buffer_op_coro(bool buffer_in_clear, bool buffer_out_clear, CompletionToken && token) {
        return asio::async_initiate<CompletionToken, async_return_function>(
          [this, buffer_in_clear, buffer_out_clear] // It is imperative to capture any parameters BY VALUE or to forward/move them.
            (auto completion_handler) mutable {
            const constexpr auto TAG = "async_initiate_function";
            asio::co_spawn(strand,
                           [this, &TAG, buffer_in_clear, buffer_out_clear,
                            completion_handler = std::move(completion_handler)]() mutable -> asio::awaitable<void> {
              // this gets the executor that asio has already conveniently associated with the completion handler.
              auto to_assoc_executor = asio::bind_executor(asio::get_associated_executor(completion_handler), asio::use_awaitable);
              tout(TAG) << "Inside" << std::endl;
              tout(TAG) << "Work" << std::endl;
              auto buffer_in_size = buffer_in.size(), buffer_out_size = buffer_out.size();
              if (buffer_in_clear)
                buffer_in = "";
              if (buffer_out_clear)
                buffer_out = "";
              co_await asio::post(to_assoc_executor);
              std::move(completion_handler)(std::error_code{}, buffer_in_size, buffer_out_size);
            }, asio::detached);
          },
          token);
      }
      // endregion
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
  }

  /// The user only interacts with this class. It hides away the `shared_ptr`.
  template <typename ServiceExecutor>
  requires my_is_executor<ServiceExecutor>::value
  class ModernIOService {
    using ModernIOServiceImplType = ModernIOServiceImpl<ServiceExecutor>;

    std::shared_ptr<ModernIOServiceImplType> impl;
    /// This work guard is necessary to ensure that the destructor can post the destruction.
    decltype(impl->make_destructor_work_guard()) workGuard;
  public:
    /**
     * The construct of this wrapper only accepts executors.
     * For it to accept execution_contexts directly we would have to add facilities to unpack executors from execution_contexts.
     * So to use this with an execution_context you just have to call `ctx.get_executor()` before passing it to the constructor.
     * @param exe The executor the service should use.
     */
    explicit ModernIOService(ServiceExecutor &&exe) : impl{std::make_shared<ModernIOServiceImplType>(std::forward<ServiceExecutor>(exe))}, workGuard{impl->make_destructor_work_guard()} {
      impl->init();
    }

    ModernIOService(ModernIOService &&) noexcept = default; // change default to delete if you don't want the service to be moveable
    ModernIOService& operator=(ModernIOService&&) noexcept = default;

    ModernIOService(const ModernIOService&) = delete;
    ModernIOService& operator=(ModernIOService const&) = delete;

    ~ModernIOService() {
      if (!impl) // do nothing on move
        return;
      tout() << "ModernIOService destructor" << std::endl;
      // ensure the impls destructor is called on the correct strand.
      auto fut = asio::post(workGuard.get_executor(), std::packaged_task<void()>([impl = std::move(this->impl)]() { // it's important to move the impl here
        // it's not necessary for this lambda to actually contain any code it's just here to run the destructor of the impl on the correct executor
      }));
      // fut.wait(); // uncomment this line to make the destructor synchronous
      tout() << "ModernIOService destoyed" << std::endl;
    }

    /**
     * Creates a MyAsyncStream instance for the user.
     */
    template<typename CallerExecutor>
    requires my_is_executor<CallerExecutor>::value
    MyAsyncStream<CallerExecutor, ModernIOServiceImplType> make_async_stream(CallerExecutor & exe) {
      return MyAsyncStream(impl, exe);
    }

    // region direct async functions

    /*
     * The functions in this region forward the users calls to the underlying impl.
     * They are necessary to expose the public impl functions.
     */

    template<asio::completion_token_for<typename ModernIOServiceImplType::async_return_function> CompletionToken>
    auto async_buffer_op_initiate(bool buffer_in_clear, bool buffer_out_clear,
                                  CompletionToken &&token) {
      assert(impl);
      return impl->async_buffer_op_initiate(buffer_in_clear, buffer_out_clear, std::forward<CompletionToken>(token));
    }

    template<asio::completion_token_for<typename ModernIOServiceImplType::async_return_function> CompletionToken>
    auto async_buffer_op_coro(bool buffer_in_clear, bool buffer_out_clear,
                                  CompletionToken &&token) {
      assert(impl);
      return impl->async_buffer_op_coro(buffer_in_clear, buffer_out_clear, std::forward<CompletionToken>(token));
    }

    // endregion
  };
}

/**
 * This is the actual main application loop.
 * It uses a new c++20 coroutine.
 */
template<typename T>
asio::awaitable<int> mainCo(T & srv_ctx) {
  const constexpr auto TAG = "MC";
  auto exe = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(exe);
  auto use_awaitable = asio::bind_executor(exe, asio::use_awaitable);
  auto as_tuple  = asio::experimental::as_tuple(use_awaitable);

  auto service = ModernIOService::ModernIOService(srv_ctx.get_executor());
  auto stream = service.make_async_stream(exe);
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

  // auto service2 = std::move(service);  // uncomment this line to see how important the asserts are
                                          // also uncomment the assert to see how 'helpful' the error is without it
  //async_initiate_function
  {
    tout(TAG) << "before calling" << std::endl;
    auto [ec, buffer_in_size, buffer_out_size] = co_await service.async_buffer_op_initiate(false, false, as_tuple);
    tout(TAG) << "after  calling Ec: " << ec.message() << " buffer_in_size " << buffer_in_size << " buffer_out_size " << buffer_out_size << std::endl;
  }
  //async_coro_function
  {
    tout(TAG) << "before calling" << std::endl;
    auto [ec, buffer_in_size, buffer_out_size] = co_await service.async_buffer_op_coro(false, false, as_tuple);
    tout(TAG) << "after  calling Ec: " << ec.message() << " buffer_in_size " << buffer_in_size << " buffer_out_size " << buffer_out_size << std::endl;
  }

  co_return 0;
}

int main() {
  asio::io_context app_ctx;
  asio::thread_pool srv_ctx{1};

  // Print the thread id of the service thread.
  asio::post(srv_ctx, std::packaged_task<void()>([]() {
    tout() << "ServiceThread run start" << std::endl;
  })).wait();

  auto fut = asio::co_spawn(asio::make_strand(app_ctx), mainCo(srv_ctx), asio::use_future);

  tout() << "MainThread run start" << std::endl;
  app_ctx.run();
  tout() << "MainThread run done" << std::endl;

  srv_ctx.join(); // the service thread stops here
  return fut.get();
}
