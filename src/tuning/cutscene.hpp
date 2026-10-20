#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

// BreakThrough の入手演出を音に合わせる設定です。単位はすべて「秒」。
// F1 の CutScene 調整でも変更・保存できるので、数字を試すだけなら再ビルドは不要です。
// このファイルは、新規セーブと「初期値に戻す」で使う基準値を決めます。
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
// i / ii / iii のカメラが定位置に着く時刻。字幕はこの直後に出ます。
// 遠い場所から近づく「ぐわーん」の長さは、各場面の開始との差です。
inline constexpr std::array<float,3> BreakthroughCameraArrival{1.f,4.98f,8.96f};
// 定位置との距離の倍率。2なら約2倍遠い所から始めます。
inline constexpr float BreakthroughCameraFarMultiplier = 2.15f;
// 近づく際の横回り込みと画面の傾き [ラジアン]。0ならまっすぐ進みます。
inline constexpr float BreakthroughCameraSweepYaw = .32f;
inline constexpr float BreakthroughCameraSweepRoll = -.24f;
// ivの文章が出る瞬間に、光を消して白黒の柱へ切り替えます。
inline constexpr float BreakthroughMonochromeStart = 13.f;
// ivの白黒の柱。カメラを原点とした {左右, 上下, 奥行き} です。
// Nearの左右を小さくすると左へ、上下を小さくすると下へ移動します。
// Farは奥側の端。左右・上下が0なので、画面の中心へ向かって伸びます。
inline constexpr std::array<float,3> BreakthroughMonochromePillarNear{-1.20f,-.40f,3.f};
inline constexpr std::array<float,3> BreakthroughMonochromePillarFar{0,0,12.f};
inline constexpr float BreakthroughMonochromePillarHalfWidth = .18f;
inline constexpr float BreakthroughMonochromePillarHalfHeight = .24f;
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
