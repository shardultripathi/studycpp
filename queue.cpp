// The outline of a single-producer multiple-consumer queue has been provided,
// please complete the missing portions of code---i.e. fill out the emplace()
// and pop() functions and add any necessary member variables. Here are some
// requirements/hints to guide you:
//
// Only correctness matters, we do not care about performance. i.e. using
// std::mutex and other classes from the standard library is perfectly fine.
//
// The queue should be blocking, i.e. if the queue is empty, then pop()
// should wait until a producer adds something to the queue.
//
// The queue should be unbounded, i.e. emplace() can just keep adding
// elements, there is no need to wait for the consumer to pop elements.

#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T>
class SPMCQueue {
   public:
    template <typename... Args>
    void emplace(Args&&... args) {
        // lock mutex to protect shared queue
        std::unique_lock<std::mutex> lock(mtx_);

        // construct element in place
        queue_.emplace(std::forward<Args>(args)...);

        // notify single waiting consumer
        cv_.notify_one();
    }

    T pop() {
        // lock mutex to protect shared queue
        std::unique_lock<std::mutex> lock(mtx_);

        // wait for an element to be available
        // this still works because if the mutex is locked,
        // the condition variable will unlock it and wait
        // thereby allowing the producer to lock it
        cv_.wait(lock, [this] { return !queue_.empty(); });

        // get element from queue (move it out)
        T val = std::move(queue_.front());
        queue_.pop();
        return val;
    }

   private:
    std::queue<T> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
};
