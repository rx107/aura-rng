#pragma once
#include "core/shell.hpp"
#include "tuning/aura_selector.hpp"

// 描画、クリック、スクロールが同じ四角を参照するようにする。
namespace aura::selector {
inline constexpr std::size_t CategoryCount=4;
struct Layout {
    shell::UiRect panel, list, details, close, equip, scrollbar;
};

inline Layout layout(float width,float height) {
    namespace t=tuning::aura_selector;
    constexpr float edge=tuning::shell::EdgeMargin;
    constexpr float padding=22.f;
    constexpr float listTopGap=98.f;
    constexpr float listBottomGap=90.f;
    const float panelWidth=width*t::PanelWidthRatio;
    const shell::UiRect panel{width-edge-panelWidth,t::PanelTop,panelWidth,height-t::PanelTop-t::PanelBottomGap};
    const float listHeight=std::max(0.f,panel.height-listTopGap-listBottomGap);
    const float fullRows=std::floor(listHeight/t::RowPitch);
    const shell::UiRect list{panel.x+padding,panel.y+listTopGap,panel.width-2.f*padding,fullRows*t::RowPitch};
    const float detailWidth=std::clamp(width*.34f,345.f,510.f);
    const shell::UiRect details{edge,height-t::DetailBottomGap-t::DetailHeight,detailWidth,t::DetailHeight};
    const shell::UiRect close{panel.x+panel.width-54.f,panel.y+16.f,34.f,34.f};
    const shell::UiRect equip{details.x+padding,details.y+details.height-64.f,details.width-2.f*padding,44.f};
    const shell::UiRect scrollbar{list.x+list.width-8.f,list.y,8.f,list.height};
    return {panel,list,details,close,equip,scrollbar};
}

inline shell::UiRect tab(const Layout& layout,std::size_t index) {
    constexpr float gap=8.f;
    constexpr float count=static_cast<float>(CategoryCount);
    const float width=(layout.list.width-gap*(count-1.f))/count;
    return {layout.list.x+(width+gap)*static_cast<float>(index),layout.panel.y+layout.panel.height-64.f,width,42.f};
}

inline shell::UiRect row(const Layout& layout,std::size_t visibleIndex) {
    namespace t=tuning::aura_selector;
    return {layout.list.x,layout.list.y+static_cast<float>(visibleIndex)*t::RowPitch,layout.list.width-18.f,t::RowPitch-t::RowGap};
}

inline int visibleRows(const Layout& layout) {
    return static_cast<int>(layout.list.height/tuning::aura_selector::RowPitch);
}

inline int maxScroll(const Layout& layout,std::size_t itemCount) {
    return std::max(0,static_cast<int>(itemCount)-visibleRows(layout));
}

inline shell::UiRect thumb(const Layout& layout,std::size_t itemCount,int scroll) {
    auto result=layout.scrollbar;
    const int maximum=maxScroll(layout,itemCount);
    if(maximum==0 || result.height<=0.f)return result;
    const float minimum=std::min(26.f,result.height);
    const float proportional=result.height*static_cast<float>(visibleRows(layout))/static_cast<float>(itemCount);
    result.height=std::clamp(proportional,minimum,result.height);
    const float ratio=static_cast<float>(std::clamp(scroll,0,maximum))/static_cast<float>(maximum);
    result.y+=(layout.scrollbar.height-result.height)*ratio;
    return result;
}
} // namespace aura::selector
