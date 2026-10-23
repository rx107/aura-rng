#include "core/presentation.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace aura {
std::string stageDisplayName(Biome stage,Language language) {
    switch(stage) {
    case Biome::Normal: return tr(language,"Quiet Garden","静かな庭園");
    case Biome::Desert: return tr(language,"Sunlit Dunes","陽光の砂丘");
    case Biome::Ocean: return tr(language,"Azure Ocean","蒼海の惑星");
    case Biome::Forest: return tr(language,"Verdant Forest","深緑の惑星");
    case Biome::Volcano: return tr(language,"Ember Volcano","火山の惑星");
    case Biome::Extra: return tr(language,"Extra Stage","星のエクストラステージ");
    }
    return {};
}
std::string displayName(const Definition& d, Language language) {
    if (language == Language::English) return d.name;
    const std::pair<const char*, const char*> names[] = {
        {"still", "コモン"}, {"glimmer", "アンコモン"}, {"ember", "残火"}, {"stardust", "星屑"}, {"verdant", "翠緑"},
        {"tidal", "潮流"}, {"amethyst", "アメジスト"}, {"solar", "太陽"}, {"aurora", "オーロラ"},
        {"astral", "星界"}, {"eventide", "宵闇"}, {"zenith", "天頂"}, {"pixelation", "ピクセレーション"},
        {"equinox", "エキノックス"}, {"breakthrough", "ブレイクスルー"}, {"nanally", "ナナリー"}, {"singularity", "特異点"},
        {"illusionary","イリュージョナリー"}, {"dune","砂丘"}, {"mirage","蜃気楼"}, {"pharaoh","ファラオ"},
        {"frutiger_aero","フルティガー・エアロ"}};
    for (const auto& [id, name] : names) if (d.id == id) return name;
    return d.name;
}
void Presentation::showPreview(std::size_t index, std::size_t catalogSize, bool& autoRoll) {
    if (index >= catalogSize) throw std::out_of_range("Invalid preview aura");
    preview = index;
    autoRoll = false;
}
unsigned Presentation::level(const Progress& progress,const std::vector<Definition>& catalog) const {
    const auto index=shown(progress);
    const auto& id=catalog.at(index).id;
    if(id=="singularity") return std::min(3u,singularityLevel.value_or(progress.auraLevel(index,catalog)));
    if(id=="pixelation") return std::min(3u,pixelationLevel.value_or(progress.auraLevel(index,catalog)));
    return 0;
}
bool validUsername(const std::string& value) {
    if(value.empty() || value.size()>64) return false;
    for(std::size_t i=0;i<value.size();) {
        const auto first=static_cast<unsigned char>(value[i]);
        unsigned code{},length{};
        if(first<0x80) {code=first;length=1;}
        else if(first>=0xc2 && first<=0xdf) {code=first&0x1f;length=2;}
        else if(first>=0xe0 && first<=0xef) {code=first&0x0f;length=3;}
        else if(first>=0xf0 && first<=0xf4) {code=first&7;length=4;}
        else return false;
        if(i+length>value.size()) return false;
        for(unsigned j=1;j<length;++j) {
            const auto next=static_cast<unsigned char>(value[i+j]);
            if((next&0xc0)!=0x80) return false;
            code=(code<<6)|(next&0x3f);
        }
        if((length==2 && code<0x80) || (length==3 && code<0x800) || (length==4 && code<0x10000) ||
            code>0x10ffff || (code>=0xd800 && code<=0xdfff) || code<0x20 || (code>=0x7f && code<=0x9f) ||
            code==0x2028 || code==0x2029) return false;
        i+=length;
    }
    return true;
}
bool validDiscordApplicationId(const std::string& value) {
    return value.size()<=20 && std::all_of(value.begin(),value.end(),[](unsigned char c){return c>='0' && c<='9';});
}
namespace {
void validate(const Settings& s) {
    if(!validUsername(s.username)) throw std::runtime_error("Username requires 1..64 UTF-8 bytes without control characters");
    if(!validDiscordApplicationId(s.discordApplicationId)) throw std::runtime_error("Discord Application ID requires at most 20 digits");
    if (!std::isfinite(s.luck) || s.luck<1 || s.luck>1'000'000 || !std::isfinite(s.volume) || s.volume<0 || s.volume>1)
        throw std::runtime_error("Invalid luck or volume");
    if ((s.avatar!=Avatar::Block && s.avatar!=Avatar::Vrm) || !validBiome(s.biome))
        throw std::runtime_error("Invalid avatar or biome");
    if(!std::isfinite(s.breakthroughCaptionOffset) || std::abs(s.breakthroughCaptionOffset)>5.f)
        throw std::runtime_error("Caption offset must be between -5 and 5 seconds");
    if(!std::isfinite(s.pixelationBpm) || (s.pixelationBpm!=0 && (s.pixelationBpm<30 || s.pixelationBpm>300)) ||
        !std::isfinite(s.pixelationBeatOffset) || std::abs(s.pixelationBeatOffset)>2)
        throw std::runtime_error("Beat settings require BPM=0 or 30..300, offset=-2..2 seconds");
    for(const float bpm:{s.equinoxBpm,s.equinoxCutsceneBpm})
        if(!std::isfinite(bpm) || bpm<30 || bpm>300) throw std::runtime_error("Equinox BPM must be 30..300");
    for(const float offset:{s.equinoxBeatOffset,s.equinoxCutsceneBeatOffset})
        if(!std::isfinite(offset) || std::abs(offset)>2) throw std::runtime_error("Equinox beat offset must be -2..2 seconds");
    float previous=-1;
    for(const float second:s.breakthroughWordTimes) {
        if(!std::isfinite(second) || second<0 || second>=BreakthroughCutsceneDuration || second<=previous)
            throw std::runtime_error("Caption words must have ascending times inside the cutscene");
        previous=second;
    }
    for (const auto* p:{&s.vrmPath,&s.bgmPath,&s.pixelationBgmPath,&s.pixelationFinalBgmPath,&s.illusionaryBgmPath,&s.equinoxBgmPath,&s.profilePhotoPath,&s.serverEndpoint})
        if (p->size()>2048 || p->find_first_of("\r\n\0",0,3)!=std::string::npos)
        throw std::runtime_error("Invalid asset path");
    if (s.language != Language::English && s.language != Language::Japanese) throw std::runtime_error("Invalid language");
    for (const float c : {s.shirt.r, s.shirt.g, s.shirt.b})
        if (!std::isfinite(c) || c < 0 || c > 1) throw std::runtime_error("Invalid shirt color");
}
}
Settings loadSettings(const std::filesystem::path& path) {
    Settings s;
    if (!std::filesystem::exists(path)) return s;
    if (std::filesystem::file_size(path) > 32768) throw std::runtime_error("Settings file too large");
    std::ifstream input(path);
    std::string header, kind, language, extra;
    std::getline(input, header);
    int version{};
    std::istringstream headerInput(header); std::string magic,headerExtra;
    if (!(headerInput>>magic>>version) || magic!="STARS_RNG_SETTINGS" || version<1 || version>9 || (headerInput>>headerExtra))
        throw std::runtime_error("Invalid settings header");
    if (!(input >> kind >> language) || kind != "language" || (language != "en" && language != "ja"))
        throw std::runtime_error("Invalid language setting");
    s.language = language == "ja" ? Language::Japanese : Language::English;
    if (!(input >> kind >> s.shirt.r >> s.shirt.g >> s.shirt.b) || kind != "shirt")
        throw std::runtime_error("Invalid shirt setting");
    if (version>=2) {
        int avatar{},biome{};
        if (!(input>>kind>>s.luck) || kind!="luck" || !(input>>kind>>avatar) || kind!="avatar" ||
            !(input>>kind>>biome) || kind!="biome" || !(input>>kind>>s.volume) || kind!="volume" ||
            !(input>>kind>>std::quoted(s.vrmPath)) || kind!="vrm" || !(input>>kind>>std::quoted(s.bgmPath)) || kind!="bgm")
            throw std::runtime_error("Invalid asset settings");
        s.avatar=static_cast<Avatar>(avatar); s.biome=static_cast<Biome>(biome);
        if (version>=3 &&
            (!(input>>kind>>std::quoted(s.pixelationBgmPath)) || kind!="pixelation_bgm" ||
             !(input>>kind>>std::quoted(s.pixelationFinalBgmPath)) || kind!="pixelation_final_bgm"))
            throw std::runtime_error("Invalid Pixelation audio settings");
        if(version>=4) {
            int enabled{};
            if(!(input>>kind>>std::quoted(s.username)) || kind!="username" ||
                !(input>>kind>>enabled) || kind!="discord_enabled" || (enabled!=0 && enabled!=1) ||
                !(input>>kind>>std::quoted(s.discordApplicationId)) || kind!="discord_application_id")
                throw std::runtime_error("Invalid profile settings");
            s.discordEnabled=enabled!=0;
        }
        if(version>=5) {
            if(!(input>>kind>>s.breakthroughCaptionOffset) || kind!="breakthrough_caption_offset" ||
                !(input>>kind) || kind!="breakthrough_word_times")
                throw std::runtime_error("Invalid caption settings");
            for(float& second:s.breakthroughWordTimes)
                if(!(input>>second)) throw std::runtime_error("Invalid caption word time");
        }
        if(version>=6 && (!(input>>kind>>std::quoted(s.illusionaryBgmPath)) || kind!="illusionary_bgm"))
            throw std::runtime_error("Invalid Illusionary audio setting");
        if(version>=7 && (!(input>>kind>>s.pixelationBpm>>s.pixelationBeatOffset) || kind!="pixelation_beat"))
            throw std::runtime_error("Invalid Pixelation beat setting");
        if(version>=8 && (!(input>>kind>>std::quoted(s.equinoxBgmPath)) || kind!="equinox_bgm" ||
            !(input>>kind>>s.equinoxBpm>>s.equinoxBeatOffset>>s.equinoxCutsceneBpm>>s.equinoxCutsceneBeatOffset) || kind!="equinox_beat"))
            throw std::runtime_error("Invalid Equinox music/beat settings");
    }
    if(version>=9) {
        int configured{};
        if(!(input>>kind>>configured) || kind!="username_configured" || (configured!=0 && configured!=1) ||
            !(input>>kind>>std::quoted(s.serverEndpoint)) || kind!="server_endpoint" ||
            !(input>>kind>>std::quoted(s.profilePhotoPath)) || kind!="profile_photo")
            throw std::runtime_error("Invalid multiplayer profile settings");
        s.usernameConfigured=configured!=0;
    } else s.usernameConfigured=s.username!="Player";
    if (input>>extra) throw std::runtime_error("Unexpected settings data");
    validate(s); return s;
}
void saveSettings(const std::filesystem::path& path, const Settings& settings) {
    validate(settings);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << "STARS_RNG_SETTINGS 9\nlanguage " << (settings.language == Language::Japanese ? "ja" : "en")
        << "\nshirt " << std::setprecision(17) << settings.shirt.r << ' ' << settings.shirt.g << ' ' << settings.shirt.b
        << "\nluck "<<settings.luck<<"\navatar "<<static_cast<int>(settings.avatar)<<"\nbiome "<<static_cast<int>(settings.biome)
        <<"\nvolume "<<settings.volume<<"\nvrm "<<std::quoted(settings.vrmPath)<<"\nbgm "<<std::quoted(settings.bgmPath)
        <<"\npixelation_bgm "<<std::quoted(settings.pixelationBgmPath)
        <<"\npixelation_final_bgm "<<std::quoted(settings.pixelationFinalBgmPath)
        <<"\nusername "<<std::quoted(settings.username)<<"\ndiscord_enabled "<<(settings.discordEnabled?1:0)
        <<"\ndiscord_application_id "<<std::quoted(settings.discordApplicationId)
        <<"\nbreakthrough_caption_offset "<<settings.breakthroughCaptionOffset<<"\nbreakthrough_word_times";
    for(const float second:settings.breakthroughWordTimes) out<<' '<<second;
    out<<"\nillusionary_bgm "<<std::quoted(settings.illusionaryBgmPath)
        <<"\npixelation_beat "<<settings.pixelationBpm<<' '<<settings.pixelationBeatOffset
        <<"\nequinox_bgm "<<std::quoted(settings.equinoxBgmPath)
        <<"\nequinox_beat "<<settings.equinoxBpm<<' '<<settings.equinoxBeatOffset<<' '
        <<settings.equinoxCutsceneBpm<<' '<<settings.equinoxCutsceneBeatOffset
        <<"\nusername_configured "<<(settings.usernameConfigured?1:0)
        <<"\nserver_endpoint "<<std::quoted(settings.serverEndpoint)
        <<"\nprofile_photo "<<std::quoted(settings.profilePhotoPath)<<'\n';
    out.flush();
    if (!out) throw std::runtime_error("Cannot write settings");
    out.close();
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace settings");
#else
    std::filesystem::rename(temporary, path);
#endif
}
CooldownVisual cooldownVisual(float remaining) {
    if (!std::isfinite(remaining) || remaining <= 0) return {};
    const float p = std::clamp(1 - remaining / RollCooldown, 0.f, 1.f);
    const float fadeIn = std::clamp(p / .06f, 0.f, 1.f);
    const float fadeOut = std::clamp((1 - p) / .10f, 0.f, 1.f);
    return {true, p, fadeIn * fadeOut, std::sin(p * 4 * 3.14159265359f) * 3.f};
}
} // namespace aura
