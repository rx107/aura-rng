#include "platform/multiplayer.hpp"
#include "platform/profile_photo.hpp"
#include <windows.h>
#include <objbase.h>
#include <wincrypt.h>
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using aura::net::Client;
using aura::net::Snapshot;
using aura::net::State;
using Json=nlohmann::json;
void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
template<class Predicate> bool eventually(Predicate&& predicate,std::chrono::milliseconds timeout=5000ms) {
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    do {if(predicate())return true;std::this_thread::sleep_for(20ms);} while(std::chrono::steady_clock::now()<deadline);
    return false;
}
bool validId(const std::string& id) {
    return id.size()==10 && id[0]>='1' && id[0]<='9' &&
        std::all_of(id.begin(),id.end(),[](char c){return c>='0' && c<='9';});
}
std::uint32_t big32(const std::uint8_t* data) {
    return std::uint32_t(data[0])<<24|std::uint32_t(data[1])<<16|std::uint32_t(data[2])<<8|data[3];
}
void little16(std::ofstream& stream,unsigned value) {stream.put(static_cast<char>(value));stream.put(static_cast<char>(value>>8));}
void little32(std::ofstream& stream,unsigned value) {little16(stream,value);little16(stream,value>>16);}
void fixtureBmp(const std::filesystem::path& path,unsigned variation) {
    // 横長の元写真を模したBMP。ゲームのWIC処理で正方形へ切り取り・縮小します。
    constexpr unsigned width=320,height=180,stride=width*3;
    std::ofstream stream(path,std::ios::binary);stream.write("BM",2);little32(stream,54+stride*height);
    little32(stream,0);little32(stream,54);little32(stream,40);little32(stream,width);little32(stream,height);
    little16(stream,1);little16(stream,24);little32(stream,0);little32(stream,stride*height);
    little32(stream,2835);little32(stream,2835);little32(stream,0);little32(stream,0);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        stream.put(static_cast<char>((x+variation*47)%256));stream.put(static_cast<char>((y+variation*91)%256));
        stream.put(static_cast<char>((x/2+y+variation*23)%256));
    }
    check(static_cast<bool>(stream),"Write photo fixture");
}
void checkPhoto(const std::vector<std::uint8_t>& data) {
    check(data.size()>32 && data.size()<=128*1024,"Receive bounded profile PNG");
    check(data[0]==137 && data[1]=='P' && data[2]=='N' && data[3]=='G',"Receive PNG signature");
    check(big32(data.data()+16)==160 && big32(data.data()+20)==160,"Receive 160px normalized photo");
}
void identityProtected(const std::filesystem::path& directory,const std::string& id) {
    const auto path=directory/L"multiplayer_identity.json";
    std::ifstream input(path,std::ios::binary);const std::string raw((std::istreambuf_iterator<char>(input)),{});
    const auto json=Json::parse(raw);
    check(json.size()==3 && json.at("schema")==1 && json.at("user_id")==id && !json.contains("secret"),"Persist only public ID and protected secret");
    const auto encoded=json.at("protected_secret").get<std::string>();
    check(encoded.size()>128 && encoded.size()%2==0,"Persist encrypted secret blob");
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    std::vector<BYTE> bytes(encoded.size()/2);
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<BYTE>(digit(encoded[2*i])*16+digit(encoded[2*i+1]));
    DATA_BLOB protectedData{static_cast<DWORD>(bytes.size()),bytes.data()},plain{};
    check(CryptUnprotectData(&protectedData,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain)!=FALSE,"Decrypt identity in same Windows account");
    const std::string secret(reinterpret_cast<const char*>(plain.pbData),plain.cbData);
    SecureZeroMemory(plain.pbData,plain.cbData);LocalFree(plain.pbData);
    check(secret.size()==64 && raw.find(secret)==std::string::npos,"Do not persist plaintext secret");
}
}

