#include "platform/profile_photo.hpp"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace aura {
namespace {
using Microsoft::WRL::ComPtr;
void photoCheck(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("Photo could not be decoded. Choose a PNG, JPEG or BMP image.");}
}
std::vector<std::uint8_t> readProfilePhoto(const std::filesystem::path& cached) {
    if(cached.empty() || !std::filesystem::exists(cached)) return {};
    const auto size=std::filesystem::file_size(cached);
    if(size>128*1024)throw std::runtime_error("Profile PNG is too large");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(cached,std::ios::binary); input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size));
    if(!input)throw std::runtime_error("Profile PNG could not be read");
    return bytes;
}
void saveProfilePhoto(const std::filesystem::path& cached,const std::vector<std::uint8_t>& bytes) {
    if(bytes.size()>128*1024)throw std::runtime_error("Profile PNG is too large");
    auto temporary=cached; temporary+=L".tmp";
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    output.close(); if(!output || !MoveFileExW(temporary.c_str(),cached.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Profile photo could not be saved");
}
std::vector<std::uint8_t> prepareProfilePhoto(const std::filesystem::path& source) {
    if(std::filesystem::file_size(source)>20*1024*1024)throw std::runtime_error("Choose a photo smaller than 20 MB");
    ComPtr<IWICImagingFactory> factory;
    photoCheck(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder; photoCheck(factory->CreateDecoderFromFilename(source.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));
    ComPtr<IWICBitmapFrameDecode> frame; photoCheck(decoder->GetFrame(0,&frame));
    UINT width{},height{}; photoCheck(frame->GetSize(&width,&height));
    if(!width || !height || width>8192 || height>8192 || static_cast<std::uint64_t>(width)*height>16'777'216)
        throw std::runtime_error("Choose a photo up to 16 megapixels and 8192 pixels per side");
    const auto side=std::min(width,height);
    WICRect crop{static_cast<INT>((width-side)/2),static_cast<INT>((height-side)/2),static_cast<INT>(side),static_cast<INT>(side)};
    ComPtr<IWICBitmapClipper> clip; photoCheck(factory->CreateBitmapClipper(&clip)); photoCheck(clip->Initialize(frame.Get(),&crop));
    ComPtr<IWICBitmapScaler> scaler; photoCheck(factory->CreateBitmapScaler(&scaler));
    photoCheck(scaler->Initialize(clip.Get(),160,160,WICBitmapInterpolationModeFant));
    ComPtr<IWICFormatConverter> pixels; photoCheck(factory->CreateFormatConverter(&pixels));
    photoCheck(pixels->Initialize(scaler.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    ComPtr<IStream> stream; photoCheck(CreateStreamOnHGlobal(nullptr,TRUE,&stream));
    ComPtr<IWICBitmapEncoder> encoder; photoCheck(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    photoCheck(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> output; photoCheck(encoder->CreateNewFrame(&output,nullptr)); photoCheck(output->Initialize(nullptr));
    photoCheck(output->SetSize(160,160)); WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA; photoCheck(output->SetPixelFormat(&format));
    photoCheck(output->WriteSource(pixels.Get(),nullptr)); photoCheck(output->Commit()); photoCheck(encoder->Commit());
    STATSTG stat{}; photoCheck(stream->Stat(&stat,STATFLAG_NONAME));
    if(stat.cbSize.QuadPart>128*1024)throw std::runtime_error("Converted profile PNG is too large");
    LARGE_INTEGER start{}; photoCheck(stream->Seek(start,STREAM_SEEK_SET,nullptr));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
    ULONG read{}; photoCheck(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read));
    if(read!=bytes.size())throw std::runtime_error("Converted profile PNG could not be read");
    return bytes;
}
}
