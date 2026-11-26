#ifndef _WIN32
#define __fastcall
#endif
#include "../../mods/live_telemetry.hpp"
#include "../../mods/live_observer_26734796.hpp"
#include "../../mods/ai_modules.hpp"
#include "../../mods/config_and_log.hpp"

#include <cassert>
#include <thread>

using namespace goh;

int main() {
    using namespace live;
    assert(std::string_view(fingerprint) == runtime::kBuild26734796Sha256);
    auto cfg = parse_ini("[other]\nenabled=1\n[ Telemetry ]\r\nenabled=YES ; hi\nfilename=sample.jsonl\n"
                         "snapshot_interval_ms=3\nflush_ms=0\nfilename=../wrong.jsonl\nfilename=CON.jsonl\n");
    assert(cfg.enabled && std::string_view(cfg.filename) == "sample.jsonl");
    assert(cfg.snapshot_interval_ms == 50 && cfg.flush_ms == 0);
    assert(!parse_ini("").enabled && parse_ini("").snapshot_interval_ms == 500);
    assert(parse_ini("[telemetry]\nflush_ms=7foo\nflush_ms=-1").flush_ms == 500);
    ReloadClock clock;
    assert(clock.due(0) && !clock.due(999) && clock.due(1000));
    assert(clock.changed(0, 10));
    clock.commit(0, 10);
    assert(!clock.changed(0, 10) && clock.changed(1, 10) && clock.changed(0, 11));

    PublishedSettings<config::Settings> published;
    config::Settings startup;
    startup.smoke_extra_programs = {"stock"};
    assert(&published.view(startup) == &startup);
    auto next = startup;
    next.modules.auto_medic = false;
    next.tuning.medic_search_radius_m = 44;
    next.smoke_extra_programs.clear();
    next.log_flush_ms = 99;
    published.publish(next);
    const auto& settings = published.view(startup);
    std::thread reader([&] {
        const auto& other = published.view(startup);
        assert(&other != &settings && !other.modules.auto_medic && other.log_flush_ms == 99);
        assert(other.smoke_extra_programs == startup.smoke_extra_programs);
    });
    reader.join();
    assert(startup.modules.auto_medic && settings.tuning.medic_search_radius_m == 44);

    Writer w;
    auto* file = std::tmpfile();
    assert(file && !w.start(file, cfg, "call_to_arms_ed.exe", fingerprint, 0, 0));
    assert(!w.start(std::tmpfile(), cfg, executable, "wrong", 0, 0));
    Config off;
    assert(!w.start(std::tmpfile(), off, executable, fingerprint, 0, 0));
    file = std::tmpfile();
    assert(file && w.start(file, cfg, executable, fingerprint, 0, 0));
    tu_combat::Stats c;
    tu_infantry::Stats i;
    tu_tanks::Stats t;
    static_assert(sizeof(c) + sizeof(i) + sizeof(t) == sizeof(Counters),
                  "Update JSONL counter names when TU Stats grow");
    c.dodges = 500;
    i.reload = 40;
    w.stats(0, 0, counters(c, i, t));
    c.dodges += 2;
    t.tank_face++;
    i.reload++;
    w.stats(50, 10, counters(c, i, t));
    w.stats(100, 20, counters(c, i, t));
    int reads = 0;
    auto snapshot = [&] {
        ++reads;
        ActorSnapshot a{};
        a.sim_tick = 20;
        a.mid = 1001;
        a.is_human = true;
        a.behavior = "a\n\"b\\";
        a.hp_ratio = std::numeric_limits<double>::infinity();
        return a;
    };
    for (unsigned id = 1; id <= 256; ++id)
        assert(w.sample(100, id, snapshot));
    assert(!w.sample(100, 257, snapshot));
    assert(w.sample(150, 257, snapshot) && !w.sample(151, 257, snapshot));
    assert(reads == 257);
    w.stats(200, 1020, counters(c, i, t));
    c.dodges = 0;
    w.stats(250, 10, counters(c, i, t));
    assert(w.module_event(151, 30, "close_grenade", "released", 101, 102));
    assert(w.reloaded(cfg, 151, 30));
    assert(std::fseek(file, 0, SEEK_SET) == 0);
    char buf[4096]{};
    std::string lines;
    while (std::fgets(buf, static_cast<int>(sizeof(buf)), file))
        lines += buf;
    assert(lines.find("\"event\":\"session_start\"") != lines.npos);
    assert(lines.find("\"counters\":{\"dodges\":2,\"reload\":1,\"tank_face\":1}") != lines.npos);
    assert(lines.find("stats_delta") == lines.rfind("stats_delta"));
    assert(lines.find("\"hp_ratio\":null") != lines.npos);
    assert(lines.find("a\\u000a\\\"b\\\\") != lines.npos);
    assert(lines.find("\"target_mid\":102") != lines.npos);
    assert(lines.find("config_reloaded") != lines.npos);
    assert(std::count(lines.begin(), lines.end(), '\n') == 262);
    assert(lines.find("scene_reset") != lines.npos);
    w.close();
    w.close();
    assert(!w.active() && !w.sample(1000, 1, snapshot));
}
