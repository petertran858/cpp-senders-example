// Copyright (c) 2025 Peter Tran. All rights reserved.

#include <iostream>

#include <exec/async_scope.hpp>
#include <exec/repeat_effect_until.hpp>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>

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

struct frame_index_cache {
    auto read() {
        auto lock = std::unique_lock(mutex);

        signal.wait(lock, [&, this] { 
            return !queue.empty();
       });

        auto frameIndex = queue.front();
        queue.pop();
        signal.notify_all();
        std::cout << "after read: i,qsize, " << frameIndex << "," << queue.size() << std::endl;
        return frameIndex;
    }

    auto write(int frameIndex) {
        auto lock = std::unique_lock(mutex);

        queue.push(frameIndex);
        signal.notify_all();
        std::cout << "after write: i,qsize, " << frameIndex << "," << queue.size() << std::endl;
    }

    std::mutex mutex;
    std::condition_variable signal;
    std::queue<int> queue;
};

stdexec::sender auto asyncRead(frame_index_cache* frame_cache)
{
    return stdexec::just(frame_cache->read());
}

int main() {
    auto io_pool = exec::static_thread_pool(2);
    auto io_sched = io_pool.get_scheduler();

    auto main_loop = stdexec::run_loop();
    auto main_sched = main_loop.get_scheduler();

    auto main_scope = exec::async_scope();
    std::atomic_bool stop_reader_requested { false };
    
    auto decoder = hw_decoder();
    auto frame_cache = frame_index_cache();

    const int limit = 100000;
    int count = limit;

    auto frame_decode_and_cache =
        io_sched.schedule()

        | stdexec::let_value([&] {
           return async_decode_frame(&decoder)
                    | stdexec::then([&frame_cache](int frameIndex) {
                        frame_cache.write(frameIndex);
                    })
                    // repeat for `count` iterations
                    | stdexec::then([&count] {
                        return --count == 0;
                    })
                    | exec::repeat_effect_until()
                    | stdexec::then([&] {
                        main_loop.finish();
                        main_scope.request_stop();

                    })
                    ;
        })

        ;

    auto frame_reader =
        io_sched.schedule()
        | stdexec::let_value([&] {
            return stdexec::just(&frame_cache)
                    | stdexec::let_value(asyncRead)

                    | stdexec::continues_on(main_sched)

                    | stdexec::then([&](int frameIndex){
                        std::cout << "process frame index: " << frameIndex << std::endl;
                        return frameIndex == (limit - 1); // last frame
                    })
                    | exec::repeat_effect_until()
              ;
        })
        ;

    main_scope.spawn(std::move(frame_decode_and_cache));
    main_scope.spawn(std::move(frame_reader));
    main_loop.run();


    stdexec::sync_wait(main_scope.on_empty());

    return 0;
}
