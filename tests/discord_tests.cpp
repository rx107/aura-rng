#include "platform/discord.hpp"
#include <windows.h>
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Json = nlohmann::json;
using namespace std::chrono_literals;
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> bool eventually(F&& condition, std::chrono::milliseconds timeout = 2500ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do { if (condition()) return true; std::this_thread::sleep_for(10ms); }
    while (std::chrono::steady_clock::now() < deadline);
    return false;
}
std::wstring endpoint() {
    static unsigned sequence{};
    return L"\\\\.\\pipe\\stars-rng-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
}
std::uint32_t read32(const unsigned char* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
void append32(std::vector<unsigned char>& data, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) data.push_back(static_cast<unsigned char>(value >> shift));
}

// This server uses a private test pipe, never a real discord-ipc endpoint.
class FakeDiscord {
public:
    enum class Mode { Normal, RejectActivity, SilentHandshake, OversizedFrame };
    struct Packet { std::uint32_t opcode; std::string payload; };
    explicit FakeDiscord(const std::wstring& path, Mode mode = Mode::Normal) : mode_(mode) {
        // A sandboxed test token may not match the creator-owner default DACL.
        // This ephemeral PID-specific fake endpoint has no secrets and rejects
        // remote clients; production Discord pipe permissions are never changed.
        SECURITY_DESCRIPTOR descriptor{};
        check(InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) != FALSE &&
            SetSecurityDescriptorDacl(&descriptor, TRUE, nullptr, FALSE) != FALSE, "Create test-only pipe security");
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
        pipe_ = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 8192, 8192, 0, &security);
        check(pipe_ != INVALID_HANDLE_VALUE, "Create isolated fake Discord pipe");
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(stop_ != nullptr, "Create fake-server stop event");
        thread_ = std::thread([this] { run(); });
    }
    ~FakeDiscord() {
        SetEvent(stop_); CancelIoEx(pipe_, nullptr);
        if (thread_.joinable()) thread_.join();
        CloseHandle(pipe_); CloseHandle(stop_);
    }
    bool send(std::uint32_t opcode, const std::string& payload) {
        std::lock_guard lock(writeMutex_);
        std::vector<unsigned char> bytes;
        append32(bytes, opcode); append32(bytes, static_cast<std::uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return transfer(true, bytes.data(), static_cast<DWORD>(bytes.size()));
    }
    std::vector<Json> activities() const {
        std::lock_guard lock(mutex_); return activities_;
    }
    std::vector<Packet> packets() const {
        std::lock_guard lock(mutex_); return packets_;
    }
    bool hasClear() const {
        const auto values = activities();
        for (const auto& j : values) if (j["args"]["activity"].is_null()) return true;
        return false;
    }
    std::size_t activityCount() const {
        const auto values = activities();
        return static_cast<std::size_t>(std::count_if(values.begin(), values.end(), [](const Json& j) {
            return !j["args"]["activity"].is_null();
        }));
    }
private:
    bool complete(OVERLAPPED& operation) {
        const std::array<HANDLE, 2> handles{operation.hEvent, stop_};
        DWORD count{};
        if (WaitForMultipleObjects(2, handles.data(), FALSE, 15000) == WAIT_OBJECT_0)
            return GetOverlappedResult(pipe_, &operation, &count, FALSE) != FALSE;
        CancelIoEx(pipe_, &operation); GetOverlappedResult(pipe_, &operation, &count, TRUE);
        return false;
    }
    bool transfer(bool writing, void* bytes, DWORD size) {
        DWORD remaining = size;
        auto current = static_cast<unsigned char*>(bytes);
        while (remaining && WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT) {
            OVERLAPPED operation{}; operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            DWORD count{};
            BOOL success = writing ? WriteFile(pipe_, current, remaining, &count, &operation) :
                ReadFile(pipe_, current, remaining, &count, &operation);
            if (!success && GetLastError() == ERROR_IO_PENDING) {
                success = complete(operation);
                if (success) success = GetOverlappedResult(pipe_, &operation, &count, FALSE);
            }
            CloseHandle(operation.hEvent);
            if (!success || count == 0) return false;
            current += count; remaining -= count;
        }
        return remaining == 0;
    }
    void run() noexcept {
        try {
            OVERLAPPED connection{}; connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            bool connected = ConnectNamedPipe(pipe_, &connection) != FALSE;
            if (!connected) {
                const auto error = GetLastError();
                connected = error == ERROR_PIPE_CONNECTED || (error == ERROR_IO_PENDING && complete(connection));
            }
            CloseHandle(connection.hEvent);
            if (!connected) return;
            while (WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT) {
                std::array<unsigned char, 8> header{};
                if (!transfer(false, header.data(), static_cast<DWORD>(header.size()))) return;
                const auto opcode = read32(header.data()), length = read32(header.data() + 4);
                if (length > 65536) return;
                std::string payload(length, '\0');
                if (length && !transfer(false, payload.data(), length)) return;
                { std::lock_guard lock(mutex_); packets_.push_back({opcode, payload}); }
                if (opcode == 0) {
                    if (mode_ == Mode::SilentHandshake) continue;
                    if (mode_ == Mode::OversizedFrame) {
                        const std::array<unsigned char, 8> oversized{1,0,0,0,0,0,2,0};
                        auto copy = oversized; transfer(true, copy.data(), static_cast<DWORD>(copy.size()));
                        continue;
                    }
                    if (!send(1, R"({"cmd":"DISPATCH","evt":"READY","data":{"v":1}})")) return;
                } else if (opcode == 1) {
                    const auto json = Json::parse(payload);
                    if (json.value("cmd", "") == "SET_ACTIVITY") {
                        { std::lock_guard lock(mutex_); activities_.push_back(json); }
                        if (mode_ == Mode::RejectActivity && !json["args"]["activity"].is_null()) {
                            send(1, Json{{"cmd", "SET_ACTIVITY"}, {"evt", "ERROR"},
                                {"data", {{"code", 4000}, {"message", "bad application"}}}, {"nonce", json["nonce"]}}.dump());
                        } else send(1, Json{{"cmd", "SET_ACTIVITY"}, {"evt", nullptr}, {"data", nullptr},
                            {"nonce", json["nonce"]}}.dump());
                    }
                }
            }
        } catch (...) { failed_ = true; }
    }
    Mode mode_;
    HANDLE pipe_{INVALID_HANDLE_VALUE}, stop_{};
    std::thread thread_;
    mutable std::mutex mutex_, writeMutex_;
    std::vector<Json> activities_;
    std::vector<Packet> packets_;
    std::atomic<bool> failed_{};
};

