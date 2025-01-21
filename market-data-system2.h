#include <memory>
#include <string>
#include <chrono>
#include <atomic>
#include <thread>
#include <queue>
#include <unordered_map>
#include <vector>
#include <deque>
#include <algorithm>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

// TCP Connection handler
class TCPReceiver {
private:
    static constexpr size_t MAX_BUFFER_SIZE = 65536;
    static constexpr int POLL_TIMEOUT_MS = 100;
    
    int socket_fd_{-1};
    std::string host_;
    int port_;
    std::vector<char> receive_buffer_;
    std::atomic<bool> running_{false};
    std::thread receiver_thread_;
    
    MarketDataHandler& message_handler_;
    AlertManager& alert_manager_;
    MetricsCollector& metrics_;
    
    // Connection status
    std::atomic<bool> connected_{false};
    std::chrono::system_clock::time_point last_receive_time_;
    
    // Statistics
    std::atomic<uint64_t> bytes_received_{0};
    std::atomic<uint64_t> packets_received_{0};
    std::atomic<uint64_t> incomplete_messages_{0};

public:
    TCPReceiver(const std::string& host, int port,
                MarketDataHandler& handler,
                AlertManager& alerts,
                MetricsCollector& metrics)
        : host_(host)
        , port_(port)
        , receive_buffer_(MAX_BUFFER_SIZE)
        , message_handler_(handler)
        , alert_manager_(alerts)
        , metrics_(metrics)
    {}
    
    ~TCPReceiver() {
        stop();
    }
    
    bool start() {
        if (running_) return false;
        
        if (!connect()) {
            return false;
        }
        
        running_ = true;
        receiver_thread_ = std::thread([this]() { runReceiveLoop(); });
        return true;
    }
    
    void stop() {
        running_ = false;
        if (receiver_thread_.joinable()) {
            receiver_thread_.join();
        }
        
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        connected_ = false;
    }

private:
    bool connect() {
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            raiseError("Failed to create socket");
            return false;
        }
        
        // Set non-blocking
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
        
