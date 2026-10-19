#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace aura {
// 保存済みの番号0=庭園、1=砂漠は変えません。新しい惑星は後ろへ追加します。
enum class Biome { Normal, Desert, Ocean, Forest, Volcano };
inline constexpr unsigned StageCount = 5;
inline constexpr bool validBiome(Biome stage) {
    return static_cast<unsigned>(stage)<StageCount;
}

// 惑星移動の時計。描画から独立しているため、移動の途中もテストできます。
// 半分の時点で景観を切り替え、残り半分で到着を見せます。
class StageTravel {
public:
    bool start(Biome destination,Biome current) {
        if(active() || !validBiome(destination) || destination==current) return false;
        target_=destination; progress_=0; switched_=false; return true;
    }
    std::optional<Biome> update(float dt,float duration) {
        if(!active() || !std::isfinite(dt) || dt<0 || !std::isfinite(duration) || duration<=0) return {};
        progress_=std::min(1.f,progress_+dt/duration);
        if(!switched_ && progress_>=.5f) {switched_=true; return target_;}
        return {};
    }
    bool active() const { return progress_>=0; }
    bool arrived() const { return progress_>=1; }
    float progress() const { return progress_; }
    Biome target() const { return target_; }
    void finish() { progress_=-1; }
private:
    Biome target_{Biome::Normal};
    float progress_{-1};
    bool switched_{};
};
}
