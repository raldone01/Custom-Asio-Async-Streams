/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef ASYNCREADSTREAMDEMO_H
#define ASYNCREADSTREAMDEMO_H

#include <random>
#include <memory>
#include <iostream>
#include <syncstream>
#include <boost/asio.hpp>

namespace my {
  namespace {
    namespace asio = boost::asio;
  }
  namespace detail {
    /**
     * This class represents the actual service producing the data that may be read asynchronously.
     */
    class ProducerImpl : public std::enable_shared_from_this<ProducerImpl> {
      // The public fields of this class are only visible to the Producer class and the AsyncXXXXXStream classes.
    public:
      /**
       * The data that the Producer produces.
       */
      std::string producedData = std::move(std::string(4, 'a'));
      /**
       * The strand used to avoid concurrent execution if the passed io context is run by multiple threads.
       */
      asio::io_context::strand strand;
    private:
      /**
       * The generator used to modify the data.
       */
      std::mt19937 gen;
      /**
       * Used to simulate a concurrent process that modifies the data.
       * MODIFICATIONS MUST BE DONE OVER USING THE STRAND.
       * Otherwise the AsyncStream and the modification may cause undefined behaviour.
       */
      asio::steady_timer timer;
      /**
       * Used to count how many times the data has been modified.
       */
      size_t ops = 0;
      static const constexpr auto MAX_MODS = 5;
      /**
       * https://stackoverflow.com/a/69753502/4479969
       */
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

      void produce() {
        // replace a random char in the string
        producedData[gen() % producedData.size()] = charset[gen() % (sizeof(charset) / sizeof(charset[0]))];
        std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                    << " Produced "
                                    << ops
                                    << " Data: "
                                    << producedData
                                    << std::endl;
        // also append a random string
        producedData += gen_string(5, gen);
      }

      /**
       * Performs an expensive operation using the io context passed to the producer in the constructor.
       * @param captured_self Important: store a strong reference to this object
       */
      void veryExpensiveOperation(std::shared_ptr<ProducerImpl> captured_self) {
        ops++;
        produce();
        if (ops < MAX_MODS) {
          // schedule another operation
          timer.expires_after(std::chrono::milliseconds(1000));
          // Important: pass the strong reference to the lambda to ensure the producer can finish producing its values without going out of reference
          // Important: bind the executor to the strand to avoid concurrent modification/reading of the producedData
          timer.async_wait(asio::bind_executor(strand, [this, captured_self = std::move(captured_self)](auto ec) {
            // when the timer runs out invoke the next modification
            veryExpensiveOperation(captured_self);
          }));
        }
      }

      /**
       * First take a look at the normal veryExpensiveOperation.
       * This function does the same but allows the operation chain to be interrupted by the user by discarding the last Producer reference.
       */
      void veryExpensiveOperationAllowEarlyExit() {
        auto captured_self = weak_from_this();
        if (auto strong_self = captured_self.lock()) {
          ops++;
          produce();
          if (ops < MAX_MODS) {
            // schedule another operation
            timer.expires_after(std::chrono::milliseconds(1000));
            // Important: the lambda only captures a weak reference to allow the producer to go out of scope to allow the operation chain to be interrupted
            // Important: bind the executor to the strand to avoid concurrent modification/reading of the producedData
            timer.async_wait(asio::bind_executor(strand, [captured_self = std::move(captured_self)](auto ec) {
              // when the timer runs out attempt to invoke the next modification
              if (auto self = captured_self.lock()) {
                self->veryExpensiveOperationAllowEarlyExit();
              }
            }));
          }
        }
      }

    public:
      explicit ProducerImpl(asio::io_context &io) : strand{io}, timer{io} {}

      /**
       * Do not invoke twice.
       */
      void startOps() {
        asio::post(strand, [this, captured_self = shared_from_this()] {
          // Choose if the operations may be stopped prematurely by the user.
          // veryExpensiveOperationAllowEarlyExit();
          veryExpensiveOperation(captured_self);
        });
      }

