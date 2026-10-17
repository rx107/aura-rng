#include "render/renderer.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aura {
using namespace DirectX;
namespace {
constexpr float Pi=3.14159265359f;
// Original procedural materials: the reference is never sampled or shipped.
// A spherical impostor reprojects its face into avatar space, so the creature
// has a front, sides and a back when the camera or player turns.
constexpr char NanallyShader[]=R"(
cbuffer Camera:register(b0) { row_major float4x4 viewProjection; float4 eyeAndTime; };
struct Input { float4 p:SV_POSITION; float3 n:NORMAL; float4 color:COLOR0; float3 world:TEXCOORD0; float3 local:TEXCOORD1; };
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float noise(float2 p) { float2 v=floor(p),u=frac(p);u=u*u*(3-2*u);return lerp(lerp(hash(v),hash(v+float2(1,0)),u.x),lerp(hash(v+float2(0,1)),hash(v+1),u.x),u.y); }
float segment(float2 q,float2 a,float2 b) { float2 v=b-a;return length(q-a-v*saturate(dot(q-a,v)/dot(v,v))); }
float circle(float2 q,float2 c,float2 scale) { return length((q-c)/scale)-1; }
float fistDistance(float2 q) {
    // Four rounded knuckles, a broad palm and an inward-facing thumb.
    float2 b=abs(q-float2(0,-.08))-float2(.69,.54);
    float d=length(max(b,0))+min(max(b.x,b.y),0)-.17;
    [unroll]for(int k=0;k<4;++k) d=min(d,circle(q,float2(-.54+k*.35,.44+sin(k*1.1)*.07),float2(.25,.33))*.25);
    return min(d,circle(q,float2(.62,-.13),float2(.32,.42))*.32);
}
float earDistance(float2 q) {
    // A slightly curved, blunt devil/cat ear rather than a flat sharp triangle.
    float x=q.x+.17*(q.y+.2)*(q.y+.2);
    float width=lerp(.88,.08,saturate((q.y+.85)/1.7));
    return max(abs(x)-width,max(-q.y-.88,q.y-.91));
}
float roundedMin(float a,float b,float k) {
    float h=max(k-abs(a-b),0)/k;return min(a,b)-h*h*k*.25;
}
float3 creatureNormal(float2 q,float yaw,float pitch) {
    float cy=cos(yaw),sy=sin(yaw),cp=cos(pitch),sp=sin(pitch);
    return float3(q.x*cy-q.y*sy*sp+sqrt(saturate(1-dot(q,q)))*sy*cp,
                  q.y*cp+sqrt(saturate(1-dot(q,q)))*sp,
                  q.x*sy+q.y*cy*sp-sqrt(saturate(1-dot(q,q)))*cy*cp);
}
float faceMask(float3 n) {
    float x=abs(n.x),y=n.y;
    // Slanted upper lids descend towards the center. Soft corners retain the
    // angry shape as it wraps around the round head at an oblique camera angle.
    float top=.48*x+.015,bottom=-.065+.095*x;
    float eyeEdge=roundedMin(roundedMin(x-.19,.815-x,.045),roundedMin(y-bottom,top-y,.045),.040);
    float eye=smoothstep(-.009,.012,eyeEdge);
    float upper=-.385+.17*x*x,lower=-.665+.36*x*x;
    float mouth=(1-smoothstep(.76,.82,x))*smoothstep(lower-.012,lower+.008,y)*(1-smoothstep(upper-.008,upper+.012,y));
    // Small dark notches leave a bold serrated grin with distinct fangs.
    float tooth1=smoothstep(-.009,.009,y-(upper-.145+abs(x-.32)*1.8));
    float tooth2=1-smoothstep(-.009,.009,y-(lower+.145-abs(x-.59)*1.8));
    mouth*=1-saturate(tooth1+tooth2);
    return saturate(eye+mouth)*smoothstep(.08,.27,-n.z);
}
float4 body(Input i):SV_TARGET {
    float2 q=i.local.xy*2; int style=(int)floor(i.color.a);float edge,alpha;
    float3 color;
    if(style==0) {
        float radius=length(q);clip(1-radius);float z=sqrt(saturate(1-radius*radius));
        float3 n=creatureNormal(q,i.color.r,i.color.g);float face=faceMask(n);
        float rim=pow(1-z,2.2),brow=exp(-pow((n.y-.48)*12,2))*smoothstep(.28,.85,abs(n.x));
        float3 velvet=float3(.026,.0045,.011)+float3(.064,.006,.012)*(.25+q.y*.3+z*.25);
        velvet+=float3(.38,.014,.043)*rim;
        velvet*=1-brow*.42;
        color=lerp(velvet,float3(1,.93,.91),face);
        alpha=1-smoothstep(.992,1,radius);
    } else if(style==1) {
        edge=earDistance(q);clip(.025-edge);
        float rim=exp(-abs(edge)*37);
        color=float3(.034,.004,.013)+float3(.40,.018,.04)*rim;
        alpha=1-smoothstep(-.015,.025,edge);
    } else {
        edge=fistDistance(q);clip(.025-edge);
        float rim=exp(-abs(edge)*42),shade=saturate(1-length(q)*.65);
        color=float3(.025,.004,.010)+float3(.06,.008,.015)*shade+float3(.45,.016,.041)*rim;
        // Fine, recessed folds separate the knuckles without a noisy texture.
        float folds=exp(-pow((q.y-.22)*28,2))*(exp(-abs(q.x+.35)*38)+exp(-abs(q.x)*38)+exp(-abs(q.x-.35)*38));
        color*=1-folds*.75;
        alpha=1-smoothstep(-.015,.025,edge);
    }
    // The spirit becomes translucent only when the camera is near its back.
    // This preserves the player's silhouette while retaining an opaque front.
    float rear=1-smoothstep(-.85,-.25,cos(i.color.r));
    return float4(color,alpha*lerp(1,.25,rear));
}
float4 glow(Input i):SV_TARGET {
    float2 q=i.local.xy*2;int style=(int)floor(i.color.a);float opacity=frac(i.color.a)*1.011;
    float time=eyeAndTime.w,r2=dot(q,q),mask=0;float3 color=i.color.rgb;
    if(style==10) {
        float radius=length(q);float face=faceMask(creatureNormal(q,i.color.r,i.color.g))*(1-smoothstep(.99,1,radius));
        float rim=exp(-abs(radius-.98)*100)*(1-smoothstep(1,1.04,radius));
        color=float3(1.3,.94,.91)*face*.86+float3(1.8,.032,.12)*rim*.62;
        return float4(color,opacity);
    }
    if(style==11) {
        float d=earDistance(q);mask=exp(-abs(d)*52)*smoothstep(.02,.56,q.y);color=float3(1.7,.038,.11);
    } else if(style==12) {
        float d=fistDistance(q);mask=exp(-abs(d)*56);color=float3(1.9,.035,.13);
    } else if(style==13) {
        // A torn, continuously advecting red/white fire corona. Its transparent
        // middle leaves the black creature visible; its fringe curls upwards.
        float2 p=q*float2(1,1.05);float r=length(p),a=atan2(p.y,p.x);
        float n=noise(float2(a*8,r*10-time*1.35))*.65+noise(float2(a*17+3,r*19-time*2.1))*.35;
        float radius=.76+(.06+.13*saturate(q.y+.25))*n;
        float flare=exp(-abs(r-radius)*(35+n*13));
        float plume=exp(-abs(r-radius-.035)*(14+n*8))*.34;
        float breakUp=.56+.44*noise(float2(a*11,time*.9));
        mask=(flare+plume)*breakUp*(1-smoothstep(.95,1.03,r));
        color=lerp(float3(1.8,.018,.095),float3(2.1,.70,.64),pow(saturate(flare),2.5));
    } else if(style==14) {
        float y=q.y*.5+.5;
        float bend=sin(y*6-time*2.5+i.color.b)*.12+sin(y*13-time*3)*.05;
        float width=(1-y)*(.40+.12*noise(float2(y*7-time*2,i.color.b)));
        mask=exp(-pow((q.x-bend)/max(.025,width),2))*sin(y*3.14159)*(.45+.55*noise(float2(y*10-time*2,q.x*6)));
    } else if(style==2) {
        mask=(exp(-q.y*q.y*600)*1.7+exp(-q.y*q.y*42)*.20)*(1-smoothstep(.90,1,abs(q.x)));
    } else if(style==4) {
        mask=exp(-r2*16)*.8+(exp(-abs(q.x)*65)*exp(-abs(q.y)*5)+exp(-abs(q.y)*65)*exp(-abs(q.x)*5))*.8;
    } else {
        mask=exp(-r2*5.5)*.30*(1-smoothstep(.80,1,max(abs(q.x),abs(q.y))));
    }
    return float4(color*mask,opacity);
})";
float random(unsigned i,unsigned salt) {
    const float v=std::sin(i*127.1f+salt*311.7f)*43758.5453f;
    return v-std::floor(v);
}
}

