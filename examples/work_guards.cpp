/* Copyright 2022 The CustomAsioAsyncStreams Contributors.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * This file shows different ways of preventing an io_context from running out of work.
 * https://stackoverflow.com/questions/71194070/asio-difference-between-prefer-require-and-make-work-guard
 * Thanks sehe!
 */

#include "Helpers.h"

#include <boost/asio.hpp>
#include <thread>

namespace asio = boost::asio;

/**
 * Although all ways shown in this programs effectively are executor_work_guards
 * you should really only use asio::make_work_guard(executor)
 * as this is the proper way to do it.
 * Also executor_work_guard has decoupled lifetime as a nifty feature. (`executor_work_guard.reset()`)
 */
int main() {
  asio::io_context workerIO;
  std::thread workerThread;
  {
    // ensure the worker io context stands by until work is posted at a later time
    // one of the below is needed for the worker to execute work which one should I use?
    auto prodWork = asio::make_work_guard(workerIO);
    // prodWork.reset(); // can be cleared
    // asio::any_io_executor prodWork2 = asio::prefer(workerIO.get_executor(), asio::execution::outstanding_work_t::tracked);
    // prodWork2 = asio::any_io_executor{}; // can be cleared
    // asio::any_io_executor prodWork3 = asio::require(workerIO.get_executor(), asio::execution::outstanding_work_t::tracked);
    // prodWork3 = asio::any_io_executor{}; // can be cleared

    workerThread = std::thread{[&workerIO] {
      tout() << "Worker run start" << std::endl;
      workerIO.run();
      tout() << "Worker run done" << std::endl;
    }};
    asio::io_context appIO;


    tout() << "Main: run start" << std::endl;

    // schedule work here
    {
      auto timer = asio::steady_timer{appIO};
      timer.expires_after(std::chrono::seconds(4));
      timer.async_wait([&workerIO] (auto ec) {
        if (ec == asio::error::operation_aborted)
          tout() << "Main: timer aborted" << std::endl;
        tout() << "Main: timer expired" << std::endl;
        asio::post(workerIO.get_executor(), [] {
          // This is never executed without a work guard.
          tout() << "Worker sent work done" << std::endl;
        });
        tout() << "Main: after work posted to worker" << std::endl;
      });
    }

    appIO.run();
    tout() << "Main run done" << std::endl;
  }
  workerThread.join(); // wait for the worker to finish its posted work
  tout() << "MainFunc exit" << std::endl;
  return 0;
}
