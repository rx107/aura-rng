#include "render/renderer.hpp"
#include "tuning/frutiger.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// 読む順番: buildFrutiger()の「草原→空→泡→蝶→きらめき」。
// 大きさ・色・量・速さの調整はtuning/frutiger.hppへ。
// 下のHLSLは透明な膜、反射、羽の模様を各ピクセルで作る専門部分です。
// 通常のアルファ合成で描き、強いbloomに頼らず、白い映り込みの形を残します。
namespace aura {
using namespace DirectX;
namespace {
constexpr float Pi=3.14159265359f;
namespace settings=tuning::frutiger;
constexpr char FrutigerShader[]=R"(
cbuffer Camera:register(b0) { row_major float4x4 viewProjection; float4 eyeAndTime; };
struct Input { float4 p:SV_POSITION; float3 n:NORMAL; float4 color:COLOR0; float3 world:TEXCOORD0; float3 local:TEXCOORD1; };
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float noise(float2 p) {
    float2 v=floor(p),u=frac(p);u=u*u*(3-2*u);
    return lerp(lerp(hash(v),hash(v+float2(1,0)),u.x),lerp(hash(v+float2(0,1)),hash(v+1),u.x),u.y);
}
float cloud(float2 p) { return noise(p)*.56+noise(p*2.03+7.7)*.28+noise(p*4.11-3.1)*.12+noise(p*8.27)*.04; }
float ellipse(float2 q,float2 center,float2 axes) { return length((q-center)/axes); }
float3 sky(float2 q,float time) {
    float3 c=lerp(float3(.32,.77,1),float3(.018,.23,.86),saturate(q.y*.58+.44));
    // 大きな入道雲の柔らかい塊。小さいノイズを重ね、平面の白い斑点に見せません。
    float n=cloud(float2(q.x*2.7+time*.025,q.y*4.0));
    float banks=smoothstep(.43,.69,n+.12*sin(q.y*9+q.x*1.9));
    float cloudShade=.81+.19*saturate(q.y+.35)+.12*noise(q*11+time*.008);
    c=lerp(c,cloudShade.xxx,banks*.96);
    float sun=length((q-float2(.58,.56))*float2(1,1.24));
    c+=float3(.39,.45,.34)*exp(-sun*sun*13);
    c=lerp(c,float3(1,.999,.93),exp(-sun*sun*540));
    return c;
}
float4 main(Input i):SV_TARGET {
    float2 q=i.local.xy*2;float r=length(q),time=eyeAndTime.w;
    int style=(int)floor(i.color.a+.00001);float opacity=frac(i.color.a)*1.011;
    float3 c=0;float a=1;
    if(style==0) {
        // 芝生の円盤。明暗の刈り筋、細い葉、小川の空の映り込みを一枚に描きます。
        clip(1-r);float blades=noise(q*180),tufts=noise(q*34);
        float stripes=.88+.12*sin(q.x*16+q.y*7);
        float light=.79+.25*tufts+.15*blades;
        c=i.color.rgb*light*stripes;
        c+=float3(.10,.13,.015)*pow(blades,5);
        float path=q.x-.24*sin(q.y*3.2+.8)-.37;
        float water=(1-smoothstep(.035,.075,abs(path)))*smoothstep(-.85,-.12,q.y);
        float ripple=.5+.5*sin(q.y*140+time*2+noise(q*32)*5);
        float3 reflected=lerp(float3(.035,.38,.69),float3(.71,.91,1),pow(ripple,8)*.6);
        c=lerp(c,reflected,water*.9);
        // 円周を少しだけ明るくし、丸い小世界としての厚みを感じさせます。
        c+=float3(.15,.24,.07)*exp(-abs(r-.975)*100);
        a=1-smoothstep(.982,1,r);
    } else if(style==1) {
        // 参考の青空を、アバター背後の大きなガラスの景色として再構成します。
        clip(1-r);c=sky(q,time*i.color.r);
        float hills=-.69+.075*sin(q.x*3.5)+.045*sin(q.x*8+1);
        float meadow=1-smoothstep(hills-.018,hills+.018,q.y);
        float3 green=lerp(float3(.08,.35,.025),float3(.30,.68,.075),saturate(q.y*1.8+1.4));
        green*=.88+.12*sin(q.x*24+q.y*8);
        c=lerp(c,green,meadow);
        float rim=exp(-abs(r-.975)*105);
        c+=float3(.48,.70,.86)*rim*.8;
        // 上部に広い白い映り込み、縁に細い二重の輪を作り、光沢の膜を見せます。
        float sheen=exp(-pow((q.y-.65+.22*q.x)/.13,2))*exp(-pow((q.x+.35)/.62,2));
        c=lerp(c,float3(1,1,1),sheen*.27);
        a=(1-smoothstep(.985,1,r))*(.86+rim*.14);
    } else if(style==2) {
        clip(1-r);float z=sqrt(saturate(1-r*r));float3 n=float3(q,z);
        float fresnel=pow(1-z,2.8);
        float3 reflected=sky(float2(q.x*.8,q.y*.75+.2),time*.55);
        reflected=lerp(float3(.16,.58,.16),reflected,smoothstep(-.35,.05,q.y));
        // 虹色の干渉膜は縁だけ。透明な中心からプレイヤーと景色が見えます。
        float phase=atan2(q.y,q.x)*2.6+r*19+time*.19;
        float3 rainbow=.5+.5*cos(phase+float3(0,2.094,4.188));
        float thinRim=exp(-abs(r-.965)*135),innerRim=exp(-abs(r-.86)*70);
        c=lerp(reflected,rainbow,fresnel*.62);
        c+=thinRim*(float3(.75,.87,1)+rainbow*.24)+innerRim*.18;
        float spec=pow(saturate(dot(n,normalize(float3(-.40,.57,.72)))),90);
        float specSmall=pow(saturate(dot(n,normalize(float3(.54,-.50,.67)))),140);
        // 窓のような長いハイライトが、平たい光の輪と透明な球を区別します。
        float window=exp(-pow((q.x+.38)/.09,4)-pow((q.y-.46)/.29,4));
        c=lerp(c,float3(1,1,1),saturate(spec*.88+specSmall*.75+window*.8));
        a=(.035+fresnel*.54+thinRim*.62+innerRim*.09+spec*.88+specSmall*.65+window*.48)*(1-smoothstep(.991,1,r));
    } else if(style==3) {
        // 一枚の羽に大きい上翅と小さい下翅。黒い縁取りと青のグラデーション。
        float upper=ellipse(q,float2(-.03,.25),float2(.93,.65));
        float lower=ellipse(q,float2(.12,-.48),float2(.69,.40));
        float shape=min(upper,lower);clip(1-shape);
        float border=smoothstep(.76,.98,shape);
        float veins=pow(.5+.5*cos(atan2(q.y+.3,q.x+.65)*19),22)*smoothstep(.05,.80,length(q-float2(-.65,-.3)));
        float shine=pow(saturate(1-length(q-float2(-.2,.3))*.74),3);
        c=i.color.rgb*(.60+.76*shine);
        c=lerp(c,float3(.006,.025,.055),max(border*.95,veins*.68));
        c+=float3(.15,.64,.92)*exp(-pow((shape-.72)/.065,2))*.24;
        a=1-smoothstep(.972,1,shape);
    } else if(style==4) {
        // 蝶の細い体。羽の中心を暗色にすることで、小さくても蝶の形を読み取れます。
        float body=ellipse(q,0,float2(.28,.94));clip(1-body);
        c=float3(.008,.022,.047)+float3(.035,.11,.18)*pow(saturate(1-body),2);
        a=1-smoothstep(.8,1,body);
    } else if(style==5) {
        // 先端が尖る草の葉。細かい葉脈と片側のハイライトで草の光沢を表現します。
        float width=(1-q.y)*.34;
        float edge=abs(q.x+q.y*q.y*.12)-width;clip(-edge);
        c=i.color.rgb*(.74+.28*q.y+.26*saturate(-q.x*2));
        c+=float3(.12,.20,.015)*exp(-abs(q.x)*32);
        a=1-smoothstep(-.07,0,edge);
    } else {
        // 鋭い四方の小さな輝き。周囲まで白く塗らず、光沢のアクセントにします。
        float cross=exp(-abs(q.x)*55)*exp(-abs(q.y)*5)+exp(-abs(q.y)*55)*exp(-abs(q.x)*5);
        float core=exp(-dot(q,q)*80);a=saturate(cross+core)*(1-smoothstep(.8,1,max(abs(q.x),abs(q.y))));
        c=lerp(i.color.rgb,float3(1,1,1),core);
    }
    return float4(saturate(c),saturate(a*opacity));
})";

// 同じ番号には同じ値を返すので、泡や草が毎フレーム急に別の場所へ飛びません。
float random(unsigned index,unsigned salt) {
    const float value=std::sin(index*127.1f+salt*311.7f)*43758.5453f;
    return value-std::floor(value);
}
}

