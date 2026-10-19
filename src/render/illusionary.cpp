#include "render/renderer.hpp"
#include "tuning/illusionary.hpp"
#include <algorithm>
#include <cmath>

namespace aura {
using namespace DirectX;
namespace {
namespace settings=tuning::illusionary;
constexpr float Pi=3.14159265359f;
float random(unsigned i,unsigned seed) {
    const float v=std::sin(i*127.1f+seed*311.7f)*43758.5453f;
    return v-std::floor(v);
}
Color rgb(const std::array<float,3>& c) { return {c[0],c[1],c[2]}; }
Color mix(Color a,Color b,float u) { return {a.r+(b.r-a.r)*u,a.g+(b.g-a.g)*u,a.b+(b.b-a.b)*u}; }
}

void Renderer::buildIllusionary(const Frame& f) {
    // 読む順番: ①時計と座標 → ②黒い帯 → ③白い放射線 → ④チェック → ⑤小さな画面。
    // 画像や動画は読み込まず、小さな板と線をまとめて GPU に送り、立体的に再構成します。
    // 黒は発光で表現できないため illusionaryInk_、光は pixelationInstances_ に分けています。
    const float t=f.time*settings::MotionSpeed;
    const float hover=std::sin(t*settings::HoverSpeed)*settings::HoverAmplitude;
    const float flashDistance=std::abs(std::fmod(t,settings::FlashPeriod)-settings::FlashAt);
    const float flash=std::max(0.f,1-flashDistance/settings::FlashDuration)*settings::FlashStrength;
    const Color blue=mix(rgb(settings::Blue),rgb(settings::White),flash);
    const Color white=rgb(settings::White),ink=rgb(settings::Ink);
    const float ox=f.avatarX,oz=f.avatarZ;
    auto point=[&](float x,float y,float z) {return XMVectorSet(ox+x,y,oz+z,1);};
    const auto cameraBasis=XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw);
    auto screenPoint=[&](float x,float y,float depth=0.f) {
        return XMVector3TransformCoord(XMVectorSet(x,y,depth,1),cameraBasis*XMMatrixTranslation(ox,settings::BurstHeight+hover,oz));
    };
    auto billboard=[&](XMVECTOR p,float w,float h,float rotation=0.f) {
        return XMMatrixScaling(w,h,1)*XMMatrixRotationZ(rotation)*cameraBasis*
            XMMatrixTranslation(XMVectorGetX(p),XMVectorGetY(p),XMVectorGetZ(p));
    };
    auto dark=[&](FXMMATRIX model,Color color=Color{.001f,.001f,.005f}) {
        // 容量の上限は調整値を増やしすぎた場合の安全柵。通常は約 400 枚です。
        if(illusionaryInk_.size()>=700)return;
        Instance v;XMStoreFloat4x4(&v.world,model);v.color={color.r,color.g,color.b,0};illusionaryInk_.push_back(v);
    };
    auto beam=[&](XMVECTOR a,XMVECTOR b,float width,Color color,float opacity) {pixelLine(a,b,width,color,opacity,f);};
    auto glow=[&](XMVECTOR p,float w,float h,Color color,unsigned style,float opacity,float turn=0.f) {
        pixelQuad(billboard(p,w,h,turn),color,style,opacity);
    };

    // ① 黒いリボン。1 周を短い台形に分け、その左右の端をぴったりつなぎます。
    // X=帯に沿う方向、Y=帯の幅方向として板を置くので、自由な傾きでも穴が開きません。
    for(unsigned ring=0;ring<settings::Ribbons.size();++ring) {
        const auto& r=settings::Ribbons[ring];
        const auto model=XMMatrixRotationX(r.tiltX)*XMMatrixRotationZ(r.tiltZ)*
            XMMatrixRotationY(t*r.spin+r.phase)*XMMatrixTranslation(ox,r.height+hover,oz);
        auto ringPoint=[&](float a,float radius,float lift=0.f) {
            return XMVector3TransformCoord(XMVectorSet(std::cos(a)*radius,lift,std::sin(a)*radius,1),model);
        };
        for(unsigned i=0;i<settings::RibbonSegments;++i) {
            const float a=i*2*Pi/settings::RibbonSegments,b=(i+1)*2*Pi/settings::RibbonSegments,m=(a+b)*.5f;
            const auto from=ringPoint(a,r.radius),to=ringPoint(b,r.radius);
            const auto along=XMVectorScale(XMVectorSubtract(to,from),1.018f);
            const auto across=XMVectorSubtract(ringPoint(m,r.radius+r.width*.5f),ringPoint(m,r.radius-r.width*.5f));
            const auto normal=XMVector3Normalize(XMVector3Cross(along,across));
            const auto middle=XMVectorScale(XMVectorAdd(from,to),.5f);
            dark(XMMATRIX(XMVectorSetW(along,0),XMVectorSetW(across,0),XMVectorSetW(normal,0),XMVectorSetW(middle,1)),ink);
            // 青い縁は切れ目を入れて回転。帯がただの輪ではなく、壊れたデータのように見えます。
            const unsigned step=(i+static_cast<unsigned>(t*5+ring*11))%17;
            if(step<12)beam(ringPoint(a,r.radius-r.width*.32f,.024f),ringPoint(b,r.radius-r.width*.32f,.024f),
                settings::RibbonEdgeWidth,blue,settings::RibbonEdgeBrightness*(ring? .55f:1.f));
            if(i%11==0)beam(ringPoint(a,r.radius+r.width*.42f,.018f),ringPoint(b,r.radius+r.width*.42f,.018f),.018f,white,.45f);
        }
    }

