#include <array>
#include <cstddef>

namespace toy {

template <typename T, size_t StackThreshold>
class HybridAllocator {
    alignas(T) std::array<std::byte, StackThreshold * sizeof(T)> stackStorage;
    size_t stackCount = 0;

    bool isStackPointer(const T* p) const noexcept {
        auto ptr = reinterpret_cast<const std::byte*>(p);
        if (ptr >= stackStorage.data() && ptr < stackStorage.data() + stackStorage.size()) {
            return true;
        }
        return false;
    }

   public:
    // allocator traits
    using value_type = T;
    using pointer = T*;

    HybridAllocator() noexcept = default;

    pointer allocate(size_t n) {
        if (stackCount + n > StackThreshold) {
            // needs to be allocated on heap
            return static_cast<pointer>(::operator new(n * sizeof(T)));
        }
        pointer ptr = reinterpret_cast<pointer>(&stackStorage[stackCount * sizeof(T)]);
        stackCount += n;
        return ptr;
    }

    void deallocate(pointer p, size_t n) noexcept {
        if (isStackPointer(p)) {
            auto dealloc_end = reinterpret_cast<const std::byte*>(p) + (n * sizeof(T));
            auto stack_top = &stackStorage[stackCount * sizeof(T)];
            if (dealloc_end == stack_top) {
                stackCount -= n;
            }
        } else {
            ::operator delete(p);
        }
    }
};

}