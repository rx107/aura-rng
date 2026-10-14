#include "core/aura.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F f) { bool failed = false; try { f(); } catch (const std::exception&) { failed = true; } check(failed, "Expected rejection"); }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Catalog path required");
        const auto defs = aura::loadCatalog(argv[1]);
        check(defs.size() == 12, "Expected 12 demo auras");
        aura::Roller roller(defs, 42), same(defs, 42);
        std::uint64_t start = 0;
        for (std::size_t i = 0; i < defs.size(); ++i) {
            const auto count = roller.tickets(i);
            check(count > 0, "Every aura must be reachable");
            check(roller.selectTicket(start) == i, "Range start");
            check(roller.selectTicket(start + count - 1) == i, "Range end");
            if (defs[i].denominator) check(count == aura::TicketPool / defs[i].denominator, "Probability weight");
            start += count;
        }
        check(start == aura::TicketPool, "Probability total");
        rejects([&] { roller.selectTicket(aura::TicketPool); });
        std::vector<std::uint64_t> observed(defs.size());
        for (int i = 0; i < 200000; ++i) { const auto n = roller.roll(); check(n == same.roll(), "Seed reproducibility"); ++observed[n]; }
        check(std::abs(static_cast<double>(observed[1]) / 200000 - .5) < .01, "Glimmer distribution");
        check(std::abs(static_cast<double>(observed[2]) / 200000 - .1) < .005, "Ember distribution");
        auto invalid = defs; invalid[1].id = invalid[0].id;
        rejects([&] { aura::Roller bad(invalid, 1); });
        invalid = defs; invalid[1].denominator = 0;
        rejects([&] { aura::Roller bad(invalid, 1); });
        invalid = defs; invalid[1].denominator = 2; invalid[2].denominator = 2;
        rejects([&] { aura::Roller bad(invalid, 1); });
        invalid = defs; invalid[1].color.r = NAN;
        rejects([&] { aura::Roller bad(invalid, 1); });
        invalid = defs; invalid[1].denominator = aura::TicketPool + 1;
        rejects([&] { aura::Roller bad(invalid, 1); });
        const auto dir = std::filesystem::temp_directory_path() / ("aura-rng-tests-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(dir);
        aura::Progress p(defs.size());
        p.record(11, defs); p.record(1, defs); p.record(1, defs);
        check(p.best == 11 && p.equipped == 1 && p.discovered() == 2, "Progress best/equip/discovery");
        const auto save = dir / "progress.txt";
        aura::saveProgress(save, p, defs);
        p.record(3, defs); aura::saveProgress(save, p, defs);
        const auto restored = aura::loadProgress(save, defs);
        check(restored.rolls == 4 && restored.counts == p.counts && restored.best == 11 && restored.equipped == 3, "Atomic overwrite round trip");
        auto reordered = defs; std::swap(reordered[3], reordered[8]);
        const auto reorderedProgress = aura::loadProgress(save, reordered);
        check(reorderedProgress.equipped == 8 && reorderedProgress.counts[8] == 1, "Stable IDs after catalog reorder");
        auto removed = defs; removed.erase(removed.begin() + 11);
        const auto removedProgress = aura::loadProgress(save, removed);
        check(removedProgress.retiredCounts.size() == 1, "Removed aura counts retained");
        aura::saveProgress(save, removedProgress, removed);
        check(aura::loadProgress(save, defs).counts[11] == 1, "Removed aura restored after reimport");
        check(aura::loadProgress(dir / "missing", defs).rolls == 0, "First run");
        { std::ofstream o(save); o << "AURA_RNG_SAVE 1\nrolls 4\nequipped glimmer\ncount glimmer 3\n"; }
        rejects([&] { aura::loadProgress(save, defs); });
        auto named = defs[1]; named.name = "光, \"星\"";
        const auto log = dir / "rolls.csv";
        aura::appendLog(log, {1, "2026-01-01T00:00:00Z", 1}, named);
        aura::appendLog(log, {2, "2026-01-01T00:00:01Z", 1}, named);
        std::ifstream in(log); std::string content((std::istreambuf_iterator<char>(in)), {});
        check(content.find("\"光, \"\"星\"\"\"") != std::string::npos, "CSV escaping / UTF-8");
        check(content.find("roll,utc_time", 1) == std::string::npos, "One CSV header");
        in.close();
        { std::ofstream o(dir / "bad.csv"); o << "id,name,denominator,red,green,blue,particles,radius,speed\na,\"unclosed,0,1,1,1,1,1,1\n"; }
        rejects([&] { aura::loadCatalog(dir / "bad.csv"); });
        check(aura::formatNumber(1000000000) == "1,000,000,000", "Number formatting");
        std::filesystem::remove_all(dir);
        std::cout << "PASS: ticket boundaries, seeded rolls, distribution, catalog validation, save/restore, UTF-8 CSV logs\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
