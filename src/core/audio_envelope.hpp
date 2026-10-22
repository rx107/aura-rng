#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace aura {
// 音楽を読み込んだ時に一度だけ、35〜180Hz付近の低音を10msごとに計測します。
// 描画中はこの短い表を読むだけ。曲ごとのBPMを決め打ちせず、実際のBassに追従します。
inline constexpr float BassEnvelopeHz=100.f;
inline std::vector<float> bassEnvelope(std::span<const std::uint8_t> pcm,unsigned rate,unsigned channels) {
    if(rate<1000 || channels==0 || channels>8 || pcm.size()%(channels*2)) return {};
    const auto frames=pcm.size()/(channels*2);
    constexpr auto EnvelopeRate=static_cast<unsigned>(BassEnvelopeHz);
    std::vector<float> result((frames*EnvelopeRate+rate-1)/rate);
    // 例: 22050Hz は10ms=220.5サンプル。220個ずつに丸め続けると音と時計がずれます。
    // 各10ms境界を元のサンプル位置から求め、220個・221個を交互に使って100Hzを保ちます。
    // 44100Hz/48000Hzのように割り切れる場合は、従来と同じ441個/480個です。
    std::size_t bin=0,nextBoundary=rate/EnvelopeRate;
    std::array<double,8> low{},sub{};
    const double a=1-std::exp(-2*3.141592653589793*180/rate);
    const double b=1-std::exp(-2*3.141592653589793*35/rate);
    double energy=0; unsigned count=0;
    for(std::size_t i=0;i<frames;++i) {
        for(unsigned c=0;c<channels;++c) {
            const auto p=(i*channels+c)*2;
            const unsigned bits=pcm[p]|(static_cast<unsigned>(pcm[p+1])<<8);
            const double sample=(bits>=32768?static_cast<int>(bits)-65536:static_cast<int>(bits))/32768.;
            low[c]+=a*(sample-low[c]); sub[c]+=b*(sample-sub[c]);
            const double bass=low[c]-sub[c]; energy+=bass*bass;
        }
        ++count;
        if(i+1>=nextBoundary || i+1==frames) {
            result[bin]=static_cast<float>(std::sqrt(energy/(count*channels)));
            ++bin;
            nextBoundary=(bin+1)*rate/EnvelopeRate;
            energy=0; count=0;
        }
    }
    // 曲ごとの音量差を吸収し、0〜1へ正規化。無音は0のままにします。
    const float peak=result.empty()?0:*std::max_element(result.begin(),result.end());
    float held=0;
    for(auto& value:result) {
        const float normalized=peak>.0001f?value/peak:0;
        held=std::max(normalized,held*.86f); value=held;
    }
    return result;
}
inline float bassAt(std::span<const float> envelope,float seconds) {
    if(envelope.empty() || !std::isfinite(seconds) || seconds<0) return 0;
    const float index=seconds*BassEnvelopeHz;
    if(index>=static_cast<float>(envelope.size())) return 0;
    const auto i=static_cast<std::size_t>(index);
    const float next=envelope[std::min(i+1,envelope.size()-1)];
    return envelope[i]+(next-envelope[i])*(index-static_cast<float>(i));
}

