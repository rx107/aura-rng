#include "platform/discord.hpp"
#include <windows.h>
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace aura {
namespace {
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
constexpr auto RetryDelay = std::chrono::seconds(5);
constexpr auto UpdateDelay = std::chrono::seconds(5);
constexpr std::size_t MaxFrameBytes = 65536;

bool validId(std::string_view id) {
    if (id.size() < 17 || id.size() > 20) return false;
    std::uint64_t value{};
    for (char c : id) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<unsigned>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return value != 0;
}

// The UI normally supplies valid UTF-8, but persisted or test input can be invalid.
// Keep whole code points and omit controls before passing data to JSON/Discord.
std::string safeUsername(std::string_view input) {
    input = input.substr(0, 384);
    std::string result;
    result.reserve(96);
    for (std::size_t i = 0; i < input.size() && result.size() < 96;) {
        const auto c = static_cast<unsigned char>(input[i]);
        if (c < 0x20 || c == 0x7f) { ++i; continue; }
        std::size_t length = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
            c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool valid = length && i + length <= input.size();
        for (std::size_t j = 1; valid && j < length; ++j)
            valid = (static_cast<unsigned char>(input[i + j]) & 0xc0) == 0x80;
        if (valid && length >= 3) {
            const auto second = static_cast<unsigned char>(input[i + 1]);
            valid = !(c == 0xe0 && second < 0xa0) && !(c == 0xed && second >= 0xa0) &&
                !(c == 0xf0 && second < 0x90) && !(c == 0xf4 && second >= 0x90);
        }
        if (!valid) {
            if (result.size() + 3 > 96) break;
            result += "\xef\xbf\xbd"; ++i; continue;
        }
        if (result.size() + length > 96) break;
        result.append(input.substr(i, length)); i += length;
    }
    if (result.empty()) result = "Player";
    return result;
}

std::uint32_t read32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
        static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
void append32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<unsigned char>((value >> shift) & 255));
}
}

struct DiscordPresence::Impl {
    struct Snapshot {
        bool enabled{};
        std::string id, username{"Player"};
        std::uint64_t rolls{};
    };
    mutable std::mutex mutex;
    std::condition_variable wake;
    Snapshot desired;
    DiscordPresenceState state{DiscordPresenceState::Disabled};
    std::string message{"Disabled"};
    std::wstring overridePath;
    std::thread worker;
    bool stopping{};
    HANDLE stopEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE pipe{INVALID_HANDLE_VALUE};
    bool ready{}, published{};
    DWORD connectionError{};
    std::string connectedId, lastUsername, pendingNonce;
    std::uint64_t lastRolls{}, nonce{};
    std::int64_t started{std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()};
    std::vector<unsigned char> incoming;
    Clock::time_point retryAt{}, sendAt{}, handshakeAt{}, ackAt{};

    explicit Impl(std::wstring path) : overridePath(std::move(path)) {
        if (!overridePath.empty() && overridePath.rfind(L"\\\\.\\pipe\\", 0) != 0 &&
            overridePath.rfind(L"\\\\?\\pipe\\", 0) != 0)
            throw std::invalid_argument("Discord test endpoint must be a local named pipe");
    }
    ~Impl() {
        { std::lock_guard lock(mutex); stopping = true; }
        if (stopEvent) SetEvent(stopEvent);
        wake.notify_one();
        if (worker.joinable()) worker.join();
        if (stopEvent) CloseHandle(stopEvent);
    }
    void setStatus(DiscordPresenceState value, std::string text) {
        std::lock_guard lock(mutex); state = value; message = std::move(text);
    }
    Snapshot snapshot() {
        std::lock_guard lock(mutex); return desired;
    }
    bool stopped() {
        std::lock_guard lock(mutex); return stopping;
    }

