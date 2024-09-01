#include <thread>
#include "asio/io_context.hpp"

class AsioThreadPool {
public:
    explicit AsioThreadPool(size_t thread_count = 0)
            : thread_count_(thread_count == 0 ? std::thread::hardware_concurrency() : thread_count),
              io_context_(),
              work_guard_(asio::make_work_guard(io_context_)) {
    }

    ~AsioThreadPool() {
        stop();
    }

    asio::io_context& get_io_context() {
        return io_context_;
    }

    void stop() {
        work_guard_.reset();
        io_context_.stop();
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void run() {
        if (threads_.empty()) {
            threads_.reserve(thread_count_);
            for (size_t i = 0; i < thread_count_; ++i) {
                threads_.emplace_back([this]() {
                    io_context_.run();
                });
            }
        }
    }

private:
    size_t thread_count_;
    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::vector<std::thread> threads_;
};