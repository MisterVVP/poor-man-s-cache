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

std::coroutine_handle<> server::AcceptConnTask::ConnAwaiter::await_suspend(std::coroutine_handle<> h)
{
    promise->eStatus = std::nullopt;

    if (!promise->eventLoopHandle) {
        promise->eventLoopHandle = h; 
    }

    if (!promise->eventLoopHandle.done()) {
        auto acHandle = handle_type::from_promise(*promise);
        acHandle.resume();
    }

    return promise->eventLoopHandle;
}

std::optional<EpollStatus> server::AcceptConnTask::ConnAwaiter::await_resume()
{
    return promise->eStatus;
}
