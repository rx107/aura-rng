#pragma once
#include "core/aura.hpp"
#include "core/stages.hpp"
#include "tuning/cutscene.hpp"
#include "tuning/game.hpp"
#include "tuning/redjohn.hpp"
#include "tuning/world.hpp"
#include <array>
#include <optional>

namespace aura {
enum class Language { English, Japanese };
enum class Avatar { Block, Vrm };
enum class CutsceneKind { Equinox, Breakthrough, RedJohn };
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
    std::string vrmPath, bgmPath, pixelationBgmPath, pixelationFinalBgmPath, illusionaryBgmPath;
    std::string username{"Player"},discordApplicationId{"1560569853184573590"};
    bool discordEnabled{true};
    float volume{.55f};
    // 字幕の微調整 [秒]。負数は早く、正数は遅く表示。F1から変更して保存できます。
    float breakthroughCaptionOffset{};
    std::array<float,8> breakthroughWordTimes{tuning::cutscene::BreakthroughWordTimes};
    // 0=曲からBPMを推定。正の値は手動の拍数/分。時差は秒、正なら鼓動を遅らせます。
    float pixelationBpm{},pixelationBeatOffset{};
    std::string equinoxBgmPath;
    // Equinoxは通常BGMとCutSceneを別々に合わせます。BPM=拍/分、正の時差=遅くする。
    float equinoxBpm{180.f},equinoxBeatOffset{.044f};
    float equinoxCutsceneBpm{180.f},equinoxCutsceneBeatOffset{.056f};
    // マルチ参加時は初期名のまま進まず、自分で名前を確定します。ID/秘密鍵は別ファイル。
    bool usernameConfigured{};
    std::string serverEndpoint{"http://127.0.0.1:8765"},profilePhotoPath;
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
    bool playEquinoxCutscene{},playBreakthroughCutscene{},playRedJohnCutscene{},stopCutscene{};
    std::optional<std::size_t> preview;
    std::optional<Language> language;
    std::optional<Color> shirt;
    std::optional<double> luck;
    std::optional<Avatar> avatar;
    std::optional<Biome> biome;
    std::optional<std::string> vrmPath, bgmPath, pixelationBgmPath, pixelationFinalBgmPath, illusionaryBgmPath;
    std::optional<float> volume;
    std::optional<std::string> username,discordApplicationId;
    std::optional<bool> discordEnabled;
    std::optional<std::uint64_t> rollCount;
    std::optional<float> breakthroughCaptionOffset;
    std::optional<std::array<float,8>> breakthroughWordTimes;
    std::optional<float> pixelationBpm,pixelationBeatOffset;
    std::optional<std::string> equinoxBgmPath;
    std::optional<float> equinoxBpm,equinoxBeatOffset,equinoxCutsceneBpm,equinoxCutsceneBeatOffset;
    bool connectMultiplayer{},disconnectMultiplayer{},clearProfilePhoto{},closeMultiplayer{};
    bool closeServerSettings{};
    std::optional<std::string> serverEndpoint,profilePhotoPath;
};
// 他の処理から読む共通名。数値の編集場所は tuning/game.hpp に集約。
inline constexpr float RollCooldown = tuning::game::RollCooldownSeconds;
inline constexpr float EquinoxCutsceneDuration = 10.9f;
inline constexpr float BreakthroughCutsceneDuration = tuning::cutscene::BreakthroughDuration;
inline constexpr float BreakthroughAudioStart = tuning::cutscene::BreakthroughAudioStart;
inline constexpr float RedJohnCutsceneDuration = tuning::redjohn::CutsceneDuration;
inline constexpr float cutsceneDuration(CutsceneKind kind) {
    switch(kind) {
    case CutsceneKind::Breakthrough: return BreakthroughCutsceneDuration;
    case CutsceneKind::RedJohn: return RedJohnCutsceneDuration;
    default: return EquinoxCutsceneDuration;
    }
}
inline constexpr float StageHalfExtent = tuning::world::HalfExtent;
inline constexpr float StageWalkLimit = StageHalfExtent-tuning::world::EdgeWalkPadding;
struct CooldownVisual { bool visible{}; float progress{}, opacity{}, waveY{}; };
CooldownVisual cooldownVisual(float remaining);
} // namespace aura
