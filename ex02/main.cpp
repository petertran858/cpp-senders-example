// Copyright (c) 2025 Peter Tran. All rights reserved.

#include <iostream>
#include <exec/async_scope.hpp>
#include <exec/repeat_effect_until.hpp>
#include <exec/sequence/ignore_all_values.hpp>
#include <exec/sequence/iterate.hpp>
#include <exec/sequence/transform_each.hpp>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>

#include "ondemand_range.hpp"
#include "decoder.hpp"

auto make_frame_sequence(hw_decoder& decoder) {
    return ondemand_sequence<hw_frame>(
        // sender to provide items
        [&decoder]{ return async_decode_frame<hw_frame>(&decoder); },

        // sender for until predicate
        [&] { return stdexec::just(false); }
    );
}

void process_frame(auto&& frame, int32_t& total) {
    static_assert(std::is_rvalue_reference_v<decltype(frame)>);

    std::cout << "frame_reader: [" << frame.index << "]: " << frame.data[0] << std::endl;
    total += frame.index;
}

int main() {
    auto transfer_context = exec::single_thread_context();
    auto read_context = exec::single_thread_context();
    auto main_loop = stdexec::run_loop();
    auto main_scope = exec::async_scope();

    auto decoder = hw_decoder();

    // frame sequence is an input_range that knows how to fetch frames from decoder
    auto frame_sequence = make_frame_sequence(decoder);

    int total = 0;
    auto frame_reader =
        read_context.get_scheduler().schedule()
        | stdexec::let_value([&] {
            return
                exec::iterate(std::move(frame_sequence))
                | exec::transform_each(stdexec::then([&total](auto&& frame) {
                    process_frame(std::forward<decltype(frame)>(frame), total);
                }))
                | exec::ignore_all_values()

                | stdexec::upon_stopped([&] {
                    std::cout << "frame_reader stopped." << std::endl;
                })
                | stdexec::then([&] {
                    std::cout << "frame_reader successfully completed." << std::endl;
                });
        });

    main_scope.spawn(std::move(frame_reader));

    // Uncomment to simulate an external stop request
    std::thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        main_scope.request_stop();
    }).detach();

    stdexec::sync_wait(main_scope.on_empty());

    std::cout << "Total: " << total << std::endl;

    return 0;
}
