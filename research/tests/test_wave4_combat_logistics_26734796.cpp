// Offline fixtures for the two build-specific hooks and three shared simulation paths.
#include "wave4_weapon_cases.hpp"
#include "wave4_vehicle_cases.hpp"
#include "wave4_share_cases.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef GOH_WAVE4_TRAMPOLINES
extern "C" {
#include "trampoline.h"
BOOL IsExecutableAddress(void*) {
    return FALSE;
}
}
namespace {
// Entry bytes checked against the SHA-256-gated executable. The next instruction is
// only decoded to locate the jump back and is never executed by the fixture.
void check_trampoline(std::array<unsigned char, 32> entry, unsigned cut, unsigned instructions) {
    unsigned char relocated[64]{}, detour{};
    TRAMPOLINE t{};
    t.pTarget = entry.data();
    t.pTrampoline = relocated;
    t.pDetour = &detour;
    assert(CreateTrampolineFunction(&t) && !t.patchAbove);
    assert(t.nIP == instructions + 1 && t.oldIPs[instructions] == cut && t.newIPs[instructions] == cut);
    assert(!std::memcmp(entry.data(), relocated, cut));
    assert(relocated[cut] == 0xff && relocated[cut + 1] == 0x25);
    void *resume{}, *redirect{};
    std::memcpy(&resume, relocated + cut + 6, sizeof resume);
    std::memcpy(&redirect, static_cast<unsigned char*>(t.pRelay) + 6, sizeof redirect);
    assert(resume == entry.data() + cut && redirect == &detour);
}
} // namespace
#endif

int main() {
    assert(goh::runtime::table().hook.human_weapon_swap == 0x95cea0);
    assert(goh::runtime::table().hook.select_exit_door == 0x79d3c0);
    goh::test::wave4_weapon::run();
    goh::test::wave4_vehicle::run();
    goh::test::wave4_share::run();
#ifdef GOH_WAVE4_TRAMPOLINES
    check_trampoline({0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18}, 5, 1);
    check_trampoline({0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x20}, 7, 2);
#endif
    std::puts("Wave 4 combat logistics fixtures passed.");
}
