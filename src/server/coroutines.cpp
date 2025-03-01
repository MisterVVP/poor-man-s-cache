#include "coroutines.h"

using namespace server;

void ThreadSwitchAwaiter::await_suspend(std::coroutine_handle<> h)
{
    std::jthread& out = *p_out;
    if (out.joinable()) {
        throw std::runtime_error("ThreadSwitchAwaiter: output jthread parameter not empty");
    }
    out = std::jthread([h] { h.resume(); });
}

