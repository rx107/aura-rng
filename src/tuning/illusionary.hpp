#pragma once
#include <array>

// Illusionary の見た目を変える入口です。変更したら Develop.cmd で再ビルドします。
// 長さはゲーム内単位（ブロックアバターの身長は約 3）。角度はラジアンです。
// まず Ribbons の先頭の半径を 3.35f → 3.65f と変え、F1 のプレビューで見比べましょう。
namespace aura::tuning::illusionary {
// RGB は赤・緑・青の強さです。1 を超える色は発光処理で強く光ります。
inline constexpr std::array<float,3> Blue={.018f,.018f,1.8f};
inline constexpr std::array<float,3> White={1.10f,1.14f,1.32f};
inline constexpr std::array<float,3> Ink={.001f,.001f,.005f}; // 黒い帯。青を増やすと紺色になる
inline constexpr float MotionSpeed=1.f;                  // 全体の動く速さ。2 にすると 2 倍速

// ── 頭の周りの黒いリボン ──
// radius=円の半径、height=中心の高さ、width=帯の幅。
// tiltX/tiltZ=傾き、spin=1 秒の回転角、phase=最初の向きです。
// 帯がアバターを隠しすぎる場合は height を上げるか width を小さくします。
struct Ribbon { float radius,height,width,tiltX,tiltZ,spin,phase; };
inline constexpr std::array<Ribbon,5> Ribbons={{
    {3.35f,4.88f,.19f,.06f,.03f,.12f,0},
    {2.48f,4.35f,.18f,.29f,-.24f,-.27f,.4f},
    {2.23f,4.02f,.16f,-.40f,.28f,.32f,1.9f},
    {1.97f,3.78f,.13f,.35f,.30f,-.37f,.7f},
    {1.84f,4.23f,.13f,.88f,-.32f,.22f,2.2f}
}};
inline constexpr unsigned RibbonSegments=64;           // 円の滑らかさ。増やすと描画量も増える
inline constexpr float RibbonEdgeWidth=.033f;          // 青い縁の太さ
inline constexpr float RibbonEdgeBrightness=.47f;      // 青い縁の明るさ。帯本体の黒は変わらない
inline constexpr float HoverAmplitude=.075f;           // 帯全体の上下幅。0 で上下しなくなる
inline constexpr float HoverSpeed=1.1f;

// ── 青と白の放射線 ──
inline constexpr float BurstHeight=4.95f;
inline constexpr unsigned RayCount=48;                 // 白・青の細い放射線の数
inline constexpr float RayInnerRadius=.52f;
inline constexpr float RayOuterRadius=4.75f;           // 大きくすると線が遠くへ伸びる
inline constexpr float RayWidth=.024f;
inline constexpr unsigned ShardCount=22;               // 上向きの短冊・破片の数
inline constexpr float ShardHeight=5.22f;
inline constexpr float ShardSpread=2.12f;
inline constexpr float ShardLength=1.92f;
inline constexpr unsigned SparkCount=9;                // 周囲の白・青の 8 方向の星
inline constexpr float SparkSize=.23f;
inline constexpr unsigned ParticleCount=48;

// ── 上の市松模様と足元のブロック ──
inline constexpr float CrownHeight=6.10f;              // 上の青黒チェックの高さ
inline constexpr float CrownWidth=2.60f;
inline constexpr unsigned CrownCells=12;              // 2 色を交互にするため偶数がおすすめ
inline constexpr float FootRadius=1.53f;               // 足元のチェックを囲む半径
inline constexpr float FootCellHeight=.30f;
inline constexpr unsigned FootColumns=16;              // 足元を一周するタイルの列数
inline constexpr unsigned FootRows=3;
// 外周の壁だけでなく、上面をこの市松模様で塞ぎます。上から見ても地面が透けません。
inline constexpr float FloorCellSize=.51f;             // 小さくするとチェックの一マスが細かくなる
inline constexpr unsigned FloorStrips=48;             // 円周を滑らかに切り抜く細い横列の数
inline constexpr float FloorSelfLight=.95f;            // 床だけの明るさ。発光のぼかしはかからない
inline constexpr std::array<float,3> FloorBlue={.025f,.065f,.94f};
inline constexpr std::array<float,3> FloorWhite={.83f,.88f,1.f};

// ── プレイヤーを回る 2 つの白い光 ──
inline constexpr float OrbitLightRadius=1.92f;         // 大きくすると体から離れた所を周回する
inline constexpr float OrbitLightHeight=2.30f;
inline constexpr float OrbitLightSpeed=1.05f;          // ラジアン/秒。2 倍にすると周回が 2 倍速
inline constexpr float OrbitLightBob=.28f;             // 周回しながら上下する幅
inline constexpr float OrbitLightWidth=1.10f;
inline constexpr float OrbitLightHeightSize=.48f;
inline constexpr float OrbitLightBrightness=.76f;
inline constexpr unsigned OrbitTrailSegments=14;
inline constexpr float OrbitTrailAngle=.76f;           // 大きくすると後ろに残る白い軌跡が長くなる

// ── 浮いている小さな画面と文字 ──
inline constexpr unsigned WindowCount=7;
inline constexpr float WindowRadius=3.38f;
inline constexpr float TitleHeight=6.20f;
inline constexpr float TitleSideOffset=-2.82f;          // 負=画面左、正=画面右
inline constexpr float TitlePixelSize=.043f;           // 大きくすると ILLUSIONARY が大きくなる
inline constexpr float CaptionPixelSize=.020f;
// 文字は発光バッチへ入れず、白い画素として描くので輪郭がぼけません。黒い下地も不要です。
inline constexpr float TextSelfLight=1.22f;             // 上げると文字だけが明るくなる。周囲の光は増えない
inline constexpr float TextPixelFill=.94f;             // 1 で画素がつながる。少し小さくすると隙間ができる
inline constexpr std::array<float,3> TextColor={.82f,.87f,1.f};

// 参考の短い白黒フラッシュ。Period の秒数ごとに At の時刻で一瞬白くなります。
// Strength を 0 にすると青のまま、1 にすると瞬間的にほぼ白になります。
inline constexpr float FlashPeriod=4.65f;
inline constexpr float FlashAt=2.15f;
inline constexpr float FlashDuration=.13f;
inline constexpr float FlashStrength=.85f;

// 不正な値で円・時間の計算が壊れないようにする安全柵です。
static_assert(MotionSpeed>0 && RibbonSegments>=24 && RibbonSegments<=72);
static_assert([] { for(const auto& r:Ribbons) if(r.radius<=0 || r.width<=0 || r.width>=r.radius) return false; return true; }());
static_assert(RayCount>=8 && RayCount<=56 && ShardCount<=24 && SparkCount<=10 && ParticleCount<=64);
static_assert(FootColumns>=8 && FootColumns<=20 && FootRows>=1 && FootRows<=3 && FootRadius>0);
static_assert(FloorCellSize>=.3f && FloorStrips>=24 && FloorStrips<=64 && FloorSelfLight>=0);
static_assert(OrbitLightRadius>0 && OrbitLightSpeed>0 && OrbitTrailSegments>=4 && OrbitTrailSegments<=20);
static_assert(TextSelfLight>=0 && TextPixelFill>0 && TextPixelFill<=1);
static_assert(WindowCount<=7 && CrownCells>=2 && CrownCells<=16);
static_assert(FlashPeriod>0 && FlashDuration>0 && FlashDuration<FlashPeriod && FlashStrength>=0 && FlashStrength<=1);
static_assert(TitlePixelSize>0 && CaptionPixelSize>0 && RibbonEdgeWidth>0);
} // namespace aura::tuning::illusionary
