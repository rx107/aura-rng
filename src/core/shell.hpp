#pragma once
#include "tuning/shell.hpp"
#include "tuning/branding.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

// 画面に描く四角と、クリック判定の四角を共有します。DirectXを使わないUIの設計図です。
namespace aura::shell {
struct UiRect {
    float x,y,width,height;
    bool contains(float px,float py) const {return px>=x && py>=y && px<x+width && py<y+height;}
};
inline UiRect leftMenuLayout(float,float) {
    namespace s=tuning::shell;
    return {s::LeftMargin,s::MenuTop,s::LeftWidth,s::MenuPadding*2+s::LeftRowHeight*3};
}
inline UiRect settingsMenuLayout() {
    namespace s=tuning::shell;
    return {s::LeftMargin,s::MenuTop,s::SettingsWidth,s::MenuPadding*2+s::SettingsRowHeight*4};
}
inline UiRect leftMenuRow(float w,float h,std::size_t row) {
    namespace s=tuning::shell;auto p=leftMenuLayout(w,h);
    return {p.x+s::MenuPadding,p.y+s::MenuPadding+row*s::LeftRowHeight,p.width-2*s::MenuPadding,s::LeftRowHeight-8};
}
inline UiRect settingsMenuRow(std::size_t row) {
    namespace s=tuning::shell;auto p=settingsMenuLayout();
    return {p.x+s::MenuPadding,p.y+s::MenuPadding+row*s::SettingsRowHeight,p.width-2*s::MenuPadding,s::SettingsRowHeight-8};
}
inline UiRect auraButtonLayout(float w,float h) {
    namespace s=tuning::shell;
    return {w-s::EdgeMargin-s::AuraButtonSize,h-s::EdgeMargin-s::AuraButtonSize,s::AuraButtonSize,s::AuraButtonSize};
}
inline UiRect rollLayout(float w,float h) {
    namespace s=tuning::shell;
    return {(w-s::RollWidth-s::ButtonGap-s::AutoWidth)*.5f,h-s::RollBottomMargin-s::RollHeight,s::RollWidth,s::RollHeight};
}
inline UiRect autoRollLayout(float w,float h) {
    namespace s=tuning::shell;auto p=rollLayout(w,h);
    return {p.x+p.width+s::ButtonGap,p.y,s::AutoWidth,p.height};
}
inline UiRect starMenuLayout(float,float) {
    namespace b=tuning::branding;
    return {b::IconLeft-8,b::IconTop-8,b::IconWidth+16,b::IconWidth+16};
}
struct State {
    bool leftOpen{},settingsOpen{};
    float spinElapsed{-1};
    bool spinning() const {return spinElapsed>=0;}
    bool active() const {return leftOpen || settingsOpen || spinning();}
    float spinProgress() const {return spinning()?std::clamp(spinElapsed/tuning::shell::LogoSpinSeconds,0.f,1.f):-1.f;}
    void close() {leftOpen=settingsOpen=false;spinElapsed=-1;}
    void tapStar() {
        if(spinning())return; // 連打をため込まず、必ず1回転だけにする。
        const bool wasOpen=leftOpen || settingsOpen;close();
        if(!wasOpen)spinElapsed=0;
    }
    // 設定は星のメニューの1階層下。戻る矢印で星のメニューへ戻ります。
    void openSettings() {if(leftOpen) {leftOpen=false;settingsOpen=true;}}
    void backToLeftMenu() {if(settingsOpen) {settingsOpen=false;leftOpen=true;}}
    void update(float seconds) {
        if(!spinning())return;
        if(std::isfinite(seconds))spinElapsed+=std::max(0.f,seconds);
        if(spinElapsed>=tuning::shell::LogoSpinSeconds) {spinElapsed=-1;leftOpen=true;}
    }
};
} // namespace aura::shell
