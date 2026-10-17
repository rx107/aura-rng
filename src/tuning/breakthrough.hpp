#pragma once
#include "core/aura.hpp"
#include <array>

// BreakThroughの「通常のオーラ表示」を調整する入口です。
// 保存 → Develop.cmd → F1でBreakThroughを表示、の順に画面を確認してください。
// 単位: 長さはゲーム内座標（アバターの身長は約3）、時間は秒、角度はラジアン。
// CutSceneは別のHLSLで描くため、このファイルの柱設定では変わりません。
// CutSceneでは柱を静止させる仕様を、通常表示の浮遊設定から分離しています。
namespace aura::tuning::breakthrough {

// RGBの色。1を超える成分は強く発光。例: blue.rを上げると紫寄りになります。
inline constexpr Color blue{.39f,.46f,1.f};
inline constexpr Color silver{.83f,.88f,1.2f};
inline constexpr Color white{1.6f,1.6f,1.75f};
inline constexpr Color plateInk{.62f,.66f,.83f};

// 柱1本の設定。x/zはアバターからの横/奥行き、yは柱の中心の高さ。
// width/height/depthは横幅/全高/厚み、tilt/turnは傾き/水平回転。
// 例: 最初の行のheightを8にすると中央の柱が高くなります。
// 7本の本数と並びは維持してください（CutSceneの7本との対応が分かりやすくなります）。
struct Tower { float x,y,z,width,height,depth,tilt,turn; };
inline constexpr std::array<Tower,7> towers{{
    {0,4.05f,.65f,.86f,7.3f,.74f,0,.10f},
    {-2.15f,1.95f,.25f,.70f,3.9f,.64f,.42f,-.12f},
    {2.15f,1.95f,.25f,.70f,3.9f,.64f,-.42f,.12f},
    {-1.35f,3.35f,1.25f,.48f,3.5f,.50f,.10f,-.10f},
    {1.35f,3.35f,1.25f,.48f,3.5f,.50f,-.10f,.10f},
    {-.96f,2.2f,-.20f,.44f,2.3f,.50f,-.13f,.10f},
    {.96f,2.2f,-.20f,.44f,2.3f,.50f,.13f,-.10f}
}};
// 浮遊の片側の幅と周期。例: floatAmplitudeとfloatAmplitudeStepを両方0で全柱停止。
// floatPeriodSeconds=6でゆっくりに。
// Step値は柱ごとのばらつき。0にすると同じ幅・周期になります。
inline constexpr float floatAmplitude=.14f;
inline constexpr float floatAmplitudeStep=.02f;
inline constexpr float floatPeriodSeconds=3.6f;
inline constexpr float floatPeriodStepSeconds=.35f;
inline constexpr float floatPhaseStep=.91f; // ラジアン。これと周期のStepも0にすると上下が同期。
inline constexpr unsigned fragmentsPerTower=24; // 破片の数/柱。例: 12で軽く、少ない見た目。

// 縦に巻き付く細い光。例: strandCount=4で本数を減らし、strandHeight=7で上まで伸ばす。
inline constexpr unsigned strandCount=7;
inline constexpr unsigned strandSegments=84;
inline constexpr float strandRadius=2.1f;
inline constexpr float strandRadiusWave=.40f;
inline constexpr float strandHeight=5.8f;
inline constexpr float strandForwardRotation=.22f; // ラジアン/秒。負にすると逆回転。
inline constexpr float strandReverseRotation=-.27f;

// 二つの強い光の高さ。例: upperLightHeight=4.3で上側の光を高く。
inline constexpr float upperLightHeight=3.90f;
inline constexpr float lowerLightHeight=.25f;

// 足元の渦。例: floorOuterRadius=3で広く、floorOuterRotation=.11で外側の回転を半分に。
inline constexpr unsigned floorSegments=96;
inline constexpr float floorOuterRadius=2.45f;
inline constexpr float floorRingSpacing=.30f;
inline constexpr float floorOuterRotation=.22f;
inline constexpr float floorInnerRotation=-.3f;

// 周囲から集まる霧と上昇する粒子。増やすと密度・描画量が増えます。
// 例: mistCount=26で霧を半分、dustRiseSpeed=.14で粒子の上昇を半分に。
inline constexpr unsigned mistCount=52;
inline constexpr float mistTravelSpeed=.075f; // 周期/秒。
inline constexpr float mistBrightness=.36f;
inline constexpr unsigned dustCount=220;
inline constexpr float dustRiseSpeed=.28f; // 座標/秒。
inline constexpr float dustBrightness=.63f;

// 頭上の名前。heightを増やすと上へ、forwardを増やすとカメラ側へ。
// 例: titleHeight=3.9で少し上へ。titleWidth/titleTextHeightで文字だけの大きさを変更。
inline constexpr float titleHeight=3.55f;
inline constexpr float titleForward=1.8f;
inline constexpr float titleWidth=3.30f;
inline constexpr float titleTextHeight=.35f;
inline constexpr float subtitleWidth=1.8f;
inline constexpr float subtitleTextHeight=.14f;
inline constexpr float subtitleOffsetY=-.26f; // 主タイトルからの高さ。負にすると下へ。

static_assert(towers.size()==7);
static_assert([] { for(const auto& tower:towers) if(tower.width<=0 || tower.height<=0 || tower.depth<=0) return false; return true; }());
static_assert(floatAmplitude>=0 && floatAmplitudeStep>=0 && floatPeriodSeconds>0 && floatPeriodStepSeconds>=0);
static_assert(fragmentsPerTower<=64 && strandCount<=12 && strandSegments>=3 && strandSegments<=128);
static_assert(floorSegments>=3 && floorSegments<=256 && mistCount<=128 && dustCount<=512);
} // namespace aura::tuning::breakthrough
