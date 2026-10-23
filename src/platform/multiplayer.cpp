#include "platform/multiplayer.hpp"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cwctype>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace aura::net {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr std::size_t MaxPhoto = 128 * 1024, MaxResponse = 512 * 1024;
struct Internet {
    HINTERNET value{};
    explicit Internet(HINTERNET handle = nullptr) : value(handle) {}
    ~Internet() { if(value) WinHttpCloseHandle(value); }
    Internet(const Internet&) = delete;
};
std::wstring wide(const std::string& text) {
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(count<=0) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count);
    return result;
}
bool digits(const std::string& text) {
    return text.size()==10 && text[0]>='1' && text[0]<='9' &&
        std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';});
}
std::string hex(const unsigned char* data,std::size_t count) {
    constexpr char alphabet[]="0123456789abcdef";
    std::string result;result.reserve(count*2);
    for(std::size_t i=0;i<count;++i) {result.push_back(alphabet[data[i]>>4]);result.push_back(alphabet[data[i]&15]);}
    return result;
}
std::vector<unsigned char> unhex(const std::string& text) {
    if(text.empty() || text.size()%2 || text.size()>8192) throw std::runtime_error("Invalid identity file");
    const auto digit=[](char c)->unsigned {if(c>='0' && c<='9') return c-'0';if(c>='a' && c<='f') return c-'a'+10;throw std::runtime_error("Invalid identity file");};
    std::vector<unsigned char> result(text.size()/2);
    for(std::size_t i=0;i<result.size();++i) result[i]=static_cast<unsigned char>(digit(text[2*i])*16+digit(text[2*i+1]));
    return result;
}
std::string base64(const std::vector<std::uint8_t>& input) {
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;result.reserve((input.size()+2)/3*4);
    for(std::size_t i=0;i<input.size();i+=3) {
        unsigned value=static_cast<unsigned>(input[i])<<16;
        if(i+1<input.size()) value|=static_cast<unsigned>(input[i+1])<<8;
        if(i+2<input.size()) value|=input[i+2];
        result.push_back(alphabet[(value>>18)&63]);result.push_back(alphabet[(value>>12)&63]);
        result.push_back(i+1<input.size()?alphabet[(value>>6)&63]:'=');result.push_back(i+2<input.size()?alphabet[value&63]:'=');
    }
    return result;
}
bool safeText(const std::string& value) {
    if(value.empty() || value.size()>96 || std::all_of(value.begin(),value.end(),[](char c){return c==' ';})) return false;
    for(unsigned char c:value) if(c<32 || c==127) return false;
    try {wide(value);return true;} catch(...) {return false;}
}
bool safeHash(const std::string& value) {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});
}
struct Endpoint {std::wstring host;INTERNET_PORT port{};bool secure{};};
Endpoint parseEndpoint(const std::string& endpoint) {
    if(endpoint.size()>512) throw std::runtime_error("Invalid server address");
    const auto url=wide(endpoint);
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts)) throw std::runtime_error("Use https://your-server.example");
    Endpoint result{std::wstring(parts.lpszHostName,parts.dwHostNameLength),parts.nPort,parts.nScheme==INTERNET_SCHEME_HTTPS};
    std::transform(result.host.begin(),result.host.end(),result.host.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    const std::wstring path(parts.lpszUrlPath?parts.lpszUrlPath:L"",parts.dwUrlPathLength);
    const bool local=result.host==L"localhost" || result.host==L"127.0.0.1" || result.host==L"::1" || result.host==L"[::1]";
    if((!result.secure && !(local && parts.nScheme==INTERNET_SCHEME_HTTP)) || parts.dwUserNameLength || parts.dwPasswordLength ||
        parts.dwExtraInfoLength || (!path.empty() && path!=L"/") || result.host.empty())
        throw std::runtime_error("External servers require HTTPS; use a root address without credentials or a path");
    return result;
}
bool validPlayer(const LocalPlayer& p) {
    return std::isfinite(p.x) && std::abs(p.x)<=1000 && std::isfinite(p.z) && std::abs(p.z)<=1000 &&
        std::isfinite(p.yaw) && std::abs(p.yaw)<=1000 && validBiome(p.planet) && p.auraLevel<=3 &&
        !p.auraId.empty() && p.auraId.size()<=48 && p.auraId.front()>='a' && p.auraId.front()<='z' &&
        std::all_of(p.auraId.begin(),p.auraId.end(),[](char c){return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';}) &&
        std::isfinite(p.shirt.r) && p.shirt.r>=0 && p.shirt.r<=1 && std::isfinite(p.shirt.g) && p.shirt.g>=0 && p.shirt.g<=1 &&
        std::isfinite(p.shirt.b) && p.shirt.b>=0 && p.shirt.b<=1;
}
Json playerJson(const LocalPlayer& p) {
    return {{"x",p.x},{"z",p.z},{"yaw",p.yaw},{"walking",p.walking},{"planet",static_cast<unsigned>(p.planet)},
        {"aura_id",p.auraId},{"aura_level",p.auraLevel},{"shirt",{p.shirt.r,p.shirt.g,p.shirt.b}}};
}
LocalPlayer readPlayer(const Json& json) {
    if(!json.is_object() || json.size()!=8 || !json.at("walking").is_boolean() || !json.at("planet").is_number_unsigned() ||
        !json.at("aura_level").is_number_unsigned() || !json.at("shirt").is_array() || json.at("shirt").size()!=3)
        throw std::runtime_error("Invalid server player");
    LocalPlayer result;
    result.x=json.at("x").get<float>();result.z=json.at("z").get<float>();result.yaw=json.at("yaw").get<float>();
    result.walking=json.at("walking").get<bool>();result.planet=static_cast<Biome>(json.at("planet").get<unsigned>());
    result.auraId=json.at("aura_id").get<std::string>();result.auraLevel=json.at("aura_level").get<unsigned>();
    result.shirt={json.at("shirt")[0].get<float>(),json.at("shirt")[1].get<float>(),json.at("shirt")[2].get<float>()};
    if(!validPlayer(result)) throw std::runtime_error("Invalid server player");
    return result;
}
std::string errorMessage(const std::string& code) {
    if(code=="already_connected") return "This User ID is already connected. Disconnect the other game or wait 12 seconds.";
    if(code=="server_full") return "Server is full (32 players).";
    if(code=="protocol_mismatch") return "Server and game versions are incompatible.";
    if(code=="invalid_session") return "Connection expired. Press Connect to reconnect.";
    if(code=="photo_must_be_160px_png" || code=="invalid_photo" || code=="photo_too_large") return "Profile photo must be a 160 x 160 PNG below 128 KiB.";
    return "Server rejected the request ("+code.substr(0,64)+").";
}
}

struct Client::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping{},initialized{},wanted{};
    std::uint64_t generation{};
    std::string endpoint,username,userId,secret;
    std::vector<std::uint8_t> photo;
    std::filesystem::path identityPath;
    LocalPlayer local;
    Snapshot view;
    HINTERNET activeRequest{};
    std::thread worker;

    Impl():worker([this]{run();}) {}
    ~Impl() {
        {std::lock_guard lock(mutex);stopping=true;wanted=false;++generation;cancelLocked();}
        wake.notify_one();
        if(worker.joinable()) worker.join();
    }
    void cancelLocked() {
        // WinHTTPの要求ハンドルを閉じると、workerの待機中の通信もキャンセルされます。
        if(activeRequest) {WinHttpCloseHandle(activeRequest);activeRequest=nullptr;}
    }
    void saveIdentity() {
        DATA_BLOB plain{static_cast<DWORD>(secret.size()),reinterpret_cast<BYTE*>(secret.data())},protectedData{};
        if(!CryptProtectData(&plain,L"Star's RNG multiplayer identity",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&protectedData))
            throw std::runtime_error("Cannot protect local identity");
        const auto protectedHex=hex(protectedData.pbData,protectedData.cbData);LocalFree(protectedData.pbData);
        const auto temporary=identityPath.wstring()+L".tmp";
        {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
            if(!stream || !(stream<<Json{{"schema",1},{"user_id",userId},{"protected_secret",protectedHex}}.dump(2)<<'\n'))
                throw std::runtime_error("Cannot save local identity");}
        if(!MoveFileExW(temporary.c_str(),identityPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace local identity");
    }
    void generateIdentity() {
        std::array<unsigned char,40> bytes{};
        if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
            throw std::runtime_error("Cannot generate secure identity");
        std::uint64_t value{};for(unsigned i=0;i<8;++i) value=(value<<8)|bytes[i];
        // 公開IDは必ず10桁。衝突はjoin時に検出し、新しいIDを保存して再試行します。
        userId=std::to_string(1'000'000'000ULL+value%9'000'000'000ULL);
        secret=hex(bytes.data()+8,32);saveIdentity();view.userId=userId;
    }
    bool initialize(const std::filesystem::path& directory) {
        std::lock_guard lock(mutex);
        if(initialized) return true;
        try {
            std::filesystem::create_directories(directory);identityPath=directory/L"multiplayer_identity.json";
            if(std::filesystem::exists(identityPath)) {
                if(std::filesystem::file_size(identityPath)>16384) throw std::runtime_error("Invalid local identity file");
                std::ifstream stream(identityPath,std::ios::binary);const auto json=Json::parse(stream);
                userId=json.at("user_id").get<std::string>();
                if(json.at("schema")!=1 || !digits(userId)) throw std::runtime_error("Invalid local identity file");
                auto bytes=unhex(json.at("protected_secret").get<std::string>());
                DATA_BLOB protectedData{static_cast<DWORD>(bytes.size()),bytes.data()},plain{};
                if(!CryptUnprotectData(&protectedData,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain))
                    throw std::runtime_error("Identity belongs to another Windows account. Restore the original account or remove multiplayer_identity.json.");
                secret.assign(reinterpret_cast<char*>(plain.pbData),plain.cbData);SecureZeroMemory(plain.pbData,plain.cbData);LocalFree(plain.pbData);
                if(!safeHash(secret)) throw std::runtime_error("Invalid local identity file");
            } else generateIdentity();
            initialized=true;view.userId=userId;view.status="Offline";return true;
        } catch(const Json::exception&) {view.state=State::Error;view.status="Invalid local multiplayer identity file.";return false;
        } catch(const std::exception& e) {view.state=State::Error;view.status=e.what();return false;}
    }
    struct Response {DWORD status{};std::vector<std::uint8_t> body;};
    Response request(HINTERNET connection,const Endpoint& server,const wchar_t* method,const std::wstring& path,
        const std::string& data,const std::string& token,std::uint64_t command) {
        HINTERNET handle=WinHttpOpenRequest(connection,method,path.c_str(),nullptr,WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,server.secure?WINHTTP_FLAG_SECURE:0);
        if(!handle) throw std::runtime_error("Cannot create a network request");
        {std::lock_guard lock(mutex);if(stopping || command!=generation) {WinHttpCloseHandle(handle);throw std::runtime_error("Cancelled");} activeRequest=handle;}
        const auto release=[&] {std::lock_guard lock(mutex);if(activeRequest==handle) {activeRequest=nullptr;WinHttpCloseHandle(handle);}};
        try {
            DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            DWORD disabled=WINHTTP_DISABLE_COOKIES;
            DWORD logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
            if(!WinHttpSetOption(handle,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)) ||
                !WinHttpSetOption(handle,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) ||
                !WinHttpSetOption(handle,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof(logon)))
                throw std::runtime_error("Cannot configure secure network request");
            std::wstring headers=L"Content-Type: application/json\r\n";
            if(!token.empty()) headers+=L"Authorization: Bearer "+wide(token)+L"\r\n";
            if(!WinHttpSendRequest(handle,headers.c_str(),static_cast<DWORD>(headers.size()),data.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(data.data()),
                static_cast<DWORD>(data.size()),static_cast<DWORD>(data.size()),0) || !WinHttpReceiveResponse(handle,nullptr))
                throw std::runtime_error("Cannot reach server. Check its address, HTTPS certificate and firewall.");
            Response response;DWORD size=sizeof(response.status);
            if(!WinHttpQueryHeaders(handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&response.status,&size,WINHTTP_NO_HEADER_INDEX))
                throw std::runtime_error("Invalid server response");
            std::array<std::uint8_t,8192> buffer{};
            for(;;) {DWORD read{};if(!WinHttpReadData(handle,buffer.data(),static_cast<DWORD>(buffer.size()),&read)) throw std::runtime_error("Network response interrupted");
                if(!read) break;if(response.body.size()+read>MaxResponse) throw std::runtime_error("Server response is too large");
                response.body.insert(response.body.end(),buffer.begin(),buffer.begin()+read);}
            release();return response;
        } catch(...) {release();throw;}
    }
    static Json jsonResponse(const Response& response) {
        auto json=Json::parse(response.body.begin(),response.body.end());
        if(response.status!=200) throw std::runtime_error(errorMessage(json.value("error",std::string("unknown"))));
        return json;
    }
    void run() {
        std::uint64_t handled{};
        for(;;) {
            std::string address,name,id,key;std::vector<std::uint8_t> avatar;std::uint64_t command{};
            {std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping || (wanted && generation!=handled);});if(stopping) return;
                command=generation;handled=command;address=endpoint;name=username;id=userId;key=secret;avatar=photo;}
            std::string token;
            try {
                const auto server=parseEndpoint(address);
                Internet session(WinHttpOpen(L"StarsRng/0.1 multiplayer",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
                if(!session.value) throw std::runtime_error("Cannot initialize WinHTTP");
                WinHttpSetTimeouts(session.value,1000,1000,1000,1000);
                Internet connection(WinHttpConnect(session.value,server.host.c_str(),server.port,0));
                if(!connection.value) throw std::runtime_error("Cannot initialize server connection");
                for(unsigned attempt=0;attempt<3;++attempt) {
                    const auto joined=request(connection.value,server,L"POST",L"/v1/join",Json{{"protocol",1},{"user_id",id},{"secret",key},
                        {"username",name},{"avatar_png",base64(avatar)}}.dump(),{},command);
                    const auto json=Json::parse(joined.body.begin(),joined.body.end());
                    if(joined.status==409 && json.value("error",std::string())=="identity_conflict") {
                        std::lock_guard lock(mutex);if(command!=generation) throw std::runtime_error("Cancelled");generateIdentity();id=userId;key=secret;continue;
                    }
                    const auto accepted=jsonResponse(joined);
                    token=accepted.at("token").get<std::string>();
                    if(!safeHash(token) || accepted.at("protocol")!=1 || accepted.at("user_id")!=id) throw std::runtime_error("Invalid server session");
                    break;
                }
                if(token.empty()) throw std::runtime_error("Could not reserve a User ID. Try Connect again.");
                std::map<std::string,std::vector<std::uint8_t>> photos;
                auto next=Clock::now();
                std::exception_ptr updateFailure;
                try { for(;;) {
                    LocalPlayer player;
                    {std::lock_guard lock(mutex);if(stopping || command!=generation || !wanted) break;player=local;}
                    const auto updated=jsonResponse(request(connection.value,server,L"POST",L"/v1/update",Json{{"player",playerJson(player)}}.dump(),token,command));
                    if(updated.at("protocol")!=1 || !updated.at("players").is_array() || updated.at("players").size()>31)
                        throw std::runtime_error("Invalid player list");
                    std::vector<RemotePlayer> players;std::map<std::string,bool> retained;
                    for(const auto& item:updated.at("players")) {
                        RemotePlayer remote;remote.userId=item.at("user_id").get<std::string>();remote.username=item.at("username").get<std::string>();
                        remote.avatarHash=item.at("avatar_hash").get<std::string>();remote.player=readPlayer(item.at("player"));
                        if(!digits(remote.userId) || remote.userId==id || !safeText(remote.username) || (!remote.avatarHash.empty() && !safeHash(remote.avatarHash)))
                            throw std::runtime_error("Invalid remote identity");
                        if(!remote.avatarHash.empty()) {
                            retained[remote.avatarHash]=true;
                            if(!photos.count(remote.avatarHash)) {
                                const auto image=request(connection.value,server,L"GET",L"/v1/photo/"+wide(remote.userId)+L"/"+wide(remote.avatarHash),{},token,command);
                                // 相手が取得直前に退出した場合は既定アイコンのままにします。
                                if(image.status==200 && image.body.size()<=MaxPhoto) {
                                    std::array<unsigned char,32> digest{};
                                    BCRYPT_ALG_HANDLE algorithm{};
                                    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0) {
                                        const auto status=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(image.body.data()),static_cast<ULONG>(image.body.size()),digest.data(),static_cast<ULONG>(digest.size()));
                                        BCryptCloseAlgorithmProvider(algorithm,0);
                                        if(status==0 && hex(digest.data(),digest.size())==remote.avatarHash) photos[remote.avatarHash]=image.body;
                                    }
                                }
                            }
                            const auto found=photos.find(remote.avatarHash);if(found!=photos.end()) remote.avatarPng=found->second;
                        }
                        players.push_back(std::move(remote));
                    }
                    for(auto it=photos.begin();it!=photos.end();) {if(!retained.count(it->first)) it=photos.erase(it);else ++it;}
                    {std::lock_guard lock(mutex);if(command!=generation || !wanted) break;view.state=State::Connected;view.status="Connected";view.players=std::move(players);}
                    next=std::max(next+std::chrono::milliseconds(100),Clock::now()+std::chrono::milliseconds(40));
                    std::unique_lock lock(mutex);wake.wait_until(lock,next,[&]{return stopping || command!=generation || !wanted;});
                    if(stopping || command!=generation || !wanted) break;
                }} catch(...) {updateFailure=std::current_exception();}
                // 切断処理もworkerで行うため、UIはネットワーク応答を待ちません。
                std::uint64_t leaveCommand;{std::lock_guard lock(mutex);leaveCommand=generation;}
                if(!token.empty()) {try {request(connection.value,server,L"POST",L"/v1/leave","{}",token,leaveCommand);} catch(...) {}}
                if(updateFailure) std::rethrow_exception(updateFailure);
            } catch(const Json::exception&) {
                std::lock_guard lock(mutex);
                if(command==generation && wanted && !stopping) {view.state=State::Error;view.status="Invalid server response.";view.players.clear();wanted=false;}
            } catch(const std::exception& error) {
                std::lock_guard lock(mutex);
                if(command==generation && wanted && !stopping) {view.state=State::Error;view.status=error.what();view.players.clear();wanted=false;}
            }
            SecureZeroMemory(key.data(),key.size());
        }
    }
};

