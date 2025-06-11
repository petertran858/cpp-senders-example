#include <iostream>
#include "buffering_range.hpp"

#include <exec/async_scope.hpp>
#include <exec/repeat_effect_until.hpp>
#include <exec/sequence/ignore_all_values.hpp>
#include <exec/sequence/iterate.hpp>
#include <exec/sequence/transform_each.hpp>
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


// Example usage
int main() {
    auto decoder = hw_decoder();

    auto client_data = hw_decoder::client_data_t{};

    auto range = make_buffering_range<int>();

    for (int i = 0; i < 10; ++i) {
        decoder.decode_next_frame(
            &client_data,
            [&range](hw_decoder::client_data_t*, int value) {
                range.write(value);
            }
        );
    }

    int total = 0;
    auto sum =
        exec::iterate(range | std::views::take(5))
        | exec::transform_each(stdexec::then([&total](auto value)
            {
                total += value;
            }))
        ;

    stdexec::sync_wait(exec::ignore_all_values(sum));

    std::cout << "Total: " << total << std::endl;

    return 0;
}