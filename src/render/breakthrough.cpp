#include "render/renderer.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace aura {
using namespace DirectX;
namespace {
constexpr float Pi=3.14159265359f;
constexpr char MaterialShader[]=R"(
cbuffer Camera:register(b0) { row_major float4x4 viewProjection; float4 eyeAndTime; };
Texture2D lettering:register(t0); SamplerState linearClamp:register(s0);
struct Input { float4 p:SV_POSITION; float3 n:NORMAL; float4 color:COLOR0; float3 world:TEXCOORD0; float3 local:TEXCOORD1; };
float hash(float2 p) { float3 a=frac(float3(p.x,p.y,p.x)*.1031); a+=dot(a,a.yzx+33.33); return frac((a.x+a.y)*a.z); }
float noise(float2 p) { float2 v=floor(p),u=frac(p);u=u*u*(3-2*u);return lerp(lerp(hash(v),hash(v+float2(1,0)),u.x),lerp(hash(v+float2(0,1)),hash(v+1),u.x),u.y); }
float cloud(float2 p) { float a=noise(p)*.57;p=mul(p,float2x2(1.6,-1.2,1.2,1.6))+9;a+=noise(p)*.28;return a+noise(p*2.1)*.15; }
float4 main(Input i):SV_TARGET {
    float2 q=i.local.xy*2;float r2=dot(q,q),edge=max(abs(q.x),abs(q.y));
    float time=eyeAndTime.w,mask=0;int style=(int)floor(i.color.a+.00001);float opacity=frac(i.color.a)*1.011;
    if(style==0) {
        float2 p=q*2.3+float2(time*.075,-time*.055)+i.world.xz*.12;
        float smoke=cloud(p+cloud(p*1.7+time*.03)*1.1);
        mask=pow(saturate(smoke-.19),1.7)*exp(-r2*2.1)*(1-smoothstep(.7,1,edge));
    } else if(style==1) {
        mask=(1-smoothstep(.66,.94,edge))*(.50+hash(floor(i.world.xz*17))*1.2);
    } else if(style==2) {
        mask=(exp(-q.y*q.y*400)*1.4+exp(-q.y*q.y*24)*.28)*(1-smoothstep(.82,1,abs(q.x)));
    } else if(style==3) {
        float2 uv=q*.5+.5;
        float2 cells=uv*float2(12,76),grid=frac(cells);
        float seed=hash(floor(cells)+floor(i.world.xz*11));
        float panel=(.045+step(.44,seed)*seed*.26+step(.94,seed)*.65);
        float seams=exp(-min(grid.x,1-grid.x)*120)*.18+exp(-min(grid.y,1-grid.y)*120)*.10;
        float blocks=step(.69,hash(floor(uv*float2(5,22))+floor(i.world.xz*3)));
        float frame=exp(-abs(abs(q.x)-.95)*90)*.53+exp(-abs(abs(q.y)-.98)*150)*1.1;
        float flicker=.65+.35*noise(uv*33+float2(0,time*.32));
        mask=((panel+seams)*(1-blocks*.88)*flicker+frame)*(1-smoothstep(.985,1,edge));
    } else if(style==8 || style==9) {
        float2 uv=float2(q.x*.5+.5,(.5-q.y*.5+(style-8))*.5);
        mask=lettering.Sample(linearClamp,uv).a;
    } else {
        float cross=exp(-abs(q.x)*70-abs(q.y)*2.8)+exp(-abs(q.y)*70-abs(q.x)*2.8);
        mask=exp(-r2*16)*1.0+cross*1.9;
    }
    return float4(i.color.rgb*mask,opacity);
})";

