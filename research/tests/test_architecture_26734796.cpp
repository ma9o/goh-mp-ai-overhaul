// Offline ASan/UBSan test suite covering the shared architecture headers:
//   - mods/dynamic_store.hpp    (GrowableTable, SparseTable, StableTable, HeapAtomicArray)
//   - mods/config_and_log.hpp   (goh_ai.ini parsing, clamping, path hygiene, log level gating)
//   - mods/game_runtime.hpp     (PE32+ parsing, masked pattern scan, live MSVC x64 RTTI vtable resolver, HookGate)
//   - mods/actor_coordinator.hpp (priority arbitration, preemption callbacks, scene/clock invalidation)
//   - mods/game_classifiers.hpp (unified classifiers, WP deny-list + INI smoke extensibility, live carried AT scan)

#include "../../mods/ai_modules.hpp"
#include "actor_coordinator.hpp"
#include "config_and_log.hpp"
#include "dynamic_store.hpp"
#include "game_classifiers.hpp"
#include "game_runtime.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using goh::runtime::Address;

void test_dynamic_stores() {
    // 1. GrowableTable: lazy allocation, growth past InitialCap, cap enforcement, in-place fill, release.
    goh::store::GrowableTable<int, 4, 10> gt;
    assert(gt.empty() && gt.size() == 0);
    int* s0 = gt.append_slot();
    assert(s0 && gt.size() == 4);
    *s0 = 11;
    gt[3] = 44;
    // Grow from 4 up to 10 via append_slot.
    for (int i = 4; i < 10; ++i) {
        int* s = gt.append_slot();
        assert(s != nullptr);
        *s = i * 10;
    }
    assert(gt.size() == 10);
    assert(gt.append_slot() == nullptr); // Hit MaxCap cleanly.
    assert(gt[0] == 11 && gt[3] == 44 && gt[9] == 90);
    int& ref0 = gt[0];
    gt.fill(0);
    assert(ref0 == 0 && gt.size() == 10); // In-place fill keeps references valid.
    gt.release();
    assert(gt.empty());

    // 2. SparseTable: O(1) sparse keys without inserting on find().
    goh::store::SparseTable<int, std::uint16_t> st;
    assert(st.find(42) == nullptr && st.empty());
    st[17] = 100;
    st[65535] = 200;
    assert(st.size() == 2);
    assert(st.find(17) && *st.find(17) == 100);
    assert(st.find(65535) && *st.find(65535) == 200);
    assert(st.find(18) == nullptr && st.size() == 2);
    st.fill(0);
    assert(st.empty());
    st.release();

    // 3. StableTable: pointer stability across growth with non-movable std::atomic members.
    struct AtomicSlot {
        std::atomic<std::uint64_t> tag{0};
        int payload{0};
    };
    goh::store::StableTable<AtomicSlot, 4, 32> stable;
    AtomicSlot* p0 = stable.append_slot();
    assert(p0 && stable.size() == 4);
    p0->tag.store(0xDEADBEEF);
    p0->payload = 7;
    AtomicSlot* p3 = &stable[3];
    p3->payload = 99;
    for (int i = 4; i < 32; ++i) {
        AtomicSlot* slot = stable.append_slot();
        assert(slot != nullptr);
        slot->payload = i;
    }
    assert(stable.append_slot() == nullptr);
    assert(&stable[0] == p0 && p0->tag.load() == 0xDEADBEEF && p0->payload == 7);
    assert(&stable[3] == p3 && p3->payload == 99);
    stable.release();
    assert(stable.empty());

    // try_append_slot SFINAE helper: nullptr for fixed std::array, allocates for StableTable.
    std::array<int, 2> fixed{1, 2};
    assert(goh::store::try_append_slot(fixed) == nullptr);
    assert(goh::store::try_append_slot(stable) != nullptr);
    stable.release();

    // 4. HeapAtomicArray: wait-free query returns nullptr before allocate() and after release().
    goh::store::HeapAtomicArray<std::uint32_t, 64> atomic_arr;
    assert(atomic_arr.try_get() == nullptr);
    atomic_arr.allocate();
    assert(atomic_arr.try_get() != nullptr);
    (*atomic_arr.try_get())[12].store(555u, std::memory_order_release);
    assert(atomic_arr[12].load(std::memory_order_acquire) == 555u);
    atomic_arr.clear_values();
    assert((*atomic_arr.try_get())[12].load(std::memory_order_acquire) == 0u);
    atomic_arr.release();
    assert(atomic_arr.try_get() == nullptr);
}

