#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

// BreakThrough の入手演出を音に合わせる設定です。単位はすべて「秒」。
// F1 の CutScene 調整でも変更・保存できるので、数字を試すだけなら再ビルドは不要です。
// このファイルは、新規セーブと「初期値に戻す」で使う基準値を決めます。
namespace aura::tuning::cutscene {
inline constexpr float BreakthroughDuration = 30.f;
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
