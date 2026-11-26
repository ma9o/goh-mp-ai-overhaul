#pragma once
#include "config_and_log.hpp"
#include "game_runtime.hpp"

// Shared by the existing order and ranking hooks in the combat, tank and infantry TUs.
namespace goh::engine_fixes {
using runtime::Address;
using runtime::Byte;
using runtime::read;
inline runtime::HookGate gate{};
inline Address image{};
struct Ops {
    Address (*serial_actor)(){};
    bool (*host_main)(Address) = &runtime::is_host_main_thread;
    Address (*resolve)(Address image, Address rva) = [](Address base, Address rva) { return base + rva; };
};
inline Ops ops{};
template <class Fn> inline Fn function(Address rva) {
    return reinterpret_cast<Fn>(ops.resolve(image, rva));
}
inline Address current() {
    if (!gate.active() || !config::active().modules.engine_fixes || !ops.serial_actor) return 0;
    const auto actor = ops.serial_actor();
    return actor && ops.host_main(image) && runtime::actor_ai_allowed(actor) ? actor : 0;
}
inline bool authorized(Address actor, Address brain, bool human_only = true) {
    if (!actor || current() != actor || !brain || read<Address>(actor, 0xdc8) != brain ||
        read<Address>(brain, 0x38) != actor)
        return false;
    const auto vt = read<Address>(brain);
    const bool human = vt == image + runtime::table().vt.human_brain;
    if (!human && (human_only || vt != image + runtime::table().vt.vehicle_brain)) return false;
    const auto type = read<Byte>(actor, 0x30);
    return (read<unsigned>(brain, 0x398) & 3u) <= 1u && (type & 2) && (type & (human ? 4 : 8)) &&
           !(read<unsigned>(actor, 0x38) & 0xa20u) && read<Address>(actor, 0xde0) && read<Address>(actor, 0xdd8) &&
           !runtime::is_inhibited_or_direct_control(actor, brain);
}
inline bool firing_permitted(Address brain) {
    const auto mode = (read<unsigned>(brain, 0x398) >> 10) & 3;
    return mode == 0 || (mode == 2 && (read<unsigned>(brain, 0x39c) & (1u << 18)));
}
inline void reset_dry_timer(Address child, unsigned phase) {
    if (phase != 1) return;
    const auto actor = current();
    if (!actor) return;
    const auto brain = read<Address>(actor, 0xdc8);
    if (read<Address>(child, 8) != actor + 0xaf0 || !authorized(actor, brain, false) ||
        (read<unsigned>(brain, 0x398) & 3) != 1 || !firing_permitted(brain))
        return;
    const auto weapon = function<Address (*)(Address)>(runtime::table().fn.weapon_select)(read<Address>(actor, 0xdd8));
    const auto charger = read<Address>(weapon, 0xb0);
    if (charger && ((read<Byte>(charger, 0x14) & 0x14) || !function<Byte (*)(Address)>(0x820c50)(charger)))
        runtime::write<int>(child, 0xc4, 0);
}
} // namespace goh::engine_fixes
