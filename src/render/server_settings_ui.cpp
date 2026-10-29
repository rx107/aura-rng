#include "render/renderer.hpp"
#include "imgui.h"
#include <winhttp.h>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>

// 右下メニューから開く「接続先」専用画面です。
// ここは入力と表示だけ。保存・接続・切断はDebugActionsを受けたAppが実行します。
namespace aura {
namespace {
std::string lowerAscii(std::string value) {
    for(char& c:value)if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');
    return value;
}

std::optional<std::string> serverAddress(std::string value,Language language,std::string& error) {
    const auto t=[&](const char* en,const char* ja){return tr(language,en,ja);};
    const auto whitespace=[](unsigned char c){return c==' ' || c=='\t' || c=='\r' || c=='\n';};
    while(!value.empty() && whitespace(static_cast<unsigned char>(value.front())))value.erase(value.begin());
    while(!value.empty() && whitespace(static_cast<unsigned char>(value.back())))value.pop_back();
    if(value.empty() || value.size()>512 || std::any_of(value.begin(),value.end(),[](unsigned char c){return c<=32 || c==127;})) {
        error=t("Enter a server URL or IP address (up to 512 bytes).","サーバーのURLかIPアドレスを入力してください（512バイト以内）。");return {};
    }
    // IPだけでも入力可能。PC内のテストはHTTP:8765、外部の接続先はHTTPSを標準にします。
    // 通信クライアントと同じ制限を守り、外部HTTPや証明書検証の無効化は追加しません。
    if(value.find("://")==std::string::npos) {
        if(value=="::1")value="[::1]";
        else if(value.front()!='[' && std::count(value.begin(),value.end(),':')>1)value="["+value+"]";
        const auto lower=lowerAscii(value);
        const auto hostEnd=value.front()=='['?value.find(']')+1:value.find(':');
        const auto host=lower.substr(0,hostEnd);
        const bool local=host=="localhost" || host=="127.0.0.1" || host=="[::1]";
        const bool hasPort=value.front()=='['?value.find(':',value.find(']'))!=std::string::npos:value.find(':')!=std::string::npos;
        if(local && !hasPort)value+=":8765";
        value=(local?"http://":"https://")+value;
    }
    if(value.size()>512) {
        error=t("The complete server URL must be at most 512 bytes.","接続先URL全体を512バイト以内にしてください。");return {};
    }
    try {
        const auto url=widen(value);
        URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
        parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
        if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts)) {
            error=t("Check the URL and port, for example https://your-server.example.","URLとポート番号を確認してください。例: https://your-server.example");return {};
        }
        if(!parts.dwHostNameLength || !parts.lpszHostName) {
            error=t("Enter a server hostname or IP address.","サーバーのホスト名かIPアドレスを入力してください。");return {};
        }
        // IPv6も含めたホスト名の比較は、UTF-16の元の値で行います。
        std::wstring wideHost(parts.lpszHostName,parts.dwHostNameLength);
        for(wchar_t& c:wideHost)if(c>=L'A' && c<=L'Z')c=static_cast<wchar_t>(c-L'A'+L'a');
        const bool local=wideHost==L"localhost" || wideHost==L"127.0.0.1" || wideHost==L"::1" || wideHost==L"[::1]";
        const bool validScheme=parts.nScheme==INTERNET_SCHEME_HTTPS || (local && parts.nScheme==INTERNET_SCHEME_HTTP);
        const std::wstring path(parts.lpszUrlPath?parts.lpszUrlPath:L"",parts.dwUrlPathLength);
        if(!validScheme || !parts.dwHostNameLength || !parts.nPort || parts.dwUserNameLength || parts.dwPasswordLength ||
            parts.dwExtraInfoLength || (!path.empty() && path!=L"/")) {
            error=t("External servers require HTTPS. Use a root address without a password, path, or query.",
                "外部サーバーはHTTPSを使用します。パス・パスワード・クエリを含まない接続先を指定してください。");return {};
        }
        if(!value.empty() && value.back()=='/')value.pop_back();
        return value;
    } catch(const std::exception&) {
        error=t("The server address could not be read. Check the characters in the URL.","接続先を読み取れませんでした。URLの文字を確認してください。");return {};
    }
}
}

