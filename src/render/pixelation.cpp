#include "render/renderer.hpp"
#include "tuning/pixelation.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace aura {
using namespace DirectX;
namespace px=tuning::pixelation;
namespace {
constexpr float Pi=3.14159265359f;
// コード地図:
// 1. PixelShader / BloomShader: GPU で光を描く低レベル部分（普段の調整は不要）。
// 2. createPixelation〜pixelText: GPU の準備と「板・線・文字」を描く共通部品。
// 3. buildPixelation: 下にある演出本体。サイズ・個数・速度の入口は tuning/pixelation.hpp。
// 画像を毎フレーム読み込まず、小さな板をまとめて描いて発光させています。
constexpr char PixelShader[]=R"(
cbuffer Camera : register(b0) { row_major float4x4 viewProjection; float4 eyeAndTime; };
Texture2D lettering : register(t0); SamplerState linearClamp : register(s0);
struct Input { float4 p:SV_POSITION; float3 n:NORMAL; float4 color:COLOR0; float3 world:TEXCOORD0; float3 local:TEXCOORD1; };
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float4 main(Input i):SV_TARGET {
    float2 q=i.local.xy*2;
    int style=(int)floor(i.color.a+.00001);
    float opacity=frac(i.color.a)*1.011;
    float r2=dot(q,q),edge=max(abs(q.x),abs(q.y)),mask=0;
    if(style==0) { // Soft emission around a light source (bounded, inexpensive bloom).
        mask=exp(-r2*4.2)*.24*(1-smoothstep(.78,1,edge));
    } else if(style==1) { // Crisp luminous square with a soft fringe.
        mask=(1-smoothstep(.48,.64,edge))*.90+exp(-r2*3.2)*.28;
    } else if(style==2) { // Thin bright line and its wider Gaussian halo.
        float ends=1-smoothstep(.89,1,abs(q.x));
        mask=(exp(-q.y*q.y*280)*1.8+exp(-q.y*q.y*13)*.36)*ends;
    } else if(style==3) { // Translucent HUD, pixel grid, rolling scan line, frame.
        float frame=exp(-abs(edge-.92)*100)*.75;
        float2 grid=abs(frac((q+1)*float2(20,12))-.5);
        float lattice=(1-smoothstep(.025,.07,min(grid.x,grid.y)))*.035;
        float scan=exp(-abs(q.y-(frac(eyeAndTime.w*.55)*2-1))*42)*.20;
        float noise=hash(floor((q+1)*float2(31,19))+floor(eyeAndTime.w*7));
        mask=(.023+lattice+scan+step(.96,noise)*.09+frame)*(1-smoothstep(.96,1,edge));
    } else if(style==5) {
        mask=exp(-r2*13)*1.6+exp(-r2*3.2)*.35;
    } else if(style==6) {
        float triangleEdge=max(abs(q.x)*1.73+q.y,-q.y*2);
        mask=(1-smoothstep(.72,.91,triangleEdge))*.75;
    } else if(style==7) { // Expanding impact ring with a bright edge and a soft pressure wave.
        float radius=sqrt(r2);
        mask=(exp(-abs(radius-.72)*95)*1.65+exp(-abs(radius-.72)*17)*.42)*(1-smoothstep(.90,1,radius));
    } else if(style>=8) {
        float2 uv=float2(q.x*.5+.5,.5-q.y*.5);
        if(style==9) uv.x=.4375+uv.x*.125;
        uv.y=(uv.y+(style-8))*.25;
        mask=lettering.Sample(linearClamp,uv).a;
    } else { // Anamorphic sparkle, stronger around the white center.
        float cross=exp(-abs(q.x)*65)*exp(-abs(q.y)*2.8)+exp(-abs(q.y)*65)*exp(-abs(q.x)*2.8);
        mask=exp(-r2*15)*.8+cross*1.7;
    }
    return float4(i.color.rgb*mask,opacity);
})";
constexpr char BloomShader[]=R"(
Texture2D source : register(t0);
Texture2D blurred : register(t1);
SamplerState linearClamp : register(s0);
cbuffer Blur : register(b0) { float4 stepAndGain; float4 bounds; };
struct Output { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };
Output vertex(uint id:SV_VertexID) {
    Output o; o.uv=float2((id<<1)&2,id&2);
    o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float4 blur(Output i):SV_TARGET {
    float2 delta=stepAndGain.xy;
    float3 c=source.Sample(linearClamp,i.uv).rgb*.227027;
    c+=(source.Sample(linearClamp,i.uv+delta*1.384615).rgb+source.Sample(linearClamp,i.uv-delta*1.384615).rgb)*.316216;
    c+=(source.Sample(linearClamp,i.uv+delta*3.230769).rgb+source.Sample(linearClamp,i.uv-delta*3.230769).rgb)*.070270;
    return float4(c,1);
}
float4 composite(Output i):SV_TARGET {
    clip(min(min(i.uv.x-bounds.x,bounds.z-i.uv.x),min(i.uv.y-bounds.y,bounds.w-i.uv.y)));
    float3 core=source.Sample(linearClamp,i.uv).rgb;
    float3 glow=blurred.Sample(linearClamp,i.uv).rgb;
    return float4(core+glow*2.8,1);
})";