// Analytic box intersections and a few noise octaves replace the reference movie.
// The six camera shots, captions, blackout and final closing aperture share its timing.
constexpr char CinematicShader[]=R"(
cbuffer Cinematic:register(b0) { float4 screenAndTime; };
struct Input { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };
float hash(float2 p) { float3 a=frac(float3(p.x,p.y,p.x)*.1031);a+=dot(a,a.yzx+33.33);return frac((a.x+a.y)*a.z); }
float noise(float2 p) { float2 v=floor(p),u=frac(p);u=u*u*(3-2*u);return lerp(lerp(hash(v),hash(v+float2(1,0)),u.x),lerp(hash(v+float2(0,1)),hash(v+1),u.x),u.y); }
float cloud(float2 p) { float n=noise(p)*.54;p=mul(p,float2x2(1.6,-1.2,1.2,1.6))+17;n+=noise(p)*.28;p=mul(p,float2x2(1.6,-1.2,1.2,1.6))+9;return n+noise(p)*.13+noise(p*2.03)*.05; }
float2 rot(float2 p,float a) {float c=cos(a),s=sin(a);return float2(c*p.x-s*p.y,s*p.x+c*p.y);}
float star(float2 p,float size) {float2 a=abs(p)/size;return exp(-dot(a,a)*7)+exp(-a.x*90-a.y*3.4)+exp(-a.y*90-a.x*3.4);}
float hitBox(float3 ro,float3 rd,float3 halfSize) {
    float3 inverse=sign(rd)/max(abs(rd),.00001);
    float3 a=(-halfSize-ro)*inverse,b=(halfSize-ro)*inverse;
    float3 lo=min(a,b),hi=max(a,b);
    float nearT=max(lo.x,max(lo.y,lo.z)),farT=min(hi.x,min(hi.y,hi.z));
    return farT>=max(nearT,0)?max(nearT,0):10000;
}
float4 main(Input i):SV_TARGET {
    float t=screenAndTime.x,aspect=screenAndTime.y;
    float2 raw=(i.uv-.5)*float2(aspect,1),q=raw;
    float yaw=.48,roll=0,distance=11,targetY=3.7,pitch=.10;
    bool hidden=false;
    if(t<3.4) { distance=4.15;targetY=6.0;roll=-.30+.055*t;yaw=.24+t*.035;pitch=.12; }
    else if(t<7.3) { distance=4.6;targetY=1.5;roll=-.48+(t-3.4)*.065;yaw=.45+(t-3.4)*.06;pitch=.12; }
    else if(t<11.1) { distance=8.6;targetY=3.7;roll=-.22+(t-7.3)*.07;yaw=.36;pitch=.10; }
    else if(t<14.15) { hidden=true;roll=.12; }
    else if(t<15.05) { return float4(0,0,0,1); }
    else {distance=12.2+(t-15.05)*.045;targetY=3.9;roll=.015*sin(t*.4);yaw=.28+.017*(t-15);pitch=.04;}
    q=rot(q,roll);
    float3 target=float3(0,targetY,0),eye=target+float3(sin(yaw)*distance,sin(pitch)*distance,-cos(yaw)*distance);
    float3 forward=normalize(target-eye),right=normalize(cross(float3(0,1,0),forward)),up=cross(forward,right);
    float3 rd=normalize(forward*1.6+right*q.x-up*q.y);
    float2 warp=float2(noise(q*3.5+t*.021),noise(q*3.5+19-t*.017));
    float fog=pow(saturate(cloud(q*3.9+warp*2.4+float2(t*.019,-t*.012))-.16),1.3);
    float3 color=float3(.006,.008,.024)+fog*float3(.16,.17,.34);
    float2 lightPos=float2(0,0);
    if(!hidden) {
        float3 toLight=float3(0,3.9,0)-eye;
        lightPos=float2(dot(toLight,right),-dot(toLight,up))*1.6/max(.01,dot(toLight,forward));
        float nearest=10000;float3 surface=0;float columnID=0;float3 halfSize=0;
        [unroll]for(int n=0;n<7;++n) {
            float side=n==1 || n==3 || n==5?-1:1;
            float3 center;float3 bounds;float tilt;
            if(n==0){center=float3(0,4.05,.30);bounds=float3(.43,3.65,.38);tilt=0;}
            else if(n<3){center=float3(side*2.15,1.95,.35);bounds=float3(.35,1.95,.32);tilt=-side*.42;}
            else if(n<5){center=float3(side*1.35,3.35,1.25);bounds=float3(.24,1.75,.25);tilt=-side*.10;}
            else {center=float3(side*.96,2.2,-.20);bounds=float3(.22,1.15,.25);tilt=side*.13;}
            // The cinematic holds the monoliths still while the camera and mist move.
            float3 localEye=eye-center,localRay=rd;
            localEye.xy=rot(localEye.xy,-tilt);localRay.xy=rot(localRay.xy,-tilt);
            float hit=hitBox(localEye,localRay,bounds);
            if(hit<nearest) {nearest=hit;surface=localEye+localRay*hit;columnID=n;halfSize=bounds;}
        }
        if(nearest<9999) {
            float3 normalized=abs(surface/halfSize);
            float2 uv=surface.xy/(halfSize.xy*2)+.5;
            if(normalized.x>normalized.z)uv=float2(surface.z/(halfSize.z*2)+.5,uv.y);
            float2 cell=uv*float2(12,65),g=frac(cell);
            float seed=hash(floor(cell)+columnID*17);
            float blocks=step(.65,hash(floor(uv*float2(5,24))+columnID*7));
            float glass=.10+seed*.18+step(.92,seed)*.6;
            glass*=1-blocks*.90;
            float seams=exp(-min(g.x,1-g.x)*110)*.13+exp(-min(g.y,1-g.y)*100)*.06;
            float rim=exp(-min(uv.x,1-uv.x)*90)*.52+exp(-min(uv.y,1-uv.y)*190)*1.2;
            color=float3(.20,.23,.46)*(glass+seams)+rim*float3(.64,.69,1);
            color+=pow(noise(uv*float2(18,120)+columnID*3),9)*1.2;
            color*=.72+.28*cloud(uv*18+t*.055);
        }
    }
    float2 p=q-lightPos;
    float r=length(p),angle=atan2(p.y,p.x);
    float strength=t<15.05?.75:1.4;
    float glow=exp(-r*r*22)*.26;
    float flare=star(p,.19)*1.7+exp(-abs(p.y)*300)*exp(-abs(p.x)*.65)*.46;
    color+=float3(.66,.70,1)*(flare+glow)*strength;
    float litSmoke=pow(saturate(cloud(p*7+warp*1.8+float2(t*.043,-t*.031))-.22),1.5);
    color+=float3(.19,.20,.38)*litSmoke*exp(-r*r*1.7)*strength;
    // Slender winding filaments sweep in from the corners and coil around the flare.
    [unroll]for(int j=0;j<9;++j) {
        float offset=j*.6981;
        float turbulence=cloud(p*6+j*8+t*.025);
        float wave=sin(r*(3.3+j*.11)-t*.28+offset)*(.28+.18*r)+(turbulence-.5)*.9;
        float band=abs(sin(angle+offset+wave+r*.8));
        float filament=exp(-band*max(90,r*110))*exp(-r*.66)*(1-exp(-r*22));
        float variation=.4+.6*turbulence;
        color+=float3(.28,.31,.70)*filament*variation*(j<3?1.2:.48);
    }
    // Tiny drifting motes and Roman fragments occupy the same luminous volume.
    float2 cells=floor(q*38),particle=frac(q*38)-.5;
    float seed=hash(cells);
    float dots=exp(-dot(particle,particle)*850)*step(.965,seed);
    color+=float3(.65,.70,1)*dots*(.4+.6*sin(t+seed*40)*sin(t+seed*40));
    float aperture=.395;
    if(t<3.4)aperture=.395*(1-smoothstep(3.16,3.4,t));
    else if(t<7.3)aperture=.395*smoothstep(3.4,3.58,t)*(1-smoothstep(7.02,7.3,t));
    else if(t<11.1)aperture=.395*smoothstep(7.3,7.48,t)*(1-smoothstep(10.85,11.1,t));
    else if(t>=28.0)aperture=.395*(1-smoothstep(28.0,29.0,t));
    float mask=1-smoothstep(aperture,aperture+.0015,abs(i.uv.y-.5));
    float flash=exp(-pow((t-15.08)/.14,2));
    float inversion=exp(-pow((t-13.47)/.06,2));
    color=lerp(color,1-color,inversion);
    color=lerp(color,float3(.98,.98,1),flash);
    float desaturate=smoothstep(27.2,28.8,t)*.8;
    color=lerp(color,dot(color,float3(.299,.587,.114)).xxx,desaturate);
    color*=mask*smoothstep(0,.25,t)*(1-smoothstep(28.9,29.0,t));
    float endFlash=exp(-pow((t-29.05)/.045,2))*.68;
    color+=endFlash;
    return float4(saturate(color),1);
})";

