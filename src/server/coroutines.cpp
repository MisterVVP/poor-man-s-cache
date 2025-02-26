#include "coroutines.h"

using namespace server;

IncomingConnectionAwaiter HandleReqTask::promise_type::await_transform(const AcceptConnTask &ac)
{
    AcceptConnTask::promise_type &act = ac.c_handle.promise();
    return IncomingConnectionAwaiter{&act};
};

IncomingConnectionAwaiter::IncomingConnectionAwaiter(AcceptConnTask::promise_type *promise): promise(promise) {}

std::coroutine_handle<> IncomingConnectionAwaiter::await_suspend(std::coroutine_handle<>)
{
    if (promise != nullptr) {
        return AcceptConnTask::handle_type::from_promise(*promise);
    }
    return std::noop_coroutine();
}


HandleEventAwaiter::HandleEventAwaiter(HandleReqTask::promise_type *promise): promise(promise) {}

std::coroutine_handle<> HandleEventAwaiter::await_suspend(std::coroutine_handle<>)
{
    if (promise != nullptr) {
        return HandleReqTask::handle_type::from_promise(*promise);
    }
    return std::noop_coroutine();
}

auto AcceptConnTask::promise_type::await_transform(HandleReqTask &hr) -> HandleEventAwaiter
{
    HandleReqTask::promise_type &hrt = hr.c_handle.promise();
    return HandleEventAwaiter{&hrt};
}