void test_benchmark_player_mask() {
    using namespace goh::config;
    using goh::runtime::actor_ai_allowed;
    constexpr auto all = UINT32_MAX;
    assert(parse_ini("").ai_player_mask == all);
    assert(parse_ini("[modules]\nai_player_mask=0").ai_player_mask == all);
    assert(parse_ini("[testing]\nai_player_mask=10").ai_player_mask == 10u);
    assert(parse_ini("[testing]\nai_player_mask=0X80000002 ; high bit").ai_player_mask == 0x80000002u);
    for (const char* invalid : {"", "-", "-1", "0x", "nope", "2junk", "4294967296", "0x10000000000000000"}) {
        assert(parse_ini(std::string("[testing]\nai_player_mask=1\nai_player_mask=") + invalid).ai_player_mask == all);
    }
    alignas(8) std::array<std::uint8_t, 0xDB0> actor{};
    const auto address = reinterpret_cast<Address>(actor.data());
    reset_defaults();
    assert(player_ai_enabled(255) && actor_ai_allowed(0));
    active() = parse_ini("[testing]\nai_player_mask=0x80000002");
    assert(!actor_ai_allowed(0) && !actor_ai_allowed(1));
    for (std::uint8_t player : {0, 1, 2, 31, 32, 255}) {
        actor[0xDA8] = player;
        const bool expected = player == 1 || player == 31;
        assert(player_ai_enabled(player) == expected && actor_ai_allowed(address) == expected);
    }
    active() = parse_ini("[testing]\nai_player_mask=0");
    actor[0xDA8] = 0;
    assert(!actor_ai_allowed(address) && !player_ai_enabled(0));
    assert(parse_ini("").sim_speed == 0.0f);
    assert(parse_ini("[testing]\nsim_speed=off").sim_speed == 0.0f);
    assert(parse_ini("[testing]\nsim_speed=10").sim_speed == 10.0f);
    assert(parse_ini("[testing]\nsim_speed=99").sim_speed == 20.0f);
    assert(parse_ini("[testing]\nsim_speed=0.05").sim_speed == 0.25f);
    assert(parse_ini("[testing]\nsim_speed=10\nsim_speed=0").sim_speed == 0.0f);

    const auto def_test = parse_ini("");
    assert(!def_test.skip_intro && !def_test.auto_dismiss_loading && !def_test.hot_reload &&
           def_test.restart_seq == 0 && def_test.auto_mission.empty());
    const auto auto_cfg = parse_ini("[testing]\nskip_intro=true\nauto_dismiss_loading=1\nhot_reload=yes\n"
                                    "restart_seq=42\nauto_mission=single/00-bootcamp/02_vehicle-control:0\n");
    assert(auto_cfg.skip_intro && auto_cfg.auto_dismiss_loading && auto_cfg.hot_reload && auto_cfg.restart_seq == 42u &&
           auto_cfg.auto_mission == "single/00-bootcamp/02_vehicle-control:0");
    assert(parse_ini("[testing]\nauto_mission=single/00-bootcamp/02_vehicle-control:0\nauto_mission=off")
               .auto_mission.empty());
    for (const char* bad : {"", "noslash", "/leading/slash", "trailing/slash/", "a//b", "../escape/m", "bad path/m"}) {
        assert(parse_ini(std::string("[testing]\nauto_mission=") + bad).auto_mission.empty());
    }
    reset_defaults();
}

