/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef ASYNCWRITESTREAMDEMO_H
#define ASYNCWRITESTREAMDEMO_H

/**
 * Check out the AsyncReadStream before looking at this file.
 * AsyncReadStream is more detailed.
 */

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
     * This class represents the actual service consuming the data that may be written asynchronously.
     */
    class ConsumerImpl : public std::enable_shared_from_this<ConsumerImpl> {
      // The public fields of this class are only visible to the Consumer class and the AsyncXXXXXStream classes.
      bool consuming = false;
    public:
      std::string consumedData;
      /**
       * The strand used to avoid concurrent execution if the passed io context is run by multiple threads.
       */
      asio::io_context::strand strand;
    private:
      /**
       * Used to simulate a concurrent process that uses the data.
       * MODIFICATIONS MUST BE DONE OVER USING THE STRAND.
       * Otherwise the AsyncStream and the modification may cause undefined behaviour.
       */
      asio::steady_timer timer;

      bool consume() {
        // replace a random char in the string
        if (consumedData.size()) {
          auto out = consumedData[0];
          consumedData = consumedData.erase(0, 1);
          std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                      << " Consumed "
                                      << out
                                      << std::endl;
          return true;
        }
        return false;
      }

      /**
       * Performs an expensive operation using the io context passed to the consumer in the constructor.
       * @param captured_self Important: store a strong reference to this object
       */
      void veryExpensiveOperation(std::shared_ptr<ConsumerImpl> captured_self) {
        if (consume()) {
          // schedule another operation
          timer.expires_after(std::chrono::milliseconds(1000));
          // Important: pass the strong reference to the lambda to ensure the producer can finish producing its values without going out of reference
          // Important: bind the executor to the strand to avoid concurrent modification/reading of the consumedData
          timer.async_wait(asio::bind_executor(strand, [this, captured_self = std::move(captured_self)](auto ec) {
            // when the timer runs out invoke the next modification
            veryExpensiveOperation(captured_self);
          }));
        } else
          consuming = false;
      }

    public:
      explicit ConsumerImpl(asio::io_context &io) : strand{io}, timer{io} {}
      /**
       * Do not invoke twice.
       */
      void ensureConsuming() {
        if (!consuming) {
          consuming = true;
          asio::post(strand, [this, captured_self = shared_from_this()] {
            veryExpensiveOperation(captured_self);
          });
        }
      }

      /**
       * The Consumer wrapper ensures that this destructor is only called on its strand.
       */
      ~ConsumerImpl() {
        std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                    << " ConsImpl being destroyed"
                                    << std::endl;
      }
    };
  }

  using namespace detail;

  /**
   * This class is used by the user to indirectly interact with the ProducerImpl.
   */
  class Consumer {
    /**
     * The friend declaration can be avoided by a stream factory function.
     */
    template<typename Executor> requires asio::is_executor<Executor>::value
    friend class MyAsyncWriteStream;

    std::shared_ptr <ConsumerImpl> impl;
  public:
    explicit Consumer(asio::io_context &io) : impl{std::make_shared<ConsumerImpl>(io)} {
      impl->ensureConsuming();
    }

    ~Consumer() {
      // ensure the impl destructor is only called on the correct strand.
      auto strand = impl->strand; // copy the strand before use as the move would invalidate it otherwise
      auto fut = asio::post(strand, std::packaged_task<void()>([impl = std::move(this->impl)]() {})); // it's important to move the impl here
      // it's not necessary for this lambda to actually contain any code it's just here to
      // uncomment the following line to make the destructor synchron
      // fut.wait();
      std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                  << " Producer destroyed" << std::endl;
    }
  };

  /**
   * Stream specifications https://www.boost.org/doc/libs/1_78_0/libs/beast/doc/html/beast/concepts/streams.html
   * NOTE: It is possible to make it a bidirectional stream by adding an async_read_some function.
   * @tparam Executor The executor used to call the handlers of the AsyncStream.
   */
  template<typename Executor> requires asio::is_executor<Executor>::value
  class MyAsyncWriteStream {
    typedef void async_rw_handler(boost::system::error_code, size_t);

    /**
     * Holds the executor used to invoke the completion_handlers.
     */
    Executor executor;

    /**
     * Hold a weak_ptr to the ConsumerImpl.
     * MyAsyncWriteStream behaves like a file descriptor.
     * If the Consumer object is destroyed by the user an error code will be returned on the next write.
     */
    std::weak_ptr<ConsumerImpl> implRef;
  public:

    /**
     * If MyAsyncWriteStream should prevent the ConsumerImpl from being destroyed even though the Consumer class was destroyed
     * then a shared_ptr to the consumer should be kept.
     * This is because otherwise the ConsumerImpls destructor could be invoked from a wrong execution_context.
     *
     * Alternatively a weak_ptr to ConsumerImpl may be used though the destructor of MyAsyncWriteStream would need the same logic as the Consumer has right now.
     */
    // std::shared_ptr<Consumer> consumerRef;

    explicit MyAsyncWriteStream(Executor exe, Consumer &consumer) : executor{exe}, implRef{consumer.impl} {}

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
     * This function implements the whole AsyncWriteStream.
     * What a horrible template mess!
     * @param buffer The buffer to read from
     * @param token Might be one of asio::use_awaitable, asio::use_future, asio::as_tuple(asio::use_awaitable), asio::deferred or many more.
     * @return Depends on what token was chosen.
     */
    template<typename ConstBufferSequence,
        asio::completion_token_for <async_rw_handler>
        CompletionToken = typename asio::default_completion_token<Executor>::type>
    requires asio::is_const_buffer_sequence<ConstBufferSequence>::value
    auto async_write_some(const ConstBufferSequence &buffer,
                         CompletionToken &&token = typename asio::default_completion_token<Executor>::type()) {
      // the async_initiate function takes a lambda that receives a completion_handler to invoke to indicate the completion of the asynchronous operation
      // The lambda will be called in the same execution_context as the async_write_some function.
      // async_initiate directly calls the lambda we passed to it.
      // Therefore, it is OK for us to capture by reference.
      return asio::async_initiate<CompletionToken, async_rw_handler>([&](auto completion_handler) {
        // If you get an error on the following line it means that your handler
        // does not meet the documented type requirements for a WriteHandler.
        BOOST_ASIO_WRITE_HANDLER_CHECK(CompletionToken, completion_handler) type_check;

        std::osyncstream(std::cout) << "T" << std::hash<std::thread::id>{}(std::this_thread::get_id())
                                    << " write async_init" << std::endl;
        // Get a strong reference to the ConsumerImpl.
        auto impl = this->implRef.lock();

        // Check if the implementation we refer to still exists.
        if (impl == nullptr) {
          // Do not directly invoke the completion_handler
          // According to the specification the completion_handler MUST NOT be invoked in the async_write_some function.
          asio::post(this->executor, [completion_handler = std::forward<CompletionToken>(completion_handler)]() mutable {
            std::osyncstream(std::cout) << "T" << std::hash < std::thread::id > {}(std::this_thread::get_id())
                                        << " write bad_descriptor" << std::endl;
            completion_handler(asio::error::bad_descriptor, 0);
          });
          return;
        }
        // Construct a WorkGuard to prevent the user's executor from running out of work while the async operation is still in progress
        auto resultWorkGuard = asio::make_work_guard(this->executor);

        // Post work to the strand of the ConsumerImpl and perform the write.
        // This avoids concurrent access to the data.
        // NOTE: Capture the completion_handler by reference! It is fine to capture this by reference since the user must ensure the streams lifetimes.
        // NOTE: Do NOT take the buffer by reference!
        asio::post(impl->strand, [this, buffer = std::move(buffer), impl,
            resultWorkGuard = std::move(resultWorkGuard),
            completion_handler = std::forward<CompletionToken>(completion_handler)]() mutable {
          // We made it to the ConsumerImpls execution_context! Yay
          std::osyncstream(std::cout) << "T" << std::hash < std::thread::id > {}(std::this_thread::get_id())
                                      << " write performing write" << std::endl;

          // The rest is smooth sailing. Get the iterators from the buffer and perform the actual write.
          auto buf_begin = asio::buffers_begin(buffer);
          auto buf_end = asio::buffers_end(buffer);
          boost::system::error_code err = asio::error::fault; // set a general failure just in case
          size_t it = 0;
          while (buf_begin != buf_end) {
            /*
            if (output buffer is full) {
              // wait for the output buffer to empty
              err = asio::error::would_block;
              // err = asio::error::eof; // Return this only if you know the output buffer won't empty again.
              goto completion;

              // NOTE: DO NOT BLOCK HERE. This would block the ConsumerImpl which you NEVER want to block.
              // Instead, you could save the stream(this) and the completion_handler to a vector in the ConsumerImpl
              // and have it finish the write call the completion_handler.
              // (Use post(stream->executor, lambda { completion_handler(ec, n); }); To ensure the correct execution_context.)
            }*/

            // Really copy the data from the buffer.
            impl->consumedData.push_back(static_cast<char>(*buf_begin++));
            it++;
          }
          // Hurray! The write completed successfully
          err = asio::error::eof;
          completion:
          impl->ensureConsuming();
          // Observe how the execution_context changes between the next two log statements
          std::osyncstream(std::cout) << "T" << std::hash < std::thread::id > {}(std::this_thread::get_id())
                                      << " write before completion post" << std::endl;
          asio::post(this->executor,
                     [err, it, completion_handler = std::forward<CompletionToken>(completion_handler)]() mutable {
                       std::osyncstream(std::cout) << "T" << std::hash < std::thread::id > {}(std::this_thread::get_id())
                                                   << " write invoking completion_handler: "
                                                   << err.message() << " " << it << std::endl;
                       completion_handler(err, it);
                     });
        });
      }, token);
    }
  };
}

#endif //ASYNCWRITESTREAMDEMO_H
