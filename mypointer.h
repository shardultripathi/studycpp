#include <atomic>
#include <cassert>
#include <concepts>
#include <stdexcept>
#include <type_traits>

namespace toy {

template <typename T>
concept Array = std::is_array_v<T>;

template <typename T>
concept NonArray = !std::is_array_v<T>;

template <typename T>
class unique_ptr {
    static constexpr bool is_array = std::is_array_v<T>;
    using element_type = std::remove_extent_t<T>;
    element_type *ptr;

   public:
    constexpr unique_ptr() noexcept : ptr(nullptr) {}
    constexpr explicit unique_ptr(element_type *p) noexcept : ptr(p) {}

    // Copy operations deleted
    unique_ptr(const unique_ptr &) = delete;
    unique_ptr &operator=(const unique_ptr &) = delete;

    // Move operations
    constexpr unique_ptr(unique_ptr &&rhs) noexcept : ptr(rhs.ptr) {
        rhs.ptr = nullptr;
    }

    constexpr unique_ptr &operator=(unique_ptr &&rhs) noexcept {
        if (&rhs == this) return *this;

        if constexpr (is_array) {
            delete[] ptr;
        } else {
            delete ptr;
        }

        ptr = rhs.ptr;
        rhs.ptr = nullptr;
        return *this;
    }

    // Accessor methods with concepts
    constexpr auto operator*() const
        requires NonArray<T>
    {
        return *ptr;
    }

    constexpr auto operator->() const
        requires NonArray<T>
    {
        return ptr;
    }

    constexpr auto operator[](size_t i) const
        requires Array<T>
    {
        return ptr[i];
    }

    // Common methods
    [[nodiscard]] constexpr element_type *get() const noexcept {
        return ptr;
    }

    constexpr void reset(element_type *p = nullptr) {
        auto old = ptr;
        ptr = p;
        if constexpr (is_array) {
            delete[] old;
        } else {
            delete old;
        }
    }

    [[nodiscard]] constexpr element_type *release() noexcept {
        auto tmp = ptr;
        ptr = nullptr;
        return tmp;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return ptr != nullptr;
    }

    constexpr ~unique_ptr() {
        if constexpr (is_array) {
            delete[] ptr;
        } else {
            delete ptr;
        }
    }

    // Comparison operators
    [[nodiscard]] friend constexpr bool operator==(const unique_ptr &lhs, std::nullptr_t) noexcept {
        return lhs.ptr == nullptr;
    }

    [[nodiscard]] friend constexpr bool operator==(const unique_ptr &lhs, const unique_ptr &rhs) noexcept {
        return lhs.ptr == rhs.ptr;
    }
};

template <class T>
class shared_ptr {
    class control_block {
        T *ptr;
        std::atomic<size_t> ref_count;

       public:
        explicit control_block(T *p) noexcept : ptr(p), ref_count(1) {}

        control_block(const control_block &rhs) = delete;
        control_block &operator=(const control_block &rhs) = delete;
        control_block(control_block &&rhs) = delete;
        control_block &operator=(control_block &&rhs) = delete;

        T *get() const noexcept {
            return ptr;
        }
        size_t use_count() const noexcept {
            return ref_count;
        }
        void increment_ref() noexcept {
            ref_count.fetch_add(1, std::memory_order_relaxed);
        }
        bool decrement_ref() noexcept {
            if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete ptr;
                ptr = nullptr;
                return true;
            }
            return false;
        }
        ~control_block() {
            assert(ref_count == 0);
            assert(ptr == nullptr);
        }
    };

    control_block *ctrl;

   public:
    // default
    shared_ptr() noexcept : ctrl(nullptr) {}

    // only construct control_block here
    explicit shared_ptr(T *ptr) noexcept {
        ctrl = ptr ? new control_block(ptr) : nullptr;
    }

    // copy
    shared_ptr(const shared_ptr &rhs) noexcept : ctrl(rhs.ctrl) {
        if (ctrl) {
            ctrl->increment_ref();
        }
    }

    // copy assignment
    shared_ptr &operator=(const shared_ptr &rhs) noexcept {
        if (&rhs == this) {
            return *this;
        }
        // first take care of original shared_ptr logic
        if (ctrl && ctrl->decrement_ref()) {
            delete ctrl;
        }
        // then copy rhs
        ctrl = rhs.ctrl;
        if (ctrl) {
            ctrl->increment_ref();
        }
        return *this;
    }

    // move
    shared_ptr(shared_ptr &&rhs) noexcept : ctrl(rhs.ctrl) {
        rhs.ctrl = nullptr;
    }

    // move assignment
    shared_ptr &operator=(shared_ptr &&rhs) noexcept {
        if (&rhs == this) {
            return *this;  // do nothing for self move
        }
        if (ctrl && ctrl->decrement_ref()) {
            delete ctrl;
        }
        ctrl = rhs.ctrl;
        rhs.ctrl = nullptr;
        return *this;
    }

    // destruct
    ~shared_ptr() {
        if (ctrl && ctrl->decrement_ref()) {
            delete ctrl;
        }
    }

    // accessors
    T *get() const noexcept {
        return ctrl ? ctrl->get() : nullptr;
    }

    T &operator*() const {
        if (!ctrl || !ctrl->get()) {
            throw std::runtime_error("dereferencing null shared pointer");
        }
        return *ctrl->get();
    }

    T *operator->() const noexcept {
        return get();
    }
};

}  // namespace toy