// Copyright (c) 2025 Peter Tran. All rights reserved.
#pragma once

#include <functional>
#include <exec/async_scope.hpp>
#include <exec/single_thread_context.hpp>

/// A mock HW decoder.
struct hw_decoder
{
    struct client_data_t {
    };

    using callback_t = std::function<void(client_data_t*, int)>;

    // simulate a HW decoder's async callback
    void decode_next_frame(client_data_t* clientData, callback_t on_frame_cb) {
        auto s1 =
            ctx.get_scheduler().schedule()
            | stdexec::then([=, this]{
                on_frame_cb(clientData, index++);
            })
            ;

        scope.spawn(std::move(s1));
    }

    ~hw_decoder() {
        stdexec::sync_wait(scope.on_empty());
    }

    exec::single_thread_context ctx;
    exec::async_scope scope;
    int index {};
};


// Opstate that is the bridge between C++ senders and C-style callback.
template <class Receiver>
struct decode_frame_op_state : hw_decoder::client_data_t {
    using operation_state_concept = stdexec::operation_state_t;

    // C-style callback registered with hw_decoder
    static void on_frame(hw_decoder::client_data_t* baseOp, int frameIndex) {
        auto op = static_cast<decode_frame_op_state*>(baseOp);
        stdexec::set_value(std::move(op->receiver), frameIndex);
    }
 
    void start() noexcept {
        // initiate async operation
        decoder->decode_next_frame(this, &on_frame);
    }

    Receiver receiver;
    hw_decoder* decoder;
};

struct frame_index_sender_t {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(int),
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
    return frame_index_sender_t { .decoder = decoder };
}
