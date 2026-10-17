#pragma once
#include <array>

// アバターの姿勢を調整する入口。ブロックと VRM の両方が同じ値を使います。
// 位置・高さはゲーム内の長さ、角度はラジアン（約 0.52 = 30 度）。
// 速度は sin(time * 速度) の係数です。大きくすると揺れが速くなります。
// 左右の配列は { X マイナス側, X プラス側 }。片方だけ変えると非対称のポーズになります。
// 例: singularity::Lift を 0.55f → 0.85f にすると、0.30 だけ高く浮きます。
namespace aura::tuning::avatar {
namespace walking {
// 通常時: 停止中は呼吸だけ、移動中は腕と脚を交互に振ります。
inline constexpr float GaitFrequency=3.f;       // 大きくすると同じ歩行量で足を速く振る
inline constexpr float IdleLift=.018f;          // 停止中の上下動の幅
inline constexpr float IdleFrequency=2.f;
inline constexpr float StepLift=.035f;          // 歩行時の上下動を足す幅
inline constexpr float ArmSwing=.50f;           // 腕の前後の振り幅
inline constexpr float ArmOut=.06f;             // 腕を体から少し離す角度
inline constexpr float LegSwing=.55f;           // 脚の前後の振り幅
inline constexpr float KneeBend=.70f;           // 後ろへ下げた脚の膝の曲げ幅
inline constexpr float BodyRoll=.025f;          // 歩行中の体の左右の傾き
}
namespace singularity {
// 固定ポーズ: 歩いても手足は振らず、宇宙を漂う姿勢のまま移動します。
inline constexpr float Lift=.55f;
inline constexpr float BobAmplitude=.12f;
inline constexpr float BobFrequency=.8f;
inline constexpr float BodyPitch=-.22f;         // 絶対値を増やすと前後の傾きが強くなる
inline constexpr float RollAmplitude=.08f;
inline constexpr float RollFrequency=.6f;
inline constexpr std::array<float,2> ArmPitch={-.3f,-.3f};
inline constexpr std::array<float,2> ArmOut={.90f,.95f};
inline constexpr std::array<float,2> LegPitch={-.35f,.50f};
inline constexpr std::array<float,2> LegOut={.13f,.18f};
inline constexpr std::array<float,2> Knee={-.12f,-.25f};
}
namespace pixelation {
// 固定ポーズ。Lv2 と Lv3 の浮遊速度は action 時計を共有します。
inline constexpr float Lift=.38f;
inline constexpr float BobAmplitude=.07f;
inline constexpr float BobFrequency=2.f;
inline constexpr float RollAmplitude=.025f;
inline constexpr float RollFrequency=1.5f;
inline constexpr std::array<float,2> ArmOut={.55f,.55f};
inline constexpr std::array<float,2> ArmPitch={-.35f,-.35f};
inline constexpr std::array<float,2> LegPitch={-.07f,.10f};
inline constexpr std::array<float,2> LegOut={.04f,.04f};
inline constexpr std::array<float,2> Knee={-.04f,-.09f};
}
namespace equinox {
// 固定ポーズ: 手を少し開き、まっすぐ静かに浮遊します。BreakThrough でも使います。
inline constexpr float Lift=.28f;
inline constexpr float BobAmplitude=.04f;
inline constexpr float BobFrequency=1.3f;
inline constexpr float RollAmplitude=.015f;
inline constexpr float RollFrequency=.8f;
inline constexpr std::array<float,2> ArmOut={.35f,.35f};
inline constexpr std::array<float,2> ArmPitch={-.15f,-.15f};
inline constexpr std::array<float,2> LegOut={.03f,.03f};
}
static_assert(walking::GaitFrequency>0 && walking::IdleFrequency>0);
static_assert(singularity::BobFrequency>0 && pixelation::BobFrequency>0 && equinox::BobFrequency>0);
} // namespace aura::tuning::avatar
