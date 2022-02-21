#ifndef ASYNCWRITESTREAMDEMO_H
#define ASYNCWRITESTREAMDEMO_H

/**
 * TBD
 */


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

struct ConsumerImpl {

};

class Consumer {
  std::shared_ptr<ConsumerImpl> impl;
};
*/

#endif //ASYNCWRITESTREAMDEMO_H
