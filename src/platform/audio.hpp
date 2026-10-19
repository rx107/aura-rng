#pragma once
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <filesystem>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

namespace aura {
class Music {
public:
    ~Music();
    void update(const std::filesystem::path& path,bool active,float volume,float dt,float startSeconds=0,float durationSeconds=0);
    const std::string& status() const { return status_; }
    bool playing() const { return playing_; }
    // 実際に出力したサンプル数から求める秒数。読み込み待ち時間を含めない時計です。
    // 再生できない場合は nullopt。MP4 の元時刻ではなく、切り出した音声の 0 秒から。
    // 1 回再生の終了時は指定したクリップ長以上を返し、映像も確実に終了させます。
    std::optional<float> playbackSeconds() const noexcept;
    std::size_t decodedBytes() const { return pcm_.size(); }
    void reload() { clear(); attemptedLoad_=false; }
private:
    void load(const std::filesystem::path& path,float startSeconds,float durationSeconds);
    void clear();
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* master_{};
    IXAudio2SourceVoice* voice_{};
    std::vector<BYTE> pcm_;
    std::filesystem::path attempted_;
    std::string status_{"No music selected"};
    bool mf_{},playing_{},attemptedLoad_{};
    float gain_{};
    float clipStart_{},clipDuration_{};
    unsigned sampleRate_{};
    float playbackEndSeconds_{};
};
// Small original PCM effects. No decoding, looping or allocations during playback.
class SoundEffects {
public:
    ~SoundEffects();
    void playShot(float volume) noexcept;
    void playImpact(float volume) noexcept;
    std::size_t playCount() const { return counts_[0]+counts_[1]; }
    std::size_t shotCount() const { return counts_[0]; }
    std::size_t impactCount() const { return counts_[1]; }
    bool available() const { return ready_; }
    const std::string& status() const { return status_; }
private:
    void initialize();
    void play(unsigned sound,float volume) noexcept;
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* master_{};
    std::array<IXAudio2SourceVoice*,4> voices_{};
    std::array<std::vector<std::int16_t>,2> pcm_;
    std::array<std::chrono::steady_clock::time_point,2> lastPlay_{};
    std::array<std::size_t,2> counts_{};
    std::string status_{"Ready / original synthesized effects"};
    bool attempted_{},ready_{};
};
}
