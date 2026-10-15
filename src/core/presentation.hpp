#pragma once
#include "core/aura.hpp"
#include <optional>

namespace aura {
enum class Language { English, Japanese };
inline const char* tr(Language language, const char* english, const char* japanese) {
    return language == Language::Japanese ? japanese : english;
}
std::string displayName(const Definition& definition, Language language);
struct Settings {
    Language language{Language::English};
    Color shirt{.54f, .83f, .89f};
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
};
inline constexpr float RollCooldown = 1.4f;
struct CooldownVisual { bool visible{}; float progress{}, opacity{}, waveY{}; };
CooldownVisual cooldownVisual(float remaining);
} // namespace aura
