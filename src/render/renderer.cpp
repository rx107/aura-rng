#include "render/renderer.hpp"
#include "tuning/game.hpp"
#include "tuning/world.hpp"
#include "tuning/stages.hpp"
#include "tuning/ui.hpp"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
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
// GPU初期化はこのファイル前半。画面の絵は world()、UIは ui.cpp を見てください。
// 普段変えたい数値は src/tuning に集約し、DirectX用の頂点・バッファ設定と分けています。
constexpr unsigned MaxInstances = 4096;
constexpr float Pi = 3.14159265359f;
const D2D1_COLOR_F White = D2D1::ColorF(tuning::ui::Text[0],tuning::ui::Text[1],tuning::ui::Text[2]);
const D2D1_COLOR_F Muted = D2D1::ColorF(tuning::ui::MutedText[0],tuning::ui::MutedText[1],tuning::ui::MutedText[2]);
const D2D1_COLOR_F Mint = D2D1::ColorF(tuning::ui::Accent[0],tuning::ui::Accent[1],tuning::ui::Accent[2]);
const D2D1_COLOR_F Panel = D2D1::ColorF(tuning::ui::Panel[0],tuning::ui::Panel[1],tuning::ui::Panel[2]);
const D2D1_COLOR_F Border = D2D1::ColorF(tuning::ui::Border[0],tuning::ui::Border[1],tuning::ui::Border[2]);
D2D1_COLOR_F tint(Color c, float alpha = 1) { return D2D1::ColorF(c.r, c.g, c.b, alpha); }
bool inside(float x, float y, float rx, float ry, float w, float h) { return x >= rx && x < rx+w && y >= ry && y < ry+h; }
// 惑星ごとの空と遠景の色。RGBは0〜1。海は青、森は緑、火山は赤茶にしています。
Color stageSky(Biome biome) {
    switch(biome) {
    case Biome::Desert: return {.075f,.059f,.045f};
    case Biome::Ocean: return {.032f,.115f,.17f};
    case Biome::Forest: return {.022f,.075f,.054f};
    case Biome::Volcano: return {.10f,.028f,.025f};
    case Biome::Extra: return {.024f,.014f,.075f};
    default: return {.028f,.046f,.066f};
    }
}
Color stageFog(Biome biome) {
    switch(biome) {
    case Biome::Desert: return {.15f,.11f,.073f};
    case Biome::Ocean: return {.07f,.23f,.29f};
    case Biome::Forest: return {.031f,.12f,.079f};
    case Biome::Volcano: return {.15f,.046f,.032f};
    case Biome::Extra: return {.075f,.045f,.16f};
    default: return {.042f,.069f,.098f};
    }
}
constexpr char Shader[] = R"(
cbuffer Camera : register(b0) { row_major float4x4 viewProjection; float4 eyeAndTime; float4 auraLight; };
struct Input { float3 p:POSITION; float3 n:NORMAL; float4 a:INSTANCE0; float4 b:INSTANCE1; float4 c:INSTANCE2; float4 d:INSTANCE3; float4 color:COLOR0; };
struct Output { float4 p:SV_POSITION; float3 n:NORMAL; float4 color:COLOR0; float3 world:TEXCOORD0; float3 local:TEXCOORD1; };
Output vsMain(Input v) {
    Output o; row_major float4x4 model = float4x4(v.a,v.b,v.c,v.d);
    float4 world = mul(float4(v.p,1),model);
    o.p=mul(world,viewProjection); o.n=normalize(mul(float4(v.n,0),model).xyz);
    o.color=v.color; o.world=world.xyz; o.local=v.p; return o;
}
float4 psMain(Output i):SV_TARGET {
    float diffuse=max(0,dot(normalize(i.n),normalize(float3(-.4,.85,-.5))));
    float3 color=i.color.rgb*(.43+.57*diffuse)*auraLight.x+i.color.rgb*i.color.a*.55;
    float fog=saturate((distance(i.world,eyeAndTime.xyz)-17)/35);
    return float4(lerp(color,auraLight.yzw,fog*.7),1);
}
float4 holeMain(Output i):SV_TARGET {
    float2 q=i.local.xy*2; float r=length(q),angle=atan2(q.y,q.x);
    float ellipse=length(q*float2(1,4.4));
    float turbulence=.80+.20*sin(angle*17-eyeAndTime.w*2.8+r*24);
    float disk=exp(-abs(ellipse-.82)*36)*turbulence;
    float ring=exp(-abs(r-.43)*95);
    float halo=exp(-abs(r-.46)*14)*.25;
    float inside=1-smoothstep(.405,.425,r);
    float3 hot=lerp(float3(1,.33,.09),float3(.96,.76,1),.5+.5*sin(angle*2+eyeAndTime.w*.4));
    float3 color=hot*(disk*1.8+ring*1.9)+float3(.36,.10,.72)*halo;
    color=lerp(color,float3(.002,.001,.008),inside);
    float alpha=max(inside,saturate(disk+ring+halo));
    clip(alpha-.012); return float4(color,alpha);
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
    createPipeline(); createEffects(); createStar(); createTargets(); initializeDebug();
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
    ps.Reset(); compile("holeMain","ps_4_0",&ps);
    checked(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&holePs_),"Black hole shader");
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
    buffer(96,D3D11_BIND_CONSTANT_BUFFER,D3D11_USAGE_DEFAULT,nullptr,&constants_);
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
void Renderer::updateVrm(const Frame& f) {
    const auto& s=f.presentation.settings;
    if(vrmFuture_.valid() && vrmFuture_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try { vrm_=vrmFuture_.get(); loadedVrm_=loadingVrm_; vrmStatus_=vrm_->title+" / "+formatNumber(vrm_->vertexCount())+" vertices"; }
        catch(const std::exception& e) { vrmStatus_=e.what(); }
    }
    if(s.avatar!=Avatar::Vrm || vrmFuture_.valid()) return;
    if(requestedVrm_==s.vrmPath) return;
    requestedVrm_=s.vrmPath;
    if(s.vrmPath.empty()) { vrmStatus_="Choose a VRM file"; return; }
    loadingVrm_=s.vrmPath; vrmStatus_="Loading VRM...";
    const auto device=device_; const auto path=std::filesystem::path(widen(s.vrmPath));
    vrmFuture_=std::async(std::launch::async,[device,path] {
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        struct Cleanup { HRESULT result; ~Cleanup(){if(SUCCEEDED(result)) CoUninitialize();} } cleanup{com};
        checked(com,"VRM loader COM"); return VrmAvatar::load(device.Get(),path);
    });
}
void Renderer::world(const Frame& f) {
    redJohnInstances_=0;
    // 1. 前フレームの絵を消す → 2. ステージ → 3. アバター → 4. 表示オーラ → 5. GPUへ送る。
    // この関数は描画専用です。所持数・ロール回数を書き換えません。
    instances_.clear(); starInstances_.clear(); pixelationInstances_.clear(); equinoxOrbs_.clear(); equinoxTitles_.clear(); nanallyInstances_.clear(); illusionaryInk_.clear(); frutigerInstances_.clear(); holeVisible_=false;
    const auto& d=f.roller.catalog()[f.presentation.shown(f.progress)];
    const unsigned level=f.presentation.level(f.progress,f.roller.catalog());
    const auto biome=f.presentation.settings.biome;
    const bool desert=biome==Biome::Desert, ocean=biome==Biome::Ocean;
    const bool forest=biome==Biome::Forest, volcano=biome==Biome::Volcano, extra=biome==Biome::Extra;
    const float layout=tuning::world::PropLayoutScale;
    const float islandRadius=StageHalfExtent*tuning::stages::OceanIslandRadiusRatio;
    const Color baseColor=desert?Color{.38f,.23f,.13f}:ocean?Color{.045f,.17f,.23f}:
        forest?Color{.065f,.12f,.075f}:volcano?Color{.075f,.062f,.069f}:Color{.095f,.15f,.18f};
    if(extra) {
        // 初回だけ、丸い星の輪郭を水平に細かく切った床の寸法を計算します。
        // 1枚の四角い床を残さないので、上から見ても横から見ても黄色い星の形になります。
        // 形の変更は tuning/stages.hpp の ExtraOuter/InnerRadiusRatio が入口です。
        static const auto slices=[] {
            constexpr unsigned Points=320;
            std::array<XMFLOAT2,Points> outline{};
            for(unsigned i=0;i<Points;++i) {
                const float a=i*2*Pi/Points,r=extraStageRadius(a,StageHalfExtent);
                outline[i]={std::cos(a)*r,std::sin(a)*r};
            }
            std::vector<std::array<float,3>> result;
            const float step=2*StageHalfExtent/tuning::stages::ExtraFloorSlices;
            for(unsigned row=0;row<tuning::stages::ExtraFloorSlices;++row) {
                const float z=-StageHalfExtent+(row+.5f)*step;
                std::vector<float> crossings;
                for(unsigned i=0;i<Points;++i) {
                    const auto a=outline[i],b=outline[(i+1)%Points];
                    if((a.y<=z && b.y>z) || (b.y<=z && a.y>z))
                        crossings.push_back(a.x+(z-a.y)*(b.x-a.x)/(b.y-a.y));
                }
                std::sort(crossings.begin(),crossings.end());
                for(std::size_t i=0;i+1<crossings.size();i+=2)
                    result.push_back({(crossings[i]+crossings[i+1])*.5f,z,crossings[i+1]-crossings[i]});
            }
            return result;
        }();
        const float step=2*StageHalfExtent/tuning::stages::ExtraFloorSlices;
        for(const auto& slice:slices) {
            const float shade=.5f+.5f*std::cos(slice[1]*.055f);
            box(slice[0],-.09f-tuning::stages::ExtraPlanetThickness*.5f,slice[1],slice[2],
                tuning::stages::ExtraPlanetThickness,step+.002f,{.88f,.49f,.075f});
            box(slice[0],-.03f,slice[1],slice[2],.12f,step+.002f,
                {1.f,.79f+shade*.13f,.16f+shade*.12f},0,.17f);
        }
    } else {
    box(0,-.6f,0,StageHalfExtent*2,1.0f,StageHalfExtent*2,baseColor);
    box(0,ocean?-.25f:-.12f,0,StageHalfExtent*2-.3f,.10f,StageHalfExtent*2-.3f,
        desert?Color{.68f,.46f,.24f}:ocean?Color{.075f,.35f,.44f}:
        forest?Color{.12f,.23f,.13f}:volcano?Color{.15f,.12f,.13f}:Color{.17f,.28f,.29f});
    // 半径から枚数を計算するため、HalfExtentだけで床全体の広さを変更できます。
    for (int x=-tuning::world::TileHalfCount;x<=tuning::world::TileHalfCount;++x)
    for (int z=-tuning::world::TileHalfCount;z<=tuning::world::TileHalfCount;++z) {
        const bool path=std::abs(x)<=tuning::world::PathHalfTiles || std::abs(z)<=tuning::world::PathHalfTiles;
        const float checker=((x+z)%2==0)?.035f:0;
        const float tileX=static_cast<float>(x)*tuning::world::TileSize,tileZ=static_cast<float>(z)*tuning::world::TileSize;
        Color c=desert?(path?Color{.72f+checker,.52f+checker,.31f+checker}:Color{.83f+checker,.64f+checker,.39f+checker}):
            (path?Color{.27f+checker,.34f+checker,.37f+checker}:Color{.14f+checker,.30f+checker,.26f+checker});
        float tileY=-.03f;
        if(ocean) {
            // 島の外側は青い水面。十字の桟橋は同じ高さなので中央から歩いて渡れます。
            const float radius=std::sqrt(tileX*tileX+tileZ*tileZ);
            const float coast=islandRadius+std::sin(std::atan2(tileZ,tileX)*5)*layout;
            const bool dock=std::abs(tileX)<tuning::world::TileSize*.75f || std::abs(tileZ)<tuning::world::TileSize*.75f;
            if(radius<coast) c=radius<coast*.72f?Color{.20f+checker,.43f+checker,.29f+checker}:Color{.83f+checker,.75f+checker,.49f+checker};
            else if(dock) c={.42f+checker,.29f+checker,.19f+checker};
            else {
                const float wave=.5f+.5f*std::sin(tileX*.6f+tileZ*.35f+f.time*tuning::stages::OceanWaveSpeed);
                c={.07f+wave*.025f,.32f+wave*.07f,.44f+wave*.09f};
                tileY=-.21f+(wave-.5f)*tuning::stages::OceanWaveHeight;
            }
        } else if(forest) c=path?Color{.29f+checker,.25f+checker,.16f+checker}:Color{.105f+checker,.26f+checker,.135f+checker};
        else if(volcano) c=path?Color{.24f+checker,.21f+checker,.22f+checker}:Color{.105f+checker,.105f+checker,.135f+checker};
        box(tileX,tileY,tileZ,tuning::world::TileSize-.02f,.12f,tuning::world::TileSize-.02f,c);
    }
    }
    // リスポーン台をロケットの発着台として共用。中心は stages.hpp から取得します。
    // 台を二重に描かないので、同じ高さの面がちらつくこともありません。
    box(tuning::stages::RespawnX,.07f,tuning::stages::RespawnZ,4.6f,.14f,4.6f,desert?Color{.56f,.38f,.24f}:ocean?Color{.51f,.46f,.30f}:
        forest?Color{.23f,.30f,.22f}:volcano?Color{.25f,.22f,.25f}:extra?Color{.91f,.67f,.20f}:Color{.20f,.29f,.32f});
    for (int i=0;i<4;++i) {
        const float a=i*Pi/2;
        box(tuning::stages::RespawnX+std::sin(a)*2.25f,.16f,tuning::stages::RespawnZ+std::cos(a)*2.25f,4.5f,.025f,.035f,d.color,a,1);
    }
    // 床を細かくして木を最大まで増やした場合も、アバターやオーラが消えないようにします。
    // 新しい景観の飾りだけを先に止め、後で描く機体/人/最大160粒子/最大128点の輪に512箱を確保。
    const auto sceneryRoom=[&](std::size_t cubes) {return instances_.size()+cubes<=MaxInstances-512;};
    const float trees[][2]={{-6,-5},{6,5},{-6,6},{6,-6},{-8,1},{8,0},
        {-18,-18},{18,18},{-18,18},{18,-18},{-26,-8},{26,8},{-8,26},{8,-26},{-26,25},{26,-25},{-12,-24},{12,24}};
    for (const auto& p:trees) {
        if(ocean || forest || volcano || extra) break;
        // 外周の木/サボテンもステージの広さに合わせて配置だけ拡大します。
        const float treeX=p[0]*tuning::world::PropLayoutScale;
        const float treeZ=p[1]*tuning::world::PropLayoutScale;
        if(desert) {
            const float tall=2.2f+.35f*std::sin(p[0]);
            box(treeX,tall*.5f,treeZ,.52f,tall,.50f,{.22f,.40f,.25f});
            box(treeX-.52f,1.15f,treeZ,.62f,.30f,.32f,{.27f,.46f,.29f});
            box(treeX-.75f,1.45f,treeZ,.30f,.85f,.32f,{.27f,.46f,.29f});
            box(treeX+.50f,.85f,treeZ,.62f,.28f,.32f,{.25f,.43f,.27f});
            box(treeX+.74f,1.03f,treeZ,.30f,.62f,.32f,{.25f,.43f,.27f});
            box(treeX+.8f,.15f,treeZ+.6f,.65f,.3f,.55f,{.55f,.37f,.24f},.3f);
            continue;
        }
        box(treeX,.85f,treeZ,.4f,1.7f,.4f,{.21f,.20f,.18f});
        box(treeX,2.2f,treeZ,1.9f,1.4f,1.9f,{.13f,.35f,.29f});
        box(treeX-.1f,3.05f,treeZ,1.3f,.6f,1.3f,{.21f,.45f,.34f});
    }
    // 海: ヤシ、桟橋、白い波。波は少数の細い箱を動かすだけで、重い流体計算はしません。
    if(ocean) {
        for(unsigned i=0;i<7;++i) {
            const float a=i*2.399963f;
            const float radius=islandRadius*(i<3?.56f:.81f);
            const float x=std::cos(a)*radius,z=std::sin(a)*radius;
            if(std::abs(x)<2.f || std::abs(z)<2.f || std::hypot(x-tuning::stages::RocketX,z-tuning::stages::RocketZ)<3.f) continue;
            if(!sceneryRoom(11)) break;
            for(int trunk=0;trunk<4;++trunk)
                box(x+trunk*.10f, .45f+trunk*.70f,z,.42f,.78f,.42f,{.40f,.28f,.16f},a);
            for(int leaf=0;leaf<6;++leaf) {
                const float angle=leaf*Pi/3+a;
                box(x+.3f+std::cos(angle)*1.f,3.f,z+std::sin(angle)*1.f,2.4f,.13f,.55f,{.17f,.46f,.28f},-angle);
            }
            box(x+.25f,2.65f,z,.65f,.45f,.65f,{.37f,.28f,.15f},a);
        }
        for(unsigned i=0;i<tuning::stages::OceanWaveCount && sceneryRoom(1);++i) {
            const float a=i*2.399963f;
            const float radius=islandRadius+2.f+std::fmod(i*3.71f, std::max(1.f,StageHalfExtent-islandRadius-4.f));
            const float x=std::cos(a)*radius,z=std::sin(a)*radius;
            if(std::abs(x)<2.f || std::abs(z)<2.f) continue;
            const float phase=f.time*tuning::stages::OceanWaveSpeed+i;
            box(x+std::sin(phase)*.35f,-.085f+std::sin(phase)*tuning::stages::OceanWaveHeight,z,
                1.6f+.5f*std::sin(i*2.f),.025f,.10f,{.49f,.84f,.87f},-a+.4f,.45f);
        }
        // 桟橋の杭と板の継ぎ目。プレイヤーの道の中央は空けたままです。
        for(int i=0;i<14;++i) {
            const float along=(i-6.5f)*(StageHalfExtent/7.f);
            if(std::abs(along)<islandRadius) continue;
            if(!sceneryRoom(6)) break;
            for(float side:{-1.f,1.f}) {
                box(side*1.12f,.28f,along,.16f,.80f,.16f,{.31f,.22f,.16f});
                box(along,.28f,side*1.12f,.16f,.80f,.16f,{.31f,.22f,.16f});
            }
            box(0,.04f,along,2.f,.025f,.06f,{.24f,.17f,.12f});
            box(along,.04f,0,.06f,.025f,2.f,{.24f,.17f,.12f});
        }
    }
    // 森: 決まった種から配置を計算するため、毎フレーム同じ森になります。
    // 木は中央7単位と十字路、ロケット発着場を避けて置きます。
    if(forest) {
        for(unsigned i=0;i<tuning::stages::ForestTreeCount;++i) {
            const float angle=i*2.399963f;
            const float radius=7.5f+std::sqrt(static_cast<float>(i)/tuning::stages::ForestTreeCount)*(StageHalfExtent-9.f);
            const float x=std::cos(angle)*radius,z=std::sin(angle)*radius;
            if(std::abs(x)<3.1f || std::abs(z)<3.1f || std::hypot(x-tuning::stages::RocketX,z-tuning::stages::RocketZ)<3.3f) continue;
            if(!sceneryRoom(5)) break;
            const float height=tuning::stages::ForestTreeHeight*(.78f+.32f*(.5f+.5f*std::sin(i*2.7f)));
            const float sway=std::sin(f.time*.35f+i)*.055f;
            box(x,height*.38f,z,.60f,height*.76f,.60f,{.20f,.16f,.115f},angle);
            box(x,height*.70f,z,3.1f,height*.36f,3.1f,{.065f,.24f,.135f},angle+sway);
            box(x-.18f,height*.91f,z,2.55f,height*.27f,2.55f,{.10f,.34f,.17f},angle+.4f+sway);
            box(x+.12f,height*1.065f,z,1.7f,height*.18f,1.7f,{.17f,.42f,.22f},angle+sway);
            box(x,.10f,z,1.3f,.20f,1.2f,{.14f,.23f,.12f},angle);
        }
        for(unsigned i=0;i<45;++i) {
            const float a=i*2.399963f,r=5.8f+std::fmod(i*3.21f,StageHalfExtent-8.f);
            const float x=std::cos(a)*r,z=std::sin(a)*r;
            if(std::abs(x)<2.7f || std::abs(z)<2.7f) continue;
            if(!sceneryRoom(i%3==0?4:2)) break;
            box(x,.22f,z,.75f,.44f,.85f,{.13f,.34f,.15f},a);
            box(x+.34f,.38f,z,.11f,.48f,.56f,{.24f,.48f,.20f},a+.8f);
            if(i%3==0) {box(x-.3f,.16f,z+.2f,.07f,.30f,.07f,{.65f,.63f,.45f});box(x-.3f,.33f,z+.2f,.33f,.12f,.30f,{.75f,.28f,.21f},a);}
        }
        for(unsigned i=0;i<tuning::stages::ForestFireflyCount && sceneryRoom(1);++i) {
            const float a=i*2.399963f+f.time*tuning::stages::ForestFireflySpeed*.12f;
            const float r=3.8f+std::fmod(i*1.47f,StageHalfExtent*.72f);
            const float pulse=.5f+.5f*std::sin(f.time*1.8f+i*1.7f);
            box(std::cos(a)*r,.6f+std::fmod(i*.43f,3.f)+std::sin(f.time*tuning::stages::ForestFireflySpeed+i)*.24f,
                std::sin(a)*r,.045f+pulse*.025f,.045f+pulse*.025f,.045f+pulse*.025f,{.71f,1.f,.30f},a,1.4f);
        }
    }
    // 火山: 中央の平らな玄武岩広場を残し、溶岩の枝と遠景の火口を描きます。
    if(volcano) {
        const float pulse=.65f+.35f*std::sin(f.time*tuning::stages::LavaPulseSpeed);
        for(int branch=0;branch<5 && sceneryRoom(1);++branch) for(int segment=0;segment<19 && sceneryRoom(1);++segment) {
            const float r=6.f+segment*(StageHalfExtent-8.f)/19.f;
            const float a=branch*2*Pi/5+.26f*std::sin(r*.25f+branch);
            box(std::cos(a)*r,.065f,std::sin(a)*r,.58f+.18f*std::sin(static_cast<float>(segment)),.06f,1.7f,{1.f,.17f+pulse*.13f,.035f},-a+Pi/2,1.4f);
        }
        for(int i=0;i<28;++i) {
            const float a=i*2.399963f,r=8.f+std::fmod(i*3.13f,StageHalfExtent-11.f);
            const float x=std::cos(a)*r,z=std::sin(a)*r;
            if(std::abs(x)<3.f || std::abs(z)<3.f) continue;
            if(!sceneryRoom(2)) break;
            const float h=.65f+1.8f*(.5f+.5f*std::sin(i*2.1f));
            box(x,h*.5f,z,1.05f,h,1.2f,{.13f,.125f,.17f},a);
            box(x+.25f,h+.14f,z,.85f,.28f,.88f,{.22f,.20f,.25f},a+.2f);
        }
        const float mountainX=StageHalfExtent*tuning::stages::VolcanoXRatio,mountainZ=StageHalfExtent*tuning::stages::VolcanoZRatio;
        // 層を上に重ねて裾の広い山を作り、最上部だけ輪にして火口を空けます。
        for(int layer=0;layer<8;++layer) {
            if(!sceneryRoom(layer<6?1:16)) break;
            const float h=tuning::stages::VolcanoHeight/8.f,r=(7.6f-layer*.63f)*layout;
            const float y=layer*h+h*.5f;
            if(layer<6) box(mountainX,y,mountainZ,r*2,h+.04f,r*2,{.14f+layer*.009f,.12f+layer*.008f,.15f+layer*.009f},.18f);
            else for(int side=0;side<16;++side) {
                const float angle=side*Pi/8;
                box(mountainX+std::cos(angle)*r,y,mountainZ+std::sin(angle)*r,1.35f*layout,h+.06f,1.35f*layout,{.27f,.19f,.19f},angle);
            }
        }
        if(sceneryRoom(1)) box(mountainX,tuning::stages::VolcanoHeight*.77f,mountainZ,5.2f*layout,.16f,5.2f*layout,{1.f,.24f,.045f},.18f,2.f);
        for(unsigned i=0;i<tuning::stages::VolcanoEmberCount && sceneryRoom(1);++i) {
            const float a=i*2.399963f,r=3.2f+std::fmod(i*2.3f,StageHalfExtent*.77f);
            const float y=.5f+std::fmod(i*.71f+f.time*tuning::stages::VolcanoEmberRiseSpeed,6.f);
            const float size=.035f+.04f*(.5f+.5f*std::sin(i*1.7f));
            box(std::cos(a)*r+std::sin(f.time*.4f+i)*.35f,y,std::sin(a)*r,size,size*1.7f,size,{1.f,.43f,.09f},a,1.5f);
        }
    }
    if(extra) {
        // 小さな星は歩く場所をふさがない外側へ配置。少数の箱だけで漂う宇宙を作ります。
        for(unsigned i=0;i<tuning::stages::ExtraSkyStarCount && sceneryRoom(3);++i) {
            const float angle=i*2.399963f;
            const float radius=extraStageRadius(angle,StageHalfExtent)+4.f+std::fmod(i*2.71f,10.f);
            const float x=std::cos(angle)*radius,z=std::sin(angle)*radius;
            const float y=1.2f+std::fmod(i*.71f,10.f)+std::sin(f.time*tuning::stages::ExtraSkyStarSpeed+i)*.35f;
            const float size=.09f+std::fmod(i*.031f,.10f);
            const Color color=i%3==0?Color{.74f,.79f,1.f}:Color{1.f,.91f,.50f};
            box(x,y,z,size,size,size,color,angle,1.f);
            box(x,y,z,size*2.8f,size*.26f,size*.26f,color,angle,.55f);
            box(x,y,z,size*.26f,size*2.8f,size*.26f,color,angle,.55f);
        }
        // 星の5つの先端を小さな金の輪で示します。中央の大きいオーラを隠さない低い飾りです。
        for(unsigned tip=0;tip<5;++tip) {
            const float angle=Pi/2+tip*2*Pi/5,r=StageHalfExtent*.75f;
            for(unsigned mark=0;mark<16 && sceneryRoom(1);++mark) {
                const float a=mark*2*Pi/16;
                box(std::cos(angle)*r+std::cos(a)*1.25f,.08f,std::sin(angle)*r+std::sin(a)*1.25f,
                    .30f,.035f,.08f,{1.f,.96f,.63f},-a+Pi/2,.4f);
            }
        }
    }
    for (float x : {-4.f,4.f}) for (float z : {-3.f,3.f}) {
        box(x,.55f,z,.5f,1.1f,.5f,{.31f,.37f,.39f});
        box(x,1.15f,z,.56f,.14f,.56f,volcano?Color{1.f,.51f,.22f}:forest?Color{.79f,.96f,.46f}:extra?Color{1.f,.91f,.51f}:Color{.50f,.94f,.80f},0,1);
    }
    // 全惑星共通の小型ロケット。リスポーンと同じXZに置き、乗降スペースの上に機体を浮かべます。
    // tuning/stages.hpp の RespawnX / RespawnZ / RocketScale / RocketBoardingClearance が調整入口。
    const float rocketX=tuning::stages::RocketX,rocketZ=tuning::stages::RocketZ,rocketScale=tuning::stages::RocketScale;
    // 発着場のXZは動かさず、大きいオーラの頭上を機体で隠さないように空けます。
    const bool tallAura=d.id=="illusionary" || d.id=="nanally" || d.id=="breakthrough" || d.id=="equinox" || d.id=="singularity";
    const float nearDock=tallAura?std::clamp((6.f-std::hypot(f.avatarX-rocketX,f.avatarZ-rocketZ))/4.f,0.f,1.f):0.f;
    const float rocketClearance=std::lerp(tuning::stages::RocketBoardingClearance,tuning::stages::RocketEffectClearance,nearDock);
    const Color rocketAccent{.22f,.65f,.85f};
    for(int i=0;i<16;++i) {
        const float a=i*Pi/8;
        box(rocketX+std::cos(a)*1.82f,.18f,rocketZ+std::sin(a)*1.82f,.53f,.035f,.075f,{.38f,.86f,1.f},-a+Pi/2,1.f);
    }
    auto rocketPart=[&](float x,float y,float z,float sx,float sy,float sz,Color color,float angle=0.f,float glow=0.f) {
        box(rocketX+x*rocketScale,.24f+rocketClearance+y*rocketScale,rocketZ+z*rocketScale,
            sx*rocketScale,sy*rocketScale,sz*rocketScale,color,angle,glow);
    };
    rocketPart(0,1.85f,0,1.15f,2.6f,1.15f,{.82f,.86f,.89f},Pi/4);
    rocketPart(0,1.85f,0,1.34f,2.6f,.78f,{.87f,.91f,.93f});
    rocketPart(0,.61f,0,1.26f,.22f,1.26f,{.23f,.30f,.38f},Pi/4);
    rocketPart(0,1.19f,0,1.37f,.22f,.82f,rocketAccent);
    for(int ring=0;ring<6;++ring) {
        const float width=1.24f-ring*.18f;
        rocketPart(0,3.19f+ring*.14f,0,width,.17f,width,ring<2?Color{.85f,.90f,.93f}:rocketAccent,Pi/4);
    }
    rocketPart(0,2.45f,-.62f,.55f,.64f,.12f,{.12f,.20f,.29f});
    rocketPart(0,2.45f,-.69f,.36f,.43f,.035f,{.35f,.88f,1.f},0,.75f);
    for(int fin=0;fin<4;++fin) {
        const float a=fin*Pi/2;
        rocketPart(std::cos(a)*.82f,.65f,std::sin(a)*.82f,.70f,.85f,.20f,rocketAccent,-a);
        rocketPart(std::cos(a)*.95f,.18f,std::sin(a)*.95f,.65f,.16f,.38f,{.19f,.25f,.32f},-a);
    }
    box(rocketX-2.05f,.54f,rocketZ-1.18f,.50f,1.05f,.40f,{.19f,.27f,.34f});
    box(rocketX-2.05f,1.14f,rocketZ-1.18f,.74f,.20f,.55f,{.25f,.65f,.77f},0,.55f);
    // The lightweight primitive avatar remains available while a VRM is loading.
    const bool useVrm=f.presentation.settings.avatar==Avatar::Vrm && vrmReady();
    const bool floating=d.id=="singularity";
    const bool pixelated=d.id=="pixelation";
    const bool equinox=d.id=="equinox";
    const bool breakthrough=d.id=="breakthrough";
    const bool nanally=d.id=="nanally";
    const bool illusionary=d.id=="illusionary";
    const bool frutiger=d.id=="frutiger_aero"; frutigerVisible_=frutiger;
    const bool redJohn=d.id=="red_john";
    const bool desertAura=d.id=="dune" || d.id=="mirage" || d.id=="pharaoh";
    pixelationVisible_=pixelated; equinoxVisible_=equinox; breakthroughVisible_=breakthrough;
    nanallyVisible_=nanally;
    illusionaryVisible_=illusionary; desertAuraVisible_=desertAura;
    const float effectTime=f.effectTime>=0?f.effectTime:pixelationMotionTime(f.time,pixelated?level:0);
    const float actionTime=f.actionTime>=0?f.actionTime:pixelationActionTime(f.time,pixelated?level:0);
    const auto pose=d.id=="red_john"?redJohnPose(f.time):illusionary?illusionaryPose(f.time):pixelated?pixelationPose(actionTime):(equinox || breakthrough)?equinoxPose(f.time):avatarPose(f.time,f.walk,f.movement,floating);
    // Illusionary は胴の中心を支点に後傾し、腰・首・肘を別々に曲げます。
    // 他のポーズは追加角度が 0 なので、従来の位置と形を保ちます。
    const float posePivot=illusionary?1.45f:0.f;
    auto base=XMMatrixTranslation(0,-posePivot,0)*XMMatrixRotationX(pose.bodyPitch)*XMMatrixRotationZ(pose.bodyRoll)*
        XMMatrixRotationY(f.facing)*XMMatrixTranslation(f.avatarX,pose.lift+posePivot,f.avatarZ);
    blockAvatar(f,useVrm);
    const float t=f.time*d.speed;
    for (unsigned i=0;!pixelated && !equinox && !breakthrough && !nanally && !illusionary && !desertAura && !frutiger && !redJohn && i<tuning::world::StandardRingSegments;++i) {
        const float a=i*2*Pi/tuning::world::StandardRingSegments+t*tuning::world::StandardRingAngularSpeed;
        if(floating && level>0) particleStar(f.avatarX+std::cos(a)*d.radius,tuning::world::StandardRingHeight,f.avatarZ+std::sin(a)*d.radius,.085f,d.color,a,f);
        else box(f.avatarX+std::cos(a)*d.radius,tuning::world::StandardRingHeight,f.avatarZ+std::sin(a)*d.radius,.11f,.045f,.11f,d.color,a,1);
    }
    for (unsigned i=0;!pixelated && !equinox && !breakthrough && !nanally && !illusionary && !desertAura && !frutiger && !redJohn && i<d.particles;++i) {
        const float u=static_cast<float>(i)/std::max(1u,d.particles),a=u*2*Pi*3+t;
        const float y=.4f+std::fmod(u*3.3f+f.time*tuning::world::StandardParticleRiseSpeed,3.3f);
        const float r=d.radius*(.70f+.25f*std::sin(y*1.4f+u*5));
        const float s=.045f+.045f*(.5f+.5f*std::sin(i*1.7f));
        if(floating && level>0) {
            Color color=i%3?Color{.83f,.72f,1}:Color{1,.86f,.50f};
            if(level==3) {
                const auto star=singularityStar(f.time,i,d.particles);
                particleStar(f.avatarX+star.x,star.y,f.avatarZ+star.z,star.size,color,a+f.time,f);
            } else particleStar(f.avatarX+std::cos(a)*r,y,f.avatarZ+std::sin(a)*r,.08f+s*.7f,color,a+f.time*.3f,f);
        } else box(f.avatarX+std::cos(a)*r,y,f.avatarZ+std::sin(a)*r,s,s*1.8f,s,d.color,a,1);
    }
    if(floating && level>=2) {
        const float radius=level==3?tuning::world::LargeBlackHoleRadius:tuning::world::SmallBlackHoleRadius;
        XMStoreFloat4x4(&blackHole_.world,XMMatrixScaling(radius*2,radius*2,1)*XMMatrixRotationZ(.10f)*
            XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw)*XMMatrixTranslation(f.avatarX,tuning::world::BlackHoleHeight,f.avatarZ));
        blackHole_.color={1,1,1,1}; holeVisible_=true;
    }
    if(pixelated) buildPixelation(f);
    if(equinox) buildEquinox(f);
    if(breakthrough) buildBreakthrough(f);
    if(nanally) buildNanally(f);
    if(illusionary) buildIllusionary(f);
    if(desertAura) buildDesert(f);
    if(frutiger) buildFrutiger(f);
    if(redJohn) {const auto before=instances_.size();buildRedJohn(f);redJohnInstances_=instances_.size()-before;}
    const float worldW=logicalW_-596,worldH=logicalH_-280;
    D3D11_VIEWPORT viewport{256*scale_,96*scale_,worldW*scale_,worldH*scale_,0,1}; context_->RSSetViewports(1,&viewport);
    const float focus=redJohn?2.3f:illusionary?tuning::game::CameraFocusIllusionary:breakthrough?tuning::game::CameraFocusBreakthrough:nanally?tuning::game::CameraFocusNanally:
        holeVisible_?tuning::game::CameraFocusBlackHole:equinox?tuning::game::CameraFocusEquinox:
        (floating || pixelated)?tuning::game::CameraFocusFloating:tuning::game::CameraFocusNormal;
    const float cameraDistance=f.cameraDistance+(illusionary?2.5f:0.f);
    const XMVECTOR eye=XMVectorSet(f.avatarX+std::sin(f.cameraYaw)*std::cos(f.cameraPitch)*cameraDistance,
        focus+std::sin(f.cameraPitch)*cameraDistance,f.avatarZ-std::cos(f.cameraYaw)*std::cos(f.cameraPitch)*cameraDistance,1);
    const auto view=XMMatrixLookAtLH(eye,XMVectorSet(f.avatarX,focus,f.avatarZ,1),XMVectorSet(0,1,0,0));
    const auto projection=XMMatrixPerspectiveFovLH(tuning::game::FieldOfView,worldW/worldH,.1f,160);
    const float tagHeight=redJohn?3.05f:nanally?6.8f:breakthrough?5.2f:equinox?4.80f:(pixelated && level>=2?4.5f:3.7f)+pose.lift;
    XMFLOAT3 tag; XMStoreFloat3(&tag,XMVector3Project(XMVectorSet(f.avatarX,tagHeight,f.avatarZ,1),
        viewport.TopLeftX,viewport.TopLeftY,viewport.Width,viewport.Height,0,1,projection,view,XMMatrixIdentity()));
    nameplate_={tag.x/scale_,std::clamp(tag.y/scale_,178.f,logicalH_-400)}; nameplateVisible_=!breakthrough && !illusionary && tag.z>0 && tag.z<1;
    struct Camera { XMFLOAT4X4 vp; XMFLOAT4 eye,light; } camera{};
    XMStoreFloat4x4(&camera.vp,view*projection); XMStoreFloat4(&camera.eye,eye); camera.eye.w=pixelated?effectTime:f.time;
    const auto fogColor=stageFog(biome);
    // xはオーラ時の照明、y/z/wは惑星の霧のRGB。既存の96バイト定数バッファを共用します。
    camera.light={illusionary?.22f:pixelated?.30f:(equinox || breakthrough)?.20f:1.f,fogColor.r,fogColor.g,fogColor.b};
    context_->UpdateSubresource(constants_.Get(),0,nullptr,&camera,0,0);
    D3D11_MAPPED_SUBRESOURCE mapped{}; checked(context_->Map(instanceBuffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped), "Map instances");
    std::memcpy(mapped.pData,instances_.data(),instances_.size()*sizeof(Instance)); context_->Unmap(instanceBuffer_.Get(),0);
    ID3D11Buffer* streams[]={vertices_.Get(),instanceBuffer_.Get()}; const UINT strides[]={sizeof(Vertex),sizeof(Instance)},offsets[]={0,0};
    context_->IASetVertexBuffers(0,2,streams,strides,offsets); context_->IASetIndexBuffer(indices_.Get(),DXGI_FORMAT_R16_UINT,0);
    context_->IASetInputLayout(layout_.Get()); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(),nullptr,0); ID3D11Buffer* cb=constants_.Get(); context_->VSSetConstantBuffers(0,1,&cb);
    context_->PSSetConstantBuffers(0,1,&cb);
    context_->PSSetShader(ps_.Get(),nullptr,0); context_->GSSetShader(nullptr,nullptr,0);
    context_->RSSetState(raster_.Get()); context_->OMSetDepthStencilState(depthState_.Get(),0);
    context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    context_->DrawIndexedInstanced(36,static_cast<UINT>(instances_.size()),0,0,0);
    if(useVrm) vrm_->draw(context_.Get(),view*projection,base,pose);
    drawEffects(f);
    drawRemotePlayers(f,view*projection,eye,fogColor);
}
void Renderer::rect(float x,float y,float w,float h,D2D1_COLOR_F c,float r) {
    brush_->SetColor(c); const auto area=D2D1::RectF(x,y,x+w,y+h);
    if(r) d2d_->FillRoundedRectangle(D2D1::RoundedRect(area,r,r),brush_.Get()); else d2d_->FillRectangle(area,brush_.Get());
}
void Renderer::stroke(float x,float y,float w,float h,D2D1_COLOR_F c,float r) {
    brush_->SetColor(c); const auto area=D2D1::RectF(x,y,x+w,y+h);
    if(r) d2d_->DrawRoundedRectangle(D2D1::RoundedRect(area,r,r),brush_.Get(),1); else d2d_->DrawRectangle(area,brush_.Get(),1);
}
void Renderer::text(const std::wstring& value,float x,float y,float w,float h,float size,D2D1_COLOR_F color,bool bold,bool center) {
    auto font=std::find_if(fonts_.begin(),fonts_.end(),[&](const auto& f){return f.size==size && f.bold==bold;});
    if(font==fonts_.end()) {
        Font f{size,bold,{}};
        checked(write_->CreateTextFormat(L"Segoe UI",nullptr,bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"ja-jp",&f.format), "Text format");
        f.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); fonts_.push_back(std::move(f)); font=fonts_.end()-1;
    }
    brush_->SetColor(color);
    font->format->SetTextAlignment(center?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);
    d2d_->DrawText(value.data(),static_cast<UINT32>(value.size()),font->format.Get(),D2D1::RectF(x,y,x+w,y+h),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::label(const std::string& value,float x,float y,float w,float h,float size,D2D1_COLOR_F color,bool bold,bool center) {
    text(widen(value),x,y,w,h,size,color,bold,center);
}
void Renderer::draw(const Frame& frame,bool present) {
    updateVrm(frame);
    debugUi(frame);
    const auto sky=stageSky(frame.presentation.settings.biome);
    const float clear[]={sky.r,sky.g,sky.b,1}; context_->ClearRenderTargetView(target_.Get(),clear);
    context_->ClearDepthStencilView(depth_.Get(),D3D11_CLEAR_DEPTH,1,0);
    ID3D11RenderTargetView* view=target_.Get(); context_->OMSetRenderTargets(1,&view,depth_.Get());
    if(frame.cutsceneTime>=0) {
        const float cinematicOpacity=tuning::cutscene::endingOpacity(frame.cutsceneTime,cutsceneDuration(frame.cutsceneKind),
            frame.cutsceneKind==CutsceneKind::Breakthrough);
        if(cinematicOpacity<1) world(frame);
        else {
        instances_.clear(); starInstances_.clear(); pixelationInstances_.clear(); equinoxOrbs_.clear(); equinoxTitles_.clear(); nanallyInstances_.clear(); illusionaryInk_.clear(); frutigerInstances_.clear();
        pixelationVisible_=equinoxVisible_=breakthroughVisible_=holeVisible_=false;
        nanallyVisible_=false;
        illusionaryVisible_=desertAuraVisible_=frutigerVisible_=false;
        redJohnInstances_=0;
        }
        if(frame.cutsceneKind==CutsceneKind::Breakthrough) drawBreakthroughCutscene(frame);
        else if(frame.cutsceneKind==CutsceneKind::RedJohn) drawRedJohnCutscene(frame);
        else drawCutscene(frame);
    } else world(frame);
    context_->OMSetRenderTargets(0,nullptr,nullptr);
    d2d_->BeginDraw(); d2d_->SetTransform(D2D1::Matrix3x2F::Scale(scale_,scale_));
    if(frame.cutsceneTime>=0) {
        if(frame.cutsceneKind==CutsceneKind::Breakthrough) breakthroughCutsceneUi(frame);
        else if(frame.cutsceneKind==CutsceneKind::RedJohn) redJohnCutsceneUi(frame);
        else cutsceneUi(frame);
    } else ui(frame);
    checked(d2d_->EndDraw(), "Draw UI");
    context_->OMSetRenderTargets(1,&view,nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if(present) checked(swap_->Present(1,0), "Present frame");
}
Hit Renderer::hit(float px,float py,const Frame& f) const {
    if(f.cutsceneTime>=0) return {};
    const float x=px/scale_,y=py/scale_,w=logicalW_,h=logicalH_,bx=w*.5f-105,by=h-146;
    if(inside(x,y,w-458,26,116,28)) return {Action::Multiplayer,0};
    if(inside(x,y,w-330,26,112,28)) return {Action::Debug,0};
    if(f.stageTravelProgress>=0) return {};
    if(f.stageSelectOpen) {
        const auto panel=stageSelectLayout(w,h);
        if(inside(x,y,panel.x+panel.width-48,panel.y+16,30,30)) return {Action::CloseStages,0};
        for(unsigned i=0;i<StageCount;++i)
            if(inside(x,y,panel.x+20,panel.y+112+i*64.f,panel.width-40,56) &&
                static_cast<Biome>(i)!=f.presentation.settings.biome) return {Action::Travel,i};
        return {};
    }
    if(inside(x,y,w-487,114,130,26)) return {Action::Stages,0};
    if(inside(x,y,bx,by,210,62)) return {Action::Roll,0};
    if(inside(x,y,bx+228,by,134,62)) return {Action::Auto,0};
    if(inside(x,y,w-179,by,130,62)) return {Action::Logs,0};
    for(unsigned category=0;category<4;++category)
        if(inside(x,y,34+(category%2)*100.f,235+(category/2)*29.f,96,24)) return {Action::CollectionCategory,category};
    const auto items=collectionItems(f);
    const int visible=std::max(1,static_cast<int>((h-500)/48));
    const int start=std::clamp(f.collectionScroll,0,std::max(0,static_cast<int>(items.size())-visible));
    for(int row=0;row<visible && start+row<static_cast<int>(items.size());++row)
        if(inside(x,y,34,301+row*48.f,196,43)) return {Action::Equip,items[start+row]};
    return {};
}
int Renderer::region(float px,float py) const {
    const float x=px/scale_,y=py/scale_;
    if(inside(x,y,24,96,216,logicalH_-280)) return 1;
    if(inside(x,y,logicalW_-314,96,290,logicalH_-280)) return 2;
    return 0;
}
bool Renderer::canOrbit(float px,float py) const {
    if(multiplayerPanelVisible_ && inside(px,py,multiplayerBounds_.x,multiplayerBounds_.y,multiplayerBounds_.z,multiplayerBounds_.w)) return false;
    if(debugOpen_ && inside(px,py,debugBounds_.x,debugBounds_.y,debugBounds_.z,debugBounds_.w)) return false;
    return inside(px/scale_,py/scale_,256,96,logicalW_-596,logicalH_-280);
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
