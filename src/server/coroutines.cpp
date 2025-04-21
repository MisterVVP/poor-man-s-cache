#include "coroutines.h"

using namespace server;

std::coroutine_handle<> server::HandleReqAwaiter::await_suspend(std::coroutine_handle<> h)
{
    if (!h.done()) {
       auto hrt = HandleReqTask::handle_type::from_promise(*promise);
       if (!hrt.done()) {
         hrt.resume();
       }
       return h;
    }

    return std::noop_coroutine();
}

std::coroutine_handle<> server::ConnAwaiter::await_suspend(std::coroutine_handle<> h)
{
    promise->eStatus = EpollStatus::NotReady();

    if (!h.done()) {
        auto acHandle = AcceptConnTask::handle_type::from_promise(*promise);
        if (!acHandle.done()) {
            acHandle.resume();
        }
        return h;
    }

    return std::noop_coroutine();
}

EpollStatus server::ConnAwaiter::await_resume()
{
    return promise->eStatus;
}
