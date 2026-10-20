#pragma once
#include "tuning/stages.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace aura {
// 保存済みの番号0=庭園、1=砂漠は変えません。新しい惑星は後ろへ追加します。
enum class Biome { Normal, Desert, Ocean, Forest, Volcano, Extra };
inline constexpr unsigned StageCount = 6;
inline constexpr bool validBiome(Biome stage) {
    return static_cast<unsigned>(stage)<StageCount;
}
// 黄色い星の輪郭。cosを使うため先端も谷も丸くなり、鋭い五角星にはなりません。
// 描画と移動制限が同じ式を使うので、星の外へ歩いて落ちることを防げます。
inline float extraStageRadius(float angle,float halfExtent) {
    const float middle=(tuning::stages::ExtraOuterRadiusRatio+tuning::stages::ExtraInnerRadiusRatio)*.5f;
    const float wave=(tuning::stages::ExtraOuterRadiusRatio-tuning::stages::ExtraInnerRadiusRatio)*.5f;
    return halfExtent*(middle+wave*std::cos(5.f*(angle-1.57079632679f)));
}
inline void clampStagePosition(Biome stage,float& x,float& z,float halfExtent,float padding) {
    if(stage==Biome::Extra) {
        const float radius=std::hypot(x,z);
        const float limit=std::max(0.f,extraStageRadius(std::atan2(z,x),halfExtent)-padding);
        if(radius>limit && radius>0) {x*=limit/radius; z*=limit/radius;}
    } else {
        const float limit=std::max(0.f,halfExtent-padding);
        x=std::clamp(x,-limit,limit); z=std::clamp(z,-limit,limit);
    }
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
