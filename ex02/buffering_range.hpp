#pragma once
#include <iostream>
#include <ranges>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <chrono>

// Approach 1: Using a blocking queue with timeout
template<typename T>
class buffering_range {
private:
    mutable std::queue<T> buffer_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable bool finished_ = false;
    std::function<void(std::function<void(T)>)> callback_setup_;

public:
    buffering_range() = default;
    ~buffering_range() = default;

    template<typename Item = T>
    void write(Item&& item) {
        auto lock = std::lock_guard(mutex_);
        buffer_.push(std::forward<Item>(item));
        cv_.notify_one();
    }

    class iterator {
    private:
        const buffering_range* parent_;
        std::optional<T> current_;
        bool at_end_ = false;

    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = const T&;

        iterator() = default;

        explicit iterator(const buffering_range* parent, bool at_end = false)
            : parent_(parent), at_end_(at_end) {
            if (!at_end_) {
                ++(*this); // Load first item
            }
        }

         // Copy/move constructors and assignment operators
        iterator(const iterator&) = default;
        iterator(iterator&&) = default;
        iterator& operator=(const iterator&) = default;
        iterator& operator=(iterator&&) = default;

        // Destructor
        ~iterator() = default;

        iterator& operator++() {
            if (at_end_) return *this;
            
            std::unique_lock<std::mutex> lock(parent_->mutex_);
            
            // Wait for data with timeout
            if (parent_->cv_.wait_for(lock, std::chrono::milliseconds(100), 
                [this] { return !parent_->buffer_.empty() || parent_->finished_; })) {
                
                if (!parent_->buffer_.empty()) {
                    current_ = std::move(parent_->buffer_.front());
                    parent_->buffer_.pop();
                } else {
                    at_end_ = true;
                    current_.reset();
                }
            } else {
                // Timeout - treat as end
                at_end_ = true;
                current_.reset();
            }
            
            return *this;
        }

        iterator operator++(int) {
            iterator temp = *this;
            ++(*this);
            return temp;
        }

        const T& operator*() const {
            return *current_;
        }

        bool operator==(std::default_sentinel_t) const {
            return at_end_;
        }

        // Equality operators (required for incrementable via weakly_equality_comparable)
        friend bool operator==(const iterator& lhs, const iterator& rhs) {

        std::optional<T> current_;
        bool at_end_ = false;
            return lhs.parent_ == rhs.parent_ &&
                   lhs.at_end_ == rhs.at_end_ &&
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
};

// Make it satisfy the range concept
template<typename T>
inline constexpr bool std::ranges::enable_borrowed_range<buffering_range<T>> = false;

// Helper function to create the range
template<typename T>
auto make_buffering_range() {
    return buffering_range<T>();
}
