#include "platform/audio.hpp"
#include "render/renderer.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace aura {
using Microsoft::WRL::ComPtr;
void Music::clear() {
    if(voice_) { voice_->DestroyVoice(); voice_=nullptr; }
    pcm_.clear(); playing_=false; gain_=0;
}
Music::~Music() {
    clear(); if(master_) master_->DestroyVoice(); engine_.Reset(); if(mf_) MFShutdown();
}
void Music::load(const std::filesystem::path& path) {
    if(path.empty()) throw std::runtime_error("Choose the Singularity BGM file");
    if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)>128ull*1024*1024)
        throw std::runtime_error("BGM is missing or exceeds 128 MiB");
    if(!mf_) { checked(MFStartup(MF_VERSION,MFSTARTUP_LITE),"Media Foundation startup"); mf_=true; }
    if(!engine_) {
        checked(XAudio2Create(&engine_,0,XAUDIO2_DEFAULT_PROCESSOR),"XAudio2 startup");
    }
    if(!master_) {
        checked(engine_->CreateMasteringVoice(&master_),"Audio output device");
    }
    ComPtr<IMFSourceReader> reader; checked(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader),"Open BGM");
    checked(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE),"BGM stream selection");
    checked(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),TRUE),"BGM audio stream");
    ComPtr<IMFMediaType> request; checked(MFCreateMediaType(&request),"BGM media type");
    checked(request->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio),"BGM audio type");
    checked(request->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM),"BGM PCM type");
    checked(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),nullptr,request.Get()),"Decode BGM to PCM");
    ComPtr<IMFMediaType> format; checked(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),&format),"BGM output format");
    WAVEFORMATEX* raw{}; UINT length{};
    checked(MFCreateWaveFormatExFromMFMediaType(format.Get(),&raw,&length),"BGM wave format");
    std::unique_ptr<WAVEFORMATEX,decltype(&CoTaskMemFree)> wave(raw,CoTaskMemFree);
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
    if(pcm_.empty()) throw std::runtime_error("BGM contains no audio");
    checked(engine_->CreateSourceVoice(&voice_,wave.get()),"BGM source voice");
    checked(voice_->SetVolume(0),"BGM initial volume");
    XAUDIO2_BUFFER buffer{}; buffer.AudioBytes=static_cast<UINT32>(pcm_.size()); buffer.pAudioData=pcm_.data();
    buffer.Flags=XAUDIO2_END_OF_STREAM; buffer.LoopCount=XAUDIO2_LOOP_INFINITE;
    checked(voice_->SubmitSourceBuffer(&buffer),"BGM loop buffer");
}
void Music::update(const std::filesystem::path& path,bool active,float volume,float dt) {
    if(path!=attempted_) { clear(); attempted_=path; attemptedLoad_=false; status_="Ready when Singularity is displayed"; }
    if(active && !attemptedLoad_) {
        attemptedLoad_=true;
        try {load(path); status_="Ready";}
        catch(const std::exception& e) {clear(); status_=e.what();}
    }
    if(!voice_) return;
    const float target=active?std::clamp(volume,0.f,1.f):0;
    gain_+=std::clamp(target-gain_,-std::max(dt,0.f)*2.f,std::max(dt,0.f)*2.f);
    if(active && !playing_) {checked(voice_->Start(),"Start BGM"); playing_=true;}
    checked(voice_->SetVolume(gain_),"BGM volume");
    if(!active && gain_<=.0001f && playing_) {checked(voice_->Stop(),"Stop BGM"); playing_=false;}
    status_=playing_?(active?"Playing / loop":"Fading out"):"Stopped";
}
}
