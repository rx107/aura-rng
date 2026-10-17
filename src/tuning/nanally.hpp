#pragma once
#include <array>

// Nanally の見た目を変える入口です。変更後はビルドし直してください。
// まず 1 個だけ変更 → F1 のオーラ一覧で Nanally を表示 → 結果を確認、がおすすめです。
// 座標の単位はゲーム内の長さ。ブロックアバターの身長は約 3 です。
// SideOffset はプレイヤーから左右への距離、BehindDistance は背後への距離です。
// Width/Height は描画用の板全体の大きさなので、透明な余白も少し含みます。
namespace aura::tuning::nanally {

// 【頭と耳】例: HeadWidth を 6.35f → 7.0f にすると、頭だけ横に広がります。
inline constexpr float HeadWidth = 6.35f;
inline constexpr float HeadHeight = 5.85f;
inline constexpr float HeadCenterHeight = 3.52f; // 大きくすると頭が上へ移動。
inline constexpr float HeadBehindDistance = 2.85f; // 大きくするとプレイヤーから後ろへ離れる。
inline constexpr float EarSideOffset = 2.35f;
inline constexpr float EarCenterHeight = 6.0f;
inline constexpr float EarBehindDistance = 2.87f;
inline constexpr float EarWidth = 1.55f;
inline constexpr float EarHeight = 2.0f;
inline constexpr float EarTiltRadians = .24f; // ラジアン。大きくすると左右の耳が外側に傾く。

// 【拳】例: FistSideOffset を 3.45f → 4.0f にすると、両手が左右へ開きます。
inline constexpr float FistSideOffset = 3.45f;
inline constexpr float FistCenterHeight = 1.63f;
inline constexpr float FistBehindDistance = 1.95f;
inline constexpr float FistWidth = 2.10f;
inline constexpr float FistHeight = 2.05f;
inline constexpr float FistTiltRadians = .21f;

// 【浮遊】振幅は上下移動の片道分、速度は 1 秒あたりの位相の進みです。
// 例: HoverAmplitude を .11f → .22f で上下の揺れが 2 倍。0 なら停止します。
inline constexpr float HoverAmplitude = .11f; // 頭・耳・拳に共通する揺れ。
inline constexpr float HoverSpeed = 1.45f; // 大きくすると揺れの周期が短くなる。
inline constexpr float FistHoverAmplitude = .14f; // 拳だけに追加する揺れ。
inline constexpr float FistHoverSpeed = 1.6f;

// 【赤い炎】頭と拳の外周を包む炎の大きさです。
// 頭自体を大きくした場合は、この炎も少し大きくすると輪郭に合います。
inline constexpr float HeadFlameWidth = 8.15f;
inline constexpr float HeadFlameHeight = 7.75f;
inline constexpr float FistFlameWidth = 3.0f;
inline constexpr float FistFlameHeight = 3.25f;
inline constexpr float FlameSizeMultiplier = 1.0f; // 炎の大きさ全体の倍率。1.1f なら 10% 拡大。
inline constexpr float FlameOpacityMultiplier = 1.0f; // 外周・立ち上る炎の濃さ。0 なら炎だけ消える。
inline constexpr unsigned FlameTongueCount = 26; // 細い炎の本数。増やすと密度と描画負荷が増える。
inline constexpr unsigned EmberCount = 52; // 浮き上がる火の粉の数。0 なら火の粉なし。
inline constexpr float FlameTongueSpeed = .57f; // 大きくすると細い炎の流れが速くなる。
inline constexpr float EmberRiseSpeed = .21f; // 大きくすると火の粉の上昇が速くなる。

// 【プレイヤー周囲の小さな電撃】Count は発生候補の数です。
// 常時すべてが光るのではなく、各候補が短時間だけ点滅します。
inline constexpr unsigned ElectricBoltCount = 18; // 0 なら電撃なし。36 なら候補数が 2 倍。
inline constexpr unsigned ElectricBoltPointCount = 6; // 1 本を折れ線で表す点の数。増やすと折れ曲がりが増える。
inline constexpr float ElectricLengthMin = .29f; // 電撃の最短の長さ。
inline constexpr float ElectricLengthVariation = .32f; // 最短値に 0～この値の長さが追加される。
inline constexpr float ElectricWidth = .12f; // 大きくすると線が太くなる。
inline constexpr float ElectricZigzagWidth = .075f; // 大きくすると左右へのギザギザが広がる。
inline constexpr float ElectricRadiusMin = .64f; // プレイヤーの中心から電撃までの最小距離。
inline constexpr float ElectricRadiusVariation = .24f;
inline constexpr float ElectricSpeedMin = 2.3f; // 大きくすると点滅する頻度が増える。
inline constexpr float ElectricSpeedVariation = 1.7f;
inline constexpr float ElectricVisibleFraction = .30f; // 1 周期のうち光る割合。0.5f なら半分の時間光る。

// 【色】順番は {赤, 緑, 青}。0 はその色なし、1 を超えると強い発光になります。
// 例: ElectricColor を {1.55f, .025f, .025f} にすると青緑の電撃が赤くなります。
// FlameTongueColor は細い炎、CuffAndEmberColor は手首の輪と火の粉に使います。
// 顔・拳の表面や瞳の色は、上級向けの nanally.cpp 内のシェーダーで計算しています。
inline constexpr std::array<float, 3> FlameTongueColor{1.9f, .018f, .095f};
inline constexpr std::array<float, 3> CuffAndEmberColor{1.7f, .11f, .20f};
inline constexpr std::array<float, 3> ElectricColor{.025f, 1.55f, 1.24f};

// 入力ミスによるゼロ除算や極端な描画負荷を、ビルド時に見つけます。
static_assert(HeadWidth > 0 && HeadHeight > 0 && EarWidth > 0 && EarHeight > 0);
static_assert(FistWidth > 0 && FistHeight > 0);
static_assert(HeadFlameWidth > 0 && HeadFlameHeight > 0 && FistFlameWidth > 0 && FistFlameHeight > 0);
static_assert(HoverAmplitude >= 0 && HoverSpeed >= 0 && FistHoverAmplitude >= 0 && FistHoverSpeed >= 0);
static_assert(FlameSizeMultiplier > 0 && FlameOpacityMultiplier >= 0);
static_assert(FlameTongueCount >= 1 && FlameTongueCount <= 200 && EmberCount <= 400);
static_assert(FlameTongueSpeed >= 0 && EmberRiseSpeed >= 0);
static_assert(ElectricBoltCount <= 200 && ElectricBoltPointCount >= 2 && ElectricBoltPointCount <= 32);
static_assert(ElectricLengthMin > 0 && ElectricLengthVariation >= 0 && ElectricWidth > 0 && ElectricZigzagWidth >= 0);
static_assert(ElectricRadiusMin >= 0 && ElectricRadiusVariation >= 0);
static_assert(ElectricSpeedMin >= 0 && ElectricSpeedVariation >= 0);
static_assert(ElectricVisibleFraction > 0 && ElectricVisibleFraction <= 1);

} // namespace aura::tuning::nanally
