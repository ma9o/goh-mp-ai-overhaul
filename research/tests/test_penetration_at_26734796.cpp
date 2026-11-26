// Offline GEM fixtures. GOH_AT_TRAMPOLINES links MinHook's instruction copier
// and accepts two 64-byte entry samples from the fingerprinted PE.
#include "../../mods/penetration_at_module.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#ifdef GOH_AT_TRAMPOLINES
extern "C" {
#include "trampoline.h"
BOOL IsExecutableAddress(void*) {
    return FALSE;
}
}
#endif

namespace at = goh::penetration_at;
namespace d = at::detail;
namespace p = at::policy;
namespace pr = goh::target_prio::detail;
using goh::target_prio::Address;

template <std::size_t N> struct Memory {
    alignas(16) std::array<std::byte, N> data{};
    Address ptr() const {
        return reinterpret_cast<Address>(data.data());
    }
    template <class T> void put(std::size_t off, T v) {
        assert(off + sizeof v <= N);
        std::memcpy(data.data() + off, &v, sizeof v);
    }
    void tag(std::size_t off, std::string_view name) {
        assert(name.size() <= 15);
        std::memcpy(data.data() + off + 0x40, name.data(), name.size());
        put(off + 0x50, name.size());
        put(off + 0x58, std::size_t{15});
    }
};
void volumes() {
    const p::Estimate weak{35, 65, .95f}, clean{35, 25, .95f}, glancing{35, 25, .41f};
    const auto body = p::Part::body;
    assert(!weak.clean() && clean.clean() && clean.strong());
    assert((p::Estimate{10.5f, 10, .9f}.clean()));
    assert((p::Estimate{205, 200, .9f}.clean()));
    for (auto part : {p::Part::gun, p::Part::mantlet, p::Part::track}) {
        assert(p::volume_score(body, clean, false) > p::volume_score(part, clean, true));
        assert(p::volume_score(part, weak, false) > p::volume_score(body, weak, true));
    }
    assert(p::volume_score(body, clean, false) > p::volume_score(body, glancing, true));
    assert(p::volume_score(body, clean, true) > p::volume_score(body, clean, false));
    assert(p::volume_score(body, weak, true) == p::volume_score(body, weak, false));
    assert(p::part("trackleft") == p::part("trackright") && p::part("trackright") == p::Part::track);
    assert(p::part("gun2") == p::Part::gun && p::part("aux_turret1") == p::Part::turret);
    for (auto tag : {"", "body"})
        assert(p::permitted_volume(tag, false, false, false, p::Part::engine));
    assert(!p::permitted_volume("gun1", false, false, false, body));
    assert(p::permitted_volume("port", true, false, false, p::Part::other));
    assert(!p::permitted_volume("gun1", true, false, true, p::Part::gun));
    assert(!p::permitted_volume("", false, true, false, p::Part::gun));
    p::Outcome unknown;
    unknown.observe(body, {NAN, 2, 1});
    assert(!unknown.seen);
    Memory<0x1000> actor;
    actor.tag(0xde8, "human");
    assert(!pr::armoured_class(actor.ptr()));
    Memory<0x1800> def;
    Memory<0x60> tag;
    tag.tag(0, "weapon_throwing");
    def.put(0x11e0, tag.ptr());
    def.put(0x11e8, tag.ptr() + 0x60);
    Memory<0xb0> weapon;
    weapon.put(0x98, def.ptr());
    assert(d::shell(weapon.ptr(), 1) == p::Shell::unavailable);
}
void ammo() {
    p::Outcome strong, marginal, bounce, tracks;
    strong.observe(p::Part::body, {40, 25, .8f});
    marginal.observe(p::Part::body, {21, 20, 1});
    bounce.observe(p::Part::body, {20, 44, 1});
    tracks.observe(p::Part::track, {5, 5, 1});
    auto rank = [](auto kind, const p::Outcome& o, bool ready = false, bool open = false) {
        return p::shell_priority(kind, o, ready, open);
    };
    using S = p::Shell;
    assert(p::ready_soon(true, false, 10) && p::ready_soon(false, true, 1.49f));
    assert(!p::ready_soon(false, true, 1.5f) && !p::ready_soon(false, false, 0) && !p::ready_soon(false, true, NAN));
    assert(p::actively_reloading(true, false, 0) && p::actively_reloading(false, true, 5.5f));
    assert(!p::actively_reloading(false, true, 0.f) && !p::actively_reloading(false, true, 25.f) &&
           !p::actively_reloading(false, false, 3.f) && !p::actively_reloading(false, true, NAN));
    p::Outcome borderline;
    borderline.observe(p::Part::body, {100, 99, .9f});
    assert(!borderline.clean && rank(S::standard, borderline) > rank(S::explosive, tracks));
    assert(!p::subcaliber_improves(22, 24, false));
    assert(p::subcaliber_improves(25, 24, false) && p::subcaliber_improves(22, 24, true));
    assert(rank(S::subcaliber, strong, true) > rank(S::standard, strong));
    assert(rank(S::standard, strong) > rank(S::subcaliber, strong));
    assert(rank(S::cumulative, strong) > rank(S::standard, marginal));
    assert(rank(S::subcaliber, strong) > rank(S::standard, bounce));
    assert(rank(S::standard, strong) > rank(S::subcaliber, bounce, true));
    assert(rank(S::explosive, tracks) > rank(S::standard, bounce));
    assert(rank(S::explosive, tracks, false, true) > rank(S::standard, strong));
    assert(rank(S::standard, tracks, true) == 0);
    // All four row records exist. Only the fifth live, compatible inventory
    // definition improves the score. The inventory location must name that stack.
    std::array<Address, 4> present{11, 12, 13, 14};
    std::array<Memory<0x50>, 5> stacks{};
    std::array<Address, 5> ptrs{};
    std::array<Address, 5> defs{11, 19, 18, 15, 14};
    for (unsigned i = 0; i < stacks.size(); ++i) {
        stacks[i].put(0x40, defs[i]);
        stacks[i].put(0x48, i == 2 ? 0 : 1);
        ptrs[i] = stacks[i].ptr();
    }
    auto begin = reinterpret_cast<Address>(ptrs.data());
    unsigned visits = 0;
    assert(pr::overflow_ammo(
        {begin, begin + sizeof(ptrs)}, present, [](Address a) { return a != 19; },
        [&](pr::Ammo a) {
            assert(a.stack == ptrs[3] && a.def == 15 && !a.record);
            assert(rank(S::cumulative, strong) > rank(S::standard, bounce));
            ++visits;
        }));
    assert(visits == 1);
    assert(
        !pr::overflow_ammo({begin, begin + 1}, present, [](Address) { return true; }, [](pr::Ammo) { assert(false); }));
}
void targets_and_fire() {
    p::Outcome medium, heavy, marginal, empty;
    medium.observe(p::Part::turret, {45, 25, .8f});
    heavy.observe(p::Part::body, {10, 30, 1});
    marginal.observe(p::Part::body, {29, 30, 1});
    assert(.5f + p::killability(medium, true, false) > .8f + p::killability(heavy, true, true));
    assert(p::killability(heavy, true, false) == 0 && p::killability(heavy, false, true) == 0);
    assert(p::killability(marginal, true, true) > 0);
    assert(p::danger(1, true, {}, 50) < p::danger(1, false, {}, 50));
    for (auto kind : {p::Infantry{true, false, false, true}, p::Infantry{false, false, true, true}}) {
        float limit = kind.launcher ? 75.f : 28.f;
        assert(p::danger(0, false, kind, limit) > p::danger(1, false, {}, limit));
        assert(p::danger(0, false, kind, limit + .01f) == 0);
        assert(!p::stray(true, true, kind, 80));
    }
    assert(!p::stray(true, true, {false, true, false, true}, 80));
    p::Infantry rifleman{false, false, false, true};
    assert(!p::stray(true, true, {}, 80) && !p::stray(false, true, rifleman, 80) &&
           !p::stray(true, false, rifleman, 80));
    assert(p::stray(true, true, rifleman, 40.01f) && !p::stray(true, true, rifleman, 40));
    for (int mode : {0, 1, 2, 3, 4})
        for (int opt : {0, 1, 2, 3, 4, 5}) {
            int result = mode == 2 && opt == 3 ? 0 : opt;
            assert(p::fire_option(mode, opt, true, 36, false, true, heavy, true) == result);
            assert(p::fire_option(mode, opt, true, 41, true, false, empty, false) == result);
            assert(p::fire_option(mode, opt, false, 100, true, true, heavy, true) == opt);
        }
    assert(p::fire_option(2, 3, true, 35, false, true, heavy, true) == 3);
    assert(p::fire_option(2, 3, true, 40, true, false, empty, false) == 3);
    for (auto o : {empty, medium, marginal})
        assert(p::fire_option(2, 3, true, 80, false, true, o, true) == 3);
    assert(p::fire_option(2, 3, true, 80, false, true, heavy, false) == 3);
    assert(p::fire_option(2, 3, true, NAN, true, true, heavy, true) == 3);
}
void memory_guards() {
    Memory<0xe00> actor;
    Memory<0x400> brain;
    Memory<0xa0> weaponry;
    Memory<0x20> chassis;
    pr::base = 0x100000;
    actor.put(0xdc8, brain.ptr());
    brain.put(0x38, actor.ptr());
    actor.put(0xdd8, weaponry.ptr());
    actor.put(0xde0, chassis.ptr());
    for (Address vt : {0x110ff88, 0x101fd68}) {
        brain.put(0, pr::base + vt);
        brain.put(0x398, 1u);
        assert(pr::authorized_actor(actor.ptr(), brain.ptr()));
        for (unsigned inhibit : {1u, 2u, 32u, 63u}) {
            brain.put(0x39c, inhibit);
            assert(!pr::authorized_actor(actor.ptr(), brain.ptr()));
        }
        brain.put(0x39c, 0u);
        chassis.put(0x18, 1u);
        assert(!pr::authorized_actor(actor.ptr(), brain.ptr()));
        chassis.put(0x18, 0u);
        weaponry.put(0x70, 2u);
        assert(!pr::authorized_actor(actor.ptr(), brain.ptr()));
        weaponry.put(0x70, 0u);
        brain.put(0x398, 1u << 10);
        assert(!pr::authorized_actor(actor.ptr(), brain.ptr()));
        brain.put(0x398, 2u << 10);
        assert(!pr::authorized_actor(actor.ptr(), brain.ptr()));
        brain.put(0x39c, 1u << 18);
        assert(pr::authorized_actor(actor.ptr(), brain.ptr()));
        brain.put(0x39c, 0u);
    }
    brain.put(0, pr::base + 0x1020020);
    assert(!pr::authorized_actor(actor.ptr(), brain.ptr()));
    pr::base = 0;
    d::cache.tick(7, 0xffffffff);
    d::cache.used = 4;
    d::cache.rays = 37;
    d::cache.tick(7, 0xffffffff);
    assert(d::cache.used == 4 && d::cache.rays == 37);
    d::cache.tick(7, 0);
    assert(!d::cache.used && !d::cache.rays);
    d::cache.used = 2;
    d::cache.tick(8, 0);
    assert(!d::cache.used);
}
void contacts_and_threats() {
    Memory<0x38> volume;
    volume.put(0xc, 2u);
    d::Contact c{};
    c.volume = volume.ptr();
    pr::SceneHit h{};
    h.kind = 4;
    h.entity = 3;
    h.volume = c.volume;
    h.normal = {-1, 0, 0};
    h.direction = {1, 0, 0};
    pr::HitVec v{&h, &h + 1, &h + 1};
    assert(d::first_contact(c, v, 3) && c.exposed && c.cosine == 1);
    c.exposed = false;
    h.entity = 4;
    assert(d::first_contact(c, v, 3) && !c.exposed);
    h.entity = 3;
    h.face = 6;
    assert(!d::first_contact(c, v, 3) && !c.exposed);
    h.kind = 1;
    assert(d::first_contact(c, v, 3) && !c.exposed);
    Memory<0xe00> actor;
    Memory<0x340> sensor;
    Memory<0xa0> weaponry;
    actor.put(0xdd0, sensor.ptr());
    actor.put(0xdd8, weaponry.ptr());
    sensor.put(0x260, actor.ptr());
    at::World w{0, 9, 100, 1, 10};
    assert(d::quiet(w, actor.ptr()));
    weaponry.put(0x90, 99u);
    assert(!d::quiet(w, actor.ptr()));
    weaponry.put(0x90, 0u);
    sensor.put(0x2f4, 60u);
    assert(!d::quiet(w, actor.ptr()));
    sensor.put(0x2f4, 59u);
    assert(d::quiet(w, actor.ptr()));
    Memory<0x80> sub;
    Address s = sub.ptr();
    sensor.put(0x280, reinterpret_cast<Address>(&s));
    sensor.put(0x288, reinterpret_cast<Address>(&s + 1));
    for (unsigned rel : {0x83, 0x103}) {
        sub.put(0x14, rel);
        sub.put(0x68, 99u);
        assert(!d::quiet(w, actor.ptr()));
    }
    sub.put(0x14, 3u);
    sub.put(0x10, 3u << 28);
    Memory<0xe00> enemy;
    Memory<0xb0> enemy_weapons;
    Memory<0x220> place;
    Address gun = place.ptr();
    sub.put(0x18, enemy.ptr());
    enemy.put(0xdd8, enemy_weapons.ptr());
    enemy_weapons.put(0x30, reinterpret_cast<Address>(&gun));
    enemy_weapons.put(0x38, reinterpret_cast<Address>(&gun + 1));
    place.put(0xd0, 1);
    place.put(0xe0, actor.ptr());
    assert(!d::quiet(w, actor.ptr()));
    place.put(0xe0, Address{});
    assert(d::quiet(w, actor.ptr()));
}
void component_and_ownership() {
    Memory<0xc00> actor;
    Memory<0x50> components;
    Memory<0x340> gun, secondary;
    Memory<0x60> gun_name, second_name;
    gun_name.tag(0, "gun");
    second_name.tag(0, "gun2");
    gun.put(0x328, gun_name.ptr());
    gun.put(0x330, gun_name.ptr() + 0x60);
    secondary.put(0x328, second_name.ptr());
    secondary.put(0x330, second_name.ptr() + 0x60);
    actor.put(0xb90, components.ptr());
    actor.put(0xb98, components.ptr() + 0x50);
    components.put(0, gun.ptr());
    components.put(8, actor.ptr());
    components.put(0x24, 1u);
    components.put(0x28, secondary.ptr());
    components.put(0x30, actor.ptr());
    assert(!d::defanged(actor.ptr()));
    components.put(0x4c, 2u);
    assert(d::defanged(actor.ptr()));
    components.put(0x24, 0u);
    assert(!d::defanged(actor.ptr()));
    d::Proposal winner{};
    winner.row = 1;
    winner.rule = 2;
    winner.ammo.def = 3;
    for (bool overflow : {false, true}) {
        Memory<0x58> out;
        Memory<0x40> source;
        winner.ammo.record = overflow ? 0 : source.ptr();
        winner.ammo.stack = overflow ? 4 : 0;
        unsigned init = 0, assign = 0, release = 0;
        Address tmp{};
        d::handoff(
            out.ptr(), winner,
            [&](Address dst, Address item) {
                assert(overflow && item == 4 && !assign);
                tmp = dst;
                ++init;
            },
            [&](Address dst, Address from) {
                assert(dst == out.ptr() + 0x18 && from == (overflow ? tmp : source.ptr()) && !release);
                ++assign;
            },
            [&](Address from) {
                assert(from == tmp && assign);
                ++release;
            });
        assert(assign == 1 && init == unsigned(overflow) && release == unsigned(overflow));
        assert(pr::read<Address>(out.ptr()) == 1 && pr::read<Address>(out.ptr(), 8) == 2 &&
               pr::read<Address>(out.ptr(), 0x10) == 3);
    }
}
void lifecycle() {
    unsigned calls = 0, removed = 0;
    auto remove = [&](void* target) {
        assert(reinterpret_cast<Address>(target) == 0x100000 + 0x92b6f0);
        ++removed;
        return 0;
    };
    auto fail = [&](void* target, void*, void** out) {
        *out = target;
        return ++calls == 2 ? 1 : 0;
    };
    assert(!at::prepare_hooks(0x100000, "wrong", fail, remove) && !calls);
    assert(!at::prepare_hooks(0x100000, goh::target_prio::executable_sha256, fail, remove));
    assert(removed == 1 && !d::accepting && !d::original_volume && !pr::select_armor);
    calls = 0;
    assert(!at::prepare_hooks(0x100000, goh::target_prio::executable_sha256, fail, [](void*) { return 1; }));
    assert(d::staged && !d::accepting && !pr::select_armor);
    at::after_hooks_removed();
    auto create = [](void* target, void*, void** out) {
        *out = target;
        return 0;
    };
    assert(at::prepare_hooks(0x100000, goh::target_prio::executable_sha256, create, remove));
    assert(d::accepting && pr::select_armor && pr::rate_armor);
    assert(!at::prepare_hooks(0x100000, goh::target_prio::executable_sha256, create, remove));
    d::original_target = [](Address w, Address t, int option, int mode, int shots) {
        assert(at::inflight_count() == 1 && w == 13 && t == 17 && option == 3 && mode == 3 && shots == -1);
    };
    d::set_target(13, 17, 3, 3, -1);
    d::original_volume = [](Address out, at::Byte skip, Address w, Address t, Address tag, Address prev) {
        assert(at::inflight_count() == 1 && out == 2 && skip == 1 && w == 3 && t == 4 && tag == 5 && prev == 6);
    };
    d::select_volume(2, 1, 3, 4, 5, 6); // No admitted actor. Forward without touching dummy addresses.
    {
        d::Guard call;
        at::deactivate();
        assert(!d::accepting && at::inflight_count() == 1);
    }
    at::after_hooks_removed();
    assert(!at::inflight_count() && !d::original_target && !pr::select_armor && !pr::rate_armor);
}
#ifdef GOH_AT_TRAMPOLINES
void trampoline(const char* path, const std::array<unsigned char, 5>& prefix) {
    unsigned char entry[64]{}, copy[64]{}, detour{};
    FILE* fp = std::fopen(path, "rb");
    assert(fp && std::fread(entry, 1, sizeof entry, fp) == sizeof entry);
    std::fclose(fp);
    assert(!std::memcmp(entry, prefix.data(), prefix.size()));
    TRAMPOLINE t{};
    t.pTarget = entry;
    t.pTrampoline = copy;
    t.pDetour = &detour;
    assert(CreateTrampolineFunction(&t) && !t.patchAbove && t.nIP == 2);
    assert(t.oldIPs[1] == 5 && t.newIPs[1] == 5 && !std::memcmp(entry, copy, 5));
    assert(copy[5] == 0xff && copy[6] == 0x25);
    Address back{}, dst{};
    std::memcpy(&back, copy + 11, 8);
    std::memcpy(&dst, static_cast<unsigned char*>(t.pRelay) + 6, 8);
    assert(back == reinterpret_cast<Address>(entry + 5) && dst == reinterpret_cast<Address>(&detour));
}
#endif
int main(int argc, char** argv) {
    volumes();
    ammo();
    targets_and_fire();
    memory_guards();
    contacts_and_threats();
    component_and_ownership();
    lifecycle();
#ifdef GOH_AT_TRAMPOLINES
    assert(argc == 3);
    trampoline(argv[1], {0x4c, 0x89, 0x4c, 0x24, 0x20});
    trampoline(argv[2], {0x48, 0x89, 0x5c, 0x24, 0x10});
#else
    (void)argc;
    (void)argv;
#endif
    std::puts("Penetration AT fixtures passed.");
}
