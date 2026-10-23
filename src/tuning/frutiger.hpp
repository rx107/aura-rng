#pragma once
#include "core/aura.hpp"

// Frutiger Aeroの見た目を自由に変えるための入口です。
// 保存 → Develop.cmdで再ビルド → F1のオーラからFrutiger Aeroを表示、で確認します。
// 長さはゲーム内座標（ブロックアバターの身長は約3）、速さは1秒あたりです。
// 画像ファイルは不要です。草原、空、反射、蝶の模様はGPU上の数式で描きます。
namespace aura::tuning::frutiger {

// 足元の丸い芝生。半径を大きくすると草原と小川が一緒に広がります。
inline constexpr float MeadowRadius=4.7f;
inline constexpr float MeadowHeight=.13f; // 地面より少し上。0にするとステージとちらつきます。
inline constexpr Color GrassColor{.22f,.64f,.035f};
inline constexpr unsigned GrassBladeCount=128; // 量を半分にしたい場合は64。0でも床は残ります。
inline constexpr float GrassBladeHeight=.25f;
inline constexpr float GrassSwaySpeed=1.35f; // 0で風による揺れを止めます。

// アバターの後ろに浮かぶ「青空を閉じ込めたガラスの景色」。
// カメラを回しても背面へ回り、プレイヤーの姿を隠さない配置になります。
inline constexpr float SkyWidth=11.8f;
inline constexpr float SkyHeight=9.6f;
inline constexpr float SkyCenterHeight=4.15f;
inline constexpr float SkyDistance=4.0f; // 大きくすると空がプレイヤーから遠ざかります。
inline constexpr float SkyOpacity=.94f;
inline constexpr float CloudSpeed=.025f; // 雲が横へ流れる速さ。0で静止。

// 透明なシャボン玉。中心を薄く、輪郭と白い映り込みを強くしてガラス感を作ります。
inline constexpr unsigned BubbleCount=22;
inline constexpr float BubbleMinSize=.36f;
inline constexpr float BubbleMaxSize=1.75f;
inline constexpr float BubbleRadius=4.7f; // 玉の飛ぶ範囲。MeadowRadius前後が目安。
inline constexpr float BubbleHeight=6.7f;
inline constexpr float BubbleRiseSpeed=.12f; // 周期/秒。例:.06でゆっくり半分の速さ。
inline constexpr float BubbleOpacity=.94f;

// 青い蝶。一匹につき羽4枚（表裏）と体2枚を描く小さな立体です。
inline constexpr unsigned ButterflyCount=7;
inline constexpr float ButterflySize=.62f;
inline constexpr float ButterflyRadius=3.2f;
inline constexpr float ButterflyOrbitSpeed=.24f; // 周回の角度/秒。負なら逆回転。
inline constexpr float ButterflyFlapSpeed=12.f; // 羽ばたきの角速度。0で羽が止まります。
inline constexpr Color ButterflyColor{.025f,.38f,1.f};

// 水滴のような小さい輝き。数を増やすと華やかになりますが、光らせすぎない量が初期値です。
inline constexpr unsigned SparkleCount=14;
inline constexpr float SparkleSize=.16f;

// 一度の描画で使える板は4096枚。ここは約350枚以内に収めるための安全確認です。
static_assert(MeadowRadius>0 && SkyWidth>0 && SkyHeight>0 && SkyDistance>0);
static_assert(GrassBladeCount<=512 && BubbleCount<=128 && ButterflyCount<=24 && SparkleCount<=64);
static_assert(BubbleMinSize>0 && BubbleMaxSize>=BubbleMinSize && BubbleRiseSpeed>=0);
} // namespace aura::tuning::frutiger
