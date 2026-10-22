#include "render/renderer.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace aura {
namespace {
constexpr char CutsceneShader[]=R"(
cbuffer Cinematic : register(b0) { float4 screenAndTime; float4 motion; };
struct Input { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };
float hash(float2 p) {
    float3 v=frac(float3(p.x,p.y,p.x)*.1031);
    v+=dot(v,v.yzx+33.33); return frac((v.x+v.y)*v.z);
}
float noise(float2 p) {
    float2 cell=floor(p),f=frac(p); f=f*f*(3-2*f);
    return lerp(lerp(hash(cell),hash(cell+float2(1,0)),f.x),
                lerp(hash(cell+float2(0,1)),hash(cell+1),f.x),f.y);
}
float clouds(float2 p) {
    float value=noise(p)*.55;
    p=mul(p,float2x2(1.6,-1.2,1.2,1.6))+17.8; value+=noise(p)*.27;
    p=mul(p,float2x2(1.6,-1.2,1.2,1.6))+11.3; value+=noise(p)*.13;
    return value+noise(p*2.04)*.05;
}
float star(float2 p,float size) {
    float2 a=abs(p)/size;
    float core=exp(-dot(a,a)*6);
    float needles=exp(-a.x*52-a.y*4)+exp(-a.y*52-a.x*4);
    return saturate(core+needles*1.8);
}
float field(float2 p,float density,float time) {
    float2 cell=floor(p*density),f=frac(p*density)-.5;
    float seed=hash(cell),size=lerp(.014,.032,hash(cell+7.3));
    f-=(float2(hash(cell+19),hash(cell+47))-.5)*.55;
    float dotStar=exp(-dot(f,f)/(size*size));
    float sparkle=seed>.996?star(f,.27):0;
    float population=step(density>50?.92:.40,seed);
    return (dotStar*.64+sparkle)*population*(.45+.55*abs(sin(time*.7+seed*23)));
}
float4 main(Input i):SV_TARGET {
    float time=screenAndTime.x,aspect=screenAndTime.y;
    // 音声の低音に合わせた、画面高に対する上下移動。字幕にも同じ移動をかける。
    float2 screenUv=i.uv-float2(0,motion.x);
    float2 q=(screenUv-.5)*float2(aspect,1);
    float angle=.65+time*2.1;
    float2 normal=float2(cos(angle),sin(angle));
    float side=dot(q,normal);
    float light=step(0,side);
    float polarity=1-light*2;

    // The same slowly moving nebula is seen as positive and negative fields.
    float2 drift=float2(time*.019,-time*.011);
    float2 warp=float2(noise(q*3.7+drift),noise(q*3.7+37-drift));
    float nebula=saturate((clouds(q*4.8+drift+warp*1.45)-.26)*2.8);
    float filaments=clouds(q*17.7-drift*.8+warp*.7);
    float dark=.035+pow(nebula,1.5)*.49+(filaments-.5)*.09;
    float color=lerp(dark,1-dark*1.10,light);
    float grain=hash(floor(i.p.xy)+floor(time*30)*13.1)-.5;
    color+=grain*.008;

    float stars=field(q+float2(time*.001,0),34,time)*.73;
    stars+=field(q+19.7,67,time)*.16;
    color=lerp(color,1-light,saturate(stars));

    // Paired white/black needle stars touch on the rotating inversion boundary.
    float central=star(q,.17);
    float halo=exp(-dot(q,q)*105)*.22;
    color+=polarity*halo;
    color=lerp(color,1-light,central);

    // Large overlapping rings drift and expand independently of the cloudy sky.
    [unroll] for(int n=0;n<6;++n) {
        float seed=hash(float2(n*47.7+2,172.4));
        float phase=frac(time*.35+seed);
        float scale=.42+phase*1.28;
        float2 center=float2((hash(float2(n*13.79+7,53.9))-.5)*aspect*1.24,
                            (hash(float2(n*39.43+11,137.3))-.5)*1.12);
        center+=float2(sin(time*.47+n),cos(time*.37+n*2))*.055;
        float radius=(.045+hash(float2(n*14.1+3,294.1))*.20)*scale;
        float distanceToRing=abs(length(q-center)-radius);
        float width=n<3?.0015:(n<5?.0032:.012);
        float aa=max(.0007,1/screenAndTime.w);
        float ring=1-smoothstep(width,width+aa,distanceToRing);
        float visibility=smoothstep(.04,.13,phase)*(1-smoothstep(.83,.97,phase));
        color=lerp(color,1-light,ring*visibility);
        if(n<2) {
            float inner=1-smoothstep(.001,.001+aa,abs(length(q-center)-radius*.73));
            color=lerp(color,1-light,inner*visibility*.75);
        }
    }

    // Tiny cardinal stars belong to the two inverse star fields as well.
    [unroll] for(int s=0;s<8;++s) {
        float2 pos=float2((hash(float2(s,53))-.5)*aspect,
                         (hash(float2(s,71))-.5)*.92);
        float size=.015+hash(float2(s,89))*.024;
        float twinkle=.75+.25*sin(time*1.3+s*2);
        color=lerp(color,1-light,star(q-pos,size)*twinkle);
    }

    // Foreground piano-key blocks gather at the beginning and end of each verse.
    float verse=frac(max(0,time-.25)/2.7)*2.7;
    float keys=saturate(1-smoothstep(.25,.78,verse)+smoothstep(2.23,2.66,verse));
    float keyX=(screenUv.x+sin(time*.73)*.012)*32;
    float column=floor(keyX);
    float top=.018+hash(float2(column,4))*hash(float2(column,14))*.24;
    float bottom=.014+hash(float2(column,8))*hash(float2(column,19))*.25;
    float bar=max(1-smoothstep(top*keys,top*keys+.0015,screenUv.y),
                  smoothstep(1-bottom*keys-.0015,1-bottom*keys,screenUv.y));
    float keyGap=1-smoothstep(.76,.79,frac(keyX));
    color=lerp(color,1-light,bar*keyGap);

    // Tall monochrome pillars cross in front of the circular outlines.
    [unroll] for(int b=0;b<3;++b) {
        float phase=frac(time*(.32+b*.037)+b*.317);
        float x=-.25+phase*1.5;
        float width=b==0?.018:(b==1?.045:.105);
        float band=1-smoothstep(width,width+.0015,abs(screenUv.x-x));
        float visibility=(b==2?.65:1)*(1-smoothstep(2.35,2.7,verse));
        color=lerp(color,1-light,band*visibility);
    }

    // Reference verse changes briefly flatten into silver haze rather than color.
    float flash=exp(-pow((time-2.84)/.085,2));
    flash+=exp(-pow((time-5.52)/.085,2))+exp(-pow((time-8.20)/.085,2));
    color=lerp(color,.56,saturate(flash*.83));
    // 終了時は黒へ落とさず、下に描いた実際のオーラへ透明度でつなげる。
    float fade=smoothstep(.15,.34,time);
    float gray=saturate(color)*fade;
    return float4(gray,gray,gray,screenAndTime.z);
})";