void Renderer::createNanally() {
    auto compile=[&](const char* entry,ID3D11PixelShader** out) {
        ComPtr<ID3DBlob> shader,errors;
        const auto hr=D3DCompile(NanallyShader,sizeof(NanallyShader)-1,"nanally.hlsl",nullptr,nullptr,entry,"ps_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors);
        if(FAILED(hr) && errors)throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        checked(hr,"Compile Nanally procedural material");
        checked(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,out),"Nanally material");
    };
    compile("body",&nanallyPs_);compile("glow",&nanallyGlowPs_);
    nanallyInstances_.reserve(8);
}

void Renderer::buildNanally(const Frame& f) {
    const float t=f.time,hover=std::sin(t*1.45f)*.11f;
    const auto avatar=XMMatrixRotationY(f.facing)*XMMatrixTranslation(f.avatarX,0,f.avatarZ);
    auto point=[&](float x,float y,float z) {return XMVector3TransformCoord(XMVectorSet(x,y,z,1),avatar);};
    // All creature parts are physically behind the player in avatar space.
    // An orbiting camera can see the reverse side instead of a front-facing decal.
    const auto center=point(0,3.52f+hover,2.85f);
    const float headY=XMVectorGetY(center),headX=XMVectorGetX(center),headZ=XMVectorGetZ(center);
    const XMVECTOR eye=XMVectorSet(f.avatarX+std::sin(f.cameraYaw)*std::cos(f.cameraPitch)*f.cameraDistance,
        2.8f+std::sin(f.cameraPitch)*f.cameraDistance,f.avatarZ-std::cos(f.cameraYaw)*std::cos(f.cameraPitch)*f.cameraDistance,1);
    const auto delta=XMVectorSubtract(eye,center);
    const float yaw=std::atan2(XMVectorGetX(delta),-XMVectorGetZ(delta));
    const float pitch=std::atan2(XMVectorGetY(delta),std::hypot(XMVectorGetX(delta),XMVectorGetZ(delta)));
    const auto headBasis=XMMatrixRotationX(pitch)*XMMatrixRotationY(-yaw)*XMMatrixTranslation(headX,headY,headZ);
    const Color faceParameters{yaw+f.facing,pitch,0};
    auto solid=[&](FXMMATRIX model,unsigned style,Color parameters=Color{}) {
        Instance v;XMStoreFloat4x4(&v.world,model);v.color={parameters.r,parameters.g,parameters.b,style+.99f};nanallyInstances_.push_back(v);
    };
    auto billboard=[&](XMVECTOR p,float w,float h,float turn=0.f) {
        return XMMatrixScaling(w,h,1)*XMMatrixRotationZ(turn)*XMMatrixRotationX(pitch)*XMMatrixRotationY(-yaw)*
            XMMatrixTranslation(XMVectorGetX(p),XMVectorGetY(p),XMVectorGetZ(p));
    };
    const Color red{1.9f,.018f,.095f},pink{1.7f,.11f,.20f},teal{.025f,1.55f,1.24f};
    // Horns are composited first, letting the head cover their rounded bases.
    for(int s:{-1,1}) {
        const auto p=point(s*2.35f,6.0f+hover,2.87f);
        const auto model=billboard(p,1.55f,2.0f,s*-.24f);
        solid(model,1,faceParameters);pixelQuad(model,red,11,.56f);
    }
    const auto head=XMMatrixScaling(6.35f,5.85f,1)*headBasis;
    solid(head,0,faceParameters);pixelQuad(head,faceParameters,10,.50f);
    pixelQuad(XMMatrixScaling(8.15f,7.75f,1)*headBasis,red,13,.70f);
    // Detached, oversized clenched hands hover beside the creature. Their red
    // cuffs are separate curved bands and retain their shape while it bobs.
    for(int s:{-1,1}) {
        const float bob=std::sin(t*1.6f+s*.65f)*.14f;
        const auto p=point(s*3.45f,1.63f+hover+bob,1.95f);
        const auto model=billboard(p,2.10f,2.05f,s*-.21f);
        solid(model,2,faceParameters);pixelQuad(model,red,12,.67f);
        for(unsigned ring=0;ring<3;++ring)for(unsigned k=0;k<28;++k) {
            const float a=k*2*Pi/28,b=(k+1)*2*Pi/28,r=.74f+ring*.07f;
            const auto band=XMMatrixRotationZ(s*-.21f)*XMMatrixRotationY(f.facing)*
                XMMatrixTranslation(XMVectorGetX(p),XMVectorGetY(p)-.34f-ring*.065f,XMVectorGetZ(p));
            const auto from=XMVector3TransformCoord(XMVectorSet(std::cos(a)*r,0,std::sin(a)*r*.80f,1),band);
            const auto to=XMVector3TransformCoord(XMVectorSet(std::cos(b)*r,0,std::sin(b)*r*.80f,1),band);
            pixelLine(from,to,.045f,pink,.52f,f);
        }
        const auto plume=point(s*3.45f,2.05f+hover+bob,2.20f);
        pixelQuad(billboard(plume,3.0f,3.25f),red,13,.57f);
    }
    // Fine flame tongues and loose embers make the silhouette breathe instead
    // of turning it into a static red outline or a full-screen wash.
    for(unsigned k=0;k<26;++k) {
        const float a=k*2*Pi/26,phase=std::fmod(t*.57f+random(k,4),1.f);
        const float r=3.10f+phase*.32f,x=std::cos(a)*r,y=3.52f+std::sin(a)*r*.94f+hover+phase*.30f;
        const auto p=point(x,y,2.90f);
        const float size=.36f+random(k,1)*.46f,fade=std::sin(phase*Pi);
        pixelQuad(billboard(p,size,size*2.6f,.1f*std::sin(k+t)),red,14,.36f*fade);
    }
    for(unsigned k=0;k<52;++k) {
        const float phase=std::fmod(t*.21f+random(k,5),1.f),angle=random(k,2)*2*Pi;
        const float radius=2.5f+random(k,3)*1.7f;
        const auto p=point(std::cos(angle)*radius,.35f+phase*6.4f,2.3f+std::sin(angle)*.7f);
        const float size=.018f+random(k,6)*.032f;
        pixelBillboard(XMVectorGetX(p),XMVectorGetY(p),XMVectorGetZ(p),size,size,pink,4,std::sin(phase*Pi)*.52f,f);
    }
    // Brief, small teal zigzags cling to the player. Each bolt lives only a
    // fraction of a second; no permanent electric cage obscures the avatar.
    for(unsigned k=0;k<18;++k) {
        const float clock=t*(2.3f+random(k,1)*1.7f)+random(k,3)*11;
        const auto cycle=static_cast<unsigned>(std::floor(clock));
        const float phase=clock-std::floor(clock);
        if(phase>.30f)continue;
        const float a=random(k+cycle*19,4)*2*Pi;
        const float radius=.64f+random(k,5)*.24f;
        const float y=.35f+random(k+cycle*7,6)*2.20f;
        const float length=.29f+random(k+cycle*11,8)*.32f;
        const float direction=random(k+cycle*13,2)>.5f?1.f:-1.f;
        XMVECTOR previous{};
        for(unsigned j=0;j<6;++j) {
            const float u=j/5.f,wave=j>0 && j<5?(j%2?1.f:-1.f)*.075f:0;
            const auto next=point(std::cos(a)*(radius+wave),y+u*length,std::sin(a)*(radius+wave)+direction*u*.09f);
            if(j)pixelLine(previous,next,.12f,teal,.84f*std::sin(phase/.30f*Pi),f);
            previous=next;
        }
        if(phase<.11f)pixelBillboard(XMVectorGetX(previous),XMVectorGetY(previous),XMVectorGetZ(previous),.15f,.15f,teal,4,.52f,f);
    }
}
} // namespace aura