    // This bounded overlapped I/O is only used on the worker. The stop event
    // cancels pending I/O immediately; normal timeout also cancels before return.
    bool transfer(bool writing, void* data, DWORD size, DWORD& transferred, bool clearing = false) {
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent) return false;
        const BOOL success = writing ? WriteFile(pipe, data, size, &transferred, &operation) :
            ReadFile(pipe, data, size, &transferred, &operation);
        bool complete = success != FALSE;
        if (!success && GetLastError() == ERROR_IO_PENDING) {
            std::array<HANDLE, 2> events{operation.hEvent, stopEvent};
            const auto wait = WaitForMultipleObjects(clearing || !stopEvent ? 1 : 2,
                events.data(), FALSE, 200);
            if (wait == WAIT_OBJECT_0) complete = GetOverlappedResult(pipe, &operation, &transferred, FALSE) != FALSE;
            else {
                CancelIoEx(pipe, &operation);
                // Complete cancellation before the OVERLAPPED stack storage goes away.
                GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            }
        }
        CloseHandle(operation.hEvent);
        return complete;
    }
    bool send(std::uint32_t opcode, std::string_view payload, bool clearing = false) {
        if (pipe == INVALID_HANDLE_VALUE || payload.size() > MaxFrameBytes) return false;
        std::vector<unsigned char> bytes;
        bytes.reserve(8 + payload.size());
        append32(bytes, opcode); append32(bytes, static_cast<std::uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        DWORD transferred{};
        return transfer(true, bytes.data(), static_cast<DWORD>(bytes.size()), transferred, clearing) &&
            transferred == bytes.size();
    }
    std::string activity(const Snapshot* data) {
        Json value = nullptr;
        if (data) value = {{"type", 0}, {"details", "Username: " + data->username},
            {"state", "Rolls: " + std::to_string(data->rolls)}, {"timestamps", {{"start", started}}}};
        pendingNonce = std::to_string(++nonce);
        return Json{{"cmd", "SET_ACTIVITY"}, {"args", {{"pid", GetCurrentProcessId()}, {"activity", value}}},
            {"nonce", pendingNonce}}.dump();
    }
    void disconnect(bool clear) {
        if (pipe != INVALID_HANDLE_VALUE) {
            if (clear && ready && published) send(1, activity(nullptr), true);
            CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE;
        }
        ready = published = false; incoming.clear(); connectedId.clear(); pendingNonce.clear();
    }
    bool connect(const Snapshot& data) {
        connectionError = ERROR_SUCCESS;
        for (int index = 0; index < (overridePath.empty() ? 10 : 1); ++index) {
            const auto path = overridePath.empty() ? L"\\\\?\\pipe\\discord-ipc-" + std::to_wstring(index) : overridePath;
            pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) break;
            const auto error = GetLastError();
            if (connectionError == ERROR_SUCCESS || error != ERROR_FILE_NOT_FOUND) connectionError = error;
        }
        if (pipe == INVALID_HANDLE_VALUE) return false;
        connectedId = data.id;
        if (!send(0, Json{{"v", 1}, {"client_id", data.id}}.dump())) {
            disconnect(false); return false;
        }
        handshakeAt = Clock::now();
        setStatus(DiscordPresenceState::Connecting, "Connecting to Discord...");
        return true;
    }
    bool receive() {
        DWORD available{};
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
        if (available) {
            if (incoming.size() + available > MaxFrameBytes + 8) return false;
            const auto previous = incoming.size(); incoming.resize(previous + available);
            DWORD transferred{};
            if (!transfer(false, incoming.data() + previous, available, transferred)) return false;
            incoming.resize(previous + transferred);
        }
        while (incoming.size() >= 8) {
            const auto opcode = read32(incoming.data());
            const auto length = read32(incoming.data() + 4);
            if (length > MaxFrameBytes) return false;
            if (incoming.size() < 8 + length) break;
            std::string payload(reinterpret_cast<const char*>(incoming.data() + 8), length);
            incoming.erase(incoming.begin(), incoming.begin() + 8 + length);
            if (opcode == 2) return false;
            if (opcode == 3) { if (!send(4, payload)) return false; continue; }
            if (opcode == 4) continue;
            if (opcode != 1) return false;
            const auto json = Json::parse(payload, nullptr, false);
            if (!json.is_object()) return false;
            const auto event = json.find("evt");
            if (event != json.end() && *event == "ERROR") {
                std::string code;
                const auto error = json.find("data");
                if (error != json.end() && error->is_object() && error->contains("code") && (*error)["code"].is_number_integer())
                    code = " (" + (*error)["code"].dump() + ")";
                setStatus(DiscordPresenceState::Error, "Discord rejected the activity" + code + "; check Application ID");
                return false;
            }
            if (event != json.end() && *event == "READY") {
                ready = true; sendAt = Clock::time_point{};
                setStatus(DiscordPresenceState::Connecting, "Discord ready; sending activity...");
            }
            const auto replyNonce = json.find("nonce");
            const auto command = json.find("cmd");
            if (ready && !pendingNonce.empty() && command != json.end() && *command == "SET_ACTIVITY" &&
                replyNonce != json.end() && *replyNonce == pendingNonce) {
                pendingNonce.clear();
                setStatus(DiscordPresenceState::Connected, "Connected / activity updated");
            }
        }
        return true;
    }
    void run() noexcept {
        try {
            while (!stopped()) {
                const auto data = snapshot();
                const auto now = Clock::now();
                const bool usable = data.enabled && validId(data.id);
                if (!usable || (!connectedId.empty() && connectedId != data.id)) {
                    disconnect(true); retryAt = Clock::time_point{};
                    if (!data.enabled) setStatus(DiscordPresenceState::Disabled, "Disabled");
                    else if (data.id.empty()) setStatus(DiscordPresenceState::MissingApplicationId, "Application ID required");
                    else if (!validId(data.id)) setStatus(DiscordPresenceState::InvalidApplicationId, "Invalid Application ID (17-20 digits)");
                }
                if (usable) {
                    if (pipe == INVALID_HANDLE_VALUE && now >= retryAt) {
                        retryAt = now + RetryDelay;
                        if (!connect(data)) setStatus(DiscordPresenceState::WaitingForDiscord,
                            "Waiting for Discord desktop... (Windows " + std::to_string(connectionError) + ")");
                    }
                    if (pipe != INVALID_HANDLE_VALUE) {
                        if (!receive()) {
                            const bool wasError = currentState() == DiscordPresenceState::Error;
                            disconnect(false); retryAt = now + RetryDelay;
                            if (!wasError) setStatus(DiscordPresenceState::WaitingForDiscord, "Discord disconnected; retrying...");
                        } else if ((!ready && now - handshakeAt > RetryDelay) ||
                            (!pendingNonce.empty() && now - ackAt > RetryDelay * 2)) {
                            disconnect(false); retryAt = now + RetryDelay;
                            setStatus(DiscordPresenceState::Error, "Discord response timed out; retrying...");
                        } else if (ready && pendingNonce.empty() && now >= sendAt &&
                            (!published || lastUsername != data.username || lastRolls != data.rolls)) {
                            if (send(1, activity(&data))) {
                                published = true; lastUsername = data.username; lastRolls = data.rolls;
                                sendAt = now + UpdateDelay; ackAt = now;
                            } else {
                                disconnect(false); retryAt = now + RetryDelay;
                                setStatus(DiscordPresenceState::WaitingForDiscord, "Discord disconnected; retrying...");
                            }
                        }
                    }
                }
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(100));
            }
            disconnect(true);
        } catch (...) {
            disconnect(false);
            setStatus(DiscordPresenceState::Error, "Discord integration unavailable; restart to retry");
        }
    }
    DiscordPresenceState currentState() const {
        std::lock_guard lock(mutex); return state;
    }
};

