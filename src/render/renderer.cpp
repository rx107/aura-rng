#include "render/renderer.hpp"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace aura {
using namespace DirectX;
namespace {
constexpr unsigned MaxInstances = 2048;
constexpr float Pi = 3.14159265359f;
const D2D1_COLOR_F White = D2D1::ColorF(.91f, .95f, .98f);
const D2D1_COLOR_F Muted = D2D1::ColorF(.48f, .57f, .65f);
const D2D1_COLOR_F Mint = D2D1::ColorF(.55f, .96f, .82f);
const D2D1_COLOR_F Panel = D2D1::ColorF(.047f, .070f, .094f);
const D2D1_COLOR_F Border = D2D1::ColorF(.13f, .19f, .23f);
D2D1_COLOR_F tint(Color c, float alpha = 1) { return D2D1::ColorF(c.r, c.g, c.b, alpha); }
bool inside(float x, float y, float rx, float ry, float w, float h) { return x >= rx && x < rx+w && y >= ry && y < ry+h; }
std::string odds(const Definition& d) { return d.denominator ? "1 / " + formatNumber(d.denominator) : "COMMON"; }
constexpr char Shader[] = R"(
cbuffer Camera : register(b0) { row_major float4x4 viewProjection; float4 eyeAndTime; };
struct Input { float3 p:POSITION; float3 n:NORMAL; float4 a:INSTANCE0; float4 b:INSTANCE1; float4 c:INSTANCE2; float4 d:INSTANCE3; float4 color:COLOR0; };
struct Output { float4 p:SV_POSITION; float3 n:NORMAL; float4 color:COLOR0; float3 world:TEXCOORD0; };
Output vsMain(Input v) {
    Output o; row_major float4x4 model = float4x4(v.a,v.b,v.c,v.d);
    float4 world = mul(float4(v.p,1),model);
    o.p=mul(world,viewProjection); o.n=normalize(mul(float4(v.n,0),model).xyz);
    o.color=v.color; o.world=world.xyz; return o;
}
float4 psMain(Output i):SV_TARGET {
    float diffuse=max(0,dot(normalize(i.n),normalize(float3(-.4,.85,-.5))));
    float3 color=i.color.rgb*(.43+.57*diffuse)+i.color.rgb*i.color.a*.55;
    float fog=saturate((distance(i.world,eyeAndTime.xyz)-17)/35);
    return float4(lerp(color,float3(.042,.069,.098),fog*.7),1);
})";
}
void checked(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream message; message << operation << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ')';
        throw std::runtime_error(message.str());
    }
}
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(static_cast<std::size_t>(n), L' ');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(), n);
    return result;
}
void Renderer::initialize(HWND window, bool warp) {
    window_ = window; warp_ = warp;
    RECT client{}; GetClientRect(window_, &client);
    width_ = static_cast<unsigned>(client.right); height_ = static_cast<unsigned>(client.bottom);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = width_; desc.BufferDesc.Height = height_;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; desc.OutputWindow = window_; desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    auto create = [&](D3D_DRIVER_TYPE type) {
        return D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, 3, D3D11_SDK_VERSION, &desc, &swap_, &device_, nullptr, &context_);
    };
    HRESULT hr = create(warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE);
    if (FAILED(hr) && !warp) { swap_.Reset(); context_.Reset(); device_.Reset(); hr = create(D3D_DRIVER_TYPE_WARP); warp_ = true; }
    checked(hr, "Create Direct3D 11 device");
    ComPtr<IDXGIDevice> dxgi; checked(device_.As(&dxgi), "DXGI device");
    ComPtr<IDXGIAdapter> adapter; checked(dxgi->GetAdapter(&adapter), "DXGI adapter");
    DXGI_ADAPTER_DESC info{}; checked(adapter->GetDesc(&info), "Adapter description");
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, info.Description, -1, nullptr, 0, nullptr, nullptr);
    std::string name(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, info.Description, -1, name.data(), bytes, nullptr, nullptr);
    if (!name.empty()) name.pop_back(); adapterName_ = name;
    if (warp_) adapterName_ += " (WARP)";
    ComPtr<IDXGIFactory> factory; checked(adapter->GetParent(IID_PPV_ARGS(&factory)), "DXGI factory");
    checked(factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER), "Window association");
    checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf()), "Direct2D factory");
    checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(write_.GetAddressOf())), "DirectWrite factory");
    createPipeline(); createTargets();
    instances_.reserve(MaxInstances);
}
void Renderer::createPipeline() {
    ComPtr<ID3DBlob> vs, ps, errors;
    auto compile = [&](const char* entry, const char* profile, ID3DBlob** output) {
        const HRESULT hr = D3DCompile(Shader, sizeof(Shader)-1, "embedded.hlsl", nullptr, nullptr, entry, profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS, 0, output, &errors);
        if (FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        checked(hr, "Compile shader");
    };
    compile("vsMain", "vs_4_0", &vs); compile("psMain", "ps_4_0", &ps);
    checked(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_), "Vertex shader");
    checked(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_), "Pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"INSTANCE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,0,D3D11_INPUT_PER_INSTANCE_DATA,1},
        {"INSTANCE",1,DXGI_FORMAT_R32G32B32A32_FLOAT,1,16,D3D11_INPUT_PER_INSTANCE_DATA,1},
        {"INSTANCE",2,DXGI_FORMAT_R32G32B32A32_FLOAT,1,32,D3D11_INPUT_PER_INSTANCE_DATA,1},
        {"INSTANCE",3,DXGI_FORMAT_R32G32B32A32_FLOAT,1,48,D3D11_INPUT_PER_INSTANCE_DATA,1},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,64,D3D11_INPUT_PER_INSTANCE_DATA,1}};
    checked(device_->CreateInputLayout(elements, 7, vs->GetBufferPointer(), vs->GetBufferSize(), &layout_), "Input layout");
    const Vertex cube[] = {
        {{-.5f,-.5f,-.5f},{0,0,-1}},{{-.5f,.5f,-.5f},{0,0,-1}},{{.5f,.5f,-.5f},{0,0,-1}},{{.5f,-.5f,-.5f},{0,0,-1}},
        {{.5f,-.5f,.5f},{0,0,1}},{{.5f,.5f,.5f},{0,0,1}},{{-.5f,.5f,.5f},{0,0,1}},{{-.5f,-.5f,.5f},{0,0,1}},
        {{-.5f,-.5f,.5f},{-1,0,0}},{{-.5f,.5f,.5f},{-1,0,0}},{{-.5f,.5f,-.5f},{-1,0,0}},{{-.5f,-.5f,-.5f},{-1,0,0}},
        {{.5f,-.5f,-.5f},{1,0,0}},{{.5f,.5f,-.5f},{1,0,0}},{{.5f,.5f,.5f},{1,0,0}},{{.5f,-.5f,.5f},{1,0,0}},
        {{-.5f,.5f,-.5f},{0,1,0}},{{-.5f,.5f,.5f},{0,1,0}},{{.5f,.5f,.5f},{0,1,0}},{{.5f,.5f,-.5f},{0,1,0}},
        {{-.5f,-.5f,.5f},{0,-1,0}},{{-.5f,-.5f,-.5f},{0,-1,0}},{{.5f,-.5f,-.5f},{0,-1,0}},{{.5f,-.5f,.5f},{0,-1,0}}};
    std::array<unsigned short,36> index{};
    for (unsigned short face=0; face<6; ++face) {
        const auto b=static_cast<unsigned short>(face*4), j=static_cast<unsigned short>(face*6);
        index[j]=b; index[j+1]=b+1; index[j+2]=b+2; index[j+3]=b; index[j+4]=b+2; index[j+5]=b+3;
    }
    auto buffer = [&](UINT size, UINT bind, D3D11_USAGE usage, const void* data, ID3D11Buffer** output) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth=size; d.BindFlags=bind; d.Usage=usage;
        if (usage==D3D11_USAGE_DYNAMIC) d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=data;
        checked(device_->CreateBuffer(&d, data?&initial:nullptr, output), "Create buffer");
    };
    buffer(sizeof(cube),D3D11_BIND_VERTEX_BUFFER,D3D11_USAGE_IMMUTABLE,cube,&vertices_);
    buffer(sizeof(index),D3D11_BIND_INDEX_BUFFER,D3D11_USAGE_IMMUTABLE,index.data(),&indices_);
    buffer(sizeof(Instance)*MaxInstances,D3D11_BIND_VERTEX_BUFFER,D3D11_USAGE_DYNAMIC,nullptr,&instanceBuffer_);
    buffer(80,D3D11_BIND_CONSTANT_BUFFER,D3D11_USAGE_DEFAULT,nullptr,&constants_);
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_BACK; rd.DepthClipEnable=TRUE;
    checked(device_->CreateRasterizerState(&rd,&raster_), "Rasterizer state");
    D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=TRUE; dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc=D3D11_COMPARISON_LESS;
    checked(device_->CreateDepthStencilState(&dd,&depthState_), "Depth state");
}
void Renderer::createTargets() {
    scale_=std::min(static_cast<float>(width_)/1440.f,static_cast<float>(height_)/900.f);
    scale_=std::max(.1f,scale_); logicalW_=width_/scale_; logicalH_=height_/scale_;
    ComPtr<ID3D11Texture2D> back; checked(swap_->GetBuffer(0,IID_PPV_ARGS(&back)), "Back buffer");
    checked(device_->CreateRenderTargetView(back.Get(),nullptr,&target_), "Render target");
    D3D11_TEXTURE2D_DESC dd{}; dd.Width=width_; dd.Height=height_; dd.MipLevels=1; dd.ArraySize=1;
    dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count=1; dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> texture; checked(device_->CreateTexture2D(&dd,nullptr,&texture), "Depth texture");
    checked(device_->CreateDepthStencilView(texture.Get(),nullptr,&depth_), "Depth view");
    ComPtr<IDXGISurface> surface; checked(back.As(&surface), "DXGI surface");
    const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
    checked(d2dFactory_->CreateDxgiSurfaceRenderTarget(surface.Get(),props,&d2d_), "Direct2D surface");
    checked(d2d_->CreateSolidColorBrush(White,&brush_), "UI brush");
}
void Renderer::resize(unsigned width,unsigned height) {
    if (!width || !height || !swap_ || (width==width_ && height==height_)) return;
    context_->OMSetRenderTargets(0,nullptr,nullptr);
    brush_.Reset(); d2d_.Reset(); target_.Reset(); depth_.Reset(); context_->Flush();
    width_=width; height_=height;
    checked(swap_->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0), "Resize buffers"); createTargets();
}
void Renderer::box(float x,float y,float z,float sx,float sy,float sz,Color color,float rotation,float glow) {
    if (instances_.size()>=MaxInstances) return;
    Instance instance{};
    XMStoreFloat4x4(&instance.world,XMMatrixScaling(sx,sy,sz)*XMMatrixRotationY(rotation)*XMMatrixTranslation(x,y,z));
    instance.color={color.r,color.g,color.b,glow}; instances_.push_back(instance);
}
void Renderer::world(const Frame& f) {
    instances_.clear();
    const auto& d=f.roller.catalog()[f.progress.equipped];
    box(0,-.6f,0,16,1.0f,16,{.095f,.15f,.18f});
    box(0,-.12f,0,15.7f,.10f,15.7f,{.17f,.28f,.29f});
    for (int x=-7;x<=7;++x) for (int z=-7;z<=7;++z) {
        const bool path=std::abs(x)<=1 || std::abs(z)<=1;
        const float checker=((x+z)%2==0)?.035f:0;
        const Color c=path?Color{.27f+checker,.34f+checker,.37f+checker}:Color{.14f+checker,.30f+checker,.26f+checker};
        box(static_cast<float>(x),-.03f,static_cast<float>(z),.98f,.12f,.98f,c);
    }
    // A low central plinth and its emissive frame are built from the same cube mesh.
    box(0,.07f,0,4.6f,.14f,4.6f,{.20f,.29f,.32f});
    for (int i=0;i<4;++i) {
        const float a=i*Pi/2; box(std::sin(a)*2.25f,.16f,std::cos(a)*2.25f,4.5f,.025f,.035f,d.color,a,1);
    }
    const float trees[][2]={{-5,-4},{5,4},{-5,5},{5,-5},{-6,1},{6,0}};
    for (const auto& p:trees) {
        box(p[0],.85f,p[1],.4f,1.7f,.4f,{.21f,.20f,.18f});
        box(p[0],2.2f,p[1],1.9f,1.4f,1.9f,{.13f,.35f,.29f});
        box(p[0]-.1f,3.05f,p[1],1.3f,.6f,1.3f,{.21f,.45f,.34f});
    }
    for (float x : {-4.f,4.f}) for (float z : {-3.f,3.f}) {
        box(x,.55f,z,.5f,1.1f,.5f,{.31f,.37f,.39f});
        box(x,1.15f,z,.56f,.14f,.56f,{.50f,.94f,.80f},0,1);
    }
    // The avatar uses original primitive geometry; no external models or textures.
    const float bob=.025f*std::sin(f.time*2), swing=.14f*std::sin(f.walk*9);
    const float ca=std::cos(f.facing),sa=std::sin(f.facing);
    auto part=[&](float x,float y,float z,float sx,float sy,float sz,Color c) {
        box(f.avatarX+x*ca+z*sa,y+bob,f.avatarZ-x*sa+z*ca,sx,sy,sz,c,f.facing);
    };
    const Color skin{.95f,.76f,.47f}, shirt{.54f,.83f,.89f}, pants{.20f,.29f,.42f};
    part(-.27f,.62f,swing,.46f,.95f,.52f,pants); part(.27f,.62f,-swing,.46f,.95f,.52f,pants);
    part(0,1.56f,0,1.03f,.96f,.60f,shirt);
    part(-.77f,1.56f,-swing,.43f,.96f,.54f,skin); part(.77f,1.56f,swing,.43f,.96f,.54f,skin);
    part(0,2.42f,0,.73f,.73f,.73f,skin);
    part(-.16f,2.48f,-.375f,.085f,.10f,.02f,{.10f,.13f,.15f});
    part(.16f,2.48f,-.375f,.085f,.10f,.02f,{.10f,.13f,.15f});
    part(0,2.26f,-.375f,.24f,.045f,.02f,{.25f,.19f,.17f});
    const float t=f.time*d.speed;
    for (unsigned i=0;i<48;++i) {
        const float a=i*2*Pi/48+t*.5f;
        box(f.avatarX+std::cos(a)*d.radius,.25f,f.avatarZ+std::sin(a)*d.radius,.11f,.045f,.11f,d.color,a,1);
    }
    for (unsigned i=0;i<d.particles;++i) {
        const float u=static_cast<float>(i)/std::max(1u,d.particles),a=u*2*Pi*3+t;
        const float y=.4f+std::fmod(u*3.3f+f.time*.38f,3.3f);
        const float r=d.radius*(.70f+.25f*std::sin(y*1.4f+u*5));
        const float s=.045f+.045f*(.5f+.5f*std::sin(i*1.7f));
        box(f.avatarX+std::cos(a)*r,y,f.avatarZ+std::sin(a)*r,s,s*1.8f,s,d.color,a,1);
    }
    const float worldW=logicalW_-596,worldH=logicalH_-280;
    D3D11_VIEWPORT viewport{256*scale_,96*scale_,worldW*scale_,worldH*scale_,0,1}; context_->RSSetViewports(1,&viewport);
    const XMVECTOR eye=XMVectorSet(f.avatarX+std::sin(f.cameraYaw)*f.cameraDistance,
        f.cameraDistance*.57f,f.avatarZ-std::cos(f.cameraYaw)*f.cameraDistance,1);
    const auto view=XMMatrixLookAtLH(eye,XMVectorSet(f.avatarX,1.1f,f.avatarZ,1),XMVectorSet(0,1,0,0));
    const auto projection=XMMatrixPerspectiveFovLH(.80f,worldW/worldH,.1f,80);
    struct Camera { XMFLOAT4X4 vp; XMFLOAT4 eye; } camera{};
    XMStoreFloat4x4(&camera.vp,view*projection); XMStoreFloat4(&camera.eye,eye); camera.eye.w=f.time;
    context_->UpdateSubresource(constants_.Get(),0,nullptr,&camera,0,0);
    D3D11_MAPPED_SUBRESOURCE mapped{}; checked(context_->Map(instanceBuffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped), "Map instances");
    std::memcpy(mapped.pData,instances_.data(),instances_.size()*sizeof(Instance)); context_->Unmap(instanceBuffer_.Get(),0);
    ID3D11Buffer* streams[]={vertices_.Get(),instanceBuffer_.Get()}; const UINT strides[]={sizeof(Vertex),sizeof(Instance)},offsets[]={0,0};
    context_->IASetVertexBuffers(0,2,streams,strides,offsets); context_->IASetIndexBuffer(indices_.Get(),DXGI_FORMAT_R16_UINT,0);
    context_->IASetInputLayout(layout_.Get()); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(),nullptr,0); ID3D11Buffer* cb=constants_.Get(); context_->VSSetConstantBuffers(0,1,&cb);
    context_->PSSetShader(ps_.Get(),nullptr,0); context_->GSSetShader(nullptr,nullptr,0);
    context_->RSSetState(raster_.Get()); context_->OMSetDepthStencilState(depthState_.Get(),0);
    context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    context_->DrawIndexedInstanced(36,static_cast<UINT>(instances_.size()),0,0,0);
}
void Renderer::rect(float x,float y,float w,float h,D2D1_COLOR_F c,float r) {
    brush_->SetColor(c); const auto area=D2D1::RectF(x,y,x+w,y+h);
    if(r) d2d_->FillRoundedRectangle(D2D1::RoundedRect(area,r,r),brush_.Get()); else d2d_->FillRectangle(area,brush_.Get());
}
void Renderer::stroke(float x,float y,float w,float h,D2D1_COLOR_F c,float r) {
    brush_->SetColor(c); const auto area=D2D1::RectF(x,y,x+w,y+h);
    if(r) d2d_->DrawRoundedRectangle(D2D1::RoundedRect(area,r,r),brush_.Get(),1); else d2d_->DrawRectangle(area,brush_.Get(),1);
}
void Renderer::text(const std::wstring& value,float x,float y,float w,float h,float size,D2D1_COLOR_F color,bool bold) {
    auto font=std::find_if(fonts_.begin(),fonts_.end(),[&](const auto& f){return f.size==size && f.bold==bold;});
    if(font==fonts_.end()) {
        Font f{size,bold,{}};
        checked(write_->CreateTextFormat(L"Segoe UI",nullptr,bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"ja-jp",&f.format), "Text format");
        f.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); fonts_.push_back(std::move(f)); font=fonts_.end()-1;
    }
    brush_->SetColor(color);
    d2d_->DrawText(value.data(),static_cast<UINT32>(value.size()),font->format.Get(),D2D1::RectF(x,y,x+w,y+h),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::label(const std::string& value,float x,float y,float w,float h,float size,D2D1_COLOR_F color,bool bold) {
    text(widen(value),x,y,w,h,size,color,bold);
}
void Renderer::ui(const Frame& f) {
    const float w=logicalW_,h=logicalH_,right=w-314;
    const auto& defs=f.roller.catalog(); const auto& equipped=defs[f.progress.equipped];
    rect(0,0,w,80,D2D1::ColorF(.035f,.053f,.075f)); rect(24,24,32,32,Mint,8);
    text(L"A",31,25,25,30,22,Panel,true); text(L"AURA",68,20,160,34,26,White,true);
    text(L"RNG / EARLY ACCESS",69,53,240,20,10,Muted);
    rect(w-316,26,110,27,Panel,13); text(L"DX11  /  NATIVE",w-303,32,105,20,10,Mint,true);
    const auto fps=f.fps>0?std::to_string(static_cast<int>(f.fps)):"--"; label(fps+" FPS",w-187,30,75,25,13,Muted);
    text(L"v0.1.0",w-94,30,70,25,13,Muted);
    // Collection panel.
    rect(24,96,216,h-280,Panel,14); stroke(24,96,216,h-280,Border,14);
    text(L"COLLECTION",42,114,180,24,12,Muted,true);
    label(std::to_string(f.progress.discovered())+" / "+std::to_string(defs.size()),42,146,170,42,30,White,true);
    text(L"Auras discovered",42,187,170,22,12,Muted);
    rect(42,220,180,3,Border,1);
    rect(42,220,180*static_cast<float>(f.progress.discovered())/static_cast<float>(defs.size()),3,Mint,1);
    const int visible=std::max(1,static_cast<int>((h-280-150)/48));
    const int start=std::clamp(f.collectionScroll,0,std::max(0,static_cast<int>(defs.size())-visible));
    for (int row=0;row<visible && start+row<static_cast<int>(defs.size());++row) {
        const auto i=static_cast<std::size_t>(start+row); const float y=242+row*48.f;
        const bool owned=f.progress.counts[i]!=0;
        if(i==f.progress.equipped) rect(34,y-5,196,43,D2D1::ColorF(.09f,.15f,.17f),6);
        rect(43,y+5,6,6,owned?tint(defs[i].color):Border,2);
        label(defs[i].name,59,y,132,21,14,owned?White:Muted,i==f.progress.equipped);
        label(owned?"x"+formatNumber(f.progress.counts[i]):"UNDISCOVERED",59,y+22,150,18,10,Muted);
    }
    if(static_cast<int>(defs.size())>visible) {
        rect(234,244,2,h-448,Border,1);
        rect(234,244+(h-448)*static_cast<float>(start)/static_cast<float>(defs.size()),2,
            (h-448)*static_cast<float>(visible)/static_cast<float>(defs.size()),Mint,1);
    }
    // World labels sit over the Direct3D viewport.
    stroke(256,96,w-596,h-280,Border,14);
    text(L"SANCTUARY",276,116,180,25,12,White,true);
    text(L"01 / THE QUIET GARDEN",276,140,240,20,10,Muted);
    rect(w-487,114,130,26,D2D1::ColorF(.05f,.10f,.12f,.90f),13);
    text(L"BIOME  /  NORMAL",w-475,121,125,18,10,Mint);
    const float titleX=276,titleY=h-281;
    text(L"EQUIPPED AURA",titleX,titleY,300,20,10,Muted,true);
    label(equipped.name,titleX,titleY+23,w-650,45,32,tint(equipped.color),true);
    label(odds(equipped),titleX,titleY+67,w-650,25,12,Muted);
    // Session feed; file log keeps every result, the on-screen feed is bounded.
    rect(right,96,290,h-280,Panel,14); stroke(right,96,290,h-280,Border,14);
    text(L"ROLL HISTORY",right+20,114,240,24,12,Muted,true);
    rect(right+20,153,5,5,Mint,2); text(L"THIS SESSION",right+33,147,160,20,10,Mint);
    const int logVisible=std::max(1,static_cast<int>((h-280-122)/56));
    const int logStart=std::clamp(f.logScroll,0,std::max(0,static_cast<int>(f.logs.size())-logVisible));
    if(f.logs.empty()) {
        text(L"Your first aura is waiting.",right+20,207,250,25,16,White);
        text(L"Press ROLL to begin.",right+20,237,250,24,12,Muted);
    }
    for (int row=0;row<logVisible && row+logStart<static_cast<int>(f.logs.size());++row) {
        const auto& e=f.logs[f.logs.size()-1-static_cast<std::size_t>(row+logStart)]; const auto& d=defs[e.aura];
        const float y=182+row*56.f;
        rect(right+20,y+7,6,6,tint(d.color),2);
        label(d.name,right+36,y,172,22,15,tint(d.color),true);
        label("#"+formatNumber(e.roll),right+214,y+3,58,19,10,Muted);
        label(odds(d),right+36,y+24,170,18,10,Muted);
        label(e.time.size()>=19?e.time.substr(11,8):"",right+210,y+24,65,18,10,Muted);
        rect(right+20,y+49,250,1,D2D1::ColorF(.08f,.12f,.15f));
    }
    // A single stable action area provides both manual and unattended rolling.
    rect(24,h-166,w-48,100,Panel,14); stroke(24,h-166,w-48,100,Border,14);
    text(L"TOTAL ROLLS",44,h-148,140,20,10,Muted,true);
    label(formatNumber(f.progress.rolls),44,h-125,170,40,30,White,true);
    text(L"BEST DISCOVERY",230,h-148,180,20,10,Muted,true);
    label(f.progress.rolls?defs[f.progress.best].name:"Waiting for a roll",230,h-121,205,26,17,White,true);
    const float bx=w*.5f-105,by=h-146;
    const bool ready=f.cooldown<=0;
    const bool hover=inside(f.mouseX/scale_,f.mouseY/scale_,bx,by,210,62);
    rect(bx,by,210,62,ready?(hover?D2D1::ColorF(.68f,1,.88f):Mint):D2D1::ColorF(.18f,.31f,.32f),10);
    text(ready?L"ROLL":L"ROLLING...",bx+54,by+13,150,32,24,ready?Panel:White,true);
    if(!ready) rect(bx+10,by+56,190*(1-f.cooldown/1.4f),3,Mint,1);
    text(L"SPACE",bx+82,by+67,80,16,9,Muted);
    const float ax=bx+228;
    rect(ax,by,134,62,f.autoRoll?D2D1::ColorF(.10f,.22f,.20f):D2D1::ColorF(.08f,.12f,.15f),10);
    stroke(ax,by,134,62,f.autoRoll?Mint:Border,10);
    text(L"AUTO ROLL",ax+21,by+12,110,22,12,f.autoRoll?Mint:White,true);
    text(f.autoRoll?L"ON  /  F":L"OFF  /  F",ax+35,by+34,100,20,10,Muted);
    rect(w-179,by,130,62,D2D1::ColorF(.08f,.12f,.15f),10);
    text(L"OPEN LOGS",w-158,by+14,110,24,12,White,true);
    text(L"CSV / UTC",w-147,by+36,100,18,10,Muted);
    text(L"WASD / MOVE     RIGHT DRAG / ORBIT     WHEEL / ZOOM     CLICK OWNED AURA / EQUIP",24,h-39,w-320,23,11,Muted);
    text(L"LUCK  1.00x",w-136,h-39,112,22,11,Mint,true);
    if(!f.notice.empty()) {
        rect(276,174,w-650,56,D2D1::ColorF(.23f,.13f,.10f,.94f),8);
        label(f.notice,288,188,w-680,28,12,D2D1::ColorF(1,.75f,.56f));
    }
}
void Renderer::draw(const Frame& frame,bool present) {
    const float clear[]={.028f,.046f,.066f,1}; context_->ClearRenderTargetView(target_.Get(),clear);
    context_->ClearDepthStencilView(depth_.Get(),D3D11_CLEAR_DEPTH,1,0);
    ID3D11RenderTargetView* view=target_.Get(); context_->OMSetRenderTargets(1,&view,depth_.Get());
    world(frame); context_->OMSetRenderTargets(0,nullptr,nullptr);
    d2d_->BeginDraw(); d2d_->SetTransform(D2D1::Matrix3x2F::Scale(scale_,scale_)); ui(frame);
    checked(d2d_->EndDraw(), "Draw UI");
    if(present) checked(swap_->Present(1,0), "Present frame");
}
Hit Renderer::hit(float px,float py,const Frame& f) const {
    const float x=px/scale_,y=py/scale_,w=logicalW_,h=logicalH_,bx=w*.5f-105,by=h-146;
    if(inside(x,y,bx,by,210,62)) return {Action::Roll,0};
    if(inside(x,y,bx+228,by,134,62)) return {Action::Auto,0};
    if(inside(x,y,w-179,by,130,62)) return {Action::Logs,0};
    const int visible=std::max(1,static_cast<int>((h-280-150)/48));
    const int start=std::clamp(f.collectionScroll,0,std::max(0,static_cast<int>(f.roller.catalog().size())-visible));
    for(int row=0;row<visible && start+row<static_cast<int>(f.progress.counts.size());++row)
        if(inside(x,y,34,237+row*48.f,196,43)) return {Action::Equip,static_cast<std::size_t>(start+row)};
    return {};
}
int Renderer::region(float px,float py) const {
    const float x=px/scale_,y=py/scale_;
    if(inside(x,y,24,96,216,logicalH_-280)) return 1;
    if(inside(x,y,logicalW_-314,96,290,logicalH_-280)) return 2;
    return 0;
}
void Renderer::capture(const std::filesystem::path& path) {
    ComPtr<ID3D11Texture2D> back; checked(swap_->GetBuffer(0,IID_PPV_ARGS(&back)), "Capture back buffer");
    D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging; checked(device_->CreateTexture2D(&desc,nullptr,&staging), "Capture texture");
    context_->CopyResource(staging.Get(),back.Get());
    ComPtr<IWICImagingFactory> factory; checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)), "WIC factory");
    ComPtr<IWICStream> stream; checked(factory->CreateStream(&stream), "WIC stream");
    checked(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE), "Screenshot file");
    ComPtr<IWICBitmapEncoder> encoder; checked(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder), "PNG encoder");
    checked(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache), "PNG initialize");
    ComPtr<IWICBitmapFrameEncode> frame; checked(encoder->CreateNewFrame(&frame,nullptr), "PNG frame");
    checked(frame->Initialize(nullptr), "PNG frame initialize"); checked(frame->SetSize(width_,height_), "PNG size");
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA; checked(frame->SetPixelFormat(&format), "PNG format");
    D3D11_MAPPED_SUBRESOURCE mapped{}; checked(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped), "Capture map");
    const HRESULT hr=frame->WritePixels(height_,mapped.RowPitch,mapped.RowPitch*height_,static_cast<BYTE*>(mapped.pData));
    context_->Unmap(staging.Get(),0); checked(hr, "PNG pixels"); checked(frame->Commit(), "PNG frame commit"); checked(encoder->Commit(), "PNG commit");
}
} // namespace aura
