#pragma once
#include <array>

namespace aura::tuning::ui {
// UIの共通色 [R=赤, G=緑, B=青]。各0〜1。オーラの発光色とは別です。
// 例: Accentを{1.f,.55f,.20f}にすると、メインのミント色がオレンジになります。
inline constexpr std::array<float,3> Text{.91f,.95f,.98f};
inline constexpr std::array<float,3> MutedText{.48f,.57f,.65f};
inline constexpr std::array<float,3> Accent{.55f,.96f,.82f};
inline constexpr std::array<float,3> Panel{.047f,.070f,.094f};
inline constexpr std::array<float,3> Border{.13f,.19f,.23f};
constexpr bool validColor(std::array<float,3> color) {
    for(float channel:color) if(!(channel>=0.f && channel<=1.f)) return false;
    return true;
}
static_assert(validColor(Text) && validColor(MutedText) && validColor(Accent) && validColor(Panel) && validColor(Border),
    "UI colors must be between 0 and 1");
}
