#pragma once
#include <array>

// 砂漠をテーマにした 3 種類のオーラの調整用ファイルです。
// 変更 → 保存 → Develop.cmd → F1 のオーラ一覧で表示、の順に確認できます。
// 長さの単位はゲーム内の長さです。ブロックアバターの身長は約 3 です。
// Speed は大きくすると速く、Count / Segments は増やすと細かくなり描画負荷も増えます。
namespace aura::tuning::desert {

// 【共通の色】{赤, 緑, 青}。1 を超える成分は、発光が強くなります。
// 例: SandColor の 1.2f を .6f にすると、砂の赤みが弱まります。
inline constexpr std::array<float, 3> SandColor{1.2f, .65f, .20f};
inline constexpr std::array<float, 3> PaleSandColor{1.1f, .86f, .46f};
inline constexpr std::array<float, 3> TurquoiseColor{.10f, .94f, 1.15f};
inline constexpr std::array<float, 3> GoldColor{1.3f, .82f, .16f};

// 【Dune / 砂丘】足元の砂の輪と、周囲を上昇する砂のらせんです。
// 例: DuneRadius を 1.75f → 2.2f にすると、砂が回る範囲が外側へ広がります。
inline constexpr float DuneRadius = 1.75f;
inline constexpr float DuneHeight = 2.75f; // らせんの一番上の高さ。
inline constexpr float DuneSpeed = .65f; // らせんが回る速度。
inline constexpr float DuneLineWidth = .10f; // 大きくすると砂の帯が太くなります。
inline constexpr unsigned DuneSpiralSegments = 32; // らせん 1 本を何本の短い線に分けるか。
inline constexpr unsigned DuneGrainCount = 48; // 上に流れる細かい砂の粒の数。
inline constexpr float DuneGrainSize = .075f;
inline constexpr float DuneGrainRiseSpeed = .18f; // 1 秒で上昇周期が進む割合。

// 【Mirage / 蜃気楼】青緑の揺らぐ光の弧と、小さな結晶です。
// 例: MirageWaveHeight を .16f → .30f にすると、光の揺れが大きくなります。
inline constexpr float MirageRadius = 1.95f;
inline constexpr float MirageSpeed = .45f;
inline constexpr float MirageWaveHeight = .16f;
inline constexpr unsigned MirageArcSegments = 22;
inline constexpr unsigned MirageCrystalCount = 12;
inline constexpr float MirageCrystalSize = .19f;
inline constexpr float MirageCrystalBobHeight = .13f; // 結晶が上下する片道分の距離。
inline constexpr unsigned MirageMoteCount = 20; // 空中の小さな光の数。

// 【Pharaoh / ファラオ】金色のピラミッド、左右の翼、背後の太陽です。
// 例: PharaohWingWidth を 2.3f → 3.0f にすると、翼が左右に広がります。
inline constexpr float PharaohPyramidRadius = 1.95f;
inline constexpr float PharaohPyramidHeight = 3.65f;
inline constexpr float PharaohPyramidSpeed = .13f;
inline constexpr float PharaohWingWidth = 2.3f;
inline constexpr unsigned PharaohFeatherCount = 12; // 片側の羽の数。
inline constexpr float PharaohSunHeight = 3.70f;
inline constexpr float PharaohSunRadius = .56f;
inline constexpr unsigned PharaohSunSegments = 36;
inline constexpr unsigned PharaohGlintCount = 28;
inline constexpr float PharaohHoverHeight = .07f; // 太陽と翼の上下の揺れ幅。0 なら停止。
inline constexpr float PharaohHoverSpeed = 1.35f;

// 足元の輪を何本の短い線でつなぐか。大きいほど円がなめらかになります。
inline constexpr unsigned FloorRingSegments = 32;

// 入力ミスや極端な描画負荷を、ビルド時に見つけます。
static_assert(DuneRadius > 0 && DuneHeight > 0 && DuneLineWidth > 0 && DuneGrainSize > 0);
static_assert(DuneSpeed >= 0 && DuneGrainRiseSpeed >= 0);
static_assert(DuneSpiralSegments >= 8 && DuneSpiralSegments <= 80 && DuneGrainCount <= 160);
static_assert(MirageRadius > 0 && MirageSpeed >= 0 && MirageWaveHeight >= 0);
static_assert(MirageArcSegments >= 8 && MirageArcSegments <= 60);
static_assert(MirageCrystalCount >= 3 && MirageCrystalCount <= 40 && MirageCrystalSize > 0);
static_assert(MirageCrystalBobHeight >= 0 && MirageMoteCount <= 120);
static_assert(PharaohPyramidRadius > 0 && PharaohPyramidHeight > 0 && PharaohPyramidSpeed >= 0);
static_assert(PharaohWingWidth > 0 && PharaohFeatherCount >= 3 && PharaohFeatherCount <= 40);
static_assert(PharaohSunHeight > 0 && PharaohSunRadius > 0);
static_assert(PharaohSunSegments >= 8 && PharaohSunSegments <= 100 && PharaohGlintCount <= 120);
static_assert(PharaohHoverHeight >= 0 && PharaohHoverSpeed >= 0);
static_assert(FloorRingSegments >= 8 && FloorRingSegments <= 96);

} // namespace aura::tuning::desert
