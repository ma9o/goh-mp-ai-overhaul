// Build 26734796 offline policy, native adapter and MinHook entry fixtures.
#include "../../mods/wave5_hooks.hpp"
#include "wave5_order_cases.hpp"
#include "wave5_dirt_cases.hpp"
#include "wave5_crush_cases.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

#ifdef GOH_WAVE5_TRAMPOLINES
extern "C" {
#include "trampoline.h"
BOOL IsExecutableAddress(void*) {
    return FALSE;
}
}
#endif

namespace {
using goh::runtime::Address;

void invalid_inputs() {
    // A malformed native tag row must fail before its string-field offset wraps.
    assert(!goh::auto_smoke::SmokeTagEquals(std::numeric_limits<Address>::max() - 0x3f, "destroyed"));
    struct ExhaustedTable {
        int* append_slot() {
            throw std::bad_alloc();
        }
    } exhausted;
    assert(goh::store::try_append_slot(exhausted) == nullptr);
}
void hook_batch() {
    goh::runtime::HookBatch<3> hooks;
    using Fn = void (*)();
    Fn detour = +[] {}, original{};
    auto create = [&](void*, void*, void** trampoline) {
        *trampoline = reinterpret_cast<void*>(detour);
        return 0;
    };
    for (Address offset : {1u, 2u, 3u})
        assert(hooks.add(100, offset, detour, original, create) && original == detour);
    assert(!hooks.add(100, 4, detour, original, create));
    std::vector<Address> removed;
    assert(!hooks.rollback([&](void* target) {
        removed.push_back(reinterpret_cast<Address>(target));
        return removed.back() == 102 ? 1 : 0;
    }));
    assert((removed == std::vector<Address>{103, 102, 101}) && !hooks.empty());
    assert(hooks.rollback([](void* target) { return reinterpret_cast<Address>(target) == 102 ? 0 : 1; }));
    assert(hooks.empty());
}

#ifdef GOH_WAVE5_TRAMPOLINES
void check_trampoline(const char* path, std::initializer_list<unsigned char> prefix,
                      std::initializer_list<unsigned> boundaries) {
    std::array<unsigned char, 64> entry{}, relocated{};
    unsigned char detour{};
    auto* file = std::fopen(path, "rb");
    assert(file && std::fread(entry.data(), 1, entry.size(), file) == entry.size());
    std::fclose(file);
    assert(std::equal(prefix.begin(), prefix.end(), entry.begin()));
    const auto cut = prefix.size();
    TRAMPOLINE t{};
    t.pTarget = entry.data();
    t.pTrampoline = relocated.data();
    t.pDetour = &detour;
    assert(CreateTrampolineFunction(&t) && !t.patchAbove);
    assert(t.nIP == boundaries.size() && std::equal(boundaries.begin(), boundaries.end(), t.oldIPs));
    assert(std::equal(boundaries.begin(), boundaries.end(), t.newIPs));
    assert(std::equal(prefix.begin(), prefix.end(), relocated.begin()));
    assert(relocated[cut] == 0xff && relocated[cut + 1] == 0x25);
    void *resume{}, *redirect{};
    std::memcpy(&resume, relocated.data() + cut + 6, sizeof resume);
    std::memcpy(&redirect, static_cast<unsigned char*>(t.pRelay) + 6, sizeof redirect);
    assert(resume == entry.data() + cut && redirect == &detour);
}
#endif
} // namespace

int main(int argc, char** argv) {
    invalid_inputs();
    hook_batch();
    goh::test::wave5_order::run();
    goh::test::wave5_dirt::run();
    goh::test::wave5_crush::run();
#ifdef GOH_WAVE5_TRAMPOLINES
    assert(argc == 7);
    check_trampoline(argv[1], {0x48, 0x89, 0x5c, 0x24, 0x08}, {0, 5});
    check_trampoline(argv[2], {0x48, 0x89, 0x5c, 0x24, 0x10}, {0, 5});
    check_trampoline(argv[3], {0x48, 0x89, 0x5c, 0x24, 0x08}, {0, 5});
    check_trampoline(argv[4], {0x41, 0x57, 0x48, 0x81, 0xec, 0xd0, 0x01, 0x00, 0x00}, {0, 2, 9});
    check_trampoline(argv[5], {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x48, 0x08}, {0, 3, 7});
    check_trampoline(argv[6], {0x40, 0x55, 0x57, 0x48, 0x8d, 0xac, 0x24, 0xf8, 0xfc, 0xff, 0xff}, {0, 2, 3, 11});
#else
    (void)argc;
    (void)argv;
#endif
    std::puts("Wave 5 combat and hazard fixtures passed.");
}