float random(unsigned i,unsigned salt=0) {
    const float value=std::sin(i*127.1f+salt*311.7f)*43758.5453f;
    return value-std::floor(value);
}
}

void Renderer::createBreakthrough() {
    auto shader=[&](const char* source,std::size_t size,ID3D11PixelShader** output,const char* name) {
        ComPtr<ID3DBlob> code,errors;
        const HRESULT hr=D3DCompile(source,size,name,nullptr,nullptr,"main","ps_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        if(FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        checked(hr,"Compile BreakThrough shader");
        checked(device_->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,output),name);
    };
    shader(MaterialShader,sizeof(MaterialShader)-1,&breakthroughPs_,"breakthrough-material.hlsl");
    shader(CinematicShader,sizeof(CinematicShader)-1,&breakthroughCutscenePs_,"breakthrough-cutscene.hlsl");
    D3D11_BUFFER_DESC constants{};constants.ByteWidth=16;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    constants.Usage=D3D11_USAGE_DEFAULT;
    checked(device_->CreateBuffer(&constants,nullptr,&breakthroughConstants_),"BreakThrough CutScene constants");
    D3D11_TEXTURE2D_DESC description{};description.Width=1024;description.Height=256;
    description.MipLevels=description.ArraySize=1;description.SampleDesc.Count=1;
    description.Format=DXGI_FORMAT_B8G8R8A8_UNORM;description.Usage=D3D11_USAGE_DEFAULT;
    description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;checked(device_->CreateTexture2D(&description,nullptr,&texture),"BreakThrough lettering");
    ComPtr<IDXGISurface> surface;checked(texture.As(&surface),"BreakThrough lettering surface");
    ComPtr<ID2D1RenderTarget> canvas;
    const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(description.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    checked(d2dFactory_->CreateDxgiSurfaceRenderTarget(surface.Get(),properties,&canvas),"BreakThrough lettering canvas");
    ComPtr<ID2D1SolidColorBrush> brush;checked(canvas->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White),&brush),"BreakThrough lettering brush");
    canvas->BeginDraw();canvas->Clear(D2D1::ColorF(0,0));
    ComPtr<IDWriteTextFormat> font;
    checked(write_->CreateTextFormat(L"Times New Roman",nullptr,DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,88,L"en-us",&font),"BreakThrough lettering font");
    font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    constexpr wchar_t name[]=L"BREAKTHROUGH";
    canvas->DrawText(name,static_cast<UINT32>(std::size(name)-1),font.Get(),D2D1::RectF(0,0,1024,128),brush.Get());
    // A smaller echo of the name floats above the plate, as in the reference.
    ComPtr<IDWriteTextFormat> subtitleFormat;
    checked(write_->CreateTextFormat(L"Times New Roman",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_EXPANDED,38,L"en-us",&subtitleFormat),"BreakThrough small lettering font");
    subtitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);subtitleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    canvas->DrawText(name,static_cast<UINT32>(std::size(name)-1),subtitleFormat.Get(),D2D1::RectF(0,128,1024,256),brush.Get());
    checked(canvas->EndDraw(),"Render BreakThrough lettering");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&breakthroughLettering_),"BreakThrough lettering source");
}

