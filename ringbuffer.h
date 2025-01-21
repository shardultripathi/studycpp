// Inspired from https://www.youtube.com/watch?v=K3P_Lmq6pw0, but changed to
// rm the custom allocator and we templatized the capacity.

#include <stdio.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace toy {

template <typename T, std::size_t Capacity>
class SPSC_FifoQ {
    static_assert(Capacity > 0);
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2!");
    static constexpr int CACHE_LINE_SIZE = 64;

   public:
    SPSC_FifoQ() = default;

    ~SPSC_FifoQ() {
        // Acquire the latest position of both the producer and consumer.
        auto consumer_pos = consumer_pos_.load(std::memory_order_acquire);
        auto producer_pos = producer_pos_.load(std::memory_order_acquire);

        // Delete every pushed entry that was not popped.
        while (!IsEmpty(producer_pos, consumer_pos)) {
            std::destroy_at(
                std::launder(reinterpret_cast<T*>(&ring_[Index(consumer_pos++)])));
        }

        // Free the allocated ring buffer space.
        // std::allocator_traits<Alloc>::deallocate(*this, ring_, Capacity);
    }

    template <typename... Args>
    bool Push(Args&&... args) {
        // memory_order_relaxed: i.e. producer (this thread) owns this atomic.
        const auto producer_pos = producer_pos_.load(std::memory_order_relaxed);
        // memory_order_acquire: to download consumer's pos with this thread.
        const auto consumer_pos = consumer_pos_.load(std::memory_order_acquire);
        if (IsFull(producer_pos, consumer_pos)) {
            return false;
        }

        // Construct the object emplace.
        ::new (&ring_[Index(producer_pos)]) T(std::forward<Args>(args)...);

        // Advance the producer's cursor.
        // memory_order_release: to inform consumer that we advanced.
        producer_pos_.store(producer_pos + 1, std::memory_order_release);
        return true;
    }

    bool Pop(T& out) {
        // memory_order_relaxed: i.e. consumer (this thread) owns this atomic.
        const auto consumer_pos = consumer_pos_.load(std::memory_order_relaxed);
        // memory_order_acquire: to download the producer's pos into this thread.
        const auto producer_pos = producer_pos_.load(std::memory_order_acquire);
        if (IsEmpty(producer_pos, consumer_pos)) {
            return false;
        }

        std::byte* element = &ring_[Index(consumer_pos)];
        out = std::move(*std::launder(reinterpret_cast<T*>(element)));
        std::destroy_at(std::launder(reinterpret_cast<T*>(element)));

        // memory_order_release: i.e. publish to producer that we advanced.
        consumer_pos_.store(consumer_pos + 1, std::memory_order_release);
        return true;
    }

   private:
    static auto Index(const std::size_t pos) {
        return (pos & (Capacity - 1)) * sizeof(T);
    }
    static bool IsFull(const std::size_t producer_pos,
                       const std::size_t consumer_pos) {
        return producer_pos - consumer_pos >= Capacity;
    }
    static bool IsEmpty(const std::size_t producer_pos,
                        const std::size_t consumer_pos) {
        return producer_pos == consumer_pos;
    }

    alignas(T) std::byte ring_[sizeof(T) * Capacity];
    alignas(CACHE_LINE_SIZE)
        std::atomic<std::size_t> producer_pos_ = {0};
    alignas(CACHE_LINE_SIZE)
        std::atomic<std::size_t> consumer_pos_ = {0};
};

}  // namespace toy
int test_ring() {
    toy::SPSC_FifoQ<std::string, 4> q;
    for (int i = 0; i < 8; ++i) {
        char buf[100];
        // std::sprintf(buf, "This is string: %d", i);
        q.Push(std::string(buf));
    }
    for (int i = 0; i < 9; ++i) {
        std::string t;
        q.Pop(t);
        std::cout << i << ": " << t << "\n";
    }
    return 0;
}