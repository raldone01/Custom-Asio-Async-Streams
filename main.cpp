#include "src/AsyncReadStreamDemo.h"

#include <coroutine>
#include <boost/asio/experimental/as_single.hpp>
#include <boost/bind/bind.hpp>
#include <boost/thread/thread.hpp>

using namespace my;

struct ProducerImpl : std::enable_shared_from_this<ProducerImpl> {
  std::string producedData;
  asio::io_context::strand strand;
private:
  std::mt19937 gen;
  asio::steady_timer timer;
  size_t ops = 0;

  // https://stackoverflow.com/a/69753502/4479969
  constexpr static const char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";

  template<typename URBG>
  std::string gen_string(std::size_t length, URBG &&g) {
    std::string result;
    result.resize(length);
    std::sample(std::cbegin(charset),
                std::cend(charset),
                std::begin(result),
                std::intptr_t(length),
                std::forward<URBG>(g));
    return result;
  }

  void veryExpensiveOperationALLOWINTERRUPTION() {
    auto captured_self = weak_from_this();
    if (auto self = captured_self.lock()) {
      ops++;
      producedData = gen_string(1024, gen);
      std::osyncstream(std::cout) << "T: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << " Produced! "
                                  << ops
                                  << std::endl;
      if (ops < 5) {
        timer.expires_after(std::chrono::milliseconds(1000));
        timer.async_wait(asio::bind_executor(strand, [captured_self](auto ec) {
          if (auto self = captured_self.lock()) {
            self->veryExpensiveOperation();
          }
        }));
      }
    }
  }

  void veryExpensiveOperation() {
    auto captured_self = shared_from_this();
    ops++;
    producedData = gen_string(1024, gen);
    std::osyncstream(std::cout) << "T: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << " Produced! "
                                << ops
                                << std::endl;
    if (ops < 5) {
      timer.expires_after(std::chrono::milliseconds(1000));
      timer.async_wait(asio::bind_executor(strand, [captured_self](auto ec) {
        captured_self->veryExpensiveOperation();
      }));
    }
  }

public:
  ProducerImpl(asio::io_context &io) : strand{io}, timer{io} {
    asio::post(strand, [this] { veryExpensiveOperation(); });
  }

  ~ProducerImpl() {
    std::osyncstream(std::cout) << "PROD IMPL DEAD!" << std::endl;
  }
};

struct Producer {
  std::shared_ptr<ProducerImpl> impl;

  Producer(asio::io_context &io) : impl{std::move(std::make_shared<ProducerImpl>(io))} {}

  ~Producer() {
    std::osyncstream(std::cout) << "PROD DEAD!" << std::endl;
  }
};

typedef void async_rw_handler(boost::system::error_code, size_t);

/**
 * Stream specifictations https://www.boost.org/doc/libs/1_78_0/libs/beast/doc/html/beast/concepts/streams.html
 * @tparam Executor
 */
template<typename Executor> requires asio::is_executor<Executor>::value
class MyAsyncReadStream {
  Executor executor;
  size_t head;
  size_t end;
public:
  std::weak_ptr<ProducerImpl> implRef;

  explicit MyAsyncReadStream(Executor exe, Producer &producer, size_t start, size_t end) : executor{exe}, head{start},
                                                                                           end{end},
                                                                                           implRef{producer.impl} {}

  /**
   * Needed by the stream specification.
   */
  typedef Executor executor_type;

  /**
   * Needed by the stream specification.
   * @return
   */
  auto get_executor() {
    return executor;
  }