struct Verse {
    float start,end;
    wchar_t character;
    std::wstring_view caption;
};
constexpr std::array<Verse,4> Verses={{{.27f,2.65f,L'陰',L"陰 : The Negative, that you've granted"},
    {2.95f,5.35f,L'陽',L"陽 : The Positive, that you've taken"},
    {5.63f,8.05f,L'衡',L"衡 : The Equinox, Everything from everywhere, even from nothing"},
    {8.30f,10.65f,L'無',L"無 : And you, now having it all, bring yourself close to The ZERO."}}};

std::vector<D2D1_POINT_2F> halfPlane(float width,float nx,float ny,bool positive) {
    const std::array<D2D1_POINT_2F,4> rectangle={{{0,0},{width,0},{width,480},{0,480}}};
    std::vector<D2D1_POINT_2F> result;
    auto distance=[&](D2D1_POINT_2F p) { return ((p.x-width*.5f)*nx+(p.y-240)*ny)*(positive?1.f:-1.f); };
    for(std::size_t n=0;n<rectangle.size();++n) {
        const auto a=rectangle[n],b=rectangle[(n+1)%rectangle.size()];
        const float da=distance(a),db=distance(b);
        if(da>=0) result.push_back(a);
        if((da>=0)!=(db>=0)) {
            const float u=da/(da-db);
            result.push_back({a.x+(b.x-a.x)*u,a.y+(b.y-a.y)*u});
        }
    }
    return result;
}
}

void Renderer::createCutscene() {
    ComPtr<ID3DBlob> shader,errors;
    const HRESULT hr=D3DCompile(CutsceneShader,sizeof(CutsceneShader)-1,"equinox-cutscene.hlsl",nullptr,nullptr,
        "main","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors);
    if(FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    checked(hr,"Compile Equinox CutScene shader");
    checked(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&cutscenePs_),"Equinox CutScene shader");
    D3D11_BUFFER_DESC description{}; description.ByteWidth=32; description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    description.Usage=D3D11_USAGE_DEFAULT;
    checked(device_->CreateBuffer(&description,nullptr,&cutsceneConstants_),"Equinox CutScene constants");
    checked(write_->CreateTextFormat(L"Times New Roman",nullptr,DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,14,L"en-us",&cutsceneFont_),"Equinox CutScene subtitle font");
    checked(cutsceneFont_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER),"Equinox subtitle alignment");
    checked(cutsceneFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),"Equinox subtitle paragraph");
    checked(cutsceneFont_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),"Equinox subtitle wrapping");
    checked(write_->CreateTextFormat(L"Yu Mincho",nullptr,DWRITE_FONT_WEIGHT_REGULAR,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,132,L"ja-jp",&cutsceneKanjiFont_),"Equinox CutScene glyph font");
    checked(cutsceneKanjiFont_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER),"Equinox glyph alignment");
    checked(cutsceneKanjiFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),"Equinox glyph paragraph");
}