int main(int argc,char** argv) {
    if(argc!=3) {std::cerr<<"Usage: multiplayer_client_tests endpoint scratch-directory\n";return 2;}
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com)) {std::cerr<<"COM initialization failed\n";return 1;}
    try {
        const std::string endpoint=argv[1];const std::filesystem::path scratch=argv[2];
        std::filesystem::create_directories(scratch);
        const auto firstDir=scratch/L"first",secondDir=scratch/L"second";
        fixtureBmp(scratch/L"first-source.bmp",1);fixtureBmp(scratch/L"second-source.bmp",2);
        const auto firstPhoto=aura::prepareProfilePhoto(scratch/L"first-source.bmp");
        const auto secondPhoto=aura::prepareProfilePhoto(scratch/L"second-source.bmp");
        checkPhoto(firstPhoto);checkPhoto(secondPhoto);
        Client first,second;
        check(first.initialize(firstDir) && second.initialize(secondDir),"Initialize two isolated identities");
        const auto firstId=first.snapshot().userId,secondId=second.snapshot().userId;
        check(validId(firstId) && validId(secondId) && firstId!=secondId,"Generate distinct random 10 digit IDs");
        identityProtected(firstDir,firstId);identityProtected(secondDir,secondId);
        {Client restored;check(restored.initialize(firstDir) && restored.snapshot().userId==firstId,"Restore persistent ID");}
        aura::net::LocalPlayer firstPlayer,secondPlayer;
        firstPlayer.auraId="equinox";firstPlayer.auraLevel=2;firstPlayer.shirt={.1f,.7f,.9f};firstPlayer.x=12.5f;
        secondPlayer.auraId="frutiger_aero";secondPlayer.planet=aura::Biome::Extra;secondPlayer.z=-8.25f;
        first.submit(firstPlayer);second.submit(secondPlayer);
        first.connect(endpoint,"星 Player一",firstPhoto);second.connect(endpoint,"海 Player二",secondPhoto);
        check(eventually([&]{return first.snapshot().state==State::Connected && second.snapshot().state==State::Connected;}),"Connect two WinHTTP clients");
        check(eventually([&]{return first.snapshot().players.size()==1 && second.snapshot().players.size()==1 &&
            !first.snapshot().players[0].avatarPng.empty() && !second.snapshot().players[0].avatarPng.empty();}),"Exchange snapshots and normalized photos");
        const auto remoteFirst=second.snapshot().players.front(),remoteSecond=first.snapshot().players.front();
        check(remoteFirst.userId==firstId && remoteSecond.userId==secondId,"Remote identity matches peer");
        check(remoteFirst.username=="星 Player一" && remoteSecond.username=="海 Player二","Transmit UTF-8 names");
        check(remoteFirst.player.auraId=="equinox" && remoteFirst.player.auraLevel==2,"Transmit aura ID and level");
        check(std::abs(remoteFirst.player.shirt.g-.7f)<.001f,"Transmit shirt color");
        check(remoteSecond.player.planet==aura::Biome::Extra,"Transmit planet independently of renderer filtering");
        checkPhoto(remoteFirst.avatarPng);checkPhoto(remoteSecond.avatarPng);
        check(remoteFirst.avatarHash!=remoteSecond.avatarHash,"Keep distinct profile photos");
        firstPlayer.x=-29.75f;firstPlayer.z=3.25f;firstPlayer.yaw=2.5f;firstPlayer.walking=true;
        firstPlayer.planet=aura::Biome::Desert;firstPlayer.auraId="pharaoh";firstPlayer.auraLevel=3;firstPlayer.shirt={1,.4f,.05f};
        const auto begin=std::chrono::steady_clock::now();
        for(unsigned i=0;i<1000;++i)first.submit(firstPlayer);
        check(std::chrono::steady_clock::now()-begin<250ms,"Submit does not wait for network");
        check(eventually([&]{const auto snapshot=second.snapshot();return snapshot.players.size()==1 &&
            std::abs(snapshot.players[0].player.x+29.75f)<.001f && snapshot.players[0].player.planet==aura::Biome::Desert &&
            snapshot.players[0].player.auraId=="pharaoh" && snapshot.players[0].player.walking;}),"Update movement planet aura and walking");
        first.connect(endpoint,"改名した Player",firstPhoto);
        check(eventually([&]{const auto snapshot=second.snapshot();return first.snapshot().state==State::Connected && snapshot.players.size()==1 &&
            snapshot.players[0].username=="改名した Player" && snapshot.players[0].userId==firstId;}),"Reconnect with new name and stable identity");
        const auto disconnectBegin=std::chrono::steady_clock::now();first.disconnect();
        check(std::chrono::steady_clock::now()-disconnectBegin<250ms,"Disconnect does not wait for server response");
        check(first.snapshot().state==State::Offline,"Immediate local offline state");
        check(eventually([&]{return second.snapshot().players.empty();}),"Peer sees explicit disconnect");
        const auto collisionDir=scratch/L"collision";
        {Client generated;check(generated.initialize(collisionDir),"Generate unrelated collision credential");}
        {std::ifstream input(collisionDir/L"multiplayer_identity.json",std::ios::binary);auto identity=Json::parse(input);input.close();
            identity["user_id"]=firstId;std::ofstream output(collisionDir/L"multiplayer_identity.json",std::ios::binary|std::ios::trunc);output<<identity.dump();}
        {Client collision;check(collision.initialize(collisionDir),"Load simulated random ID collision");
            collision.connect(endpoint,"Collision");
            check(eventually([&]{return collision.snapshot().state==State::Connected;}),"Regenerate ID after reservation collision");
            const auto newId=collision.snapshot().userId;
            check(validId(newId) && newId!=firstId && newId!=secondId,"Use new distinct ID after collision");
            identityProtected(collisionDir,newId);collision.disconnect();
            check(eventually([&]{return second.snapshot().players.empty();}),"Remove collision test client after disconnect");}
        {Client duplicate;check(duplicate.initialize(secondDir),"Load duplicate identity");duplicate.connect(endpoint,"Duplicate");
            check(eventually([&]{return duplicate.snapshot().state==State::Error;}),"Reject concurrent use of same identity");
            check(duplicate.snapshot().userId==secondId,"Do not regenerate identity for duplicate login");}
        second.disconnect();
        {Client invalid;check(invalid.initialize(scratch/L"invalid"),"Initialize error case");
            invalid.connect("http://192.0.2.1:8765","Unsafe HTTP");
            check(invalid.snapshot().state==State::Error,"Reject unencrypted external HTTP without network access");
            invalid.connect("https://user:secret@example.invalid","Credentials");
            check(invalid.snapshot().state==State::Error,"Reject credentials in endpoint");
            invalid.connect("http://127.0.0.1:1","Offline");
            check(eventually([&]{return invalid.snapshot().state==State::Error;}),"Offline server reports error without blocking caller");}
        std::cout<<"WinHTTP multiplayer integration passed: identities, 2 clients, UTF-8, photos, movement, reconnect, disconnect, collision, validation.\n";
        CoUninitialize();return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Multiplayer client integration failed: "<<error.what()<<'\n';CoUninitialize();return 1;
    }
}
