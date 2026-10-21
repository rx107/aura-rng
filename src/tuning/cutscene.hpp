#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

// 入手演出のカメラ・フェード・字幕を調整する入口です。
// カメラやサイズを変えたら保存→Develop.cmdで再ビルドしてください。
// BreakThroughの字幕の時刻だけはF1でも変更・保存でき、再ビルド不要です。
namespace aura::tuning::cutscene {
// Equinoxは終わりに「演出 → 実際のオーラ」を重ね、音量も同じ曲線で下げます。
// BreakThroughは instantSwitch=true を渡し、最後の1フレームまで表示して即切り替えます。
inline constexpr float EndingFadeSeconds = .8f;
inline constexpr float endingOpacity(float elapsed,float duration,bool instantSwitch=false) {
    if(instantSwitch) return elapsed<duration?1.f:0.f;
    const float progress=std::clamp((elapsed-(duration-EndingFadeSeconds))/EndingFadeSeconds,0.f,1.f);
    // smoothstep と同じ曲線。出始めと終わりの速度が0になり、切り替えが自然です。
    return 1-progress*progress*(3-2*progress);
}

// Equinox の音声から取り出した低音の強さ [0～1] で、画面全体を上下します。
// .008 は画面高の0.8%。.004にすると半分、0にすると揺れなしになります。
// 一定のテンポを仮定せず、再生中の音声位置に対応する低音データを使います。
inline constexpr float EquinoxBassShakeHeight = .008f;

inline constexpr float BreakthroughDuration = 30.f;
// iiiのカメラ。距離は注視点からの長さ、角度はラジアンです。
// pitch<0で見上げ、pitch>0で見下ろします。rollは画面そのものの傾きです。
// focalLengthを小さくすると広角になり、手前と奥の距離感が強くなります。
struct BreakthroughShot {
    float start,end;
    float distanceFrom,distanceTo;
    float yawFrom,yawTo,pitch;
    float targetHeight,rollFrom,rollTo,focalLength;
};
// i・iiは初実装 v0.1.6 の「柱の一部を近くから斜めに映す」カメラです。
// targetHeightを上げると柱の高い場所へ、distanceを下げると柱へ近づきます。
// yawSpeedは回り込む速さ、rollSpeedは画面が傾く速さです（ラジアン/秒）。
struct BreakthroughOriginalShot {
    float start,end,distance,targetHeight,yaw,yawSpeed,roll,rollSpeed,pitch;
};
inline constexpr BreakthroughOriginalShot BreakthroughOriginalI{
    0,3.4f,4.15f,6.0f,.24f,.035f,-.30f,.055f,.12f};
inline constexpr BreakthroughOriginalShot BreakthroughOriginalII{
    3.4f,7.3f,4.6f,1.5f,.45f,.06f,-.48f,.065f,.12f};
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
inline constexpr float BreakthroughMonolithEnd = 14.15f;
// v0.1.13と同じ前進速度を保ったまま、ivの終わりで黒画面へ切ります。
// TravelSecondsを短くすると、この短い場面の中でより速く前進します。
inline constexpr float BreakthroughMonolithTravelSeconds = 18.9f;
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
// 初実装の黒画面と、その後に戻る7本柱の全景。
// 黒画面は薄く重ねず完全な黒にし、字幕だけを出します。
inline constexpr float BreakthroughBlackoutStart = BreakthroughMonolithEnd;
inline constexpr float BreakthroughBlackoutEnd = 15.05f;
inline constexpr float BreakthroughFinalDistance = 12.2f;
inline constexpr float BreakthroughFinalPullbackSpeed = .045f;
inline constexpr float BreakthroughFinalYawSpeed = .017f;
// it wasn't gone と with only 5 fragments left は同じ傾き・横移動にします。
inline constexpr float BreakthroughFloatingCaptionAngle = -2.f;
inline constexpr float BreakthroughFloatingCaptionSpeed = 6.f; // 基準画面480px高でのpx/秒。
inline constexpr float BreakthroughFloatingCaptionMoveSeconds = 4.f;
inline float breakthroughCaptionDrift(float elapsed,float start) {
    return std::clamp(elapsed-start,0.f,BreakthroughFloatingCaptionMoveSeconds)*BreakthroughFloatingCaptionSpeed;
}

// GPUに渡すカメラの位置と向き。ゲームとテストが同じ計算を使います。
struct BreakthroughCamera {
    std::array<float,3> eye{},target{};
    float roll=0,focalLength=1.6f;
    bool monolithSpace=false;
    float whiteout=0;
    bool blackout=false;
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
    const auto original=[&](const BreakthroughOriginalShot& shot) {
        const float local=elapsed-shot.start;
        const float yaw=shot.yaw+local*shot.yawSpeed;
        camera.target={0,shot.targetHeight,0};
        // 元の式をそのまま共有します。横方向の距離をpitchで縮めない点も原版通りです。
        camera.eye={std::sin(yaw)*shot.distance,
            shot.targetHeight+std::sin(shot.pitch)*shot.distance,-std::cos(yaw)*shot.distance};
        camera.roll=shot.roll+local*shot.rollSpeed;
        camera.focalLength=1.6f;
    };
    if(elapsed<BreakthroughOriginalI.end)original(BreakthroughOriginalI);
    else if(elapsed<BreakthroughOriginalII.end)original(BreakthroughOriginalII);
    else if(elapsed<BreakthroughMonolithStart)orbit(BreakthroughCoreRush,true);
    else if(elapsed<BreakthroughMonolithEnd) {
        const float local=elapsed-BreakthroughMonolithStart;
        const float u=std::clamp(local/BreakthroughMonolithTravelSeconds,0.f,1.f);
        const float bob=BreakthroughMonolithBobHeight*std::sin(local*BreakthroughMonolithBobSpeed);
        camera.eye={0,BreakthroughMonolithEyeHeight+bob,
            -BreakthroughMonolithStartDistance+BreakthroughMonolithTravelDistance*u};
        camera.target={0,3.9f+bob,15.f};
        camera.focalLength=BreakthroughMonolithFocalLength;
        camera.monolithSpace=true;
        const float fade=std::clamp(local/BreakthroughWhiteoutSeconds,0.f,1.f);
        camera.whiteout=(1-fade)*(1-fade);
    } else if(elapsed<BreakthroughBlackoutEnd) {
        // 描画は黒になりますが、デバッグやテストで向きを調べても正常な視点を返します。
        camera.eye={0,4.f,-12.f};camera.target={0,3.9f,0};
        camera.blackout=true;
    } else {
        // 原版の後半: 7本柱の全景を少しずつ引きながら横へ回り込む。
        const float distance=BreakthroughFinalDistance+(elapsed-BreakthroughBlackoutEnd)*BreakthroughFinalPullbackSpeed;
        const float yaw=.28f+BreakthroughFinalYawSpeed*(elapsed-15.f);
        camera.target={0,3.9f,0};
        camera.eye={std::sin(yaw)*distance,3.9f+std::sin(.04f)*distance,-std::cos(yaw)*distance};
        camera.roll=.015f*std::sin(elapsed*.4f);
        camera.focalLength=1.6f;
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