      /**
       * The Producer wrapper ensures that this destructor is only called on its strand.
       * It may also be called by veryExpensiveOperationAllowEarlyExit() but is already on our strand.
       */
      ~ProducerImpl() {
        std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                    << " ProdImpl being destroyed"
                                    << std::endl;
      }
    };
  }

  using namespace detail;

  /**
   * This class is used by the user to indirectly interact with the ProducerImpl.
   */
  class Producer {
    /**
     * The friend declaration can be avoided by a stream factory function.
     */
    template<typename Executor> requires asio::is_executor<Executor>::value
    friend
    class MyAsyncReadStream;

    std::shared_ptr<ProducerImpl> impl;
  public:
    explicit Producer(asio::io_context &io) : impl{std::make_shared<ProducerImpl>(io)} {
      impl->startOps();
    }

    ~Producer() {
      // ensure the impl destructor is only called on the correct strand.
      auto strand = impl->strand; // copy the strand before use as the move would invalidate it otherwise
      auto fut = asio::post(strand, std::packaged_task<void()>(
          [impl = std::move(this->impl)]() {})); // it's important to move the impl here
      // it's not necessary for this lambda to actually contain any code it's just here to
      // uncomment the following line to make the destructor synchron
      // fut.wait();
      std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                  << " Producer destroyed" << std::endl;
    }
  };

  /**
   * Stream specifications https://www.boost.org/doc/libs/1_78_0/libs/beast/doc/html/beast/concepts/streams.html
   * NOTE: It is possible to make it a bidirectional stream by adding an async_write_some function.
   * @tparam Executor The executor used to call the handlers of the AsyncStream.
   */
  template<typename Executor> requires asio::is_executor<Executor>::value
  class MyAsyncReadStream {
    typedef void async_rw_handler(boost::system::error_code, size_t);

    /**
     * Holds the executor used to invoke the completion_handlers.
     */
    Executor executor;
    /**
     * The current position in the producedData.
     * NOTE: This might not exist if you are implementing something that actually consumes the read data.
     */
    size_t head;
    /**
     * The position to stop the read.
     * NOTE: This might not exist if you are implementing something that actually consumes the read data.
     */
    size_t end;

    /**
     * Hold a weak_ptr to the ProducerImpl.
     * MyAsyncReadStream behaves like a file descriptor.
     * If the Producer object is destroyed by the user an error code will be returned on the next read.
     */
    std::weak_ptr<ProducerImpl> implRef;
  public:

    /**
     * If MyAsyncReadStream should prevent the ProducerImpl from being destroyed even though the Producer class was destroyed
     * then a shared_ptr to the producer should be kept.
     * This is because otherwise the ProducerImpls destructor could be invoked from a wrong execution_context.
     *
     * Alternatively a weak_ptr to ProducerImpl may be used though the destructor of MyAsyncReadStream would need the same logic as the Producer has right now.
     */
    // std::shared_ptr<Producer> producerRef;

    explicit MyAsyncReadStream(Executor exe, Producer &producer, size_t start, size_t end) : executor{exe}, head{start},
                                                                                             end{end},
                                                                                             implRef{producer.impl} {}

    /**
     * Needed by the stream specification.
     */
    typedef Executor executor_type;

    /**
     * Needed by the stream specification.
     * @return Returns the executor used to invoke the completion_handlers.
     */
    auto get_executor() {
      return executor;
    }