    // ② 中央の白い破片と、外へ伸びる細い線。長さ・角度に規則的な乱数を使います。
    // 番号が同じなら同じ乱数になるので、毎フレームちらついて飛び散ることはありません。
    for(unsigned i=0;i<settings::RayCount;++i) {
        const float angle=i*2.399963f+t*.045f;
        const float inner=settings::RayInnerRadius+random(i,3)*.65f;
        const float length=(.45f+random(i,2)*.55f)*settings::RayOuterRadius;
        const float depth=(random(i,9)-.5f)*1.5f;
        const float speed=.83f+.17f*std::sin(t*1.6f+i);
        const auto a=screenPoint(std::cos(angle)*inner,std::sin(angle)*inner,depth);
        const auto b=screenPoint(std::cos(angle)*length*speed,std::sin(angle)*length*speed,depth);
        // 上側の線を明るく、下側は細く抑え、アバター本体を見やすくします。
        const float fade=std::sin(angle)>-.15f?.55f:.20f;
        beam(a,b,settings::RayWidth*(i%5?1.f:2.2f),i%4?white:blue,fade);
        if(i%5==0)beam(screenPoint(std::cos(angle)*length*.72f,std::sin(angle)*length*.72f,depth),b,.095f,blue,.65f);
    }
    for(unsigned i=0;i<settings::ShardCount;++i) {
        const float angle=(random(i,4)-.5f)*2.36f;
        const float length=settings::ShardLength*(.24f+random(i,6)*.76f);
        const float x=(random(i,5)-.5f)*settings::ShardSpread;
        const float lean=std::sin(angle),up=std::cos(angle);
        const auto a=screenPoint(x,settings::ShardHeight-settings::BurstHeight-.12f,(random(i,7)-.5f)*.7f);
        const auto b=XMVectorAdd(a,XMVector3TransformNormal(XMVectorSet(lean*length,up*length,0,0),cameraBasis));
        beam(a,b,i%4?.046f:.14f,i%3?white:blue,i%3?.73f:.65f);
    }
    glow(point(0,4.03f+hover,0),3.6f,3.6f,blue,0,.35f);
    glow(point(0,3.80f+hover,0),1.1f,.42f,white,4,.76f);
    glow(point(0,.34f,0),6.8f,2.4f,blue,0,.44f);

    // ③ 上の青黒チェック。板はカメラを向き、前後に細い白線を置きます。
    // 足元は一周するチェック壁なので、カメラを回しても模様が途切れません。
    const auto crown=screenPoint(0,settings::CrownHeight-settings::BurstHeight,-.12f);
    dark(billboard(crown,settings::CrownWidth+.16f,.40f),ink);
    const float cell=settings::CrownWidth/settings::CrownCells;
    for(unsigned i=0;i<settings::CrownCells;++i) {
        if(i%2)continue;
        const auto p=XMVectorAdd(crown,XMVector3TransformNormal(XMVectorSet((i+.5f)*cell-settings::CrownWidth*.5f,0,-.016f,0),cameraBasis));
        glow(p,cell*1.45f,.27f,blue,1,.93f);
    }
    for(int side:{-1,1})beam(screenPoint(side*(settings::CrownWidth*.5f+.18f),settings::CrownHeight-settings::BurstHeight,-.14f),
        screenPoint(side*(settings::CrownWidth*.5f+.33f),settings::CrownHeight-settings::BurstHeight+.22f,-.14f),.033f,white,.60f);
    for(unsigned row=0;row<settings::FootRows;++row)for(unsigned i=0;i<settings::FootColumns;++i) {
        const float angle=i*2*Pi/settings::FootColumns;
        const float radius=settings::FootRadius+.05f*std::sin(t*.7f+i);
        const float y=.16f+(row+.5f)*settings::FootCellHeight;
        const float width=2*radius*std::tan(Pi/settings::FootColumns);
        const auto tile=XMMatrixScaling(width*1.02f,settings::FootCellHeight,1)*XMMatrixRotationY(-angle)*
            XMMatrixTranslation(ox+std::sin(angle)*radius,y,oz-std::cos(angle)*radius);
        dark(tile,{.002f,.002f,.03f});
        const auto emission=XMMatrixScaling(width*1.48f,settings::FootCellHeight*1.48f,1)*XMMatrixRotationY(-angle)*
            XMMatrixTranslation(ox+std::sin(angle)*(radius+.012f),y,oz-std::cos(angle)*(radius+.012f));
        pixelQuad(emission,(i+row)%2?blue:white,1,(i+row)%2?.61f:.49f);
    }

