#include "render/renderer.hpp"
#include "core/equinox_animation.hpp"
#include "tuning/equinox.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// 読む順番: buildEquinox()で部品の配置 → createEquinox()で描画準備。
// 普段の見た目調整はtuning/equinox.hppへ。下のHLSLは「各ピクセルを何色にするか」
// をGPUで計算する専門部分なので、サイズ・位置・量の変更には触る必要がありません。

namespace aura {
using namespace DirectX;
namespace {
constexpr float Pi=3.14159265359f;
namespace settings=tuning::equinox;
constexpr char OrbShader[]=R"(
cbuffer Camera:register(b0) { row_major float4x4 viewProjection; float4 eyeAndTime; };
Texture2D lettering:register(t0); SamplerState linearClamp:register(s0);
struct Input { float4 p:SV_POSITION; float3 n:NORMAL; float4 color:COLOR0; float3 world:TEXCOORD0; float3 local:TEXCOORD1; };
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float noise(float2 p) { float2 v=floor(p),u=frac(p); u=u*u*(3-2*u); return lerp(lerp(hash(v),hash(v+float2(1,0)),u.x),lerp(hash(v+float2(0,1)),hash(v+1),u.x),u.y); }
float cloud(float2 p) { return noise(p)*.53+noise(p*2.07+7)*.28+noise(p*4.1-9)*.13+noise(p*8.3)*.06; }
float volumeHash(float3 p) { return frac(sin(dot(p,float3(127.1,311.7,74.7)))*43758.5453); }
float volumeNoise(float3 p) {
    float3 v=floor(p),u=frac(p);u=u*u*(3-2*u);
    float4 a=float4(volumeHash(v),volumeHash(v+float3(1,0,0)),volumeHash(v+float3(0,1,0)),volumeHash(v+float3(1,1,0)));
    float4 b=float4(volumeHash(v+float3(0,0,1)),volumeHash(v+float3(1,0,1)),volumeHash(v+float3(0,1,1)),volumeHash(v+1));
    return lerp(lerp(lerp(a.x,a.y,u.x),lerp(a.z,a.w,u.x),u.y),
                lerp(lerp(b.x,b.y,u.x),lerp(b.z,b.w,u.x),u.y),u.z);
}
float stormDensity(float3 p,float time) {
    // 雷雲の密度。細い筋模様を使わず、柔らかい立体ノイズ全体を時間で流動させる。
    p=p*2.35-float3(time*.17,time*.24,-time*.11);
    p+=float3(sin(p.y*.81+time*.43),cos(p.z*.73-time*.39),sin(p.x*.89+time*.31))*.38;
    float n=volumeNoise(p)*.68+volumeNoise(p*1.83+float3(13,7,19))*.32;
    return smoothstep(.29,.74,n);
}
float segmentDistance(float2 p,float2 a,float2 b) {
    float2 d=b-a;return length(p-a-d*saturate(dot(p-a,d)/dot(d,d)));
}
float4 main(Input i):SV_TARGET {
    float2 q=i.local.xy*2; float r=length(q),a=atan2(q.y,q.x),time=eyeAndTime.w;
    int style=(int)floor(i.color.a);
    if(style==3) {
        // 第2行の左半分に4文字を配置。元の衡と同じ128px四方で字の大きさを保つ。
        float2 uv=float2((i.color.r+q.x*.5+.5)*.125,(1.5-q.y*.5)*.25);
        float alpha=lettering.Sample(linearClamp,uv).a;
        float c=.82+.10*sin(time*.8);
        return float4(c,c,c,alpha);
    }
    if(style==4 || style==5) {
        float2 uv=float2(q.x*.5+.5,(.5-q.y*.5+(style==5?2:0))*.25);
        float alpha=lettering.Sample(linearClamp,uv).a;
        float2 texel=float2(1.5/1024.,1.5/512.);
        float border=max(max(lettering.Sample(linearClamp,uv+float2(texel.x,0)).a,
                             lettering.Sample(linearClamp,uv-float2(texel.x,0)).a),
                         max(lettering.Sample(linearClamp,uv+float2(0,texel.y)).a,
                             lettering.Sample(linearClamp,uv-float2(0,texel.y)).a));
        // 玉と同じ拍で切り替える。別の秒時計を使わないので音楽調整後も一致する。
        bool black=i.color.r>.5;
        float c=black?.009:.99,edge=black?.60:.025;
        float outAlpha=saturate(alpha+max(0,border-alpha)*.48);
        return float4(lerp(edge.xxx,c.xxx,alpha/max(.0001,outAlpha)),outAlpha);
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
    float z=sqrt(saturate(1-r*r));
    // 球の奥行きを5層に分け、手前の雲による光の吸収を重ねて煙の厚みを作る。
    float transmission=1,cloudLight=0,stepLength=2*z/5;
    [unroll]for(int layer=0;layer<5;++layer) {
        float3 p=float3(q,z-(layer+.5)*stepLength);
        float density=stormDensity(p,time);
        float opacity=1-exp(-density*stepLength*1.9);
        float lighting=.41+.55*saturate(.5+p.y*.34+p.z*.27);
        cloudLight+=transmission*opacity*lighting;
        transmission*=1-opacity;
    }
    // 不規則な二連フラッシュ。雲の背後の光と短い稲妻を組み合わせる。
    float clock=time*.71+style*.37,cycle=floor(clock),phase=frac(clock);
    float flash=(exp(-phase*65)+.65*exp(-abs(phase-.075)*100))*step(phase,.17);
    float2 flashPoint=float2(hash(float2(cycle,style+3)),hash(float2(cycle,style+11)))*.75-.375;
    float diffuse=exp(-dot(q-flashPoint,q-flashPoint)*5.5)*flash*(.24+.76*transmission);
    float bolt=min(segmentDistance(q,flashPoint+float2(-.10,.35),flashPoint+float2(.08,.10)),
                   min(segmentDistance(q,flashPoint+float2(.08,.10),flashPoint+float2(-.08,-.07)),
                       segmentDistance(q,flashPoint+float2(-.08,-.07),flashPoint+float2(.13,-.32))));
    float lightning=exp(-bolt*115)*flash*(.12+.32*transmission);
    float rings=exp(-abs(r-.95)*180)+exp(-abs(r-.87)*180)*.28;
    float star=1-smoothstep(.43,.47,pow(abs(q.x),.55)+pow(abs(q.y),.55));
    float3 color;
    if(style==0) {
        float gray=saturate(.10+cloudLight*.90+z*.055+diffuse*.31+lightning*.26);
        color=lerp(gray.xxx,float3(.012,.015,.022),star);
        color+=rings*.48;
    } else {
        color=float3(.005,.007,.012)+cloudLight*.38+diffuse*.85+lightning*.9+rings*.36;
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
    // OSのフォントで文字を小さな画像（アトラス）へ描き、一枚の画像としてGPUに渡す。
    // ここは文字の解像度の準備。画面に見える大きさは設定ヘッダー側で変更する。
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
    const wchar_t* words[]={L"EQUINOX",L"陰",L"陽",L"衡",L"無",L"EQUILIBRIUM INCARNATE"};
    for(unsigned i=0;i<6;++i) {
        const bool glyph=i>=1 && i<=4;
        ComPtr<IDWriteTextFormat> font;
        checked(write_->CreateTextFormat(glyph?L"Yu Gothic":L"Times New Roman",nullptr,DWRITE_FONT_WEIGHT_BOLD,
            i==0?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,glyph?100.f:(i==0?102.f:31.f),L"ja-jp",&font),"Equinox lettering font");
        font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        const auto value=std::wstring(words[i]);
        const auto rect=glyph?D2D1::RectF((i-1)*128.f,128,i*128.f,256):
            D2D1::RectF(0,i==0?0.f:256.f,1024,i==0?128.f:384.f);
        canvas->DrawText(value.data(),static_cast<UINT32>(value.size()),font.Get(),rect,brush.Get());
    }
    checked(canvas->EndDraw(),"Render Equinox lettering");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&equinoxLettering_),"Equinox lettering source");
    equinoxOrbs_.reserve(4);
    equinoxTitles_.reserve(2);
    createCutscene();
}
void Renderer::buildEquinox(const Frame& source) {
    // 毎フレーム、足元 → 霧 → 玉とトレイル → 入替の爆発 → 名前 → 粒子を並べる。
    const auto& f=source;
    const float t=f.time,ox=f.avatarX,oz=f.avatarZ;
    const float beat=equinox::beatClock(f.equinoxBeat,t);
    const unsigned polarity=equinox::polarity(beat);
    const Color silver=settings::silver,white=settings::white;
    // pointはアバター中心の相対座標をワールド座標へ変換する小さな補助関数。
    auto point=[&](float x,float y,float z){return XMVectorSet(ox+x,y,oz+z,1);};
    auto line=[&](XMVECTOR a,XMVECTOR b,float width,float power){pixelLine(a,b,width,silver,power,f);};
    auto material=[&](FXMMATRIX model,unsigned style,float selection=0) {Instance v;XMStoreFloat4x4(&v.world,model);v.color={selection,1,1,style+.95f};equinoxOrbs_.push_back(v);};
    material(XMMatrixScaling(settings::floorDiameter,settings::floorDiameter,1)*XMMatrixRotationX(Pi*.5f)*XMMatrixTranslation(ox,settings::floorHeight,oz),2);
    const auto billboard=XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw);
    // 同じ大きさの四角い面のまま、4拍ごとに「陰→陽→衡→無」を切り替える。
    material(XMMatrixScaling(settings::floorGlyphSize,settings::floorGlyphSize,1)*XMMatrixRotationX(Pi*.5f)*XMMatrixTranslation(ox,settings::floorGlyphHeight,oz),3,
        static_cast<float>(equinox::floorGlyphIndex(beat)));
    // 足元の輪: 短い線分をつなぎ、少しずつ上下へ揺らして柔らかい輪にする。
    for(unsigned ring=0;ring<settings::floorRingCount;++ring) {
        const float radius=settings::floorInnerRadius+ring*settings::floorRingSpacing,height=settings::floorRingHeight+ring*settings::floorRingHeightStep;
        for(unsigned i=0;i<settings::floorRingSegments;++i) {
            const float a=i*2*Pi/settings::floorRingSegments+t*(ring%2?settings::floorOddRotation:settings::floorEvenRotation),b=a+2*Pi/settings::floorRingSegments;
            const float wave=settings::floorWaveHeight*std::sin(a*3+t);
            line(point(std::cos(a)*radius,height+wave,std::sin(a)*radius),
                 point(std::cos(b)*radius,height+wave,std::sin(b)*radius),.17f+ring*.025f,.31f+ring*.12f);
        }
    }
    // 霧: uが0→1へ進むほど外側から中心へ近づき、前後を薄くして自然に出入りさせる。
    for(unsigned i=0;i<settings::mistCount;++i) {
        const float u=std::fmod(i*.618034f+t*settings::mistTravelSpeed,1.f);
        const float a=i*2.399963f+t*.22f+u*1.25f,r=settings::mistOuterRadius*(1-u)+.15f;
        const float size=.60f+.45f*(1-u),fade=std::sin(u*Pi);
        pixelBillboard(ox+std::cos(a)*r,.30f+.52f*u,oz+std::sin(a)*r,size,size*.60f,silver,0,settings::mistBrightness*fade,f,a);
    }
    struct Orb {float x,y,z;unsigned style;}; Orb orbs[2];
    // 玉: 同じ音楽の拍を使い、1拍ごとに白黒を同時交換。必ず白1個・黒1個になる。
    for(unsigned side=0;side<2;++side) {
        const auto p=equinox::orbPosition(beat,side);
        orbs[side]={p.x,p.y,p.z,side^polarity};
    }
    // トレイル: 現在の玉と同じ位置計算を過去の拍で呼ぶ。波や独立した点は加えない。
    // u=0は玉の中心、u=1は尾の先。先へ行くほど幅と明るさを0に近づける。
    for(unsigned side=0;side<2;++side) {
        auto trail=[&](float u) {
            const auto p=equinox::orbPosition(beat-u*settings::trailBeats,side);
            return point(p.x,p.y,p.z);
        };
        for(unsigned j=0;j<settings::trailSegments;++j) {
            const float u=j/static_cast<float>(settings::trailSegments),v=(j+1)/static_cast<float>(settings::trailSegments);
            line(trail(u),trail(v),settings::trailWidth*std::pow(1-u,1.15f),settings::trailBrightness*std::pow(1-u,1.5f));
        }
    }
    // 白黒が交換された位置へ短い爆発を残す。1拍ごとの交換と同じ時計なので同期する。
    const float burstAge=equinox::transitionAge(beat);
    if(burstAge<settings::burstBeats) for(unsigned side=0;side<2;++side) {
        const auto center=equinox::orbPosition(beat-burstAge,side);
        const float u=burstAge/settings::burstBeats,fade=(1-u)*(1-u);
        const float radius=settings::orbRadius+.12f+u*settings::burstRadius;
        const auto face=billboard*XMMatrixTranslation(ox+center.x,center.y,oz+center.z);
        for(unsigned j=0;j<settings::burstRingSegments;++j) {
            const float a=j*2*Pi/settings::burstRingSegments,b=(j+1)*2*Pi/settings::burstRingSegments;
            line(XMVector3TransformCoord(XMVectorSet(std::cos(a)*radius,std::sin(a)*radius,0,1),face),
                 XMVector3TransformCoord(XMVectorSet(std::cos(b)*radius,std::sin(b)*radius,0,1),face),
                 .11f*(1-u)+.015f,settings::burstBrightness*fade);
        }
        for(unsigned j=0;j<settings::burstFragments;++j) {
            const float a=j*2.399963f+side*Pi,dy=1-2*(j+.5f)/settings::burstFragments;
            const float horizontal=std::sqrt(std::max(0.f,1-dy*dy));
            const auto direction=XMVectorSet(std::cos(a)*horizontal,dy,std::sin(a)*horizontal,0);
            const float reach=settings::orbRadius+u*settings::burstRadius*1.4f;
            const auto start=point(center.x,center.y,center.z)+direction*reach;
            line(start,start+direction*(.15f+.26f*u),.055f*(1-u)+.01f,settings::burstBrightness*fade);
        }
        pixelBillboard(ox+center.x,center.y,oz+center.z,settings::orbRadius*3.5f,settings::orbRadius*3.5f,white,0,
            settings::burstBrightness*std::pow(1-u,5.f),f);
    }
    const float eyeX=std::sin(f.cameraYaw),eyeZ=-std::cos(f.cameraYaw);
    if(orbs[0].x*eyeX+orbs[0].z*eyeZ>orbs[1].x*eyeX+orbs[1].z*eyeZ) std::swap(orbs[0],orbs[1]);
    for(const auto& orb:orbs) {
        constexpr float radius=settings::orbRadius;
        material(XMMatrixScaling(radius*2,radius*2,1)*XMMatrixRotationZ(t*settings::orbFaceRotation)*billboard*XMMatrixTranslation(ox+orb.x,orb.y,oz+orb.z),orb.style);
        const auto face=billboard*XMMatrixTranslation(ox+orb.x,orb.y,oz+orb.z);
        for(unsigned layer=0;layer<2;++layer) for(unsigned i=0;i<settings::orbRimSegments;++i) {
            const float a=i*2*Pi/settings::orbRimSegments,b=(i+1)*2*Pi/settings::orbRimSegments,r=radius*(layer?1.12f:1.025f);
            line(XMVector3TransformCoord(XMVectorSet(std::cos(a)*r,std::sin(a)*r,0,1),face),
                 XMVector3TransformCoord(XMVectorSet(std::cos(b)*r,std::sin(b)*r,0,1),face),.065f,orb.style?.25f:.40f);
        }
        // 外側の光は控えめにし、内部の雷雲の明暗が白飛びしないようにする。
        pixelBillboard(ox+orb.x,orb.y,oz+orb.z,radius*2.67f,radius*2.67f,silver,0,orb.style?settings::blackHaloBrightness:settings::whiteHaloBrightness,f);
        if(orb.style) {
            pixelBillboard(ox+orb.x,orb.y,oz+orb.z,2.2f,2.2f,white,4,.90f,f);
            pixelBillboard(ox+orb.x,orb.y,oz+orb.z,5.2f,.55f,white,4,.65f,f);
        } else pixelBillboard(ox+orb.x,orb.y,oz+orb.z,4.3f,.25f,white,4,.16f,f);
    }
    // 名前: 光をにじませる処理の後で重ねる。黒い文字を黒いまま読めるようにするため。
    auto title=[&](float y,float width,float height,unsigned style) {
        Instance v; XMStoreFloat4x4(&v.world,XMMatrixScaling(width,height,1)*billboard*XMMatrixTranslation(ox,y,oz));
        v.color={static_cast<float>(polarity),1,1,style+.95f}; equinoxTitles_.push_back(v);
    };
    title(settings::titleHeight,settings::titleWidth,settings::titleTextHeight,4);
    title(settings::subtitleHeight,settings::subtitleWidth,settings::subtitleTextHeight,5);
    // 粒子: 番号iから位置をばらけさせ、同じ配置が毎フレーム安定して再現されるようにする。
    for(unsigned i=0;i<settings::dustCount;++i) {
        const float a=i*2.399963f+t*settings::dustRotation,r=2.f+2.f*(.5f+.5f*std::sin(i*4.3f));
        const float y=.15f+std::fmod(i*.137f+t*settings::dustRiseSpeed,5.5f),s=.03f+.07f*std::abs(std::sin(i*3.1f));
        pixelBillboard(ox+std::cos(a)*r,y,oz+std::sin(a)*r,s,s,white,i%4?1:6,settings::dustBrightness,f,t*.35f+i);
        if(i%17==0) pixelBillboard(ox+std::cos(a)*r,y,oz+std::sin(a)*r,.4f,.4f,white,4,.45f,f);
    }
}
} // namespace aura