        // Set TCP_NODELAY
        int yes = 1;
        setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        
        // Set receive buffer size
        int rcvbuf = 8 * 1024 * 1024; // 8MB
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
            raiseError("Invalid address");
            return false;
        }
        
        if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            if (errno != EINPROGRESS) {
                raiseError("Connect failed");
                return false;
            }
        }
        
        connected_ = true;
        last_receive_time_ = std::chrono::system_clock::now();
        return true;
    }
    
    void runReceiveLoop() {
        struct pollfd pfd{};
        pfd.fd = socket_fd_;
        pfd.events = POLLIN;
        
        std::string message_buffer;
        
        while (running_) {
            int ret = poll(&pfd, 1, POLL_TIMEOUT_MS);
            
            if (ret < 0) {
                if (errno == EINTR) continue;
                raiseError("Poll failed");
                break;
            }
            
            if (ret == 0) {
                // Timeout - check connection health
                checkConnectionHealth();
                continue;
            }
            
            if (pfd.revents & POLLIN) {
                if (!receiveData(message_buffer)) {
                    break;
                }
            }
            
            if (pfd.revents & (POLLERR | POLLHUP)) {
                raiseError("Connection error or hangup");
                break;
            }
        }
        
        reconnect();
    }
    
    bool receiveData(std::string& message_buffer) {
        ssize_t bytes = recv(socket_fd_, receive_buffer_.data(), 
                           receive_buffer_.size(), 0);
        
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            raiseError("Receive failed");
            return false;
        }
        
        if (bytes == 0) {
            raiseError("Connection closed by peer");
            return false;
        }
        
        last_receive_time_ = std::chrono::system_clock::now();
        bytes_received_ += bytes;
        packets_received_++;
        
        // Process received data
        message_buffer.append(receive_buffer_.data(), bytes);
        
        // Find complete messages and process them
        size_t pos = 0;
        while ((pos = message_buffer.find('\n')) != std::string::npos) {
            std::string message = message_buffer.substr(0, pos);
            message_handler_.onMessage(message);
            message_buffer.erase(0, pos + 1);
        }
        
        // Check if message buffer is getting too large
        if (message_buffer.size() > MAX_BUFFER_SIZE) {
            incomplete_messages_++;
            alert_manager_.raiseAlert({
                "Message buffer overflow",
                AlertManager::AlertLevel::WARNING,
                std::chrono::system_clock::now(),
                "TCPReceiver",
                "",
                {{"buffer_size", std::to_string(message_buffer.size())}}
            });
            message_buffer.clear();
        }
        
        return true;
    }
    
    void checkConnectionHealth() {
        auto now = std::chrono::system_clock::now();
        auto silence_duration = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_receive_time_).count();
            
        if (silence_duration > 5) {
            alert_manager_.raiseAlert({
                "No data received for 5 seconds",
                AlertManager::AlertLevel::WARNING,
                now,
                "TCPReceiver",
                "",
                {{"silence_duration", std::to_string(silence_duration)}}
            });
        }
    }
    
    void reconnect() {
        while (running_) {
            close(socket_fd_);
            connected_ = false;
            
            alert_manager_.raiseAlert({
                "Attempting to reconnect",
                AlertManager::AlertLevel::WARNING,
                std::chrono::system_clock::now(),
                "TCPReceiver",
                "",
                {}
            });
            
            if (connect()) {
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    void raiseError(const std::string& message) {
        alert_manager_.raiseAlert({
            message + ": " + strerror(errno),
            AlertManager::AlertLevel::CRITICAL,
            std::chrono::system_clock::now(),
            "TCPReceiver",
            "",
            {{"errno", std::to_string(errno)}}
        });
    }
};

// Market data message types
struct MarketDataMessage {
    enum class Type {
        TRADE,
        QUOTE,
        ORDER_BOOK,
        SYSTEM_STATUS,
        HEARTBEAT
    };

    std::string symbol;
    Type type;
    uint64_t sequence_number;
    std::chrono::system_clock::time_point timestamp;
    std::string raw_message;
};

// Enhanced metrics collection
class MetricsCollector {
private:
    struct TimeWindow {
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        std::vector<uint64_t> latencies;
        uint64_t message_count{0};
        std::unordered_map<MarketDataMessage::Type, uint64_t> message_type_counts;
        std::unordered_map<std::string, uint64_t> symbol_counts;
        uint64_t sequence_gaps{0};
        uint64_t out_of_order_messages{0};
    };

    std::deque<TimeWindow> time_windows_;
    static constexpr size_t MAX_WINDOWS = 60; // 1 minute of history
    std::chrono::seconds window_size_{1};

    // Circular buffer for percentile calculations
    std::vector<uint64_t> latency_buffer_;
    size_t latency_buffer_pos_{0};
    static constexpr size_t LATENCY_BUFFER_SIZE = 10000;

    // Memory metrics
    std::atomic<size_t> memory_used_{0};
    std::atomic<size_t> peak_memory_used_{0};
    
    // Queue metrics
    std::atomic<size_t> queue_size_{0};
    std::atomic<size_t> peak_queue_size_{0};

public:
    void recordLatency(uint64_t latency_micros) {
        latency_buffer_[latency_buffer_pos_] = latency_micros;
        latency_buffer_pos_ = (latency_buffer_pos_ + 1) % LATENCY_BUFFER_SIZE;
        
        auto now = std::chrono::system_clock::now();
        getCurrentWindow(now).latencies.push_back(latency_micros);
    }

    void recordMessage(const MarketDataMessage& msg) {
        auto now = std::chrono::system_clock::now();
        auto& window = getCurrentWindow(now);
        window.message_count++;
        window.message_type_counts[msg.type]++;
        window.symbol_counts[msg.symbol]++;
    }

    void recordSequenceGap() {
        auto now = std::chrono::system_clock::now();
        getCurrentWindow(now).sequence_gaps++;
    }

    void recordOutOfOrderMessage() {
        auto now = std::chrono::system_clock::now();
        getCurrentWindow(now).out_of_order_messages++;
    }

    void recordMemoryUsage(size_t bytes) {
        memory_used_ = bytes;
        peak_memory_used_ = std::max(peak_memory_used_.load(), bytes);
    }

    void recordQueueSize(size_t size) {
        queue_size_ = size;
        peak_queue_size_ = std::max(peak_queue_size_.load(), size);
    }

    // Advanced metric calculations
    struct MetricsSummary {
        double p50_latency;
        double p95_latency;
        double p99_latency;
        double avg_latency;
        uint64_t message_rate;
        uint64_t sequence_gaps;
        uint64_t out_of_order_messages;
        std::unordered_map<MarketDataMessage::Type, uint64_t> message_type_distribution;
        std::unordered_map<std::string, uint64_t> symbol_distribution;
        size_t memory_used;
        size_t peak_memory_used;
        size_t queue_size;
        size_t peak_queue_size;
    };

    MetricsSummary getMetricsSummary() {
        MetricsSummary summary;
        
        // Calculate latency percentiles
        std::vector<uint64_t> sorted_latencies(latency_buffer_.begin(), latency_buffer_.end());
        std::sort(sorted_latencies.begin(), sorted_latencies.end());
        
        summary.p50_latency = sorted_latencies[LATENCY_BUFFER_SIZE * 50 / 100];
        summary.p95_latency = sorted_latencies[LATENCY_BUFFER_SIZE * 95 / 100];
        summary.p99_latency = sorted_latencies[LATENCY_BUFFER_SIZE * 99 / 100];
        
        // Calculate other metrics
        uint64_t total_messages = 0;
        uint64_t total_sequence_gaps = 0;
        uint64_t total_out_of_order = 0;
        
        for (const auto& window : time_windows_) {
            total_messages += window.message_count;
            total_sequence_gaps += window.sequence_gaps;
            total_out_of_order += window.out_of_order_messages;
            
            // Aggregate message type counts
            for (const auto& [type, count] : window.message_type_counts) {
                summary.message_type_distribution[type] += count;
            }
            
            // Aggregate symbol counts
            for (const auto& [symbol, count] : window.symbol_counts) {
                summary.symbol_distribution[symbol] += count;
            }
        }
        
        summary.message_rate = total_messages / time_windows_.size();
        summary.sequence_gaps = total_sequence_gaps;
        summary.out_of_order_messages = total_out_of_order;
        summary.memory_used = memory_used_;
        summary.peak_memory_used = peak_memory_used_;
        summary.queue_size = queue_size_;
        summary.peak_queue_size = peak_queue_size_;
        
        return summary;
    }

private:
    TimeWindow& getCurrentWindow(std::chrono::system_clock::time_point now) {
        if (time_windows_.empty() || 
            now - time_windows_.back().start_time >= window_size_) {
            // Create new window
            while (time_windows_.size() >= MAX_WINDOWS) {
                time_windows_.pop_front();
            }
            
            TimeWindow new_window;
            new_window.start_time = now;
            new_window.end_time = now + window_size_;
            time_windows_.push_back(std::move(new_window));
        }
        return time_windows_.back();
    }
};

// Enhanced alert manager
class AlertManager {
public:
    enum class AlertLevel {
        INFO,
        WARNING,
        CRITICAL
    };

    struct Alert {
        std::string message;
        AlertLevel level;
        std::chrono::system_clock::time_point timestamp;
        std::string source;
        std::string symbol;
        std::unordered_map<std::string, std::string> additional_info;
    };

private:
    std::queue<Alert> alert_queue_;
    std::atomic<size_t> active_alerts_{0};
    std::unordered_map<std::string, std::chrono::system_clock::time_point> alert_cooldowns_;
    static constexpr auto COOLDOWN_PERIOD = std::chrono::seconds(60);
    
    // Alert thresholds
    struct Thresholds {
        uint64_t latency_warning_micros{100};
        uint64_t latency_critical_micros{1000};
        uint64_t min_message_rate{1000};
        uint64_t max_message_rate{100000};
        size_t max_queue_size{10000};
        uint64_t max_sequence_gaps{10};
        uint64_t max_out_of_order{100};
    } thresholds_;

public:
    void raiseAlert(const Alert& alert) {
        // Check cooldown
        auto now = std::chrono::system_clock::now();
        auto cooldown_key = alert.source + alert.message;
        
        auto it = alert_cooldowns_.find(cooldown_key);
        if (it != alert_cooldowns_.end() && 
            now - it->second < COOLDOWN_PERIOD) {
            return; // Alert in cooldown
        }
        
        alert_cooldowns_[cooldown_key] = now;
        alert_queue_.push(alert);
        active_alerts_++;
        
        // Log or forward alert based on level
        processAlert(alert);
    }

    void checkMetrics(const MetricsCollector::MetricsSummary& metrics) {
        auto now = std::chrono::system_clock::now();
        
        // Check latency thresholds
        if (metrics.p99_latency > thresholds_.latency_critical_micros) {
            raiseAlert({
                "Critical latency threshold exceeded",
                AlertLevel::CRITICAL,
                now,
                "LatencyMonitor",
                "",
                {{"p99_latency", std::to_string(metrics.p99_latency)}}
            });
        } else if (metrics.p95_latency > thresholds_.latency_warning_micros) {
            raiseAlert({
                "Warning latency threshold exceeded",
                AlertLevel::WARNING,
                now,
                "LatencyMonitor",
                "",
                {{"p95_latency", std::to_string(metrics.p95_latency)}}
            });
        }
        
        // Check message rate
        if (metrics.message_rate < thresholds_.min_message_rate) {
            raiseAlert({
                "Message rate below minimum threshold",
                AlertLevel::WARNING,
                now,
                "MessageRateMonitor",
                "",
                {{"rate", std::to_string(metrics.message_rate)}}
            });
        }
        
        // Check sequence gaps
        if (metrics.sequence_gaps > thresholds_.max_sequence_gaps) {
            raiseAlert({
                "Excessive sequence gaps detected",
                AlertLevel::CRITICAL,
                now,
                "SequenceMonitor",
                "",
                {{"gaps", std::to_string(metrics.sequence_gaps)}}
            });
        }
        
        // Check queue size
        if (metrics.queue_size > thresholds_.max_queue_size) {
            raiseAlert({
                "Processing queue size exceeded threshold",
                AlertLevel::WARNING,
                now,
                "QueueMonitor",
                "",
                {{"size", std::to_string(metrics.queue_size)}}
            });
        }
    }

private:
    void processAlert(const Alert& alert) {
        std::stringstream ss;
        ss << "[" << std::chrono::system_clock::to_time_t(alert.timestamp)
           << "][" << toString(alert.level) << "] "
           << alert.source << ": " << alert.message;
        
        if (!alert.symbol.empty()) {
            ss << " (Symbol: " << alert.symbol << ")";
        }
        
        for (const auto& [key, value] : alert.additional_info) {
            ss << "\n  " << key << ": " << value;
        }
        
        // In real system, would send to logging/monitoring service
        std::cerr << ss.str() << std::endl;
    }

    static const char* toString(AlertLevel level) {
        switch (level) {
            case AlertLevel::INFO: return "INFO";
            case AlertLevel::WARNING: return "WARNING";
            case AlertLevel::CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }
};

// Market data message parser
class MessageParser {
public:
    static MarketDataMessage parse(const std::string& raw_message) {
        MarketDataMessage msg;
        msg.raw_message = raw_message;
        msg.timestamp = std::chrono::system_clock::now();
        
        // In real implementation, would parse based on protocol
        // Example parsing logic:
        if (raw_message.find("TRADE") != std::string::npos) {
            msg.type = MarketDataMessage::Type::TRADE;
        } else if (raw_message.find("QUOTE") != std::string::npos) {
            msg.type = MarketDataMessage::Type::QUOTE;
        } else if (raw_message.find("BOOK") != std::string::npos) {
            msg.type = MarketDataMessage::Type::ORDER_BOOK;
        } else if (raw_message.find("STATUS") != std::string::npos) {
            msg.type = MarketDataMessage::Type::SYSTEM_STATUS;
        } else {
            msg.type = MarketDataMessage::Type::HEARTBEAT;
        }
        
        // Extract symbol and sequence number
        // This is placeholder logic - real implementation would depend on protocol
        size_t sym_pos = raw_message.find("symbol=");
        if (sym_pos != std::string::npos) {
            msg.symbol = raw_message.substr(sym_pos + 7, 4);
        }
        
        size_t seq_pos = raw_message.find("seq=");
        if (seq_pos != std::string::npos) {
            msg.sequence_number = std::stoull(raw_message.substr(seq_pos + 4, 10));
        }
        
        return msg;
    }
};

// Main market data processor
class MarketDataProcessor : public MarketDataHandler {
private:
    MetricsCollector metrics_;
    AlertManager alerts_;
    std::thread monitoring_thread_;
    std::atomic<bool> running_{false};
    std::queue<MarketDataMessage> message_queue_;
    std::unordered_map<std::string, uint64_t> last_sequence_numbers_;
    std::unordered_map<std::string, double> latest_prices_;

    static constexpr auto MONITORING_INTERVAL = std::chrono::seconds(1);

public:
    MarketDataProcessor() {
        start();
    }

    ~MarketDataProcessor() {
        stop();
    }

    void onMessage(const std::string& raw_message) override {
        auto msg = MessageParser::parse(raw_message);
        auto now = std::chrono::system_clock::now();
        
        // Record basic metrics
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            now - msg.timestamp
        ).count();
        metrics_.recordLatency(latency);
        metrics_.recordMessage(msg);
        
        // Check sequence numbers
        auto it = last_sequence_numbers_.find(msg.symbol);
        if (it != last_sequence_numbers_.end()) {
            if (msg.sequence_number <= it->second) {
                metrics_.recordOutOfOrderMessage();
            } else if (msg.sequence_number > it->second + 1) {
                metrics_.recordSequenceGap();
            }
        }
        last_sequence_numbers_[msg.symbol] = msg.sequence_number;
        
        // Update queue metrics
        message_queue_.push(std::move(msg));
        metrics_.recordQueueSize(message_queue_.size());
        
        // Process the message
        processNextMessage();
    }

private:
    void start() {
        running_ = true;
        monitoring_thread_ = std::thread([this]() {
            while (running_) {
                // Get metrics summary and check alerts
                auto metrics_summary = metrics_.getMetricsSummary();
                alerts_.checkMetrics(metrics_summary);
                
                // Log periodic statistics
                logStatistics(metrics_summary);
                
                std::this_thread::sleep_for(MONITORING_INTERVAL);
            }
        });
    }

    void stop() {
        running_ = false;
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
    }

    void processNextMessage() {
        while (!message_queue_.empty()) {
            auto& msg = message_queue_.front();
            
            switch (msg.type) {
                case MarketDataMessage::Type::TRADE:
                    processTrade(msg);
                    break;
                case MarketDataMessage::Type::QUOTE:
                    processQuote(msg);
                    break;
                case MarketDataMessage::Type::ORDER_BOOK:
                    processOrderBook(msg);
                    break;
                case MarketDataMessage::Type::SYSTEM_STATUS:
                    processSystemStatus(msg);
                    break;
                case MarketDataMessage::Type::HEARTBEAT:
                    processHeartbeat(msg);
                    break;
            }
            
            message_queue_.pop();
            metrics_.recordQueueSize(message_queue_.size());
        }
    }

    void processTrade(const MarketDataMessage& msg) {
        // Example trade processing
        // In real implementation, would parse price and quantity
        // Update latest price
        // Update VWAP calculations
        // Update trading statistics
        
        AlertManager::Alert alert{
            "New trade processed",
            AlertManager::AlertLevel::INFO,
            std::chrono::system_clock::now(),
            "TradeProcessor",
            msg.symbol,
            {{"sequence", std::to_string(msg.sequence_number)}}
        };
        alerts_.raiseAlert(alert);
    }

    void processQuote(const MarketDataMessage& msg) {
        // Example quote processing
        // Update bid/ask prices
        // Update spread statistics
        // Check for crossed markets
        
        AlertManager::Alert alert{
            "Quote processed",
            AlertManager::AlertLevel::INFO,
            std::chrono::system_clock::now(),
            "QuoteProcessor",
            msg.symbol,
            {{"sequence", std::to_string(msg.sequence_number)}}
        };
        alerts_.raiseAlert(alert);
    }

    void processOrderBook(const MarketDataMessage& msg) {
        // Example order book processing
        // Update price levels
        // Calculate book depth
        // Check for price inversions
        
        AlertManager::Alert alert{
            "Order book update processed",
            AlertManager::AlertLevel::INFO,
            std::chrono::system_clock::now(),
            "OrderBookProcessor",
            msg.symbol,
            {{"sequence", std::to_string(msg.sequence_number)}}
        };
        alerts_.raiseAlert(alert);
    }

    void processSystemStatus(const MarketDataMessage& msg) {
        // Example system status processing
        // Check exchange status
        // Handle trading halts
        // Update system state
        
        AlertManager::Alert alert{
            "System status update received",
            AlertManager::AlertLevel::INFO,
            std::chrono::system_clock::now(),
            "SystemStatusProcessor",
            msg.symbol,
            {{"sequence", std::to_string(msg.sequence_number)}}
        };
        alerts_.raiseAlert(alert);
    }

    void processHeartbeat(const MarketDataMessage& msg) {
        // Example heartbeat processing
        // Update connection status
        // Check message gaps
        // Monitor latency
        
        AlertManager::Alert alert{
            "Heartbeat received",
            AlertManager::AlertLevel::INFO,
            std::chrono::system_clock::now(),
            "HeartbeatProcessor",
            msg.symbol,
            {{"sequence", std::to_string(msg.sequence_number)}}
        };
        alerts_.raiseAlert(alert);
    }

    void logStatistics(const MetricsCollector::MetricsSummary& metrics) {
        std::stringstream ss;
        ss << "\n=== Market Data Processing Statistics ===\n"
           << "Latency (microseconds):\n"
           << "  P50: " << metrics.p50_latency << "\n"
           << "  P95: " << metrics.p95_latency << "\n"
           << "  P99: " << metrics.p99_latency << "\n"
           << "Message Rate: " << metrics.message_rate << " msg/s\n"
           << "Sequence Gaps: " << metrics.sequence_gaps << "\n"
           << "Out of Order Messages: " << metrics.out_of_order_messages << "\n"
           << "Queue Size: " << metrics.queue_size << " (Peak: " << metrics.peak_queue_size << ")\n"
           << "Memory Used: " << metrics.memory_used / 1024 << "KB (Peak: " << metrics.peak_memory_used / 1024 << "KB)\n"
           << "\nMessage Type Distribution:\n";
        
        for (const auto& [type, count] : metrics.message_type_distribution) {
            ss << "  " << static_cast<int>(type) << ": " << count << " messages\n";
        }
        
        // In real system, would send to logging service
        std::cerr << ss.str() << std::endl;
    }
};