void test_config_and_log() {
    using namespace goh::config;

    const std::string ini = "# Comment line\r\n"
                            "; Semicolon comment\r\n"
                            "[logging]\r\n"
                            "level = verbose\r\n"
                            "flush_ms = 250\r\n"
                            "file = ../escape.log\r\n" // Must be rejected by path hygiene!
                            "\r\n"
                            "[modules]\r\n"
                            "grenade_dodge = false\r\n"
                            "soft_cover_lof = 0\r\n"
                            "attack_detour = no\r\n"
                            "target_prioritization = off\r\n"
                            "close_grenade = true\r\n"
                            "penetration_at = 1\r\n"
                            "auto_smoke = yes\r\n"
                            "auto_medic = on\r\n"
                            "auto_resupply = false\r\n"
                            "tactical_infantry = false\r\n"
                            "tactical_objectives = false\r\n"
                            "tank_crusher = false\r\n"
                            "tank_auto_repair = false\r\n"
                            "tank_crew_fill = false\r\n"
                            "internal_crew_swap = false\r\n"
                            "nontank_crew_fill = false\r\n"
                            "\r\n"
                            "[tuning]\r\n"
                            "grenade_detect_radius_m = 30.0\r\n"
                            "medic_search_radius_m = 9999.0\r\n" // Out of [5, 100] -> clamped or kept default
                            "resupply_search_radius_m = 80.0\r\n"
                            "objectives_pursue_radius_m = 60.0\r\n"
                            "objectives_cap_radius_m = 45.0\r\n"
                            "crew_fill_emit_backoff_s = 12.0\r\n"
                            "smoke_vehicle_cooldown_s = 30.0\r\n"
                            "smoke_infantry_cooldown_s = 10.0\r\n"
                            "soft_cover_log_interval_ms = 2500\r\n"
                            "\r\n"
                            "[smoke]\r\n"
                            "extra_grenades = Mod_Smoke_Canister, m15_wp , mod_smoke_canister\r\n"
                            "extra_programs = custom_smoke_discharger, 2in_wp\r\n";

    Settings s = parse_ini(ini);

    assert(s.log_level == goh::log::Level::verbose);
    assert(s.log_flush_ms == 250u);
    assert(s.log_filename == "goh_ai.log"); // Rejected "../escape.log"!

    const auto fixes_default = parse_ini("");
    assert(fixes_default.modules.engine_fixes && fixes_default.modules.lull_reload);
    assert(fixes_default.tuning.lull_reload_ratio == .35f);
    assert(fixes_default.tuning.recrew_search_radius_m == 28.f &&
           fixes_default.tuning.scavenge_search_radius_m == 16.f);
    const auto low = parse_ini("[tuning]\nrecrew_search_radius_m=-1\nscavenge_search_radius_m=0");
    const auto high = parse_ini("[tuning]\nrecrew_search_radius_m=55\nscavenge_search_radius_m=40");
    const auto adjusted = parse_ini("[tuning]\nrecrew_search_radius_m=20.5\nscavenge_search_radius_m=14");
    assert(low.tuning.recrew_search_radius_m == 8.f && low.tuning.scavenge_search_radius_m == 6.f);
    assert(high.tuning.recrew_search_radius_m == 50.f && high.tuning.scavenge_search_radius_m == 35.f);
    assert(adjusted.tuning.recrew_search_radius_m == 20.5f && adjusted.tuning.scavenge_search_radius_m == 14.f);
    const auto fixes_off = parse_ini("[modules]\nengine_fixes=off\nlull_reload=false\n[tuning]\nlull_reload_ratio=-1");
    assert(!fixes_off.modules.engine_fixes && !fixes_off.modules.lull_reload &&
           fixes_off.tuning.lull_reload_ratio == .1f);
    assert(parse_ini("[tuning]\nlull_reload_ratio=8").tuning.lull_reload_ratio == .75f);
    assert(parse_ini("[tuning]\nlull_reload_ratio=nan").tuning.lull_reload_ratio == .35f);
    for (const auto& [name, toggle] : std::array<std::pair<const char*, bool ModuleToggles::*>, 12>{{
             {"nontank_auto_repair", &ModuleToggles::nontank_auto_repair},
             {"recrew_weapons", &ModuleToggles::recrew_weapons},
             {"battlefield_scavenge", &ModuleToggles::battlefield_scavenge},
             {"advance_responsive", &ModuleToggles::advance_responsive},
             {"attack_order_polish", &ModuleToggles::attack_order_polish},
             {"hit_the_dirt", &ModuleToggles::hit_the_dirt},
             {"crush_avoidance", &ModuleToggles::crush_avoidance},
             {"fast_weapon_swap", &ModuleToggles::fast_weapon_swap},
             {"grenade_ff_guard", &ModuleToggles::grenade_ff_guard},
             {"safe_disembark", &ModuleToggles::safe_disembark},
             {"transport_unload", &ModuleToggles::transport_unload},
             {"squad_logistics", &ModuleToggles::squad_logistics},
         }}) {
        assert(Settings{}.modules.*toggle);
        const auto settings = parse_ini(std::string("[modules]\n") + name + "=off");
        assert(!(settings.modules.*toggle));
    }
    assert(parse_ini("").tuning.squad_share_radius_m == 18.f);
    assert(parse_ini("[tuning]\nsquad_share_radius_m=4").tuning.squad_share_radius_m == 6.f);
    assert(parse_ini("[tuning]\nsquad_share_radius_m=25").tuning.squad_share_radius_m == 25.f);
    assert(parse_ini("[tuning]\nsquad_share_radius_m=42").tuning.squad_share_radius_m == 40.f);
    assert(parse_ini("[tuning]\nsquad_share_radius_m=nan").tuning.squad_share_radius_m == 18.f);
    // Valid safe filename is accepted.
    Settings s_name = parse_ini("[logging]\nfile = custom_ai_26734796.log\nlevel = error\n");
    assert(s_name.log_filename == "custom_ai_26734796.log");
    assert(s_name.log_level == goh::log::Level::error);

    assert(!s.modules.grenade_dodge);
    assert(!s.modules.soft_cover_lof);
    assert(!s.modules.attack_detour);
    assert(!s.modules.target_prioritization);
    assert(s.modules.close_grenade);
    assert(s.modules.penetration_at);
    assert(s.modules.auto_smoke);
    assert(s.modules.auto_medic);
    assert(!s.modules.auto_resupply);
    assert(!s.modules.tactical_infantry);
    assert(!s.modules.tactical_objectives);
    assert(!s.modules.tank_crusher);
    assert(!s.modules.tank_auto_repair);
    assert(!s.modules.tank_crew_fill);
    assert(!s.modules.internal_crew_swap);
    assert(!s.modules.nontank_crew_fill);
    const auto defaults = parse_ini("").modules;
    assert(defaults.tank_facing && Settings{}.tuning.tank_facing_min_angle_deg == 24.f);
    const auto facing = parse_ini("[modules]\ntank_facing=false\n[tuning]\ntank_facing_min_angle_deg=29\n");
    assert(!facing.modules.tank_facing && facing.tuning.tank_facing_min_angle_deg == 29.f);
    assert(parse_ini("[tuning]\ntank_facing_min_angle_deg=1").tuning.tank_facing_min_angle_deg == 15.f);
    assert(parse_ini("[tuning]\ntank_facing_min_angle_deg=95").tuning.tank_facing_min_angle_deg == 60.f);
    assert(defaults.tank_crew_fill && defaults.internal_crew_swap && defaults.nontank_crew_fill);
    assert(defaults.ammo_scavenge && defaults.tank_target_memory && defaults.cannon_aim_settle &&
           defaults.allegiance_guard && defaults.overflow_cover && defaults.weapon_team_cohesion &&
           defaults.mg_crew_small_arms && defaults.movement_recovery && defaults.convoy_spacing &&
           defaults.flanking_wedge && defaults.cover_frag && defaults.role_target_split && defaults.offensive_smoke);
    const auto tier12 =
        parse_ini("[modules]\n"
                  "ammo_scavenge=false\ntank_target_memory=0\ncannon_aim_settle=off\nallegiance_guard=no\n"
                  "overflow_cover=false\nweapon_team_cohesion=false\nmg_crew_small_arms=false\n"
                  "movement_recovery=false\nconvoy_spacing=false\nflanking_wedge=false\ncover_frag=false\n"
                  "role_target_split=false\noffensive_smoke=false\n"
                  "[tuning]\n"
                  "scavenge_radius_m=99\ntank_memory_hold_s=0.1\ncannon_settle_max_s=99\n"
                  "overflow_cover_radius_m=1\nmovement_stall_s=99\nconvoy_gap_m=1\n");
    assert(!tier12.modules.ammo_scavenge && !tier12.modules.tank_target_memory && !tier12.modules.cannon_aim_settle &&
           !tier12.modules.allegiance_guard && !tier12.modules.overflow_cover && !tier12.modules.weapon_team_cohesion &&
           !tier12.modules.mg_crew_small_arms && !tier12.modules.movement_recovery && !tier12.modules.convoy_spacing &&
           !tier12.modules.flanking_wedge && !tier12.modules.cover_frag && !tier12.modules.role_target_split &&
           !tier12.modules.offensive_smoke);
    assert(tier12.tuning.scavenge_radius_m == 40.0f && tier12.tuning.tank_memory_hold_s == 1.0f &&
           tier12.tuning.cannon_settle_max_s == 5.0f && tier12.tuning.overflow_cover_radius_m == 3.0f &&
           tier12.tuning.movement_stall_s == 8.0f && tier12.tuning.convoy_gap_m == 4.0f);

    assert(std::fabs(s.tuning.grenade_detect_radius_m - 30.0f) < 1e-4f);
    assert(s.tuning.medic_search_radius_m <= 100.0f && s.tuning.medic_search_radius_m >= 5.0f);
    assert(std::fabs(s.tuning.resupply_search_radius_m - 80.0f) < 1e-4f);
    assert(std::fabs(s.tuning.objectives_pursue_radius_m - 60.0f) < 1e-4f);
    assert(std::fabs(s.tuning.objectives_cap_radius_m - 45.0f) < 1e-4f);
    assert(std::fabs(s.tuning.crew_fill_emit_backoff_s - 12.0f) < 1e-4f);
    assert(std::fabs(s.tuning.smoke_vehicle_cooldown_s - 30.0f) < 1e-4f);
    assert(std::fabs(s.tuning.smoke_infantry_cooldown_s - 10.0f) < 1e-4f);
    assert(s.tuning.soft_cover_log_interval_ms == 2500u);

    // Log level gating.
    s.log_level = goh::log::Level::off;
    goh::log::configure(s);
    assert(!goh::log::enabled(goh::log::Level::error));
    assert(!goh::log::enabled(goh::log::Level::summary));
    assert(!goh::log::enabled(goh::log::Level::verbose));

    s.log_level = goh::log::Level::summary;
    goh::log::configure(s);
    assert(goh::log::enabled(goh::log::Level::error));
    assert(goh::log::enabled(goh::log::Level::summary));
    assert(!goh::log::enabled(goh::log::Level::verbose));

    s.log_level = goh::log::Level::verbose;
    goh::log::configure(s);
    assert(goh::log::enabled(goh::log::Level::verbose));

    // Publishing a live edit updates every reader's POD view and keeps startup-owned text fixed.
    const auto old_filename = goh::log::state().filename;
    active().smoke_extra_grenades = {"startup_only"};
    auto update = parse_ini("[modules]\nauto_smoke=false\n[tuning]\nmedic_search_radius_m=46\n"
                            "[logging]\nfilename=renamed.log\nlevel=error\nflush_ms=210\n"
                            "[smoke]\nextra_grenades=hot_list\n"
                            "[testing]\nai_player_mask=2\nsim_speed=12.5\nauto_dismiss_loading=true\n"
                            "suppress_cursor_grab=true\nrestart_seq=7\nquit_seq=3");
    published().publish(update);
    goh::log::configure(update);
    std::thread live_reader([] {
        const auto& current = active();
        assert(!current.modules.auto_smoke && current.tuning.medic_search_radius_m == 46);
        assert(!player_ai_enabled(0) && player_ai_enabled(1));
        assert(current.sim_speed == 12.5f && current.auto_dismiss_loading && current.suppress_cursor_grab &&
               current.restart_seq == 7u && current.quit_seq == 3u);
        assert(current.smoke_extra_grenades == std::vector<std::string>{"startup_only"});
    });
    live_reader.join();
    assert(goh::log::state().filename == old_filename && goh::log::state().flush_ms == 210);
    assert(!goh::log::enabled(goh::log::Level::summary));
    // Restore default config.
    reset_defaults();
    goh::log::configure(active());
}