void configurationAndProtocol() {
    const auto path = endpoint();
    FakeDiscord server(path);
    aura::DiscordPresence client(path);
    client.update(false, "", "Player", 0, 0);
    check(client.state() == aura::DiscordPresenceState::Disabled, "Presence defaults off");
    client.update(true, "", "Player", 0, 0);
    check(client.state() == aura::DiscordPresenceState::MissingApplicationId, "Missing public Application ID");
    client.update(true, "secret or token", "Player", 0, 0);
    check(client.state() == aura::DiscordPresenceState::InvalidApplicationId, "Non-numeric token rejected");
    client.update(true, "99999999999999999999", "Player", 0, 0);
    check(client.state() == aura::DiscordPresenceState::InvalidApplicationId, "Overflowing snowflake rejected");
    const std::string username = "星\"\\Player\n" + std::string("\xff") + std::string(150, 'x');
    client.update(true, "123456789012345678", username, 42, 0);
    const bool connected = eventually([&] { return client.state() == aura::DiscordPresenceState::Connected; });
    if (!connected) {
        std::cerr << "Client status: " << client.status() << '\n';
        for (const auto& packet : server.packets()) std::cerr << "Opcode " << packet.opcode << ": " << packet.payload << '\n';
    }
    check(connected, "READY and SET_ACTIVITY acknowledged");
    const auto packets = server.packets();
    check(!packets.empty() && packets[0].opcode == 0, "Handshake uses opcode 0");
    const auto handshake = Json::parse(packets[0].payload);
    check(handshake["v"] == 1 && handshake["client_id"] == "123456789012345678", "Public ID only in handshake");
    const auto activity = server.activities().at(0)["args"]["activity"];
    const auto details = activity.at("details").get<std::string>();
    check(details.size() <= 106 && details.find("星\"\\Player") != std::string::npos && details.find('\n') == std::string::npos,
        "UTF-8, quotes, escapes, invalid bytes and bounds handled safely");
    check(activity["state"] == "Rolls: 42" && activity["type"] == 0 && activity["timestamps"]["start"].is_number_integer(),
        "Username and roll count sent as playing activity");
    check(server.send(3, "ping-test"), "Send isolated PING");
    check(eventually([&] { const auto all = server.packets();
        return std::any_of(all.begin(), all.end(), [](const auto& p) { return p.opcode == 4 && p.payload == "ping-test"; });
    }), "PING echoed as PONG");
    for (std::uint64_t rolls = 0; rolls < 1000; ++rolls) client.update(true, "123456789012345678", "更新🌟", rolls, .016f);
    client.update(true, "123456789012345678", "更新🌟", std::numeric_limits<std::uint64_t>::max(), .016f);
    std::this_thread::sleep_for(300ms);
    check(server.activityCount() == 1, "Rapid rolls are coalesced");
    check(eventually([&] { return server.activityCount() == 2; }, 6000ms), "Latest roll snapshot published within 5 seconds");
    const auto final = server.activities().back()["args"]["activity"];
    check(final["details"] == "Username: 更新🌟" && final["state"] == "Rolls: 18446744073709551615", "Latest username and full uint64 count");
    client.update(false, "123456789012345678", "Player", 0, .016f);
    check(eventually([&] { return server.hasClear(); }), "Disable clears activity");
    check(eventually([&] { return client.state() == aura::DiscordPresenceState::Disabled; }), "Disabled status after clear");
}

