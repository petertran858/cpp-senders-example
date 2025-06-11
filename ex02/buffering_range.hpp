#pragma once
#include <ranges>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

/// An input range suitable for producer/consumer use cases.
/// The producer uses `write()` to add items to the range.
/// The consumer uses the normal `std::ranges` range adaptors.
/// Internally items are buffered in a thread-safe blocking queue.
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

    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = const T&;

        iterator() = default;

        explicit iterator(const buffering_range* parent)
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
            } else {
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
};

// Make it satisfy the range concept
template<typename T>
inline constexpr bool std::ranges::enable_borrowed_range<buffering_range<T>> = false;

// Helper function to create the range
template<typename T>
auto make_buffering_range() {
    return buffering_range<T>();
}