unsigned char glyph(char c,unsigned row) {
    // Five by seven original bitmap lettering for the orbiting holograms.
    const unsigned char* p=nullptr;
    static const unsigned char P[]={30,17,17,30,16,16,16}, I[]={31,4,4,4,4,4,31};
    static const unsigned char X[]={17,17,10,4,10,17,17}, E[]={31,16,16,30,16,16,31};
    static const unsigned char L[]={16,16,16,16,16,16,31}, A[]={14,17,17,31,17,17,17};
    static const unsigned char T[]={31,4,4,4,4,4,4}, O[]={14,17,17,17,17,17,14};
    static const unsigned char N[]={17,25,25,21,19,19,17}, Y[]={17,17,10,4,4,4,4};
    static const unsigned char R[]={30,17,17,30,20,18,17}, D[]={30,17,17,17,17,17,30};
    static const unsigned char Q[]={14,17,17,17,21,18,13}, U[]={17,17,17,17,17,17,14};
    static const unsigned char one[]={4,12,4,4,4,4,14}, two[]={14,17,1,2,4,8,31};
    switch(c) {case 'P':p=P;break;case 'I':p=I;break;case 'X':p=X;break;case 'E':p=E;break;
        case 'L':p=L;break;case 'A':p=A;break;case 'T':p=T;break;case 'O':p=O;break;case 'N':p=N;break;
        case 'Y':p=Y;break;case 'R':p=R;break;case 'D':p=D;break;case 'Q':p=Q;break;case 'U':p=U;break;
        case '1':p=one;break;case '2':p=two;break;default:break;}
    return p?p[row]:0;
}
Color bright(Color c,float strength,float white=0) {
    return {c.r*strength+white,c.g*strength+white,c.b*strength+white};
}
}

