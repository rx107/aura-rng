#include "render/renderer.hpp"
#include "tuning/redjohn.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

// 参考動画の読み方: 黒い導入 → 青/白/黄の帯 → 目の接写 → 横顔 → 大きなタイトル。
// 映像を貼り付ける代わりに、帯はGPU、シルエットと文字はDirect2Dで軽く描きます。
// 時刻・帯の速さ・黒帯幅は tuning/redjohn.hpp。写真の人物は描画しません。
namespace aura {
namespace {
namespace cfg=tuning::redjohn;
constexpr char RedJohnShader[]=R"(
cbuffer Cinematic:register(b0){float4 screen;float4 timing;};
struct Input{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};
float hash(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float rectangle(float2 p,float4 r){return step(r.x,p.x)*step(p.x,r.z)*step(r.y,p.y)*step(p.y,r.w);}
float4 main(Input i):SV_TARGET{
 float t=screen.x,opacity=screen.z,letterbox=screen.w;
 float2 p=i.uv;float y=(p.y-letterbox)/(1-2*letterbox);
 float grain=(hash(floor(i.p.xy)+floor(t*24)*17)-.5)*timing.w;
 float3 color=float3(.008,.009,.009)+grain*.11;
 float local=(t-timing.x)*timing.z;
 float blueMask=0,ivoryMask=0,goldMask=0;
 if(local>=0 && t<timing.y){
   // 縦の帯は少し斜め。進入・退出だけを速くし、中央の場面は読み取れる速さに。
   float settle=smoothstep(0,.48,local);
   float exit=smoothstep(timing.y-.40,timing.y,t);
   float x=p.x+.028*(y-.5)+(1-settle)*.90-exit*1.12;
   if(local<.77){
     blueMask=max(rectangle(float2(x,y),float4(.27,0,.39,1)),rectangle(float2(x,y),float4(.45,0,.51,1)));
     blueMask=max(blueMask,rectangle(float2(x,y),float4(.57,0,.70,1)));
   }else if(local<1.72){
     blueMask=rectangle(float2(x,y),float4(.12,0,.48,1));
     goldMask=rectangle(float2(x,y),float4(.49,0,.73,1));
     ivoryMask=rectangle(float2(x,y),float4(.735,0,1.08,1));
   }else if(local<3.40){
     float sweep=frac(local*.67)*.11;
     blueMask=rectangle(float2(x,y),float4(.16+sweep,0,.55+sweep,1));
     ivoryMask=rectangle(float2(x,y),float4(.57+sweep,0,.74+sweep,1));
     goldMask=rectangle(float2(x,y),float4(.76+sweep,0,.83+sweep,1));
   }else{
     blueMask=rectangle(float2(x,y),float4(-.13,0,.59,1));
     ivoryMask=rectangle(float2(x,y),float4(.60,0,.84,1));
     goldMask=rectangle(float2(x,y),float4(.855,0,.90,1));
     blueMask=max(blueMask,rectangle(float2(x,y),float4(.93,0,.975,1)));
   }
   color=lerp(color,float3(.35,.46,.74)+grain,blueMask);
   color=lerp(color,float3(.88,.89,.85)+grain*.6,ivoryMask);
   color=lerp(color,float3(.90,.69,.31)+grain,goldMask);
 }
 color*=.91+.09*sin(y*3.14159);
 if(p.y<letterbox || p.y>1-letterbox)color=0;
 return float4(saturate(color),opacity);
})";
}

void Renderer::createRedJohnCutscene(){
    ComPtr<ID3DBlob> shader,errors;
    const HRESULT hr=D3DCompile(RedJohnShader,sizeof(RedJohnShader)-1,"red-john-intro.hlsl",nullptr,nullptr,
        "main","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors);
    if(FAILED(hr)&&errors)throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    checked(hr,"Compile Red john CutScene");
    checked(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&redJohnCutscenePs_),"Red john CutScene shader");
    checked(write_->CreateTextFormat(L"Impact",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,62,L"en-us",&redJohnTitleFont_),"Red john title font");
    redJohnTitleFont_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    redJohnTitleFont_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

void Renderer::drawRedJohnCutscene(const Frame& f){
    const float opacity=tuning::cutscene::endingOpacity(f.cutsceneTime,RedJohnCutsceneDuration);
    const float constants[]={f.cutsceneTime,static_cast<float>(width_)/height_,opacity,cfg::Letterbox,
        cfg::PanelsStart,cfg::EndStart,cfg::PanelSpeed,cfg::FilmGrain};
    context_->UpdateSubresource(cutsceneConstants_.Get(),0,nullptr,constants,0,0);
    ID3D11RenderTargetView* target=target_.Get();context_->OMSetRenderTargets(1,&target,nullptr);
    context_->OMSetDepthStencilState(effectDepth_.Get(),0);context_->OMSetBlendState(effectBlend_.Get(),nullptr,0xffffffff);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
    context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
    context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(bloomVs_.Get(),nullptr,0);context_->PSSetShader(redJohnCutscenePs_.Get(),nullptr,0);
    ID3D11Buffer* cb=cutsceneConstants_.Get();context_->PSSetConstantBuffers(0,1,&cb);context_->Draw(3,0);
}

void Renderer::redJohnCutsceneUi(const Frame& f){
    const float t=f.cutsceneTime,ending=tuning::cutscene::endingOpacity(t,RedJohnCutsceneDuration);
    const float w=static_cast<float>(width_)*480/height_,factor=static_cast<float>(height_)/480;
    const float top=480*cfg::Letterbox,bottom=480-top,h=bottom-top;
    const auto ink=D2D1::ColorF(.006f,.008f,.008f,ending);
    D2D1_MATRIX_3X2_F previous;d2d_->GetTransform(&previous);
    d2d_->SetTransform(D2D1::Matrix3x2F::Scale(factor,factor));
    d2d_->PushAxisAlignedClip(D2D1::RectF(0,top,w,bottom),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    auto polygon=[&](const auto& points,D2D1_COLOR_F c){
        ComPtr<ID2D1PathGeometry> path;checked(d2dFactory_->CreatePathGeometry(&path),"Intro silhouette");
        ComPtr<ID2D1GeometrySink> sink;checked(path->Open(&sink),"Intro silhouette path");
        sink->BeginFigure(points.front(),D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(points.data()+1,static_cast<UINT32>(points.size()-1));sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        checked(sink->Close(),"Intro silhouette close");brush_->SetColor(c);d2d_->FillGeometry(path.Get(),brush_.Get());
    };
    auto silhouette=[&](float x,float scale){
        // 既存ブロックアバターを思わせる角のある頭・胴・手足。俳優の輪郭は使いません。
        const std::array<D2D1_POINT_2F,20> shape={{{-.21f,.06f},{.14f,.06f},{.22f,.15f},{.28f,.16f},{.22f,.22f},
            {.17f,.27f},{.18f,.33f},{.35f,.39f},{.42f,.70f},{.34f,.73f},{.25f,.53f},
            {.24f,.77f},{.19f,1.14f},{.02f,1.14f},{.01f,.79f},{-.06f,1.13f},{-.23f,1.13f},
            {-.24f,.78f},{-.33f,.73f},{-.35f,.39f}}};
        auto points=shape;for(auto& p:points){p.x=x+p.x*scale;p.y=top+(p.y-.02f)*scale;}
        polygon(points,ink);
        // 黒の中へ細いアイボリーの切れ目を入れ、顔と襟の方向を読み取れるようにします。
        rect(x+.17f*scale,top+.17f*scale,.045f*scale,.012f*scale,D2D1::ColorF(.88f,.89f,.85f,ending));
    };
    if(t>=cfg::EyeStart&&t<cfg::PortraitStart){
        // 黄色の細い帯の中へ、ブロックアバターの目を大きくトリミング。
        d2d_->PushAxisAlignedClip(D2D1::RectF(w*.49f,top,w*.735f,bottom),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float x=w*.60f+(t-cfg::EyeStart)*6;
        brush_->SetColor(ink);d2d_->FillEllipse(D2D1::Ellipse({x,top+h*.53f},h*.21f,h*.105f),brush_.Get());
        brush_->SetColor(D2D1::ColorF(.92f,.74f,.40f,ending));
        d2d_->FillEllipse(D2D1::Ellipse({x,top+h*.54f},h*.13f,h*.066f),brush_.Get());
        brush_->SetColor(ink);d2d_->FillEllipse(D2D1::Ellipse({x+h*.015f,top+h*.54f},h*.055f,h*.072f),brush_.Get());
        rect(x-h*.20f,top+h*.34f,h*.38f,h*.026f,ink);
        d2d_->PopAxisAlignedClip();
    }else if(t>=cfg::PortraitStart&&t<cfg::TitleStart){
        d2d_->PushAxisAlignedClip(D2D1::RectF(w*.53f,top,w*.79f,bottom),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        silhouette(w*.67f-(t-cfg::PortraitStart)*5,h*.98f);
        d2d_->PopAxisAlignedClip();
    }else if(t>=cfg::TitleStart&&t<cfg::EndStart){
        const float u=std::clamp((t-cfg::TitleStart)/(cfg::TitleSettle-cfg::TitleStart),0.f,1.f);
        const float eased=u*u*(3-2*u);
        silhouette(w*.72f,h*.94f);
        // 最初は文字をはみ出すほど大きく、0.6秒ほどで左下へ収める。
        const float scale=2.5f-1.5f*eased;
        const float x=w*.105f-(1-eased)*w*.43f,y=bottom-75-(1-eased)*h*.24f;
        const auto transform=D2D1::Matrix3x2F::Scale(scale,scale)*D2D1::Matrix3x2F::Translation(x,y)*D2D1::Matrix3x2F::Scale(factor,factor);
        d2d_->SetTransform(transform);brush_->SetColor(D2D1::ColorF(.91f,.92f,.88f,ending));
        const wchar_t title[]=L"RED JOHN";
        d2d_->DrawText(title,8,redJohnTitleFont_.Get(),D2D1::RectF(0,0,400,90),brush_.Get());
        d2d_->SetTransform(D2D1::Matrix3x2F::Scale(factor,factor));
        if(u>=1)text(L"STAR'S RNG / EXTRA AURA",w*.107f,bottom-94,w*.48f,18,11,D2D1::ColorF(.06f,.08f,.10f,ending),true);
    }
    if(t>=cfg::EndStart){
        const float appear=std::clamp((t-cfg::EndStart)/.45f,0.f,1.f);
        const float cx=w*.5f,cy=240,r=66;
        brush_->SetColor(D2D1::ColorF(.65f,.045f,.065f,appear*ending));
        d2d_->DrawEllipse(D2D1::Ellipse({cx,cy},r,r),brush_.Get(),2.6f);
        for(float sign:{-1.f,1.f})d2d_->FillEllipse(D2D1::Ellipse({cx+sign*22,cy-14},9,3.8f),brush_.Get());
        std::array<D2D1_POINT_2F,25> smile{};
        for(std::size_t i=0;i<smile.size();++i){const float x=-33+66.f*i/(smile.size()-1);smile[i]={cx+x,cy+29-.018f*x*x};}
        for(std::size_t i=1;i<smile.size();++i)d2d_->DrawLine(smile[i-1],smile[i],brush_.Get(),3.4f);
    }
    d2d_->PopAxisAlignedClip();
    const bool ja=f.presentation.settings.language==Language::Japanese;
    text(ja?L"Esc / スキップ":L"Esc / Skip",w-115,16,104,20,11,D2D1::ColorF(1,1,1,.65f*ending),false,true);
    d2d_->SetTransform(previous);
}
} // namespace aura
