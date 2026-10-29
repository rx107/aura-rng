#pragma once

// 全面ゲーム画面に重ねるメニューの調整値です。単位は1440×900基準の画面ピクセル。
// 例: PanelOpacity=.68f → .50f でメニュー越しのステージがもっと見えます。
namespace aura::tuning::shell {
inline constexpr float EdgeMargin=24.f;
inline constexpr float LeftMargin=20.f,MenuTop=96.f,LeftWidth=240.f;
inline constexpr float LeftRowHeight=62.f,MenuPadding=16.f;
inline constexpr float RightWidth=288.f,RightRowHeight=58.f,RightBottomGap=92.f;
inline constexpr float HamburgerSize=52.f;
inline constexpr float RollWidth=210.f,RollHeight=54.f,AutoWidth=134.f,ButtonGap=14.f;
inline constexpr float RollBottomMargin=42.f;
inline constexpr float PanelOpacity=.68f; // 1で後ろが見えない。0で背景が完全に透明。
inline constexpr float HoverOpacity=.12f; // マウスを乗せた行だけに重ねる明るさ。
inline constexpr float LogoSpinSeconds=.65f; // 星を押してから左メニューが出るまでの1回転の秒数。
static_assert(PanelOpacity>0 && PanelOpacity<1 && HoverOpacity>=0 && HoverOpacity<1);
static_assert(LogoSpinSeconds>0 && LeftWidth>0 && RightWidth>0 && MenuPadding>0);
static_assert(LeftRowHeight>0 && RightRowHeight>0 && HamburgerSize>0 && RollWidth>0 && RollHeight>0);
} // namespace aura::tuning::shell
