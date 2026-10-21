#include "core/animation.hpp"
#include "tuning/avatar.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace aura {
// コード地図: 通常/固定姿勢 → Pixelation の 2 時計 → 弾の軌道 → 吸い込まれる星。
// ここは描画 API を使わない計算部分です。日常の見た目調整は src/tuning/ へ。
AvatarPose avatarPose(float time,float walkPhase,float movement,bool singularity) {
    namespace pose=tuning::avatar;
    AvatarPose p;
    if(singularity) {
        // 先に return するので、このポーズには下の歩行アニメーションが混ざりません。
        p.fixed=true;
        p.lift=pose::singularity::Lift+pose::singularity::BobAmplitude*std::sin(time*pose::singularity::BobFrequency);
        p.bodyPitch=pose::singularity::BodyPitch;
        p.bodyRoll=pose::singularity::RollAmplitude*std::sin(time*pose::singularity::RollFrequency);
        p.armPitch=pose::singularity::ArmPitch; p.armOut=pose::singularity::ArmOut;
        p.legPitch=pose::singularity::LegPitch; p.legOut=pose::singularity::LegOut;
        p.knee=pose::singularity::Knee;
        return p;
    }
    // movement=0 で停止、1 で通常歩行。左右の符号を逆にして交互に振ります。
    const float blend=std::clamp(movement,0.f,1.f),swing=std::sin(walkPhase*pose::walking::GaitFrequency)*blend;
    p.lift=pose::walking::IdleLift*std::sin(time*pose::walking::IdleFrequency)+pose::walking::StepLift*std::abs(swing);
    p.armPitch={-pose::walking::ArmSwing*swing,pose::walking::ArmSwing*swing};
    p.armOut={pose::walking::ArmOut,pose::walking::ArmOut};
    p.legPitch={pose::walking::LegSwing*swing,-pose::walking::LegSwing*swing};
    p.knee={-pose::walking::KneeBend*std::max(0.f,-swing),-pose::walking::KneeBend*std::max(0.f,swing)};
    p.bodyRoll=pose::walking::BodyRoll*swing;
    return p;
}
AvatarPose pixelationPose(float time) {
    namespace pose=tuning::avatar::pixelation;
    AvatarPose p;
    p.fixed=true;
    p.lift=pose::Lift+pose::BobAmplitude*std::sin(time*pose::BobFrequency);
    p.bodyRoll=pose::RollAmplitude*std::sin(time*pose::RollFrequency);
    p.armOut=pose::ArmOut; p.armPitch=pose::ArmPitch;
    p.legPitch=pose::LegPitch; p.legOut=pose::LegOut; p.knee=pose::Knee;
    return p;
}
AvatarPose illusionaryPose(float time) {
    namespace pose=tuning::avatar::illusionary;
    AvatarPose p; p.fixed=true;
    p.lift=pose::Lift+pose::BobAmplitude*std::sin(time*pose::BobFrequency);
    p.bodyPitch=pose::BodyPitch;
    p.bodyRoll=pose::BodyRoll;
    p.torsoPitch=pose::TorsoPitch; p.headPitch=pose::HeadPitch;
    p.armOut=pose::ArmOut; p.armPitch=pose::ArmPitch;
    p.elbowPitch=pose::ElbowPitch;
    p.legPitch=pose::LegPitch; p.legOut=pose::LegOut; p.knee=pose::Knee;
    return p;
}
float pixelationMotionSpeed(unsigned level) {
    namespace effect=tuning::pixelation;
    return level>=3?effect::FinalMotionSpeed:(level>=1?effect::BoostedSpeed:effect::BaseSpeed);
}
float pixelationMotionTime(float elapsed,unsigned level) {
    if (!std::isfinite(elapsed) || elapsed<=0) return 0;
    return static_cast<float>(std::min(static_cast<double>(elapsed)*pixelationMotionSpeed(level),
        static_cast<double>(std::numeric_limits<float>::max())));
}
float pixelationActionSpeed(unsigned level) {
    // Lv3 でも FinalMotionSpeed を使いません。弾・爆発・浮遊は Lv2 と同じです。
    return level>=1?tuning::pixelation::BoostedSpeed:tuning::pixelation::BaseSpeed;
}
float pixelationActionTime(float elapsed,unsigned level) {
    if (!std::isfinite(elapsed) || elapsed<=0) return 0;
    return static_cast<float>(std::min(static_cast<double>(elapsed)*pixelationActionSpeed(level),
        static_cast<double>(std::numeric_limits<float>::max())));
}
AvatarPose equinoxPose(float time) {
    namespace pose=tuning::avatar::equinox;
    AvatarPose p;
    p.fixed=true;
    p.lift=pose::Lift+pose::BobAmplitude*std::sin(time*pose::BobFrequency);
    p.bodyRoll=pose::RollAmplitude*std::sin(time*pose::RollFrequency);
    p.armOut=pose::ArmOut; p.armPitch=pose::ArmPitch; p.legOut=pose::LegOut;
    return p;
}
EffectPoint pixelationPlayer(float orbitTime,unsigned player) {
    namespace effect=tuning::pixelation;
    constexpr double Pi=3.14159265358979323846;
    // 現在の機体・発射時の機体・光の尾の3つが必ず同じ軌道を使います。
    // 描画側だけで上下位置を足すと、発射する弾が機体から離れるので、ここでまとめて計算します。
    const double clock=std::isfinite(orbitTime)?std::clamp(static_cast<double>(orbitTime),0.,1.e12):0.;
    const double angle=clock*effect::PlayerOrbitSpeed+(player%2)*Pi+effect::PlayerOrbitPhase;
    return {static_cast<float>(std::cos(angle)*effect::PlayerOrbitRadius),
        static_cast<float>(effect::PlayerHeight+effect::PlayerBobAmplitude*std::sin(angle*effect::PlayerBobFrequency)),
        static_cast<float>(std::sin(angle)*effect::PlayerOrbitRadius)};
}
PixelationVolley pixelationVolley(float effectTime) {
    const double time=std::isfinite(effectTime)?std::clamp(static_cast<double>(effectTime),0.,1.e12):0.;
    const auto cycle=std::floor(time/PixelationVolleyPeriod);
    return pixelationVolley(static_cast<float>(time),static_cast<float>(time),static_cast<float>(cycle*PixelationVolleyPeriod));
}
PixelationVolley pixelationVolley(float actionTime,float orbitTime,float shotOrbitTime) {
    constexpr float Pi=3.14159265359f;
    // 壊れた時計値を先に制限し、サイクル番号の整数変換を安全にします。
    const double time=std::isfinite(actionTime)?std::clamp(static_cast<double>(actionTime),0.,1.e12):0.;
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
    for (unsigned i=0;i<2;++i) {
        volley.players[i]=pixelationPlayer(orbitTime,i);
        volley.launches[i]=pixelationPlayer(shotOrbitTime,i);
        if (volley.flightProgress>=1) volley.balls[i]=volley.meeting;
        else {
            const float u=volley.flightProgress,arc=tuning::pixelation::ProjectileArc*std::sin(Pi*u);
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
