#pragma once

// Pixelation の見た目を変える入口。変更後は Develop.cmd で再ビルドします。
// 長さはゲーム内単位（アバターの身長は約 3）、角度はラジアンです。
// まず 1 項目だけ変え、F1 → Pixelation → Lv0〜3 で見比べると分かりやすいです。
namespace aura::tuning::pixelation {
// ── 2 本の時計 ──
// Motion: 周囲の粒子・立方体・PLAYER の回転。Lv3 だけさらに速くします。
// Action: 弾→衝突→爆発・アバター浮遊。Lv2 と Lv3 は必ず同じ速さです。
// 例: FinalMotionSpeed を 3 → 4 にすると周囲だけ高速化し、弾と浮遊は変わりません。
inline constexpr float BaseSpeed=1.f;
inline constexpr float BoostedSpeed=1.6f;        // Lv1〜3 の Action / Lv1〜2 の Motion
inline constexpr float FinalMotionSpeed=3.f;    // Lv3 の Motion のみ

// 色は RGB（赤・緑・青）各 0〜1。例: {1,0,0} は赤、{0,1,1} は青緑。
// キーの位置は 1 周を 0〜1 で表す割合。最初と最後の色を同じにすると滑らかにループします。
struct Rgb { float r,g,b; };
inline constexpr float ColorCycleSpeed=.5f;     // Motion 時計 2 秒で 1 周。大きいほど色替えが速い
inline constexpr float ColorKeyPositions[]={0,.22f,.50f,.70f,.86f,1};
inline constexpr Rgb ColorPalette[]={{.08f,1,.70f},{.63f,1,.05f},{1,.27f,.045f},
    {.84f,.06f,1},{.04f,1,.78f},{.08f,1,.70f}};
inline constexpr unsigned ColorKeyCount=sizeof(ColorKeyPositions)/sizeof(ColorKeyPositions[0]);

// ── 打ち合い ── 時間は Action 時計の秒。実時間は速度倍率で短くなります。
inline constexpr float VolleyPeriod=1.6f;       // 小さくすると発射間隔が短くなる
inline constexpr float VolleyFlight=.64f;       // 小さくすると玉が早く中央に着く
inline constexpr float MeetingHeight=3.60f;     // 衝突・爆発の高さ。両方の弾が必ずここへ着く
inline constexpr float ProjectileArc=.35f;      // 大きくすると弾道の上向きカーブが強くなる
inline constexpr float ProjectileSize=.30f;
inline constexpr float ProjectileCoreSize=.18f;
inline constexpr unsigned ProjectileTrailCount=6; // 1〜6。増やすと玉の残像が増える
inline constexpr float ProjectileTrailStep=.025f; // 残像同士の時間差。大きいほど尾が長い

// PLAYER の周回。double は既存の計算精度を保つためです。
inline constexpr double PlayerOrbitSpeed=.95;   // Motion 時計 1 秒あたりの回転角
inline constexpr double PlayerOrbitPhase=.3;
inline constexpr double PlayerOrbitRadius=2.45; // 大きくすると PLAYER が外側を回る
inline constexpr double PlayerHeight=2.5;
inline constexpr double PlayerBobAmplitude=.36;
inline constexpr double PlayerBobFrequency=1.4;
inline constexpr float PlayerScreenWidth=1.35f;
inline constexpr float PlayerScreenHeight=.88f;
inline constexpr unsigned PlayerOrbitSegments=64; // 大きくすると画面の輪郭がなめらか（描画量も増える）
inline constexpr unsigned TetherSegments=16;     // PLAYER とアバター間のギザギザ線の分割数

// ── 衝突の爆発 ── Lv3 は規模だけ拡大。経過時間は Lv2 と共有します。
inline constexpr float ImpactDuration=.65f;     // 長くすると爆発が残る時間が延びる
inline constexpr float FinalImpactScale=1.30f;  // Lv3 の爆発全体の倍率
inline constexpr float ImpactStartRadius=.08f;
inline constexpr float ImpactExpansion=1.70f;   // 大きくすると破片・衝撃波が遠くまで飛ぶ
inline constexpr unsigned ImpactShardCount=36;  // 多いほど破片が増える（描画量も増える）
inline constexpr unsigned ImpactWaveCount=2;   // 1〜2。衝撃波の輪の枚数

// ── 床・回転する立方体 ──
inline constexpr unsigned FloorRingSegments=96;
inline constexpr float OuterRingRadius=2.25f;
inline constexpr float MiddleRingRadius=1.93f;
inline constexpr float UpperRingRadius=1.66f;
inline constexpr float FloorGlowSize=7.f;
inline constexpr unsigned FloorPillarCount=64;
inline constexpr float FloorPillarRadius=2.15f;
inline constexpr unsigned CageCount=3;
inline constexpr float CageSize=2.6f;           // 大きくするとアバターを囲む立方体が大きくなる
inline constexpr float CageSizeStep=.14f;
inline constexpr float CageHeight=2.55f;        // 立方体の中心の高さ

// ── 周囲の粒子・名前 ──
inline constexpr unsigned FragmentCount=180;   // 増やすと周囲の四角い粒子が増える
inline constexpr float FragmentInnerRadius=1.65f;
inline constexpr float FragmentRadiusSpread=1.9f;
inline constexpr float FragmentRiseRange=5.3f;  // 大きくすると粒子がより上まで昇る
inline constexpr float FragmentRiseSpeed=.30f;
inline constexpr float FragmentMinSize=.045f;
inline constexpr float FragmentSizeVariation=.065f;
inline constexpr unsigned ScanStreakCount=18;
inline constexpr float NameHeight=.90f;
inline constexpr float NameDepth=-.20f;         // 名前を置く前後位置（アバターからの Z 差分）
inline constexpr float NamePixelSize=.045f;     // 大きくすると PIXELATION の文字が大きくなる

static_assert(BaseSpeed>0 && BoostedSpeed>0 && FinalMotionSpeed>0);
static_assert(ColorCycleSpeed>0 && ColorKeyCount>=2);
static_assert(ColorKeyCount==sizeof(ColorPalette)/sizeof(ColorPalette[0]));
static_assert([] { for(unsigned i=1;i<ColorKeyCount;++i) if(ColorKeyPositions[i]<=ColorKeyPositions[i-1]) return false;
    return ColorKeyPositions[0]==0 && ColorKeyPositions[ColorKeyCount-1]==1; }(),"色のキーは 0〜1 の昇順で指定してください");
// 0 除算や負の残像サイズを避けるチェック。失敗したら、この範囲に値を戻してください。
static_assert(VolleyFlight>0 && VolleyPeriod>VolleyFlight,"VolleyPeriod は VolleyFlight より長くしてください");
static_assert(ProjectileTrailCount>0 && ProjectileTrailCount<=6 && ProjectileTrailStep>0,"玉の残像は 1〜6 個、時間差は正の値です");
static_assert(ImpactDuration>0 && FinalImpactScale>0);
static_assert(ImpactWaveCount>0 && ImpactWaveCount<=2,"衝撃波は 1〜2 枚です");
static_assert(FloorRingSegments>=3 && PlayerOrbitSegments>=3 && TetherSegments>0,"円は 3 分割以上、線は 1 分割以上必要です");
static_assert(FloorPillarCount>0 && CageCount>0 && FragmentCount>0 && ImpactShardCount>0);
static_assert(FragmentRiseRange>0 && CageSize>0 && PlayerOrbitRadius>0);
} // namespace aura::tuning::pixelation