void Renderer::drawCutscene(const Frame& frame) {
    if(frame.cutsceneTime<0 || frame.cutsceneTime>EquinoxCutsceneDuration) return;
    const float opacity=tuning::cutscene::endingOpacity(frame.cutsceneTime,EquinoxCutsceneDuration);
    const float shake=-tuning::cutscene::EquinoxBassShakeHeight*std::clamp(frame.cutsceneBeat,0.f,1.f);
    const float constants[]={frame.cutsceneTime,static_cast<float>(width_)/height_,opacity,static_cast<float>(height_),shake,0,0,0};
    context_->UpdateSubresource(cutsceneConstants_.Get(),0,nullptr,constants,0,0);
    ID3D11RenderTargetView* target=target_.Get(); context_->OMSetRenderTargets(1,&target,nullptr);
    context_->OMSetDepthStencilState(effectDepth_.Get(),0); context_->OMSetBlendState(effectBlend_.Get(),nullptr,0xffffffff);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
    context_->RSSetViewports(1,&viewport); context_->RSSetState(raster_.Get());
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(bloomVs_.Get(),nullptr,0); context_->PSSetShader(cutscenePs_.Get(),nullptr,0);
    ID3D11Buffer* cb=cutsceneConstants_.Get(); context_->PSSetConstantBuffers(0,1,&cb); context_->Draw(3,0);
}

void Renderer::cutsceneUi(const Frame& frame) {
    const float time=frame.cutsceneTime;
    if(time<0 || time>EquinoxCutsceneDuration) return;
    const float referenceWidth=static_cast<float>(width_)*480/height_;
    const float factor=static_cast<float>(height_)/480;
    D2D1_MATRIX_3X2_F previous; d2d_->GetTransform(&previous);
    const float ending=tuning::cutscene::endingOpacity(time,EquinoxCutsceneDuration);
    const float shake=-480*tuning::cutscene::EquinoxBassShakeHeight*std::clamp(frame.cutsceneBeat,0.f,1.f);
    d2d_->SetTransform(D2D1::Matrix3x2F::Translation(0,shake)*D2D1::Matrix3x2F::Scale(factor,factor));
    const Verse* verse=nullptr;
    for(const auto& v:Verses) if(time>=v.start && time<=v.end) { verse=&v; break; }
    if(verse) {
        const auto length=std::min(verse->caption.size(),static_cast<std::size_t>((time-verse->start)*90)+1);
        const float opacity=std::clamp((verse->end-time)/.14f,0.f,1.f)*ending;
        const float ghost=std::clamp((time-verse->start-1.85f)*3,0.f,1.f)*opacity*.11f;
        const float angle=.65f+time*2.1f;
        const float nx=std::cos(angle),ny=std::sin(angle);
        for(bool positive:{false,true}) {
            const auto points=halfPlane(referenceWidth,nx,ny,positive);
            if(points.size()<3) continue;
            ComPtr<ID2D1PathGeometry> geometry; checked(d2dFactory_->CreatePathGeometry(&geometry),"CutScene subtitle mask");
            ComPtr<ID2D1GeometrySink> sink; checked(geometry->Open(&sink),"CutScene subtitle mask sink");
            sink->BeginFigure(points.front(),D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(points.data()+1,static_cast<UINT32>(points.size()-1)); sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            checked(sink->Close(),"CutScene subtitle mask path");
            d2d_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),geometry.Get()),nullptr);
            const float shade=positive?0.f:1.f;
            if(ghost>0) {
                brush_->SetColor(D2D1::ColorF(shade,shade,shade,ghost));
                d2d_->DrawText(&verse->character,1,cutsceneKanjiFont_.Get(),D2D1::RectF(0,128,referenceWidth,344),brush_.Get());
            }
            // A single physical pixel keeps the reference's inverse lettering legible
            // when a foreground white/black pillar crosses the underlying half-plane.
            const float outline=1/factor;
            const std::array<D2D1_POINT_2F,4> offsets={{{-outline,0},{outline,0},{0,-outline},{0,outline}}};
            brush_->SetColor(D2D1::ColorF(1-shade,1-shade,1-shade,opacity*.67f));
            for(const auto offset:offsets) d2d_->DrawText(verse->caption.data(),static_cast<UINT32>(length),cutsceneFont_.Get(),
                D2D1::RectF(16+offset.x,420+offset.y,referenceWidth-16+offset.x,452+offset.y),brush_.Get());
            brush_->SetColor(D2D1::ColorF(shade,shade,shade,opacity));
            d2d_->DrawText(verse->caption.data(),static_cast<UINT32>(length),cutsceneFont_.Get(),
                D2D1::RectF(16,420,referenceWidth-16,452),brush_.Get());
            d2d_->PopLayer();
        }
    }
    // スキップ表示は操作用なので揺らさず、最後だけ演出と一緒に消します。
    d2d_->SetTransform(D2D1::Matrix3x2F::Scale(factor,factor));
    const bool japanese=frame.presentation.settings.language==Language::Japanese;
    rect(referenceWidth-112,8,104,23,D2D1::ColorF(0,0,0,.40f*ending),3);
    text(japanese?L"Esc / スキップ":L"Esc / Skip",referenceWidth-108,12,96,17,10,D2D1::ColorF(1,1,1,.82f*ending),false,true);
    d2d_->SetTransform(previous);
}
} // namespace aura
