#include <iostream>
#include <ranges>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <future>
#include <chrono>

// Approach 1: Using a blocking queue with timeout
template<typename T>
class async_callback_range {
private:
    mutable std::queue<T> buffer_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable bool finished_ = false;
    std::function<void(std::function<void(T)>)> callback_setup_;

public:
    explicit async_callback_range(std::function<void(std::function<void(T)>)> setup)
        : callback_setup_(std::move(setup)) {
        
        // Set up the callback when range is created
        callback_setup_([this](T item) {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.push(std::move(item));
            cv_.notify_one();
        });
    }

    class iterator {
    private:
        const async_callback_range* parent_;
        std::optional<T> current_;
        bool at_end_ = false;

    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = const T&;

        explicit iterator(const async_callback_range* parent, bool at_end = false)
            : parent_(parent), at_end_(at_end) {
            if (!at_end_) {
                ++(*this); // Load first item
            }
        }

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

        void operator++(int) {
            ++(*this);
        }

        const T& operator*() const {
            return *current_;
        }

        bool operator==(std::default_sentinel_t) const {
            return at_end_;
        }
    };

    iterator begin() const {
        return iterator(this);
    }

    std::default_sentinel_t end() const {
        return std::default_sentinel;
    }

    void finish() const {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        cv_.notify_all();
    }
};

// Make it satisfy the range concept
template<typename T>
inline constexpr bool std::ranges::enable_borrowed_range<async_callback_range<T>> = false;

// Helper function to create the range
template<typename T>
auto make_async_callback_range(std::function<void(std::function<void(T)>)> setup) {
    return async_callback_range<T>(std::move(setup));
}

// Example usage
int main() {
    // Example 1: Simulating async callbacks (like WebSocket messages)
    std::optional<std::thread> t;
    auto range = make_async_callback_range<int>([&t](auto callback) {
        // Simulate async source in another thread
        t = std::thread([callback = std::move(callback)]() {
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                callback(i * i); // Send i^2
            }
        });
        t->detach();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Allow time for async processing

    // Use with ranges algorithms
    for (auto value : range | std::views::take(5)) {
        std::cout << "Received value: " << value << std::endl;
    }

    t->join();

    return 0;
}