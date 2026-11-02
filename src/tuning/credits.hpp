#pragma once

// クレジットを自分で調整する入口です。長さは1440×900基準、時間は秒。
// 数字を変更 → 保存 → Develop.cmdで再ビルド → 左メニューのクレジット、で見比べます。
namespace aura::tuning::credits {
inline constexpr float PanelOpacity=.70f; // .70=不透明度70%。.90にすると背景がさらに濃くなる。
inline constexpr float PanelGray=.38f; // 0=黒、1=白。RGBを同じ値にして無彩色のグレーにする。
inline constexpr float PanelMaxWidth=1120.f; // 幅を広げると中の文字・画像も一緒に大きくなる。
inline constexpr float HorizontalMargin=48.f,VerticalMargin=70.f;
inline constexpr float ReferenceWidth=1336.f,ReferenceHeight=688.f; // 参考レイアウトの縦横比。
inline constexpr float FirstRevealSeconds=.08f; // 開いてから最初の内容が出始めるまで。
inline constexpr float RevealStaggerSeconds=.18f; // 項目の間隔。.30なら順番がもっとはっきりする。
inline constexpr float FadeSeconds=.45f; // 1項目が見えるまでの時間。大きくするとゆっくり。
inline constexpr float SlidePixels=28.f; // 左から近づく距離。0ならフェードだけになる。
inline constexpr float OutlineWidth=1.8f; // 白い文字の縁の太さ。2.5なら輪郭が太くなる。
inline constexpr float CloseSize=32.f; // 右上の×の大きさ。クリック判定も一緒に変わる。
static_assert(PanelOpacity>0 && PanelOpacity<1 && PanelGray>=0 && PanelGray<=1);
static_assert(ReferenceWidth>0 && ReferenceHeight>0 && PanelMaxWidth>0);
static_assert(FadeSeconds>0 && FirstRevealSeconds>=0 && RevealStaggerSeconds>=0 && SlidePixels>=0);
} // namespace aura::tuning::credits