void write_u16(std::vector<std::uint8_t>& buf, std::size_t off, std::uint16_t v) {
    std::memcpy(buf.data() + off, &v, sizeof(v));
}

void write_u32(std::vector<std::uint8_t>& buf, std::size_t off, std::uint32_t v) {
    std::memcpy(buf.data() + off, &v, sizeof(v));
}

void write_u64(std::vector<std::uint8_t>& buf, std::size_t off, std::uint64_t v) {
    std::memcpy(buf.data() + off, &v, sizeof(v));
}

void test_pe_pattern_and_rtti_resolver() {
    using namespace goh::runtime;

    // Build a minimal 16 KB synthetic PE32+ image in memory:
    //   0x0000..0x0FFF : DOS + PE headers + 3 section headers
    //   0x1000..0x1FFF : .text  (RX, 0x60000020)
    //   0x2000..0x2FFF : .rdata (R,  0x40000040)
    //   0x3000..0x3FFF : .data  (RW, 0xC0000040)
    alignas(16) std::vector<std::uint8_t> img(0x4000, 0);
    const Address base = reinterpret_cast<Address>(img.data());

    // DOS header
    write_u16(img, 0x00, 0x5A4Du); // "MZ"
    write_u32(img, 0x3C, 0x80u);   // e_lfanew

    // PE signature + COFF header at 0x80
    write_u32(img, 0x80, 0x00004550u); // "PE\0\0"
    write_u16(img, 0x84, 0x8664u);     // AMD64
    write_u16(img, 0x86, 3u);          // NumberOfSections = 3
    write_u16(img, 0x94, 0xF0u);       // SizeOfOptionalHeader = 240

    // OptionalHeader64 at 0x98
    write_u16(img, 0x98, 0x020Bu); // PE32+ magic
    write_u32(img, 0xD0, 0x4000u); // SizeOfImage

    // Section headers at 0x98 + 0xF0 = 0x188 (40 bytes each)
    auto put_sec = [&](std::size_t idx, const char* name, std::uint32_t va, std::uint32_t sz, std::uint32_t ch) {
        const std::size_t off = 0x188 + idx * 40;
        std::memcpy(img.data() + off, name, std::strlen(name));
        write_u32(img, off + 8, sz);  // VirtualSize
        write_u32(img, off + 12, va); // VirtualAddress
        write_u32(img, off + 16, sz); // SizeOfRawData
        write_u32(img, off + 36, ch); // Characteristics
    };
    put_sec(0, ".text", 0x1000u, 0x1000u, 0x60000020u);
    put_sec(1, ".rdata", 0x2000u, 0x1000u, 0x40000040u);
    put_sec(2, ".data", 0x3000u, 0x1000u, 0xC0000040u);

    PeView pe{};
    assert(parse_pe64(base, pe, img.size()));
    assert(pe.section_count == 3);
    assert(pe.find_section(".text") != nullptr);
    assert(pe.find_section(".rdata") != nullptr);
    assert(pe.find_section(".data") != nullptr);

    // Pattern scanning in .text
    const std::uint8_t unique_bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
    std::memcpy(img.data() + 0x1234, unique_bytes, sizeof(unique_bytes));
    const std::uint8_t dup_bytes[] = {0x90, 0xCC, 0x90, 0xCC};
    std::memcpy(img.data() + 0x1400, dup_bytes, sizeof(dup_bytes));
    std::memcpy(img.data() + 0x1500, dup_bytes, sizeof(dup_bytes));

    const Byte pat_unique[] = {0x48, 0x89, 0x5C, 0x00, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
    const Byte pat_missing[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    assert(find_unique_pattern(pe, ".text", pat_unique, "xxx?xxxxxx") == 0x1234u);
    assert(find_unique_pattern(pe, ".text", dup_bytes, "xxxx") == 0u);     // Ambiguous -> fail closed!
    assert(find_unique_pattern(pe, ".text", pat_missing, "xxxxxx") == 0u); // Missing -> 0.

    // Synthetic MSVC x64 RTTI for ".?AVeHumanBrain@Actor@@"
    const char* mangled = ".?AVeHumanBrain@Actor@@";
    const std::uint32_t td_rva = 0x3100u;
    const std::uint32_t col_rva = 0x2200u;
    const std::uint32_t vt_meta_rva = 0x2400u; // [meta] = base + col_rva; vtable = 0x2408

    // TypeDescriptor in .data at 0x3100: +0x00 pVFTable (inside .rdata), +0x08 spare=0, +0x10 name
    write_u64(img, td_rva + 0x00, base + 0x2010u);
    write_u64(img, td_rva + 0x08, 0u);
    std::memcpy(img.data() + td_rva + 0x10, mangled, std::strlen(mangled) + 1);

    // RTTICompleteObjectLocator in .rdata at 0x2200
    write_u32(img, col_rva + 0x00, 1u);      // signature == 1 (x64 relative)
    write_u32(img, col_rva + 0x04, 0u);      // offset == 0
    write_u32(img, col_rva + 0x08, 0u);      // cdOffset == 0
    write_u32(img, col_rva + 0x0C, td_rva);  // pTypeDescriptor RVA
    write_u32(img, col_rva + 0x10, 0x2240u); // pClassDescriptor RVA (dummy)
    write_u32(img, col_rva + 0x14, col_rva); // pSelf == col_rva

    // Vtable pointer in .rdata at 0x2400 -> vtable at 0x2408
    write_u64(img, vt_meta_rva, base + col_rva);
    write_u64(img, vt_meta_rva + 8, base + 0x1234u);

    assert(resolve_rtti_vtable(pe, mangled, 0) == 0x2408u);
    assert(resolve_rtti_vtable(pe, mangled, 16) == 0u); // Wrong subobject offset -> 0.
    assert(resolve_rtti_vtable(pe, ".?AVDoesNotExist@@", 0) == 0u);

    // Duplicate vtable reference -> fail closed (ambiguous = 0).
    write_u64(img, 0x2500u, base + col_rva);
    assert(resolve_rtti_vtable(pe, mangled, 0) == 0u);
    write_u64(img, 0x2500u, 0u);
    assert(resolve_rtti_vtable(pe, mangled, 0) == 0x2408u);

    // Reuse the synthetic primary vtable to exercise the entries used by the vehicle adapters.
    auto check_entry = [&](const char* name, Address& entry, Address expected) {
        std::memcpy(img.data() + td_rva + 0x10, name, std::strlen(name) + 1);
        assert(entry == expected);
        const auto audit = resolve_and_verify_vtables(base, true, img.size());
        assert(audit.mismatched == 1 && audit.missing + 1 == audit.checked);
        assert(entry == 0x2408u);
        reset_table();
    };
    check_entry(".?AUeCollect@Order@@", table().vt.collect_order, 0x01109BB8);
    check_entry(".?AVeStuffExtender@Entity@@", table().vt.stuff_ext, 0x010FD200);
    check_entry(".?AVeCannonExtender@Entity@@", table().vt.cannon_ext, 0x0110F040);
    check_entry(".?AVeChassisTrack@Actor@@", table().vt.chassis_track, 0x01105E80);
    check_entry(".?AUeAdvance@Order@@", table().vt.advance_order, 0x0110A070);
    check_entry(".?AVeCrushScannerVehicle@Actor@@", table().vt.crush_scanner_vehicle, 0x01110BC0);
    check_entry(".?AVeHS_RootSoldier@Human@@", table().vt.root_soldier, 0x01110CD0);
    check_entry(".?AUeRotate@Order@@", table().vt.rotate_order, 0x0110C760);
    check_entry(".?AUeMove@Order@@", table().vt.move_order, 0x0110C810);
    check_entry(".?AUeMoveToCover@Order@@", table().vt.move_to_cover, 0x01109C60);
    check_entry(".?AVeGUI_PageLoading@GUI2@@", table().vt.page_loading, 0x01058828);
    check_entry(".?AUcmGameRestart@Scene@Events@@", table().vt.cm_game_restart, 0x0104FEF0);

    // Corrupt MZ header fails cleanly.
    write_u16(img, 0x00, 0x1234u);
    PeView bad{};
    assert(!parse_pe64(base, bad, img.size()));
}

void test_hook_gate_multithreaded() {
    using namespace goh::runtime;

    HookGate gate{};
    global_inflight.store(0, std::memory_order_release);
    global_accepting.store(false, std::memory_order_release);
    assert(!gate.active());
    gate.activate();
    assert(!gate.active()); // Still gated until global_accepting is true.
    global_accepting.store(true, std::memory_order_release);
    assert(gate.active());

    {
        HookGuard g1(gate);
        assert(gate.count() == 1 && global_inflight.load() == 1);
        {
            HookGuard g2(gate);
            assert(gate.count() == 2 && global_inflight.load() == 2);
        }
        assert(gate.count() == 1 && global_inflight.load() == 1);
    }
    assert(gate.count() == 0 && global_inflight.load() == 0);

    std::vector<std::thread> workers;
    workers.reserve(4);
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&gate]() {
            for (int i = 0; i < 4000; ++i) {
                HookGuard g(gate);
            }
        });
    }
    for (auto& w : workers)
        w.join();
    gate.deactivate();
    global_accepting.store(false, std::memory_order_release);
    assert(!gate.active());
    assert(gate.count() == 0 && global_inflight.load() == 0);
}