void Renderer::buildBreakthrough(const Frame& f) {
    const float t=f.time,ox=f.avatarX,oz=f.avatarZ;
    const Color blue{.39f,.46f,1.f},silver{.83f,.88f,1.2f},white{1.6f,1.6f,1.75f};
    auto point=[&](float x,float y,float z){return XMVectorSet(ox+x,y,oz+z,1);};
    auto line=[&](XMVECTOR a,XMVECTOR b,float width,float energy){pixelLine(a,b,width,silver,energy,f);};
    auto block=[&](FXMMATRIX transform,Color color) {
        if(instances_.size()>=4096)return;
        Instance instance;XMStoreFloat4x4(&instance.world,transform);instance.color={color.r,color.g,color.b,.08f};
        instances_.push_back(instance);
    };
    // Seven broken glass monoliths. The tall central spine is surrounded by
    // staggered rear pillars and a pair that lean away from the floating avatar.
    struct Tower {float x,y,z,width,height,depth,tilt,turn;};
    const Tower towers[]={{0,4.05f,.65f,.86f,7.3f,.74f,0,.10f},
        {-2.15f,1.95f,.25f,.70f,3.9f,.64f,.42f,-.12f},{2.15f,1.95f,.25f,.70f,3.9f,.64f,-.42f,.12f},
        {-1.35f,3.35f,1.25f,.48f,3.5f,.50f,.10f,-.10f},{1.35f,3.35f,1.25f,.48f,3.5f,.50f,-.10f,.10f},
        {-.96f,2.2f,-.20f,.44f,2.3f,.50f,-.13f,.10f},{.96f,2.2f,-.20f,.44f,2.3f,.50f,.13f,-.10f}};
    for(unsigned n=0;n<std::size(towers);++n) {
        const auto c=towers[n];
        const float amplitude=.14f+.02f*static_cast<float>(n%3);
        const float period=3.6f+.35f*static_cast<float>(n%4);
        const float drift=amplitude*std::sin(t*(2*Pi/period)+static_cast<float>(n)*.91f);
        // Glass, core, attached fragments and cap glow all share this moving model.
        const auto model=XMMatrixRotationY(c.turn)*XMMatrixRotationZ(c.tilt)*XMMatrixTranslation(ox+c.x,c.y+drift,oz+c.z);
        // The opaque core supplies the dark negative space between glass details.
        block(XMMatrixScaling(c.width*.73f,c.height*.96f,c.depth*.72f)*model,{.018f,.022f,.052f});
        for(unsigned face=0;face<4;++face) {
            const float angle=face*Pi*.5f,w=face%2?c.depth:c.width,d=face%2?c.width:c.depth;
            pixelQuad(XMMatrixScaling(w,c.height,1)*XMMatrixTranslation(0,0,-d*.51f)*XMMatrixRotationY(angle)*model,
                silver,3,.75f);
        }
        for(unsigned face=0;face<4;++face) {
            const float sx=face&1?1.f:-1.f,sz=face&2?1.f:-1.f;
            line(XMVector3TransformCoord(XMVectorSet(sx*c.width*.51f,-c.height*.50f,sz*c.depth*.51f,1),model),
                 XMVector3TransformCoord(XMVectorSet(sx*c.width*.51f,c.height*.50f,sz*c.depth*.51f,1),model),.035f,.48f);
        }
        for(unsigned i=0;i<24;++i) {
            const unsigned seed=n*43+i;
            const float s=.10f+random(seed,3)*.22f,y=(random(seed,8)-.5f)*c.height;
            const float x=(random(seed,1)-.5f)*c.width*1.1f,z=(random(seed,5)>.5f?-1:1)*c.depth*.52f;
            block(XMMatrixScaling(s,s*(.6f+random(seed,2)*1.8f),s*.8f)*XMMatrixTranslation(x,y,z)*model,
                {.013f+random(seed,4)*.018f,.017f+random(seed,4)*.022f,.045f+random(seed,4)*.045f});
            if(i%3==0) {
                const auto p=XMVector3TransformCoord(XMVectorSet(x,y,z,1),model);
                pixelBillboard(XMVectorGetX(p),XMVectorGetY(p),XMVectorGetZ(p),s*.32f,s*.42f,silver,1,.55f,f);
            }
        }
        const auto top=XMVector3TransformCoord(XMVectorSet(0,c.height*.5f,0,1),model);
        pixelBillboard(XMVectorGetX(top),XMVectorGetY(top),XMVectorGetZ(top),1.4f,.16f,white,4,.65f,f);
        pixelBillboard(XMVectorGetX(top),XMVectorGetY(top),XMVectorGetZ(top),1.0f,1.0f,blue,0,.47f,f);
    }
    // Two interacting light sources and thin winding trails, rather than a solid cage.
    for(unsigned strand=0;strand<7;++strand) {
        XMVECTOR previous{};
        for(unsigned i=0;i<=84;++i) {
            const float u=i/84.f,a=u*Pi*2.2f+strand*.9f+t*(strand%2?.22f:-.27f);
            const float radius=2.1f+.40f*std::sin(u*Pi*2+strand*.8f+t*.3f);
            const float y=.18f+u*5.8f;
            const auto next=point(std::cos(a)*radius,y,std::sin(a)*radius);
            if(i)line(previous,next,.035f+strand*.003f,.17f+strand*.018f);
            previous=next;
        }
    }
    for(unsigned source=0;source<2;++source) {
        const float y=source?3.90f:.25f;
        pixelBillboard(ox,y,oz,3.4f,3.4f,blue,0,.75f,f);
        pixelBillboard(ox,y,oz,1.6f,1.6f,white,4,.94f,f,t*.035f);
        pixelBillboard(ox,y,oz,8.6f,.24f,white,4,source?.88f:.55f,f);
        pixelBillboard(ox,y,oz,3.9f,.045f,white,4,.87f,f);
        for(unsigned ray=0;ray<12;++ray) {
            const float a=ray*2*Pi/12+t*.033f,length=1.2f+random(ray,source)*1.65f;
            const auto orientation=XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw)*XMMatrixTranslation(ox,y,oz);
            line(XMVector3TransformCoord(XMVectorSet(std::cos(a)*.12f,std::sin(a)*.12f,0,1),orientation),
                 XMVector3TransformCoord(XMVectorSet(std::cos(a)*length,std::sin(a)*length,0,1),orientation),.045f,.21f);
        }
    }
    // A loosely wound floor vortex and cold, thin clouds travelling inward.
    for(unsigned ring=0;ring<2;++ring)for(unsigned i=0;i<96;++i) {
        const float a=i*2*Pi/96+t*(ring?-.3f:.22f),b=a+2*Pi/96,r=2.45f-ring*.30f;
        line(point(std::cos(a)*r,.15f+.04f*std::sin(a*3+t),std::sin(a)*r),
             point(std::cos(b)*r,.15f+.04f*std::sin(b*3+t),std::sin(b)*r),.06f,.27f);
    }
    for(unsigned i=0;i<52;++i) {
        const float age=std::fmod(t*.075f+random(i,4),1.f),a=i*2.399963f+t*.05f,r=3.4f*(1-age)+.25f;
        const float y=.25f+random(i,6)*4.8f,size=1.25f+random(i,7)*1.5f;
        const float fade=std::sin(age*Pi);
        pixelBillboard(ox+std::cos(a)*r,y,oz+std::sin(a)*r,size,size,blue,0,.36f*fade,f,i*.7f);
    }
    for(unsigned i=0;i<220;++i) {
        const float a=i*2.399963f+t*.10f,r=.65f+random(i,3)*2.5f;
        const float phase=std::fmod(i*.127f+t*.28f,7.4f),fade=std::min(1.f,phase*2)*std::min(1.f,(7.4f-phase)*2);
        const float size=.018f+random(i,7)*.045f;
        const float x=ox+std::cos(a)*r,z=oz+std::sin(a)*r;
        pixelBillboard(x,.16f+phase,z,size,size,silver,1,fade*.63f,f,t*.1f+i);
        if(i%21==0)pixelBillboard(x,.16f+phase,z,.30f,.30f,silver,4,fade*.46f,f);
    }
    const auto plate=XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw)*
        XMMatrixTranslation(ox+std::sin(f.cameraYaw)*1.8f,3.55f,oz-std::cos(f.cameraYaw)*1.8f);
    // Keep the narrow serif plate ahead of the avatar and darken only its backing.
    // A tiny opaque card preserves readable negative space under the shared bloom.
    block(XMMatrixScaling(3.55f,.40f,.025f)*XMMatrixTranslation(0,0,.035f)*plate,{.002f,.003f,.008f});
    const Color plateInk{.62f,.66f,.83f};
    pixelQuad(XMMatrixScaling(3.30f,.35f,1)*plate,plateInk,8,.78f);
    // The small echo sits below the title, leaving the upper star at y=3.9 clear.
    pixelQuad(XMMatrixScaling(1.8f,.14f,1)*XMMatrixTranslation(0,-.26f,0)*plate,silver,9,.67f);
    for(float y:{-.17f,.17f})line(XMVector3TransformCoord(XMVectorSet(-1.7f,y,0,1),plate),
        XMVector3TransformCoord(XMVectorSet(1.7f,y,0,1),plate),.021f,.65f);
}

