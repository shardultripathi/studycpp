// Type your code here, or load an example.
#include <atomic>
#include <iostream>
#include <mutex>
#include <vector>

template <int...>
struct Vector {};

template <typename, typename>
constexpr int product;

template <int V1, int V2>
constexpr int product<Vector<V1>, Vector<V2>> = V1 * V2;

template <int V1, int... V1s, int V2, int... V2s>
constexpr int product<Vector<V1, V1s...>, Vector<V2, V2s...>> = V1 * V2 + product<Vector<V1s...>, Vector<V2s...>>;

template <int... V1s, int... V2s>
constexpr int product2(Vector<V1s...>, Vector<V2s...>) {
    return (... + (V1s * V2s));
}

template <typename, typename>
struct product3 {
};

template <int H1, int H2>
struct product3<Vector<H1>, Vector<H2>> {
    static constexpr int value = H1 * H2;
};

template <int V1, int... V1s, int V2, int... V2s>
struct product3<Vector<V1, V1s...>, Vector<V2, V2s...>> {
    static constexpr int value = V1 * V2 + product3<Vector<V1s...>, Vector<V2s...>>::value;
};

int fun() {
    constexpr Vector<2, 3> a;
    constexpr Vector<3, 4> b;

    static_assert(product<Vector<2, 3>, Vector<3, 4>> == 18);
    static_assert(product2(a, b) == 18);
    static_assert(product3<Vector<2, 3>, Vector<3, 4>>::value == 18);

    char myChar[1] = {'A'};
    int* newptr = reinterpret_cast<int*>(myChar);
    *newptr = 90;
    std::cout << myChar << std::endl;
}