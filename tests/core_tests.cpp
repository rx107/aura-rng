#include "core/aura.hpp"
#include "core/presentation.hpp"
#include "core/animation.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
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
        check(defs.size() == 17, "Expected 13 rollable auras and four previews");
        const auto findAura=[&](const std::string& id) {
            const auto found=std::find_if(defs.begin(),defs.end(),[&](const auto& d){return d.id==id;});
            check(found!=defs.end(),"Required aura ID"); return static_cast<std::size_t>(found-defs.begin());
        };
        const auto singularity=findAura("singularity"),pixelation=findAura("pixelation"),equinox=findAura("equinox"),breakthrough=findAura("breakthrough"),nanally=findAura("nanally");
        const auto previewIndices={pixelation,equinox,breakthrough,nanally};
        check(std::count_if(defs.begin(),defs.end(),[](const auto& d){return d.previewOnly;})==4 &&
            defs[nanally].previewOnly && defs[nanally].denominator==0,"Nanally is the fourth unpriced preview");
        aura::Roller roller(defs, 42), same(defs, 42);
        std::uint64_t start = 0;
        for (std::size_t i = 0; i < defs.size(); ++i) {
            const auto count = roller.tickets(i);
            if(defs[i].previewOnly) {
                check(count==0 && aura::probabilityTickets(defs,i)==0,"Preview-only auras have no tickets");
                continue;
            }
            check(count > 0, "Every aura must be reachable");
            check(roller.selectTicket(start) == i, "Range start");
            check(roller.selectTicket(start + count - 1) == i, "Range end");
            if (defs[i].denominator) check(count == aura::TicketPool / defs[i].denominator, "Probability weight");
            start += count;
        }
        check(start == aura::TicketPool, "Probability total");
        auto previousDefinitions=defs; std::erase_if(previousDefinitions,[](const auto& d){return d.previewOnly;});
        aura::Roller previousRoller(previousDefinitions,42);
        for(std::size_t i=0;i<defs.size();++i) if(!defs[i].previewOnly) {
            const auto old=std::find_if(previousDefinitions.begin(),previousDefinitions.end(),[&](const auto& d){return d.id==defs[i].id;});
            check(old!=previousDefinitions.end() && roller.tickets(i)==previousRoller.tickets(static_cast<std::size_t>(old-previousDefinitions.begin())),
                "Adding a preview preserves every previous aura's ticket count");
        }
        for(auto preview:previewIndices)
            check(aura::oddsLabel(defs,preview)=="Preview only" && aura::oddsInLabel(defs,preview)=="Preview only",
                "Unpriced previews do not claim a probability");
        check(roller.tickets(0) == aura::TicketPool / 2, "Common is exactly 50 percent");
        check(roller.tickets(1) == aura::TicketPool / 4, "Uncommon is exactly 25 percent");
        check(defs[3].id=="stardust" && roller.tickets(3)==69'888'989'000ULL,"Stardust receives only the remaining probability");
        check(aura::oddsInLabel(defs,1)=="1 in 4","Avatar probability uses 1 in N");
        check(defs[0].name == "Common" && defs[1].name == "Uncommon", "Renamed aura labels with stable IDs");
        std::set<std::uint64_t> uniqueWeights;
        for(std::size_t i=0;i<defs.size();++i) {
            if(defs[i].previewOnly) continue;
            check(uniqueWeights.insert(roller.tickets(i)).second, "Every demo aura has a distinct probability");
            check(aura::probabilityTickets(defs,i) == roller.tickets(i), "Displayed probability matches the actual ticket pool");
        }
        check(aura::oddsLabel(defs,0) == "1 / 2", "Common probability label");
        for(double luck:{1.5,2.,10.,1000.,1'000'000.}) {
            roller.setLuck(luck); previousRoller.setLuck(luck); std::uint64_t total=0;
            for(std::size_t i=0;i<defs.size();++i) {
                total+=roller.tickets(i);
                if(defs[i].previewOnly) continue;
                const auto old=std::find_if(previousDefinitions.begin(),previousDefinitions.end(),[&](const auto& d){return d.id==defs[i].id;});
                check(roller.tickets(i)==previousRoller.tickets(static_cast<std::size_t>(old-previousDefinitions.begin())),
                    "Preview leaves every existing aura's Luck-adjusted probability unchanged");
            }
            check(total==aura::TicketPool,"Luck preserves total probability");
            for(auto preview:previewIndices) check(roller.tickets(preview)==0,"Luck never makes previews rollable");
            check(roller.tickets(0)<aura::TicketPool/2 && roller.tickets(singularity)>1000,"Luck raises rare tail and reduces Common");
        }
        roller.setLuck(2);
        check(roller.tickets(0)==aura::TicketPool/4,"2x Luck gives Common a 25 percent probability");
        check(std::abs(static_cast<double>(roller.tickets(singularity))-2000)<=1,"Rare aura approximates twice the base odds at 2x");
        rejects([&]{roller.setLuck(NAN);}); rejects([&]{roller.setLuck(0);}); rejects([&]{roller.setLuck(1'000'001);});
        roller.setLuck(1);
        for(std::size_t i=0;i<defs.size();++i) check(roller.tickets(i)==aura::probabilityTickets(defs,i),"Reset Luck restores exact baseline");
        rejects([&] { roller.selectTicket(aura::TicketPool); });
        std::vector<std::uint64_t> observed(defs.size());
        for (int i = 0; i < 200000; ++i) { const auto n = roller.roll(); check(n == same.roll(), "Seed reproducibility"); ++observed[n]; }
        check(std::abs(static_cast<double>(observed[0]) / 200000 - .5) < .01, "Common distribution");
        check(std::abs(static_cast<double>(observed[2]) / 200000 - .125) < .005, "Ember distribution");
        check(std::abs(static_cast<double>(observed[1]) / 200000 - .25) < .005,"Uncommon distribution");
        for(auto preview:previewIndices) check(observed[preview]==0,"Preview-only auras are never rolled");
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
        invalid=defs; invalid[pixelation].denominator=2;
        rejects([&] { aura::Roller bad(invalid,1); });
        const auto dir = std::filesystem::temp_directory_path() / ("aura-rng-tests-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(dir);
        aura::Progress p(defs.size());
        for(auto preview:previewIndices) rejects([&] { p.record(preview,defs); });
        check(p.rolls==0 && p.discovered()==0,"Rejected preview records leave progress intact");
        const auto invalidPreviewSave=dir/"invalid-preview-progress.txt";
        for(auto preview:previewIndices) {
            auto invalidPreviewProgress=p; invalidPreviewProgress.equipped=preview;
            rejects([&]{aura::saveProgress(invalidPreviewSave,invalidPreviewProgress,defs);});
            invalidPreviewProgress=p; invalidPreviewProgress.best=preview;
            rejects([&]{aura::saveProgress(invalidPreviewSave,invalidPreviewProgress,defs);});
            invalidPreviewProgress=p; invalidPreviewProgress.counts[preview]=1; invalidPreviewProgress.rolls=1;
            rejects([&]{aura::saveProgress(invalidPreviewSave,invalidPreviewProgress,defs);});
        }
        check(!std::filesystem::exists(invalidPreviewSave),"Invalid preview state is rejected before writing a save");
        for(auto preview:previewIndices) {
            {std::ofstream o(invalidPreviewSave); o<<"AURA_RNG_SAVE 1\nrolls 2\nequipped "<<defs[preview].id<<"\ncount still 1\ncount "<<defs[preview].id<<" 1\n";}
            rejects([&]{aura::loadProgress(invalidPreviewSave,defs);});
            {std::ofstream o(invalidPreviewSave); o<<"AURA_RNG_SAVE 1\nrolls 1\nequipped "<<defs[preview].id<<"\ncount still 1\ncount "<<defs[preview].id<<" 0\n";}
            const auto unownedPreviewSave=aura::loadProgress(invalidPreviewSave,defs);
            check(unownedPreviewSave.equipped==0 && unownedPreviewSave.best==0 && unownedPreviewSave.counts[preview]==0,
                "A preview ID without inventory cannot become equipment or best discovery");
        }
        auto previewFirst=defs; std::swap(previewFirst[0],previewFirst[pixelation]);
        auto initialPreviewFirst=aura::loadProgress(dir/"preview-first-missing",previewFirst);
        check(initialPreviewFirst.equipped==pixelation && initialPreviewFirst.best==pixelation && !previewFirst[initialPreviewFirst.equipped].previewOnly,
            "Common is preferred after moving a preview to the first catalog row");
        previewFirst[pixelation].id="common";
        initialPreviewFirst=aura::loadProgress(dir/"preview-first-no-still-missing",previewFirst);
        check(initialPreviewFirst.equipped==1 && initialPreviewFirst.best==1,"A catalog without the old Common ID starts with its first rollable aura");
        previewFirst[0].id="still";
        initialPreviewFirst=aura::loadProgress(dir/"preview-still-missing",previewFirst);
        check(initialPreviewFirst.equipped==1 && initialPreviewFirst.best==1,"A preview-only Common ID never overrides the rollable default");
        p.record(11, defs);
        check(p.equipped == 0, "Rolling cannot equip a new aura");
        p.equipped=11; p.record(1, defs); p.record(1, defs);
        check(p.best == 11 && p.equipped == 11 && p.discovered() == 2, "Progress best/equip/discovery");
        const auto save = dir / "progress.txt";
        aura::saveProgress(save, p, defs);
        p.record(3, defs); p.equipped=3; aura::saveProgress(save, p, defs);
        const auto restored = aura::loadProgress(save, defs);
        check(restored.rolls == 4 && restored.counts == p.counts && restored.best == 11 && restored.equipped == 3, "Atomic overwrite round trip");
        auto editedCounter=p;
        const auto counterCounts=editedCounter.counts;
        editedCounter.setRollCount(1'000'000'000'000'000ULL);
        const auto editedSave=dir/"edited-counter.txt";
        aura::saveProgress(editedSave,editedCounter,defs);
        const auto editedRestored=aura::loadProgress(editedSave,defs);
        check(editedRestored.rolls==1'000'000'000'000'000ULL && editedRestored.counts==counterCounts &&
            editedRestored.equipped==p.equipped && editedRestored.best==p.best,"An edited roll counter saves independently of inventory and equipment");
        {std::ifstream input(editedSave); std::string bytes((std::istreambuf_iterator<char>(input)),{});
            check(bytes.starts_with("AURA_RNG_SAVE 2\n") && bytes.find("\ncollected 4\n")!=std::string::npos,
                "Save schema 2 records the actual inventory sum separately from the displayed roll counter");}
        editedCounter.setRollCount(0); editedCounter.record(0,defs);
        check(editedCounter.rolls==1 && editedCounter.best==p.best && editedCounter.counts[0]==counterCounts[0]+1 &&
            editedCounter.equipped==p.equipped,"Rolling after resetting the counter keeps an existing rarer best discovery");
        aura::Progress firstWithEditedCounter(defs.size()); firstWithEditedCounter.setRollCount(500); firstWithEditedCounter.record(1,defs);
        check(firstWithEditedCounter.best==1 && firstWithEditedCounter.rolls==501,"The first acquired aura sets best even after editing the counter upward");
        editedCounter.setRollCount(std::numeric_limits<std::uint64_t>::max());
        const auto maxCounterCounts=editedCounter.counts;
        rejects([&]{editedCounter.record(0,defs);});
        check(editedCounter.counts==maxCounterCounts,"A counter overflow rejects the roll before changing inventory");
        {std::ofstream out(editedSave); out<<"AURA_RNG_SAVE 1\nrolls 3\nequipped zenith\ncount zenith 2\ncount retired_star 1\n";}
        auto legacyCounter=aura::loadProgress(editedSave,defs); legacyCounter.setRollCount(0);
        aura::saveProgress(editedSave,legacyCounter,defs); const auto retiredCounter=aura::loadProgress(editedSave,defs);
        check(retiredCounter.rolls==0 && retiredCounter.counts[11]==2 && retiredCounter.retiredCounts.size()==1 &&
            retiredCounter.retiredCounts[0].second==1 && retiredCounter.equipped==11,
            "Schema 1 migrates to an independent counter while collected includes retired inventory");
        for(const char* corrupt:{"AURA_RNG_SAVE 2\nrolls 100\ncollected 3\nequipped still\ncount still 2\n",
                "AURA_RNG_SAVE 2\nrolls 2\nequipped still\ncount still 2\n",
                "AURA_RNG_SAVE 2\nrolls 2\ncollected 2\ncollected 2\nequipped still\ncount still 2\n"}) {
            {std::ofstream out(editedSave);out<<corrupt;} rejects([&]{aura::loadProgress(editedSave,defs);});
        }
        aura::Progress overflowInventory(defs.size()); overflowInventory.counts[0]=std::numeric_limits<std::uint64_t>::max(); overflowInventory.counts[1]=1;
        rejects([&]{aura::saveProgress(dir/"overflow-inventory.txt",overflowInventory,defs);});
        check(!std::filesystem::exists(dir/"overflow-inventory.txt"),"Inventory-total overflow is rejected before writing");
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
        for(auto preview:previewIndices) rejects([&] { aura::appendLog(log,{3,"2026-01-01T00:00:02Z",preview},defs[preview]); });
        check(std::filesystem::file_size(log)==content.size(),"Rejected preview log leaves CSV intact");
        { std::ofstream o(dir / "bad.csv"); o << "id,name,denominator,red,green,blue,particles,radius,speed\na,\"unclosed,0,1,1,1,1,1,1\n"; }
        rejects([&] { aura::loadCatalog(dir / "bad.csv"); });
        {std::ofstream o(dir/"legacy.csv"); o<<"id,name,denominator,red,green,blue,particles,radius,speed\ncommon,Common,0,1,1,1,0,1,1\n";}
        const auto legacyCatalog=aura::loadCatalog(dir/"legacy.csv");
        check(legacyCatalog.size()==1 && !legacyCatalog[0].previewOnly,"Legacy nine-column catalogs remain rollable");
        {std::ofstream o(dir/"bad-preview.csv"); o<<"id,name,denominator,red,green,blue,particles,radius,speed,preview_only\ncommon,Common,0,1,1,1,0,1,1,2\n";}
        rejects([&]{aura::loadCatalog(dir/"bad-preview.csv");});
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
        autoRoll=true; const auto prototypeRolls=p.rolls; const auto prototypeCounts=p.counts;
        presentation.showPreview(pixelation,defs.size(),autoRoll);
        check(!autoRoll && presentation.shown(p)==pixelation && p.rolls==prototypeRolls && p.counts==prototypeCounts,
            "Pixelation can be previewed without equipment, inventory, or history changes");
        autoRoll=true; presentation.showPreview(equinox,defs.size(),autoRoll);
        check(!autoRoll && presentation.shown(p)==equinox && p.rolls==prototypeRolls && p.counts==prototypeCounts,
            "Equinox preview leaves roll history and inventory untouched");
        autoRoll=true; presentation.showPreview(breakthrough,defs.size(),autoRoll);
        check(!autoRoll && presentation.shown(p)==breakthrough && p.equipped==3 && p.rolls==prototypeRolls && p.counts==prototypeCounts,
            "BreakThrough preview stops AutoRoll and leaves equipment, roll history, and inventory untouched");
        autoRoll=true; presentation.showPreview(nanally,defs.size(),autoRoll);
        check(!autoRoll && presentation.shown(p)==nanally && p.equipped==3 && p.rolls==prototypeRolls && p.counts==prototypeCounts,
            "Nanally preview stops AutoRoll and leaves equipment, roll history, and inventory untouched");
        const auto settingsPath=dir/"settings.txt";
        check(aura::loadSettings(settingsPath).language==aura::Language::English,"Settings default");
        aura::Settings settings; settings.language=aura::Language::Japanese; settings.shirt={.95f,.2f,.3f};
        settings.luck=12.5; settings.avatar=aura::Avatar::Vrm; settings.biome=aura::Biome::Desert;
        settings.vrmPath="C:/モデル/Chocolat.vrm"; settings.bgmPath="C:/Music/Tides of the Outer Reach.mp3"; settings.volume=.35f;
        settings.pixelationBgmPath="C:/音楽/Pixelation base.mp3";
        settings.pixelationFinalBgmPath="C:/Music/Pixelation \"final\".mp3";
        settings.username="星の \"Player\" 🌟"; settings.discordEnabled=false; settings.discordApplicationId="12345678901234567890";
        aura::saveSettings(settingsPath,settings); const auto loadedSettings=aura::loadSettings(settingsPath);
        check(loadedSettings.language==aura::Language::Japanese && std::abs(loadedSettings.shirt.r-.95f)<.0001f,
            "Language and shirt color round trip");
        check(loadedSettings.luck==12.5 && loadedSettings.avatar==aura::Avatar::Vrm && loadedSettings.biome==aura::Biome::Desert &&
            loadedSettings.vrmPath==settings.vrmPath && loadedSettings.bgmPath==settings.bgmPath && loadedSettings.volume==settings.volume &&
            loadedSettings.pixelationBgmPath==settings.pixelationBgmPath && loadedSettings.pixelationFinalBgmPath==settings.pixelationFinalBgmPath,
            "Extended settings / UTF-8 audio paths / quoted names round trip");
        check(loadedSettings.username==settings.username && !loadedSettings.discordEnabled && loadedSettings.discordApplicationId==settings.discordApplicationId,
            "Schema 4 preserves Unicode/quoted usernames and Discord preferences");
        settings.breakthroughCaptionOffset=-.18f;
        settings.breakthroughWordTimes={21.f,21.19f,21.4f,21.72f,22.11f,22.24f,22.6f,23.2f};
        for(unsigned i=0;i<aura::StageCount;++i) {
            settings.biome=static_cast<aura::Biome>(i); aura::saveSettings(settingsPath,settings);
            const auto planetSettings=aura::loadSettings(settingsPath);
            check(planetSettings.biome==settings.biome && planetSettings.breakthroughCaptionOffset==-.18f &&
                planetSettings.breakthroughWordTimes==settings.breakthroughWordTimes,
                "All five planets and custom per-word caption times survive save/reload");
            check(!aura::stageDisplayName(settings.biome,aura::Language::English).empty() &&
                !aura::stageDisplayName(settings.biome,aura::Language::Japanese).empty(),"All planets have bilingual names");
        }
        settings.biome=static_cast<aura::Biome>(5); rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.biome=aura::Biome::Desert;
        settings.breakthroughCaptionOffset=NAN; rejects([&]{aura::saveSettings(settingsPath,settings);});
        settings.breakthroughCaptionOffset=5.1f; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.breakthroughCaptionOffset=0;
        settings.breakthroughWordTimes[1]=settings.breakthroughWordTimes[0]; rejects([&]{aura::saveSettings(settingsPath,settings);});
        settings.breakthroughWordTimes=aura::tuning::cutscene::BreakthroughWordTimes;
        {std::ofstream o(settingsPath); o<<"STARS_RNG_SETTINGS 4\nlanguage ja\nshirt 0.5 0.7 0.9\nluck 2.5\navatar 0\nbiome 1\nvolume 0.25\nvrm \"\"\nbgm \"space.mp3\"\npixelation_bgm \"base.mp3\"\npixelation_final_bgm \"final.mp3\"\nusername \"Existing Player\"\ndiscord_enabled 0\ndiscord_application_id \"12345\"\n";}
        const auto migratedV4=aura::loadSettings(settingsPath);
        check(migratedV4.biome==aura::Biome::Desert && migratedV4.username=="Existing Player" && !migratedV4.discordEnabled &&
            migratedV4.breakthroughCaptionOffset==0 && migratedV4.breakthroughWordTimes==aura::tuning::cutscene::BreakthroughWordTimes,
            "Schema 4 preserves current users and adds the corrected caption defaults");
        aura::StageTravel flight;
        check(!flight.active() && !flight.start(aura::Biome::Normal,aura::Biome::Normal) &&
            !flight.start(static_cast<aura::Biome>(99),aura::Biome::Normal),"Current/invalid planets cannot start a flight");
        check(flight.start(aura::Biome::Ocean,aura::Biome::Normal) && !flight.start(aura::Biome::Forest,aura::Biome::Normal),
            "A flight locks its destination while underway");
        check(!flight.update(.49f,1.f) && flight.progress()<.5f,"A planet does not switch before the covered midpoint");
        check(flight.update(.02f,1.f)==aura::Biome::Ocean && !flight.update(.02f,1.f),"Destination is applied exactly once at the midpoint");
        check(!flight.update(3.f,1.f) && flight.arrived(),"Long frames complete a flight without applying the destination twice");
        flight.finish();
        check(!flight.active() && flight.start(aura::Biome::Volcano,aura::Biome::Ocean),"Arrival unlocks a new flight");
        check(!flight.update(NAN,1.f) && flight.progress()==0,"Invalid time cannot poison the travel clock");
        check(flight.update(2.f,1.f)==aura::Biome::Volcano && flight.arrived(),"A single long frame still applies its planet and arrives");
        const auto& words=aura::tuning::cutscene::BreakthroughWordTimes;
        check(!aura::tuning::cutscene::breakthroughWordVisible(words[0]-.001f,0,words) &&
            aura::tuning::cutscene::breakthroughWordVisible(words[0],0,words) &&
            !aura::tuning::cutscene::breakthroughWordVisible(words[0],1,words),"Caption words use their individual measured onsets");
        check(aura::tuning::cutscene::breakthroughWordVisible(words[4]-.1f,4,words,-.1f) &&
            !aura::tuning::cutscene::breakthroughWordVisible(words[4],4,words,.1f),"Caption offset advances or delays without changing audio");
        auto lateWords=words; lateWords.back()=28.5f;
        check(aura::tuning::cutscene::breakthroughWordVisible(29.f,7,lateWords) &&
            !aura::tuning::cutscene::breakthroughWordVisible(30.f,7,lateWords,4.f),"Late customized words keep their hold time within cutscene boundary");
        {std::ofstream o(settingsPath); o<<"STARS_RNG_SETTINGS 1\nlanguage ja\nshirt 0.5 0.7 0.9\n";}
        const auto migrated=aura::loadSettings(settingsPath);
        check(migrated.language==aura::Language::Japanese && migrated.luck==1 && migrated.avatar==aura::Avatar::Block &&
            migrated.username=="Player" && migrated.discordEnabled && migrated.discordApplicationId=="1560569853184573590","Schema 1 settings migrate with the game Discord Application ID");
        {std::ofstream o(settingsPath); o<<"STARS_RNG_SETTINGS 2\nlanguage en\nshirt 0.5 0.7 0.9\nluck 2.5\navatar 1\nbiome 1\nvolume 0.25\nvrm \"avatar.vrm\"\nbgm \"space.mp3\"\n";}
        const auto migratedV2=aura::loadSettings(settingsPath);
        check(migratedV2.luck==2.5 && migratedV2.vrmPath=="avatar.vrm" && migratedV2.bgmPath=="space.mp3" &&
            migratedV2.pixelationBgmPath.empty() && migratedV2.pixelationFinalBgmPath.empty() && migratedV2.username=="Player" &&
            migratedV2.discordEnabled && migratedV2.discordApplicationId=="1560569853184573590","Schema 2 retains audio paths and defaults new profile settings");
        {std::ofstream o(settingsPath); o<<"STARS_RNG_SETTINGS 3\nlanguage en\nshirt 0.5 0.7 0.9\nluck 2.5\navatar 1\nbiome 1\nvolume 0.25\nvrm \"avatar.vrm\"\nbgm \"space.mp3\"\npixelation_bgm \"base.mp3\"\npixelation_final_bgm \"final.mp3\"\n";}
        const auto migratedV3=aura::loadSettings(settingsPath);
        check(migratedV3.pixelationBgmPath=="base.mp3" && migratedV3.pixelationFinalBgmPath=="final.mp3" && migratedV3.username=="Player" &&
            migratedV3.discordEnabled && migratedV3.discordApplicationId=="1560569853184573590","Schema 3 retains Pixelation music and defaults new profile settings");
        check(aura::validUsername(std::string(64,'a')) && aura::validUsername("星🌟") && !aura::validUsername(std::string(65,'a')) &&
            !aura::validUsername("") && !aura::validUsername("bad\nname") && !aura::validUsername("bad\tname") &&
            !aura::validUsername(std::string("a\0b",3)) && !aura::validUsername("a\x7f") && !aura::validUsername("\xc0\xaf") &&
            !aura::validUsername("\xed\xa0\x80") && !aura::validUsername("\xf4\x90\x80\x80") && !aura::validUsername("\xe2\x80\xa8"),
            "Username validation enforces byte limits, strict UTF-8, and no control characters or line breaks");
        check(aura::validDiscordApplicationId("") && aura::validDiscordApplicationId("12345678901234567890") &&
            !aura::validDiscordApplicationId("123456789012345678901") && !aura::validDiscordApplicationId("12a") &&
            !aura::validDiscordApplicationId("12\n"),"Discord Application IDs accept only up to 20 digits or an empty value");
        settings.username="bad\nname"; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.username="Player";
        settings.discordApplicationId="wrong"; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.discordApplicationId.clear();
        aura::saveSettings(settingsPath,settings);
        check(aura::loadSettings(settingsPath).discordApplicationId.empty(),"An explicitly cleared Application ID stays empty rather than restoring the default");
        settings.luck=INFINITY; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.luck=1;
        settings.avatar=static_cast<aura::Avatar>(3); rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.avatar=aura::Avatar::Block;
        settings.vrmPath="bad\npath"; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.vrmPath.clear();
        settings.pixelationBgmPath="bad\npath"; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.pixelationBgmPath.clear();
        settings.pixelationFinalBgmPath="bad\rpath"; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.pixelationFinalBgmPath.clear();
        settings.shirt.g=NAN; rejects([&] { aura::saveSettings(settingsPath,settings); });
        check(aura::displayName(defs[0],aura::Language::Japanese)=="コモン","Japanese aura names");
        check(aura::displayName(defs[pixelation],aura::Language::Japanese)=="ピクセレーション","Japanese Pixelation name");
        check(aura::displayName(defs[equinox],aura::Language::Japanese)=="エキノックス","Japanese Equinox name");
        check(aura::displayName(defs[breakthrough],aura::Language::Japanese)=="ブレイクスルー","Japanese BreakThrough name");
        check(aura::displayName(defs[nanally],aura::Language::Japanese)=="ナナリー" && aura::displayName(defs[nanally],aura::Language::English)=="Nanally","Bilingual Nanally name");
        check(!aura::cooldownVisual(0).visible && !aura::cooldownVisual(-1).visible,"Cooldown bar disappears at completion");
        check(aura::cooldownVisual(aura::RollCooldown).opacity==0,"Cooldown fades in");
        check(aura::cooldownVisual(.001f).opacity<.01f,"Cooldown fades out");
        const auto waveA=aura::cooldownVisual(aura::RollCooldown*.875f);
        const auto waveB=aura::cooldownVisual(aura::RollCooldown*.625f);
        check(waveA.progress<waveB.progress && waveA.waveY>2.9f && waveB.waveY<-2.9f,"Star follows a left-to-right wave");
        check(aura::formatNumber(1000000000) == "1,000,000,000", "Number formatting");
        aura::Progress levels(defs.size());
        check(levels.auraLevel(singularity,defs)==0,"Unowned Singularity starts at level zero");
        for(unsigned count=1;count<=6;++count) {
            levels.record(singularity,defs);
            check(levels.auraLevel(singularity,defs)==std::min(3u,count-1),"Duplicate-based Aura Level, capped at three");
        }
        aura::saveProgress(save,levels,defs);
        check(aura::loadProgress(save,defs).auraLevel(singularity,defs)==3,"Aura Level survives save via stable counts");
        auto previousCatalog=previousDefinitions; previousCatalog.erase(previousCatalog.begin()+3);
        check(previousCatalog.size()==12 && previousDefinitions.size()==13,"Historical rollable catalogs contain 12 and 13 entries");
        aura::Progress previousProgress(previousCatalog.size());
        for(int i=0;i<3;++i) previousProgress.record(previousCatalog.size()-1,previousCatalog);
        previousProgress.equipped=previousCatalog.size()-1; aura::saveProgress(save,previousProgress,previousCatalog);
        const auto migratedProgress=aura::loadProgress(save,defs);
        check(migratedProgress.equipped==singularity && migratedProgress.auraLevel(singularity,defs)==2 && migratedProgress.counts[3]==0,
            "Previous 12-aura saves retain Singularity counts and gain a derived Aura Level");
        for(auto preview:previewIndices) check(migratedProgress.counts[preview]==0,"12-aura saves add every preview with zero inventory");
        aura::Progress v013Progress(previousDefinitions.size());
        for(int i=0;i<4;++i) v013Progress.record(previousDefinitions.size()-1,previousDefinitions);
        v013Progress.equipped=previousDefinitions.size()-1; aura::saveProgress(save,v013Progress,previousDefinitions);
        const auto v014Progress=aura::loadProgress(save,defs);
        check(v014Progress.rolls==4 && v014Progress.equipped==singularity && v014Progress.auraLevel(singularity,defs)==3 &&
            v014Progress.counts[pixelation]==0 && v014Progress.counts[equinox]==0 && v014Progress.counts[breakthrough]==0 && v014Progress.counts[nanally]==0,
            "Previous 13-aura saves retain counts, equipment, and Singularity level");
        auto previousV014=defs;
        std::erase_if(previousV014,[](const auto& d){return d.id=="equinox" || d.id=="breakthrough" || d.id=="nanally";});
        check(previousV014.size()==14,"The v0.1.4 catalog contains 13 rollable auras and Pixelation");
        aura::Progress v014Saved(previousV014.size());
        const auto v014Singularity=static_cast<std::size_t>(std::find_if(previousV014.begin(),previousV014.end(),[](const auto& d){return d.id=="singularity";})-previousV014.begin());
        v014Saved.record(v014Singularity,previousV014); v014Saved.equipped=v014Singularity;
        aura::saveProgress(save,v014Saved,previousV014); const auto v015Progress=aura::loadProgress(save,defs);
        check(v015Progress.equipped==singularity && v015Progress.counts[singularity]==1 && v015Progress.counts[equinox]==0 &&
            v015Progress.counts[pixelation]==0 && v015Progress.counts[breakthrough]==0 && v015Progress.counts[nanally]==0,
            "Previous 14-entry saves add Equinox, BreakThrough, and Nanally with zero preview inventory");
        auto previousV015=defs; std::erase_if(previousV015,[](const auto& d){return d.id=="breakthrough" || d.id=="nanally";});
        check(previousV015.size()==15,"The v0.1.5 catalog contains 13 rollable auras and two previews");
        aura::Progress v015Saved(previousV015.size());
        const auto v015Singularity=static_cast<std::size_t>(std::find_if(previousV015.begin(),previousV015.end(),[](const auto& d){return d.id=="singularity";})-previousV015.begin());
        for(unsigned i=0;i<4;++i) v015Saved.record(v015Singularity,previousV015);
        v015Saved.record(0,previousV015); v015Saved.equipped=v015Singularity;
        aura::saveProgress(save,v015Saved,previousV015); const auto v016Progress=aura::loadProgress(save,defs);
        check(v016Progress.rolls==5 && v016Progress.equipped==singularity && v016Progress.best==singularity &&
            v016Progress.counts[0]==1 && v016Progress.counts[singularity]==4 && v016Progress.auraLevel(singularity,defs)==3 &&
            v016Progress.counts[pixelation]==0 && v016Progress.counts[equinox]==0 && v016Progress.counts[breakthrough]==0 && v016Progress.counts[nanally]==0,
            "Previous 15-entry saves preserve all discoveries and equipment while adding BreakThrough and Nanally with zero inventory");
        auto previousV017=defs; std::erase_if(previousV017,[](const auto& d){return d.id=="nanally";});
        check(previousV017.size()==16,"The v0.1.7 catalog contains 13 rollable auras and three previews");
        aura::Progress v017Saved(previousV017.size());
        const auto v017Singularity=static_cast<std::size_t>(std::find_if(previousV017.begin(),previousV017.end(),[](const auto& d){return d.id=="singularity";})-previousV017.begin());
        for(unsigned i=0;i<3;++i) v017Saved.record(v017Singularity,previousV017);
        v017Saved.record(1,previousV017); v017Saved.equipped=v017Singularity; v017Saved.setRollCount(123456);
        aura::saveProgress(save,v017Saved,previousV017); const auto v018Progress=aura::loadProgress(save,defs);
        check(v018Progress.rolls==123456 && v018Progress.equipped==singularity && v018Progress.best==singularity &&
            v018Progress.counts[1]==1 && v018Progress.counts[singularity]==3 && v018Progress.auraLevel(singularity,defs)==2 &&
            v018Progress.counts[pixelation]==0 && v018Progress.counts[equinox]==0 && v018Progress.counts[breakthrough]==0 && v018Progress.counts[nanally]==0,
            "Previous 16-entry saves preserve the edited roll counter, discoveries, equipment, and levels while adding Nanally with zero inventory");
        const auto levelCounts=levels.counts;
        presentation.preview=singularity; presentation.singularityLevel=1;
        check(presentation.level(levels,defs)==1 && levels.counts==levelCounts,"Debug Aura Level leaves inventory untouched");
        presentation.singularityLevel.reset(); check(presentation.level(levels,defs)==3,"Actual Aura Level restored");
        presentation.preview=0; presentation.singularityLevel=3; check(presentation.level(levels,defs)==0,"Other auras have no Singularity level");
        presentation.preview=pixelation;
        check(presentation.level(levels,defs)==0 && levels.auraLevel(pixelation,defs)==0,"Unowned Pixelation starts at level zero");
        for(unsigned level=0;level<=3;++level) {
            presentation.pixelationLevel=level;
            check(presentation.level(levels,defs)==level && levels.counts==levelCounts,"Pixelation levels are isolated previews");
        }
        presentation.pixelationLevel=9; check(presentation.level(levels,defs)==3,"Pixelation preview level is capped");
        presentation.preview=singularity; check(presentation.level(levels,defs)==3,"Pixelation level cannot override Singularity");
        presentation.singularityLevel=9; check(presentation.level(levels,defs)==3,"Singularity preview level remains capped");
        presentation.preview=equinox; check(presentation.level(levels,defs)==0,"Equinox has no borrowed Aura Level");
        presentation.preview=breakthrough; check(presentation.level(levels,defs)==0,"BreakThrough has no borrowed Aura Level");
        presentation.preview=nanally; check(presentation.level(levels,defs)==0,"Nanally has no borrowed Aura Level");
        presentation.pixelationLevel.reset(); presentation.preview=pixelation;
        check(presentation.level(levels,defs)==0,"Restoring Pixelation's owned level returns to zero");
        auto futurePixelation=defs; futurePixelation[pixelation].previewOnly=false; futurePixelation[pixelation].denominator=2'000'000;
        aura::Progress futurePixelationProgress(defs.size());
        for(unsigned count=1;count<=6;++count) {
            futurePixelationProgress.record(pixelation,futurePixelation);
            check(futurePixelationProgress.auraLevel(pixelation,futurePixelation)==std::min(3u,count-1),
                "Pixelation's future duplicate rule starts at zero and caps at three");
        }
        const auto fixedIdle=aura::avatarPose(2.4f,0,0,true),fixedMoving=aura::avatarPose(2.4f,1.3f,1,true);
        check(fixedIdle.fixed && fixedMoving.fixed && fixedIdle.armPitch==fixedMoving.armPitch && fixedIdle.armOut==fixedMoving.armOut &&
            fixedIdle.legPitch==fixedMoving.legPitch && fixedIdle.knee==fixedMoving.knee && fixedIdle.lift==fixedMoving.lift,"Fixed aura pose ignores walking");
        const auto idle=aura::avatarPose(2.4f,0,0,false),walkA=aura::avatarPose(2.4f,.5f,1,false),walkB=aura::avatarPose(2.4f,1.5f,1,false);
        check(!idle.fixed && idle.legPitch[0]==0 && walkA.legPitch[0]*walkA.legPitch[1]<0 && walkA.legPitch!=walkB.legPitch,
            "Standing has no gait; walking alternates legs");
        const auto pixelPose=aura::pixelationPose(2.4f);
        check(pixelPose.fixed && std::isfinite(pixelPose.lift) && std::isfinite(pixelPose.bodyRoll) &&
            pixelPose.armOut[0]==pixelPose.armOut[1] && pixelPose.armOut[0]>.5f && pixelPose.lift>.3f,
            "Pixelation uses a finite fixed floating pose shared by both avatars");
        const auto equinoxPose=aura::equinoxPose(2.4f);
        check(equinoxPose.fixed && std::isfinite(equinoxPose.lift) && std::isfinite(equinoxPose.bodyRoll) &&
            equinoxPose.armOut[0]==equinoxPose.armOut[1] && equinoxPose.armOut[0]>.3f && equinoxPose.lift>.23f &&
            equinoxPose.legPitch[0]==0 && equinoxPose.legPitch[1]==0,"Equinox has a finite calm fixed floating pose");
        check(aura::EquinoxCutsceneDuration==10.9f,"Equinox cutscene has one shared duration");
        check(aura::pixelationMotionTime(3,0)==3 && std::abs(aura::pixelationMotionTime(3,1)-4.8f)<.00001f &&
            aura::pixelationMotionTime(3,1)==aura::pixelationMotionTime(3,2) && aura::pixelationMotionTime(3,3)==9 &&
            aura::pixelationMotionSpeed(3)==3 && aura::pixelationMotionSpeed(3)>aura::pixelationMotionSpeed(2),
            "Pixelation levels one and two run at 1.6x and level three accelerates the shared motion clock to 3x");
        check(aura::pixelationMotionTime(-1,1)==0 && aura::pixelationMotionTime(NAN,1)==0,"Invalid motion times do not propagate");
        check(aura::pixelationActionTime(3,0)==3 && std::abs(aura::pixelationActionTime(3,2)-4.8f)<.00001f &&
            aura::pixelationActionTime(3,2)==aura::pixelationActionTime(3,3) && aura::pixelationActionSpeed(2)==aura::pixelationActionSpeed(3) &&
            aura::pixelationActionSpeed(3)<aura::pixelationMotionSpeed(3),
            "Level three keeps volley and floating action at level two speed while the surrounding aura runs faster");
        check(aura::pixelationActionTime(-1,3)==0 && aura::pixelationActionTime(NAN,3)==0,"Invalid action times do not propagate");
        auto pointDistance=[](const aura::EffectPoint& a,const aura::EffectPoint& b) {
            return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
        };
        for(unsigned cycle=0;cycle<20;++cycle) {
            const float launchTime=cycle*aura::PixelationVolleyPeriod;
            const auto launch=aura::pixelationVolley(launchTime+.0001f);
            const auto midway=aura::pixelationVolley(launchTime+aura::PixelationVolleyFlight*.5f);
            const auto impact=aura::pixelationVolley(launchTime+aura::PixelationVolleyFlight+.0001f);
            check(launch.shotCycle==cycle && launch.flightVisible && launch.flightProgress<.001f &&
                pointDistance(launch.balls[0],launch.launches[0])<.002f && pointDistance(launch.balls[1],launch.launches[1])<.002f,
                "Both players fire from the captured launch coordinates at the same time");
            check(midway.flightVisible && pointDistance(midway.players[0],midway.launches[0])>.1f &&
                pointDistance(midway.balls[0],midway.balls[1])>1,"Players keep orbiting while the independent balls converge");
            check(!impact.flightVisible && impact.flightProgress==1 && impact.impactCycle==cycle && impact.impactAge>=0 && impact.impactAge<.001f &&
                pointDistance(impact.balls[0],impact.meeting)==0 && pointDistance(impact.balls[1],impact.meeting)==0,
                "Both balls meet at exactly the same point on the shared impact event");
        }
        const auto initialVolley=aura::pixelationVolley(0),lateVolley=aura::pixelationVolley(aura::PixelationVolleyPeriod+.1f);
        check(initialVolley.shotCycle==0 && initialVolley.impactCycle==-1 && initialVolley.impactAge==-1 &&
            lateVolley.shotCycle==1 && lateVolley.impactCycle==0 && lateVolley.impactAge>1,
            "Volley shot and impact cycle IDs support synchronized one-shot audio events");
        for(unsigned cycle=0;cycle<12;++cycle) {
            const float actionLaunch=cycle*aura::PixelationVolleyPeriod;
            const float elapsedLaunch=actionLaunch/aura::pixelationActionSpeed(3);
            const float orbitLaunch=aura::pixelationMotionTime(elapsedLaunch,3);
            const float actionNow=actionLaunch+.0001f;
            const auto fastLaunch=aura::pixelationVolley(actionNow,aura::pixelationMotionTime(actionNow/aura::pixelationActionSpeed(3),3),orbitLaunch);
            check(fastLaunch.flightVisible && fastLaunch.shotCycle==cycle &&
                pointDistance(fastLaunch.players[0],fastLaunch.launches[0])<.001f &&
                pointDistance(fastLaunch.players[1],fastLaunch.launches[1])<.001f,
                "Slow volley launches coincide with the actual fast-orbiting players");
            const float actionImpact=actionLaunch+aura::PixelationVolleyFlight+.0001f;
            const float elapsedImpact=actionImpact/aura::pixelationActionSpeed(3);
            const auto levelThree=aura::pixelationVolley(actionImpact,aura::pixelationMotionTime(elapsedImpact,3),orbitLaunch);
            const auto levelTwo=aura::pixelationVolley(actionImpact);
            check(levelThree.shotCycle==levelTwo.shotCycle && levelThree.impactCycle==levelTwo.impactCycle &&
                levelThree.flightProgress==levelTwo.flightProgress && levelThree.impactAge==levelTwo.impactAge &&
                pointDistance(levelThree.balls[0],levelThree.meeting)==0 && pointDistance(levelThree.balls[1],levelThree.meeting)==0,
                "Both levels collide simultaneously and share the same explosion age despite different orbit clocks");
        }
        const float launchClock=aura::PixelationVolleyPeriod*2;
        const auto beforeSpeedSwitch=aura::pixelationVolley(launchClock+.24f,launchClock+.24f,launchClock);
        const auto afterSpeedSwitch=aura::pixelationVolley(launchClock+.40f,launchClock+.54f,launchClock);
        check(pointDistance(beforeSpeedSwitch.launches[0],afterSpeedSwitch.launches[0])==0 &&
            pointDistance(beforeSpeedSwitch.launches[1],afterSpeedSwitch.launches[1])==0 &&
            afterSpeedSwitch.flightProgress>beforeSpeedSwitch.flightProgress &&
            pointDistance(beforeSpeedSwitch.players[0],afterSpeedSwitch.players[0])>.1f,
            "Keeping the shot orbit anchor fixed preserves both projectile origins across a mid-flight level switch");
        const auto slowPose=aura::pixelationPose(aura::pixelationActionTime(2.4f,2));
        const auto fastAuraPose=aura::pixelationPose(aura::pixelationActionTime(2.4f,3));
        check(slowPose.lift==fastAuraPose.lift && slowPose.bodyRoll==fastAuraPose.bodyRoll && slowPose.armPitch==fastAuraPose.armPitch,
            "Avatar floating has exactly the same phase at levels two and three");
        check(aura::StageHalfExtent==32 && aura::StageWalkLimit>30,"Stage width is four times the original 16 units");
        const auto starA=aura::singularityStar(.5f,0,160),starB=aura::singularityStar(2.5f,0,160),starC=aura::singularityStar(4.5f,0,160);
        auto distance=[](const aura::AbsorbedStar& s){return std::sqrt(s.x*s.x+s.z*s.z+(s.y-5.7f)*(s.y-5.7f));};
        check(distance(starA)>distance(starB) && distance(starB)>distance(starC) && starA.size>starC.size,
            "Stars spiral toward the black hole and shrink before respawning");
        std::filesystem::remove_all(dir);
        std::cout << "PASS: exact Common probability, distinct odds, fixed equipment, preview isolation, bilingual settings, shirt colors, cooldown wave, save/restore, UTF-8 CSV\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