void Renderer::drawBreakthroughCutscene(const Frame& frame) {
    if(frame.cutsceneTime<0 || frame.cutsceneTime>BreakthroughCutsceneDuration)return;
    const float constants[]={frame.cutsceneTime,static_cast<float>(width_)/height_,static_cast<float>(width_),static_cast<float>(height_)};
    context_->UpdateSubresource(breakthroughConstants_.Get(),0,nullptr,constants,0,0);
    ID3D11RenderTargetView* target=target_.Get();context_->OMSetRenderTargets(1,&target,nullptr);
    context_->OMSetDepthStencilState(effectDepth_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
    context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
    context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(bloomVs_.Get(),nullptr,0);context_->PSSetShader(breakthroughCutscenePs_.Get(),nullptr,0);
    ID3D11Buffer* cb=breakthroughConstants_.Get();context_->PSSetConstantBuffers(0,1,&cb);context_->Draw(3,0);
}

void Renderer::breakthroughCutsceneUi(const Frame& frame) {
    const float t=frame.cutsceneTime;
    if(t<0 || t>BreakthroughCutsceneDuration)return;
    const float factor=static_cast<float>(height_)/480,referenceWidth=static_cast<float>(width_)*480/height_;
    D2D1_MATRIX_3X2_F previous;d2d_->GetTransform(&previous);
    auto caption=[&](std::wstring_view value,float x,float y,float angle,float opacity=1.f,float size=14.f) {
        const float width=static_cast<float>(value.size())*size*.49f+14;
        d2d_->SetTransform(D2D1::Matrix3x2F::Rotation(angle,D2D1::Point2F(x,y))*D2D1::Matrix3x2F::Scale(factor,factor));
        rect(x-width*.5f,y-10,width,21,D2D1::ColorF(0,0,0,opacity*.94f));
        brush_->SetColor(D2D1::ColorF(.97f,.97f,1.f,opacity));
        if(size==14) d2d_->DrawText(value.data(),static_cast<UINT32>(value.size()),cutsceneFont_.Get(),
            D2D1::RectF(x-width*.5f+3,y-10,x+width*.5f-3,y+11),brush_.Get());
        else text(std::wstring(value),x-width*.5f,y-9,width,21,size,D2D1::ColorF(.97f,.97f,1.f,opacity),false,true);
    };
    const float center=referenceWidth*.5f;
    if(t>=1.12f && t<3.1f)caption(L"i - started with few",center-180,206,-3);
    if(t>=5.10f && t<7.0f)caption(L"ii - only less it gets..",center+162,307,2);
    if(t>=9.08f && t<10.85f)caption(L"iii - time by time, now empty",center,263,-17);
    if(t>=13.0f && t<14.12f)caption(L"iv - and there was nothing left",center+91,327,-14);
    if(t>=14.2f && t<15.0f) {
        d2d_->SetTransform(D2D1::Matrix3x2F::Scale(factor,factor));
        text(L"get out of my head",center-160,228,320,24,11,D2D1::ColorF(.83f,.83f,.86f),false,true);
    }
    if(t>=16.05f && t<25.4f)caption(L"it wasn't gone",center-210+std::min(t-16.05f,4.f)*6,285,-2);
    if(t>=20.0f && t<26.0f)caption(L"with only 5 fragments left",center+175,300,7);
    if(t>=22.35f && t<25.4f) {
        constexpr std::array<std::wstring_view,8> words={L"how",L"much",L"do",L"you",L"believe",L"in",L"this",L"world?"};
        const unsigned count=std::min(8u,static_cast<unsigned>((t-22.35f)*4.f)+1);
        for(unsigned i=0;i<count;++i)caption(words[i],center,105.f+static_cast<float>(i)*29.f,0);
    }
    if(t>=15.35f && t<28.9f) {
        constexpr std::array<std::wstring_view,7> numerals={L"I",L"II",L"III",L"IV",L"V",L"VI",L"VII"};
        d2d_->SetTransform(D2D1::Matrix3x2F::Scale(factor,factor));
        for(unsigned i=0;i<numerals.size();++i) {
            const float x=center+(random(i,2)-.5f)*referenceWidth*.83f;
            const float y=91+random(i,6)*283;
            const float opacity=.37f+.34f*std::sin(t*.45f+i)*std::sin(t*.45f+i);
            text(std::wstring(numerals[i]),x-25,y,50,24,11,D2D1::ColorF(.91f,.91f,1.f,opacity),true,true);
        }
    }
    d2d_->SetTransform(D2D1::Matrix3x2F::Scale(factor,factor));
    const bool japanese=frame.presentation.settings.language==Language::Japanese;
    rect(referenceWidth-112,8,104,23,D2D1::ColorF(0,0,0,.40f),3);
    text(japanese?L"Esc / スキップ":L"Esc / Skip",referenceWidth-108,12,96,17,10,D2D1::ColorF(1,1,1,.82f),false,true);
    d2d_->SetTransform(previous);
}
} // namespace aura
