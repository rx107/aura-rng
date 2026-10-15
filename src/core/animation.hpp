#pragma once
#include <array>

namespace aura {
struct AvatarPose {
    bool fixed{};
    float lift{},bodyPitch{},bodyRoll{};
    std::array<float,2> armPitch{},armOut{},legPitch{},legOut{},knee{};
};
// Both renderers use this pose. Fixed auras ignore gait phase and movement blend.
AvatarPose avatarPose(float time,float walkPhase,float movement,bool singularity);
AvatarPose pixelationPose(float time);
struct AbsorbedStar { float x{},y{},z{},size{}; };
AbsorbedStar singularityStar(float time,unsigned index,unsigned count);
}
