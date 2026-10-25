#pragma once
#include <array>

// Red john の「部屋・ベッド・赤い顔」を調整する入口です。
// 長さはゲーム内単位。ブロックアバターの立った身長は約 2.8 です。
// 変更 → Develop.cmd → F1 の Red john プレビュー、の順で見比べてください。
// 例: FaceRadius を 1.06f → 1.25f にすると、壁の赤い顔だけが大きくなります。
namespace aura::tuning::redjohn {
inline constexpr float CutsceneDuration=10.9f; // 参考動画は約10.89秒。演出全体の長さ（秒）
// 時刻は「再生開始からの秒」。大きくするとその場面だけ遅くなります。
inline constexpr float PanelsStart=2.05f; // 黒い導入の後、青・白・黄色の帯が現れる。
inline constexpr float EyeStart=2.85f, PortraitStart=3.80f; // 自作アバターの目 → 横顔シルエット。
inline constexpr float TitleStart=5.45f, TitleSettle=6.05f; // 大きい文字が滑り、RED JOHN の全体が読める。
inline constexpr float EndStart=8.75f; // 帯が去り、赤い顔の輪郭を残して部屋のオーラへつなぐ。
inline constexpr float Letterbox=.18f; // 画面上下の黒帯。0なら全画面、.18なら各18%。
inline constexpr float PanelSpeed=1.f; // 帯が横切る速さの倍率。場面の開始時刻は変えない。
inline constexpr float FilmGrain=.025f; // 0で粒状感なし。増やすと古いフィルム風になる。

// ── 小さな部屋 ──
// アバターから見て +Z が背面、-Z が正面です。部屋も体の向きと一緒に回ります。
inline constexpr float RoomWidth=6.5f;
inline constexpr float RoomHeight=4.8f;
inline constexpr float RoomBack=1.47f;
inline constexpr float RoomFront=-2.55f;
inline constexpr float FloorHeight=.075f;
inline constexpr float WallThickness=.12f;
// true: 外側へカメラを回すと、手前の壁だけを省略してアバターを見せる「ドールハウス表示」。
inline constexpr bool CutawayWalls=true;
inline constexpr std::array<float,3> WallColor={.73f,.72f,.64f};
inline constexpr std::array<float,3> CarpetColor={.34f,.30f,.25f};
inline constexpr std::array<float,3> SkirtingColor={.20f,.13f,.10f};
inline constexpr float RoomSelfLight=.45f; // 部屋の暗さの補助。大きいほどステージ照明に左右されにくい

// ── マットレスと枕 ──
// BedTop は座りポーズと合わせる基準です。床から .60 の高さに腰を置きます。
inline constexpr float BedTop=.60f;
inline constexpr float BedCenterX=.12f;
inline constexpr float BedCenterZ=.49f; // 前端=-.22、後端=1.20。曲げた脚が寝具の中へ埋もれない位置
inline constexpr float BedWidth=4.56f;
inline constexpr float BedDepth=1.42f; // 手前へ張り出しすぎると、上げた膝から下が隠れるので薄めにする
inline constexpr float BedThickness=.35f;
inline constexpr unsigned MattressSlices=24; // 増やすと丸い角が滑らかになる。24 で十分軽い
inline constexpr std::array<float,3> LinenColor={.77f,.80f,.78f};
inline constexpr float PillowX=1.42f;
inline constexpr float PillowZ=.52f;
inline constexpr float PillowWidth=1.16f;
inline constexpr float PillowDepth=.67f;
inline constexpr float PillowHeight=.21f;
inline constexpr unsigned PillowSlices=14;

// ── 赤いペンキの顔 ──
// 顔は写真を貼らず、輪・目・口・垂れる塗料を細い立体の線で描きます。
inline constexpr float FaceCenterX=-.72f;
inline constexpr float FaceCenterY=3.36f;
inline constexpr float FaceRadius=1.06f;
inline constexpr float PaintWidth=.063f; // 太くするとマーカー、細くすると刷毛のかすれた線に近づく
inline constexpr unsigned FaceSegments=78;
inline constexpr float PaintDripScale=1.f; // 0 で垂れを消す。2 で縦に 2 倍長くする
inline constexpr std::array<float,3> PaintColor={.66f,.075f,.095f};
inline constexpr float PaintSelfLight=.42f; // 赤を判別できる明るさ。強い発光ではありません

// ── 窓から差す昼の光と埃 ──
inline constexpr float WindowWidth=1.12f;
inline constexpr float WindowHeight=2.34f;
inline constexpr float WindowX=2.22f;
inline constexpr float WindowY=3.03f;
inline constexpr unsigned DustCount=12; // 静かな部屋なので粒子は少なめ。0 なら完全に消える
inline constexpr float DustSpeed=.11f; // 1 秒ごとの舞い上がる速さ
inline constexpr float DustBrightness=.075f; // 埃だけの弱い発光。上げすぎると部屋が眩しくなる

// 極端な設定で部品が裏返ったり、描画上限を超えたりしないための安全柵です。
static_assert(CutsceneDuration>0 && RoomWidth>BedWidth && RoomHeight>BedTop);
static_assert(0<=PanelsStart && PanelsStart<EyeStart && EyeStart<PortraitStart && PortraitStart<TitleStart);
static_assert(TitleStart<TitleSettle && TitleSettle<EndStart && EndStart<CutsceneDuration);
static_assert(Letterbox>=0 && Letterbox<.45f && PanelSpeed>0 && FilmGrain>=0);
static_assert(RoomBack>RoomFront && BedTop>BedThickness && WallThickness>0);
static_assert(FaceRadius>0 && PaintWidth>0 && PaintDripScale>=0);
static_assert(MattressSlices>=12 && MattressSlices<=32 && PillowSlices>=8 && PillowSlices<=20);
static_assert(FaceSegments>=40 && FaceSegments<=96 && DustCount<=20);
} // namespace aura::tuning::redjohn
