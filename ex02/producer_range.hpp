// Copyright (c) 2025 Peter Tran. All rights reserved.
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>

template<typename T>
using write_gate_func = std::function<bool(const std::queue<T>& q)>;

/// An input range that allows items to be appended for producer-consumer use cases.
/// The producer uses `add()` to add items to the range.
/// The consumer uses the normal `std::ranges` range adaptors.
/// Internally items are buffered in a thread-safe blocking queue.
template<typename T>
class producer_range {
private:
    mutable std::queue<T> buffer_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable bool finished_ = false;
    write_gate_func<T> write_gate_;

public:
    producer_range(write_gate_func<T> write_gate) {
        write_gate_ = write_gate;
    }
    ~producer_range() = default;

    template<typename Item = T>
    void add(Item&& item) {
        auto lock = std::lock_guard(mutex_);
        if (!finished_) {
            buffer_.push(std::forward<Item>(item));
        }
        cv_.notify_all();
    }

    class iterator {
    private:
        const producer_range* parent_;
        std::optional<T> current_;

    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&&;

        iterator() = default;

        explicit iterator(const producer_range* parent)
            : parent_(parent) {
            ++(*this); // Load first item
        }

        // Copy/move constructors and assignment operators
        iterator(const iterator&) = default;
        iterator(iterator&&) = default;
        iterator& operator=(const iterator&) = default;
        iterator& operator=(iterator&&) = default;

        // Destructor
        ~iterator() = default;

        iterator& operator++() {
            auto lock = std::unique_lock(parent_->mutex_);
            
            // Wait for data
            parent_->cv_.wait(lock,
                [this] { return !parent_->buffer_.empty() || parent_->finished_; });
                
            if (!parent_->buffer_.empty()) {
                current_ = std::move(parent_->buffer_.front());
                parent_->buffer_.pop();
                parent_->cv_.notify_all();
            } else {
                current_.reset();
            }
            
            return *this;
        }

        iterator operator++(int) {
            iterator temp = std::move(*this);
            ++(*this);
            return temp;
        }

        const T& operator*() const {
            return *current_;
        }

        bool operator==(std::default_sentinel_t) const {
            auto lock = std::unique_lock(parent_->mutex_);
            return parent_->finished_;
        }

        // Equality operators (required for incrementable via weakly_equality_comparable)
        friend bool operator==(const iterator& lhs, const iterator& rhs) {

            return lhs.parent_ == rhs.parent_ &&
                   (!lhs.current_ && !rhs.current_) || *lhs.current_ == *rhs.current_;
        }
    };

    iterator begin() const {
        return iterator(this);
    }

    std::default_sentinel_t end() const {
        return std::default_sentinel;
    }

    void finish() const {
        auto lock = std::lock_guard(mutex_);
        finished_ = true;
        cv_.notify_all();
    }

    stdexec::sender auto async_write_gate() {
        auto lock = std::unique_lock(mutex_);
        // Wait for gate to open
        cv_.wait(lock, [this] { return write_gate_(buffer_) || finished_; });
        return stdexec::just();
    }
};

// Make it satisfy the range concept
template<typename T>
inline constexpr bool std::ranges::enable_borrowed_range<producer_range<T>> = false;

// Helper function to create the range
template<typename T>
auto make_producer_range(write_gate_func<T> write_gate) {
    return producer_range<T>(write_gate);
}
