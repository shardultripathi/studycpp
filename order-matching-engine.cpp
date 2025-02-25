#include <iostream>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <functional>
#include <vector>
#include <memory>
#include <limits>

// Forward declarations
class Order;
class OrderBook;
class MatchingEngine;

// Trade notification callback type
using TradeNotifierCallback = std::function<void(const std::string& symbol, double price, int size, 
                                               const std::string& buyOrderId, const std::string& sellOrderId)>;

// Order class represents a single order in the system
class Order {
public:
    enum class Side { BUY, SELL };

    Order(const std::string& id, const std::string& symbol, double price, int size, Side side)
        : id_(id), symbol_(symbol), price_(price), remainingSize_(size), side_(side) {}

    const std::string& getId() const { return id_; }
    const std::string& getSymbol() const { return symbol_; }
    double getPrice() const { return price_; }
    int getRemainingSize() const { return remainingSize_; }
    Side getSide() const { return side_; }
    
    bool reduceSize(int size) {
        if (size > remainingSize_) return false;
        remainingSize_ -= size;
        return true;
    }

    bool isFilled() const { return remainingSize_ == 0; }

private:
    std::string id_;
    std::string symbol_;
    double price_;
    int remainingSize_;
    Side side_;
};

// Price-time priority comparison for buy orders (highest price first)
struct BuyOrderComparator {
    bool operator()(const std::shared_ptr<Order>& lhs, const std::shared_ptr<Order>& rhs) const {
        if (lhs->getPrice() != rhs->getPrice()) {
            return lhs->getPrice() > rhs->getPrice(); // Higher prices have priority for buy orders
        }
        return lhs->getId() < rhs->getId(); // Earlier orders have priority
    }
};

// Price-time priority comparison for sell orders (lowest price first)
struct SellOrderComparator {
    bool operator()(const std::shared_ptr<Order>& lhs, const std::shared_ptr<Order>& rhs) const {
        if (lhs->getPrice() != rhs->getPrice()) {
            return lhs->getPrice() < rhs->getPrice(); // Lower prices have priority for sell orders
        }
        return lhs->getId() < rhs->getId(); // Earlier orders have priority
    }
};

// OrderBook maintains buy and sell orders for a single symbol
class OrderBook {
public:
    explicit OrderBook(const std::string& symbol) : symbol_(symbol) {}

    // Add an order to the book
    void addOrder(std::shared_ptr<Order> order) {
        orders_[order->getId()] = order;
        
        if (order->getSide() == Order::Side::BUY) {
            buyOrders_.insert(order);
        } else {
            sellOrders_.insert(order);
        }
    }

    // Remove an order from the book
    bool removeOrder(const std::string& orderId) {
        auto it = orders_.find(orderId);
        if (it == orders_.end()) {
            return false;
        }

        auto order = it->second;
        if (order->getSide() == Order::Side::BUY) {
            buyOrders_.erase(order);
        } else {
            sellOrders_.erase(order);
        }

        orders_.erase(it);
        return true;
    }

    // Try to match incoming order with existing orders
    void matchOrder(std::shared_ptr<Order> incomingOrder, TradeNotifierCallback notifier) {
        if (incomingOrder->getSide() == Order::Side::BUY) {
            matchBuyOrder(incomingOrder, notifier);
        } else {
            matchSellOrder(incomingOrder, notifier);
        }

        // If order isn't fully filled, add it to the book
        if (!incomingOrder->isFilled()) {
            addOrder(incomingOrder);
        }
    }

    // Get current best bid (highest buy price)
    double getBestBid() const {
        if (buyOrders_.empty()) {
            return 0.0;
        }
        return (*buyOrders_.begin())->getPrice();
    }

    // Get current best ask (lowest sell price)
    double getBestAsk() const {
        if (sellOrders_.empty()) {
            return std::numeric_limits<double>::max();
        }
        return (*sellOrders_.begin())->getPrice();
    }

    // Get current spread
    double getSpread() const {
        if (buyOrders_.empty() || sellOrders_.empty()) {
            return std::numeric_limits<double>::max();
        }
        return getBestAsk() - getBestBid();
    }

private:
    std::string symbol_;
    std::unordered_map<std::string, std::shared_ptr<Order>> orders_;
    std::set<std::shared_ptr<Order>, BuyOrderComparator> buyOrders_;
    std::set<std::shared_ptr<Order>, SellOrderComparator> sellOrders_;

    // Match incoming buy order with existing sell orders
    void matchBuyOrder(std::shared_ptr<Order> buyOrder, TradeNotifierCallback notifier) {
        // Keep matching until the order is filled or no matching sell orders exist
        while (!buyOrder->isFilled() && !sellOrders_.empty()) {
            auto bestSell = *sellOrders_.begin();
            
            // If the best sell price is higher than the buy price, we can't match
            if (bestSell->getPrice() > buyOrder->getPrice()) {
                break;
            }

            // Determine the trade size (minimum of remaining sizes)
            int tradeSize = std::min(buyOrder->getRemainingSize(), bestSell->getRemainingSize());
            
            // Execute the trade
            double tradePrice = bestSell->getPrice(); // Match at the resting order's price
            
            // Remove the sell order from the set before modifying it
            sellOrders_.erase(sellOrders_.begin());
            
            // Update order sizes
            buyOrder->reduceSize(tradeSize);
            bestSell->reduceSize(tradeSize);
            
            // Notify about the trade
            if (notifier) {
                notifier(symbol_, tradePrice, tradeSize, buyOrder->getId(), bestSell->getId());
            }
            
            // If the sell order still has remaining size, add it back to the set
            if (!bestSell->isFilled()) {
                sellOrders_.insert(bestSell);
            } else {
                orders_.erase(bestSell->getId());
            }
        }
    }

