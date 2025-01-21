#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace toy {

// order: id, price, size, buyorsell, timestamp
// level: price, totalsize, vector<order>
// map: price -> level
// unordered_map: order_id -> order

class DefaultTraits {
   public:
    using price_type = int32_t;
    using size_type = int32_t;
    using ts_type = int64_t;
};

template <typename Traits = toy::DefaultTraits>
class Order {
    int64_t orderId;
    Traits::price_type price;
    Traits::size_type size;
    // Traits::ts_type timestamp;
    bool buy;
};

template <typename Traits>
class OrderBook {
};

}  // namespace toy