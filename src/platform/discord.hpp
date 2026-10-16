#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace aura {
enum class DiscordPresenceState {
    Disabled, MissingApplicationId, InvalidApplicationId, Connecting,
    WaitingForDiscord, Connected, Error
};

// Local Discord desktop IPC only. No token, SDK, DLL, or network connection.
// update() only replaces a small snapshot; all pipe operations run on a worker.
class DiscordPresence {
public:
    // A local named-pipe override is reserved for the isolated fake-server test.
    explicit DiscordPresence(std::wstring pipeOverride = {});
    ~DiscordPresence();
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;
    void update(bool enabled, const std::string& applicationId,
        const std::string& username, std::uint64_t rolls, float dt);
    std::string status() const;
    DiscordPresenceState state() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
