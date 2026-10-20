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
    const unsigned step=std::max(1u,rate/100);
    const auto frames=pcm.size()/(channels*2);
    std::vector<float> result((frames+step-1)/step);
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
        if(count==step || i+1==frames) {
            result[i/step]=static_cast<float>(std::sqrt(energy/(count*channels)));
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
}
