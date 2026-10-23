#pragma once
#include "tuning/equinox.hpp"
#include <algorithm>
#include <cmath>

// Equinoxの配置はGPUや音声装置を使わない計算だけに分けています。
// 描画とテストがこの同じ計算を使うため、玉・尾・文字の拍がずれません。
namespace aura::equinox {
inline constexpr float Pi=3.14159265359f;
struct Point { float x{},y{},z{}; };

// 音楽側が渡すbeatは「経過秒」ではなく連続した経過拍数です。
// 音楽がない/未読込なら負数を渡し、ゲームの秒数から同じ単位へ変換します。
inline float beatClock(float musicBeat,float elapsed) {
    if(std::isfinite(musicBeat) && musicBeat>=0) return musicBeat;
    return std::isfinite(elapsed)?std::max(0.f,elapsed)*tuning::equinox::fallbackBpm/60.f:0.f;
}
// 加速と減速を含む共通の周回角度です。sinの往復分を足すため、平均の周回時間は変わりません。
// 角度の増え方は「1 + 強さ*cos(...)」。強さを1未満に保つと常に前へ進みます。
inline float orbOrbitAngle(float beat) {
    if(!std::isfinite(beat)) beat=0;
    const float base=beat*2*Pi/tuning::equinox::orbitBeats;
    return base+tuning::equinox::orbitSpeedVariation*tuning::equinox::orbitSpeedCycleOrbits*
        std::sin(base/tuning::equinox::orbitSpeedCycleOrbits);
}
// 平均速度を1とした、その拍での速さ。画面を使わないテストでも逆走しないか確認できます。
inline float orbSpeedScale(float beat) {
    if(!std::isfinite(beat)) beat=0;
    const float base=beat*2*Pi/tuning::equinox::orbitBeats;
    return 1+tuning::equinox::orbitSpeedVariation*std::cos(base/tuning::equinox::orbitSpeedCycleOrbits);
}
inline Point orbPosition(float beat,unsigned side) {
    const float angle=orbOrbitAngle(beat)+(side&1u)*Pi;
    return {std::cos(angle)*tuning::equinox::orbOrbitRadius,
            tuning::equinox::orbHeight+std::sin(angle)*tuning::equinox::orbBobHeight,
            std::sin(angle)*tuning::equinox::orbOrbitRadius};
}
// 片側の値をXORして使うので、必ず白が1個・黒が1個になります。
inline unsigned polarity(float beat) {
    if(!std::isfinite(beat) || beat<0) return 0;
    return static_cast<unsigned>(std::fmod(std::floor(beat/tuning::equinox::polarityBeats),2.f));
}
// 入替から何拍経ったか。burstBeats未満のときだけ爆発を描きます。
inline float transitionAge(float beat) {
    if(!std::isfinite(beat) || beat<0) return tuning::equinox::polarityBeats;
    return std::fmod(beat,tuning::equinox::polarityBeats);
}
// アトラスの左から「陰・陽・衡・無」。文字の変更も音楽と同じ拍に同期。
inline unsigned floorGlyphIndex(float beat) {
    if(!std::isfinite(beat) || beat<0) return 0;
    return static_cast<unsigned>(std::fmod(std::floor(beat/tuning::equinox::floorGlyphBeats),4.f));
}
} // namespace aura::equinox
