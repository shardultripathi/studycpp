#include <chrono>
#include <iostream>
#include <list>
#include <thread>
#include <vector>

#include "mypointer.h"
#include "myvector.h"
#include "test.h"
#include "threadpool.h"
#include "variant.h"

std::atomic<int> count = 0;

int add(int a, int b) {
    ++count;
    return a + b;
}

int main() {
    Test t1;
    t1.foo();
    t1.bar();

    Adaptor<int> adaptor;
    Adaptor<int, std::list> adaptor2;

    toy::shared_ptr<int> ptr_sp(new int(5));
    *ptr_sp = 10;
    *ptr_sp.get() = 11;
    ptr_sp = std::move(ptr_sp);

    toy::shared_ptr<int> nullp;
    toy::unique_ptr<int> unq(new int(7));

    toy::vector<int> vec;
    vec.reserve(5);
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    auto newvec = std::move(vec);
    for (int i = 0; i < newvec.size(); i++) {
        std::cout << newvec[i] << std::endl;
    }
    toy::vector<Test, std::allocator<Test>> testvec;
    for (int i = 0; i < newvec.size(); i++) {
        testvec.emplace_back(newvec[i]);
        std::cout << testvec[i].val << std::endl;
        testvec[i].bar();
    }

    constexpr int num_workers = 5;
    toy::threadpool pool(num_workers);
    for (int i = 1; i < 100; ++i) {
        for (int j = 1; j < 100; ++j) {
            int res = pool.enqueue(&add, i, j).get();
            std::cout << res << std::endl;
        }
    }
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(2s);
    std::cout << "enqueue counter: " << count << std::endl;

    toy::variant<int, float, std::string> v;
    v = 1123;
    std::cout << v.get<0>() << std::endl;
    v = 3.14f;
    std::cout << v.get<1>() << std::endl;
}
