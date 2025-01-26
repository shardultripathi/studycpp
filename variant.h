#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace toy {

template <typename T, typename... Types>
concept IsAnyOf = (std::is_same_v<T, Types> || ...);

template <typename... Types>
class variant {
    template <size_t I>
    using type_at = std::tuple_element_t<I, std::tuple<Types...>>;

    static constexpr size_t storage_size = std::max({sizeof(Types)...});
    static constexpr size_t align_size = std::max({alignof(Types)...});

   public:
    variant() = default;
    ~variant() { destroy(); }

    template <typename T>
    variant(T&& value) {
        assign(std::forward<T>(value));
    }

    template <typename T>
    variant& operator=(T&& t) noexcept {
        assign(std::forward<T>(t));
        return *this;
    }

    size_t index() const noexcept { return type_index_; }

    template <size_t I>
    auto& get() {
        static_assert(I < sizeof...(Types), "Index out of bounds");
        if (type_index_ != I) {
            throw std::bad_variant_access{};
        }
        return *std::launder(reinterpret_cast<type_at<I>*>(storage_));
    }

   private:
    using destructor_fn = void (*)(void*);

    template <typename T>
    static void destroy_impl(void* ptr) {
        static_cast<T*>(ptr)->~T();
    }

    static constexpr destructor_fn destructor_table[] = {
        &destroy_impl<Types>...};

    void destroy() {
        if (type_index_ != std::variant_npos) {
            destructor_table[type_index_](storage_);
            type_index_ = std::variant_npos;
        }
    }

    template <typename T>
    void assign(T&& value) {
        static_assert(IsAnyOf<T, Types...>, "Type not in variant");
        destroy();  // Clean up existing value
        type_index_ = index_of<T>();
        T* ptr = std::launder(reinterpret_cast<T*>(storage_));
        ::new (ptr) T(std::forward<T>(value));
    }

    template <typename T>
    static constexpr size_t index_of() {
        size_t idx = 0;
        constexpr bool found = ((std::is_same_v<T, Types> ? true : (++idx, false)) || ...);
        static_assert(found, "Type not in variant");
        return idx;
    }

    alignas(align_size) std::byte storage_[storage_size];
    size_t type_index_ = std::variant_npos;
};

}  // namespace toy