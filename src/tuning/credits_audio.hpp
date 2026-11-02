#pragma once

// クレジットを開くときの「ポコッ」という短い音です。外部の音声ファイルは使いません。
// 変更後は保存し、Develop.cmdで再ビルドすると反映されます。
namespace aura::tuning::credits_audio {
inline constexpr float Gain=.60f; // .60f → .40f で控えめな音量。ゲーム側の音量も掛けます。
inline constexpr float DurationSeconds=.095f; // .095秒。大きくすると余韻が長くなります。
inline constexpr float StartFrequencyHz=700.f; // 最初の高さ。大きくすると「ピコッ」に近づきます。
inline constexpr float EndFrequencyHz=180.f; // 最後の高さ。小さくすると低い「ポコッ」になります。
inline constexpr float PitchFallRate=48.f; // 大きくすると、音の高さが素早く下がります。
inline constexpr float AttackSeconds=.0015f; // 立ち上がりを滑らかにし、不要なクリック音を防ぎます。
inline constexpr float DecayRate=28.f; // 大きくすると早く小さくなり、短く歯切れのよい音になります。
inline constexpr unsigned CooldownMilliseconds=90; // 開閉を連打しても音が重なり過ぎない間隔。
static_assert(Gain>=0 && Gain<=1 && DurationSeconds>.01f && DurationSeconds<1);
static_assert(StartFrequencyHz>0 && StartFrequencyHz<12000 && EndFrequencyHz>0 && EndFrequencyHz<12000);
static_assert(PitchFallRate>0 && AttackSeconds>0 && AttackSeconds<DurationSeconds && DecayRate>0);
}
