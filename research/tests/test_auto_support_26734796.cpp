// Offline fixtures only. This program links no game code and installs no hooks.
#include "../../mods/auto_medic_module.hpp"
#include "../../mods/auto_resupply_module.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>

namespace s = goh::ai_support;
namespace med = goh::auto_medic;
namespace supply = goh::auto_resupply;
using s::Address;
using s::Byte;

template <std::size_t N> struct Memory {
    std::array<std::byte, N> bytes{};
    Address address() const {
        return reinterpret_cast<Address>(bytes.data());
    }
    template <class T> void put(unsigned offset, T value) {
        assert(offset + sizeof(value) <= N);
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
    void string(unsigned offset, std::string_view value) {
        auto length = value.size();
        if (length <= 15)
            std::memcpy(bytes.data() + offset, value.data(), value.size());
        else
            put(offset, reinterpret_cast<Address>(value.data()));
        put(offset + 0x10, length);
        put(offset + 0x18, std::max<std::size_t>(15, length));
    }
    template <class T, std::size_t M>
    void range(unsigned offset, const std::array<T, M>& values, std::size_t count = M) {
        assert(count <= M);
        put(offset, reinterpret_cast<Address>(values.data()));
        put(offset + 8, reinterpret_cast<Address>(values.data() + count));
    }
};
struct Human {
    Memory<0x1400> actor;
    Memory<0x600> brain, root;
    Memory<0x200> chassis, weaponry, agg, vit, manager, box;
    explicit Human(unsigned id) {
        actor.put(0x30, Byte{7});
        actor.put(0xC0, id);
        actor.put(0xB0, Address{700});
        actor.put(0xDB4, 0x100u);
        actor.put(0xDAA, std::uint16_t{3});
        actor.put(0xDC8, brain.address());
        actor.put(0xDE0, chassis.address());
        actor.put(0xDD8, weaponry.address());
        actor.put(0x578, agg.address());
        agg.put(0x18, vit.address());
        agg.put(0x28, manager.address());
        manager.put(0xA0, box.address());
        box.put(0x50, actor.address());
        brain.put(0, s::base + 0x110FF88);
        brain.put(0x38, actor.address());
        brain.put(0xE0, root.address());
        root.put(0, s::base + 0x1110CD0);
        root.put(0x30, brain.address());
    }
};
void authority() {
    Human h(10);
    const auto a = h.actor.address();
    assert(s::autonomous(a, true) && s::free_slot(a));
    assert(med::donor_available(a));
    // Passive donors remain eligible under Hold Fire / Hold Position (only the medic moves).
    h.brain.put(0x398, (1u << 10u) | (1u << 12u));
    assert(med::donor_available(a));
    assert(!s::autonomous(a, true)); // Hold Fire still blocks medics
    assert(s::autonomous(a, false)); // Hold Position (move==1) allowed for resupply (with return-to-post)
    assert(s::hold_position(h.brain.address()));
    // Player-owned units (control == 1) are eligible; invalid controls (2, 3) are rejected.
    h.brain.put(0x398, 1u);
    assert(s::autonomous(a, true) && s::autonomous(a, false) && med::donor_available(a));
    assert(!s::hold_position(h.brain.address()));
    h.brain.put(0x398, 0u);
    for (unsigned count = 1; count <= 63; ++count)
        assert(!s::policy_allows(0, count, true));
    assert(!s::policy_allows(0, 1u << 28u, false));
    assert(!s::policy_allows(0, 1u << 20u, true));
    // Bit 24 (inside current-order callback) MUST NOT veto policy_allows (called from permit() during Exec),
    // while free_slot MUST reject installing new orders when bit 24 is set.
    assert(s::policy_allows(0, 1u << 24u, true) && s::policy_allows(1u, 1u << 24u, false));
    h.brain.put(0x39C, 1u << 24u);
    assert(s::autonomous(a, true) && !s::free_slot(a));
    h.brain.put(0x39C, 0u);
    assert(s::policy_allows(0u, 0, true) && s::policy_allows(1u, 0, true));
    assert(!s::policy_allows(2u, 0, true) && !s::policy_allows(3u, 0, true));
    // Move modes: 0 (free), 1 (hold, with return-to-post), and 2 (zone) allowed; 3 (invalid) rejected.
    assert(s::policy_allows(0u << 12u, 0, false) && s::policy_allows(1u << 12u, 0, false) &&
           s::policy_allows(2u << 12u, 0, false));
    assert(s::policy_allows(1u << 12u, 0, true));
    assert(!s::policy_allows(3u << 12u, 0, false) && !s::policy_allows(3u << 12u, 0, true));
    assert(!s::policy_allows(1u << 10u, 0, true) && s::policy_allows(2u << 10u, 0, true));
    Memory<0x80> native_order;
    native_order.put(0, s::base + 0x110D278);
    h.brain.put(0xF0, native_order.address());
    assert(!s::free_slot(a));
    native_order.put(0, s::base + 0x110D490);
    native_order.put(8, a + 0xAF0);
    assert(s::free_slot(a));
    native_order.put(0x20, 1u);
    assert(!s::free_slot(a));
    h.brain.put(0xF0, Address{});
    h.root.put(0x568, Address{123});
    assert(!s::autonomous(a, true));
    h.root.put(0x568, Address{});
    h.chassis.put(0x18, 1);
    assert(!s::autonomous(a, false));
    h.chassis.put(0x18, 0);
    h.weaponry.put(0x70, Byte{2});
    assert(!s::autonomous(a, true));
    h.weaponry.put(0x70, Byte{});
    h.vit.put(0x174, 2u);
    assert(!s::autonomous(a, true));
    h.vit.put(0x174, 0u);
    h.brain.put(0, Address{987});
    assert(!s::autonomous(a, false));
    h.brain.put(0, s::base + 0x101FD68);
    h.actor.put(0x30, Byte{11});
    assert(!med::donor_available(a));
    Memory<0x180> crew;
    Memory<0x28> hull;
    Memory<0x400> hull_definition;
    Memory<0x60> hull_tag;
    hull_tag.string(0x40, "body");
    hull_definition.put(0x328, hull_tag.address());
    hull_definition.put(0x330, hull_tag.address() + 0x60);
    hull.put(0, hull_definition.address());
    hull.put(8, a);
    h.actor.put(0xB90, hull.address());
    h.actor.put(0xB98, hull.address() + 0x28);
    h.actor.put(0x568, crew.address());
    crew.put(0x10, crew.address() + 0x100);
    crew.put(0x18, crew.address() + 0x108);
    assert(med::donor_available(a));
    hull.put(0x24, Byte{2});
    assert(!med::donor_available(a));
}
void carried_smoke() {
    Human donor(40);
    Memory<0x1200> definition;
    Memory<0xc0> key;
    key.string(0x40, "grenade");
    key.string(0xa0, "rdg1_smoke");
    definition.put(0xB80, key.address());
    definition.put(0xB88, key.address() + 0xc0);
    Memory<0xe8> stack, second;
    const auto b = donor.box.address(), a = donor.actor.address();
    stack.put(0x30, b);
    stack.put(0x40, definition.address());
    stack.put(0x48, 1);
    second = stack;
    std::array items{stack.address(), second.address()};
    donor.box.range(0x10, items, 1);
    // Non-medic donors may give their 1 smoke grenade; medic donors (require_extra=true) must keep 1.
    assert(med::spare(a, false) == stack.address());
    assert(!med::spare(a, true));
    stack.put(0x48, 2);
    assert(med::spare(a, true) == stack.address());
    key.string(0xa0, "m15_wp");
    assert(!med::spare(a, false) && !med::spare(a, true));
    key.string(0xa0, "m24_smoke");
    stack.put(0x48, 1);
    donor.box.range(0x10, items);
    assert(med::spare(a, true) == stack.address());
    second.put(0x38, Address{1});
    assert(!med::spare(a, true) && med::spare(a, false) == stack.address());
    second.put(0x38, Address{});
    second.put(0x18, Address{1});
    assert(!med::spare(a, true) && med::spare(a, false) == stack.address());
    stack.put(0x50, Address{1});
    assert(!med::spare(a, true) && !med::spare(a, false));
}
void observations() {
    Human h(12);
    Memory<0x400> sensor;
    Memory<0x80> subject;
    auto a = h.actor.address();
    sensor.put(0x260, a);
    h.actor.put(0xDD0, sensor.address());
    h.actor.put(0x74, s::Vec3{100, 100, 0});
    s::World w{s::base, 1, 100, 10, 10};
    s::Vec3 point{100, 100, 0};
    std::array rows{subject.address()};
    sensor.range(0x280, rows);
    subject.put(0x14, 2u);
    subject.put(0x10, 1u << 28u);
    subject.put(0x28, s::Vec3{110, 100, 0});
    auto observe = [&] {
        s::Danger d{};
        s::observe(w, a, point, 50, d);
        return d;
    };
    assert(observe().observed && !observe().unsafe);
    subject.put(0x64, 50u);
    assert(observe().unsafe && observe().direction);
    subject.put(0x64, 49u);
    assert(!observe().unsafe);
    subject.put(0x10, 3u << 28u);
    assert(observe().unsafe);
    subject.put(0x28, s::Vec3{900, 900, 0});
    assert(!observe().unsafe);
    subject.put(0x14, 0x102u);
    subject.put(0x68, 99u);
    assert(observe().unsafe);
    subject.put(0x6c, Byte{1});
    assert(!observe().unsafe);
    sensor.put(0x2F4, 61u);
    assert(observe().unsafe);
    // Return-to-post overrun check ignores distant incoming fire when no hostile is near the post.
    assert(!s::hostile_near(w, a, {100, 100, 0}, 50));
    subject.put(0x6c, Byte{0});
    subject.put(0x28, s::Vec3{105, 100, 0});
    subject.put(0x10, 3u << 28u);
    assert(s::hostile_near(w, a, {100, 100, 0}, 50));
    assert(!s::hostile_near(w, a, {100, 100, 0}, 4));
    point = {1000, 1000, 0};
    assert(!observe().observed && !observe().unsafe);
}
void scene_and_cloud() {
    Human medic(8), patient(9);
    Memory<0x200> scene, old_cloud, new_cloud;
    std::array objects{medic.actor.address(), patient.actor.address(), old_cloud.address(), new_cloud.address()};
    for (auto* cloud : {&old_cloud, &new_cloud}) {
        cloud->put(0x30, Byte{1});
        cloud->put(0xC0, ~0u);
        cloud->put(0x74, s::Vec3{40, 0, 0});
        cloud->string(0xF0, "smoke_volume_cylinder");
    }
    s::World w{s::base, scene.address(), 20, 1, 10};
    scene.range(0x48, objects, 3);
    med::Job j{};
    j.endpoint = {40, 0, 0};
    assert(med::cloud(w, j, true) && j.previous_count == 1);
    assert(!med::cloud(w, j, false));
    scene.range(0x48, objects);
    assert(med::cloud(w, j, false));
    new_cloud.put(0x38, 0x200u);
    assert(!med::cloud(w, j, false));
    new_cloud.put(0x38, 0u);
    new_cloud.string(0xF0, "m15_wp");
    assert(!med::cloud(w, j, false));
    auto key = s::identity(patient.actor.address());
    assert(s::resolve(w, key) == patient.actor.address());
    patient.actor.put(0xC0, 10u);
    assert(!s::resolve(w, key));
    s::Danger threat{true, true, {60, 0, 0}, true};
    assert(med::screens(w, {30, 0, 0}, j.endpoint, threat));
    assert(med::screens(w, {30, 6, 0}, j.endpoint, threat));
    assert(!med::screens(w, {45, 0, 0}, j.endpoint, threat));
    assert(!med::screens(w, {30, 9, 0}, j.endpoint, threat));
}
void supply_policy() {
    supply::Trip detour{};
    detour.previous = {3, 4, 0};
    detour.travelled = 55;
    assert(supply::distance_travelled(detour, {0, 0, 0}) == supply::travel_metres);
    assert(supply::distance_travelled(detour, {0, -1, 0}) > supply::travel_metres);
    assert(supply::affordable(495, 500, 5) && !supply::affordable(496, 500, 5));
    assert(supply::affordable(999, 0, 4) && !supply::affordable(INT_MAX, INT_MAX, 1));
    assert(!supply::affordable(-1, 0, 1) && !supply::affordable(1, 2, -1));
    std::array<float, 10> weights{};
    weights[3] = 2;
    assert(supply::uncontested(1, 0, 'd', weights.data()));
    assert(!supply::uncontested(.99f, 0, 'd', weights.data()));
    assert(!supply::uncontested(1, -.01f, 'd', weights.data()));
    assert(!supply::uncontested(1, 0, 'a', weights.data()));
    weights[7] = .01f;
    assert(!supply::uncontested(1, 0, 'd', weights.data()));
    weights[7] = std::numeric_limits<float>::quiet_NaN();
    assert(!supply::uncontested(1, 0, 'd', weights.data()));
    Human h(61);
    Memory<0xe8> gun;
    supply::Gun selected{123, gun.address(), 0, 4, true};
    assert(supply::ammo_count(h.actor.address(), {}) == -1);
    assert(supply::ammo(h.actor.address(), {}) == supply::Ammo::unknown);
    // eItem+0x58 is magazine capacity (remains 47 on an empty DP-27); live loaded count is eItem+0xDC (0x7DC450).
    gun.put(0x50, Address{23});
    gun.put(0x58, 47);
    gun.put(0xDC, 0);
    assert(supply::ammo_count(h.actor.address(), selected) == 0);
    assert(supply::ammo(h.actor.address(), selected) == supply::Ammo::empty);
    gun.put(0xDC, 7);
    assert(supply::ammo_count(h.actor.address(), selected) == 7);
    assert(supply::ammo(h.actor.address(), selected) == supply::Ammo::available);
    Memory<0x200> fire;
    std::array exts{fire.address()};
    h.agg.range(0, exts);
    fire.put(0, s::base + 0x10FD478);
    fire.put(0x18, h.actor.address());
    Memory<0x60> props;
    props.string(0x40, "burning");
    h.actor.put(0x4E8, props.address());
    h.actor.put(0x4F0, props.address() + 0x60);
    assert(supply::serviceable(h.actor.address()));
    fire.put(0xa0, 1.f);
    assert(!supply::serviceable(h.actor.address()));
    fire.put(0xa0, 0.f);
    props.string(0x40, "destroyed");
    assert(!supply::serviceable(h.actor.address()));
}
void structure_and_template() {
    Human owner(2), recipient(3);
    Memory<0x1400> child;
    Memory<0xB80> point;
    Memory<0x400> place;
    Memory<0x200> placer, scene;
    child.put(0x30, Byte{1});
    child.put(0xC0, 11u);
    std::array objects{owner.actor.address(), child.address()};
    scene.range(0x48, objects);
    std::array extensions{point.address()};
    owner.agg.range(0, extensions);
    point.put(0, s::base + 0x10FBFF0);
    point.put(0x18, owner.actor.address());
    owner.actor.put(0x568, placer.address());
    placer.put(0x28, place.address());
    placer.put(0x30, place.address() + 0x298);
    Memory<0x200> child_components;
    child.put(0x578, child_components.address());
    s::World world{s::base, scene.address(), 10, 1, 1};
    assert(!supply::parent_flag(world, child.address()));
    place.put(0x280, child.address());
    assert(supply::parent_flag(world, child.address()) == owner.actor.address());
    owner.root.put(0, Address{}); // The flag proof only uses the actual structure relationship.
    Memory<0x100> header, branch, leaf;
    Address tree = header.address();
    header.put(8, branch.address());
    header.put(0x19, Byte{1});
    branch.string(0x20, "tank");
    branch.put(0, leaf.address());
    branch.put(0x10, tree);
    branch.put(0x70, Address{123});
    leaf.string(0x20, "gun");
    leaf.put(0, tree);
    leaf.put(0x10, tree);
    leaf.put(0x70, Address{456});
    auto saved = s::base;
    s::base = reinterpret_cast<Address>(&tree) - 0x159BE88;
    recipient.actor.string(0xF0, "gun");
    assert(supply::cached_template(recipient.actor.address()) == 456);
    recipient.actor.string(0xF0, "tank");
    assert(supply::cached_template(recipient.actor.address()) == 123);
    recipient.actor.string(0xF0, "truck");
    assert(!supply::cached_template(recipient.actor.address()));
    branch.put(0x70, Address{});
    recipient.actor.string(0xF0, "tank");
    assert(!supply::cached_template(recipient.actor.address()));
    s::base = saved;
}
void lifecycle() {
    Human actor(1), patient(2), source(3);
    med::reset();
    supply::reset();
    s::World w{s::base, 11, UINT_MAX - 2, 1, 1};
    med::refresh(w);
    supply::refresh(w);
    auto& m = med::jobs[0];
    m.medic = s::identity(actor.actor.address());
    m.patient = s::identity(patient.actor.address());
    m.donor = s::identity(source.actor.address());
    m.start = w.now;
    med::phase(m, w, med::Phase::fetch, 4);
    assert(med::occupied(source.actor.address()) && med::patient_claimed(patient.actor.address()));
    w.now = 1;
    med::refresh(w);
    assert(m.phase == med::Phase::cooldown);
    assert(!med::occupied(source.actor.address()) && med::patient_claimed(patient.actor.address()));
    w.now = 13;
    med::refresh(w);
    assert(!med::patient_claimed(patient.actor.address()));
    for (unsigned i = 0; i < supply::max_inbound_per_source; ++i) {
        auto& ti = supply::trips[i];
        ti.actor = s::identity(actor.actor.address());
        ti.dest.source = s::identity(source.actor.address());
        ti.phase = supply::Phase::wait;
        ti.since = w.now;
        ti.timeout = 12;
        assert(supply::claimed_count(source.actor.address()) == i + 1);
        assert(supply::claimed(source.actor.address()) == (i + 1 >= supply::max_inbound_per_source));
    }
    // Returning units release source claims while remaining occupied until arrival.
    supply::trips[0].phase = supply::Phase::returning;
    assert(supply::claimed_count(source.actor.address()) == supply::max_inbound_per_source - 1);
    assert(!supply::claimed(source.actor.address()));
    assert(supply::occupied(actor.actor.address()));
    supply::trips[0].phase = supply::Phase::wait;
    // Medic return-to-post uses its own phase wait instead of the 70 s job start cap.
    med::Job ret_job{};
    ret_job.start = 0;
    ret_job.phase = med::Phase::returning;
    ret_job.since = 80;
    ret_job.wait = 35;
    s::World w_ret{s::base, 11, 90, 1, 1};
    assert(!med::expired(w_ret, ret_job));
    w_ret.now = 115;
    assert(med::expired(w_ret, ret_job));
    auto& t = supply::trips[0];
    w.now += 12;
    supply::refresh(w);
    assert(t.phase == supply::Phase::cooldown && !supply::claimed(source.actor.address()));
    assert(supply::suppressed(w, actor.actor.address(), source.actor.address()));
    assert(!supply::suppressed(w, patient.actor.address(), source.actor.address()));
    w.scene = 12;
    supply::refresh(w);
    assert(!t.actor.ptr);
    auto& watch = s::watches[0];
    watch.generation = 1;
    watch.order = 123;
    s::watched_count = 1;
    s::OrderRef old{&watch, 1};
    assert(old.get() == 123);
    auto destroyed = [](Address o, unsigned) -> Address { return o; };
    assert(s::destroy(destroyed, 123, 1) == 123 && !old.get() && s::quiescent());
    watch.generation = 2;
    watch.order = 123;
    s::watched_count = 1;
    assert(!old.get());
    assert(s::guarded([](Address, int) { return 77; }, 123, 0) == 2);
    assert(s::guarded([](Address, int) { return 77; }, 321, 0) == 77);
    s::destroy(destroyed, 123, 1);
}
void transfer_ownership() {
    Human donor(1), medic(2);
    Memory<0x200> op;
    std::array operations{op.address()};
    donor.manager.range(0xA8, operations);
    op.put(0, s::base + 0x10FF790);
    op.put(0xF0, 37);
    op.put(0x88, donor.actor.address());
    med::Job j{};
    auto& mark = med::transfers[0];
    mark.generation = 42;
    mark.operation = op.address();
    med::transfer_count = 1;
    j.donor = s::identity(donor.actor.address());
    j.medic = s::identity(medic.actor.address());
    j.operation = op.address();
    j.action = 37;
    j.transfer = &mark;
    j.transfer_serial = 42;
    assert(med::queued(j, donor.actor.address()) == op.address());
    op.put(0xF0, 38);
    assert(!med::queued(j, donor.actor.address()));
    op.put(0xF0, 37);
    mark.generation = 43;
    assert(!med::queued(j, donor.actor.address()));
    mark.generation = 42;
    med::original_transfer_destroy = [](Address o, unsigned) -> Address { return o; };
    assert(med::transfer_destroy(medic.actor.address(), 1) == medic.actor.address());
    assert(med::queued(j, donor.actor.address()) == op.address());
    med::transfer_destroy(op.address(), 1);
    assert(!med::queued(j, donor.actor.address()) && med::quiescent());
}
void hook_rollback() {
    s::base = 0;
    unsigned made = 0, removed = 0;
    auto create = [&](void*, void*, void** original) {
        *original = nullptr;
        return ++made == 3 ? 1 : 0;
    };
    auto remove = [&](void*) {
        ++removed;
        return 0;
    };
    assert(!s::prepare_hooks(100, "wrong", true, create, remove) && made == 0);
    assert(!s::prepare_hooks(100, s::executable_sha256, false, create, remove) && made == 0);
    assert(!s::prepare_hooks(100, s::executable_sha256, true, create, remove));
    assert(made == 3 && removed == 2 && !s::base && s::actor_tls == TLS_OUT_OF_INDEXES);
    made = removed = 0;
    assert(!med::prepare_smoke_hooks(100, s::executable_sha256, true, create, remove));
    assert(made == 3 && removed == 2 && !med::release_installed && !med::original_transfer_destroy);
    unsigned replacements = 0;
    auto accept = [&](void* entry, void* hook, void** trampoline) {
        *trampoline = nullptr;
        auto rva = reinterpret_cast<Address>(entry) - 100;
        if (rva == 0x8313B0 && hook == reinterpret_cast<void*>(&med::release)) ++replacements;
        if (rva == 0x903FE0 && hook == reinterpret_cast<void*>(&med::shot_exec)) ++replacements;
        if (rva == 0x7E94F0 && hook == reinterpret_cast<void*>(&med::transfer_destroy)) ++replacements;
        return 0;
    };
    assert(med::prepare_smoke_hooks(100, s::executable_sha256, true, accept, remove));
    assert(replacements == 3 && med::release_installed && med::quiescent());
    med::after_hooks_removed();
}
int main() {
    static_assert(!med::runtime_allowed && !supply::runtime_allowed);
    s::base = 0x140000000ull;
    authority();
    carried_smoke();
    observations();
    scene_and_cloud();
    supply_policy();
    structure_and_template();
    lifecycle();
    transfer_ownership();
    hook_rollback();
    std::puts("Auto-Medic and Auto-Resupply offline fixtures passed");
}
