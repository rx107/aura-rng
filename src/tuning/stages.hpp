#pragma once

// 惑星の見た目を変更する入口。保存 → Develop.cmd で作り直すと反映されます。
// ステージの広さは world.hpp の HalfExtent、惑星選択はゲーム内の Stage Select です。
namespace aura::tuning::stages {

// ロケットの惑星間移動にかかる秒数。3.6f → 5.f で出発/到着の演出がゆっくりになります。
inline constexpr float TravelDurationSeconds = 3.6f;

// 全惑星にあるロケットの位置 [ワールド単位]。Xは左右、Zは前後。
// Xを4.5f → 6.fにすると、発着場ごと中央から右に離れます。
inline constexpr float RocketX = 4.5f;
inline constexpr float RocketZ = 1.f;
inline constexpr float RocketScale = 1.f; // ロケット本体の大きさ。1.2fなら20%大きくなります。

// 海: 島の半径はステージ半幅に対する割合。0.43f → 0.55fで砂浜が広くなります。
inline constexpr float OceanIslandRadiusRatio = .43f;
inline constexpr unsigned OceanWaveCount = 36; // 水面を流れる白い波の数。増やすほどにぎやか。
inline constexpr float OceanWaveSpeed = .55f; // 波の進む速さ。0.fなら静止。
inline constexpr float OceanWaveHeight = .045f; // 水面の上下幅。大きいほど波が弾みます。

// 森: 木の本数。中央の広場と十字の道は、木を置かずに空けています。
inline constexpr unsigned ForestTreeCount = 64;
inline constexpr float ForestTreeHeight = 4.6f; // 平均の高さ。6.fなどで背の高い森にできます。
inline constexpr unsigned ForestFireflyCount = 36; // 黄緑色に光るホタルの数。
inline constexpr float ForestFireflySpeed = .38f; // ホタルが漂う速さ。0.fなら静止。

// 火山: 山の位置は半幅に対する割合。Zはプラスにすると中央より奥に配置されます。
inline constexpr float VolcanoXRatio = -.52f;
inline constexpr float VolcanoZRatio = .58f;
inline constexpr float VolcanoHeight = 8.f; // 山頂の高さ。大きいほど高い山になります。
inline constexpr unsigned VolcanoEmberCount = 46; // 空中を上がる火の粉の数。
inline constexpr float VolcanoEmberRiseSpeed = .8f; // 火の粉の上昇速度 [単位/秒]。
inline constexpr float LavaPulseSpeed = 1.3f; // 溶岩が明るく脈打つ速さ。0.fなら明るさ固定。

// 増やしすぎて重くならないための上限。数字を誤入力した場合はビルド時に教えてくれます。
static_assert(TravelDurationSeconds >= 1.f && TravelDurationSeconds <= 12.f);
static_assert(RocketScale >= .5f && RocketScale <= 1.5f);
static_assert(RocketX >= -12.f && RocketX <= 12.f && RocketZ >= -12.f && RocketZ <= 12.f);
static_assert(OceanIslandRadiusRatio >= .3f && OceanIslandRadiusRatio <= .75f);
static_assert(OceanWaveCount <= 64 && OceanWaveSpeed >= 0.f && OceanWaveHeight >= 0.f && OceanWaveHeight <= .2f);
static_assert(ForestTreeCount >= 12 && ForestTreeCount <= 80 && ForestTreeHeight >= 2.f && ForestTreeHeight <= 8.f);
static_assert(ForestFireflyCount <= 64 && ForestFireflySpeed >= 0.f);
static_assert(VolcanoXRatio >= -.7f && VolcanoXRatio <= .7f && VolcanoZRatio >= -.7f && VolcanoZRatio <= .7f);
static_assert(VolcanoHeight >= 3.f && VolcanoHeight <= 12.f);
static_assert(VolcanoEmberCount <= 64 && VolcanoEmberRiseSpeed >= 0.f && LavaPulseSpeed >= 0.f);
}
