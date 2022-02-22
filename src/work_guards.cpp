/**
 * This file shows different ways of preventing an io_context from running out of work.
 * https://stackoverflow.com/questions/71194070/asio-difference-between-prefer-require-and-make-work-guard
 */

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <syncstream>
#include <iostream>

namespace asio = boost::asio;

int main() {
  asio::io_context workerIO;
  boost::thread workerThread;
  {
    // ensure the worker io context stands by until work is posted at a later time
    // one of the below is needed for the worker to execute work which one should I use?
    auto prodWork = asio::make_work_guard(workerIO);
    // prodWork.reset(); // can be cleared
    // asio::any_io_executor prodWork2 = asio::prefer(workerIO.get_executor(), asio::execution::outstanding_work_t::tracked);
    // prodWork2 = asio::any_io_executor{}; // can be cleared
    // asio::any_io_executor prodWork3 = asio::require(workerIO.get_executor(), asio::execution::outstanding_work_t::tracked);
    // prodWork3 = asio::any_io_executor{}; // can be cleared

    workerThread = boost::thread{[&workerIO] {
      std::osyncstream(std::cout) << "Worker RUN START: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
      workerIO.run();
      std::osyncstream(std::cout) << "Worker RUN ENDED: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
    }};
    asio::io_context appIO;


    std::osyncstream(std::cout) << "Main RUN START: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;

    // schedule work here
    {
      auto timer = asio::steady_timer{appIO};
      timer.expires_after(std::chrono::seconds(4));
      timer.async_wait([&workerIO] (auto ec) {
        if (ec == asio::error::operation_aborted)
          std::osyncstream(std::cout) << "Main: timer aborted " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
        std::osyncstream(std::cout) << "Main: timer expired " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
        asio::post(workerIO.get_executor(), [] {
          // This is never executed without a work guard.
          std::osyncstream(std::cout) << "Worker WORK DONE " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
        });
        std::osyncstream(std::cout) << "Main: work posted to worker " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
      });
    }

    appIO.run();
    std::osyncstream(std::cout) << "Main RUN ENDED: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
  }
  workerThread.join(); // wait for the worker to finish its posted work
  std::osyncstream(std::cout) << "Main EXIT: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
  return 0;
}
