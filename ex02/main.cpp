// Copyright (c) 2025 Peter Tran. All rights reserved.

#include <iostream>
#include <exec/async_scope.hpp>
#include <exec/repeat_effect_until.hpp>
#include <exec/sequence/ignore_all_values.hpp>
#include <exec/sequence/iterate.hpp>
#include <exec/sequence/transform_each.hpp>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>

#include "buffering_range.hpp"
#include "decoder.hpp"

int main() {
    auto io_context = exec::single_thread_context();
    auto main_loop = stdexec::run_loop();
    auto main_scope = exec::async_scope();

    auto decoder = hw_decoder();

    auto frame_cache_range = make_buffering_range<int>();

    int count = 10;

    auto frame_transfer =
        io_context.get_scheduler().schedule()
        | stdexec::let_value([&] {
            return
                async_decode_frame(&decoder)
                | stdexec::then([&](auto value) {
                    std::cout << "frame_transfer: " << value << std::endl;
                    frame_cache_range.write(value);
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
        io_context.get_scheduler().schedule()
        | stdexec::let_value([&] {
            return
                exec::iterate(std::views::all(frame_cache_range))
                | exec::transform_each(stdexec::then([&total](auto value) {
                    std::cout << "frame_reader: " << value << std::endl;
                    total += value;
                }))
                | exec::ignore_all_values()

                | stdexec::upon_stopped([&] {
                    std::cout << "frame_reader stopped." << std::endl;
                })
                | stdexec::then([&] {
                    std::cout << "frame_reader successfully completed." << std::endl;
                });
        });

    main_scope.spawn(std::move(frame_transfer));
    main_scope.spawn(std::move(frame_reader));

    // Uncomment to simulate an external stop request
    std::thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        frame_cache_range.finish();
    }).detach();

    stdexec::sync_wait(main_scope.on_empty());

    std::cout << "Total: " << total << std::endl;

    return 0;
}