    // Match incoming sell order with existing buy orders
    void matchSellOrder(std::shared_ptr<Order> sellOrder, TradeNotifierCallback notifier) {
        // Keep matching until the order is filled or no matching buy orders exist
        while (!sellOrder->isFilled() && !buyOrders_.empty()) {
            auto bestBuy = *buyOrders_.begin();
            
            // If the best buy price is lower than the sell price, we can't match
            if (bestBuy->getPrice() < sellOrder->getPrice()) {
                break;
            }

            // Determine the trade size
            int tradeSize = std::min(sellOrder->getRemainingSize(), bestBuy->getRemainingSize());
            
            // Execute the trade
            double tradePrice = bestBuy->getPrice(); // Match at the resting order's price
            
            // Remove the buy order from the set before modifying it
            buyOrders_.erase(buyOrders_.begin());
            
            // Update order sizes
            sellOrder->reduceSize(tradeSize);
            bestBuy->reduceSize(tradeSize);
            
            // Notify about the trade
            if (notifier) {
                notifier(symbol_, tradePrice, tradeSize, bestBuy->getId(), sellOrder->getId());
            }
            
            // If the buy order still has remaining size, add it back to the set
            if (!bestBuy->isFilled()) {
                buyOrders_.insert(bestBuy);
            } else {
                orders_.erase(bestBuy->getId());
            }
        }
    }
};

// MatchingEngine manages multiple order books and provides the main API
class MatchingEngine {
public:
    MatchingEngine() : nextOrderId_(1) {}

    // Set the trade notifier callback
    void setTradeNotifier(TradeNotifierCallback notifier) {
        tradeNotifier_ = notifier;
    }

    // Place a new order
    std::string placeOrder(const std::string& symbol, double price, int size, bool isBuy) {
        // Generate a unique order ID
        std::string orderId = generateOrderId();
        
        // Create the order object
        auto order = std::make_shared<Order>(
            orderId, 
            symbol, 
            price, 
            size, 
            isBuy ? Order::Side::BUY : Order::Side::SELL
        );

        // Get or create the order book for this symbol
        auto& orderBook = getOrderBook(symbol);
        
        // Try to match the order immediately
        orderBook.matchOrder(order, tradeNotifier_);
        
        return orderId;
    }

    // Cancel an existing order
    bool cancelOrder(const std::string& orderId) {
        for (auto& [symbol, orderBook] : orderBooks_) {
            if (orderBook.removeOrder(orderId)) {
                return true;
            }
        }
        return false;
    }

    // Get market data for a symbol
    struct MarketData {
        double bestBid;
        double bestAsk;
        double spread;
    };

    MarketData getMarketData(const std::string& symbol) {
        auto& orderBook = getOrderBook(symbol);
        return {
            orderBook.getBestBid(),
            orderBook.getBestAsk(),
            orderBook.getSpread()
        };
    }

private:
    std::unordered_map<std::string, OrderBook> orderBooks_;
    TradeNotifierCallback tradeNotifier_;
    int nextOrderId_;

    // Get or create an order book for a symbol
    OrderBook& getOrderBook(const std::string& symbol) {
        auto it = orderBooks_.find(symbol);
        if (it == orderBooks_.end()) {
            it = orderBooks_.emplace(symbol, OrderBook(symbol)).first;
        }
        return it->second;
    }

    // Generate a unique order ID
    std::string generateOrderId() {
        return "order-" + std::to_string(nextOrderId_++);
    }
};

// Example usage
int main() {
    // Create a matching engine
    MatchingEngine engine;
    
    // Set up a trade notifier
    engine.setTradeNotifier([](const std::string& symbol, double price, int size, 
                              const std::string& buyOrderId, const std::string& sellOrderId) {
        std::cout << "TRADE: " << symbol << " - " << size << " shares at $" << price 
                  << " (Buy: " << buyOrderId << ", Sell: " << sellOrderId << ")" << std::endl;
    });
    
    // Place some orders
    std::string order1 = engine.placeOrder("AAPL", 150.0, 100, true);  // Buy 100 AAPL at $150
    std::string order2 = engine.placeOrder("AAPL", 151.0, 50, true);   // Buy 50 AAPL at $151
    std::string order3 = engine.placeOrder("AAPL", 149.0, 75, false);  // Sell 75 AAPL at $149
    
    // This sell should match with our buy orders
    std::string order4 = engine.placeOrder("AAPL", 150.0, 200, false); // Sell 200 AAPL at $150
    
    // Cancel an order
    engine.cancelOrder(order1);
    
    // Get market data
    auto marketData = engine.getMarketData("AAPL");
    std::cout << "AAPL - Best Bid: $" << marketData.bestBid 
              << ", Best Ask: $" << marketData.bestAsk 
              << ", Spread: $" << marketData.spread << std::endl;
    
    return 0;
}
