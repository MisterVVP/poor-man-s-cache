#pragma once

struct NonCopyableOrMovable
{
    NonCopyableOrMovable() = default;

    NonCopyableOrMovable(const NonCopyableOrMovable&) = delete;
    NonCopyableOrMovable(NonCopyableOrMovable&&) = delete;

    NonCopyableOrMovable& operator=(const NonCopyableOrMovable&) = delete;
    NonCopyableOrMovable& operator=(NonCopyableOrMovable&&) = delete;
};

struct NonCopyable {
    NonCopyable() = default;

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};