#include "core/aura.hpp"
#include "core/presentation.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
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
        check(roller.tickets(0) == aura::TicketPool / 2, "Common is exactly 50 percent");
        check(defs[0].name == "Common" && defs[1].name == "Uncommon", "Renamed aura labels with stable IDs");
        std::set<std::uint64_t> uniqueWeights;
        for(std::size_t i=0;i<defs.size();++i) {
            check(uniqueWeights.insert(roller.tickets(i)).second, "Every demo aura has a distinct probability");
            check(aura::probabilityTickets(defs,i) == roller.tickets(i), "Displayed probability matches the actual ticket pool");
        }
        check(aura::oddsLabel(defs,0) == "1 / 2", "Common probability label");
        for(double luck:{1.5,2.,10.,1000.,1'000'000.}) {
            roller.setLuck(luck); std::uint64_t total=0;
            for(std::size_t i=0;i<defs.size();++i) total+=roller.tickets(i);
            check(total==aura::TicketPool,"Luck preserves total probability");
            check(roller.tickets(0)<aura::TicketPool/2 && roller.tickets(11)>1000,"Luck raises rare tail and reduces Common");
        }
        roller.setLuck(2);
        check(roller.tickets(0)==aura::TicketPool/4,"2x Luck gives Common a 25 percent probability");
        check(std::abs(static_cast<double>(roller.tickets(11))-2000)<=1,"Rare aura approximates twice the base odds at 2x");
        rejects([&]{roller.setLuck(NAN);}); rejects([&]{roller.setLuck(0);}); rejects([&]{roller.setLuck(1'000'001);});
        roller.setLuck(1);
        for(std::size_t i=0;i<defs.size();++i) check(roller.tickets(i)==aura::probabilityTickets(defs,i),"Reset Luck restores exact baseline");
        rejects([&] { roller.selectTicket(aura::TicketPool); });
        std::vector<std::uint64_t> observed(defs.size());
        for (int i = 0; i < 200000; ++i) { const auto n = roller.roll(); check(n == same.roll(), "Seed reproducibility"); ++observed[n]; }
        check(std::abs(static_cast<double>(observed[0]) / 200000 - .5) < .01, "Common distribution");
        check(std::abs(static_cast<double>(observed[2]) / 200000 - .125) < .005, "Ember distribution");
        auto invalid = defs; invalid[1].id = invalid[0].id;
        rejects([&] { aura::Roller bad(invalid, 1); });
        invalid = defs; invalid[0].denominator = 0;
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
        p.record(11, defs);
        check(p.equipped == 0, "Rolling cannot equip a new aura");
        p.equipped=11; p.record(1, defs); p.record(1, defs);
        check(p.best == 11 && p.equipped == 11 && p.discovered() == 2, "Progress best/equip/discovery");
        const auto save = dir / "progress.txt";
        aura::saveProgress(save, p, defs);
        p.record(3, defs); p.equipped=3; aura::saveProgress(save, p, defs);
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
        aura::Presentation presentation; bool autoRoll=true;
        const auto countsBefore=p.counts;
        const auto rollsBefore=p.rolls;
        presentation.showPreview(10,defs.size(),autoRoll);
        check(!autoRoll && presentation.shown(p)==10 && p.equipped==3 && p.counts==countsBefore && p.rolls==rollsBefore,
            "Unowned debug preview stops AutoRoll and leaves inventory untouched");
        p.record(2,defs);
        check(presentation.shown(p)==10 && p.equipped==3, "Roll keeps preview and equipment fixed");
        presentation.preview.reset(); check(presentation.shown(p)==3,"Restore Collection aura");
        rejects([&] { presentation.showPreview(defs.size(),defs.size(),autoRoll); });
        const auto settingsPath=dir/"settings.txt";
        check(aura::loadSettings(settingsPath).language==aura::Language::English,"Settings default");
        aura::Settings settings; settings.language=aura::Language::Japanese; settings.shirt={.95f,.2f,.3f};
        settings.luck=12.5; settings.avatar=aura::Avatar::Vrm; settings.biome=aura::Biome::Desert;
        settings.vrmPath="C:/モデル/Chocolat.vrm"; settings.bgmPath="C:/Music/Tides of the Outer Reach.mp3"; settings.volume=.35f;
        aura::saveSettings(settingsPath,settings); const auto loadedSettings=aura::loadSettings(settingsPath);
        check(loadedSettings.language==aura::Language::Japanese && std::abs(loadedSettings.shirt.r-.95f)<.0001f,
            "Language and shirt color round trip");
        check(loadedSettings.luck==12.5 && loadedSettings.avatar==aura::Avatar::Vrm && loadedSettings.biome==aura::Biome::Desert &&
            loadedSettings.vrmPath==settings.vrmPath && loadedSettings.bgmPath==settings.bgmPath && loadedSettings.volume==settings.volume,"Extended settings / UTF-8 paths round trip");
        {std::ofstream o(settingsPath); o<<"STARS_RNG_SETTINGS 1\nlanguage ja\nshirt 0.5 0.7 0.9\n";}
        const auto migrated=aura::loadSettings(settingsPath);
        check(migrated.language==aura::Language::Japanese && migrated.luck==1 && migrated.avatar==aura::Avatar::Block,"Previous settings migrate");
        settings.luck=INFINITY; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.luck=1;
        settings.avatar=static_cast<aura::Avatar>(3); rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.avatar=aura::Avatar::Block;
        settings.vrmPath="bad\npath"; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.vrmPath.clear();
        settings.shirt.g=NAN; rejects([&] { aura::saveSettings(settingsPath,settings); });
        check(aura::displayName(defs[0],aura::Language::Japanese)=="コモン","Japanese aura names");
        check(!aura::cooldownVisual(0).visible && !aura::cooldownVisual(-1).visible,"Cooldown bar disappears at completion");
        check(aura::cooldownVisual(aura::RollCooldown).opacity==0,"Cooldown fades in");
        check(aura::cooldownVisual(.001f).opacity<.01f,"Cooldown fades out");
        const auto waveA=aura::cooldownVisual(aura::RollCooldown*.875f);
        const auto waveB=aura::cooldownVisual(aura::RollCooldown*.625f);
        check(waveA.progress<waveB.progress && waveA.waveY>2.9f && waveB.waveY<-2.9f,"Star follows a left-to-right wave");
        check(aura::formatNumber(1000000000) == "1,000,000,000", "Number formatting");
        std::filesystem::remove_all(dir);
        std::cout << "PASS: exact Common probability, distinct odds, fixed equipment, preview isolation, bilingual settings, shirt colors, cooldown wave, save/restore, UTF-8 CSV\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
