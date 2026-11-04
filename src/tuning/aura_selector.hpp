#pragma once

// 炎ボタンから開くオーラ選択画面の見た目。長さは1440×900基準の論理ピクセルです。
namespace aura::tuning::aura_selector {
inline constexpr float PanelOpacity=.70f; // 1で背景が見えない。0に近づけるとワールドが見えやすい。
inline constexpr float PanelGray=.35f;
inline constexpr float PanelWidthRatio=.44f; // 画面幅に対する右パネルの幅。
inline constexpr float PanelTop=94.f;
inline constexpr float PanelBottomGap=92.f; // 下の炎ボタンとの間に余白を残す。
inline constexpr float RowPitch=70.f; // 行を大きくすると、一度に見えるオーラの数が減る。
inline constexpr float RowGap=8.f;
inline constexpr float DetailHeight=198.f;
inline constexpr float DetailBottomGap=118.f;
}
