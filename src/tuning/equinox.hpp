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
// 「陰→陽→衡→無」の四角い表示面の一辺・高さ。大きくすると文字も拡大。
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
// 床の面・輪・周囲の粒子で共有する回転速度。単位はラジアン/秒。
// 1.1で約5.7秒に1周、0で停止、負の値で反時計回りになります。
// この1か所を変えると、床と粒子が一緒に速く/遅く回ります。
inline constexpr float floorRotation=1.10f;
inline constexpr float floorEvenRotation=floorRotation;
inline constexpr float floorOddRotation=floorRotation;
inline constexpr float floorWaveHeight=.04f;   // 上下の波の幅。0で平らな輪。

// アバターへ集まる薄い霧。例: mistCount=11で半分の量、mistBrightness=.12で半分の明るさ。
inline constexpr unsigned mistCount=22;
inline constexpr float mistTravelSpeed=.16f; // 周期/秒。増やすと中心へ速く集まります。
inline constexpr float mistOuterRadius=4.1f;
inline constexpr float mistBrightness=.24f;

// 二つの玉。例: orbRadius=1で小さく、orbOrbitRadius=3でプレイヤーから離れます。
inline constexpr float orbRadius=1.80f;
inline constexpr float orbOrbitRadius=3.60f;
inline constexpr float orbHeight=2.70f;
inline constexpr float orbBobHeight=.20f; // 上下移動の片側の幅。0で同じ高さを周回。
// 音楽から受け取る「拍数」を共通の時計にします。BGMなしではfallbackBpmで動きます。
// 例: orbitBeats=8で周回を半分の速さに、polarityBeats=2で2拍ごとに白黒交換。
inline constexpr float fallbackBpm=180.f; // 提供BGMの主拍を全曲で計測した既定値。
inline constexpr float orbitBeats=4.0f;
// 1周の時間を保ちながら、周回の途中で少し加速・減速します。
// 0で等速、.22なら平均速度の78〜122%。1未満なので逆走・停止しません。
// orbitSpeedCycleOrbits=1なら1周に1回、2なら2周かけて強弱が1回変化します。
// 玉・トレイル・交換時の爆発位置はすべて同じ計算なので、強弱を変えても離れません。
inline constexpr float orbitSpeedVariation=.22f;
inline constexpr float orbitSpeedCycleOrbits=1.0f;
inline constexpr float polarityBeats=1.0f;
inline constexpr float floorGlyphBeats=4.0f; // 4拍ごとに次の字へ。4文字で16拍のループ。
inline constexpr float orbFaceRotation=-.14f; // 玉の正面の回転、ラジアン/秒。
inline constexpr unsigned orbRimSegments=96; // 輪郭の分割数。通常はそのままで十分。
inline constexpr float whiteHaloBrightness=.20f; // 上げすぎると内部の雷雲が白飛びします。
inline constexpr float blackHaloBrightness=.16f;

// 玉の実際の過去位置を結んだトレイル。波や点は足さず、尾の先ほど細く薄くします。
// trailBeatsを増やすと長い尾、trailWidthを増やすと太い尾になります。
inline constexpr float trailBeats=.80f;
inline constexpr float trailWidth=.20f;
inline constexpr float trailBrightness=.62f;
inline constexpr unsigned trailSegments=40;

// 白黒が入れ替わった瞬間だけ、玉の中心からリングと小さな破片を外へ飛ばします。
// burstBeatsは表示時間（拍）、burstRadiusは広がる距離。量はburstFragmentsで調整。
inline constexpr float burstBeats=.42f;
inline constexpr float burstRadius=1.65f;
inline constexpr float burstBrightness=.82f;
inline constexpr float burstRingWidth=.15f; // 広がる輪の初期の太さ。大きいほど力強く見えます。
inline constexpr float burstFragmentWidth=.070f; // 外へ飛ぶ光の破片の太さ。
inline constexpr unsigned burstRingSegments=48;
inline constexpr unsigned burstFragments=18;

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
inline constexpr float dustRotation=floorRotation; // 床と同じ方向・速度。通常はfloorRotationだけ変更。
inline constexpr float dustRiseSpeed=.22f; // 座標/秒。
inline constexpr float dustBrightness=.40f;

// 0除算や極端な描画量を防ぐ範囲チェック。初めはこの範囲内で値を試してください。
static_assert(floorRingCount<=8 && floorRingSegments>=3 && floorRingSegments<=256);
static_assert(orbRadius>0 && orbOrbitRadius>0 && fallbackBpm>0 && orbitBeats>0);
static_assert(orbitSpeedVariation>=0 && orbitSpeedVariation<1 && orbitSpeedCycleOrbits>0);
static_assert(polarityBeats>0 && floorGlyphBeats>0);
static_assert(orbRimSegments>=3 && orbRimSegments<=256);
static_assert(trailBeats>=0 && trailWidth>0 && trailSegments>=1 && trailSegments<=128);
static_assert(burstBeats>0 && burstBeats<polarityBeats && burstRadius>0);
static_assert(burstRingWidth>0 && burstFragmentWidth>0);
static_assert(burstRingSegments>=3 && burstRingSegments<=128 && burstFragments<=64);
static_assert(mistCount<=128 && dustCount<=512);
} // namespace aura::tuning::equinox
