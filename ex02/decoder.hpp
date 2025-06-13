// Copyright (c) 2025 Peter Tran. All rights reserved.
#pragma once

#include <functional>
#include <exec/any_sender_of.hpp>
#include <exec/async_scope.hpp>
#include <exec/single_thread_context.hpp>

/// Simulated frame data structure.
/// A heavyweight resource that should generally be move-only.
struct hw_frame {
    int index;
    std::vector<int32_t> data; // Simulated frame data
};

/// A mock HW decoder representing a legacy C-style API.
struct hw_decoder
{
    struct client_data_t {
    };

    template <class T>
    using callback_t = std::function<void(client_data_t*, T&& frame)>;

    // simulate a HW decoder's async callback
    void decode_next_frame(client_data_t* clientData, callback_t<hw_frame> on_frame_cb) {
        auto s1 =
            ctx.get_scheduler().schedule()
            | stdexec::then([=, this] {
                // contrive some frame data
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                uint8_t offset = index*4;
                auto frame = hw_frame { .index = index++, .data = { offset++, offset++, offset++, offset++} };

                // perform C-style callback
                on_frame_cb(clientData, std::move(frame));
            })
            ;

        scope.spawn(std::move(s1));
    }

    ~hw_decoder() {
        stdexec::sync_wait(scope.on_empty());
    }

    exec::single_thread_context ctx;
    exec::async_scope scope;
    int32_t index {};
};


// Opstate that is the bridge between C++ senders and C-style callback.
template <class Receiver>
struct decode_frame_op_state : hw_decoder::client_data_t {
    using operation_state_concept = stdexec::operation_state_t;

    // C-style callback registered with hw_decoder
    static void on_frame(hw_decoder::client_data_t* baseOp, hw_frame&& frame) {
        auto op = static_cast<decode_frame_op_state*>(baseOp);
        stdexec::set_value(std::move(op->receiver), std::move(frame));
    }
 
    void start() noexcept {
        // initiate async operation
        decoder->decode_next_frame(this, &on_frame);
    }

    Receiver receiver;
    hw_decoder* decoder;
};

template <class Frame>
struct frame_index_sender_t {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(Frame&&),
        stdexec::set_error_t(std::exception_ptr)>;

    template <stdexec::receiver_of<completion_signatures> Receiver>
    auto connect(Receiver&& __receiver) const {
        return decode_frame_op_state<std::decay_t<Receiver>>
            {
                .receiver = std::forward<Receiver>(__receiver),
                .decoder = decoder
            };
    }

    hw_decoder* decoder;
};

// factory suitable for use in `let_value`
stdexec::sender auto async_decode_frame(hw_decoder* decoder) {
    return frame_index_sender_t<hw_frame> { .decoder = decoder };
}
