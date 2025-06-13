// Copyright (c) 2025 Peter Tran. All rights reserved.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

template <class... Ts>
using any_sender_of =
  typename exec::any_receiver_ref<stdexec::completion_signatures<Ts...>>::template any_sender<>;

template <typename Item>
using any_item_sender = any_sender_of<stdexec::set_value_t(Item&&), stdexec::set_error_t(std::exception_ptr)>;

template <typename Item>
using any_item_sender_provider = std::function<any_item_sender<Item>()>;

/// An input range that fetches items in an on-demand fashion via the passed in sender factory.
/// The consumer uses the normal `std::ranges` range adaptors.
template<typename T>
class ondemand_range {
private:
    mutable std::atomic_bool finished_ { false };
    any_item_sender_provider<T> any_item_sender_provider_;

public:
    ondemand_range(any_item_sender_provider<T>&& provider) : any_item_sender_provider_(std::move(provider)) {
    }
    ~ondemand_range() = default;

    class iterator {
    private:
        const ondemand_range* parent_;
        std::optional<T> current_;

    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&&;

        iterator() = default;

        explicit iterator(const ondemand_range* parent)
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
            std::tie(current_) = stdexec::sync_wait(parent_->any_item_sender_provider_()).value();
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
            return parent_->finished_.load();
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
        finished_.store(true);
    }
};

// Satisfy the range concept
template<typename T>
inline constexpr bool std::ranges::enable_borrowed_range<ondemand_range<T>> = false;

// Helper function to create the range
template<typename T>
auto make_ondemand_range(any_item_sender_provider<T>&& provider) {
    return ondemand_range<T>(std::move(provider));
}
