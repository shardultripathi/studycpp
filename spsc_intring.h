#include <stdio.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>
class BoundedBlockingQueue {
    std::vector<int> ring_;
    int capacity_;
    alignas(64) std::atomic<int> consumer_, producer_;

    bool isEmpty(const int producer, const int consumer) {
        return consumer == producer;
    }
    bool isFull(const int producer, const int consumer) {
        return producer - consumer >= capacity_;
    }
    int index(const int pos) {
        return (pos % capacity_);
    }

   public:
    BoundedBlockingQueue(int capacity) : capacity_(capacity),
                                         producer_(0),
                                         consumer_(0) {
        ring_.resize(capacity_);
    }

    void enqueue(int element) {
        int producer = producer_.load(std::memory_order_relaxed);
        int consumer = consumer_.load(std::memory_order_acquire);
        while (isFull(producer, consumer)) {
            producer = producer_.load(std::memory_order_relaxed);
            consumer = consumer_.load(std::memory_order_acquire);
        }
        ring_[index(producer)] = element;
        producer_.store(producer + 1, std::memory_order_release);
    }

    int dequeue() {
        int consumer = consumer_.load(std::memory_order_relaxed);
        int producer = producer_.load(std::memory_order_acquire);
        while (isEmpty(producer, consumer)) {
            consumer = consumer_.load(std::memory_order_relaxed);
            producer = producer_.load(std::memory_order_acquire);
        }
        int ret = ring_[index(consumer)];
        consumer_.store(consumer + 1, std::memory_order_release);
        return ret;
    }

    int size() {
        int producer = producer_.load(std::memory_order_acquire);
        int consumer = consumer_.load(std::memory_order_acquire);
        return producer - consumer;
    }
};