DiscordPresence::DiscordPresence(std::wstring pipeOverride) : impl_(std::make_unique<Impl>(std::move(pipeOverride))) {}
DiscordPresence::~DiscordPresence() = default;
void DiscordPresence::update(bool enabled, const std::string& applicationId, const std::string& username,
    std::uint64_t rolls, float dt) {
    (void)dt;
    std::lock_guard lock(impl_->mutex);
    const auto name = safeUsername(username);
    const bool changed = impl_->desired.enabled != enabled || impl_->desired.id != applicationId ||
        impl_->desired.username != name || impl_->desired.rolls != rolls;
    impl_->desired = {enabled, applicationId, name, rolls};
    if (!impl_->worker.joinable()) {
        if (!enabled) { impl_->state = DiscordPresenceState::Disabled; impl_->message = "Disabled"; }
        else if (applicationId.empty()) {
            impl_->state = DiscordPresenceState::MissingApplicationId; impl_->message = "Application ID required";
        } else if (!validId(applicationId)) {
            impl_->state = DiscordPresenceState::InvalidApplicationId; impl_->message = "Invalid Application ID (17-20 digits)";
        } else if (impl_->stopEvent) {
            try { impl_->worker = std::thread([this] { impl_->run(); }); }
            catch (...) { impl_->state = DiscordPresenceState::Error; impl_->message = "Could not start Discord worker"; }
        } else { impl_->state = DiscordPresenceState::Error; impl_->message = "Could not start Discord IPC"; }
    }
    if (changed) impl_->wake.notify_one();
}
std::string DiscordPresence::status() const {
    std::lock_guard lock(impl_->mutex); return impl_->message;
}
DiscordPresenceState DiscordPresence::state() const { return impl_->currentState(); }
}