  template<typename MutableBufferSequence,
      asio::completion_token_for<async_rw_handler>
      CompletionToken = typename asio::default_completion_token<asio::io_context>::type>
  requires asio::is_mutable_buffer_sequence<MutableBufferSequence>::value
  auto async_read_some(const MutableBufferSequence &buffer,
                       CompletionToken &&token = typename asio::default_completion_token<Executor>::type()) {
    // return asio::async_initiate<CompletionToken, async_rw_handler>(async_init(this), token, buffer);
    return asio::async_initiate<CompletionToken, async_rw_handler>([&](auto completion_handler) {
      // If you get an error on the following line it means that your handler
      // does not meet the documented type requirements for a ReadHandler.
      BOOST_ASIO_READ_HANDLER_CHECK(CompletionToken, completion_handler) type_check;

      std::osyncstream(std::cout) << "T: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                  << " before read" << std::endl;
      auto impl = this->implRef.lock();
      auto resultWorkGuard = asio::make_work_guard(this->executor);
      if (impl == nullptr) {
        // asio::post(trackedExecutor = std::move(trackedExecutor), [completion_handler = std::move(completion_handler)]() { completion_handler(asio::error::bad_descriptor, 0); });
        completion_handler(asio::error::bad_descriptor, 0);
        return;
      }
      // auto implWorkGuard = asio::make_work_guard(impl->strand);
      std::osyncstream(std::cout) << "T: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                  << " before post" << std::endl;
      asio::post(impl->strand, [this, impl, buffer, resultWorkGuard = std::move(
          resultWorkGuard), completion_handler = std::forward<CompletionToken>(completion_handler)]() mutable {
        std::osyncstream(std::cout) << "T: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << " reading"
                                    << std::endl;
        auto buf_begin = asio::buffers_begin(buffer);
        auto buf_end = asio::buffers_end(buffer);
        boost::system::error_code err = asio::error::fault;
        size_t it = 0;
        while (head <= end) {
          if (buf_begin == buf_end) {
            err = asio::error::no_buffer_space;
            goto completion;
          }
          if (impl->producedData.size() <= head) {
            // wait for new data
            err = asio::error::would_block;
            // err = asio::error::eof;
            goto completion;
          }
          *buf_begin++ = impl->producedData[head];
          head++;
          it++;
        }
        err = asio::error::eof;
        completion:
        asio::post(this->executor,
                   [err, it, completion_handler = std::forward<CompletionToken>(completion_handler)]() mutable {
                     std::osyncstream(std::cout) << "CP: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                                 << " "
                                                 << err.message() << " " << it << std::endl;
                     completion_handler(err, it);
                   });
      });
    }, token);
  }
};

/*
template<typename Executor> requires asio::is_executor<Executor>::value
struct MyAsyncWriteStream {
  Executor executor;
  explicit MyAsyncWriteStream(Executor exe) : executor{exe} {}
  auto get_executor() {
    return executor;
  }
  template<typename ConstBufferSequence,
      asio::completion_token_for<void(std::error_code, size_t)>
      CompletionToken = typename asio::default_completion_token<asio::io_context>::type> requires asio::is_const_buffer_sequence<ConstBufferSequence>::value
  auto async_write_some(ConstBufferSequence buffer, CompletionToken && token = typename asio::default_completion_token<asio::io_context>::type()) {
    return asio::async_initiate<CompletionToken, void(std::string)>(
        [&](auto completion_handler) mutable {
          auto trackedExecutor = asio::prefer(executor,
                                              asio::execution::outstanding_work_t::tracked);
          asio::post(strand, [this, beginAddr, endAddr, buffer, trackedExecutor = std::move(trackedExecutor), completion_handler = std::move(completion_handler)]() mutable {
            size_t it;
            auto err = m_mem_write(beginAddr, endAddr, buffer, it);
            asio::post(trackedExecutor, [err, it, completion_handler = std::move(
                completion_handler)]() { completion_handler(err, it); });
          });
        },
        token);
  }
};
*/
struct ConsumerImpl {

};

class Consumer {
  std::shared_ptr<ConsumerImpl> impl;
};

boost::asio::awaitable<void> mainCo(asio::io_context &appIO, Producer & prod) {
  try {
    auto appStrand = asio::io_context::strand{appIO};
    auto readStream = MyAsyncReadStream(appStrand, prod, 0, 99);
    std::vector<std::byte> dataBackend;
    std::osyncstream(std::cout) << "BRD: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
    auto dynBuffer = asio::dynamic_buffer(dataBackend, 50);
    auto [ec, n] = co_await asio::async_read(readStream, dynBuffer, asio::experimental::as_single(asio::use_awaitable)); // WARNING after co_await calls your execution_context might have changed
    std::osyncstream(std::cout) << "RD: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << " "
                                << ec.message() << " " << n << std::endl;
  } catch (std::exception &e) {
    std::printf("echo Exception: %s\n", e.what());
  }
}

int main() {
  asio::io_context prodIO;
  boost::thread prodThread;
  {
    // ensure the producer io context doesn't exit
    auto prodWork = asio::make_work_guard(prodIO);

    prodThread = boost::thread{[&prodIO] {
      std::osyncstream(std::cout) << "PROD RUN START: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                  << std::endl;
      prodIO.run();
      std::osyncstream(std::cout) << "PROD RUN ENDED: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                  << std::endl;
    }};
    asio::io_context appIO;

    auto prod = Producer{prodIO};

    asio::co_spawn(appIO, mainCo(appIO, prod), asio::detached);

    std::osyncstream(std::cout) << "Main START: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                << std::endl;

    appIO.run();
    std::osyncstream(std::cout) << "Main DONE: " << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                << std::endl;
  }
  prodThread.join();
  std::osyncstream(std::cout) << "Main EXIT: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
  return 42;
}