void Renderer::createFrutiger() {
    ComPtr<ID3DBlob> shader,errors;
    const HRESULT hr=D3DCompile(FrutigerShader,sizeof(FrutigerShader)-1,"frutiger-aero.hlsl",nullptr,nullptr,"main","ps_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors);
    if(FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    checked(hr,"Compile Frutiger Aero material");
    checked(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&frutigerPs_),"Frutiger Aero material");
    frutigerInstances_.reserve(512);
}

void Renderer::buildFrutiger(const Frame& f) {
    const float t=f.time,ox=f.avatarX,oz=f.avatarZ;
    // 専用の板を通常合成で描きます。既存オーラ用の発光バッチへは追加しません。
    auto material=[&](FXMMATRIX model,Color tint,unsigned style,float opacity=1.f) {
        if(frutigerInstances_.size()>=2000 || opacity<=0) return;
        Instance v;XMStoreFloat4x4(&v.world,model);
        v.color={tint.r,tint.g,tint.b,style+std::clamp(opacity,0.f,.99f)};
        frutigerInstances_.push_back(v);
    };
    const auto faceCamera=XMMatrixRotationX(f.cameraPitch)*XMMatrixRotationY(-f.cameraYaw);
    auto billboard=[&](float x,float y,float z,float width,float height,Color tint,unsigned style,float opacity=1.f,float roll=0.f) {
        material(XMMatrixScaling(width,height,1)*XMMatrixRotationZ(roll)*faceCamera*XMMatrixTranslation(x,y,z),tint,style,opacity);
    };
    auto doubleSided=[&](FXMMATRIX model,Color tint,unsigned style,float opacity=1.f) {
        material(model,tint,style,opacity);
        material(XMMatrixRotationY(Pi)*model,tint,style,opacity);
    };

    // 1. 円形の草原。表と裏を描くため、上から覗いても床が抜けません。
    const auto meadow=XMMatrixScaling(settings::MeadowRadius*2,settings::MeadowRadius*2,1)*
        XMMatrixRotationX(Pi*.5f)*XMMatrixTranslation(ox,settings::MeadowHeight,oz);
    doubleSided(meadow,settings::GrassColor,0);
    for(unsigned i=0;i<settings::GrassBladeCount;++i) {
        const float angle=i*2.399963f,radius=settings::MeadowRadius*std::sqrt(.10f+random(i,2)*.86f);
        const float height=settings::GrassBladeHeight*(.55f+random(i,3));
        const float sway=.13f*std::sin(t*settings::GrassSwaySpeed+i*.83f);
        const auto model=XMMatrixScaling(.065f+random(i,5)*.055f,height,1)*XMMatrixRotationZ(sway)*
            XMMatrixRotationY(angle)*XMMatrixTranslation(ox+std::cos(angle)*radius,settings::MeadowHeight+height*.43f,oz+std::sin(angle)*radius);
        doubleSided(model,settings::GrassColor,5,.96f);
    }

    // 2. 空はカメラの反対側へ置くので、プレイヤーを隠さず景色の奥行きを作れます。
    billboard(ox-std::sin(f.cameraYaw)*settings::SkyDistance,settings::SkyCenterHeight,
        oz+std::cos(f.cameraYaw)*settings::SkyDistance,settings::SkyWidth,settings::SkyHeight,
        {settings::CloudSpeed/.025f,1,1},1,settings::SkyOpacity);

    // 3. シャボン玉。各玉が違う速さでゆっくり昇り、上下端で自然に薄くなります。
    for(unsigned i=0;i<settings::BubbleCount;++i) {
        const float u=std::fmod(random(i,8)+t*settings::BubbleRiseSpeed*(.52f+random(i,9)*.40f),1.f);
        const float angle=i*2.399963f+t*.065f,radius=settings::BubbleRadius*(.48f+random(i,10)*.48f);
        const float size=settings::BubbleMinSize+(settings::BubbleMaxSize-settings::BubbleMinSize)*random(i,11);
        const float fade=std::clamp(std::sin(u*Pi)*3.f,0.f,1.f);
        billboard(ox+std::cos(angle)*radius,settings::MeadowHeight+.30f+u*settings::BubbleHeight,
            oz+std::sin(angle)*radius,size,size,{1,1,1},2,fade*settings::BubbleOpacity);
    }
    // 大きい泡を両脇へ置き、透明な光沢が最初の一目でも伝わるようにします。
    for(unsigned side=0;side<2;++side) {
        const float angle=-f.cameraYaw+(side?Pi*.66f:-Pi*.39f);
        billboard(ox+std::cos(angle)*3.5f,2.5f+side*.7f+std::sin(t*.6f+side)*.26f,
            oz+std::sin(angle)*3.5f,2.15f,2.15f,{1,1,1},2,.97f);
    }

    // 4. 青い蝶。羽の根元を中心に回転し、単なる平面画像の拡大縮小とは違う羽ばたきに。
    for(unsigned i=0;i<settings::ButterflyCount;++i) {
        const float angle=i*2*Pi/settings::ButterflyCount+t*settings::ButterflyOrbitSpeed;
        const float radius=settings::ButterflyRadius*(.62f+random(i,20)*.35f);
        const float y=.85f+random(i,21)*2.4f+.24f*std::sin(t*1.55f+i);
        const float size=settings::ButterflySize*(.72f+random(i,22)*.48f);
        const float bank=.22f*std::sin(t*.9f+i),flap=.28f+.90f*(.5f+.5f*std::sin(t*settings::ButterflyFlapSpeed+i*1.7f));
        const auto origin=XMMatrixRotationZ(bank)*faceCamera*XMMatrixTranslation(ox+std::cos(angle)*radius,y,oz+std::sin(angle)*radius);
        for(int side:{-1,1}) {
            const auto wing=XMMatrixScaling(size*.74f,size,1)*XMMatrixTranslation(side*size*.34f,0,0)*
                XMMatrixRotationY(side*flap)*origin;
            doubleSided(wing,settings::ButterflyColor,3);
        }
        doubleSided(XMMatrixScaling(size*.15f,size*.75f,1)*origin,{1,1,1},4);
    }

    // 5. 水滴が光を返す程度の小さい星。発光の大きい白い霧は付けません。
    for(unsigned i=0;i<settings::SparkleCount;++i) {
        const float angle=i*2.399963f+t*.07f,radius=settings::MeadowRadius*(.5f+random(i,30)*.48f);
        const float pulse=std::pow(.5f+.5f*std::sin(t*1.2f+i*2.1f),4.f);
        billboard(ox+std::cos(angle)*radius,.30f+random(i,31)*1.5f,oz+std::sin(angle)*radius,
            settings::SparkleSize,settings::SparkleSize,{.83f,1,.68f},6,pulse*.85f);
    }

    // 透明な板は遠いものから順に描きます。近い泡の奥に蝶と青空が正しく透けます。
    const float distance=f.cameraDistance;
    const XMFLOAT3 eye{ox+std::sin(f.cameraYaw)*std::cos(f.cameraPitch)*distance,
        2+std::sin(f.cameraPitch)*distance,oz-std::cos(f.cameraYaw)*std::cos(f.cameraPitch)*distance};
    auto distanceSquared=[&](const Instance& v) {
        const float x=v.world._41-eye.x,y=v.world._42-eye.y,z=v.world._43-eye.z;
        return x*x+y*y+z*z;
    };
    std::stable_sort(frutigerInstances_.begin(),frutigerInstances_.end(),[&](const Instance& a,const Instance& b) {
        return distanceSquared(a)>distanceSquared(b);
    });
}
} // namespace aura
