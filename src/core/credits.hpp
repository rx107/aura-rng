#pragma once
#include "core/shell.hpp"
#include "tuning/credits.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

// DirectXから独立したクレジット画面の設計図。描画とクリック判定で同じ座標を使います。
namespace aura::credits {
inline constexpr std::size_t GroupCount=7;
inline shell::UiRect layout(float width,float height) {
    namespace c=tuning::credits;
    const float aspect=c::ReferenceWidth/c::ReferenceHeight;
    const float availableWidth=std::max(1.f,width-c::HorizontalMargin*2);
    const float availableHeight=std::max(1.f,height-c::VerticalMargin*2);
    const float panelWidth=std::min({c::PanelMaxWidth,availableWidth,availableHeight*aspect});
    const float panelHeight=panelWidth/aspect;
    return {(width-panelWidth)*.5f,(height-panelHeight)*.5f,panelWidth,panelHeight};
}
inline shell::UiRect closeLayout(float width,float height) {
    const auto panel=layout(width,height);
    return {panel.x+panel.width-tuning::credits::CloseSize-12,panel.y+12,
        tuning::credits::CloseSize,tuning::credits::CloseSize};
}
struct Appearance {float alpha{},offsetX{};};
inline Appearance appearance(float elapsed,std::size_t index) {
    namespace c=tuning::credits;
    if(!std::isfinite(elapsed) || elapsed<0 || index>=GroupCount)return {0,-c::SlidePixels};
    const float start=c::FirstRevealSeconds+static_cast<float>(index)*c::RevealStaggerSeconds;
    const float progress=std::clamp((elapsed-start)/c::FadeSeconds,0.f,1.f);
    const float eased=progress*progress*(3-2*progress); // 始まりと終わりをなめらかにする。
    return {eased,-c::SlidePixels*(1-eased)};
}
inline constexpr float RevealFinishedSeconds=tuning::credits::FirstRevealSeconds+
    static_cast<float>(GroupCount-1)*tuning::credits::RevealStaggerSeconds+tuning::credits::FadeSeconds;
} // namespace aura::credits
