#pragma once
#include "core/aura.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace aura::net {
// 通信するのは「見た目」と現在位置だけ。ロール・所持品・LuckはPC内に保存します。
struct LocalPlayer {
    float x{}, z{}, yaw{};
    bool walking{};
    Biome planet{Biome::Normal};
    std::string auraId{"common"};
    unsigned auraLevel{};
    Color shirt{.54f,.83f,.89f};
};
struct RemotePlayer {
    std::string userId, username, avatarHash;
    LocalPlayer player;
    // 160x160 PNG。最初の取得後はメモリ内で再利用し、毎秒送り直しません。
    std::vector<std::uint8_t> avatarPng;
};
enum class State { Offline, Connecting, Connected, Error };
struct Snapshot {
    State state{State::Offline};
    std::string status{"Offline"}, userId;
    // サーバーは全員を返します。描画側で自分と同じplanetだけ表示してください。
    std::vector<RemotePlayer> players;
};

class Client {
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    // 10桁の公開IDと秘密鍵をPC内に初回生成。秘密鍵をUI/ログに表示しないでください。
    bool initialize(const std::filesystem::path& saveDirectory);
    // 空PNG=既定のプロフィール。外部サーバーはhttps://、HTTPはlocalhostだけ。
    // 呼び出しは即時に戻り、DNS/通信/画像取得は専用スレッドで行います。
    void connect(const std::string& endpoint, const std::string& username,
        const std::vector<std::uint8_t>& avatarPng = {});
    void disconnect();
    void submit(const LocalPlayer& player);
    Snapshot snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
