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

using until_sender = any_sender_of<stdexec::set_value_t(bool)>;
using until_sender_provider = std::function<until_sender()>;

/// A move-only input range that fetches items in an on-demand fashion.
/// When constructing the client must pass in two factories;
/// * a factory that returns a 'provider' sender for providing items
/// * a factory that returns a 'until predicate' sender for determining when to stop
///
/// Supports move-only-semantics (no copy). The Item type must be move-only as well.
template<typename Item>
class ondemand_range {
public:
    using until_predicate = std::function<bool()>;
    using sentinel = std::default_sentinel_t;

    ondemand_range(any_item_sender_provider<Item> provider, until_sender_provider until_provider)
        : any_item_sender_provider_(provider), until_sender_provider_(until_provider) {
    }
    ~ondemand_range() = default;

    // move-only
    ondemand_range(ondemand_range&&) = default;
    ondemand_range& operator=(ondemand_range&&) = default;
    ondemand_range(const ondemand_range&) = delete;
    ondemand_range& operator=(const ondemand_range&) = delete;

    class move_iterator {
    private:
        const ondemand_range* parent_;
        any_item_sender_provider<Item> any_item_sender_provider_;
        until_sender_provider until_sender_provider_;
        mutable std::optional<Item> current_;   // mutable for move-semantics

    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = Item;
        using difference_type = std::ptrdiff_t;
        using pointer = Item*;
        using reference = Item&;

        move_iterator() = default;

        explicit move_iterator(const ondemand_range* parent)
            : parent_(parent)
            , any_item_sender_provider_(parent->any_item_sender_provider_)
            , until_sender_provider_(parent->until_sender_provider_) {
            ++(*this); // Load first item
        }

        // move-only
        move_iterator(move_iterator&&) = default;
        move_iterator& operator=(move_iterator&&) = default;
        move_iterator(const move_iterator&) = delete;
        move_iterator& operator=(const move_iterator&) = delete;

        // Destructor
        ~move_iterator() = default;

        move_iterator& operator++() {
            auto [until_pred] = stdexec::sync_wait(until_sender_provider_()).value();
            if (until_pred) return *this;

            std::tie(current_) = stdexec::sync_wait(any_item_sender_provider_()).value();
            return *this;
        }

        move_iterator operator++(int) {
            move_iterator temp = std::move(*this);
            ++(*this);
            return temp;
        }

        Item operator*() const {
            return std::move(*current_);
        }

        bool operator==(std::default_sentinel_t) const {
            auto [until_pred] = stdexec::sync_wait(until_sender_provider_()).value();
            return until_pred;
        }

        // Equality operators (required for incrementable via weakly_equality_comparable)
        friend bool operator==(const move_iterator& lhs, const move_iterator& rhs) {
            return lhs.parent_ == rhs.parent_ &&
                   (!lhs.current_ && !rhs.current_) || *lhs.current_ == *rhs.current_;
        }
    };

    move_iterator begin() const {
        return move_iterator(this);
    }

    std::default_sentinel_t end() const {
        return std::default_sentinel;
    }

private:
    any_item_sender_provider<Item> any_item_sender_provider_;
    until_sender_provider until_sender_provider_;
};

// Satisfy the range concept
template<typename Item>
inline constexpr bool std::ranges::enable_borrowed_range<ondemand_range<Item>> = false;

/// Create an ondemand_range wrapped in an owning_view, intended for move-only items.
template <typename Item>
auto ondemand_sequence(any_item_sender_provider<Item> item_provider, until_sender_provider until_provider) {
    auto item_sequence = ondemand_range<Item>(item_provider, until_provider);
    return std::ranges::owning_view(std::move(item_sequence));
}