    // ④ 青と白の星。8 本の短い線で描くため、専用画像を追加する必要はありません。
    for(unsigned i=0;i<settings::SparkCount;++i) {
        const float angle=i*2.399963f+t*.15f;
        const float radius=2.20f+random(i,4)*1.10f;
        const auto p=screenPoint(std::cos(angle)*radius,std::sin(angle)*radius*.75f,(random(i,6)-.5f)*1.2f);
        const float size=settings::SparkSize*(.65f+.7f*random(i,1));
        const Color c=i%3?blue:white;
        for(unsigned ray=0;ray<4;++ray) {
            const float a=ray*Pi*.25f+t*.035f;
            const auto d=XMVector3TransformNormal(XMVectorSet(std::cos(a)*size,std::sin(a)*size,0,0),cameraBasis);
            beam(XMVectorSubtract(p,d),XMVectorAdd(p,d),ray%2?.032f:.055f,c,.72f);
        }
        glow(p,size*2.6f,size*2.6f,c,4,.64f);
    }
    for(unsigned i=0;i<settings::ParticleCount;++i) {
        const float angle=i*2.399963f+t*.10f;
        const float r=1.4f+random(i,2)*2.3f;
        const float y=.65f+std::fmod(random(i,3)*5.8f+t*.20f,5.8f);
        const float size=.025f+random(i,5)*.045f;
        glow(point(std::cos(angle)*r,y,std::sin(angle)*r),size,size,i%3?blue:white,1,.64f,t*.2f+i);
    }

    // ⑤ 薄い窓枠・折れ線・短い文字。ランダムな文字列ではなく、読める単語を控えめに配置。
    // 文字は 5×7 の小さな四角から構成され、他のオーラと同じ軽量な仕組みを使います。
    constexpr const char* captions[]={"REVERIE","DECAY","NO REALITY","PARADOX","DREAM STATE","REVERIE","NO REALITY"};
    for(unsigned i=0;i<settings::WindowCount;++i) {
        const int side=i%2?1:-1;
        const float x=side*(settings::WindowRadius*.67f+random(i,8)*1.08f);
        const float y=-2.55f+static_cast<float>(i)*.73f+.075f*std::sin(t*.75f+i);
        const auto p=screenPoint(x,y,-.20f);
        const float width=.62f+random(i,1)*.52f,height=.14f+(i%3)*.085f;
        glow(p,width,height,white,3,.30f);
        // 真っ直ぐ結ばず、一度折れて止まる配線。画像の細いデバッグ表示の雰囲気です。
        const auto p0=screenPoint(x-side*width*.5f,y,-.20f);
        const auto p1=screenPoint(x-side*(width*.5f+.30f),y+.12f,-.20f);
        const auto p2=screenPoint(x-side*(width*.5f+.93f),y+.12f,-.20f);
        beam(p0,p1,.013f,i%2?white:blue,.35f);beam(p1,p2,.013f,i%2?white:blue,.35f);
        if(i<4) {
            const auto textPoint=screenPoint(x,y+.005f,-.235f);
            dark(billboard(textPoint,width*.93f,.15f),i%2?Color{.70f,.71f,.76f}:ink);
            const auto letters=screenPoint(x,y+.005f,-.260f);
            pixelText(captions[i],XMVectorGetX(letters),XMVectorGetY(letters),XMVectorGetZ(letters),
                settings::CaptionPixelSize,i%2?Color{.025f,.03f,.18f}:white,f);
        }
    }
    // 見出しだけは十分な大きさにし、背景に細い黒板を敷いて読めるようにします。
    const auto title=screenPoint(settings::TitleSideOffset,settings::TitleHeight-settings::BurstHeight,-.32f);
    dark(billboard(title,2.65f,.39f),ink);
    const auto letters=XMVectorAdd(title,XMVector3TransformNormal(XMVectorSet(0,0,-.025f,0),cameraBasis));
    pixelText("ILLUSIONARY",XMVectorGetX(letters),XMVectorGetY(letters),XMVectorGetZ(letters),settings::TitlePixelSize,white,f);
    beam(screenPoint(settings::TitleSideOffset+1.42f,settings::TitleHeight-settings::BurstHeight,-.34f),
        screenPoint(settings::TitleSideOffset+2.13f,settings::TitleHeight-settings::BurstHeight,-.34f),.013f,white,.47f);
}
} // namespace aura
