#pragma once
#include "lib.hpp"
#include "macros.hpp"
#include <cstddef>

// =========================================================
// ADDRESSES & CONSTANTS (NSO = Ghidra - 0x100)
// =========================================================
#define ADDR_OPERATOR_NEW    FIX(0x009F88C0)
#define ADDR_OPERATOR_DELETE FIX(0x009F88A0)

// Wrappers for calling the game's internal allocators
inline void* GameOperatorNew(size_t size) {
    typedef void* (*OpNewT)(size_t);
    auto func = (OpNewT)(exl::util::GetMainModuleInfo().m_Total.m_Start + ADDR_OPERATOR_NEW);
    return func(size);
}

inline void GameOperatorDelete(void* ptr) {
    if (!ptr) return;
    typedef void (*OpDelT)(void*);
    auto func = (OpDelT)(exl::util::GetMainModuleInfo().m_Total.m_Start + ADDR_OPERATOR_DELETE);
    func(ptr);
}

template <class T>
class Allocator {
public:
    using value_type = T;

    Allocator() noexcept {}
    template <class U> Allocator(Allocator<U> const&) noexcept {}

    value_type* allocate(std::size_t n) {
        return reinterpret_cast<value_type*>(GameOperatorNew(n * sizeof(value_type)));
    }

    void deallocate(value_type* p, std::size_t) noexcept {
        GameOperatorDelete(reinterpret_cast<void*>(p));
    }
};

template <class T, class U>
bool operator==(Allocator<T> const&, Allocator<U> const&) noexcept { return true; }

template <class T, class U>
bool operator!=(Allocator<T> const& x, Allocator<U> const& y) noexcept { return !(x == y); }
