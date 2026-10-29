#include "core/aura.hpp"
#include "core/presentation.hpp"
#include "core/animation.hpp"
#include "core/audio_envelope.hpp"
#include "core/equinox_animation.hpp"
#include "core/shell.hpp"
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
        {
            aura::shell::State menu;
            menu.tapStar();menu.update(aura::tuning::shell::LogoSpinSeconds*.4f);
            const auto progress=menu.spinProgress();menu.tapStar();
            check(menu.spinning() && !menu.leftOpen && menu.spinProgress()==progress,"Double tap restarted or prematurely opened star menu");
            menu.update(aura::tuning::shell::LogoSpinSeconds*.61f);
            check(menu.leftOpen && !menu.spinning() && !menu.rightOpen,"Menu must open after one full spin");
            menu.tapStar();menu.update(3);
            check(!menu.active(),"Closing left menu reopened it later");
            menu.tapStar();menu.update(.1f);menu.toggleRight();menu.update(3);
            check(menu.rightOpen && !menu.leftOpen && !menu.spinning(),"Right menu did not cancel pending left menu reveal");
            menu.close();menu.update(3);check(!menu.active(),"Dismissed menus reopened");
        }
        const auto defs = aura::loadCatalog(argv[1]);
        check(defs.size() == 23, "Expected 22 rollable auras and one administrator preview");
        const auto findAura=[&](const std::string& id) {
            const auto found=std::find_if(defs.begin(),defs.end(),[&](const auto& d){return d.id==id;});
            check(found!=defs.end(),"Required aura ID"); return static_cast<std::size_t>(found-defs.begin());
        };
        const auto singularity=findAura("singularity"),pixelation=findAura("pixelation"),equinox=findAura("equinox"),breakthrough=findAura("breakthrough"),nanally=findAura("nanally");
        const auto illusionary=findAura("illusionary"),dune=findAura("dune"),mirage=findAura("mirage"),pharaoh=findAura("pharaoh"),frutiger=findAura("frutiger_aero");
        const auto redJohn=findAura("red_john");
        const auto previewIndices={nanally};
        const auto extraIndices={pixelation,equinox,breakthrough,illusionary,frutiger,redJohn};
        const auto promotedExtraIndices={pixelation,equinox,breakthrough,illusionary};
        check(std::count_if(defs.begin(),defs.end(),[](const auto& d){return d.previewOnly;})==1 && defs[nanally].previewOnly,
            "Only Nanally remains an administrator preview");
        check(aura::auraCategory(defs[0])==aura::AuraCategory::Normal && aura::auraCategory(defs[singularity])==aura::AuraCategory::Normal &&
            aura::auraCategory(defs[dune])==aura::AuraCategory::Desert && aura::auraCategory(defs[mirage])==aura::AuraCategory::Desert &&
            aura::auraCategory(defs[pharaoh])==aura::AuraCategory::Desert && aura::auraCategory(defs[nanally])==aura::AuraCategory::Administrator,
            "Collection groups have stable categories independent of CSV order");
        const auto categoryCount=[&](aura::AuraCategory category) {
            return std::count_if(defs.begin(),defs.end(),[&](const auto& definition){return aura::auraCategory(definition)==category;});
        };
        check(categoryCount(aura::AuraCategory::Normal)==13 && categoryCount(aura::AuraCategory::Desert)==3 &&
            categoryCount(aura::AuraCategory::Extra)==6 && categoryCount(aura::AuraCategory::Administrator)==1,
            "Collection contains 13 Normal, 3 Desert, 6 Extra, and 1 Administrator auras");
        aura::Roller roller(defs,42),same(defs,42);
        for(unsigned stage=0;stage<aura::StageCount;++stage) {
            const auto biome=static_cast<aura::Biome>(stage);
            aura::Roller stageRoller(defs,73,biome),stageSame(defs,73,biome);
            std::uint64_t start=0;
            for(std::size_t i=0;i<defs.size();++i) {
                const bool normal=aura::auraCategory(defs[i])==aura::AuraCategory::Normal;
                const bool eligible=normal || (biome==aura::Biome::Desert && aura::auraCategory(defs[i])==aura::AuraCategory::Desert) ||
                    (biome==aura::Biome::Extra && aura::auraCategory(defs[i])==aura::AuraCategory::Extra);
                check(aura::availableInBiome(defs[i],biome)==eligible,"Each planet enforces its own aura access");
                const auto count=stageRoller.tickets(i);
                check(count==aura::probabilityTickets(defs,i,biome),"Displayed stage odds match the actual ticket distribution");
                if(!eligible) {check(count==0,"Unavailable auras get no ticket interval"); continue;}
                check(count>0,"Every eligible aura has a reachable ticket interval");
                check(stageRoller.selectTicket(start)==i && stageRoller.selectTicket(start+count-1)==i,"Both interval boundaries select the eligible aura");
                if(defs[i].denominator) check(count==aura::TicketPool/defs[i].denominator,"Declared stage odds remain exact");
                start+=count;
            }
            check(start==aura::TicketPool,"Every stage sums to exactly 100 percent");
            check(stageRoller.tickets(0)==aura::TicketPool/2 && stageRoller.tickets(1)==aura::TicketPool/4,
                "Common and Uncommon retain exact 1/2 and 1/4 on every planet");
            for(unsigned i=0;i<20000;++i) {
                const auto result=stageRoller.roll();
                check(result==stageSame.roll() && aura::availableInBiome(defs[result],biome),"Seeded rolls are deterministic and never leave the planet's catalog");
            }
            for(double luck:{1.5,2.,10.,1000.,1'000'000.}) {
                stageRoller.setLuck(luck); std::uint64_t total=0;
                for(std::size_t i=0;i<defs.size();++i) {
                    total+=stageRoller.tickets(i);
                    if(!aura::availableInBiome(defs[i],biome)) check(stageRoller.tickets(i)==0,"Luck cannot unlock a different planet's aura");
                }
                check(total==aura::TicketPool,"Stage Luck adjustment preserves 100 percent total");
                check(stageRoller.tickets(0)<aura::TicketPool/2 && stageRoller.tickets(singularity)>1000,"Luck raises rare odds within the chosen planet");
            }
            stageRoller.setLuck(1);
            for(std::size_t i=0;i<defs.size();++i) check(stageRoller.tickets(i)==aura::probabilityTickets(defs,i,biome),"Luck reset restores the chosen stage's baseline");
        }
        check(roller.tickets(3)==69'888'989'000ULL,"Normal Stardust receives the exact unused probability");
        roller.setBiome(aura::Biome::Desert);
        check(roller.tickets(3)==49'684'989'000ULL && roller.tickets(dune)==aura::TicketPool/50 &&
            roller.tickets(mirage)==aura::TicketPool/5000 && roller.tickets(pharaoh)==aura::TicketPool/250000,
            "Desert keeps its three original distinct probabilities");
        roller.setLuck(2); roller.setBiome(aura::Biome::Extra);
        check(roller.luck()==2 && roller.tickets(0)==aura::TicketPool/4 && roller.tickets(dune)==0,
            "Planet change reapplies existing Luck and clears stale desert weights");
        roller.setLuck(1);
        check(roller.tickets(3)==69'887'139'001ULL,"Extra's six fixed rare odds consume only its own Stardust remainder");
        check(defs[frutiger].name=="Frutiger Aero" && defs[frutiger].denominator==5'000'000 &&
            roller.tickets(frutiger)==aura::TicketPool/5'000'000 && !defs[frutiger].previewOnly,
            "Frutiger Aero has an exact obtainable Extra-only 1 in 5,000,000 interval");
        check(defs[redJohn].name=="Red john" && defs[redJohn].denominator==6'000'000 &&
            roller.tickets(redJohn)==aura::TicketPool/6'000'000 && !defs[redJohn].previewOnly &&
            aura::oddsInLabel(defs,redJohn)=="1 in 6,000,000",
            "Red john has a reachable Extra-only interval and its editable 1 in 6,000,000 label");
        for(auto extra:extraIndices) {
            check(aura::auraCategory(defs[extra])==aura::AuraCategory::Extra && roller.tickets(extra)>0,
                "All six Extra auras can be obtained only in Extra");
            check(aura::oddsLabel(defs,extra)!="Preview only","Obtainable Extra auras show their editable CSV odds");
        }
        roller.setBiome(aura::Biome::Normal);
        check(aura::oddsLabel(defs,nanally)=="Preview only" && aura::oddsInLabel(defs,nanally)=="Preview only","Administrator aura stays unpriced");
        check(aura::oddsInLabel(defs,1)=="1 in 4" && defs[0].name=="Common" && defs[1].name=="Uncommon","Stable IDs keep the requested names and odds labels");
        roller.setLuck(2);
        check(roller.tickets(0)==aura::TicketPool/4 && std::abs(static_cast<double>(roller.tickets(singularity))-2000)<=1,
            "2x Luck has the expected CDF behavior");
        rejects([&]{roller.setLuck(NAN);}); rejects([&]{roller.setLuck(0);}); rejects([&]{roller.setLuck(1'000'001);});
        rejects([&]{roller.setBiome(static_cast<aura::Biome>(99));});
        roller.setLuck(1);
        rejects([&]{roller.selectTicket(aura::TicketPool);});
        std::vector<std::uint64_t> observed(defs.size());
        for(int i=0;i<200000;++i) {const auto n=roller.roll(); check(n==same.roll(),"Seed reproducibility"); ++observed[n];}
        check(std::abs(static_cast<double>(observed[0])/200000-.5)<.01 &&
            std::abs(static_cast<double>(observed[1])/200000-.25)<.005 &&
            std::abs(static_cast<double>(observed[2])/200000-.125)<.005,"Baseline distribution matches its declared probabilities");
        for(std::size_t i=0;i<defs.size();++i) if(!aura::availableInBiome(defs[i],aura::Biome::Normal))
            check(observed[i]==0,"Normal rolls cannot produce any limited or administrator aura");
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
        auto previewFirst=defs; std::swap(previewFirst[0],previewFirst[nanally]);
        auto initialPreviewFirst=aura::loadProgress(dir/"preview-first-missing",previewFirst);
        check(initialPreviewFirst.equipped==nanally && initialPreviewFirst.best==nanally && !previewFirst[initialPreviewFirst.equipped].previewOnly,
            "Common is preferred after moving a preview to the first catalog row");
        previewFirst[nanally].id="common";
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
        autoRoll=true; presentation.showPreview(redJohn,defs.size(),autoRoll);
        check(!autoRoll && presentation.shown(p)==redJohn && p.equipped==3 && p.rolls==prototypeRolls && p.counts==prototypeCounts,
            "Unowned Red john preview stops AutoRoll without recording a discovery or changing equipment");
        const auto settingsPath=dir/"settings.txt";
        const auto newPlayerSettings=aura::loadSettings(settingsPath);
        check(newPlayerSettings.language==aura::Language::English && !newPlayerSettings.usernameConfigured &&
            newPlayerSettings.username=="Player" && newPlayerSettings.serverEndpoint=="http://127.0.0.1:8765" &&
            newPlayerSettings.profilePhotoPath.empty(),"New players start with an unconfirmed name, a local server, and no profile photo");
        aura::Settings settings; settings.language=aura::Language::Japanese; settings.shirt={.95f,.2f,.3f};
        settings.luck=12.5; settings.avatar=aura::Avatar::Vrm; settings.biome=aura::Biome::Desert;
        settings.vrmPath="C:/モデル/Chocolat.vrm"; settings.bgmPath="C:/Music/Tides of the Outer Reach.mp3"; settings.volume=.35f;
        settings.pixelationBgmPath="C:/音楽/Pixelation base.mp3";
        settings.pixelationFinalBgmPath="C:/Music/Pixelation \"final\".mp3";
        settings.illusionaryBgmPath="C:/音楽/The Weirdos \"loop\".ogg";
        settings.pixelationBpm=179.2f; settings.pixelationBeatOffset=-.17f;
        settings.equinoxBgmPath="C:/音楽/陰陽 \"Equinox\".mp3";
        settings.equinoxBpm=153.35f; settings.equinoxBeatOffset=.11f;
        settings.equinoxCutsceneBpm=181.5f; settings.equinoxCutsceneBeatOffset=-.23f;
        settings.username="星の \"Player\" 🌟"; settings.discordEnabled=false; settings.discordApplicationId="12345678901234567890";
        settings.usernameConfigured=true;
        settings.serverEndpoint="https://rng.example.test:8765";
        settings.profilePhotoPath="C:/写真/星の \"profile\".png";
        aura::saveSettings(settingsPath,settings); const auto loadedSettings=aura::loadSettings(settingsPath);
        check(loadedSettings.language==aura::Language::Japanese && std::abs(loadedSettings.shirt.r-.95f)<.0001f,
            "Language and shirt color round trip");
        check(loadedSettings.luck==12.5 && loadedSettings.avatar==aura::Avatar::Vrm && loadedSettings.biome==aura::Biome::Desert &&
            loadedSettings.vrmPath==settings.vrmPath && loadedSettings.bgmPath==settings.bgmPath && loadedSettings.volume==settings.volume &&
            loadedSettings.pixelationBgmPath==settings.pixelationBgmPath && loadedSettings.pixelationFinalBgmPath==settings.pixelationFinalBgmPath,
            "Extended settings / UTF-8 audio paths / quoted names round trip");
        check(loadedSettings.illusionaryBgmPath==settings.illusionaryBgmPath,"Schema 6 preserves the Illusionary OGG path and quoted Unicode names");
        check(loadedSettings.pixelationBpm==179.2f && loadedSettings.pixelationBeatOffset==-.17f,
            "Schema 7 preserves custom BPM and beat alignment");
        check(loadedSettings.equinoxBgmPath==settings.equinoxBgmPath &&
            loadedSettings.equinoxBpm==settings.equinoxBpm && loadedSettings.equinoxBeatOffset==settings.equinoxBeatOffset &&
            loadedSettings.equinoxCutsceneBpm==settings.equinoxCutsceneBpm && loadedSettings.equinoxCutsceneBeatOffset==settings.equinoxCutsceneBeatOffset,
            "Schema 8 round trips a quoted Unicode Equinox path and independent normal/cutscene beat settings");
        check(loadedSettings.username==settings.username && !loadedSettings.discordEnabled && loadedSettings.discordApplicationId==settings.discordApplicationId,
            "Schema 4 preserves Unicode/quoted usernames and Discord preferences");
        check(loadedSettings.usernameConfigured && loadedSettings.serverEndpoint==settings.serverEndpoint &&
            loadedSettings.profilePhotoPath==settings.profilePhotoPath,
            "Schema 9 round trips the confirmed-name flag, custom server address, and quoted Unicode profile photo path");
        {
            auto pending=settings; pending.usernameConfigured=false;
            aura::saveSettings(settingsPath,pending);
            check(!aura::loadSettings(settingsPath).usernameConfigured,
                "Schema 9 keeps an explicitly unconfirmed custom name pending rather than inferring confirmation");
            pending.username="Player"; pending.usernameConfigured=true;
            aura::saveSettings(settingsPath,pending);
            const auto explicitlyConfirmed=aura::loadSettings(settingsPath);
            check(explicitlyConfirmed.username=="Player" && explicitlyConfirmed.usernameConfigured,
                "Schema 9 distinguishes a deliberately confirmed Player name from the untouched initial name");
        }
        settings.breakthroughCaptionOffset=-.18f;
        settings.breakthroughWordTimes={21.f,21.19f,21.4f,21.72f,22.11f,22.24f,22.6f,23.2f};
        for(unsigned i=0;i<aura::StageCount;++i) {
            settings.biome=static_cast<aura::Biome>(i); aura::saveSettings(settingsPath,settings);
            const auto planetSettings=aura::loadSettings(settingsPath);
            check(planetSettings.biome==settings.biome && planetSettings.breakthroughCaptionOffset==-.18f &&
                planetSettings.breakthroughWordTimes==settings.breakthroughWordTimes,
                "All six planets and custom per-word caption times survive save/reload");
            check(!aura::stageDisplayName(settings.biome,aura::Language::English).empty() &&
                !aura::stageDisplayName(settings.biome,aura::Language::Japanese).empty(),"All planets have bilingual names");
        }
        settings.biome=static_cast<aura::Biome>(aura::StageCount); rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.biome=aura::Biome::Desert;
        settings.breakthroughCaptionOffset=NAN; rejects([&]{aura::saveSettings(settingsPath,settings);});
        settings.breakthroughCaptionOffset=5.1f; rejects([&]{aura::saveSettings(settingsPath,settings);}); settings.breakthroughCaptionOffset=0;
        settings.breakthroughWordTimes[1]=settings.breakthroughWordTimes[0]; rejects([&]{aura::saveSettings(settingsPath,settings);});
        settings.breakthroughWordTimes=aura::tuning::cutscene::BreakthroughWordTimes;
        aura::saveSettings(settingsPath,settings);
        {
            std::ifstream oldSettings(settingsPath); std::string all((std::istreambuf_iterator<char>(oldSettings)),{});
            check(all.starts_with("STARS_RNG_SETTINGS 9\n"),"New settings use schema 9");
            const auto eraseRow=[&](const char* field) {
                const auto row=all.find(field);
                check(row!=std::string::npos,"Settings migration fixture contains the field being removed");
                all.erase(row,all.find('\n',row)-row+1);
            };
            // まず直前のschema 8へ戻す。新しい3行を残すと旧形式の正しいfixtureになりません。
            all.replace(0,20,"STARS_RNG_SETTINGS 8");
            eraseRow("username_configured "); eraseRow("server_endpoint "); eraseRow("profile_photo ");
            {std::ofstream legacySettings(settingsPath); legacySettings<<all;}
            const auto migratedV8=aura::loadSettings(settingsPath);
            const aura::Settings defaults;
            check(migratedV8.username==settings.username && migratedV8.usernameConfigured &&
                migratedV8.serverEndpoint==defaults.serverEndpoint && migratedV8.profilePhotoPath.empty() &&
                migratedV8.equinoxBgmPath==settings.equinoxBgmPath && migratedV8.equinoxBpm==settings.equinoxBpm &&
                migratedV8.equinoxBeatOffset==settings.equinoxBeatOffset && migratedV8.equinoxCutsceneBpm==settings.equinoxCutsceneBpm &&
                migratedV8.equinoxCutsceneBeatOffset==settings.equinoxCutsceneBeatOffset,
                "Schema 8 preserves existing Equinox settings and infers a configured custom name while defaulting multiplayer fields");
            auto untouchedName=all;
            const auto nameRow=untouchedName.find("username ");
            check(nameRow!=std::string::npos,"Schema 8 fixture has its existing username row");
            untouchedName.replace(nameRow,untouchedName.find('\n',nameRow)-nameRow,"username \"Player\"");
            {std::ofstream legacySettings(settingsPath); legacySettings<<untouchedName;}
            const auto migratedUntouched=aura::loadSettings(settingsPath);
            check(migratedUntouched.username=="Player" && !migratedUntouched.usernameConfigured &&
                migratedUntouched.serverEndpoint==defaults.serverEndpoint && migratedUntouched.profilePhotoPath.empty(),
                "An untouched schema 8 Player name still requires multiplayer name confirmation");
            all.replace(0,20,"STARS_RNG_SETTINGS 7");
            eraseRow("equinox_bgm "); eraseRow("equinox_beat ");
            {std::ofstream legacySettings(settingsPath); legacySettings<<all;}
            const auto migratedV7=aura::loadSettings(settingsPath);
            check(migratedV7.pixelationBpm==settings.pixelationBpm && migratedV7.pixelationBeatOffset==settings.pixelationBeatOffset &&
                migratedV7.illusionaryBgmPath==settings.illusionaryBgmPath && migratedV7.equinoxBgmPath.empty() &&
                migratedV7.equinoxBpm==defaults.equinoxBpm && migratedV7.equinoxBeatOffset==defaults.equinoxBeatOffset &&
                migratedV7.equinoxCutsceneBpm==defaults.equinoxCutsceneBpm && migratedV7.equinoxCutsceneBeatOffset==defaults.equinoxCutsceneBeatOffset,
                "Schema 7 retains Pixelation alignment and migrates both Equinox clocks to current defaults");
            all.replace(all.find("STARS_RNG_SETTINGS 7"),20,"STARS_RNG_SETTINGS 6");
            const auto beatRow=all.find("pixelation_beat ");
            check(beatRow!=std::string::npos,"New settings contain the beat alignment field");
            all.erase(beatRow,all.find('\n',beatRow)-beatRow+1);
            {std::ofstream legacySettings(settingsPath); legacySettings<<all;}
            const auto migratedV6=aura::loadSettings(settingsPath);
            check(migratedV6.illusionaryBgmPath==settings.illusionaryBgmPath && migratedV6.pixelationBpm==0 && migratedV6.pixelationBeatOffset==0,
                "Schema 6 keeps audio paths and defaults to automatic beat detection");
            check(migratedV6.equinoxBgmPath.empty() && migratedV6.equinoxBpm==defaults.equinoxBpm &&
                migratedV6.equinoxCutsceneBpm==defaults.equinoxCutsceneBpm,
                "Schema 6 also receives independent default Equinox beat clocks");
            all.replace(all.find("STARS_RNG_SETTINGS 6"),20,"STARS_RNG_SETTINGS 5");
            const auto musicRow=all.find("illusionary_bgm ");
            check(musicRow!=std::string::npos,"New settings contain the named Illusionary music field");
            all.erase(musicRow,all.find('\n',musicRow)-musicRow+1);
            std::ofstream legacySettings(settingsPath); legacySettings<<all;
        }
        const auto migratedV5=aura::loadSettings(settingsPath);
        check(migratedV5.biome==settings.biome && migratedV5.breakthroughWordTimes==settings.breakthroughWordTimes &&
            migratedV5.illusionaryBgmPath.empty(),"Schema 5 keeps existing planet and captions while defaulting the new BGM path");
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
        for(unsigned i=0;i<180;++i) {
            const float angle=i*.034906585f;
            float x=std::cos(angle)*100.f,z=std::sin(angle)*100.f;
            aura::clampStagePosition(aura::Biome::Extra,x,z,aura::StageHalfExtent,1.2f);
            check(std::abs(std::hypot(x,z)-(aura::extraStageRadius(angle,aura::StageHalfExtent)-1.2f))<.001f,
                "The star planet walking boundary follows every rounded tip and valley");
        }
        float safeX=1,safeZ=-2; aura::clampStagePosition(aura::Biome::Extra,safeX,safeZ,aura::StageHalfExtent,1.2f);
        check(safeX==1 && safeZ==-2,"Star boundary leaves an interior position unchanged");
        safeX=100;safeZ=-100; aura::clampStagePosition(aura::Biome::Normal,safeX,safeZ,aura::StageHalfExtent,1.2f);
        check(safeX==aura::StageWalkLimit && safeZ==-aura::StageWalkLimit,"Existing square planet limits are unchanged");
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
        {
            // 復帰区間はv0.1.6公開版から計測した視点、維持区間は移動方向を確認します。
            using aura::tuning::cutscene::breakthroughCamera;
            const auto distanceToTarget=[](const auto& camera) {
                const auto x=camera.eye[0]-camera.target[0],y=camera.eye[1]-camera.target[1],z=camera.eye[2]-camera.target[2];
                return std::sqrt(x*x+y*y+z*z);
            };
            struct OriginalView {float time;std::array<float,3> eye;float height,roll;};
            constexpr OriginalView originalViews[]={
                {0,{.98646590f,6.49680566f,-4.03105260f},6.f,-.30f},
                {1.5f,{1.19663980f,6.49680566f,-3.97373290f},6.f,-.2175f},
                {3.399f,{1.45791737f,6.49680566f,-3.88548284f},6.f,-.113055f},
                {3.4f,{2.00084146f,2.05067615f,-4.14205667f},1.5f,-.48f},
                {5.1f,{2.41219966f,2.05067615f,-3.91679624f},1.5f,-.3695f},
                {7.299f,{2.90651815f,2.05067615f,-3.56541053f},1.5f,-.226565f},
                {15.05f,{3.38150384f,4.38786988f,-11.72200630f},3.9f,-.00390236f},
                {22.5f,{4.96791115f,4.40127630f,-11.50879452f},3.9f,.00618178f},
                {29.999f,{6.56284504f,4.41477090f,-11.07409586f},3.9f,-.00805366f}};
            for(const auto& view:originalViews) {
                const auto camera=breakthroughCamera(view.time);
                bool sameEye=true;
                for(unsigned axis=0;axis<3;++axis)sameEye&=std::abs(camera.eye[axis]-view.eye[axis])<.00002f;
                check(sameEye && std::abs(camera.target[1]-view.height)<.00001f &&
                    std::abs(camera.roll-view.roll)<.00001f && camera.focalLength==1.6f &&
                    !camera.monolithSpace && !camera.blackout && camera.whiteout==0,
                    "BreakThrough i, ii and finale match the initial v0.1.6 camera instead of the new orbit or corridor");
            }
            check(breakthroughCamera(3.399f).target[1]==6.f && breakthroughCamera(3.4f).target[1]==1.5f,
                "The original close shots change with a direct camera cut at 3.4 seconds");
            const auto rushA=breakthroughCamera(7.6f),rushB=breakthroughCamera(8.6f);
            const auto rushC=breakthroughCamera(9.8f),rushD=breakthroughCamera(10.8f);
            const float rushDistanceA=distanceToTarget(rushA),rushDistanceD=distanceToTarget(rushD);
            bool sameRushDirection=true;
            for(unsigned axis=0;axis<3;++axis)
                sameRushDirection=sameRushDirection && std::abs(
                    (rushA.eye[axis]-rushA.target[axis])/rushDistanceA-
                    (rushD.eye[axis]-rushD.target[axis])/rushDistanceD)<.0001f;
            check(sameRushDirection && rushA.target==rushD.target && rushA.roll==rushD.roll &&
                rushDistanceD<rushDistanceA*.25f &&
                distanceToTarget(rushC)-rushDistanceD>distanceToTarget(rushA)-distanceToTarget(rushB),
                "BreakThrough iii accelerates straight toward the bright core without orbiting or rolling");
            const float ivStart=aura::tuning::cutscene::BreakthroughMonolithStart;
            const auto flash=breakthroughCamera(ivStart),revealed=breakthroughCamera(ivStart+.7f);
            const auto monolithLate=breakthroughCamera(14.14f);
            check(flash.monolithSpace && flash.whiteout==1 && revealed.whiteout==0 && monolithLate.monolithSpace &&
                revealed.focalLength<1.6f &&
                revealed.eye[0]==0 && monolithLate.eye[0]==0 && monolithLate.eye[2]>revealed.eye[2] &&
                monolithLate.eye[2]<monolithLate.target[2],
                "BreakThrough iv retains the current whiteout and forward wide-angle monolith camera");
            check(std::abs(revealed.eye[2]-(-20.f+3.8f*.7f/18.9f))<.00001f,
                "Shortening the monolith scene does not speed up the preserved iv camera");
            check(!breakthroughCamera(14.149f).blackout && breakthroughCamera(14.15f).blackout &&
                breakthroughCamera(14.2f).blackout && breakthroughCamera(15.049f).blackout &&
                !breakthroughCamera(15.05f).blackout && !breakthroughCamera(15.05f).monolithSpace &&
                !breakthroughCamera(29.f).monolithSpace,
                "Get out of my head appears on black before a direct cut back to the original seven-pillar finale");
            using aura::tuning::cutscene::breakthroughIvCaptionVisible;
            const float inkStart=aura::tuning::cutscene::BreakthroughIvCaptionStart;
            const float inkEnd=aura::tuning::cutscene::BreakthroughIvCaptionEnd;
            check(!breakthroughIvCaptionVisible(std::nextafter(inkStart,-INFINITY)) && breakthroughIvCaptionVisible(inkStart) &&
                breakthroughIvCaptionVisible(std::nextafter(inkEnd,-INFINITY)) && !breakthroughIvCaptionVisible(inkEnd),
                "BreakThrough white/black rendering starts on the first iv caption frame and ends on its exact shared boundary");
            for(float time:{-1.f,0.f,12.9f,14.15f,14.5f,15.05f,29.f,30.f,NAN,INFINITY,-INFINITY})
                check(!breakthroughIvCaptionVisible(time),"The iv ink mode cannot affect other shots, blackout, finale or invalid times");
            check(aura::tuning::cutscene::breakthroughCaptionDrift(17.05f,16.05f)==
                    aura::tuning::cutscene::breakthroughCaptionDrift(21.f,20.f) &&
                aura::tuning::cutscene::breakthroughCaptionDrift(20.f,20.f)==0 &&
                aura::tuning::cutscene::breakthroughCaptionDrift(30.f,20.f)==24.f,
                "Both floating finale captions share their slow drift and stop after four seconds");
            for(unsigned frame=0;frame<=240;++frame) {
                const auto camera=breakthroughCamera(frame*.125f);
                check(std::all_of(camera.eye.begin(),camera.eye.end(),[](float value){return std::isfinite(value);}) &&
                    std::all_of(camera.target.begin(),camera.target.end(),[](float value){return std::isfinite(value);}) &&
                    std::isfinite(camera.roll) && camera.focalLength>0 && (camera.blackout || distanceToTarget(camera)>0) &&
                    camera.whiteout>=0 && camera.whiteout<=1,
                    "Every BreakThrough camera sample has a finite view and a bounded whiteout");
                if(camera.monolithSpace) check(std::abs(camera.eye[1]-aura::tuning::cutscene::BreakthroughMonolithEyeHeight)<=
                    aura::tuning::cutscene::BreakthroughMonolithBobHeight+.0001f,
                    "Monolith-space camera bob remains subtle instead of jumping up and down");
            }
            check(breakthroughCamera(-1).eye==breakthroughCamera(0).eye &&
                breakthroughCamera(40).eye==breakthroughCamera(30).eye,
                "Camera debug times outside the cutscene clamp to the first or last view");
        }
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
        for(float invalidTempo:{NAN,INFINITY,-1.f,29.f,301.f}) {
            settings.pixelationBpm=invalidTempo; rejects([&]{aura::saveSettings(settingsPath,settings);});
        }
        settings.pixelationBpm=0;
        for(float invalidAlignment:{NAN,INFINITY,-2.1f,2.1f}) {
            settings.pixelationBeatOffset=invalidAlignment; rejects([&]{aura::saveSettings(settingsPath,settings);});
        }
        settings.pixelationBeatOffset=0;
        for(const std::string& invalidPath:{std::string("bad\npath"),std::string("bad\rpath"),std::string("bad\0path",8),std::string(2049,'x')}) {
            auto bad=settings; bad.equinoxBgmPath=invalidPath;
            rejects([&]{aura::saveSettings(settingsPath,bad);});
            bad=settings; bad.profilePhotoPath=invalidPath;
            rejects([&]{aura::saveSettings(settingsPath,bad);});
            bad=settings; bad.serverEndpoint=invalidPath;
            rejects([&]{aura::saveSettings(settingsPath,bad);});
        }
        for(float invalidTempo:{NAN,INFINITY,-INFINITY,-1.f,0.f,29.f,301.f}) {
            auto bad=settings; bad.equinoxBpm=invalidTempo;
            rejects([&]{aura::saveSettings(settingsPath,bad);});
            bad=settings; bad.equinoxCutsceneBpm=invalidTempo;
            rejects([&]{aura::saveSettings(settingsPath,bad);});
        }
        for(float invalidAlignment:{NAN,INFINITY,-INFINITY,-2.1f,2.1f}) {
            auto bad=settings; bad.equinoxBeatOffset=invalidAlignment;
            rejects([&]{aura::saveSettings(settingsPath,bad);});
            bad=settings; bad.equinoxCutsceneBeatOffset=invalidAlignment;
            rejects([&]{aura::saveSettings(settingsPath,bad);});
        }
        {
            auto boundaries=settings;
            boundaries.equinoxBpm=30; boundaries.equinoxCutsceneBpm=300;
            boundaries.equinoxBeatOffset=-2; boundaries.equinoxCutsceneBeatOffset=2;
            aura::saveSettings(settingsPath,boundaries);
            const auto loaded=aura::loadSettings(settingsPath);
            check(loaded.equinoxBpm==30 && loaded.equinoxCutsceneBpm==300 &&
                loaded.equinoxBeatOffset==-2 && loaded.equinoxCutsceneBeatOffset==2,
                "Both Equinox clocks accept and persist the inclusive BPM and offset boundaries");
            std::ifstream input(settingsPath); const std::string valid((std::istreambuf_iterator<char>(input)),{}); input.close();
            const auto row=valid.find("equinox_beat ");
            check(row!=std::string::npos,"Schema 8 emits a named Equinox alignment row");
            for(const char* malformed:{"equinox_beat 0 0 180 0","equinox_beat 180 0 301 0", "equinox_beat 180 2.1 180 0",
                    "equinox_beat 180 0 180 -2.1","equinox_beat 180 0 180", "equinox_beat nan 0 180 0", "unknown_beat 180 0 180 0"}) {
                auto damaged=valid; damaged.replace(row,damaged.find('\n',row)-row,malformed);
                {std::ofstream output(settingsPath);output<<damaged;}
                rejects([&]{aura::loadSettings(settingsPath);});
            }
            {std::ofstream output(settingsPath);output<<valid<<"equinox_beat 180 0 180 0\n";}
            rejects([&]{aura::loadSettings(settingsPath);});
            auto damagedPath=valid;
            const auto pathRow=damagedPath.find("equinox_bgm ");
            damagedPath.replace(pathRow,damagedPath.find('\n',pathRow)-pathRow,"equinox_bgm \"bad\npath\"");
            {std::ofstream output(settingsPath);output<<damagedPath;}
            rejects([&]{aura::loadSettings(settingsPath);});
            // 新規の末尾3行も、値の欠落・不正なbool・改行入りのパスを受け入れない。
            const auto replaceField=[&](const char* field,const std::string& replacement) {
                auto damaged=valid;
                const auto start=damaged.find(field);
                check(start!=std::string::npos,"Schema 9 fixture contains the multiplayer field under test");
                damaged.replace(start,damaged.find('\n',start)-start,replacement);
                {std::ofstream output(settingsPath);output<<damaged;}
                rejects([&]{aura::loadSettings(settingsPath);});
            };
            for(const char* value:{"username_configured -1","username_configured 2","username_configured true","username_configured"})
                replaceField("username_configured ",value);
            replaceField("server_endpoint ","server_endpoint \"bad\naddress\"");
            replaceField("profile_photo ","profile_photo \"bad\rpath\"");
            replaceField("profile_photo ","profile_photo \""+std::string(2049,'x')+"\"");
            const auto multiplayerRow=valid.find("username_configured ");
            {std::ofstream output(settingsPath);output<<valid.substr(0,multiplayerRow);}
            rejects([&]{aura::loadSettings(settingsPath);});
            {std::ofstream output(settingsPath);output<<valid<<"username_configured 1\n";}
            rejects([&]{aura::loadSettings(settingsPath);});
        }
        settings.shirt.g=NAN; rejects([&] { aura::saveSettings(settingsPath,settings); });
        check(aura::displayName(defs[0],aura::Language::Japanese)=="コモン","Japanese aura names");
        check(aura::displayName(defs[pixelation],aura::Language::Japanese)=="ピクセレーション","Japanese Pixelation name");
        check(aura::displayName(defs[equinox],aura::Language::Japanese)=="エキノックス","Japanese Equinox name");
        check(aura::displayName(defs[frutiger],aura::Language::Japanese)=="フルティガー・エアロ","Japanese Frutiger Aero name");
        check(aura::displayName(defs[redJohn],aura::Language::Japanese)=="レッド・ジョン" &&
            aura::displayName(defs[redJohn],aura::Language::English)=="Red john","Bilingual Red john name");
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
        auto historicalCatalog=defs;
        for(auto& definition:historicalCatalog) if(aura::auraCategory(definition)==aura::AuraCategory::Extra) {definition.previewOnly=true; definition.denominator=0;}
        std::erase_if(historicalCatalog,[](const auto& d){return d.id=="illusionary" || d.id=="dune" || d.id=="mirage" || d.id=="pharaoh" || d.id=="frutiger_aero" || d.id=="red_john";});
        auto historicalRollable=historicalCatalog; std::erase_if(historicalRollable,[](const auto& d){return d.previewOnly;});
        auto previousCatalog=historicalRollable; previousCatalog.erase(previousCatalog.begin()+3);
        check(previousCatalog.size()==12 && historicalRollable.size()==13,"Historical rollable catalogs contain 12 and 13 entries");
        aura::Progress previousProgress(previousCatalog.size());
        for(int i=0;i<3;++i) previousProgress.record(previousCatalog.size()-1,previousCatalog);
        previousProgress.equipped=previousCatalog.size()-1; aura::saveProgress(save,previousProgress,previousCatalog);
        const auto migratedProgress=aura::loadProgress(save,defs);
        check(migratedProgress.equipped==singularity && migratedProgress.auraLevel(singularity,defs)==2 && migratedProgress.counts[3]==0,
            "Previous 12-aura saves retain Singularity counts and gain a derived Aura Level");
        for(auto preview:previewIndices) check(migratedProgress.counts[preview]==0,"12-aura saves add every preview with zero inventory");
        aura::Progress v013Progress(historicalRollable.size());
        for(int i=0;i<4;++i) v013Progress.record(historicalRollable.size()-1,historicalRollable);
        v013Progress.equipped=historicalRollable.size()-1; aura::saveProgress(save,v013Progress,historicalRollable);
        const auto v014Progress=aura::loadProgress(save,defs);
        check(v014Progress.rolls==4 && v014Progress.equipped==singularity && v014Progress.auraLevel(singularity,defs)==3 &&
            v014Progress.counts[pixelation]==0 && v014Progress.counts[equinox]==0 && v014Progress.counts[breakthrough]==0 && v014Progress.counts[nanally]==0,
            "Previous 13-aura saves retain counts, equipment, and Singularity level");
        auto previousV014=historicalCatalog;
        std::erase_if(previousV014,[](const auto& d){return d.id=="equinox" || d.id=="breakthrough" || d.id=="nanally";});
        check(previousV014.size()==14,"The v0.1.4 catalog contains 13 rollable auras and Pixelation");
        aura::Progress v014Saved(previousV014.size());
        const auto v014Singularity=static_cast<std::size_t>(std::find_if(previousV014.begin(),previousV014.end(),[](const auto& d){return d.id=="singularity";})-previousV014.begin());
        v014Saved.record(v014Singularity,previousV014); v014Saved.equipped=v014Singularity;
        aura::saveProgress(save,v014Saved,previousV014); const auto v015Progress=aura::loadProgress(save,defs);
        check(v015Progress.equipped==singularity && v015Progress.counts[singularity]==1 && v015Progress.counts[equinox]==0 &&
            v015Progress.counts[pixelation]==0 && v015Progress.counts[breakthrough]==0 && v015Progress.counts[nanally]==0,
            "Previous 14-entry saves add Equinox, BreakThrough, and Nanally with zero preview inventory");
        auto previousV015=historicalCatalog; std::erase_if(previousV015,[](const auto& d){return d.id=="breakthrough" || d.id=="nanally";});
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
        auto previousV017=historicalCatalog; std::erase_if(previousV017,[](const auto& d){return d.id=="nanally";});
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
        check(historicalCatalog.size()==17,"v0.1.10 fixture retains its original 17 IDs");
        aura::Progress v010Saved(historicalCatalog.size());
        v010Saved.record(singularity,historicalCatalog); v010Saved.equipped=singularity; v010Saved.setRollCount(98765);
        aura::saveProgress(save,v010Saved,historicalCatalog);
        const auto v011Restored=aura::loadProgress(save,defs);
        check(v011Restored.rolls==98765 && v011Restored.equipped==singularity && v011Restored.counts[singularity]==1 &&
            !v011Restored.counts[illusionary] && !v011Restored.counts[dune] && !v011Restored.counts[mirage] && !v011Restored.counts[pharaoh] && !v011Restored.counts[frutiger] && !v011Restored.counts[redJohn],
            "Current save keeps inventory, equipment, and edited counter while adding six empty IDs");
        aura::Roller historicalRoller(historicalCatalog,42);
        for(std::size_t i=0;i<historicalCatalog.size();++i) if(historicalCatalog[i].id!="stardust")
            check(historicalRoller.tickets(i)==roller.tickets(i),"Desert additions leave every specified old probability unchanged");
        aura::Progress desertProgress(defs.size());
        for(auto index:{dune,mirage,pharaoh}) desertProgress.record(index,defs);
        desertProgress.equipped=pharaoh; aura::saveProgress(save,desertProgress,defs);
        const auto desertRestored=aura::loadProgress(save,defs);
        check(desertRestored.rolls==3 && desertRestored.counts[dune]==1 && desertRestored.counts[mirage]==1 &&
            desertRestored.equipped==pharaoh && desertRestored.best==pharaoh,"New rollable desert inventory can be equipped, saved and restored");
        aura::Progress extraProgress(defs.size());
        for(auto index:extraIndices) extraProgress.record(index,defs);
        extraProgress.equipped=illusionary; aura::saveProgress(save,extraProgress,defs);
        const auto extraRestored=aura::loadProgress(save,defs);
        check(extraRestored.rolls==6 && extraRestored.discovered()==6 && extraRestored.equipped==illusionary &&
            extraRestored.best==redJohn && extraRestored.counts==extraProgress.counts,
            "Promoted Extra discoveries can be recorded, equipped, saved, and restored independently of the current planet");
        auto oldPreviewCatalog=defs;
        for(auto index:promotedExtraIndices) {oldPreviewCatalog[index].previewOnly=true; oldPreviewCatalog[index].denominator=0;}
        std::erase_if(oldPreviewCatalog,[](const auto& d){return d.id=="frutiger_aero" || d.id=="red_john";});
        aura::Progress oldPreviewProgress(oldPreviewCatalog.size());
        oldPreviewProgress.record(dune,oldPreviewCatalog); oldPreviewProgress.record(pharaoh,oldPreviewCatalog);
        oldPreviewProgress.record(singularity,oldPreviewCatalog); oldPreviewProgress.equipped=pharaoh; oldPreviewProgress.setRollCount(13579);
        aura::saveProgress(save,oldPreviewProgress,oldPreviewCatalog);
        const auto promotedSave=aura::loadProgress(save,defs);
        check(promotedSave.rolls==13579 && promotedSave.equipped==pharaoh && promotedSave.best==singularity &&
            std::equal(oldPreviewProgress.counts.begin(),oldPreviewProgress.counts.end(),promotedSave.counts.begin()) && promotedSave.counts[frutiger]==0 && promotedSave.counts[redJohn]==0,
            "The v0.1.11 save preserves desert discoveries, equipment, and its edited counter when four previews become obtainable");
        auto previousV017Catalog=defs;
        std::erase_if(previousV017Catalog,[](const auto& d){return d.id=="red_john";});
        aura::Progress beforeRedJohn(previousV017Catalog.size());
        beforeRedJohn.record(frutiger,previousV017Catalog); beforeRedJohn.equipped=frutiger; beforeRedJohn.setRollCount(17000);
        aura::saveProgress(save,beforeRedJohn,previousV017Catalog);
        const auto withRedJohn=aura::loadProgress(save,defs);
        check(withRedJohn.rolls==17000 && withRedJohn.equipped==frutiger && withRedJohn.best==frutiger &&
            withRedJohn.counts[frutiger]==1 && withRedJohn.counts[redJohn]==0,
            "Adding Red john preserves a v0.1.17 save and starts the new stable ID with no discovery");
        const auto reclining=aura::illusionaryPose(1.3f),recliningLater=aura::illusionaryPose(2.9f);
        constexpr float halfPi=1.57079632679f;
        check(reclining.fixed && reclining.lift>0 && std::abs(reclining.bodyRoll)<.2f &&
            reclining.bodyPitch+reclining.torsoPitch>.65f && reclining.headPitch>.5f &&
            std::abs(reclining.bodyPitch+reclining.torsoPitch+reclining.headPitch-halfPi)<.025f,
            "Illusionary arches the torso backward and looks straight upward instead of rolling sideways");
        check(std::abs(reclining.elbowPitch[0]-halfPi)<.025f && std::abs(reclining.elbowPitch[1])<.05f &&
            reclining.armOut[0]>1.2f && reclining.armOut[0]>reclining.armOut[1]+.5f &&
            reclining.armPitch[0]!=reclining.armPitch[1],
            "Illusionary raises one arm with a right-angle elbow while the opposite arm stays straight and asymmetric");
        check(recliningLater.fixed && reclining.bodyPitch==recliningLater.bodyPitch &&
            reclining.torsoPitch==recliningLater.torsoPitch && reclining.headPitch==recliningLater.headPitch &&
            reclining.armPitch==recliningLater.armPitch && reclining.armOut==recliningLater.armOut &&
            reclining.elbowPitch==recliningLater.elbowPitch && reclining.legPitch==recliningLater.legPitch &&
            reclining.knee==recliningLater.knee,
            "The shared block and VRM Illusionary pose preserves all joint angles while floating");
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
        presentation.preview=redJohn; check(presentation.level(levels,defs)==0,"Red john has no borrowed Aura Level");
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
        const auto seated=aura::redJohnPose(0),seatedLater=aura::redJohnPose(50);
        check(seated.fixed && seated.seatedHipHeight>0 && seated.legPitch[0]>seated.legPitch[1] &&
            seated.knee[0]<-1.f && seated.knee[1]<-1.f && seated.armPitch[0]>seated.armPitch[1] &&
            seated.elbowPitch[0]>seated.elbowPitch[1],
            "Red john sits with a raised knee, a folded leg, a resting arm, and a straighter supporting arm");
        check(seatedLater.fixed && seated.lift==seatedLater.lift && seated.seatedHipHeight==seatedLater.seatedHipHeight &&
            seated.armPitch==seatedLater.armPitch && seated.armOut==seatedLater.armOut && seated.armYaw==seatedLater.armYaw &&
            seated.elbowPitch==seatedLater.elbowPitch && seated.legPitch==seatedLater.legPitch &&
            seated.legOut==seatedLater.legOut && seated.legYaw==seatedLater.legYaw && seated.knee==seatedLater.knee,
            "Red john's fixed seated joints stay stable over time while the chest may breathe");
        check(idle.seatedHipHeight<0 && walkA.seatedHipHeight<0 &&
            idle.armYaw==std::array<float,2>{} && idle.legYaw==std::array<float,2>{} &&
            walkA.armYaw==std::array<float,2>{} && walkA.legYaw==std::array<float,2>{},
            "New seated height and yaw channels leave normal standing and walking unchanged");
        const auto pixelPose=aura::pixelationPose(2.4f);
        check(pixelPose.fixed && std::isfinite(pixelPose.lift) && std::isfinite(pixelPose.bodyRoll) &&
            pixelPose.armOut[0]==pixelPose.armOut[1] && pixelPose.armOut[0]>.5f && pixelPose.lift>.3f,
            "Pixelation uses a finite fixed floating pose shared by both avatars");
        const auto equinoxPose=aura::equinoxPose(2.4f);
        check(equinoxPose.fixed && std::isfinite(equinoxPose.lift) && std::isfinite(equinoxPose.bodyRoll) &&
            equinoxPose.armOut[0]==equinoxPose.armOut[1] && equinoxPose.armOut[0]>.3f && equinoxPose.lift>.23f &&
            equinoxPose.legPitch[0]==0 && equinoxPose.legPitch[1]==0,"Equinox has a finite calm fixed floating pose");
        check(aura::EquinoxCutsceneDuration==10.9f,"Equinox cutscene has one shared duration");
        check(aura::RedJohnCutsceneDuration>0 && aura::cutsceneDuration(aura::CutsceneKind::RedJohn)==aura::tuning::redjohn::CutsceneDuration &&
            aura::cutsceneDuration(aura::CutsceneKind::Equinox)==aura::EquinoxCutsceneDuration &&
            aura::cutsceneDuration(aura::CutsceneKind::Breakthrough)==aura::BreakthroughCutsceneDuration,
            "Each cutscene uses its own shared duration instead of borrowing another aura's timeline");
        {
            namespace eq=aura::equinox;
            namespace style=aura::tuning::equinox;
            const auto finite=[](const eq::Point& point) {
                return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
            };
            const auto equal=[](const eq::Point& a,const eq::Point& b) {
                return a.x==b.x && a.y==b.y && a.z==b.z;
            };
            check(eq::beatClock(7.25f,999.f)==7.25f && eq::beatClock(0,999.f)==0 &&
                eq::beatClock(-1,2)==2*style::fallbackBpm/60.f &&
                eq::beatClock(NAN,2)==eq::beatClock(-1,2) && eq::beatClock(INFINITY,-2)==0 && eq::beatClock(-1,NAN)==0,
                "Equinox uses the supplied music beat and safely falls back to elapsed time when playback is unavailable");
            float lowest=INFINITY,highest=-INFINITY,minSpeed=INFINITY,maxSpeed=0;
            for(unsigned frame=0;frame<1000;++frame) {
                const float beat=frame*.031f;
                const float speed=eq::orbSpeedScale(beat),angle=eq::orbOrbitAngle(beat);
                check(std::isfinite(speed) && speed>0 && speed>=1-style::orbitSpeedVariation-.000001f &&
                    speed<=1+style::orbitSpeedVariation+.000001f && eq::orbOrbitAngle(beat+.001f)>angle,
                    "Orb speed stays within the configured variation range and the angle always advances without reversal");
                const float modulationPeriod=style::orbitBeats*style::orbitSpeedCycleOrbits;
                check(std::abs(eq::orbOrbitAngle(beat+modulationPeriod)-angle-2*eq::Pi*style::orbitSpeedCycleOrbits)<.00005f,
                    "Acceleration and deceleration preserve the average beat-based orbit over a complete modulation cycle");
                minSpeed=std::min(minSpeed,speed); maxSpeed=std::max(maxSpeed,speed);
                const auto a=eq::orbPosition(beat,0),b=eq::orbPosition(beat,1);
                check(finite(a) && finite(b) && std::abs(std::hypot(a.x,a.z)-style::orbOrbitRadius)<.00005f &&
                    std::abs(std::hypot(b.x,b.z)-style::orbOrbitRadius)<.00005f &&
                    std::abs(a.x+b.x)<.00005f && std::abs(a.z+b.z)<.00005f &&
                    std::abs(a.y+b.y-2*style::orbHeight)<.00005f &&
                    a.y>=style::orbHeight-style::orbBobHeight-.00005f && a.y<=style::orbHeight+style::orbBobHeight+.00005f &&
                    b.y>=style::orbHeight-style::orbBobHeight-.00005f && b.y<=style::orbHeight+style::orbBobHeight+.00005f,
                    "Equinox orbs stay opposite on a finite constant-radius orbit with mirrored bounded vertical motion");
                lowest=std::min({lowest,a.y,b.y}); highest=std::max({highest,a.y,b.y});
                const unsigned swap=eq::polarity(beat),first=0u^swap,second=1u^swap;
                check(swap<=1 && first+second==1 && first!=second,
                    "Every Equinox frame has exactly one white orb and one black orb");
                const float age=eq::transitionAge(beat);
                check(std::isfinite(age) && age>=0 && age<style::polarityBeats,
                    "Color-swap explosion age remains in the same finite beat interval as polarity");
                for(unsigned side=0;side<2;++side) {
                    check(equal(eq::orbPosition(beat,side),eq::orbPosition(beat,side+2)),
                        "Orb side selection is stable for equivalent parity values");
                    for(unsigned segment=0;segment<=style::trailSegments;++segment) {
                        const float ageBeats=style::trailBeats*segment/static_cast<float>(style::trailSegments);
                        const auto past=eq::orbPosition(beat-ageBeats,side);
                        check(finite(past) && std::abs(std::hypot(past.x,past.z)-style::orbOrbitRadius)<.00005f &&
                            past.y>=style::orbHeight-style::orbBobHeight-.00005f && past.y<=style::orbHeight+style::orbBobHeight+.00005f,
                            "Every trail segment samples a finite past point on the real orb orbit, including before beat zero");
                        if(segment==0)check(equal(past,eq::orbPosition(beat,side)),"Each Equinox trail begins exactly at its current orb");
                    }
                }
            }
            check(highest-lowest>style::orbBobHeight*1.95f,"Equinox covers its intended vertical orbit range");
            // この分岐はtuningの強さを0へ変えたビルドでも使います。0ならずっと平均速度1です。
            if constexpr(style::orbitSpeedVariation==0) {
                check(minSpeed==1 && maxSpeed==1 &&
                    std::abs(eq::orbOrbitAngle(style::orbitBeats*.5f)-eq::Pi)<.00001f,
                    "A zero variation setting produces a constant-speed orbit and reaches the opposite side after half a cycle");
            } else check(maxSpeed-minSpeed>style::orbitSpeedVariation*1.95f,
                "The configured nonzero variation creates both visible acceleration and deceleration");
            check(style::floorEvenRotation==style::floorRotation && style::floorOddRotation==style::floorRotation &&
                style::dustRotation==style::floorRotation,"Floor surface, both ring groups, and particles share their rotation setting");
            for(unsigned event=1;event<=32;++event) {
                const float boundary=event*style::polarityBeats;
                const float before=std::nextafter(boundary,-INFINITY);
                check(eq::polarity(before)==((event-1)%2) && eq::polarity(boundary)==event%2 &&
                    eq::transitionAge(before)>style::polarityBeats*.99f && eq::transitionAge(boundary)==0 &&
                    eq::transitionAge(boundary+style::burstBeats*.5f)<style::burstBeats,
                    "Polarity swaps and its explosion age reset on exactly the same music-beat boundary");
            }
            for(unsigned glyph=0;glyph<20;++glyph) {
                const float boundary=glyph*style::floorGlyphBeats;
                check(eq::floorGlyphIndex(boundary)==glyph%4 &&
                    eq::floorGlyphIndex(boundary+style::floorGlyphBeats*.5f)==glyph%4,
                    "Floor atlas selection loops through Yin, Yang, Balance, Void in order and holds between changes");
                if(glyph)check(eq::floorGlyphIndex(std::nextafter(boundary,-INFINITY))==(glyph-1)%4,
                    "The floor glyph changes at the exact beat boundary without advancing one frame early");
            }
            for(float invalidBeat:{NAN,INFINITY,-INFINITY}) {
                check(equal(eq::orbPosition(invalidBeat,0),eq::orbPosition(0,0)) &&
                    equal(eq::orbPosition(invalidBeat,1),eq::orbPosition(0,1)) && eq::polarity(invalidBeat)==0 &&
                    eq::floorGlyphIndex(invalidBeat)==0 && eq::transitionAge(invalidBeat)==style::polarityBeats &&
                    eq::orbOrbitAngle(invalidBeat)==eq::orbOrbitAngle(0) && eq::orbSpeedScale(invalidBeat)==eq::orbSpeedScale(0),
                    "Malformed Equinox beat values cannot generate NaN positions or false color-change explosions");
            }
            check(eq::polarity(-1)==0 && eq::floorGlyphIndex(-1)==0 && eq::transitionAge(-1)==style::polarityBeats &&
                finite(eq::orbPosition(-style::trailBeats,0)),
                "Negative event clocks are inactive while finite negative history remains available to a trail");
        }
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
        {
            namespace pixel=aura::tuning::pixelation;
            float lowest=1000,highest=-1000;
            for(unsigned frame=0;frame<1000;++frame) for(unsigned player=0;player<2;++player) {
                const auto aircraft=aura::pixelationPlayer(frame*.031f,player);
                const float radius=std::sqrt(aircraft.x*aircraft.x+aircraft.z*aircraft.z);
                check(std::isfinite(aircraft.y) && std::abs(radius-pixel::PlayerOrbitRadius)<.0001 &&
                    aircraft.y>=pixel::PlayerHeight-pixel::PlayerBobAmplitude-.0001 &&
                    aircraft.y<=pixel::PlayerHeight+pixel::PlayerBobAmplitude+.0001,
                    "Both aircraft climb and descend on a finite constant-radius three-dimensional orbit");
                lowest=std::min(lowest,aircraft.y); highest=std::max(highest,aircraft.y);
            }
            check(highest-lowest>pixel::PlayerBobAmplitude*1.95 && pixel::PlayerOrbitSpeed>1.0,
                "Aircraft use the restored fast orbit and cover the full vertical flight range");
            check(pointDistance(aura::pixelationPlayer(NAN,0),aura::pixelationPlayer(0,0))==0 &&
                pointDistance(aura::pixelationPlayer(-1,1),aura::pixelationPlayer(0,1))==0,
                "Invalid aircraft clocks safely start at the first point of the trajectory");
            for(unsigned cycle=0;cycle<10;++cycle) {
                const float action=cycle*aura::PixelationVolleyPeriod+.2f,orbit=action*1.875f,launchOrbit=orbit-.375f;
                const auto volley=aura::pixelationVolley(action,orbit,launchOrbit);
                for(unsigned player=0;player<2;++player)
                    check(pointDistance(volley.players[player],aura::pixelationPlayer(orbit,player))==0 &&
                        pointDistance(volley.launches[player],aura::pixelationPlayer(launchOrbit,player))==0,
                        "Current aircraft and projectile launch anchors share the same rising and falling trajectory");
            }
        }
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
        std::vector<std::uint8_t> silence(48000*2);
        const auto silentBass=aura::bassEnvelope(silence,48000,1);
        check(silentBass.size()==100 && std::all_of(silentBass.begin(),silentBass.end(),[](float v){return v==0;}),
            "A silent track produces no artificial camera beat");
        check(aura::bassEnvelope(silence,999,1).empty() && aura::bassEnvelope(silence,48000,0).empty() &&
            aura::bassEnvelope(std::span<const std::uint8_t>(silence).first(3),48000,1).empty(),
            "Malformed audio dimensions cannot produce a bass timeline");
        std::vector<std::uint8_t> frequencyTest(48000*2*2);
        for(unsigned i=0;i<96000;++i) {
            const double hz=i<48000?6000.:60.;
            const auto sample=static_cast<std::int16_t>(std::sin(i*2*3.141592653589793*hz/48000)*16000);
            const auto bits=static_cast<std::uint16_t>(sample);
            frequencyTest[i*2]=static_cast<std::uint8_t>(bits&255);
            frequencyTest[i*2+1]=static_cast<std::uint8_t>(bits>>8);
        }
        const auto measuredBass=aura::bassEnvelope(frequencyTest,48000,1);
        const auto highPeak=*std::max_element(measuredBass.begin()+20,measuredBass.begin()+80);
        const auto lowPeak=*std::max_element(measuredBass.begin()+120,measuredBass.begin()+180);
        check(measuredBass.size()==200 && lowPeak>highPeak*4 &&
            std::all_of(measuredBass.begin(),measuredBass.end(),[](float value){return std::isfinite(value) && value>=0 && value<=1;}),
            "Measured low-frequency notes drive the camera while equal-volume high frequencies are suppressed");
        const std::array<float,3> beats{0,1,0};
        check(std::abs(aura::bassAt(beats,.005f)-.5f)<.0001f && aura::bassAt(beats,.01f)==1 &&
            aura::bassAt(beats,-1)==0 && aura::bassAt(beats,NAN)==0 && aura::bassAt(beats,10)==0,
            "Bass playback interpolates smoothly and safely ignores times outside the track");
        // 一定の間隔のキックを模した低音表。曲名を決め打ちせず拍を検出できることを確かめます。
        std::vector<float> kicks(3000);
        for(unsigned i=13;i<kicks.size();i+=50) {
            kicks[i]=1; kicks[i+1]=.7f; kicks[i+2]=.3f;
        }
        const auto beatGrid=aura::estimateBeatGrid(kicks);
        check(std::abs(beatGrid.bpm-120)<.2f && std::abs(beatGrid.offset-.13f)<.015f && beatGrid.confidence>.8f,
            "Repeated bass onsets recover 120 BPM and their actual starting phase");
        const aura::BeatGrid exactBeat{120,.13f,1};
        check(std::abs(aura::musicBeatPosition(1.044f,180,.044f)-3)<.00001f &&
            std::abs(aura::musicBeatPosition(.544f,180,.044f)-1.5f)<.00001f &&
            aura::musicBeatPosition(.02f,180,.044f)==0 &&
            std::abs(aura::musicBeatPosition(0,180,-.1f)-.3f)<.00001f,
            "Music beat position follows tempo and positive delay, including fractional beats");
        check(std::abs(aura::musicBeatPosition(148.75f,180,.044f,148.25f)-446.368f)<.001f &&
            std::abs(aura::musicBeatPosition(297.f,180,.044f,148.25f)-891.368f)<.001f,
            "A track ending between beats rephases each loop without accumulating drift");
        check(aura::musicBeatPosition(NAN,180,0)==0 && aura::musicBeatPosition(-1,180,0)==0 &&
            aura::musicBeatPosition(1,301,0)==0 && aura::musicBeatPosition(1,29,0)==0 &&
            aura::musicBeatPosition(1,180,INFINITY)==0 &&
            aura::musicBeatPosition(1,180,0,NAN)==3,
            "Malformed music clocks stay safe and an invalid loop length uses the nonlooping clock");
        check(aura::beatPulse(.13f,exactBeat)>.999f && aura::beatPulse(.63f,exactBeat)>.999f &&
            aura::beatPulse(.38f,exactBeat)==0 && aura::beatPulse(.23f,exactBeat,0,.1f)>.999f &&
            aura::beatPulse(.38f,exactBeat,240)>.999f,
            "Cube peaks repeat with BPM, manual tempo and positive delay shift the peak correctly");
        check(std::abs(aura::beatPulse(.629999f,exactBeat)-aura::beatPulse(.630001f,exactBeat))<.001f &&
            aura::beatPulse(-1,exactBeat)==0 && aura::beatPulse(NAN,exactBeat)==0 &&
            aura::beatPulse(1,exactBeat,NAN)==0 && aura::beatPulse(1,exactBeat,0,NAN)==0 &&
            aura::beatPulse(1,{})==0,
            "Beat wrapping is continuous and malformed playback cannot create a pulse");
        check(aura::estimateBeatGrid(std::vector<float>(500)).bpm==0 && aura::estimateBeatGrid(beats).bpm==0,
            "Silent or too-short music does not invent a BPM");
        // 22050Hzは10msで割り切れません。端数を切り捨て続けると拍が次第にずれます。
        const auto oddRateBass=aura::bassEnvelope(std::vector<std::uint8_t>(22050*2*10),22050,1);
        check(oddRateBass.size()==1000 && std::all_of(oddRateBass.begin(),oddRateBass.end(),[](float v){return v==0;}),
            "A non-divisible audio rate still produces exactly 100 envelope bins per second");
        std::filesystem::remove_all(dir);
        check(aura::tuning::cutscene::endingOpacity(10.1f,10.9f)==1 &&
            std::abs(aura::tuning::cutscene::endingOpacity(10.5f,10.9f)-.5f)<.0001f &&
            aura::tuning::cutscene::endingOpacity(10.9f,10.9f)==0,
            "CutScene fade preserves the last frame and transitions smoothly to the aura");
        check(aura::tuning::cutscene::endingOpacity(29.2f,30.f,true)==1 &&
            aura::tuning::cutscene::endingOpacity(29.999f,30.f,true)==1 &&
            aura::tuning::cutscene::endingOpacity(30.f,30.f,true)==0,
            "BreakThrough stays opaque until the exact ending and then hard-cuts without crossfade");
        std::cout << "PASS: 23 auras, exact stage odds, six planets, limited drops, fixed poses, Equinox variable speed/beat/trails/glyphs, BreakThrough ink bounds, Red john catalog/save/preview/cutscene isolation, bass envelope, schema 9 multiplayer profile migration, save/restore, UTF-8 CSV\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
