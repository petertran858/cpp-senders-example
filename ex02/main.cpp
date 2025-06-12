// Copyright (c) 2025 Peter Tran. All rights reserved.

#include <iostream>
#include <exec/async_scope.hpp>
#include <exec/repeat_effect_until.hpp>
#include <exec/sequence/ignore_all_values.hpp>
#include <exec/sequence/iterate.hpp>
#include <exec/sequence/transform_each.hpp>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>

#include "producer_range.hpp"
#include "decoder.hpp"

int main() {
    auto transfer_context = exec::single_thread_context();
    auto read_context = exec::single_thread_context();
    auto main_loop = stdexec::run_loop();
    auto main_scope = exec::async_scope();

    auto decoder = hw_decoder();

    const int frame_cache_limit = 1;
    auto frame_cache = make_producer_range<hw_frame>([frame_cache_limit](const std::queue<hw_frame>& q){
        return q.size() < frame_cache_limit;
    });

    int count = 10;

    auto frame_transfer =
        transfer_context.get_scheduler().schedule()
        | stdexec::let_value([&] {
            return
                stdexec::just()
                | stdexec::let_value([&] { return frame_cache.async_write_gate(); })

                | stdexec::let_value([&] { return async_decode_frame(&decoder); })

                | stdexec::then([&](auto&& frame) {
                    // std::cout << "frame_transfer: " << frame.index << std::endl;
                    frame_cache.add(std::move(frame));
                })

                // repeat for `count` iterations
                | stdexec::then([&count] { return --count == 0; })
                | exec::repeat_effect_until()

                | stdexec::upon_stopped([&] {
                    std::cout << "frame_transfer stopped." << std::endl;
                })
                | stdexec::then([&] {
                    std::cout << "frame_transfer successfully completed." << std::endl;
                });
        });

    int total = 0;
    auto frame_reader =
        read_context.get_scheduler().schedule()
        | stdexec::let_value([&] {
            return
                exec::iterate(std::views::all(frame_cache))
                | exec::transform_each(stdexec::then([&total](auto&& frame) {
                    std::cout << "frame_reader: [" << frame.index << "]: " << frame.data[0] << std::endl;
                    total += frame.index;
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
    main_scope.spawn(std::move(frame_transfer));

    // Uncomment to simulate an external stop request
    std::thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "frame_cache.finish" << std::endl;
        frame_cache.finish();
    }).detach();

    stdexec::sync_wait(main_scope.on_empty());

    std::cout << "Total: " << total << std::endl;

    return 0;
}
