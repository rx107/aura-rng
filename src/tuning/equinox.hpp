#pragma once
#include "core/aura.hpp"

// Equinoxの見た目を変える入口です。値を保存したらDevelop.cmdで再ビルドし、
// F1 → オーラ → Equinoxを表示、で確認できます。確率や所持数には影響しません。
// 単位: 長さはゲーム内座標（ブロックアバターの身長は約3）、時間は秒、角度はラジアン。
namespace aura::tuning::equinox {

// 周囲の光と粒子の色はRGB。1が標準の白、1超で強く発光（例: rを上げると赤みが増す）。
// 玉の内部そのものの白/黒はrender/equinox.cppのHLSLで作っています。
inline constexpr Color silver{.83f,.88f,.98f};
inline constexpr Color white{1.1f,1.1f,1.1f};

// 足元の面の直径・高さ。例: floorDiameter=9で外側の面が広がります。
inline constexpr float floorDiameter=8.2f;
inline constexpr float floorHeight=.19f;
// 「衡」の四角い表示面の一辺・高さ。大きくすると文字も拡大。例: floorGlyphSize=2.8で小さく。
// floorGlyphHeightは床の面より少し上に保つと、面同士のちらつきを防げます。
inline constexpr float floorGlyphSize=3.2f;
inline constexpr float floorGlyphHeight=.240f;

// 足元の光る輪。例: floorInnerRadius=2.5で一番内側の輪が外へ広がります。
inline constexpr unsigned floorRingCount=3;
inline constexpr unsigned floorRingSegments=128; // 円の分割数。増やすと滑らか、描画量も増えます。
inline constexpr float floorInnerRadius=2.1f;
inline constexpr float floorRingSpacing=.63f;
inline constexpr float floorRingHeight=.28f;
inline constexpr float floorRingHeightStep=.07f;
inline constexpr float floorEvenRotation=.32f; // ラジアン/秒。負にすると逆回転。
inline constexpr float floorOddRotation=-.50f;
inline constexpr float floorWaveHeight=.04f;   // 上下の波の幅。0で平らな輪。

// アバターへ集まる薄い霧。例: mistCount=11で半分の量、mistBrightness=.12で半分の明るさ。
inline constexpr unsigned mistCount=22;
inline constexpr float mistTravelSpeed=.16f; // 周期/秒。増やすと中心へ速く集まります。
inline constexpr float mistOuterRadius=4.1f;
inline constexpr float mistBrightness=.24f;

// 二つの玉。例: orbRadius=1で小さく、orbOrbitRadius=3でプレイヤーから離れます。
inline constexpr float orbRadius=1.35f;
inline constexpr float orbOrbitRadius=2.25f;
inline constexpr float orbHeight=3.1f;
inline constexpr float orbBobHeight=.20f; // 上下移動の片側の幅。0で同じ高さを周回。
inline constexpr float orbOrbitSeconds=1.565f; // 一周の秒数。増やすと遅くなり、文字の白黒周期も連動。
inline constexpr float orbFaceRotation=-.14f; // 玉の正面の回転、ラジアン/秒。
inline constexpr unsigned orbRimSegments=96; // 輪郭の分割数。通常はそのままで十分。
inline constexpr float whiteHaloBrightness=.20f; // 上げすぎると内部の雷雲が白飛びします。
inline constexpr float blackHaloBrightness=.16f;

// 玉の中心が残す独立した軌跡。例: wakeSeconds=.45で長く、wakeWidth=.08で細く。
inline constexpr float wakeSeconds=.30f; // 過去何秒分の軌道を描くか。
inline constexpr float wakeWidth=.12f;
inline constexpr float wakeBrightness=.68f;
inline constexpr float wakeVerticalWave=.24f; // 上下のなみなみの幅。0で周回軌道に沿うだけ。
inline constexpr float wakeRadialWave=.10f;   // 内外のなみなみの幅。
inline constexpr unsigned wakeSegments=32;
inline constexpr unsigned wakeDotCount=6; // 小さな光点の数。星を置く位置の都合で6以上を推奨。

// 名前と副題の表示面。高さを増やすと上へ、幅/高さを増やすと文字が横/縦に大きくなります。
// 例: titleHeight=5.5で名前を少し上へ移動。
inline constexpr float titleHeight=5.15f;
inline constexpr float titleWidth=4.0f;
inline constexpr float titleTextHeight=.54f;
inline constexpr float subtitleHeight=5.52f;
inline constexpr float subtitleWidth=5.0f;
inline constexpr float subtitleTextHeight=.25f;

// 周囲を漂う粒子。例: dustCount=70で約半分、dustRiseSpeed=.44で上昇を2倍速く。
inline constexpr unsigned dustCount=140;
inline constexpr float dustRotation=.15f; // ラジアン/秒。
inline constexpr float dustRiseSpeed=.22f; // 座標/秒。
inline constexpr float dustBrightness=.40f;

// 0除算や極端な描画量を防ぐ範囲チェック。初めはこの範囲内で値を試してください。
static_assert(floorRingCount<=8 && floorRingSegments>=3 && floorRingSegments<=256);
static_assert(orbRadius>0 && orbOrbitRadius>0 && orbOrbitSeconds>0);
static_assert(orbRimSegments>=3 && orbRimSegments<=256);
static_assert(wakeSeconds>=0 && wakeWidth>0 && wakeSegments>=1 && wakeSegments<=128);
static_assert(wakeDotCount>=1 && wakeDotCount<=24 && mistCount<=128 && dustCount<=512);
} // namespace aura::tuning::equinox
