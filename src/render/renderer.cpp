#include "render/renderer.hpp"
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
constexpr unsigned MaxInstances = 4096;
constexpr float Pi = 3.14159265359f;
const D2D1_COLOR_F White = D2D1::ColorF(.91f, .95f, .98f);
const D2D1_COLOR_F Muted = D2D1::ColorF(.48f, .57f, .65f);
const D2D1_COLOR_F Mint = D2D1::ColorF(.55f, .96f, .82f);
const D2D1_COLOR_F Panel = D2D1::ColorF(.047f, .070f, .094f);
const D2D1_COLOR_F Border = D2D1::ColorF(.13f, .19f, .23f);
D2D1_COLOR_F tint(Color c, float alpha = 1) { return D2D1::ColorF(c.r, c.g, c.b, alpha); }
bool inside(float x, float y, float rx, float ry, float w, float h) { return x >= rx && x < rx+w && y >= ry && y < ry+h; }
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
    return float4(lerp(color,float3(.042,.069,.098),fog*.7),1);
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
    instances_.clear(); starInstances_.clear(); pixelationInstances_.clear(); equinoxOrbs_.clear(); equinoxTitles_.clear(); holeVisible_=false;
    const auto& d=f.roller.catalog()[f.presentation.shown(f.progress)];
    const unsigned level=f.presentation.level(f.progress,f.roller.catalog());
    const bool desert=f.presentation.settings.biome==Biome::Desert;
    box(0,-.6f,0,StageHalfExtent*2,1.0f,StageHalfExtent*2,desert?Color{.38f,.23f,.13f}:Color{.095f,.15f,.18f});
    box(0,-.12f,0,63.7f,.10f,63.7f,desert?Color{.68f,.46f,.24f}:Color{.17f,.28f,.29f});
    for (int x=-15;x<=15;++x) for (int z=-15;z<=15;++z) {
        const bool path=std::abs(x)<=1 || std::abs(z)<=1;
        const float checker=((x+z)%2==0)?.035f:0;
        const Color c=desert?(path?Color{.72f+checker,.52f+checker,.31f+checker}:Color{.83f+checker,.64f+checker,.39f+checker}):
            (path?Color{.27f+checker,.34f+checker,.37f+checker}:Color{.14f+checker,.30f+checker,.26f+checker});
        box(static_cast<float>(x)*2,-.03f,static_cast<float>(z)*2,1.98f,.12f,1.98f,c);
    }
    // A low central plinth and its emissive frame are built from the same cube mesh.
    box(0,.07f,0,4.6f,.14f,4.6f,desert?Color{.56f,.38f,.24f}:Color{.20f,.29f,.32f});
    for (int i=0;i<4;++i) {
        const float a=i*Pi/2; box(std::sin(a)*2.25f,.16f,std::cos(a)*2.25f,4.5f,.025f,.035f,d.color,a,1);
    }
    const float trees[][2]={{-6,-5},{6,5},{-6,6},{6,-6},{-8,1},{8,0},
        {-18,-18},{18,18},{-18,18},{18,-18},{-26,-8},{26,8},{-8,26},{8,-26},{-26,25},{26,-25},{-12,-24},{12,24}};
    for (const auto& p:trees) {
        if(desert) {
            const float tall=2.2f+.35f*std::sin(p[0]);
            box(p[0],tall*.5f,p[1],.52f,tall,.50f,{.22f,.40f,.25f});
            box(p[0]-.52f,1.15f,p[1],.62f,.30f,.32f,{.27f,.46f,.29f});
            box(p[0]-.75f,1.45f,p[1],.30f,.85f,.32f,{.27f,.46f,.29f});
            box(p[0]+.50f,.85f,p[1],.62f,.28f,.32f,{.25f,.43f,.27f});
            box(p[0]+.74f,1.03f,p[1],.30f,.62f,.32f,{.25f,.43f,.27f});
            box(p[0]+.8f,.15f,p[1]+.6f,.65f,.3f,.55f,{.55f,.37f,.24f},.3f);
            continue;
        }
        box(p[0],.85f,p[1],.4f,1.7f,.4f,{.21f,.20f,.18f});
        box(p[0],2.2f,p[1],1.9f,1.4f,1.9f,{.13f,.35f,.29f});
        box(p[0]-.1f,3.05f,p[1],1.3f,.6f,1.3f,{.21f,.45f,.34f});
    }
    for (float x : {-4.f,4.f}) for (float z : {-3.f,3.f}) {
        box(x,.55f,z,.5f,1.1f,.5f,{.31f,.37f,.39f});
        box(x,1.15f,z,.56f,.14f,.56f,{.50f,.94f,.80f},0,1);
    }
    // The lightweight primitive avatar remains available while a VRM is loading.
    const bool useVrm=f.presentation.settings.avatar==Avatar::Vrm && vrmReady();
    const bool floating=d.id=="singularity";
    const bool pixelated=d.id=="pixelation";
    const bool equinox=d.id=="equinox";
    const bool breakthrough=d.id=="breakthrough";
    pixelationVisible_=pixelated; equinoxVisible_=equinox; breakthroughVisible_=breakthrough;
    const float effectTime=f.effectTime>=0?f.effectTime:pixelationMotionTime(f.time,pixelated?level:0);
    const float actionTime=f.actionTime>=0?f.actionTime:pixelationActionTime(f.time,pixelated?level:0);
    const auto pose=pixelated?pixelationPose(actionTime):(equinox || breakthrough)?equinoxPose(f.time):avatarPose(f.time,f.walk,f.movement,floating);
    auto base=XMMatrixRotationX(pose.bodyPitch)*XMMatrixRotationZ(pose.bodyRoll)*
        XMMatrixRotationY(f.facing)*XMMatrixTranslation(f.avatarX,pose.lift,f.avatarZ);
    auto part=[&](float x,float y,float z,float sx,float sy,float sz,Color c,FXMMATRIX transform) {
        if(useVrm) return;
        Instance instance{};
        XMStoreFloat4x4(&instance.world,XMMatrixScaling(sx,sy,sz)*XMMatrixTranslation(x,y,z)*transform*base);
        auto lit=c;
        if(pixelated) {const auto hue=pixelationColor(effectTime); lit={.45f+hue.r*.55f,.45f+hue.g*.55f,.45f+hue.b*.55f};}
        if(equinox) lit={.83f,.85f,.90f};
        if(breakthrough) lit={.65f,.71f,1.f};
        instance.color={lit.r,lit.g,lit.b,pixelated?1.8f:(equinox || breakthrough)?.65f:0.f}; instances_.push_back(instance);
    };
    const Color skin{.95f,.76f,.47f}, shirt=f.presentation.settings.shirt, pants{.20f,.29f,.42f};
    for(int side=0;side<2;++side) {
        const float sign=side?1.f:-1.f;
        const auto arm=XMMatrixRotationZ(sign*pose.armOut[side])*XMMatrixRotationX(pose.armPitch[side])*XMMatrixTranslation(sign*.76f,2.02f,0);
        part(0,-.46f,0,.43f,.96f,.54f,skin,arm);
        const auto hip=XMMatrixRotationZ(sign*pose.legOut[side])*XMMatrixRotationX(pose.legPitch[side])*XMMatrixTranslation(sign*.27f,1.10f,0);
        part(0,-.24f,0,.46f,.48f,.52f,pants,hip);
        const auto knee=XMMatrixRotationX(pose.knee[side])*XMMatrixTranslation(0,-.48f,0)*hip;
        part(0,-.24f,0,.46f,.48f,.52f,pants,knee);
    }
    const auto identity=XMMatrixIdentity();
    part(0,1.56f,0,1.03f,.96f,.60f,shirt,identity);
    part(0,2.42f,0,.73f,.73f,.73f,skin,identity);
    part(-.16f,2.48f,-.375f,.085f,.10f,.02f,{.10f,.13f,.15f},identity);
    part(.16f,2.48f,-.375f,.085f,.10f,.02f,{.10f,.13f,.15f},identity);
    part(0,2.26f,-.375f,.24f,.045f,.02f,{.25f,.19f,.17f},identity);
    const float t=f.time*d.speed;
    for (unsigned i=0;!pixelated && !equinox && !breakthrough && i<48;++i) {
        const float a=i*2*Pi/48+t*.5f;
        if(floating && level>0) particleStar(f.avatarX+std::cos(a)*d.radius,.25f,f.avatarZ+std::sin(a)*d.radius,.085f,d.color,a,f);
        else box(f.avatarX+std::cos(a)*d.radius,.25f,f.avatarZ+std::sin(a)*d.radius,.11f,.045f,.11f,d.color,a,1);
    }
    for (unsigned i=0;!pixelated && !equinox && !breakthrough && i<d.particles;++i) {
        const float u=static_cast<float>(i)/std::max(1u,d.particles),a=u*2*Pi*3+t;
        const float y=.4f+std::fmod(u*3.3f+f.time*.38f,3.3f);
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
        const float radius=level==3?1.65f:.72f;
        XMStoreFloat4x4(&blackHole_.world,XMMatrixScaling(radius*2,radius*2,1)*XMMatrixRotationZ(.10f)*
            XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw)*XMMatrixTranslation(f.avatarX,5.7f,f.avatarZ));
        blackHole_.color={1,1,1,1}; holeVisible_=true;
    }
    if(pixelated) buildPixelation(f);
    if(equinox) buildEquinox(f);
    if(breakthrough) buildBreakthrough(f);
    const float worldW=logicalW_-596,worldH=logicalH_-280;
    D3D11_VIEWPORT viewport{256*scale_,96*scale_,worldW*scale_,worldH*scale_,0,1}; context_->RSSetViewports(1,&viewport);
    const float focus=breakthrough?3.3f:holeVisible_?2.5f:(equinox?2.3f:(floating || pixelated?2.f:1.4f));
    const float cameraDistance=f.cameraDistance;
    const XMVECTOR eye=XMVectorSet(f.avatarX+std::sin(f.cameraYaw)*std::cos(f.cameraPitch)*cameraDistance,
        focus+std::sin(f.cameraPitch)*cameraDistance,f.avatarZ-std::cos(f.cameraYaw)*std::cos(f.cameraPitch)*cameraDistance,1);
    const auto view=XMMatrixLookAtLH(eye,XMVectorSet(f.avatarX,focus,f.avatarZ,1),XMVectorSet(0,1,0,0));
    const auto projection=XMMatrixPerspectiveFovLH(.80f,worldW/worldH,.1f,160);
    const float tagHeight=breakthrough?5.2f:equinox?4.80f:(pixelated && level>=2?4.5f:3.7f)+pose.lift;
    XMFLOAT3 tag; XMStoreFloat3(&tag,XMVector3Project(XMVectorSet(f.avatarX,tagHeight,f.avatarZ,1),
        viewport.TopLeftX,viewport.TopLeftY,viewport.Width,viewport.Height,0,1,projection,view,XMMatrixIdentity()));
    nameplate_={tag.x/scale_,std::clamp(tag.y/scale_,178.f,logicalH_-400)}; nameplateVisible_=!breakthrough && tag.z>0 && tag.z<1;
    struct Camera { XMFLOAT4X4 vp; XMFLOAT4 eye,light; } camera{};
    XMStoreFloat4x4(&camera.vp,view*projection); XMStoreFloat4(&camera.eye,eye); camera.eye.w=pixelated?effectTime:f.time;
    camera.light={pixelated?.30f:(equinox || breakthrough)?.20f:1.f,0,0,0};
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
    const float clear[]={.028f,.046f,.066f,1}; context_->ClearRenderTargetView(target_.Get(),clear);
    context_->ClearDepthStencilView(depth_.Get(),D3D11_CLEAR_DEPTH,1,0);
    ID3D11RenderTargetView* view=target_.Get(); context_->OMSetRenderTargets(1,&view,depth_.Get());
    if(frame.cutsceneTime>=0) {
        instances_.clear(); starInstances_.clear(); pixelationInstances_.clear(); equinoxOrbs_.clear(); equinoxTitles_.clear();
        pixelationVisible_=equinoxVisible_=breakthroughVisible_=holeVisible_=false;
        if(frame.cutsceneKind==CutsceneKind::Breakthrough) drawBreakthroughCutscene(frame); else drawCutscene(frame);
    } else world(frame);
    context_->OMSetRenderTargets(0,nullptr,nullptr);
    d2d_->BeginDraw(); d2d_->SetTransform(D2D1::Matrix3x2F::Scale(scale_,scale_));
    if(frame.cutsceneTime>=0) {
        if(frame.cutsceneKind==CutsceneKind::Breakthrough) breakthroughCutsceneUi(frame); else cutsceneUi(frame);
    } else ui(frame);
    checked(d2d_->EndDraw(), "Draw UI");
    context_->OMSetRenderTargets(1,&view,nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if(present) checked(swap_->Present(1,0), "Present frame");
}
Hit Renderer::hit(float px,float py,const Frame& f) const {
    if(f.cutsceneTime>=0) return {};
    const float x=px/scale_,y=py/scale_,w=logicalW_,h=logicalH_,bx=w*.5f-105,by=h-146;
    if(inside(x,y,w-330,26,112,28)) return {Action::Debug,0};
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
bool Renderer::canOrbit(float px,float py) const {
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
