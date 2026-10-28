#pragma once

// 左上のロゴを調整する入口です。位置と大きさは1440×900基準の画面ピクセル。
// 添付PNGは加工せず data/ui に置き、読み込み時だけ透明な余白を除いて描きます。
// 変更 → Develop.cmd → ゲーム画面の左上、の順で見比べてください。
namespace aura::tuning::branding {
inline constexpr float IconLeft=12.f,IconTop=12.f,IconWidth=64.f; // 星の全体。幅を増やすと輪ごと大きくなる。
inline constexpr float TitleLeft=84.f,TitleTop=17.f,TitleWidth=190.f; // タイトル。縦幅は元画像の比率から自動計算。
inline constexpr float RocketWidth=13.f; // 周回するロケットだけの大きさ。星や文字には影響しない。
inline constexpr float OrbitSeconds=5.f; // 1周の秒数。10ならゆっくり、2.5なら現在の2倍の速さ。
inline constexpr float OrbitPhase=.35f; // 起動時にいる場所（ラジアン）。0〜約6.283で1周分。
inline constexpr float RocketAngleOffset=0.f; // 機首の追加角度（度）。別画像の機首が上なら+90などで調整。

// 以下は、元の星PNG（1280×1280）の白い輪から測った楕円です。
// Centerは輪の中心、Major/MinorRadiusは長い半径/短い半径、Tiltは画像上の傾き（度）。
// 星の画像を別の輪へ差し替えたときだけ変更します。IconWidthによる拡大縮小には自動追従。
inline constexpr float RingCenterX=642.911f,RingCenterY=646.999f;
inline constexpr float RingMajorRadius=599.042f,RingMinorRadius=149.134f;
inline constexpr float RingTiltDegrees=-25.622682f;

static_assert(IconWidth>0 && TitleWidth>0 && RocketWidth>0,"Brand image widths must be positive");
static_assert(OrbitSeconds>0,"OrbitSeconds is the positive time for one rocket orbit");
static_assert(RingMajorRadius>RingMinorRadius && RingMinorRadius>0,"The orbit needs two positive ellipse radii");
} // namespace aura::tuning::branding
