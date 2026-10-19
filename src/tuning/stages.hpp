#pragma once

// 惑星の見た目を変更する入口。保存 → Develop.cmd で作り直すと反映されます。
// ステージの広さは world.hpp の HalfExtent、惑星選択はゲーム内の Stage Select です。
namespace aura::tuning::stages {

// ロケットの惑星間移動にかかる秒数。3.6f → 5.f で出発/到着の演出がゆっくりになります。
inline constexpr float TravelDurationSeconds = 3.6f;

// 全惑星共通のリスポーン/ロケット発着場 [ワールド単位]。Xは左右、Zは前後。
// 同じ値を参照するため、到着後も毎回ロケットの真下から出発できます。
// RespawnX を 0.f → 2.f にすると、プレイヤーと発着場が両方とも右へ2移動します。
inline constexpr float RespawnX = 0.f;
inline constexpr float RespawnZ = 0.f;
inline constexpr float RocketX = RespawnX;
inline constexpr float RocketZ = RespawnZ;
inline constexpr float RocketScale = .8f; // 機体の大きさ。.8f → 1.f で25%大きくなります。
// 乗降用に機体を持ち上げる高さ。通常の頭上を空け、同じXZでも体が機体に重なりません。
// 3.f → 4.f で機体だけ1高くなります。足元の発着台はそのままです。
inline constexpr float RocketBoardingClearance = 3.f;
// 大きいオーラを発着場で表示するときは機体をさらに上へ退避します。
// 11.f → 13.f で、Illusionaryなどの光や顔にロケットが重なりにくくなります。
inline constexpr float RocketEffectClearance = 11.f;

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
static_assert(RocketBoardingClearance >= 3.f && RocketBoardingClearance <= 8.f);
static_assert(RocketX >= -12.f && RocketX <= 12.f && RocketZ >= -12.f && RocketZ <= 12.f);
static_assert(OceanIslandRadiusRatio >= .3f && OceanIslandRadiusRatio <= .75f);
static_assert(OceanWaveCount <= 64 && OceanWaveSpeed >= 0.f && OceanWaveHeight >= 0.f && OceanWaveHeight <= .2f);
static_assert(ForestTreeCount >= 12 && ForestTreeCount <= 80 && ForestTreeHeight >= 2.f && ForestTreeHeight <= 8.f);
static_assert(ForestFireflyCount <= 64 && ForestFireflySpeed >= 0.f);
static_assert(VolcanoXRatio >= -.7f && VolcanoXRatio <= .7f && VolcanoZRatio >= -.7f && VolcanoZRatio <= .7f);
static_assert(VolcanoHeight >= 3.f && VolcanoHeight <= 12.f);
static_assert(VolcanoEmberCount <= 64 && VolcanoEmberRiseSpeed >= 0.f && LavaPulseSpeed >= 0.f);
}
