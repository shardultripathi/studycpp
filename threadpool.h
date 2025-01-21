#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <vector>

namespace toy {

class threadpool {
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;

    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mtx_);
                cv_.wait(lock, [&] {
                    return stop_ || !tasks_.empty();
                });
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

   public:
    explicit threadpool(size_t num) : stop_(false) {
        workers_.reserve(num);
        for (size_t idx = 0; idx < num; ++idx) {
            workers_.emplace_back([&] {
                worker_loop();
            });
        }
    }

    template <typename F, typename... Args>
    decltype(auto) enqueue(F&& f, Args&&... args) {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            [f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable {
                return std::forward<F>(f)(std::forward<Args>(args)...);
            });
        std::future<ReturnType> future = task->get_future();
        {
            std::unique_lock lock(mtx_);
            if (stop_) {
                throw std::runtime_error("queue already stopped");
            }

            tasks_.emplace([task]() mutable {
                (*task)();
            });
        }
        cv_.notify_one();
        return future;
    }

    // not copyable
    threadpool(const threadpool& rhs) = delete;
    threadpool& operator=(const threadpool& rhs) = delete;

    ~threadpool() {
        stop_ = true;
        cv_.notify_all();
        for (auto& worker : workers_) {
            worker.join();
        }
    }
};

}  // namespace toy