Color pixelationColor(float time) {
    const float phase=time*px::ColorCycleSpeed-std::floor(time*px::ColorCycleSpeed);
    const auto& keys=px::ColorKeyPositions;
    const auto& colors=px::ColorPalette;
    for(unsigned i=0;i+1<px::ColorKeyCount;++i) if(phase<=keys[i+1]) {
        float u=(phase-keys[i])/(keys[i+1]-keys[i]); u=u*u*(3-2*u);
        const auto a=colors[i],b=colors[i+1]; return {a.r+(b.r-a.r)*u,a.g+(b.g-a.g)*u,a.b+(b.b-a.b)*u};
    }
    return {colors[0].r,colors[0].g,colors[0].b};
}
void Renderer::createPixelation() {
    ComPtr<ID3DBlob> shader,errors;
    const HRESULT hr=D3DCompile(PixelShader,sizeof(PixelShader)-1,"pixelation.hlsl",nullptr,nullptr,"main","ps_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors);
    if(FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    checked(hr,"Compile Pixelation shader");
    checked(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&pixelationPs_),"Pixelation shader");
    D3D11_BLEND_DESC desc{}; auto& rt=desc.RenderTarget[0]; rt.BlendEnable=TRUE;
    rt.SrcBlend=D3D11_BLEND_SRC_ALPHA; rt.DestBlend=D3D11_BLEND_ONE; rt.BlendOp=D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha=D3D11_BLEND_ZERO; rt.DestBlendAlpha=D3D11_BLEND_ONE; rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    checked(device_->CreateBlendState(&desc,&additiveBlend_),"Additive emission blending");
    auto compile=[&](const char* entry,const char* profile,ID3DBlob** output) {
        errors.Reset(); const HRESULT result=D3DCompile(BloomShader,sizeof(BloomShader)-1,"bloom.hlsl",nullptr,nullptr,entry,profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,output,&errors);
        if(FAILED(result) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        checked(result,"Compile bloom shader");
    };
    shader.Reset(); compile("vertex","vs_4_0",&shader);
    checked(device_->CreateVertexShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&bloomVs_),"Bloom vertex shader");
    shader.Reset(); compile("blur","ps_4_0",&shader);
    checked(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&blurPs_),"Bloom blur shader");
    shader.Reset(); compile("composite","ps_4_0",&shader);
    checked(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&compositePs_),"Bloom composite shader");
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=32; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.Usage=D3D11_USAGE_DEFAULT;
    checked(device_->CreateBuffer(&cb,nullptr,&blurConstants_),"Bloom constants");
    D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD=D3D11_FLOAT32_MAX;
    checked(device_->CreateSamplerState(&sampler,&bloomSampler_),"Bloom sampler");
    pixelationInstances_.reserve(2500);
}
void Renderer::createPixelationTargets() {
    if(bloomWidth_==width_ && bloomHeight_==height_) return;
    // Allocate on first preview and cache until resize. Other auras skip these passes.
    for(unsigned i=0;i<3;++i) {
        bloomTargets_[i].Reset(); bloomSources_[i].Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=i?std::max(1u,width_/2):width_; desc.Height=i?std::max(1u,height_/2):height_;
        desc.MipLevels=desc.ArraySize=1; desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R11G11B10_FLOAT; desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture; checked(device_->CreateTexture2D(&desc,nullptr,&texture),"Bloom texture");
        checked(device_->CreateRenderTargetView(texture.Get(),nullptr,&bloomTargets_[i]),"Bloom render target");
        checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&bloomSources_[i]),"Bloom source");
    }
    bloomWidth_=width_; bloomHeight_=height_;
}
void Renderer::compositePixelation() {
    D3D11_VIEWPORT previous{}; UINT count=1; context_->RSGetViewports(&count,&previous);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(bloomVs_.Get(),nullptr,0);
    ID3D11Buffer* cb=blurConstants_.Get(); context_->PSSetConstantBuffers(0,1,&cb);
    ID3D11SamplerState* sampler=bloomSampler_.Get(); context_->PSSetSamplers(0,1,&sampler);
    context_->OMSetDepthStencilState(effectDepth_.Get(),0); context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    struct Blur {XMFLOAT4 step,bounds;} values{};
    values.bounds={previous.TopLeftX/width_,previous.TopLeftY/height_,
        (previous.TopLeftX+previous.Width)/width_,(previous.TopLeftY+previous.Height)/height_};
    ID3D11ShaderResourceView* empty[]={nullptr,nullptr};
    D3D11_VIEWPORT half{0,0,static_cast<float>(std::max(1u,width_/2)),static_cast<float>(std::max(1u,height_/2)),0,1};
    context_->RSSetViewports(1,&half); context_->PSSetShader(blurPs_.Get(),nullptr,0);
    for(unsigned pass=0;pass<2;++pass) {
        context_->PSSetShaderResources(0,2,empty);
        ID3D11RenderTargetView* target=bloomTargets_[pass+1].Get(); context_->OMSetRenderTargets(1,&target,nullptr);
        ID3D11ShaderResourceView* source=bloomSources_[pass].Get(); context_->PSSetShaderResources(0,1,&source);
        values.step=pass?XMFLOAT4{0,3.5f/height_,0,0}:XMFLOAT4{3.5f/width_,0,0,0};
        context_->UpdateSubresource(blurConstants_.Get(),0,nullptr,&values,0,0); context_->Draw(3,0);
    }
    context_->PSSetShaderResources(0,2,empty);
    ID3D11RenderTargetView* target=target_.Get(); context_->OMSetRenderTargets(1,&target,nullptr);
    ID3D11ShaderResourceView* sources[]={bloomSources_[0].Get(),bloomSources_[2].Get()}; context_->PSSetShaderResources(0,2,sources);
    D3D11_VIEWPORT full{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1}; context_->RSSetViewports(1,&full);
    context_->PSSetShader(compositePs_.Get(),nullptr,0); context_->OMSetBlendState(additiveBlend_.Get(),nullptr,0xffffffff);
    context_->Draw(3,0);
    context_->PSSetShaderResources(0,2,empty); context_->RSSetViewports(1,&previous);
}
void Renderer::pixelQuad(FXMMATRIX transform,Color color,unsigned style,float opacity) {
    if(pixelationInstances_.size()>=4096 || opacity<=0) return;
    Instance instance; XMStoreFloat4x4(&instance.world,transform);
    instance.color={color.r,color.g,color.b,static_cast<float>(style)+std::clamp(opacity,0.f,.99f)};
    pixelationInstances_.push_back(instance);
}
void Renderer::pixelBillboard(float x,float y,float z,float width,float height,Color color,unsigned style,float opacity,const Frame& f,float rotation) {
    pixelQuad(XMMatrixScaling(width,height,1)*XMMatrixRotationZ(rotation)*XMMatrixRotationX(f.cameraPitch)*
        XMMatrixRotationY(-f.cameraYaw)*XMMatrixTranslation(x,y,z),color,style,opacity);
}
void Renderer::pixelLine(FXMVECTOR from,FXMVECTOR to,float width,Color color,float opacity,const Frame& f) {
    const auto delta=XMVectorSubtract(to,from);
    if(XMVectorGetX(XMVector3LengthSq(delta))<.000001f) return;
    const auto middle=XMVectorScale(XMVectorAdd(from,to),.5f);
    const auto eye=XMVectorSet(f.avatarX+std::sin(f.cameraYaw)*std::cos(f.cameraPitch)*f.cameraDistance,
        2+std::sin(f.cameraPitch)*f.cameraDistance,f.avatarZ-std::cos(f.cameraYaw)*std::cos(f.cameraPitch)*f.cameraDistance,1);
    const auto normal=XMVector3Normalize(XMVectorSubtract(middle,eye));
    auto side=XMVector3Cross(normal,delta);
    if(XMVectorGetX(XMVector3LengthSq(side))<.000001f) return;
    side=XMVectorScale(XMVector3Normalize(side),width);
    const XMMATRIX transform(XMVectorSetW(delta,0),XMVectorSetW(side,0),XMVectorSetW(normal,0),XMVectorSetW(middle,1));
    pixelQuad(transform,color,2,opacity);
}
void Renderer::pixelText(const char* value,float x,float y,float z,float size,Color color,const Frame& f) {
    const auto count=std::strlen(value);
    const auto orientation=XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw)*XMMatrixTranslation(x,y,z);
    for(std::size_t i=0;i<count;++i) for(unsigned row=0;row<7;++row) {
        const auto bits=glyph(value[i],row);
        for(unsigned col=0;col<5;++col) if(bits&(16>>col)) {
            const float px=(static_cast<float>(i)*6+col-static_cast<float>(count*6-1)*.5f)*size;
            pixelQuad(XMMatrixScaling(size*1.60f,size*1.60f,1)*XMMatrixTranslation(px,(3.f-row)*size,0)*orientation,color,1,.93f);
        }
    }
}
void Renderer::buildPixelation(const Frame& source) {
    // ① 時計と座標。t は周囲の回転、actionTime は弾・爆発を担当します。
    // Lv3 の t だけ速くしても、弾の当たるタイミングと浮遊は Lv2 と一致します。
    Frame f=source;
    const auto level=f.presentation.level(f.progress,f.roller.catalog());
    f.time=f.effectTime>=0?f.effectTime:pixelationMotionTime(f.time,level);
    const auto color=pixelationColor(f.time),hot=bright(color,1.65f,.15f);
    const float t=f.time,ox=f.avatarX,oz=f.avatarZ;
    const float actionTime=source.actionTime>=0?source.actionTime:pixelationActionTime(source.time,level);
    const float shotOrbitTime=source.shotOrbitTime>=0?source.shotOrbitTime:
        std::max(0.f,t-pixelationVolley(actionTime).shotAge*pixelationMotionSpeed(level)/pixelationActionSpeed(level));
    const auto volley=pixelationVolley(actionTime,t,shotOrbitTime);
    auto point=[&](float x,float y,float z) {return XMVectorSet(ox+x,y,oz+z,1);};
    auto line=[&](XMVECTOR a,XMVECTOR b,float width=.10f,float energy=.8f) {pixelLine(a,b,width,hot,energy,f);};
    auto ring=[&](float radius,float y,float tilt,float turn,float energy) {
        const auto transform=XMMatrixRotationX(tilt)*XMMatrixRotationY(turn)*XMMatrixTranslation(ox,y,oz);
        for(unsigned i=0;i<px::FloorRingSegments;++i) {
            const float a=i*2*Pi/px::FloorRingSegments,b=(i+1)*2*Pi/px::FloorRingSegments;
            // 一部を欠けさせて、その欠け位置を動かすとデジタルな輪に見えます。
            if((i+static_cast<unsigned>(t*14))%24==0) continue;
            line(XMVector3TransformCoord(XMVectorSet(std::cos(a)*radius,0,std::sin(a)*radius,1),transform),
                 XMVector3TransformCoord(XMVectorSet(std::cos(b)*radius,0,std::sin(b)*radius,1),transform),.11f,energy);
        }
    };
    // ② 足元の輪と縦のピクセル。radius は輪の半径、y は床からの高さです。
    ring(px::OuterRingRadius,.21f,0,t*.32f,.70f); ring(px::MiddleRingRadius,.25f,.035f,-t*.3f,.30f);
    ring(px::UpperRingRadius,.70f,.12f,t*.5f,.32f);
    pixelQuad(XMMatrixScaling(px::FloorGlowSize,px::FloorGlowSize,1)*XMMatrixRotationX(Pi*.5f)*XMMatrixTranslation(ox,.19f,oz),color,0,.75f);
    for(unsigned i=0;i<px::FloorPillarCount;++i) {
        const float a=i*2*Pi/px::FloorPillarCount+t*.25f,r=px::FloorPillarRadius+.12f*std::sin(i*2.f);
        const float height=.08f+.40f*std::pow(.5f+.5f*std::sin(i*3.7f+t*3),3.f);
        line(point(std::cos(a)*r,.23f,std::sin(a)*r),point(std::cos(a)*r,.23f+height,std::sin(a)*r),.11f,.45f);
        if(i%2==0) pixelBillboard(ox+std::cos(a)*1.84f,.77f+.08f*std::sin(t+i),oz+std::sin(a)*1.84f,.13f,.13f,hot,1,.70f,f,t*.2f);
    }
    // ③ アバターを囲む立方体。拡大→回転→配置の順で変換します。
    for(unsigned cube=0;cube<px::CageCount;++cube) {
        const float size=px::CageSize+cube*px::CageSizeStep;
        const auto transform=XMMatrixScaling(size,size*1.15f,size)*XMMatrixRotationX(.27f+t*(.36f+cube*.14f))*
            XMMatrixRotationZ(.30f+cube*.48f+t*.22f)*XMMatrixRotationY(t*.65f+cube*.9f)*XMMatrixTranslation(ox,px::CageHeight,oz);
        XMVECTOR corners[8];
        for(unsigned i=0;i<8;++i) corners[i]=XMVector3TransformCoord(XMVectorSet(i&1?.5f:-.5f,i&2?.5f:-.5f,i&4?.5f:-.5f,1),transform);
        for(unsigned i=0;i<8;++i) for(unsigned bit=1;bit<=4;bit*=2) if(!(i&bit)) line(corners[i],corners[i|bit],.12f,cube?.33f:.75f);
    }
    // ④ PLAYER1/2 の画面。実際の周回・弾道の座標計算は core/animation.cpp にあります。
    for(unsigned player=0;player<2;++player) {
        const float playerX=volley.players[player].x,playerZ=volley.players[player].z,playerY=volley.players[player].y;
        const auto rotation=XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw);
        const auto screen=rotation*XMMatrixTranslation(ox+playerX,playerY,oz+playerZ);
        pixelQuad(XMMatrixScaling(px::PlayerScreenWidth,px::PlayerScreenHeight,1)*screen,color,3,.9f);
        pixelText(player?"PLAYER2":"PLAYER1",ox+playerX,playerY+.68f,oz+playerZ,.025f,hot,f);
        pixelText(player?"P2":"P1",ox+playerX,playerY+.48f,oz+playerZ,.018f,color,f);
        // 小さな線を組み合わせ、画面内の人形を描きます。
        auto screenLine=[&](float ax,float ay,float bx,float by,float w) {
            line(XMVector3TransformCoord(XMVectorSet(ax,ay,0,1),screen),XMVector3TransformCoord(XMVectorSet(bx,by,0,1),screen),w,.55f);
        };
        screenLine(-.12f,.26f,.12f,.26f,.12f); screenLine(-.17f,.02f,.17f,.02f,.20f);
        screenLine(-.30f,-.02f,-.18f,-.15f,.08f); screenLine(.18f,-.15f,.30f,-.02f,.08f);
        screenLine(-.12f,-.16f,-.18f,-.31f,.09f); screenLine(.12f,-.16f,.18f,-.31f,.09f);
        const auto orbit=XMMatrixRotationZ(.42f+player*.3f)*XMMatrixRotationX(.4f)*XMMatrixRotationY(t*.3f)*XMMatrixTranslation(ox+playerX,playerY,oz+playerZ);
        for(unsigned i=0;i<px::PlayerOrbitSegments;++i) {
            const float a=i*2*Pi/px::PlayerOrbitSegments,b=(i+1)*2*Pi/px::PlayerOrbitSegments;
            line(XMVector3TransformCoord(XMVectorSet(std::cos(a)*.78f,std::sin(a)*.60f,0,1),orbit),
                 XMVector3TransformCoord(XMVectorSet(std::cos(b)*.78f,std::sin(b)*.60f,0,1),orbit),.075f,.40f);
        }
        // 画面からアバターへ伸びる線を、小さく揺らしてギザギザにします。
        XMVECTOR previous=point(playerX,playerY,playerZ);
        for(unsigned i=1;i<=px::TetherSegments;++i) {
            const float u=i/static_cast<float>(px::TetherSegments),jitter=.13f*std::sin(i*7.1f+std::floor(t*15));
            const auto next=point(playerX*(1-u)+jitter,playerY+(2.35f-playerY)*u+jitter*.7f,playerZ*(1-u));
            line(previous,next,.085f,.48f); previous=next;
        }
    }
    if(level>=2) {
        // ⑤ Lv2 以上の弾と爆発。両方とも actionTime を使い、Lv3 でも速度を変えません。
        if(volley.flightVisible) for(unsigned player=0;player<2;++player) {
            const auto ball=volley.balls[player];
            const Color white{1.8f,1.8f,1.8f};
            pixelBillboard(ox+ball.x,ball.y,oz+ball.z,px::ProjectileSize,px::ProjectileSize,hot,5,.98f,f);
            pixelBillboard(ox+ball.x,ball.y,oz+ball.z,px::ProjectileCoreSize,px::ProjectileCoreSize,white,5,.85f,f);
            for(unsigned trail=1;trail<=px::ProjectileTrailCount;++trail) {
                const auto past=pixelationVolley(std::max(0.f,actionTime-trail*px::ProjectileTrailStep),t,shotOrbitTime);
                if(past.shotCycle!=volley.shotCycle) continue;
                const auto p=past.balls[player];
                const float size=.15f-trail*.015f;
                pixelBillboard(ox+p.x,p.y,oz+p.z,size,size,hot,5,.42f*(1-trail/7.f),f);
            }
        }
        constexpr float impactDuration=px::ImpactDuration;
        if(volley.impactAge>=0 && volley.impactAge<impactDuration) {
            // meeting は 2 発共通の衝突点。白い閃光→広がる輪・破片の順に見せます。
            const float u=volley.impactAge/impactDuration,fade=(1-u)*(1-u);
            const float force=level>=3?px::FinalImpactScale:1.f,r=(px::ImpactStartRadius+px::ImpactExpansion*(1-std::pow(1-u,3.f)))*force;
            const float flash=std::exp(-u*15.f),glow=std::exp(-u*5.f);
            const auto c=volley.meeting;
            const Color white{2.5f,2.5f,2.5f},blast=bright(color,2.2f,.28f);
            pixelBillboard(ox+c.x,c.y,oz+c.z,1.65f*force,1.65f*force,white,5,flash*.98f,f);
            pixelBillboard(ox+c.x,c.y,oz+c.z,(3.0f+u*2)*force,(3.0f+u*2)*force,blast,0,glow*.95f,f);
            pixelBillboard(ox+c.x,c.y,oz+c.z,(4.4f+u*2)*force,.60f*force,white,4,flash*.94f,f);
            pixelBillboard(ox+c.x,c.y,oz+c.z,1.7f*force,1.7f*force,white,4,glow*.86f,f,Pi*.25f);
            for(unsigned wave=0;wave<px::ImpactWaveCount;++wave) {
                const float radius=r*(wave?.69f:1.f);
                pixelBillboard(ox+c.x,c.y,oz+c.z,radius*2.7f,radius*2.7f,blast,7,fade*(wave?.48f:.90f),f);
            }
            for(unsigned i=0;i<px::ImpactShardCount;++i) {
                const float azimuth=i*2.399963f+static_cast<float>(volley.impactCycle%16)*.43f;
                const float dy=1.f-2.f*(i+.5f)/static_cast<float>(px::ImpactShardCount),horizontal=std::sqrt(std::max(0.f,1-dy*dy));
                const float dx=std::cos(azimuth)*horizontal,dz=std::sin(azimuth)*horizontal;
                const float reach=r*(.70f+.30f*std::sin(i*4.7f)*std::sin(i*4.7f));
                const float size=(.075f+.075f*(i%3)/2.f)*(1-u)*force;
                const auto end=point(c.x+dx*reach,c.y+dy*reach-.25f*u*u,c.z+dz*reach);
                if(i%2==0) pixelLine(point(c.x+dx*reach*.62f,c.y+dy*reach*.62f,c.z+dz*reach*.62f),
                    end,.045f*force,blast,fade*.90f,f);
                pixelBillboard(ox+c.x+dx*reach,c.y+dy*reach-.25f*u*u,oz+c.z+dz*reach,
                    size,size,i%4?blast:white,i%5?1:4,fade*.95f,f,azimuth+u*5);
            }
        }
    }
    // ⑥ 名前・大きなにじみ・周囲の破片。これも周囲用の Motion 時計です。
    pixelText("PIXELATION",ox,px::NameHeight,oz+px::NameDepth,px::NamePixelSize,hot,f);
    const float towardX=std::sin(f.cameraYaw)*std::cos(f.cameraPitch),towardZ=-std::cos(f.cameraYaw)*std::cos(f.cameraPitch);
    pixelBillboard(ox,2.45f,oz,6.8f,7.3f,color,0,.68f,f);
    pixelBillboard(ox+towardX*.75f,2.20f,oz+towardZ*.75f,3.2f,3.7f,hot,0,.78f,f);
    pixelBillboard(ox+towardX*.75f,2.9f,oz+towardZ*.75f,1.3f,1.3f,hot,4,.70f,f,t*.15f);
    for(unsigned i=0;i<px::FragmentCount;++i) {
        const float u=i/static_cast<float>(px::FragmentCount),a=i*2.399963f+t*(i%2?.22f:-.18f),radius=px::FragmentInnerRadius+px::FragmentRadiusSpread*(.5f+.5f*std::sin(i*4.37f));
        const float phase=std::fmod(u*px::FragmentRiseRange+t*px::FragmentRiseSpeed,px::FragmentRiseRange),fade=std::min(1.f,phase*2)*std::min(1.f,(px::FragmentRiseRange-phase)*2);
        const float size=px::FragmentMinSize+px::FragmentSizeVariation*(.5f+.5f*std::sin(i*9.7f));
        const float x=ox+std::cos(a)*radius,y=.12f+phase,z=oz+std::sin(a)*radius;
        pixelBillboard(x,y,z,size,size,hot,1,fade*(.40f+.35f*std::sin(t*3+i)*std::sin(t*3+i)),f,t*.3f+i);
        if(i%9==0) pixelBillboard(x,y,z,.38f,.38f,hot,4,fade*.60f,f);
    }
    // ⑦ 短い横線の走査エフェクト。画面全体の点滅ではなく、アバター周囲だけに配置します。
    for(unsigned i=0;i<px::ScanStreakCount;++i) {
        const float phase=std::fmod(t*.6f+i*.31f,1.f),a=i*3.2f+t*.25f;
        const float x=std::cos(a)*2.25f,z=std::sin(a)*2.25f,y=.5f+phase*4.1f;
        const auto orientation=XMMatrixRotationY(-f.cameraYaw)*XMMatrixTranslation(ox+x,y,oz+z);
        line(XMVector3TransformCoord(XMVectorSet(-.22f,0,0,1),orientation),XMVector3TransformCoord(XMVectorSet(.22f,0,0,1),orientation),.045f,.23f);
    }
}
} // namespace aura
