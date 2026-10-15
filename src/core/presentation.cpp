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
        {"still", "コモン"}, {"glimmer", "アンコモン"}, {"ember", "残火"}, {"verdant", "翠緑"},
        {"tidal", "潮流"}, {"amethyst", "アメジスト"}, {"solar", "太陽"}, {"aurora", "オーロラ"},
        {"astral", "星界"}, {"eventide", "宵闇"}, {"zenith", "天頂"}, {"singularity", "特異点"}};
    for (const auto& [id, name] : names) if (d.id == id) return name;
    return d.name;
}
void Presentation::showPreview(std::size_t index, std::size_t catalogSize, bool& autoRoll) {
    if (index >= catalogSize) throw std::out_of_range("Invalid preview aura");
    preview = index;
    autoRoll = false;
}
namespace {
void validate(const Settings& s) {
    if (s.language != Language::English && s.language != Language::Japanese) throw std::runtime_error("Invalid language");
    for (const float c : {s.shirt.r, s.shirt.g, s.shirt.b})
        if (!std::isfinite(c) || c < 0 || c > 1) throw std::runtime_error("Invalid shirt color");
}
}
Settings loadSettings(const std::filesystem::path& path) {
    Settings s;
    if (!std::filesystem::exists(path)) return s;
    if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("Settings file too large");
    std::ifstream input(path);
    std::string header, kind, language, extra;
    std::getline(input, header);
    if (header != "STARS_RNG_SETTINGS 1") throw std::runtime_error("Invalid settings header");
    if (!(input >> kind >> language) || kind != "language" || (language != "en" && language != "ja"))
        throw std::runtime_error("Invalid language setting");
    s.language = language == "ja" ? Language::Japanese : Language::English;
    if (!(input >> kind >> s.shirt.r >> s.shirt.g >> s.shirt.b) || kind != "shirt" || input >> extra)
        throw std::runtime_error("Invalid shirt setting");
    validate(s); return s;
}
void saveSettings(const std::filesystem::path& path, const Settings& settings) {
    validate(settings);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << "STARS_RNG_SETTINGS 1\nlanguage " << (settings.language == Language::Japanese ? "ja" : "en")
        << "\nshirt " << std::setprecision(9) << settings.shirt.r << ' ' << settings.shirt.g << ' ' << settings.shirt.b << '\n';
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
