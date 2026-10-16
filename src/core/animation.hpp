#pragma once
#include <array>
#include <cstdint>

namespace aura {
struct AvatarPose {
    bool fixed{};
    float lift{},bodyPitch{},bodyRoll{};
    std::array<float,2> armPitch{},armOut{},legPitch{},legOut{},knee{};
};
// Both renderers use this pose. Fixed auras ignore gait phase and movement blend.
AvatarPose avatarPose(float time,float walkPhase,float movement,bool singularity);
AvatarPose pixelationPose(float time);
AvatarPose equinoxPose(float time);
float pixelationMotionSpeed(unsigned level);
float pixelationMotionTime(float elapsed,unsigned level);
struct EffectPoint { float x{},y{},z{}; };
inline constexpr float PixelationVolleyPeriod=1.6f;
inline constexpr float PixelationVolleyFlight=.64f;
struct PixelationVolley {
    std::array<EffectPoint,2> players{},launches{},balls{};
    EffectPoint meeting{0,3.60f,0};
    float flightProgress{},shotAge{},impactAge{-1};
    bool flightVisible{};
    std::int64_t shotCycle{},impactCycle{-1};
};
// Coordinates are relative to the avatar. Both projectiles share one flight phase.
PixelationVolley pixelationVolley(float effectTime);
struct AbsorbedStar { float x{},y{},z{},size{}; };
AbsorbedStar singularityStar(float time,unsigned index,unsigned count);
}
