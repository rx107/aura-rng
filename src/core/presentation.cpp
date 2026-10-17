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
std::string displayName(const Definition& d, Language language) {
    if (language == Language::English) return d.name;
    const std::pair<const char*, const char*> names[] = {
        {"still", "コモン"}, {"glimmer", "アンコモン"}, {"ember", "残火"}, {"stardust", "星屑"}, {"verdant", "翠緑"},
        {"tidal", "潮流"}, {"amethyst", "アメジスト"}, {"solar", "太陽"}, {"aurora", "オーロラ"},
        {"astral", "星界"}, {"eventide", "宵闇"}, {"zenith", "天頂"}, {"pixelation", "ピクセレーション"},
        {"equinox", "エキノックス"}, {"breakthrough", "ブレイクスルー"}, {"nanally", "ナナリー"}, {"singularity", "特異点"}};
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
    if ((s.avatar!=Avatar::Block && s.avatar!=Avatar::Vrm) || (s.biome!=Biome::Normal && s.biome!=Biome::Desert))
        throw std::runtime_error("Invalid avatar or biome");
    for (const auto* p:{&s.vrmPath,&s.bgmPath,&s.pixelationBgmPath,&s.pixelationFinalBgmPath})
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
    if (header != "STARS_RNG_SETTINGS 1" && header != "STARS_RNG_SETTINGS 2" && header != "STARS_RNG_SETTINGS 3" && header != "STARS_RNG_SETTINGS 4")
        throw std::runtime_error("Invalid settings header");
    if (!(input >> kind >> language) || kind != "language" || (language != "en" && language != "ja"))
        throw std::runtime_error("Invalid language setting");
    s.language = language == "ja" ? Language::Japanese : Language::English;
    if (!(input >> kind >> s.shirt.r >> s.shirt.g >> s.shirt.b) || kind != "shirt")
        throw std::runtime_error("Invalid shirt setting");
    if (header!="STARS_RNG_SETTINGS 1") {
        int avatar{},biome{};
        if (!(input>>kind>>s.luck) || kind!="luck" || !(input>>kind>>avatar) || kind!="avatar" ||
            !(input>>kind>>biome) || kind!="biome" || !(input>>kind>>s.volume) || kind!="volume" ||
            !(input>>kind>>std::quoted(s.vrmPath)) || kind!="vrm" || !(input>>kind>>std::quoted(s.bgmPath)) || kind!="bgm")
            throw std::runtime_error("Invalid asset settings");
        s.avatar=static_cast<Avatar>(avatar); s.biome=static_cast<Biome>(biome);
        if ((header=="STARS_RNG_SETTINGS 3" || header=="STARS_RNG_SETTINGS 4") &&
            (!(input>>kind>>std::quoted(s.pixelationBgmPath)) || kind!="pixelation_bgm" ||
             !(input>>kind>>std::quoted(s.pixelationFinalBgmPath)) || kind!="pixelation_final_bgm"))
            throw std::runtime_error("Invalid Pixelation audio settings");
        if(header=="STARS_RNG_SETTINGS 4") {
            int enabled{};
            if(!(input>>kind>>std::quoted(s.username)) || kind!="username" ||
                !(input>>kind>>enabled) || kind!="discord_enabled" || (enabled!=0 && enabled!=1) ||
                !(input>>kind>>std::quoted(s.discordApplicationId)) || kind!="discord_application_id")
                throw std::runtime_error("Invalid profile settings");
            s.discordEnabled=enabled!=0;
        }
    }
    if (input>>extra) throw std::runtime_error("Unexpected settings data");
    validate(s); return s;
}
void saveSettings(const std::filesystem::path& path, const Settings& settings) {
    validate(settings);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << "STARS_RNG_SETTINGS 4\nlanguage " << (settings.language == Language::Japanese ? "ja" : "en")
        << "\nshirt " << std::setprecision(17) << settings.shirt.r << ' ' << settings.shirt.g << ' ' << settings.shirt.b
        << "\nluck "<<settings.luck<<"\navatar "<<static_cast<int>(settings.avatar)<<"\nbiome "<<static_cast<int>(settings.biome)
        <<"\nvolume "<<settings.volume<<"\nvrm "<<std::quoted(settings.vrmPath)<<"\nbgm "<<std::quoted(settings.bgmPath)
        <<"\npixelation_bgm "<<std::quoted(settings.pixelationBgmPath)
        <<"\npixelation_final_bgm "<<std::quoted(settings.pixelationFinalBgmPath)
        <<"\nusername "<<std::quoted(settings.username)<<"\ndiscord_enabled "<<(settings.discordEnabled?1:0)
        <<"\ndiscord_application_id "<<std::quoted(settings.discordApplicationId)<<'\n';
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
