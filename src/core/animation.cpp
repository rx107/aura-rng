#include "core/animation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

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
float pixelationMotionSpeed(unsigned level) {
    return level>=3?3.f:(level>=1?1.6f:1.f);
}
float pixelationMotionTime(float elapsed,unsigned level) {
    if (!std::isfinite(elapsed) || elapsed<=0) return 0;
    return static_cast<float>(std::min(static_cast<double>(elapsed)*pixelationMotionSpeed(level),
        static_cast<double>(std::numeric_limits<float>::max())));
}
float pixelationActionSpeed(unsigned level) {
    return level>=1?1.6f:1.f;
}
float pixelationActionTime(float elapsed,unsigned level) {
    if (!std::isfinite(elapsed) || elapsed<=0) return 0;
    return static_cast<float>(std::min(static_cast<double>(elapsed)*pixelationActionSpeed(level),
        static_cast<double>(std::numeric_limits<float>::max())));
}
AvatarPose equinoxPose(float time) {
    AvatarPose p;
    p.fixed=true;
    p.lift=.28f+.04f*std::sin(time*1.3f);
    p.bodyRoll=.015f*std::sin(time*.8f);
    p.armOut={.35f,.35f}; p.armPitch={-.15f,-.15f}; p.legOut={.03f,.03f};
    return p;
}
PixelationVolley pixelationVolley(float effectTime) {
    const double time=std::isfinite(effectTime)?std::clamp(static_cast<double>(effectTime),0.,1.e12):0.;
    const auto cycle=std::floor(time/PixelationVolleyPeriod);
    return pixelationVolley(static_cast<float>(time),static_cast<float>(time),static_cast<float>(cycle*PixelationVolleyPeriod));
}
PixelationVolley pixelationVolley(float actionTime,float orbitTime,float shotOrbitTime) {
    constexpr float Pi=3.14159265359f;
    // Bound exceptional clocks before converting a cycle index to an integer.
    const double time=std::isfinite(actionTime)?std::clamp(static_cast<double>(actionTime),0.,1.e12):0.;
    const double orbit=std::isfinite(orbitTime)?std::clamp(static_cast<double>(orbitTime),0.,1.e12):0.;
    const double launchOrbit=std::isfinite(shotOrbitTime)?std::clamp(static_cast<double>(shotOrbitTime),0.,1.e12):0.;
    const auto cycle=static_cast<std::int64_t>(std::floor(time/PixelationVolleyPeriod));
    const double shotTime=static_cast<double>(cycle)*PixelationVolleyPeriod;
    PixelationVolley volley;
    volley.shotCycle=cycle;
    volley.shotAge=static_cast<float>(time-shotTime);
    volley.flightProgress=std::clamp(volley.shotAge/PixelationVolleyFlight,0.f,1.f);
    volley.flightVisible=volley.shotAge<PixelationVolleyFlight;
    volley.impactCycle=volley.flightVisible?cycle-1:cycle;
    if (volley.impactCycle>=0) volley.impactAge=static_cast<float>(time-
        (static_cast<double>(volley.impactCycle)*PixelationVolleyPeriod+PixelationVolleyFlight));
    auto player=[](double clock,unsigned i) {
        constexpr double Pi=3.14159265358979323846;
        const double angle=clock*.95+i*Pi+.3;
        return EffectPoint{static_cast<float>(std::cos(angle)*2.45),
            static_cast<float>(2.5+.36*std::sin(angle*1.4)),static_cast<float>(std::sin(angle)*2.45)};
    };
    for (unsigned i=0;i<2;++i) {
        volley.players[i]=player(orbit,i);
        volley.launches[i]=player(launchOrbit,i);
        if (volley.flightProgress>=1) volley.balls[i]=volley.meeting;
        else {
            const float u=volley.flightProgress,arc=.35f*std::sin(Pi*u);
            const auto& from=volley.launches[i];
            volley.balls[i]={from.x+(volley.meeting.x-from.x)*u,
                from.y+(volley.meeting.y-from.y)*u+arc,from.z+(volley.meeting.z-from.z)*u};
        }
    }
    return volley;
}
AbsorbedStar singularityStar(float time,unsigned index,unsigned count) {
    constexpr float Pi=3.14159265359f;
    const float u=static_cast<float>(index)/std::max(1u,count),phase=std::fmod(time*.20f+u,1.f);
    const float radius=.03f+3.5f*std::pow(1-phase,.85f),angle=u*2*Pi*7+phase*Pi*8+time*.25f;
    return {std::cos(angle)*radius,.5f+5.2f*phase,std::sin(angle)*radius,
        (.12f+.025f*std::sin(index*1.7f))*std::min(1.f,phase*25)*(1-phase*.65f)};
}
}
