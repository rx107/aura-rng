#include "render/glb_model.hpp"
// ゲーム本体はvrm.cppがcgltfを定義します。この独立テストだけ、ここで実装を持ちます。
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace aura {
void checked(HRESULT result,const char* operation) {
    if(FAILED(result))throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(static_cast<unsigned long>(result)));
}
}
namespace {
using namespace DirectX;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void rejects(F&& action,const char* reason){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,reason);}
bool same(const XMFLOAT4X4& a,const XMFLOAT4X4& b,float tolerance=1e-5f) {
    const float* left=&a._11;const float* right=&b._11;
    for(std::size_t i=0;i<16;++i)if(std::abs(left[i]-right[i])>tolerance)return false;
    return true;
}
void finite(const XMFLOAT4X4& matrix) {
    for(float f:std::span<const float>(&matrix._11,16))check(std::isfinite(f),"GLB animation produced a nonfinite matrix");
}
struct Scratch {
    std::filesystem::path path;
    Scratch() {
        // 自分が新規作成した専用ディレクトリーだけを、テスト終了時に片付けます。
        const auto base=std::filesystem::temp_directory_path();
        for(unsigned i=0;i<100;++i) {
            auto candidate=base/(L"stars-rng-glb-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(i));
            if(std::filesystem::create_directory(candidate)){path=std::move(candidate);return;}
        }
        throw std::runtime_error("Cannot create GLB test scratch directory");
    }
    ~Scratch(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
void write(const std::filesystem::path& path,const std::vector<char>& bytes) {
    std::ofstream stream(path,std::ios::binary);stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(stream),"Cannot write GLB rejection fixture");
}
std::vector<char> withExternalBuffer() {
    std::string json=R"({"asset":{"version":"2.0"},"buffers":[{"uri":"https://example.invalid/mesh.bin","byteLength":12}],"scenes":[{"nodes":[]}],"scene":0})";
    while(json.size()%4)json+=' ';
    const std::array<std::uint32_t,5> header={0x46546c67u,2u,static_cast<std::uint32_t>(20+json.size()),static_cast<std::uint32_t>(json.size()),0x4e4f534au};
    std::vector<char> bytes(20+json.size());std::memcpy(bytes.data(),header.data(),20);std::memcpy(bytes.data()+20,json.data(),json.size());return bytes;
}
}
int wmain(int argc,wchar_t** argv) {
    bool com=false;
    try {
        check(argc==2,"Provide the bundled butterfly GLB path");
        aura::checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Test COM initialization");com=true;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        aura::checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr),"Test D3D11 WARP device");
        const std::filesystem::path source(argv[1]);
        auto model=aura::GlbModel::load(device.Get(),source);
        check(model->vertexCount()==29558 && model->drawCount()==3,"Butterfly mesh resource counts changed");
        check(model->nodeCount()==12 && model->animationTrackCount()==4,"Butterfly animation tracks were lost");
        check(std::abs(model->animationDuration()-1.25f)<1e-6f,"Butterfly animation duration changed");
        for(std::size_t node=0;node<model->nodeCount();++node) {
            const auto begin=model->nodeTransform(node,0),end=model->nodeTransform(node,1.25f);
            check(same(begin,end),"Butterfly animation does not loop at its authored endpoint");
            finite(model->nodeTransform(node,.3125f));finite(model->nodeTransform(node,.625f));
        }
        auto wingLocal=[&](std::size_t wing,float seconds) {
            const auto child=model->nodeTransform(wing,seconds),parent=model->nodeTransform(6,seconds);
            XMFLOAT4X4 local;XMStoreFloat4x4(&local,XMLoadFloat4x4(&child)*XMMatrixInverse(nullptr,XMLoadFloat4x4(&parent)));return local;
        };
        for(const std::size_t wing:{8u,10u}) {
            check(!same(wingLocal(wing,0),wingLocal(wing,.3125f),.001f),"Butterfly wing did not animate independently of root motion");
            check(!same(wingLocal(wing,.3125f),wingLocal(wing,.625f),.001f),"Butterfly wing became fixed midway through the clip");
        }
        finite(model->nodeTransform(8,std::numeric_limits<float>::quiet_NaN()));
        finite(model->nodeTransform(8,-1));
        rejects([&]{model->nodeTransform(model->nodeCount(),0);},"Invalid GLB node index was accepted");
        Scratch scratch;
        rejects([&]{aura::GlbModel::load(device.Get(),scratch.path/L"missing.glb");},"Missing GLB was accepted");
        write(scratch.path/L"truncated.glb",std::vector<char>{'g','l','T','F'});
        rejects([&]{aura::GlbModel::load(device.Get(),scratch.path/L"truncated.glb");},"Truncated GLB was accepted");
        write(scratch.path/L"external.glb",withExternalBuffer());
        rejects([&]{aura::GlbModel::load(device.Get(),scratch.path/L"external.glb");},"External buffer URI was accepted");

        // ファイルの構造を壊さずPOSITIONの実値だけNaNへ変え、入力値の検査も通します。
        std::ifstream input(source,std::ios::binary);std::vector<char> bytes(static_cast<std::size_t>(std::filesystem::file_size(source)));
        check(static_cast<bool>(input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()))),"Cannot read GLB fixture bytes");
        cgltf_options options{};cgltf_data* parsed{};
        check(cgltf_parse(&options,bytes.data(),bytes.size(),&parsed)==cgltf_result_success,"Cannot inspect GLB fixture");
        std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(parsed,cgltf_free);
        check(cgltf_load_buffers(&options,data.get(),nullptr)==cgltf_result_success,"Cannot inspect GLB buffers");
        const auto& primitive=data->meshes[0].primitives[0];cgltf_accessor* position=nullptr;
        for(std::size_t i=0;i<primitive.attributes_count;++i)if(primitive.attributes[i].type==cgltf_attribute_type_position)position=primitive.attributes[i].data;
        check(position && position->buffer_view && position->component_type==cgltf_component_type_r_32f,"Expected float position accessor");
        const auto* memory=cgltf_buffer_view_data(position->buffer_view)+position->offset;
        const auto offset=static_cast<std::size_t>(reinterpret_cast<const char*>(memory)-bytes.data());
        check(offset+sizeof(float)<=bytes.size(),"GLB fixture offset exceeded file");
        const float invalid=std::numeric_limits<float>::quiet_NaN();std::memcpy(bytes.data()+offset,&invalid,sizeof(invalid));
        write(scratch.path/L"nonfinite.glb",bytes);
        rejects([&]{aura::GlbModel::load(device.Get(),scratch.path/L"nonfinite.glb");},"Nonfinite GLB geometry was accepted");
        model.reset();device.Reset();CoUninitialize();com=false;
        std::cout<<"PASS: animated GLB / 29558 vertices / 3 meshes / 4 tracks / 1.25s loop / independent wings / WARP resources / malformed and external inputs rejected\n";
        return 0;
    } catch(const std::exception& error) {
        if(com)CoUninitialize();std::cerr<<error.what()<<'\n';return 1;
    }
}
