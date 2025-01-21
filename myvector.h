#include <cassert>
#include <memory>

namespace toy {

template <class T, class Allocator = std::allocator<T>>
class vector {
    T* arr;
    // stateless => not propagating in constructors
    Allocator d_alloc;
    size_t d_capacity;
    size_t d_size;
    static constexpr float growth_factor = 1.5;
    static constexpr size_t init_capacity = 4;

   public:
    // constructor
    vector() : d_capacity(init_capacity), d_size(0) {
        arr = d_alloc.allocate(d_capacity);
    };
    vector(const vector& rhs) {
        d_capacity = rhs.d_capacity;
        d_size = rhs.d_size;
        assert(rhs.d_capacity > 0);
        arr = d_alloc.allocate(rhs.d_capacity);
        auto myit = arr;
        for (auto it = rhs.arr; it < rhs.arr + rhs.d_size; ++it) {
            new (myit++) T(*it);
        }
    }
    vector& operator=(const vector& rhs) {
        if (&rhs == this) return *this;
        vector(rhs).swap(*this);
        return *this;
    }
    vector(vector&& rhs) {
        arr = rhs.arr;
        d_capacity = rhs.d_capacity;
        d_size = rhs.d_size;
        rhs.arr = nullptr;
        rhs.d_size = 0;
    }
    vector& operator=(vector&& rhs) {
        if (&rhs == this) return *this;
        vector(std::move(rhs)).swap(*this);
        return *this;
    }

    // accessors
    inline size_t size() const {
        return d_size;
    }
    inline size_t capacity() const {
        return d_capacity;
    }

    void reserve(size_t new_cap) {
        if (new_cap <= d_capacity) return;
        d_capacity = new_cap;
        assert(d_capacity > 0);
        auto new_arr = d_alloc.allocate(d_capacity);
        auto myit = new_arr;
        for (auto it = arr; it < arr + d_size; ++it) {
            new (myit++) T(std::move(*it));
        }
        // Destroy old objects
        for (auto it = arr; it < arr + d_size; ++it) {
            std::destroy_at(it);
        }
        // Deallocate old memory
        d_alloc.deallocate(arr, d_capacity);
        arr = new_arr;
    }
    void resize(size_t new_size) {
        if (new_size <= d_size) {
            for (auto it = arr + new_size; it < arr + d_size; ++it) {
                std::destroy_at(it);
            }
        } else {
            size_t new_cap = d_capacity > new_size ? d_capacity : growth_factor * new_size;
            reserve(new_cap);
            for (auto it = arr + d_size; it < arr + new_size; ++it) {
                new (it) T;  // only if default constructible
            }
        }
        d_size = new_size;
    }
    T& operator[](size_t idx) const {
        assert(idx < d_size);
        return *(arr + idx);
    }

    // modifiers
    void swap(vector& rhs) noexcept {
        std::swap(arr, rhs.arr);
        std::swap(d_capacity, rhs.d_capacity);
        std::swap(d_size, rhs.d_size);
    }
    void push_back(const T& val) {
        if (d_size >= d_capacity) {
            size_t new_cap = growth_factor * d_size;
            reserve(new_cap);
        }
        new (arr + d_size++) T(val);
    }
    void push_back(T&& val) {
        if (d_size >= d_capacity) {
            size_t new_cap = growth_factor * d_size;
            reserve(new_cap);
        }
        new (arr + d_size++) T(std::move(val));
    }

    template <class... Args>
    void emplace_back(Args&&... args) {
        if (d_size >= d_capacity) {
            size_t new_cap = growth_factor * d_size;
            reserve(new_cap);
        }
        new (arr + d_size++) T(std::forward<Args>(args)...);
    }

    // destructor
    ~vector() {
        for (auto it = arr; it < arr + d_size; ++it) {
            std::destroy_at(it);
        }
        d_alloc.deallocate(arr, d_capacity);
    }
};

}  // namespace toy
