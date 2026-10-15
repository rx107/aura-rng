#pragma once
#include "core/aura.hpp"
#include <optional>

namespace aura {
enum class Language { English, Japanese };
enum class Avatar { Block, Vrm };
enum class Biome { Normal, Desert };
inline const char* tr(Language language, const char* english, const char* japanese) {
    return language == Language::Japanese ? japanese : english;
}
std::string displayName(const Definition& definition, Language language);
struct Settings {
    Language language{Language::English};
    Color shirt{.54f, .83f, .89f};
    double luck{1};
    Avatar avatar{Avatar::Block};
    Biome biome{Biome::Normal};
    std::string vrmPath, bgmPath;
    float volume{.55f};
};
Settings loadSettings(const std::filesystem::path& path);
void saveSettings(const std::filesystem::path& path, const Settings& settings);
struct Presentation {
    Settings settings;
    std::optional<std::size_t> preview;
    std::size_t shown(const Progress& progress) const { return preview.value_or(progress.equipped); }
    void showPreview(std::size_t index, std::size_t catalogSize, bool& autoRoll);
};
struct DebugActions {
    bool roll{}, toggleAuto{}, restore{};
    std::optional<std::size_t> preview;
    std::optional<Language> language;
    std::optional<Color> shirt;
    std::optional<double> luck;
    std::optional<Avatar> avatar;
    std::optional<Biome> biome;
    std::optional<std::string> vrmPath, bgmPath;
    std::optional<float> volume;
};
inline constexpr float RollCooldown = 1.4f;
struct CooldownVisual { bool visible{}; float progress{}, opacity{}, waveY{}; };
CooldownVisual cooldownVisual(float remaining);
} // namespace aura
