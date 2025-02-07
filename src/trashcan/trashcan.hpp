#pragma once
#include <cstdint>
#include <vector>
#include "../non_copyable.h"

#ifndef NDEBUG
#include <iostream>
#endif

template <typename T>
class Trashcan : NonCopyable {
    private:
        static constexpr uint_fast64_t DEFAULT_INITIAL_CAPACITY = 10000;
        uint_fast64_t capacity = DEFAULT_INITIAL_CAPACITY;
        std::vector<const T*> garbage;
    public:
        Trashcan(uint_fast64_t capacity = DEFAULT_INITIAL_CAPACITY);
        ~Trashcan();
        void Empty();
        void AddGarbage(const T* trash);
};

template <typename T>
Trashcan<T>::Trashcan(uint_fast64_t capacity): capacity(capacity)
{
    garbage.reserve(capacity);
}

template <typename T>
Trashcan<T>::~Trashcan()
{
    Empty();
}

template <typename T>
void Trashcan<T>::Empty()
{
#ifndef NDEBUG
    std::cout << "Collecting garbage..." << std::endl;
    uint_fast64_t trashCount = 0;
#endif
    for (auto i = 0; i < garbage.size(); i++)
    {
        if (garbage[i] != nullptr) {
            delete[] garbage[i];
            garbage[i] = nullptr;
#ifndef NDEBUG
            ++trashCount;
#endif
        }
    }
#ifndef NDEBUG
    std::cout << "Deleted " << trashCount << " pointers" << std::endl;
#endif
}

template <typename T>
void Trashcan<T>::AddGarbage(const T* trash)
{
    if (trash == nullptr) return;

    for (auto i = 0; i < garbage.size(); i++)
    {
        if (!garbage[i]) {
            garbage[i] = trash;
            return;
        }
    }
    garbage.emplace_back(trash);
}