void test_actor_coordinator() {
    using namespace goh::coord;

    Coordinator c;
    static Address s_obj_actor = 0;
    static bool s_obj_busy = false;
    static bool s_preempted = false;
    s_obj_actor = 0x10000;
    s_obj_busy = true;
    s_preempted = false;

    c.register_provider(
        Module::tactical_objectives, Priority::tactical_objectives,
        [](Address actor) { return s_obj_busy && actor == s_obj_actor; },
        [](Address actor) {
            if (actor == s_obj_actor) {
                s_obj_busy = false;
                s_preempted = true;
            }
        });

    // Live provider allows self to continue, blocks OTHER modules at <= priority, permits higher priority.
    assert(c.can_act(s_obj_actor, Module::tactical_objectives, Priority::tactical_objectives, 1, 100));
    assert(!c.can_act(s_obj_actor, Module::tactical_infantry, Priority::tactical_objectives, 1, 100));
    assert(c.can_act(s_obj_actor, Module::auto_medic, Priority::auto_medic, 1, 100));
    assert(c.can_act(s_obj_actor, Module::grenade_dodge, Priority::grenade_dodge, 1, 100));

    // Higher priority claim preempts tactical_objectives via registered callback!
    assert(c.claim(s_obj_actor, 1, Module::auto_medic, Priority::auto_medic, 100, 50));
    assert(s_preempted && !s_obj_busy);

    // Lower priorities blocked while auto_medic holds lease; grenade_dodge can still act & preempt.
    assert(!c.can_act(s_obj_actor, Module::tactical_infantry, Priority::tactical_infantry, 1, 120));
    assert(!c.can_act(s_obj_actor, Module::auto_resupply, Priority::auto_resupply, 1, 120));
    assert(!c.can_act(s_obj_actor, Module::auto_medic, Priority::auto_medic, 1, 120) == false); // Self allowed!
    assert(c.can_act(s_obj_actor, Module::grenade_dodge, Priority::grenade_dodge, 1, 120));
    assert(c.claim(s_obj_actor, 1, Module::grenade_dodge, Priority::grenade_dodge, 120, 30));
    assert(!c.can_act(s_obj_actor, Module::auto_medic, Priority::auto_medic, 1, 140));

    // Lease expires cleanly after ttl (120 + 30 = 150).
    assert(c.can_act(s_obj_actor, Module::auto_medic, Priority::auto_medic, 1, 151));

    // Scene change invalidates all leases immediately.
    c.sync_world(0xAAAA, 200);
    assert(c.claim(s_obj_actor, 1, Module::auto_smoke, Priority::auto_smoke, 200, 1000));
    assert(!c.can_act(s_obj_actor, Module::auto_medic, Priority::auto_medic, 1, 210));
    c.sync_world(0xBBBB, 210);
    assert(c.lease_count() == 0);
    assert(c.can_act(s_obj_actor, Module::auto_medic, Priority::auto_medic, 1, 210));

    // A delivery reserves both participants ahead of a flag resupply. A medic can still preempt either.
    for (const Address participant : {s_obj_actor, s_obj_actor + 8}) {
        assert(c.claim(participant, 1, Module::squad_logistics, Priority::squad_logistics, 212, 5));
        assert(!c.can_act(participant, Module::auto_resupply, Priority::auto_resupply, 1, 215));
        assert(c.can_act(participant, Module::auto_medic, Priority::auto_medic, 1, 215));
        c.release(participant, Module::squad_logistics);
    }

    static_assert(static_cast<unsigned>(Module::recrew_weapons) == 13 && static_cast<unsigned>(Module::scavenge) == 14);
    static_assert(static_cast<unsigned>(Module::count) == 15 && static_cast<unsigned>(Priority::scavenge) == 28 &&
                  static_cast<unsigned>(Priority::recrew_weapons) == 32);
    assert(c.claim(s_obj_actor, 1, Module::scavenge, Priority::scavenge, 216, 10));
    assert(!c.can_act(s_obj_actor, Module::tactical_infantry, Priority::tactical_infantry, 1, 216));
    assert(c.claim(s_obj_actor, 1, Module::auto_resupply, Priority::auto_resupply, 216, 10));
    assert(c.claim(s_obj_actor, 1, Module::recrew_weapons, Priority::recrew_weapons, 216, 10));
    assert(!c.can_act(s_obj_actor, Module::auto_resupply, Priority::auto_resupply, 1, 216));
    assert(c.claim(s_obj_actor, 1, Module::squad_logistics, Priority::squad_logistics, 216, 10));
    c.release(s_obj_actor, Module::squad_logistics);

    // Movement recovery (18) and convoy spacing (15) stay below tactical infantry (20) and support modules.
    assert(c.claim(s_obj_actor, 1, Module::convoy_spacing, Priority::convoy_spacing, 220, 5));
    assert(c.can_act(s_obj_actor, Module::movement_recovery, Priority::movement_recovery, 1, 221));
    assert(c.claim(s_obj_actor, 1, Module::movement_recovery, Priority::movement_recovery, 221, 5));
    assert(!c.can_act(s_obj_actor, Module::tactical_objectives, Priority::tactical_objectives, 1, 222));
    assert(c.can_act(s_obj_actor, Module::tactical_infantry, Priority::tactical_infantry, 1, 222));
    c.release(s_obj_actor, Module::movement_recovery);

    // Backwards clock jump also invalidates leases.
    assert(c.claim(s_obj_actor, 1, Module::auto_smoke, Priority::auto_smoke, 210, 1000));
    c.sync_world(0xBBBB, 50);
    assert(c.lease_count() == 0);
    c.release_all();
}