// 音楽の拍の設計図。BPM=1分間の拍数、offset=最初の拍の位置[秒]です。
// 低音の立ち上がりの繰り返しから推定します。曲によって半拍/倍拍に解釈されるため、
// F1のBPMと時差でも調整できます。曲の読み込み時だけ計算し、描画中は再解析しません。
struct BeatGrid { float bpm{},offset{},confidence{}; };
// 音声の秒数を連続した拍数へ。ループ時も曲の先頭の拍へ合わせ直します。
// 端が半端な拍で切れた音源は、近い整数拍へ接続して位相ずれの蓄積を防ぎます。
inline float musicBeatPosition(float seconds,float bpm,float offset,float loopSeconds=0) {
    if(!std::isfinite(seconds) || seconds<0 || !std::isfinite(bpm) || bpm<30 || bpm>300 || !std::isfinite(offset)) return 0;
    double local=seconds,completed=0;
    if(std::isfinite(loopSeconds) && loopSeconds>0) {
        const double loops=std::floor(local/loopSeconds);
        local=std::fmod(local,loopSeconds);
        completed=loops*std::round(static_cast<double>(loopSeconds)*bpm/60);
    }
    return static_cast<float>(std::max(0.,completed+(local-offset)*bpm/60));
}
inline BeatGrid estimateBeatGrid(std::span<const float> envelope) {
    const auto n=std::min<std::size_t>(envelope.size(),12000); // 解析は先頭120秒まで。
    if(n<400) return {};
    std::vector<float> attacks(n);
    double energy=0;
    for(std::size_t i=2;i<n;++i) {
        attacks[i]=std::max(0.f,envelope[i]-envelope[i-2]-.008f);
        energy+=attacks[i]*attacks[i];
    }
    if(energy<.001) return {};
    float bestBpm=0,bestScore=0,rawCorrelation=0;
    for(unsigned tenth=600;tenth<=2400;++tenth) {
        const float bpm=tenth*.1f,lag=6000.f/bpm;
        const auto whole=static_cast<std::size_t>(lag); const float part=lag-static_cast<float>(whole);
        double correlation=0;
        for(std::size_t i=whole+1;i<n;++i)
            correlation+=attacks[i]*(attacks[i-whole]*(1-part)+attacks[i-whole-1]*part);
        const float normalized=static_cast<float>(correlation/energy);
        // 同じリズムの倍/半分が並ぶ場合だけ、極端なテンポを選びにくくする軽い重み。
        const float octave=std::log(bpm/150.f);
        const float score=normalized*(1+.06f*std::exp(-octave*octave/.4f));
        if(score>bestScore) {bestScore=score;bestBpm=bpm;rawCorrelation=normalized;}
    }
    if(rawCorrelation<.06f) return {};
    const float period=60.f/bestBpm;
    constexpr unsigned PhaseBins=200;
    std::array<float,PhaseBins> phase{};
    for(std::size_t i=0;i<n;++i) {
        const float cycles=static_cast<float>(i)/BassEnvelopeHz/period;
        const auto bin=std::min(PhaseBins-1,static_cast<unsigned>((cycles-std::floor(cycles))*PhaseBins));
        phase[bin]+=attacks[i];
    }
    unsigned bestPhase=0; float phaseScore=0;
    for(unsigned b=0;b<PhaseBins;++b) {
        float score=0;
        for(int d=-3;d<=3;++d)score+=phase[(static_cast<int>(b)+d+PhaseBins)%PhaseBins]*(4-std::abs(d));
        if(score>phaseScore){phaseScore=score;bestPhase=b;}
    }
    return {bestBpm,period*bestPhase/PhaseBins,std::clamp(rawCorrelation,0.f,1.f)};
}
inline float beatPulse(float seconds,BeatGrid grid,float bpmOverride=0,float offset=0) {
    if(!std::isfinite(seconds) || seconds<0 || !std::isfinite(offset) || !std::isfinite(bpmOverride) ||
        bpmOverride<0 || !std::isfinite(grid.offset)) return 0;
    const float bpm=bpmOverride>0?bpmOverride:grid.bpm;
    if(!std::isfinite(bpm) || bpm<30 || bpm>300) return 0;
    const float beat=(seconds-grid.offset-offset)*bpm/60.f;
    const float phase=beat-std::floor(beat);
    // 拍の直前に少し膨らみ、直後に素早く縮みます。両端の値は同じなので飛びません。
    const float attack=phase>.90f?(phase-.90f)/.10f:0;
    const float release=std::max(0.f,1-phase/.36f);
    return std::max(attack*attack,release*release);
}
}
