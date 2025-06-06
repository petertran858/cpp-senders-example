# cpp-senders-example

Experiments with C++ Senders/Receivers.

## Examples

### ex01

This example uses a [static_thread_pool](https://github.com/NVIDIA/stdexec/blob/main/include/exec/static_thread_pool.hpp) for scheduling IO.

Build:
```
cmake -B build && cmake --build ./build
```

Run:
```
./build/ex01/ex01
```

#### Example Output

```
after write: qsize, 1
after read: qsize, 0
process frame index: 0
after write: qsize, 1
after read: qsize, 0
process frame index: 1
after write: qsize, 1
after read: qsize, 0
process frame index: 2
```

The variable `count` controls number of iterations for `repeat_effect_until`. It is expected to be decremented every iteration until it reaches 0.

However, there is currently a bug where the count never reaches 0.

# References
* [P2300](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2300r10.html)
    * Senders proposal accepted for C++26
* [stdexec](https://github.com/NVIDIA/stdexec)
    * Senders reference implementation
* [What are Senders Good For, Anyway?](https://ericniebler.com/2024/02/04/what-are-senders-good-for-anyway/)
    * Article by Eric Niebler showing how to wrap a C-style callback in a sender