void Renderer::serverSettingsUi(const Frame& f) {
    const bool opening=f.serverSettingsOpen && !serverSettingsPanelVisible_;
    serverSettingsPanelVisible_=f.serverSettingsOpen;
    if(!serverSettingsPanelVisible_)return;
    const auto& profile=f.presentation.settings;
    const auto t=[&](const char* en,const char* ja){return tr(profile.language,en,ja);};
    if(opening) {
        // 開いた時だけ保存済みの値を写す。通信スナップショットが更新されても入力途中の文字は保ちます。
        serverEndpointDraft_.fill(0);serverUsernameDraft_.fill(0);
        std::copy_n(profile.serverEndpoint.begin(),std::min(profile.serverEndpoint.size(),serverEndpointDraft_.size()-1),serverEndpointDraft_.begin());
        std::copy_n(profile.username.begin(),std::min(profile.username.size(),serverUsernameDraft_.size()-1),serverUsernameDraft_.begin());
        serverSettingsError_.clear();
    }
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x*.5f,display.y*.5f},ImGuiCond_Appearing,{.5f,.5f});
    ImGui::SetNextWindowSize({std::min(540.f,display.x-32),std::min(620.f,display.y-32)},ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2{20,18});
    bool open=true;
    const bool expanded=ImGui::Begin(t("Server address###ServerSettings","マルチプレイヤーIP設定###ServerSettings"),&open,
        ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);
    const auto position=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    serverSettingsBounds_={position.x,position.y,size.x,size.y};
    if(expanded) {
        ImGui::TextWrapped("%s",t("Set the server address here. Saving an address does not connect automatically.",
            "サーバーの接続先を設定します。保存しただけでは自動で接続しません。"));
        ImGui::Spacing();
        ImGui::TextUnformatted(t("SERVER URL / IP","サーバーURL / IPアドレス"));
        ImGui::SetNextItemWidth(-1);
        if(opening)ImGui::SetKeyboardFocusHere();
        if(ImGui::InputTextWithHint("##server-endpoint", "https://your-server.example / 127.0.0.1:8765",
            serverEndpointDraft_.data(),serverEndpointDraft_.size()))serverSettingsError_.clear();
        ImGui::TextWrapped("%s",t("A bare external IP uses HTTPS. Localhost without a port uses http://localhost:8765.",
            "外部IPだけを入力した場合はHTTPSを使います。localhostのポート省略時は8765番です。"));
        if(ImGui::Button(t("Save address","接続先を保存"),{-1,32})) {
            if(auto endpoint=serverAddress(serverEndpointDraft_.data(),profile.language,serverSettingsError_)) {
                debugActions_.serverEndpoint=*endpoint;
                serverEndpointDraft_.fill(0);std::copy(endpoint->begin(),endpoint->end(),serverEndpointDraft_.begin());
                serverSettingsError_.clear();
            }
        }
        ImGui::TextDisabled("%s",t("Saved address:","保存中の接続先:"));
        ImGui::TextWrapped("%s",profile.serverEndpoint.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(t("USERNAME","ユーザー名"));
        ImGui::SetNextItemWidth(-1);
        if(ImGui::InputText("##server-username",serverUsernameDraft_.data(),serverUsernameDraft_.size()))serverSettingsError_.clear();
        if(!profile.usernameConfigured)ImGui::TextWrapped("%s",t("Choose the name to use before joining.","参加するときに使う名前を入力してください。"));
        const auto state=f.multiplayer?f.multiplayer->state:net::State::Offline;
        const bool validName=validUsername(serverUsernameDraft_.data());
        ImGui::BeginDisabled(!validName || state==net::State::Connecting);
        if(ImGui::Button(t("Connect","接続する"),{156,34})) {
            if(auto endpoint=serverAddress(serverEndpointDraft_.data(),profile.language,serverSettingsError_)) {
                debugActions_.serverEndpoint=*endpoint;
                debugActions_.username=std::string(serverUsernameDraft_.data());
                debugActions_.connectMultiplayer=true;
                serverSettingsError_.clear();
            }
        }
        ImGui::EndDisabled();ImGui::SameLine();
        ImGui::BeginDisabled(state==net::State::Offline);
        if(ImGui::Button(t("Disconnect","切断する"),{136,34}))debugActions_.disconnectMultiplayer=true;
        ImGui::EndDisabled();
        if(!validName)ImGui::TextWrapped("%s",t("Enter a username without line breaks (1–64 UTF-8 bytes).","ユーザー名は改行なしの1〜64バイトで入力してください。"));
        ImGui::TextUnformatted(state==net::State::Connected?t("Connected","接続中"):state==net::State::Connecting?
            t("Connecting...","接続しています..."):state==net::State::Error?t("Connection error","接続エラー"):t("Offline","オフライン"));
        if(state==net::State::Error && f.multiplayer)ImGui::TextWrapped("%s",f.multiplayer->status.c_str());
        if(!serverSettingsError_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{1.f,.66f,.52f,1});
            ImGui::TextWrapped("%s",serverSettingsError_.c_str());ImGui::PopStyleColor();
        }
        if(!f.notice.empty())ImGui::TextWrapped("%s",f.notice.c_str());
        ImGui::Spacing();
        if(ImGui::Button(t("Back to game","ゲームに戻る"),{-1,32}))debugActions_.closeServerSettings=true;
        ImGui::TextDisabled("%s",t("Esc closes this screen and keeps your connection.","Escでも閉じられます。接続はそのまま続きます。"));
    }
    ImGui::End();ImGui::PopStyleVar(3);
    if(!open)debugActions_.closeServerSettings=true;
}
} // namespace aura
