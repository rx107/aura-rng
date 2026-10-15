#pragma once
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <filesystem>
#include <string>
#include <vector>

namespace aura {
class Music {
public:
    ~Music();
    void update(const std::filesystem::path& path,bool active,float volume,float dt);
    const std::string& status() const { return status_; }
    bool playing() const { return playing_; }
    std::size_t decodedBytes() const { return pcm_.size(); }
    void reload() { clear(); attemptedLoad_=false; }
private:
    void load(const std::filesystem::path& path);
    void clear();
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* master_{};
    IXAudio2SourceVoice* voice_{};
    std::vector<BYTE> pcm_;
    std::filesystem::path attempted_;
    std::string status_{"No music selected"};
    bool mf_{},playing_{},attemptedLoad_{};
    float gain_{};
};
}
