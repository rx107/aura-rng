#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

// 入手演出のカメラ・フェード・字幕を調整する入口です。
// カメラやサイズを変えたら保存→Develop.cmdで再ビルドしてください。
// BreakThroughの字幕の時刻だけはF1でも変更・保存でき、再ビルド不要です。
namespace aura::tuning::cutscene {
// 入手演出の終わりから、この秒数だけ「演出 → 実際のオーラ」を重ねて切り替えます。
// 音量も同じ値を使います。大きくすると、画面と音の余韻が長くなります。
inline constexpr float EndingFadeSeconds = .8f;
inline constexpr float endingOpacity(float elapsed,float duration) {
    const float progress=std::clamp((elapsed-(duration-EndingFadeSeconds))/EndingFadeSeconds,0.f,1.f);
    // smoothstep と同じ曲線。出始めと終わりの速度が0になり、切り替えが自然です。
    return 1-progress*progress*(3-2*progress);
}

// Equinox の音声から取り出した低音の強さ [0～1] で、画面全体を上下します。
// .008 は画面高の0.8%。.004にすると半分、0にすると揺れなしになります。
// 一定のテンポを仮定せず、再生中の音声位置に対応する低音データを使います。
inline constexpr float EquinoxBassShakeHeight = .008f;

inline constexpr float BreakthroughDuration = 30.f;
// i～iiiのカメラ。距離は注視点からの長さ、角度はラジアンです。
// pitch<0で見上げ、pitch>0で見下ろします。rollは画面そのものの傾きです。
// focalLengthを小さくすると広角になり、手前と奥の距離感が強くなります。
struct BreakthroughShot {
    float start,end;
    float distanceFrom,distanceTo;
    float yawFrom,yawTo,pitch;
    float targetHeight,rollFrom,rollTo,focalLength;
};
// i: 低い場所から見上げて回り込む。yawFrom/Toの差を大きくすると横移動が増えます。
inline constexpr BreakthroughShot BreakthroughLowOrbit{
    .start=0,.end=3.4f,
    .distanceFrom=9.8f,.distanceTo=9.0f,
    .yawFrom=-.32f,.yawTo=.27f,.pitch=-.40f,
    .targetHeight=4.0f,.rollFrom=-.04f,.rollTo=.025f,.focalLength=1.18f};
// ii: 高い場所から斜めに見下ろして遠ざかる。distanceToを増やすと後退量が増えます。
inline constexpr BreakthroughShot BreakthroughHighPullback{
    .start=3.4f,.end=7.3f,
    .distanceFrom=9.1f,.distanceTo=13.5f,
    .yawFrom=.56f,.yawTo=.78f,.pitch=.67f,
    .targetHeight=3.7f,.rollFrom=-.30f,.rollTo=-.16f,.focalLength=1.28f};
// iii: 向きを変えずに光へ急接近。yawFromとyawToを同じにすると一直線になります。
inline constexpr BreakthroughShot BreakthroughCoreRush{
    .start=7.3f,.end=11.1f,
    .distanceFrom=15.f,.distanceTo=.72f,
    .yawFrom=.08f,.yawTo=.08f,.pitch=0,
    .targetHeight=3.9f,.rollFrom=0,.rollTo=0,.focalLength=1.25f};
// iiiは値を大きくすると後半の接近が急激になります。1なら等速です。
inline constexpr float BreakthroughRushAcceleration = 1.65f;
// コアへこの距離より近づくと光も大きく見えます。大きくすると、より早く光に包まれます。
inline constexpr float BreakthroughCoreFlareDistance = 9.f;

// iv: 閃光の後、巨大な柱の間を光源へ向かって直進します。
// 例: StartDistance=24でより奥から、TravelDistance=6で前進量を増やします。
inline constexpr float BreakthroughMonolithStart = 11.1f;
inline constexpr float BreakthroughMonolithEnd = BreakthroughDuration;
inline constexpr float BreakthroughMonolithStartDistance = 20.f;
inline constexpr float BreakthroughMonolithTravelDistance = 3.8f;
inline constexpr float BreakthroughMonolithEyeHeight = 1.05f;
inline constexpr float BreakthroughMonolithFocalLength = .83f;
inline constexpr float BreakthroughMonolithBobHeight = .055f; // 0なら浮遊するカメラの上下なし。
inline constexpr float BreakthroughMonolithBobSpeed = 1.25f;  // ラジアン/秒。
inline constexpr float BreakthroughMonolithScale = 1.f;       // 全ての巨大柱の大きさの倍率。
inline constexpr float BreakthroughWhiteoutSeconds = .43f;    // 閃光から視界が戻るまでの秒数。
// ivの文章の時刻は音楽との同期を維持します。カメラとは別に変更できます。
inline constexpr float BreakthroughIvCaptionStart = 13.f;

// GPUに渡すカメラの位置と向き。ゲームとテストが同じ計算を使います。
struct BreakthroughCamera {
    std::array<float,3> eye{},target{};
    float roll=0,focalLength=1.6f;
    bool monolithSpace=false;
    float whiteout=0;
};
inline BreakthroughCamera breakthroughCamera(float elapsed) {
    elapsed=std::clamp(elapsed,0.f,BreakthroughDuration);
    BreakthroughCamera camera;
    const auto lerp=[](float a,float b,float u){return a+(b-a)*u;};
    const auto orbit=[&](const BreakthroughShot& shot,bool rush=false) {
        float u=std::clamp((elapsed-shot.start)/(shot.end-shot.start),0.f,1.f);
        if(rush)u=std::pow(u,BreakthroughRushAcceleration);
        const float distance=lerp(shot.distanceFrom,shot.distanceTo,u);
        const float yaw=lerp(shot.yawFrom,shot.yawTo,u);
        camera.target={0,shot.targetHeight,0};
        camera.eye={std::sin(yaw)*std::cos(shot.pitch)*distance,
            shot.targetHeight+std::sin(shot.pitch)*distance,
            -std::cos(yaw)*std::cos(shot.pitch)*distance};
        camera.roll=lerp(shot.rollFrom,shot.rollTo,u);
        camera.focalLength=shot.focalLength;
    };
    if(elapsed<BreakthroughLowOrbit.end)orbit(BreakthroughLowOrbit);
    else if(elapsed<BreakthroughHighPullback.end)orbit(BreakthroughHighPullback);
    else if(elapsed<BreakthroughMonolithStart)orbit(BreakthroughCoreRush,true);
    else {
        const float local=elapsed-BreakthroughMonolithStart;
        const float u=std::clamp(local/(BreakthroughMonolithEnd-BreakthroughMonolithStart),0.f,1.f);
        const float bob=BreakthroughMonolithBobHeight*std::sin(local*BreakthroughMonolithBobSpeed);
        camera.eye={0,BreakthroughMonolithEyeHeight+bob,
            -BreakthroughMonolithStartDistance+BreakthroughMonolithTravelDistance*u};
        camera.target={0,3.9f+bob,15.f};
        camera.focalLength=BreakthroughMonolithFocalLength;
        camera.monolithSpace=true;
        const float fade=std::clamp(local/BreakthroughWhiteoutSeconds,0.f,1.f);
        camera.whiteout=(1-fade)*(1-fade);
    }
    return camera;
}
// 参考 MP4 の先頭には Collection 画面があるため、音声は 4.5 秒から使います。
// ここを変えると音楽全体の開始場所が変わります。字幕だけのずれは下の数字で調整。
inline constexpr float BreakthroughAudioStart = 4.5f;

// how / much / do / you / believe / in / this / world? の出始め。
// 「CutScene の音声が実際に再生され始めてから」の絶対秒です。
// 参考動画を 60 fps で確認した各単語の初出フレームから計測しています。
// 例: how を 22.466667f → 22.566667f にすると、その単語だけ 0.1 秒遅れます。
// 左から右へ必ず小さい順に並べます。全単語の一括移動は F1 の Offset が便利です。
inline constexpr std::array<float,8> BreakthroughWordTimes{
    22.466667f, 22.716667f, 22.950000f, 23.183333f,
    23.416667f, 23.666667f, 23.900000f, 24.133333f
};
// 最後の world? が出てから全単語が消えるまで。大きくすると長く読み残せます。
inline constexpr float BreakthroughCaptionHoldSeconds = 1.266667f;

// 「いつ消すか」も最後の単語に追従します。時刻を遅く調整しても途中で切れません。
inline constexpr float breakthroughCaptionEnd(const std::array<float,8>& times,float offset) {
    return std::min(BreakthroughDuration,times.back()+offset+BreakthroughCaptionHoldSeconds);
}
inline constexpr bool breakthroughWordVisible(float elapsed,std::size_t word,
    const std::array<float,8>& times,float offset=0.f) {
    return word<times.size() && elapsed>=times[word]+offset &&
        elapsed<breakthroughCaptionEnd(times,offset);
}
static_assert(BreakthroughAudioStart>=0 && BreakthroughDuration>0);
static_assert(BreakthroughWordTimes.front()>=0 && BreakthroughWordTimes.back()<BreakthroughDuration);
static_assert([] {
    for(std::size_t i=1;i<BreakthroughWordTimes.size();++i)
        if(BreakthroughWordTimes[i]<=BreakthroughWordTimes[i-1])return false;
    return true;
}(),"Caption word times must be in increasing order");
} // namespace aura::tuning::cutscene
