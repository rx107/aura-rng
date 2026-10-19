#pragma once
#include "core/aura.hpp"
#include "core/stages.hpp"
#include "tuning/cutscene.hpp"
#include "tuning/game.hpp"
#include "tuning/world.hpp"
#include <array>
#include <optional>

namespace aura {
enum class Language { English, Japanese };
enum class Avatar { Block, Vrm };
enum class CutsceneKind { Equinox, Breakthrough };
inline const char* tr(Language language, const char* english, const char* japanese) {
    return language == Language::Japanese ? japanese : english;
}
std::string displayName(const Definition& definition, Language language);
std::string stageDisplayName(Biome stage,Language language);
struct Settings {
    // F1で変えて保存する「プレイヤーの設定」。src/tuningはビルド時のゲーム調整値。
    // settings.txtが既にある場合は、その保存内容を優先して読み込みます。
    Language language{Language::English};
    Color shirt{.54f, .83f, .89f};
    double luck{1};
    Avatar avatar{Avatar::Block};
    Biome biome{Biome::Normal};
    std::string vrmPath, bgmPath, pixelationBgmPath, pixelationFinalBgmPath;
    std::string username{"Player"},discordApplicationId{"1560569853184573590"};
    bool discordEnabled{true};
    float volume{.55f};
    // 字幕の微調整 [秒]。負数は早く、正数は遅く表示。F1から変更して保存できます。
    float breakthroughCaptionOffset{};
    std::array<float,8> breakthroughWordTimes{tuning::cutscene::BreakthroughWordTimes};
};
Settings loadSettings(const std::filesystem::path& path);
void saveSettings(const std::filesystem::path& path, const Settings& settings);
bool validUsername(const std::string& value);
bool validDiscordApplicationId(const std::string& value);
struct Presentation {
    // equipped（実際の装備）を保ったまま、F1のプレビューだけを一時表示する仕組み。
    Settings settings;
    std::optional<std::size_t> preview;
    std::optional<unsigned> singularityLevel;
    std::optional<unsigned> pixelationLevel;
    std::size_t shown(const Progress& progress) const { return preview.value_or(progress.equipped); }
    unsigned level(const Progress& progress,const std::vector<Definition>& catalog) const;
    void showPreview(std::size_t index, std::size_t catalogSize, bool& autoRoll);
};
struct DebugActions {
    // ImGuiはこの「変更のお願い」を作り、main.cppが後で実際のゲーム状態へ反映します。
    bool roll{}, toggleAuto{}, restore{};
    bool actualLevel{};
    std::optional<unsigned> auraLevel;
    bool pixelationActualLevel{};
    std::optional<unsigned> pixelationLevel;
    bool playEquinoxCutscene{},playBreakthroughCutscene{},stopCutscene{};
    std::optional<std::size_t> preview;
    std::optional<Language> language;
    std::optional<Color> shirt;
    std::optional<double> luck;
    std::optional<Avatar> avatar;
    std::optional<Biome> biome;
    std::optional<std::string> vrmPath, bgmPath, pixelationBgmPath, pixelationFinalBgmPath;
    std::optional<float> volume;
    std::optional<std::string> username,discordApplicationId;
    std::optional<bool> discordEnabled;
    std::optional<std::uint64_t> rollCount;
    std::optional<float> breakthroughCaptionOffset;
    std::optional<std::array<float,8>> breakthroughWordTimes;
};
// 他の処理から読む共通名。数値の編集場所は tuning/game.hpp に集約。
inline constexpr float RollCooldown = tuning::game::RollCooldownSeconds;
inline constexpr float EquinoxCutsceneDuration = 10.9f;
inline constexpr float BreakthroughCutsceneDuration = tuning::cutscene::BreakthroughDuration;
inline constexpr float BreakthroughAudioStart = tuning::cutscene::BreakthroughAudioStart;
inline constexpr float cutsceneDuration(CutsceneKind kind) {
    return kind==CutsceneKind::Breakthrough?BreakthroughCutsceneDuration:EquinoxCutsceneDuration;
}
inline constexpr float StageHalfExtent = tuning::world::HalfExtent;
inline constexpr float StageWalkLimit = StageHalfExtent-tuning::world::EdgeWalkPadding;
struct CooldownVisual { bool visible{}; float progress{}, opacity{}, waveY{}; };
CooldownVisual cooldownVisual(float remaining);
} // namespace aura
