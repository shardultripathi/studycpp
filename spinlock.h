#include <atomic>

namespace toy {

class spinlock {
    // initial state -> false
    // acquire lock -> if state is false, set to true
    //              -> if state is true, busy wait until false
    //
    // release lock -> set state to false
    static constexpr int CACHE_LINE_SIZE = 64;

    // prevent false sharing of the spin lock state
    alignas(CACHE_LINE_SIZE) std::atomic<bool> state;

   public:
    spinlock() : state(false) {}

    void lock() {
        while (true) {
            // fast path
            bool exp = false;
            // minimize CAS instructions because they are more expensive for cpu
            // due to cache coherency traffic and flooding the cpu store buffer
            if (state.compare_exchange_strong(exp, true, std::memory_order_acq_rel)) {
                return;
            }

            // busy wait - simple load satisfied from cache
            while (state.load(std::memory_order_acquire)) {
                _mm_pause();
            }
        }
    }

    void unlock() {
        state.store(false, std::memory_order_release);
    }
};
}