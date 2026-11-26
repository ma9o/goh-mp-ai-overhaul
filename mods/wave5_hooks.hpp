#pragma once
#include "game_runtime.hpp"

#include <array>

namespace goh::runtime {
// A failed MinHook removal still owns a trampoline. Retain those entries until
// the coordinator has disabled and uninitialized MinHook.
template <std::size_t N> class HookBatch {
    std::array<void*, N> targets_{};
    std::size_t count_{};

public:
    template <class Detour, class Fn, class Create>
    bool add(Address image, Address rva, Detour detour, Fn& original, Create create) {
        if (count_ == N) return false;
        void* target = reinterpret_cast<void*>(image + rva);
        void* trampoline{};
        if (create(target, reinterpret_cast<void*>(detour), &trampoline) != 0) return false;
        targets_[count_++] = target;
        original = reinterpret_cast<Fn>(trampoline);
        return trampoline != nullptr;
    }

    template <class Remove> bool rollback(Remove remove) {
        for (std::size_t i = count_; i > 0; --i) {
            if (remove(targets_[i - 1]) == 0) targets_[i - 1] = nullptr;
        }
        std::size_t retained = 0;
        for (std::size_t i = 0; i < count_; ++i)
            if (targets_[i]) targets_[retained++] = targets_[i];
        count_ = retained;
        return empty();
    }
    bool empty() const {
        return count_ == 0;
    }
    void reset() {
        count_ = 0;
    }
};
} // namespace goh::runtime
