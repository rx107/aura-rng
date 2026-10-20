#include "platform/audio.hpp"
#include "render/renderer.hpp"
#include "core/audio_envelope.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <cwctype>
#include <stdexcept>

// Ogg VorbisをWindowsの追加コーデックなしで読む、小さな同梱デコーダー。
// MP3/MP4/WAVなどは従来どおりMedia Foundationが読みます。
#define STB_VORBIS_NO_PUSHDATA_API
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4456 4457 4996 4701)
#endif
#include "stb/stb_vorbis.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace aura {
using Microsoft::WRL::ComPtr;
void Music::clear() {
    if(voice_) { voice_->DestroyVoice(); voice_=nullptr; }
    pcm_.clear(); bass_.clear(); playing_=false; gain_=0; sampleRate_=0; playbackEndSeconds_=0;
}
float Music::bassPulse(float seconds) const {return bassAt(bass_,seconds);}
std::optional<float> Music::playbackSeconds() const noexcept {
    if(!voice_ || !playing_ || !sampleRate_)return std::nullopt;
    XAUDIO2_VOICE_STATE state{};
    // XAUDIO2_VOICE_NOSAMPLESPLAYED を付けると SamplesPlayed が取れないので、ここは 0。
    voice_->GetState(&state,0);
    // XAUDIO2_END_OF_STREAM に到達すると SamplesPlayed は 0 に戻ります。
    // この 0 を映像へ返すと CutScene が最初へ戻って終われなくなるため、
    // 1 回再生のバッファが空なら「終了した時刻」を保持して返します。
    if(clipDuration_>0 && state.BuffersQueued==0)return playbackEndSeconds_;
    return static_cast<float>(static_cast<double>(state.SamplesPlayed)/sampleRate_);
}
Music::~Music() {
    clear(); if(master_) master_->DestroyVoice(); engine_.Reset(); if(mf_) MFShutdown();
}
void Music::load(const std::filesystem::path& path,float startSeconds,float durationSeconds) {
    if(path.empty()) throw std::runtime_error("Choose an aura BGM file");
    if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)>128ull*1024*1024)
        throw std::runtime_error("BGM is missing or exceeds 128 MiB");
    if(!mf_) { checked(MFStartup(MF_VERSION,MFSTARTUP_LITE),"Media Foundation startup"); mf_=true; }
    if(!engine_) {
        checked(XAudio2Create(&engine_,0,XAUDIO2_DEFAULT_PROCESSOR),"XAudio2 startup");
    }
    if(!master_) {
        checked(engine_->CreateMasteringVoice(&master_),"Audio output device");
    }
    WAVEFORMATEX oggFormat{};
    std::unique_ptr<WAVEFORMATEX,decltype(&CoTaskMemFree)> waveStorage(nullptr,CoTaskMemFree);
    WAVEFORMATEX* wave=&oggFormat;
    auto extension=path.extension().wstring();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    if(extension==L".ogg") {
        FILE* file{}; _wfopen_s(&file,path.c_str(),L"rb");
        if(!file) throw std::runtime_error("Cannot open Ogg BGM");
        int error{};
        std::unique_ptr<stb_vorbis,decltype(&stb_vorbis_close)> decoder(stb_vorbis_open_file(file,1,&error,nullptr),stb_vorbis_close);
        if(!decoder) throw std::runtime_error("Invalid Ogg Vorbis BGM");
        const auto info=stb_vorbis_get_info(decoder.get());
        if(info.channels<1 || info.channels>2 || info.sample_rate<8000 || info.sample_rate>192000)
            throw std::runtime_error("Ogg BGM must be mono/stereo, 8..192 kHz");
        oggFormat.wFormatTag=WAVE_FORMAT_PCM; oggFormat.nChannels=static_cast<WORD>(info.channels);
        oggFormat.nSamplesPerSec=info.sample_rate; oggFormat.wBitsPerSample=16;
        oggFormat.nBlockAlign=oggFormat.nChannels*2; oggFormat.nAvgBytesPerSec=info.sample_rate*oggFormat.nBlockAlign;
        std::array<short,8192> decoded{};
        for(;;) {
            const int frames=stb_vorbis_get_samples_short_interleaved(decoder.get(),info.channels,decoded.data(),static_cast<int>(decoded.size()));
            if(!frames) break;
            const auto bytes=static_cast<std::size_t>(frames)*oggFormat.nBlockAlign;
            if(pcm_.size()+bytes>128ull*1024*1024) throw std::runtime_error("Decoded BGM exceeds 128 MiB");
            const auto* first=reinterpret_cast<const BYTE*>(decoded.data());
            pcm_.insert(pcm_.end(),first,first+bytes);
        }
        if(stb_vorbis_get_error(decoder.get())!=VORBIS__no_error) throw std::runtime_error("Ogg BGM decoding failed");
    } else {
    ComPtr<IMFSourceReader> reader; checked(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader),"Open BGM");
    checked(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE),"BGM stream selection");
    checked(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),TRUE),"BGM audio stream");
    ComPtr<IMFMediaType> request; checked(MFCreateMediaType(&request),"BGM media type");
    checked(request->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio),"BGM audio type");
    checked(request->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM),"BGM PCM type");
    checked(request->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16),"BGM PCM sample size");
    checked(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),nullptr,request.Get()),"Decode BGM to PCM");
    ComPtr<IMFMediaType> format; checked(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),&format),"BGM output format");
    WAVEFORMATEX* raw{}; UINT length{};
    checked(MFCreateWaveFormatExFromMFMediaType(format.Get(),&raw,&length),"BGM wave format");
    waveStorage.reset(raw); wave=raw;
    for(;;) {
        DWORD flags{}; ComPtr<IMFSample> sample;
        checked(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),0,nullptr,&flags,nullptr,&sample),"Decode BGM sample");
        if(flags&MF_SOURCE_READERF_ERROR) throw std::runtime_error("BGM decoding failed");
        if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) throw std::runtime_error("Changing BGM formats are unsupported");
        if(sample) {
            ComPtr<IMFMediaBuffer> buffer; checked(sample->ConvertToContiguousBuffer(&buffer),"BGM sample buffer");
            BYTE* bytes{}; DWORD count{}; checked(buffer->Lock(&bytes,nullptr,&count),"BGM sample lock");
            if(pcm_.size()+count>128ull*1024*1024) {buffer->Unlock(); throw std::runtime_error("Decoded BGM exceeds 128 MiB");}
            try {pcm_.insert(pcm_.end(),bytes,bytes+count);} catch(...) {buffer->Unlock(); throw;}
            checked(buffer->Unlock(),"BGM sample unlock");
        }
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    }
    if(pcm_.empty()) throw std::runtime_error("BGM contains no audio");
    if(startSeconds>0 || durationSeconds>0) {
        const auto begin=static_cast<std::size_t>(std::llround(static_cast<double>(startSeconds)*wave->nSamplesPerSec))*wave->nBlockAlign;
        if(begin>=pcm_.size()) throw std::runtime_error("Audio clip starts beyond its duration");
        pcm_.erase(pcm_.begin(),pcm_.begin()+begin);
        if(durationSeconds>0) {
            const auto bytes=static_cast<std::size_t>(std::llround(static_cast<double>(durationSeconds)*wave->nSamplesPerSec))*wave->nBlockAlign;
            if(pcm_.size()>bytes) pcm_.resize(bytes);
        }
    }
    bass_=bassEnvelope(pcm_,wave->nSamplesPerSec,wave->nChannels);
    checked(engine_->CreateSourceVoice(&voice_,wave),"BGM source voice");
    checked(voice_->SetVolume(0),"BGM initial volume");
    XAUDIO2_BUFFER buffer{}; buffer.AudioBytes=static_cast<UINT32>(pcm_.size()); buffer.pAudioData=pcm_.data();
    buffer.Flags=XAUDIO2_END_OF_STREAM; buffer.LoopCount=durationSeconds>0?0:XAUDIO2_LOOP_INFINITE;
    checked(voice_->SubmitSourceBuffer(&buffer),"BGM loop buffer");
    sampleRate_=wave->nSamplesPerSec;
    const float decodedSeconds=static_cast<float>(static_cast<double>(pcm_.size())/wave->nBlockAlign/sampleRate_);
    playbackEndSeconds_=std::max(durationSeconds,decodedSeconds);
}
void Music::update(const std::filesystem::path& path,bool active,float volume,float dt,float startSeconds,float durationSeconds) {
    if(path!=attempted_ || startSeconds!=clipStart_ || durationSeconds!=clipDuration_) {
        clear(); attempted_=path; clipStart_=startSeconds; clipDuration_=durationSeconds; attemptedLoad_=false;
        status_="Ready when the matching aura is displayed";
    }
    if(active && !attemptedLoad_) {
        attemptedLoad_=true;
        try {load(path,startSeconds,durationSeconds); status_="Ready";}
        catch(const std::exception& e) {clear(); status_=e.what();}
    }
    if(!voice_) return;
    const float target=active?std::clamp(volume,0.f,1.f):0;
    gain_+=std::clamp(target-gain_,-std::max(dt,0.f)*2.f,std::max(dt,0.f)*2.f);
    if(active && !playing_) {checked(voice_->Start(),"Start BGM"); playing_=true;}
    checked(voice_->SetVolume(gain_),"BGM volume");
    if(!active && gain_<=.0001f && playing_) {checked(voice_->Stop(),"Stop BGM"); playing_=false;}
    status_=playing_?(active?(durationSeconds>0?"Playing / clip":"Playing / loop"):"Fading out"):"Stopped";
}
namespace {
std::vector<std::int16_t> synthesizeEffect(unsigned sound) {
    constexpr unsigned Rate=48000;
    constexpr double Pi=3.14159265358979323846;
    const double duration=sound?.30:.115;
    std::vector<std::int16_t> samples(static_cast<std::size_t>(duration*Rate));
    std::uint32_t noiseState=sound?0x81915a3du:0x2718b52fu;
    double phase=0,lowNoise=0,glassPhase=0;
    for(std::size_t i=0;i<samples.size();++i) {
        const double t=static_cast<double>(i)/Rate,u=t/duration;
        noiseState^=noiseState<<13; noiseState^=noiseState>>17; noiseState^=noiseState<<5;
        const double noise=(static_cast<double>(noiseState&0xffff)/32767.5)-1;
        phase+=2*Pi*(sound?(58+190*std::exp(-t*48)):(2100-1450*u))/Rate;
        glassPhase+=2*Pi*(2300-1550*u)/Rate;
        lowNoise+=.18*(noise-lowNoise);
        const double attack=std::min(1.,t/(sound?.0007:.003)),tail=std::max(0.,1-u);
        double sample;
        if(sound) {
            // A tight broadband crack, descending bass punch and bright digital debris.
            // Independent decays keep the collision clear even while fast volleys repeat.
            const double crack=.38*noise*std::exp(-t*95);
            const double body=.65*std::sin(phase)*std::exp(-t*20);
            const double burst=.80*lowNoise*std::exp(-t*17);
            const double debris=(.16*std::sin(glassPhase)+.08*std::sin(glassPhase*1.43))*std::exp(-t*25);
            sample=(crack+body+burst+debris)*attack*tail*tail*.84;
        } else {
            const double tone=.63*std::sin(phase)+.17*std::sin(phase*2.1)+.10*noise;
            sample=tone*attack*tail*tail*std::exp(-t*15)*.42;
        }
        samples[i]=static_cast<std::int16_t>(std::clamp(sample,-1.,1.)*32767);
    }
    return samples;
}
}
SoundEffects::~SoundEffects() {
    for(auto* voice:voices_) if(voice) voice->DestroyVoice();
    if(master_) master_->DestroyVoice(); engine_.Reset();
}
void SoundEffects::initialize() {
    attempted_=true;
    pcm_[0]=synthesizeEffect(0); pcm_[1]=synthesizeEffect(1);
    checked(XAudio2Create(&engine_,0,XAUDIO2_DEFAULT_PROCESSOR),"SFX XAudio2 startup");
    checked(engine_->CreateMasteringVoice(&master_),"SFX audio output device");
    WAVEFORMATEX format{}; format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=1;
    format.nSamplesPerSec=48000; format.wBitsPerSample=16; format.nBlockAlign=2; format.nAvgBytesPerSec=96000;
    for(auto& voice:voices_) {
        checked(engine_->CreateSourceVoice(&voice,&format),"SFX source voice");
        checked(voice->Start(),"SFX source startup");
    }
    ready_=true; status_="Ready / original synthesized effects";
}
void SoundEffects::play(unsigned sound,float volume) noexcept {
    try {
        if(!std::isfinite(volume) || volume<=0) return;
        if(!attempted_) initialize();
        if(!ready_) return;
        const auto now=std::chrono::steady_clock::now();
        if(now-lastPlay_[sound]<std::chrono::milliseconds(sound?110:65)) return;
        for(unsigned i=sound*2;i<sound*2+2;++i) {
            auto* voice=voices_[i]; XAUDIO2_VOICE_STATE state{}; voice->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if(state.BuffersQueued) continue;
            checked(voice->SetVolume(std::clamp(volume,0.f,1.f)*(sound?.35f:.28f)),"SFX volume");
            XAUDIO2_BUFFER buffer{}; buffer.Flags=XAUDIO2_END_OF_STREAM;
            buffer.AudioBytes=static_cast<UINT32>(pcm_[sound].size()*sizeof(std::int16_t));
            buffer.pAudioData=reinterpret_cast<const BYTE*>(pcm_[sound].data());
            checked(voice->SubmitSourceBuffer(&buffer),"SFX buffer");
            lastPlay_[sound]=now; ++counts_[sound]; return;
        }
    } catch(const std::exception& error) {
        ready_=false;
        try {status_=std::string("SFX unavailable / ")+error.what();} catch(...) {}
    } catch(...) { ready_=false; }
}
void SoundEffects::playShot(float volume) noexcept {play(0,volume);}
void SoundEffects::playImpact(float volume) noexcept {play(1,volume);}
}
