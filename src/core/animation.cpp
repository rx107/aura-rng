#include "core/animation.hpp"
#include <algorithm>
#include <cmath>

namespace aura {
AvatarPose avatarPose(float time,float walkPhase,float movement,bool singularity) {
    AvatarPose p;
    if(singularity) {
        p.fixed=true;
        p.lift=.55f+.12f*std::sin(time*.8f);
        p.bodyPitch=-.22f; p.bodyRoll=.08f*std::sin(time*.6f);
        p.armPitch={-.3f,-.3f}; p.armOut={.90f,.95f};
        p.legPitch={-.35f,.50f}; p.legOut={.13f,.18f};
        p.knee={-.12f,-.25f};
        return p;
    }
    const float blend=std::clamp(movement,0.f,1.f),swing=std::sin(walkPhase*3.f)*blend;
    p.lift=.018f*std::sin(time*2)+.035f*std::abs(swing);
    p.armPitch={-.50f*swing,.50f*swing}; p.armOut={.06f,.06f};
    p.legPitch={.55f*swing,-.55f*swing};
    p.knee={-.70f*std::max(0.f,-swing),-.70f*std::max(0.f,swing)};
    p.bodyRoll=.025f*swing;
    return p;
}
AvatarPose pixelationPose(float time) {
    AvatarPose p;
    p.fixed=true;
    p.lift=.38f+.07f*std::sin(time*2.f);
    p.bodyRoll=.025f*std::sin(time*1.5f);
    p.armOut={.55f,.55f}; p.armPitch={-.35f,-.35f};
    p.legPitch={-.07f,.10f}; p.legOut={.04f,.04f}; p.knee={-.04f,-.09f};
    return p;
}
AbsorbedStar singularityStar(float time,unsigned index,unsigned count) {
    constexpr float Pi=3.14159265359f;
    const float u=static_cast<float>(index)/std::max(1u,count),phase=std::fmod(time*.20f+u,1.f);
    const float radius=.03f+3.5f*std::pow(1-phase,.85f),angle=u*2*Pi*7+phase*Pi*8+time*.25f;
    return {std::cos(angle)*radius,.5f+5.2f*phase,std::sin(angle)*radius,
        (.12f+.025f*std::sin(index*1.7f))*std::min(1.f,phase*25)*(1-phase*.65f)};
}
}
