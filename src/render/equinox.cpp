#include "render/renderer.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aura {
using namespace DirectX;
namespace {
constexpr float Pi=3.14159265359f;
constexpr char OrbShader[]=R"(
cbuffer Camera:register(b0) { row_major float4x4 viewProjection; float4 eyeAndTime; };
Texture2D lettering:register(t0); SamplerState linearClamp:register(s0);
struct Input { float4 p:SV_POSITION; float3 n:NORMAL; float4 color:COLOR0; float3 world:TEXCOORD0; float3 local:TEXCOORD1; };
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float noise(float2 p) { float2 v=floor(p),u=frac(p); u=u*u*(3-2*u); return lerp(lerp(hash(v),hash(v+float2(1,0)),u.x),lerp(hash(v+float2(0,1)),hash(v+1),u.x),u.y); }
float cloud(float2 p) { return noise(p)*.53+noise(p*2.07+7)*.28+noise(p*4.1-9)*.13+noise(p*8.3)*.06; }
float4 main(Input i):SV_TARGET {
    float2 q=i.local.xy*2; float r=length(q),a=atan2(q.y,q.x),time=eyeAndTime.w;
    int style=(int)floor(i.color.a);
    if(style==3) {
        float2 uv=float2(.4375+(q.x*.5+.5)*.125,(1.5-q.y*.5)*.25);
        float alpha=lettering.Sample(linearClamp,uv).a;
        float c=cos(time*3.93)>0?.025:.95;
        return float4(c,c,c,alpha);
    }
    clip(1-r);
    if(style==2) {
        float swirl=cloud(float2(r*15,a*2+time*.22));
        float bands=.5+.5*sin(r*36-a*3+time*1.7+swirl*4);
        float disk=exp(-pow((r-.69)/.23,2))*lerp(.35,.92,bands)*(.6+swirl*.8);
        float inner=exp(-r*r*11)*(.22+.25*sin(time*3.93));
        float shade=cos(time*3.93)>0?disk+inner:disk*.8-inner*.3;
        float alpha=saturate((disk+.52)*(1-smoothstep(.90,1,r)));
        return float4(shade.xxx,alpha);
    }
    float surface=cloud(q*4.5+float2(time*.035,0));
    float z=sqrt(saturate(1-r*r));
    float light=.25+.65*saturate(dot(float3(q,z),normalize(float3(-.6,.65,1))));
    float rings=exp(-abs(r-.95)*180)+exp(-abs(r-.87)*180)*.28;
    float star=1-smoothstep(.43,.47,pow(abs(q.x),.55)+pow(abs(q.y),.55));
    float3 color;
    if(style==0) {
        float crater=0;
        [unroll]for(int j=0;j<12;++j) {
            float2 center=float2(hash(float2(j,2)),hash(float2(j,7)))*1.55-.775;
            float radius=.06+hash(float2(j,3))*.15,d=length(q-center);
            crater+=exp(-pow((d-radius)*25,2))*.10-exp(-d*d/(radius*radius)*4)*.07;
        }
        float gray=saturate((.40+surface*.53+crater)*light);
        color=lerp(gray.xxx,float3(.012,.015,.022),star);
        color+=rings*.48;
    } else {
        float crack=pow(saturate(.5+.5*sin(a*11+r*29+cloud(q*8+time*.2)*8)),18);
        color=float3(.005,.007,.012)+surface*.025+crack*(.06+.35*r*r)+rings*.36;
        color+=star*2.2;
    }
    return float4(color,1-smoothstep(.985,1,r));
})";
}
void Renderer::createEquinox() {
    ComPtr<ID3DBlob> shader,errors;
    const HRESULT hr=D3DCompile(OrbShader,sizeof(OrbShader)-1,"equinox-orbs.hlsl",nullptr,nullptr,"main","ps_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors);
    if(FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    checked(hr,"Compile Equinox material");
    checked(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&equinoxOrbPs_),"Equinox orb shader");
    // Render system-font lettering into a small atlas at runtime; no font files are shipped.
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=1024;desc.Height=512;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture; checked(device_->CreateTexture2D(&desc,nullptr,&texture),"Equinox lettering texture");
    ComPtr<IDXGISurface> surface; checked(texture.As(&surface),"Equinox lettering surface");
    ComPtr<ID2D1RenderTarget> canvas;
    const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(desc.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    checked(d2dFactory_->CreateDxgiSurfaceRenderTarget(surface.Get(),props,&canvas),"Equinox lettering target");
    ComPtr<ID2D1SolidColorBrush> brush; checked(canvas->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White),&brush),"Equinox lettering brush");
    canvas->BeginDraw(); canvas->Clear(D2D1::ColorF(0,0));
    const wchar_t* words[]={L"EQUINOX",L"衡",L"EQUILIBRIUM INCARNATE"};
    const float sizes[]={102,100,31};
    for(unsigned i=0;i<3;++i) {
        ComPtr<IDWriteTextFormat> font;
        checked(write_->CreateTextFormat(i==1?L"Yu Gothic":L"Times New Roman",nullptr,DWRITE_FONT_WEIGHT_BOLD,
            i==0?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,sizes[i],L"ja-jp",&font),"Equinox lettering font");
        font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        const auto value=std::wstring(words[i]);
        canvas->DrawText(value.data(),static_cast<UINT32>(value.size()),font.Get(),D2D1::RectF(0,i*128.f,1024,(i+1)*128.f),brush.Get());
    }
    checked(canvas->EndDraw(),"Render Equinox lettering");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&equinoxLettering_),"Equinox lettering source");
    equinoxOrbs_.reserve(4);
    createCutscene();
}
void Renderer::buildEquinox(const Frame& source) {
    const auto& f=source;
    const float t=f.time,ox=f.avatarX,oz=f.avatarZ;
    const Color silver{.83f,.88f,.98f},white{1.1f,1.1f,1.1f};
    auto point=[&](float x,float y,float z){return XMVectorSet(ox+x,y,oz+z,1);};
    auto line=[&](XMVECTOR a,XMVECTOR b,float width,float power){pixelLine(a,b,width,silver,power,f);};
    auto material=[&](FXMMATRIX model,unsigned style) {Instance v;XMStoreFloat4x4(&v.world,model);v.color={1,1,1,style+.95f};equinoxOrbs_.push_back(v);};
    material(XMMatrixScaling(8.2f,8.2f,1)*XMMatrixRotationX(Pi*.5f)*XMMatrixTranslation(ox,.19f,oz),2);
    const auto billboard=XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw);
    material(XMMatrixScaling(.95f,.75f,1)*billboard*XMMatrixTranslation(ox+std::sin(f.cameraYaw)*.9f,.80f,oz-std::cos(f.cameraYaw)*.9f),3);
    // Thick, softly glowing spiral bands with independently moving dark/bright filaments.
    for(unsigned ring=0;ring<3;++ring) {
        const float radius=2.1f+ring*.63f,height=.28f+ring*.07f;
        for(unsigned i=0;i<128;++i) {
            const float a=i*2*Pi/128+t*(ring%2?-.50f:.32f),b=a+2*Pi/128;
            const float wave=.04f*std::sin(a*3+t);
            line(point(std::cos(a)*radius,height+wave,std::sin(a)*radius),
                 point(std::cos(b)*radius,height+wave,std::sin(b)*radius),.17f+ring*.025f,.31f+ring*.12f);
        }
    }
    for(unsigned i=0;i<58;++i) {
        const float u=i/58.f,a=u*2*Pi*3+t*.42f,r=1.15f+u*2.85f;
        const float size=.90f+.40f*std::sin(i*2.f)*std::sin(i*2.f);
        pixelBillboard(ox+std::cos(a)*r,.35f+.16f*std::sin(i+t),oz+std::sin(a)*r,size,size,silver,0,.56f,f);
    }
    struct Orb {float x,y,z;unsigned style;}; Orb orbs[2];
    for(unsigned side=0;side<2;++side) {
        const float a=t*2*Pi/1.565f+side*Pi;
        orbs[side]={std::cos(a)*2.25f,3.1f+.20f*std::sin(a),std::sin(a)*2.25f,side};
    }
    const float eyeX=std::sin(f.cameraYaw),eyeZ=-std::cos(f.cameraYaw);
    if(orbs[0].x*eyeX+orbs[0].z*eyeZ>orbs[1].x*eyeX+orbs[1].z*eyeZ) std::swap(orbs[0],orbs[1]);
    for(const auto& orb:orbs) {
        constexpr float radius=1.35f;
        material(XMMatrixScaling(radius*2,radius*2,1)*XMMatrixRotationZ(-t*.14f)*billboard*XMMatrixTranslation(ox+orb.x,orb.y,oz+orb.z),orb.style);
        const auto center=point(orb.x,orb.y,orb.z);
        const auto face=billboard*XMMatrixTranslation(ox+orb.x,orb.y,oz+orb.z);
        for(unsigned layer=0;layer<2;++layer) for(unsigned i=0;i<96;++i) {
            const float a=i*2*Pi/96,b=(i+1)*2*Pi/96,r=radius*(layer?1.12f:1.025f);
            line(XMVector3TransformCoord(XMVectorSet(std::cos(a)*r,std::sin(a)*r,0,1),face),
                 XMVector3TransformCoord(XMVectorSet(std::cos(b)*r,std::sin(b)*r,0,1),face),.065f,orb.style?.25f:.40f);
        }
        pixelBillboard(ox+orb.x,orb.y,oz+orb.z,3.6f,3.6f,silver,0,orb.style?.22f:.40f,f);
        if(orb.style) {
            pixelBillboard(ox+orb.x,orb.y,oz+orb.z,2.2f,2.2f,white,4,.90f,f);
            pixelBillboard(ox+orb.x,orb.y,oz+orb.z,5.2f,.55f,white,4,.65f,f);
            for(unsigned bolt=0;bolt<5;++bolt) {
                XMVECTOR previous=center;
                for(unsigned j=1;j<=6;++j) {
                    const float a=bolt*2*Pi/5+.10f*std::sin(j*7+std::floor(t*12)),r=j/6.f*radius*.87f;
                    const auto next=XMVector3TransformCoord(XMVectorSet(std::cos(a)*r,std::sin(a)*r,0,1),face);
                    line(previous,next,.050f,.35f); previous=next;
                }
            }
        } else pixelBillboard(ox+orb.x,orb.y,oz+orb.z,4.3f,.25f,white,4,.16f,f);
    }
    pixelBillboard(ox,5.15f,oz,4.0f,.54f,white,8,.72f,f);
    pixelBillboard(ox,5.52f,oz,5.0f,.25f,silver,10,.60f,f);
    // Volumetric silver dust, irregular triangles and occasional four-point stars.
    for(unsigned i=0;i<140;++i) {
        const float a=i*2.399963f+t*.15f,r=2.f+2.f*(.5f+.5f*std::sin(i*4.3f));
        const float y=.15f+std::fmod(i*.137f+t*.22f,5.5f),s=.03f+.07f*std::abs(std::sin(i*3.1f));
        pixelBillboard(ox+std::cos(a)*r,y,oz+std::sin(a)*r,s,s,white,i%4?1:6,.40f,f,t*.35f+i);
        if(i%17==0) pixelBillboard(ox+std::cos(a)*r,y,oz+std::sin(a)*r,.4f,.4f,white,4,.45f,f);
    }
}
} // namespace aura