void lifecycleAndFailures() {
    const auto path = endpoint();
    auto first = std::make_unique<FakeDiscord>(path);
    auto client = std::make_unique<aura::DiscordPresence>(path);
    client->update(true, "123456789012345678", "Player", 10, 0);
    check(eventually([&] { return client->state() == aura::DiscordPresenceState::Connected; }), "Initial connection");
    first.reset();
    check(eventually([&] { return client->state() != aura::DiscordPresenceState::Connected; }), "Detect pipe disconnect");
    FakeDiscord replacement(path);
    check(eventually([&] { return replacement.activityCount() == 1; }, 6500ms), "Reconnect after Discord restarts");
    client.reset();
    check(eventually([&] { return replacement.hasClear(); }), "Destructor clears activity before closing");

    const auto errorPath = endpoint();
    FakeDiscord errors(errorPath, FakeDiscord::Mode::RejectActivity);
    aura::DiscordPresence rejected(errorPath);
    rejected.update(true, "123456789012345678", "Player", 0, 0);
    check(eventually([&] { return rejected.state() == aura::DiscordPresenceState::Error; }), "Discord ERROR surfaced");
    check(rejected.status().find("4000") != std::string::npos, "Error code shown without leaking server data");

    const auto badPath = endpoint();
    FakeDiscord oversized(badPath, FakeDiscord::Mode::OversizedFrame);
    aura::DiscordPresence malformed(badPath);
    malformed.update(true, "123456789012345678", "Player", 0, 0);
    check(eventually([&] { return malformed.state() == aura::DiscordPresenceState::WaitingForDiscord; }), "Oversized packet rejected without allocation");

    const auto absentPath = endpoint();
    aura::DiscordPresence absent(absentPath);
    const auto before = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 1000; ++i) absent.update(true, "123456789012345678", "Player", i, .016f);
    check(std::chrono::steady_clock::now() - before < 500ms, "Unavailable Discord never blocks the game update");
    check(eventually([&] { return absent.state() == aura::DiscordPresenceState::WaitingForDiscord; }), "Missing Discord is a graceful wait state");

    const auto silentPath = endpoint();
    FakeDiscord silent(silentPath, FakeDiscord::Mode::SilentHandshake);
    const auto shutdown = std::chrono::steady_clock::now();
    { aura::DiscordPresence pending(silentPath);
      pending.update(true, "123456789012345678", "Player", 0, 0);
      check(eventually([&] { return !silent.packets().empty(); }), "Silent server accepted handshake"); }
    check(std::chrono::steady_clock::now() - shutdown < 1000ms, "Pending handshake does not stall shutdown");
}
}
int main() {
    try {
        configurationAndProtocol();
        lifecycleAndFailures();
        std::cout << "Discord local IPC: handshake, UTF-8/JSON bounds, latest-snapshot throttle, PING/PONG, clear, reconnect and failures passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
