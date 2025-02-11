namespace Toy {

template <class T>
class shared_ptr {
   private:
    class control_block_base {
       protected:
        T* ptr;
        std::atomic<size_t> ref_count;
        std::atomic<size_t> weak_count;

       public:
        explicit control_block_base(T* p)
            : ptr(p), ref_count(1), weak_count(0) {}

        // Delete all copy/move operations
        control_block_base(const control_block_base&) = delete;
        control_block_base& operator=(const control_block_base&) = delete;
        control_block_base(control_block_base&&) = delete;
        control_block_base& operator=(control_block_base&&) = delete;

        virtual ~control_block_base() {
            assert(ref_count == 0 && "Control block destroyed while shared_ptrs exist");
            assert(ptr == nullptr && "Object should have been deleted when ref_count hit 0");
        }

        virtual void destroy() noexcept = 0;

        T* get() const noexcept { return ptr; }
        size_t use_count() const noexcept { return ref_count; }

        void increment_ref() noexcept { ++ref_count; }
        void increment_weak() noexcept { ++weak_count; }

        bool decrement_ref() noexcept {
            if (--ref_count == 0) {
                destroy();
                ptr = nullptr;
                return weak_count == 0;
            }
            return false;
        }

        bool decrement_weak() noexcept {
            return (--weak_count == 0 && ref_count == 0);
        }
    };

    class default_control_block : public control_block_base {
       public:
        explicit default_control_block(T* p) : control_block_base(p) {}

        void destroy() noexcept override {
            delete this->ptr;
        }
    };

    template <typename Deleter>
    class control_block_with_deleter : public control_block_base {
        Deleter deleter;

       public:
        control_block_with_deleter(T* p, Deleter d)
            : control_block_base(p), deleter(std::move(d)) {}

        void destroy() noexcept override {
            deleter(this->ptr);
        }
    };

    control_block_base* ctrl;

   public:
    shared_ptr() noexcept : ctrl(nullptr) {}

    explicit shared_ptr(T* ptr) {
        try {
            ctrl = ptr ? new default_control_block(ptr) : nullptr;
        } catch (...) {
            delete ptr;
            throw;
        }
    }

    template <typename Deleter>
    shared_ptr(T* ptr, Deleter d) {
        try {
            ctrl = ptr ? new control_block_with_deleter<Deleter>(ptr, std::move(d)) : nullptr;
        } catch (...) {
            d(ptr);
            throw;
        }
    }

    shared_ptr(shared_ptr&& rhs) noexcept : ctrl(rhs.ctrl) {
        rhs.ctrl = nullptr;
    }

    shared_ptr& operator=(shared_ptr&& rhs) noexcept {
        if (this != &rhs) {
            if (ctrl && ctrl->decrement_ref()) {
                delete ctrl;
            }
            ctrl = rhs.ctrl;
            rhs.ctrl = nullptr;
        }
        return *this;
    }

    shared_ptr(const shared_ptr& rhs) noexcept : ctrl(rhs.ctrl) {
        if (ctrl) ctrl->increment_ref();
    }

    shared_ptr& operator=(const shared_ptr& rhs) noexcept {
        if (this != &rhs) {
            if (ctrl && ctrl->decrement_ref()) {
                delete ctrl;
            }
            ctrl = rhs.ctrl;
            if (ctrl) ctrl->increment_ref();
        }
        return *this;
    }

    ~shared_ptr() {
        if (ctrl && ctrl->decrement_ref()) {
            delete ctrl;
        }
    }

    T* get() const noexcept { return ctrl ? ctrl->get() : nullptr; }

    T& operator*() const {
        if (!ctrl || !ctrl->get()) throw std::runtime_error("Dereferencing null shared_ptr");
        return *ctrl->get();
    }

    T* operator->() const noexcept { return get(); }

    size_t use_count() const noexcept { return ctrl ? ctrl->use_count() : 0; }

    explicit operator bool() const noexcept { return get() != nullptr; }

    void reset() noexcept {
        shared_ptr().swap(*this);
    }

    template <typename Deleter>
    void reset(T* ptr, Deleter d) {
        shared_ptr(ptr, std::move(d)).swap(*this);
    }

    void reset(T* ptr) {
        shared_ptr(ptr).swap(*this);
    }

    void swap(shared_ptr& rhs) noexcept {
        std::swap(ctrl, rhs.ctrl);
    }
};

template <class T>
void swap(shared_ptr<T>& lhs, shared_ptr<T>& rhs) noexcept {
    lhs.swap(rhs);
}

}  // namespace Toy