void write_gem_sso(std::uint8_t* dst, const char* text) {
    const std::size_t len = std::strlen(text);
    assert(len < 16);
    std::memset(dst, 0, 0x20);
    std::memcpy(dst, text, len + 1);
    std::memcpy(dst + 0x10, &len, sizeof(len));
    const std::size_t cap = 15;
    std::memcpy(dst + 0x18, &cap, sizeof(cap));
}

void test_classifiers_and_carried_at() {
    using namespace goh::classify;

    // Tank & vehicle class helpers.
    assert(is_battle_tank_class("tank") && is_battle_tank_class("tank_heavy"));
    assert(!is_battle_tank_class("tank_bullet"));
    assert(is_tank_or_bullet_class("tank_bullet"));
    assert(is_armored_vehicle_class("armoredcar") && is_armored_vehicle_class("armoredcar_l"));
    assert(!is_armored_vehicle_class("car"));

    // Smoke classifier + strict WP deny-list + INI extensibility.
    goh::config::reset_defaults();
    assert(is_allowed_smoke_grenade_key("anm8_smoke"));
    assert(is_allowed_smoke_grenade_key("m24_smoke"));
    assert(is_allowed_smoke_grenade_key("nbk39"));
    assert(!is_allowed_smoke_grenade_key("m15_wp"));
    assert(!is_allowed_smoke_grenade_key("m42_wp"));
    assert(!is_allowed_smoke_grenade_key("no77_wp"));
    assert(!is_allowed_smoke_grenade_key("mod_smoke_canister"));
    assert(!is_allowed_extra_smoke_program("custom_smoke_prog"));

    // Even if a user tries to add WP or frag items in goh_ai.ini, deny-list MUST reject them!
    goh::config::active() =
        goh::config::parse_ini("[smoke]\nextra_grenades = mod_smoke_canister, m15_wp, no77_wp, frag_smoke\n"
                               "extra_programs = custom_smoke_prog, 2in_wp, wp_launcher\n");
    assert(is_allowed_smoke_grenade_key("mod_smoke_canister"));
    assert(!is_allowed_smoke_grenade_key("m15_wp"));
    assert(!is_allowed_smoke_grenade_key("no77_wp"));
    assert(!is_allowed_smoke_grenade_key("frag_smoke"));
    assert(is_allowed_extra_smoke_program("custom_smoke_prog"));
    assert(!is_allowed_extra_smoke_program("2in_wp"));
    assert(!is_allowed_extra_smoke_program("wp_launcher"));
    goh::config::reset_defaults();

    // Dynamic carried-AT inspection (actor -> aggregate -> manager -> box / right-hand holder).
    alignas(16) std::vector<std::uint8_t> actor_buf(0x600, 0);
    alignas(16) std::vector<std::uint8_t> agg_buf(0x100, 0);
    alignas(16) std::vector<std::uint8_t> mgr_buf(0x100, 0);
    alignas(16) std::vector<std::uint8_t> box_buf(0x100, 0);
    alignas(16) std::vector<std::uint8_t> item_buf(0x80, 0);
    alignas(16) std::vector<std::uint8_t> def_buf(0x1300, 0);
    alignas(16) std::vector<std::uint8_t> tag_row(0x60, 0);

    const Address actor = reinterpret_cast<Address>(actor_buf.data());
    const Address agg = reinterpret_cast<Address>(agg_buf.data());
    const Address mgr = reinterpret_cast<Address>(mgr_buf.data());
    const Address box = reinterpret_cast<Address>(box_buf.data());
    const Address item = reinterpret_cast<Address>(item_buf.data());
    const Address def = reinterpret_cast<Address>(def_buf.data());
    const Address tag_begin = reinterpret_cast<Address>(tag_row.data());
    const Address tag_end = tag_begin + 0x60;

    // Actor with no aggregate -> inventory_present == false (fallback to breed tags).
    auto r_none = inspect_carried_at_inventory(actor);
    assert(!r_none.inventory_present && !r_none.has_at_weapon);

    // Wire actor -> agg -> mgr -> box -> [item] -> def -> tag "rocketlauncher"
    std::memcpy(actor_buf.data() + 0x578, &agg, sizeof(agg));
    std::memcpy(agg_buf.data() + 0x28, &mgr, sizeof(mgr));
    std::memcpy(mgr_buf.data() + 0xA0, &box, sizeof(box));
    std::memcpy(box_buf.data() + 0x50, &actor, sizeof(actor));

    std::array<Address, 1> item_ptrs{item};
    const Address items_beg = reinterpret_cast<Address>(item_ptrs.data());
    const Address items_end = items_beg + sizeof(Address);
    std::memcpy(box_buf.data() + 0x10, &items_beg, sizeof(items_beg));
    std::memcpy(box_buf.data() + 0x18, &items_end, sizeof(items_end));

    std::memcpy(item_buf.data() + 0x30, &box, sizeof(box));
    std::memcpy(item_buf.data() + 0x40, &def, sizeof(def));
    const std::int32_t count_one = 1;
    std::memcpy(item_buf.data() + 0x48, &count_one, sizeof(count_one));

    std::memcpy(def_buf.data() + 0x11E0, &tag_begin, sizeof(tag_begin));
    std::memcpy(def_buf.data() + 0x11E8, &tag_end, sizeof(tag_end));
    write_gem_sso(tag_row.data() + 0x40, "rocketlauncher");

    auto r_at = inspect_carried_at_inventory(actor);
    assert(r_at.inventory_present && r_at.has_at_weapon);

    // Empty / expended launcher (count == 0) is NOT treated as carried AT.
    const std::int32_t count_zero = 0;
    std::memcpy(item_buf.data() + 0x48, &count_zero, sizeof(count_zero));
    auto r_empty = inspect_carried_at_inventory(actor);
    assert(r_empty.inventory_present && !r_empty.has_at_weapon);

    // Non-AT rifle with count == 1 is NOT treated as AT.
    std::memcpy(item_buf.data() + 0x48, &count_one, sizeof(count_one));
    write_gem_sso(tag_row.data() + 0x40, "rifle");
    auto r_rifle = inspect_carried_at_inventory(actor);
    assert(r_rifle.inventory_present && !r_rifle.has_at_weapon);
}

} // namespace

int main() {
    static_assert(goh::tu_combat::kHookCount == 23 && goh::tu_infantry::kHookCount == 46 &&
                  goh::tu_tanks::kHookCount == 9);
    static_assert(1 + goh::tu_combat::kHookCount + goh::tu_infantry::kHookCount + goh::tu_tanks::kHookCount == 79);
    test_dynamic_stores();
    test_benchmark_player_mask();
    test_config_and_log();
    test_pe_pattern_and_rtti_resolver();
    test_hook_gate_multithreaded();
    test_actor_coordinator();
    test_classifiers_and_carried_at();
    std::printf("Architecture offline fixtures passed.\n");
    return 0;
}
