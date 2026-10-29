#include "render/renderer.hpp"
#include "core/animation.hpp"
#include "tuning/game.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace aura {
using namespace DirectX;
// ローカルと他のプレイヤーは同じ腕・脚・固定ポーズの描画を使います。
// VRMファイルを通信しないため、他人の姿は軽いブロックアバターで表示します。
void Renderer::blockAvatar(const Frame& f,bool useVrm) {
    const auto& d=f.roller.catalog()[f.presentation.shown(f.progress)];
    const auto level=f.presentation.level(f.progress,f.roller.catalog());
    const bool floating=d.id=="singularity",pixelated=d.id=="pixelation",equinox=d.id=="equinox";
    const bool breakthrough=d.id=="breakthrough",illusionary=d.id=="illusionary",redJohn=d.id=="red_john";
    const float effectTime=f.effectTime>=0?f.effectTime:pixelationMotionTime(f.time,pixelated?level:0);
    const float actionTime=f.actionTime>=0?f.actionTime:pixelationActionTime(f.time,pixelated?level:0);
    const auto pose=redJohn?redJohnPose(f.time):illusionary?illusionaryPose(f.time):pixelated?pixelationPose(actionTime):(equinox || breakthrough)?equinoxPose(f.time):avatarPose(f.time,f.walk,f.movement,floating);
    const float posePivot=illusionary?1.45f:0.f;
    const auto base=XMMatrixTranslation(0,-posePivot,0)*XMMatrixRotationX(pose.bodyPitch)*XMMatrixRotationZ(pose.bodyRoll)*
        XMMatrixRotationY(f.facing)*XMMatrixTranslation(f.avatarX,pose.lift+posePivot,f.avatarZ);
    auto part=[&](float x,float y,float z,float sx,float sy,float sz,Color c,FXMMATRIX transform) {
        if(useVrm) return;
        Instance instance{};
        XMStoreFloat4x4(&instance.world,XMMatrixScaling(sx,sy,sz)*XMMatrixTranslation(x,y,z)*transform*base);
        auto lit=c;
        if(pixelated) {const auto hue=pixelationColor(effectTime); lit={.45f+hue.r*.55f,.45f+hue.g*.55f,.45f+hue.b*.55f};}
        if(equinox) lit={.83f,.85f,.90f};
        if(breakthrough) lit={.65f,.71f,1.f};
        // 元の服・肌・顔の濃淡を残し、横向きでも頭と手足を見分けられるようにします。
        if(illusionary) lit={c.r*.76f,c.g*.82f,std::min(1.f,c.b*.82f+.16f)};
        const bool faceDetail=illusionary && c.r<.30f && c.g<.22f;
        instance.color={lit.r,lit.g,lit.b,illusionary?(faceDetail?0.f:.82f):pixelated?1.8f:(equinox || breakthrough)?.65f:0.f}; instances_.push_back(instance);
    };
    const Color skin{.95f,.76f,.47f}, shirt=f.presentation.settings.shirt, pants{.20f,.29f,.42f};
    // 腰より上だけを回す。脚まで回すと「反り返る」代わりに全身が倒れてしまいます。
    const auto torso=XMMatrixTranslation(0,-1.08f,0)*XMMatrixRotationX(pose.torsoPitch)*XMMatrixTranslation(0,1.08f,0);
    for(int side=0;side<2;++side) {
        const float sign=side?1.f:-1.f;
        const auto arm=XMMatrixRotationZ(sign*pose.armOut[side])*XMMatrixRotationX(pose.armPitch[side])*XMMatrixRotationY(pose.armYaw[side])*
            XMMatrixTranslation(sign*.76f,2.02f,0)*torso;
        // 上腕と前腕に分け、肘を中心に曲げる。elbowPitch=0 なら元の一本腕と同じ外形です。
        part(0,-.22f,0,.43f,.48f,.54f,skin,arm);
        const auto elbow=XMMatrixRotationX(pose.elbowPitch[side])*XMMatrixTranslation(0,-.46f,0)*arm;
        part(0,-.24f,0,.43f,.48f,.54f,skin,elbow);
        const auto hip=XMMatrixRotationZ(sign*pose.legOut[side])*XMMatrixRotationX(pose.legPitch[side])*XMMatrixRotationY(pose.legYaw[side])*XMMatrixTranslation(sign*.27f,1.10f,0);
        part(0,-.24f,0,.46f,.48f,.52f,pants,hip);
        const auto knee=XMMatrixRotationX(pose.knee[side])*XMMatrixTranslation(0,-.48f,0)*hip;
        part(0,-.24f,0,.46f,.48f,.52f,pants,knee);
    }
    part(0,1.56f,0,1.03f,.96f,.60f,shirt,torso);
    // 首の付け根を支点にし、顔パーツも頭と一緒に上へ向ける。
    const auto head=XMMatrixTranslation(0,-2.055f,0)*XMMatrixRotationX(pose.headPitch)*XMMatrixTranslation(0,2.055f,0)*torso;
    part(0,2.42f,0,.73f,.73f,.73f,skin,head);
    part(-.16f,2.48f,-.375f,.085f,.10f,.02f,{.10f,.13f,.15f},head);
    part(.16f,2.48f,-.375f,.085f,.10f,.02f,{.10f,.13f,.15f},head);
    part(0,2.26f,-.375f,.24f,.045f,.02f,{.25f,.19f,.17f},head);
}
// 近い4人は通常と同じ詳細オーラ。それ以外は色と輪を表示して描画量を抑えます。
void Renderer::drawRemotePlayers(const Frame& source,FXMMATRIX vp,FXMVECTOR eye,Color fog) {
    playerTags_.clear();
    if(!source.multiplayer || source.multiplayer->state!=net::State::Connected) {remoteMotion_.clear();return;}
    const float dt=std::clamp(source.time-remoteLastTime_,0.f,.25f); remoteLastTime_=source.time;
    std::vector<const net::RemotePlayer*> players;
    for(const auto& p:source.multiplayer->players)
        if(p.userId!=source.multiplayer->userId && p.player.planet==source.presentation.settings.biome)players.push_back(&p);
    std::sort(players.begin(),players.end(),[&](const auto* a,const auto* b){
        return std::hypot(a->player.x-source.avatarX,a->player.z-source.avatarZ)<
               std::hypot(b->player.x-source.avatarX,b->player.z-source.avatarZ);});
    // オーラごとの専用shaderが混ざらないよう、プレイヤー単位で描き、元のバッチを戻します。
    auto savedInstances=std::move(instances_),savedStars=std::move(starInstances_),savedPixels=std::move(pixelationInstances_);
    auto savedOrbs=std::move(equinoxOrbs_),savedTitles=std::move(equinoxTitles_),savedNanally=std::move(nanallyInstances_);
    auto savedInk=std::move(illusionaryInk_),savedFrutiger=std::move(frutigerInstances_);
    const auto savedHole=blackHole_;
    const std::array flags={holeVisible_,pixelationVisible_,equinoxVisible_,breakthroughVisible_,nanallyVisible_,illusionaryVisible_,desertAuraVisible_,frutigerVisible_};
    std::vector<RemoteMotion> nextMotion;
    for(std::size_t number=0;number<players.size() && number<31;++number) {
        const auto& p=*players[number];
        const auto& defs=source.roller.catalog();
        const auto found=std::find_if(defs.begin(),defs.end(),[&](const auto& d){return d.id==p.player.auraId;});
        if(found==defs.end())continue;
        const auto index=static_cast<std::size_t>(found-defs.begin());
        auto motion=std::find_if(remoteMotion_.begin(),remoteMotion_.end(),[&](const auto& m){return m.id==p.userId;});
        RemoteMotion current=motion==remoteMotion_.end()?RemoteMotion{p.userId,p.player.x,p.player.z,p.player.yaw}:*motion;
        const float blend=1-std::exp(-dt*14);
        if(std::hypot(current.x-p.player.x,current.z-p.player.z)>6) {current.x=p.player.x;current.z=p.player.z;}
        current.x=std::lerp(current.x,p.player.x,blend); current.z=std::lerp(current.z,p.player.z,blend);
        current.yaw+=std::remainder(p.player.yaw-current.yaw,6.283185307f)*blend;
        nextMotion.push_back(current);
        Presentation display=source.presentation;
        display.preview=index; display.singularityLevel=p.player.auraLevel; display.pixelationLevel=p.player.auraLevel;
        display.settings.shirt=p.player.shirt; display.settings.avatar=Avatar::Block;
        Frame f{source.roller,source.progress,display,source.logs};
        f.time=source.time; f.avatarX=current.x; f.avatarZ=current.z; f.facing=current.yaw;
        f.walk=source.time*tuning::game::WalkPhaseSpeed; f.movement=p.player.walking?1.f:0.f;
        f.cameraYaw=source.cameraYaw;f.cameraPitch=source.cameraPitch;f.cameraDistance=source.cameraDistance;
        f.equinoxBeat=source.equinoxBeat;
        f.effectTime=pixelationMotionTime(source.time,p.player.auraLevel);
        f.actionTime=pixelationActionTime(source.time,p.player.auraLevel);
        instances_.clear();starInstances_.clear();pixelationInstances_.clear();equinoxOrbs_.clear();equinoxTitles_.clear();
        nanallyInstances_.clear();illusionaryInk_.clear();frutigerInstances_.clear();holeVisible_=false;
        pixelationVisible_=equinoxVisible_=breakthroughVisible_=nanallyVisible_=illusionaryVisible_=desertAuraVisible_=frutigerVisible_=false;
        blockAvatar(f);
        const auto& id=found->id;
        if(number<4) {
            if(id=="pixelation") {pixelationVisible_=true;buildPixelation(f);}
            else if(id=="equinox") {equinoxVisible_=true;buildEquinox(f);}
            else if(id=="breakthrough") {breakthroughVisible_=true;buildBreakthrough(f);}
            else if(id=="nanally") {nanallyVisible_=true;buildNanally(f);}
            else if(id=="illusionary") {illusionaryVisible_=true;buildIllusionary(f);}
            else if(id=="frutiger_aero") {frutigerVisible_=true;buildFrutiger(f);}
            else if(id=="red_john") buildRedJohn(f);
            else if(auraCategory(*found)==AuraCategory::Desert) {desertAuraVisible_=true;buildDesert(f);}
        }
        if(pixelationInstances_.empty() && frutigerInstances_.empty() && !(id=="red_john" && number<4)) {
            // 通常オーラと遠い人は少ない星で表現。1人につき24個で上限を決めます。
            for(unsigned j=0;j<24;++j) {
                const float angle=j*6.283185307f/24+source.time*found->speed;
                particleStar(current.x+std::cos(angle)*found->radius,.35f+(j%5)*.26f,
                    current.z+std::sin(angle)*found->radius,.06f,found->color,angle,f);
            }
        }
        struct Camera {XMFLOAT4X4 matrix;XMFLOAT4 eye,light;} camera{};
        XMStoreFloat4x4(&camera.matrix,vp);XMStoreFloat4(&camera.eye,eye);camera.eye.w=f.effectTime;
        camera.light={1,fog.r,fog.g,fog.b};
        context_->UpdateSubresource(constants_.Get(),0,nullptr,&camera,0,0);
        D3D11_MAPPED_SUBRESOURCE mapped{};checked(context_->Map(instanceBuffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Remote avatar");
        std::memcpy(mapped.pData,instances_.data(),instances_.size()*sizeof(Instance));context_->Unmap(instanceBuffer_.Get(),0);
        ID3D11Buffer* streams[]={vertices_.Get(),instanceBuffer_.Get()};const UINT strides[]={sizeof(Vertex),sizeof(Instance)},offsets[]={0,0};
        ID3D11RenderTargetView* target=target_.Get();context_->OMSetRenderTargets(1,&target,depth_.Get());
        context_->IASetVertexBuffers(0,2,streams,strides,offsets);context_->IASetIndexBuffer(indices_.Get(),DXGI_FORMAT_R16_UINT,0);
        context_->IASetInputLayout(layout_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
        ID3D11Buffer* cb=constants_.Get();context_->VSSetConstantBuffers(0,1,&cb);context_->PSSetConstantBuffers(0,1,&cb);
        context_->RSSetState(raster_.Get());context_->OMSetDepthStencilState(depthState_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context_->DrawIndexedInstanced(36,static_cast<UINT>(instances_.size()),0,0,0);drawEffects(f);
        // 自分と相手の名前に同じ全画面の投影を使い、位置だけ旧UI枠へずれるのを防ぎます。
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        XMFLOAT3 tag;XMStoreFloat3(&tag,XMVector3Project(XMVectorSet(current.x,3.6f,current.z,1),viewport.TopLeftX,viewport.TopLeftY,
            viewport.Width,viewport.Height,0,1,vp,XMMatrixIdentity(),XMMatrixIdentity()));
        if(tag.z>0 && tag.z<1)playerTags_.push_back({tag.x/scale_,tag.y/scale_,p.username,p.userId,displayName(*found,display.settings.language)});
    }
    remoteMotion_=std::move(nextMotion);
    instances_=std::move(savedInstances);starInstances_=std::move(savedStars);pixelationInstances_=std::move(savedPixels);
    equinoxOrbs_=std::move(savedOrbs);equinoxTitles_=std::move(savedTitles);nanallyInstances_=std::move(savedNanally);
    illusionaryInk_=std::move(savedInk);frutigerInstances_=std::move(savedFrutiger);blackHole_=savedHole;
    holeVisible_=flags[0];pixelationVisible_=flags[1];equinoxVisible_=flags[2];breakthroughVisible_=flags[3];
    nanallyVisible_=flags[4];illusionaryVisible_=flags[5];desertAuraVisible_=flags[6];frutigerVisible_=flags[7];
}
void Renderer::multiplayerLabels(const Frame&) {
    d2d_->PushAxisAlignedClip(D2D1::RectF(0,0,logicalW_,logicalH_),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for(const auto& tag:playerTags_) {
        rect(tag.x-105,tag.y-24,210,48,D2D1::ColorF(.025f,.055f,.075f,.88f),8);
        label(tag.name,tag.x-100,tag.y-22,200,20,13,D2D1::ColorF(1,1,1),true,true);
        label("#"+tag.id+" / "+tag.aura,tag.x-100,tag.y,200,18,10,D2D1::ColorF(.64f,.91f,1),false,true);
    }
    d2d_->PopAxisAlignedClip();
}
} // namespace aura
