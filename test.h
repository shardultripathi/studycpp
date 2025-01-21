#include <iostream>
#include <vector>

class Base {
   public:
    virtual void foo() = 0;
};

struct Test : public Base {
    int val;

    Test() : val(0) {}
    explicit Test(int v) : val(v) {}

    inline void foo() {
        std::cout << "out foo" << std::endl;
    }
    void bar();
};

template <class T, template <class U, class A = std::allocator<U>> class C = std::vector>
struct Adaptor {
    C<T> st;
    void push_back(T elem) { st.push_back(elem); }
};

// void Test::foo() {
//     std::cout << "out foo" << std::endl;
// }