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
namespace illusionary {
// 胸を大きく反らし、天を仰ぐ固定ポーズ。アバターの正面は Z マイナス側です。
// 角度は全てラジアン。ブロックと VRM が同じ設定を使い、歩行中も形は固定します。
inline constexpr float Lift=.90f; // 全身の高さ。増やすとチェック床から高く浮く。
inline constexpr float BobAmplitude=.07f, BobFrequency=1.1f;
inline constexpr float BodyPitch=.26f; // 全身の後傾。正へ増やすと頭側が後ろへ傾く。
inline constexpr float BodyRoll=0.f; // 横倒しにせず、顔を真上へ向ける。
inline constexpr float TorsoPitch=.55f; // 腰から上の反り。脚を残して胸・肩・頭を後方へ反らす。
inline constexpr float HeadPitch=.7607963f; // 首の反り。3つの Pitch の合計が約π/2で真上を見る。
inline constexpr std::array<float,2> ArmOut={1.45f,.40f}; // 1 番目の上腕を肩の高さまで持ち上げる。
inline constexpr std::array<float,2> ArmPitch={-.81f,-.38f}; // 胸の反りを打ち消し、伸ばした腕は斜め下・手前へ向ける。
inline constexpr std::array<float,2> ElbowPitch={1.5707963f,0.f}; // 片肘だけ直角。もう片方は真っすぐ。
inline constexpr std::array<float,2> LegPitch={-.18f,.18f};
inline constexpr std::array<float,2> LegOut={.08f,.15f};
inline constexpr std::array<float,2> Knee={-.10f,-.45f}; // 脚は浮遊したまま軽く左右差を付ける。
}
namespace redJohn {
// ベッドで片膝を立てる固定ポーズ。画像の人物そのものではなく、体の姿勢だけを再現します。
// アバターの正面は -Z。配列の 1 個目が -X 側（立てた膝・乗せる腕）、2 個目が支える手の側です。
inline constexpr float Lift=-.26f; // 下げると腰が低くなる。標準の腰 1.10 + Lift = .84、マット上面 .60 に座る高さ。
inline constexpr float HipHeight=1.10f; // VRM の腰もこの高さへ揃え、モデルが変わってもベッドの上に座らせる。
inline constexpr float BodyRoll=-.08f; // 負にすると +X 側の肩が下がり、支える手へ少しだけ体重が乗る。
inline constexpr float TorsoPitch=-.035f, HeadPitch=.015f; // 胸・顔の前後角度。大きくすると前後に傾く。
inline constexpr float BreathingAmplitude=.006f, BreathingFrequency=1.35f; // 胸だけがわずかに呼吸する。0 で完全静止。
inline constexpr std::array<float,2> ArmOut={-.12f,.28f}; // 支える手を外へ出すには 2 個目を増やす。
inline constexpr std::array<float,2> ArmPitch={1.f,-.10f};
inline constexpr std::array<float,2> ArmYaw={-1.f,0.f}; // 1 個目を変えると、膝に乗せた前腕の左右位置が変わる。
inline constexpr std::array<float,2> ElbowPitch={.55f,.04f}; // 片肘を曲げて前腕を膝の高さへ。増やしすぎると手が顔の前へ上がる。
inline constexpr std::array<float,2> LegPitch={2.40f,1.40f}; // 1 個目を増やすと立てた膝が高くなる。
inline constexpr std::array<float,2> LegOut={.12f,.50f};
inline constexpr std::array<float,2> LegYaw={.08f,-.90f}; // 2 個目で折り畳んだ脚を横向きにする。
inline constexpr std::array<float,2> Knee={-2.32f,-2.30f}; // 膝から先の曲げ。0 だと膝が伸びる。
}
static_assert(walking::GaitFrequency>0 && walking::IdleFrequency>0);
static_assert(singularity::BobFrequency>0 && pixelation::BobFrequency>0 && equinox::BobFrequency>0);
} // namespace aura::tuning::avatar