    /**
     * This function implements the whole AsyncReadStream.
     * What a horrible template mess!
     * @param buffer The buffer to write into
     * @param token Might be one of asio::use_awaitable, asio::use_future, asio::as_tuple(asio::use_awaitable), asio::deferred or many more.
     * @return Depends on what token was chosen.
     */
    template<typename MutableBufferSequence,
        asio::completion_token_for<async_rw_handler>
        CompletionToken = typename asio::default_completion_token<Executor>::type>
    requires asio::is_mutable_buffer_sequence<MutableBufferSequence>::value
    auto async_read_some(const MutableBufferSequence &buffer,
                         CompletionToken &&token = typename asio::default_completion_token<Executor>::type()) {
      // the async_initiate function takes a lambda that receives a completion_handler to invoke to indicate the completion of the asynchronous operation
      // The lambda will be called in the same execution_context as the async_read_some function.
      // async_initiate directly calls the lambda we passed to it.
      // Therefore, it is OK for us to capture by reference.
      return asio::async_initiate<CompletionToken, async_rw_handler>([&](auto completion_handler) {
        // If you get an error on the following line it means that your handler
        // does not meet the documented type requirements for a ReadHandler.
        BOOST_ASIO_READ_HANDLER_CHECK(CompletionToken, completion_handler) type_check;

        std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                    << " read async_init" << std::endl;
        // Get a strong reference to the ProducerImpl.
        auto impl = this->implRef.lock();

        // Check if the implementation we refer to still exists.
        if (impl == nullptr) {
          // Do not directly invoke the completion_handler
          // According to the specification the completion_handler MUST NOT be invoked in the async_read_some function.
          asio::post(this->executor,
                     [completion_handler = std::forward<CompletionToken>(completion_handler)]() mutable {
                       std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                                   << " read bad_descriptor" << std::endl;
                       completion_handler(asio::error::bad_descriptor, 0);
                     });
          return;
        }
        // Construct a WorkGuard to prevent the user's executor from running out of work while the async operation is still in progress
        auto resultWorkGuard = asio::make_work_guard(this->executor);

        // Post work to the strand of the ProducerImpl and perform the read.
        // This avoids concurrent access to the read data.
        // NOTE: Do not capture the completion_handler by reference! It is fine to capture this by reference since the user must ensure the streams lifetimes.
        // NOTE: Do NOT take the buffer by reference!
        asio::post(impl->strand, [this, buffer = std::move(buffer), impl,
            resultWorkGuard = std::move(resultWorkGuard),
            completion_handler = std::forward<CompletionToken>(completion_handler)]() mutable {
          // We made it to the ProducerImpls execution_context! Yay
          std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                      << " read performing read" << std::endl;

          // The rest is smooth sailing. Get the iterators from the buffer and perform the actual read.
          auto buf_begin = asio::buffers_begin(buffer);
          auto buf_end = asio::buffers_end(buffer);
          boost::system::error_code err = asio::error::fault; // set a general failure just in case
          size_t it = 0;
          while (head <= end) {
            if (buf_begin == buf_end) {
              // error the buffer is smaller than the request read amount
              err = asio::error::no_buffer_space;
              goto completion;
            }
            if (impl->producedData.size() <= head) {
              // wait for new data
              err = asio::error::would_block;
              // err = asio::error::eof; // Return this only if you know there is no more data coming.
              goto completion;

              // NOTE: DO NOT BLOCK HERE. This would blockt the ProducerImpl which you NEVER want to block.
              // Instead, you could save the stream(this) and the completion_handler to a vector in the ProducerImpl
              // and have it finish the read call the completion_handler.
              // (Use post(stream->executor, lambda { completion_handler(ec, n); }); To ensure the correct execution_context.)
            }

            // Really copy the data into the buffer.
            *buf_begin++ = impl->producedData[head];
            head++;
            it++;
          }
          // Hurray! The read completed successfully
          err = asio::error::eof;
          completion:
          // Observe how the execution_context changes between the next two log statements
          std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                      << " read before completion post" << std::endl;
          asio::post(this->executor,
                     [err, it, completion_handler = std::forward<CompletionToken>(completion_handler)]() mutable {
                       std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                                   << " read invoking completion_handler: "
                                                   << err.message() << " " << it << std::endl;
                       completion_handler(err, it);
                     });
        });
      }, token);
    }
  };
}

#endif //ASYNCREADSTREAMDEMO_H
