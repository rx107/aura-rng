#pragma once
#include <array>
#include <cstdint>
#include "tuning/pixelation.hpp"

namespace aura {
struct AvatarPose {
    // fixed=true は固定ポーズを計算した目印。歩行を混ぜない分岐は各関数にあります。
    // 手足の値はラジアンです。
    bool fixed{};
    float lift{},bodyPitch{},bodyRoll{};
    // torsoPitch は腰から上、headPitch は首から上の追加角度。
    // 0 のままなら元の姿勢を保ち、Illusionary のような反り返る姿勢だけで使います。
    float torsoPitch{},headPitch{};
    std::array<float,2> armPitch{},armOut{},legPitch{},legOut{},knee{},elbowPitch{};
};
// 姿勢を計算するだけの処理。描画は renderer.cpp / vrm.cpp が担当します。
// ブロックと VRM で共通。見た目を調整する入口は tuning/avatar.hpp です。
AvatarPose avatarPose(float time,float walkPhase,float movement,bool singularity);
AvatarPose pixelationPose(float time);
AvatarPose equinoxPose(float time);
AvatarPose illusionaryPose(float time);
float pixelationMotionSpeed(unsigned level);
float pixelationMotionTime(float elapsed,unsigned level);
float pixelationActionSpeed(unsigned level);
float pixelationActionTime(float elapsed,unsigned level);
struct EffectPoint { float x{},y{},z{}; };
EffectPoint pixelationPlayer(float orbitTime,unsigned player);
// 呼び出し元・テストで使う公開名。実際の編集先は tuning/pixelation.hpp です。
inline constexpr float PixelationVolleyPeriod=tuning::pixelation::VolleyPeriod;
inline constexpr float PixelationVolleyFlight=tuning::pixelation::VolleyFlight;
struct PixelationVolley {
    std::array<EffectPoint,2> players{},launches{},balls{};
    EffectPoint meeting{0,tuning::pixelation::MeetingHeight,0};
    float flightProgress{},shotAge{},impactAge{-1};
    bool flightVisible{};
    std::int64_t shotCycle{},impactCycle{-1};
};
// 座標はアバターからの相対位置。2 発が同じ進行度で同じ meeting に到着します。
PixelationVolley pixelationVolley(float effectTime);
// actionTime = 弾・衝突の時計、orbitTime = PLAYER の周回の時計。
// shotOrbitTime は発射した瞬間の周回時計。飛行中は固定し、Lv 切替でも発射元を動かしません。
PixelationVolley pixelationVolley(float actionTime,float orbitTime,float shotOrbitTime);
struct AbsorbedStar { float x{},y{},z{},size{}; };
AbsorbedStar singularityStar(float time,unsigned index,unsigned count);
}
