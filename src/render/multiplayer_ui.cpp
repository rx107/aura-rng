#include "render/renderer.hpp"
#include "imgui.h"
#include <wincodec.h>
#include <shobjidl.h>
#include <cstring>
#include <functional>
#include <optional>

namespace aura {
namespace {
std::optional<std::string> choosePhoto(HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return {};
    const COMDLG_FILTERSPEC filter{L"Profile photo (*.png;*.jpg;*.jpeg;*.bmp)",L"*.png;*.jpg;*.jpeg;*.bmp"};
    dialog->SetFileTypes(1,&filter);dialog->SetTitle(L"Choose profile photo / プロフィール写真を選択");
    dialog->SetOptions(FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
    if(FAILED(dialog->Show(owner)))return {};
    Microsoft::WRL::ComPtr<IShellItem> item;if(FAILED(dialog->GetResult(&item)))return {};
    PWSTR path{};if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)))return {};
    const auto u=std::filesystem::path(path).u8string();CoTaskMemFree(path);
    return std::string(reinterpret_cast<const char*>(u.data()),u.size());
}
}
// 通信から届いた小さなPNGだけをGPUへ読みます。写真を毎フレーム復号しません。
ID3D11ShaderResourceView* Renderer::photoTexture(const std::string& key,std::span<const std::uint8_t> bytes) {
    if(bytes.empty() || bytes.size()>128*1024)return nullptr;
    const auto cached=std::find_if(photoTextures_.begin(),photoTextures_.end(),[&](const auto& p){return p.key==key;});
    if(cached!=photoTextures_.end())return cached->texture.Get();
    ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return nullptr;
    ComPtr<IWICStream> stream;if(FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()))))return nullptr;
    ComPtr<IWICBitmapDecoder> decoder;if(FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder)))return nullptr;
    ComPtr<IWICBitmapFrameDecode> frame;if(FAILED(decoder->GetFrame(0,&frame)))return nullptr;
    UINT width{},height{};if(FAILED(frame->GetSize(&width,&height)) || width!=160 || height!=160)return nullptr;
    ComPtr<IWICFormatConverter> converter;if(FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return nullptr;
    std::vector<BYTE> pixels(width*height*4);if(FAILED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data())))return nullptr;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};ComPtr<ID3D11Texture2D> texture;
    if(FAILED(device_->CreateTexture2D(&desc,&data,&texture)))return nullptr;
    PhotoTexture photo;photo.key=key;
    if(FAILED(device_->CreateShaderResourceView(texture.Get(),nullptr,&photo.texture)))return nullptr;
    photoTextures_.push_back(std::move(photo));return photoTextures_.back().texture.Get();
}
void Renderer::multiplayerUi(const Frame& f) {
    multiplayerPanelVisible_=f.multiplayerOpen;
    if(!multiplayerPanelVisible_)return;
    if(photoTextures_.size()>64)photoTextures_.clear(); // 前フレームが描き終わってから古い画像を解放。
    auto t=[&](const char* en,const char* ja){return tr(f.presentation.settings.language,en,ja);};
    bool open=true;
    const auto size=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(600.f,size.x-40),std::min(700.f,size.y-48)},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({size.x*.5f,size.y*.5f},ImGuiCond_FirstUseEver,{.5f,.5f});
    const bool visible=ImGui::Begin(t("Multiplayer / Profile###Multiplayer","マルチプレイ / プロフィール###Multiplayer"),&open);
    const auto pos=ImGui::GetWindowPos(),bounds=ImGui::GetWindowSize();multiplayerBounds_={pos.x,pos.y,bounds.x,bounds.y};
    if(visible) {
        const auto& profile=f.presentation.settings;
        static std::array<char,65> name{};static std::string nameSource;
        static std::array<char,2049> endpoint{};static std::string endpointSource;
        if(nameSource!=profile.username) {name.fill(0);std::copy_n(profile.username.begin(),std::min(profile.username.size(),name.size()-1),name.begin());nameSource=profile.username;}
        if(endpointSource!=profile.serverEndpoint) {endpoint.fill(0);std::copy_n(profile.serverEndpoint.begin(),std::min(profile.serverEndpoint.size(),endpoint.size()-1),endpoint.begin());endpointSource=profile.serverEndpoint;}
        ImGui::TextColored({.55f,.96f,.82f,1},"STAR'S RNG / ONLINE");
        // この画面の開閉と、サーバーへの接続は別の状態です。
        // closeMultiplayer は画面だけを閉じる命令。切断は下の Disconnect が担当します。
        // 上側に置くと、小さな画面でもスクロールせずゲームへ戻れます。
        if(ImGui::Button(t("Back to game","ゲームに戻る"),{-1,36}))debugActions_.closeMultiplayer=true;
        ImGui::TextWrapped("%s",t("Esc also closes this profile. Your connection stays active. Joining successfully returns you to the game automatically.",
            "Escでもこの画面を閉じられます。接続はそのまま続き、参加に成功すると自動でゲームへ戻ります。"));
        ImGui::Separator();
        if(!profile.usernameConfigured)ImGui::TextWrapped("%s",t("Set your username before your first multiplayer session.","はじめて参加する前に、ユーザー名を入力して確定してください。"));
        ImGui::InputText(t("Username##mp","ユーザー名##mp"),name.data(),name.size());
        const bool valid=validUsername(name.data());ImGui::BeginDisabled(!valid);
        if(ImGui::Button(t("Save username","ユーザー名を確定・保存")))debugActions_.username=std::string(name.data());
        ImGui::EndDisabled();
        if(f.multiplayer)ImGui::Text("User ID: %s",f.multiplayer->userId.empty()?"---":f.multiplayer->userId.c_str());
        ImGui::TextDisabled("%s",t("10 random digits, independent of your username.","名前とは別のランダムな10桁。名前を変えてもIDは維持します。"));
        if(!f.profilePhoto.empty()) {
            const auto key="local-"+std::to_string(std::hash<std::string_view>{}({reinterpret_cast<const char*>(f.profilePhoto.data()),f.profilePhoto.size()}));
            if(auto* texture=photoTexture(key,f.profilePhoto))ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)),{72,72});
        } else {ImGui::TextColored({.3f,.8f,1,1},"[ * ]");ImGui::TextDisabled("%s",t("Default profile icon","標準のプロフィールアイコン"));}
        if(ImGui::Button(t("Choose profile photo...","プロフィール写真を選択...")))if(auto path=choosePhoto(window_))debugActions_.profilePhotoPath=*path;
        ImGui::SameLine();if(ImGui::Button(t("Reset photo","写真をリセット")))debugActions_.clearProfilePhoto=true;
        ImGui::TextWrapped("%s",t("The center square becomes a 160 px PNG and is shared with players on the server you join.","写真の中央を160pxの正方形にして、参加したサーバーのプレイヤーへ表示します。"));
        ImGui::Separator();
        ImGui::InputText(t("Server URL","サーバーURL"),endpoint.data(),endpoint.size());
        const auto state=f.multiplayer?f.multiplayer->state:net::State::Offline;
        ImGui::BeginDisabled(!profile.usernameConfigured || state==net::State::Connecting);
        if(ImGui::Button(t("Connect","参加する"),{150,32})) {debugActions_.serverEndpoint=std::string(endpoint.data());debugActions_.connectMultiplayer=true;}
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button(t("Disconnect","切断する"),{120,32}))debugActions_.disconnectMultiplayer=true;
        if(f.multiplayer) {
            ImGui::TextUnformatted(state==net::State::Connected?t("Connected","接続中"):state==net::State::Connecting?
                t("Connecting...","接続しています..."):state==net::State::Offline?t("Offline","オフライン"):t("Connection error","接続エラー"));
            if(state==net::State::Error)ImGui::TextWrapped("%s",f.multiplayer->status.c_str());
        }
        ImGui::TextWrapped("%s",t("Local test: http://127.0.0.1:8765. Use your VPS HTTPS URL for friends. Rolls and inventory stay saved on each PC.","PC内のテストは http://127.0.0.1:8765。友達と遊ぶ時はVPSのHTTPS URLを入力します。ロールと所持品は各PCへ保存します。"));
        if(!f.notice.empty())ImGui::TextWrapped("%s",f.notice.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(t("PLAYERS / same planet is shown in the world","参加者 / 同じ惑星の人がワールドに表示されます"));
        if(f.multiplayer)for(const auto& p:f.multiplayer->players) {
            ImGui::PushID(p.userId.c_str());
            if(auto* texture=photoTexture(p.userId+"-"+p.avatarHash,p.avatarPng)) {
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)),{40,40});ImGui::SameLine();
            }
            ImGui::BeginGroup();ImGui::TextUnformatted(p.username.c_str());
            ImGui::TextDisabled("#%s / %s",p.userId.c_str(),stageDisplayName(p.player.planet,profile.language).c_str());
            ImGui::EndGroup();ImGui::Separator();ImGui::PopID();
        }
    }
    ImGui::End();
    if(!open)debugActions_.closeMultiplayer=true;
}
}