Client::Client():impl_(std::make_unique<Impl>()) {}
Client::~Client() = default;
bool Client::initialize(const std::filesystem::path& directory) {return impl_->initialize(directory);}
void Client::connect(const std::string& endpoint,const std::string& username,const std::vector<std::uint8_t>& avatarPng) {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->initialized) {impl_->view.state=State::Error;impl_->view.status="Initialize local multiplayer identity first.";return;}
    try {parseEndpoint(endpoint);if(!safeText(username)) throw std::runtime_error("Choose a UserName (1 to 96 UTF-8 bytes).");
        if(avatarPng.size()>MaxPhoto) throw std::runtime_error("Profile photo exceeds 128 KiB.");
        impl_->endpoint=endpoint;impl_->username=username;impl_->photo=avatarPng;impl_->wanted=true;++impl_->generation;impl_->cancelLocked();
        impl_->view.state=State::Connecting;impl_->view.status="Connecting...";impl_->view.players.clear();impl_->wake.notify_one();
    } catch(const std::exception& error) {impl_->view.state=State::Error;impl_->view.status=error.what();}
}
void Client::disconnect() {
    std::lock_guard lock(impl_->mutex);impl_->wanted=false;++impl_->generation;impl_->cancelLocked();
    impl_->view.state=State::Offline;impl_->view.status="Offline";impl_->view.players.clear();impl_->wake.notify_one();
}
void Client::submit(const LocalPlayer& player) {if(validPlayer(player)) {std::lock_guard lock(impl_->mutex);impl_->local=player;}}
Snapshot Client::snapshot() const {std::lock_guard lock(impl_->mutex);return impl_->view;}
}
