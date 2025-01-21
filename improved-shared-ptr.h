namespace Toy {

template <class T>
class shared_ptr {
   private:
    // The control block manages both the owned object and reference counts.
    // It ensures the object is deleted exactly when the last strong reference is
    // released.
    class control_block {
        T *ptr;                          // The managed object
        std::atomic<size_t> ref_count;   // Number of shared_ptrs
        std::atomic<size_t> weak_count;  // Number of weak_ptrs

       public:
        // Takes ownership of the pointer. After this, ptr's lifetime is managed by
        // the control block
        explicit control_block(T *p) : ptr(p), ref_count(1), weak_count(0) {
            if (!p)
                throw std::invalid_argument(
                    "Cannot create control block with null pointer");
        }

        // Prevent copying/moving of the control block - it must stay in one place
        control_block(const control_block &) = delete;
        control_block &operator=(const control_block &) = delete;

        T *get() const noexcept { return ptr; }
        size_t use_count() const noexcept { return ref_count; }

        void increment_ref() noexcept { ++ref_count; }

        void increment_weak() noexcept { ++weak_count; }

        // Returns true if the control block should be deleted
        bool decrement_ref() noexcept {
            // If this was the last shared_ptr, delete the managed object
            if (--ref_count == 0) {
                // At this point, we're guaranteed no other shared_ptr can access the
                // object
                delete ptr;
                ptr = nullptr;  // Prevent any accidental use after delete

                // Only delete the control block if there are no weak_ptrs
                return weak_count == 0;
            }
            return false;
        }

        bool decrement_weak() noexcept {
            // Only delete the control block if this was the last weak reference
            // AND there are no more shared_ptrs
            return (--weak_count == 0 && ref_count == 0);
        }

        ~control_block() {
            // By the time we get here, ref_count must be 0 and ptr must be null
            assert(ref_count == 0 &&
                   "Control block destroyed while shared_ptrs exist");
            assert(ptr == nullptr &&
                   "Object should have been deleted when ref_count hit 0");
        }
    };

    control_block *ctrl;  // Our control block pointer, null if empty

   public:
    // Creates an empty shared_ptr
    shared_ptr() noexcept : ctrl(nullptr) {}

    // Takes ownership of a raw pointer
    explicit shared_ptr(T *ptr) {
        try {
            ctrl = ptr ? new control_block(ptr) : nullptr;
        } catch (...) {
            delete ptr;  // If control block creation fails, we must delete ptr
            throw;
        }
    }

    // Move constructor - takes ownership from rhs
    shared_ptr(shared_ptr &&rhs) noexcept : ctrl(rhs.ctrl) {
        rhs.ctrl = nullptr;  // rhs no longer owns anything
    }

    // Move assignment - properly handle current resources before taking ownership
    shared_ptr &operator=(shared_ptr &&rhs) noexcept {
        if (this != &rhs) {
            if (ctrl && ctrl->decrement_ref()) {
                delete ctrl;
            }
            ctrl = rhs.ctrl;
            rhs.ctrl = nullptr;
        }
        return *this;
    }

    // Copy constructor - increment reference count
    shared_ptr(const shared_ptr &rhs) noexcept : ctrl(rhs.ctrl) {
        if (ctrl) {
            ctrl->increment_ref();
        }
    }

    // Copy assignment - properly handle current resources before sharing
    shared_ptr &operator=(const shared_ptr &rhs) noexcept {
        if (this != &rhs) {
            // First clean up our existing resources if any
            if (ctrl && ctrl->decrement_ref()) {
                delete ctrl;
            }
            // Then take shared ownership of rhs's resources
            ctrl = rhs.ctrl;
            if (ctrl) {
                ctrl->increment_ref();
            }
        }
        return *this;
    }

    ~shared_ptr() {
        if (ctrl && ctrl->decrement_ref()) {
            delete ctrl;
        }
    }

    // Accessors and modifiers
    T *get() const noexcept { return ctrl ? ctrl->get() : nullptr; }

    T &operator*() const {
        if (!ctrl || !ctrl->get()) {
            throw std::runtime_error("Dereferencing null shared_ptr");
        }
        return *ctrl->get();
    }

    T *operator->() const noexcept { return get(); }

    size_t use_count() const noexcept { return ctrl ? ctrl->use_count() : 0; }

    explicit operator bool() const noexcept { return get() != nullptr; }

    void reset() noexcept { shared_ptr().swap(*this); }

    void reset(T *ptr) { shared_ptr(ptr).swap(*this); }

    void swap(shared_ptr &rhs) noexcept { std::swap(ctrl, rhs.ctrl); }
};

// Non-member swap function
template <class T>
void swap(shared_ptr<T> &lhs, shared_ptr<T> &rhs) noexcept {
    lhs.swap(rhs);
}

}  // namespace Toy
