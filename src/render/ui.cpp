#include "render/renderer.hpp"
#include "tuning/ui.hpp"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <stdexcept>
#include <shobjidl.h>
#include <sstream>
#include <iomanip>
#include <initializer_list>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace aura {
namespace {
constexpr float Pi = 3.14159265359f;
const D2D1_COLOR_F White = D2D1::ColorF(tuning::ui::Text[0],tuning::ui::Text[1],tuning::ui::Text[2]);
const D2D1_COLOR_F Muted = D2D1::ColorF(tuning::ui::MutedText[0],tuning::ui::MutedText[1],tuning::ui::MutedText[2]);
const D2D1_COLOR_F Mint = D2D1::ColorF(tuning::ui::Accent[0],tuning::ui::Accent[1],tuning::ui::Accent[2]);
const D2D1_COLOR_F Panel = D2D1::ColorF(tuning::ui::Panel[0],tuning::ui::Panel[1],tuning::ui::Panel[2]);
const D2D1_COLOR_F Border = D2D1::ColorF(tuning::ui::Border[0],tuning::ui::Border[1],tuning::ui::Border[2]);
D2D1_COLOR_F tint(Color c, float alpha=1) { return {c.r,c.g,c.b,alpha}; }
bool inside(float x,float y,float rx,float ry,float w,float h) { return x>=rx && x<rx+w && y>=ry && y<ry+h; }
std::optional<std::string> chooseAsset(HWND owner,bool vrm) {
    ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) return {};
    const COMDLG_FILTERSPEC filter=vrm?COMDLG_FILTERSPEC{L"VRM avatar (*.vrm)",L"*.vrm"}:COMDLG_FILTERSPEC{L"Audio (*.ogg;*.mp3;*.mpeg;*.wav;*.m4a)",L"*.ogg;*.mp3;*.mpeg;*.wav;*.m4a"};
    dialog->SetFileTypes(1,&filter); dialog->SetTitle(vrm?L"Select VRM avatar / VRMアバターを選択":L"Select aura BGM / オーラBGMを選択");
    dialog->SetOptions(FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
    if(FAILED(dialog->Show(owner))) return {};
    ComPtr<IShellItem> item; if(FAILED(dialog->GetResult(&item))) return {};
    PWSTR value{}; if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&value))) return {};
    const auto path=std::filesystem::path(value).u8string(); CoTaskMemFree(value);
    return std::string(reinterpret_cast<const char*>(path.data()),path.size());
}
}
Renderer::~Renderer() {
    if (!imguiContext_) return;
    ImGui::SetCurrentContext(imguiContext_);
    if (dx11Backend_) ImGui_ImplDX11_Shutdown();
    if (win32Backend_) ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(imguiContext_);
}
void Renderer::initializeDebug() {
    IMGUI_CHECKVERSION(); imguiContext_=ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    auto& style=ImGui::GetStyle(); style.WindowRounding=12; style.FrameRounding=6;
    style.WindowPadding={18,18}; style.ItemSpacing={10,9}; style.FontSizeBase=16;
    style.Colors[ImGuiCol_WindowBg]={.035f,.055f,.075f,.98f};
    style.Colors[ImGuiCol_CheckMark]={.55f,.96f,.82f,1};
    style.Colors[ImGuiCol_Button]={.10f,.24f,.23f,1};
    style.Colors[ImGuiCol_ButtonHovered]={.16f,.37f,.33f,1};
    wchar_t windowsDirectory[MAX_PATH]{};
    if (GetWindowsDirectoryW(windowsDirectory,MAX_PATH)) {
        for (const auto* file : {L"YuGothR.ttc",L"msgothic.ttc",L"meiryo.ttc"}) {
            auto path=std::filesystem::path(windowsDirectory)/L"Fonts"/file;
            if (!std::filesystem::exists(path)) continue;
            const auto utf8=path.u8string();
            if (io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()),16)) { japaneseFont_=true; break; }
        }
    }
    if (!japaneseFont_) io.Fonts->AddFontDefault();
    win32Backend_=ImGui_ImplWin32_Init(window_);
    if (!win32Backend_) throw std::runtime_error("ImGui Win32 initialization failed");
    dx11Backend_=ImGui_ImplDX11_Init(device_.Get(),context_.Get());
    if (!dx11Backend_) throw std::runtime_error("ImGui DirectX 11 initialization failed");
}
bool Renderer::handleMessage(HWND window,UINT message,WPARAM wp,LPARAM lp) noexcept {
    if (!imguiContext_) return false;
    try {
        ImGui::SetCurrentContext(imguiContext_);
        const LRESULT handled=ImGui_ImplWin32_WndProcHandler(window,message,wp,lp);
        const auto& io=ImGui::GetIO();
        if (message>=WM_MOUSEFIRST && message<=WM_MOUSELAST) return io.WantCaptureMouse;
        if (message>=WM_KEYFIRST && message<=WM_KEYLAST) return io.WantCaptureKeyboard;
        return handled!=0;
    } catch (...) { return false; }
}
bool Renderer::wantsKeyboard() const {
    if (!imguiContext_) return false;
    ImGui::SetCurrentContext(imguiContext_); return ImGui::GetIO().WantCaptureKeyboard;
}
DebugActions Renderer::takeDebugActions() { return std::exchange(debugActions_,{}); }
void Renderer::debugUi(const Frame& f) {
    // F1の開発パネル。ImGuiのボタンを押すとdebugActions_へ命令を積みます。
    // 実際の抽選・保存はmain.cppのapplyDebugActions()が担当します。
    ImGui::SetCurrentContext(imguiContext_);
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    if (debugOpen_) {
        const auto language=f.presentation.settings.language;
        auto t=[&](const char* en,const char* ja){return tr(language,en,ja);};
        const auto display=ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos({std::max(12.f,display.x-466),92},ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({450,std::min(690.f,std::max(420.f,display.y-112))},ImGuiCond_FirstUseEver);
        const bool expanded=ImGui::Begin(t("Star's RNG | Debug###StarDebug","Star's RNG | デバッグ###StarDebug"),&debugOpen_);
        const auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize(); debugBounds_={pos.x,pos.y,size.x,size.y};
        if (expanded) {
            ImGui::TextColored({.55f,.96f,.82f,1},"STAR'S RNG  /  ALPHA 0.1");
            ImGui::TextDisabled("Dear ImGui %s  |  F1",IMGUI_VERSION);
            ImGui::Separator();
            int selectedLanguage=language==Language::Japanese?1:0;
            const char* languages[]={"English","日本語"};
            if (ImGui::Combo(t("Language","言語"),&selectedLanguage,languages,2))
                debugActions_.language=selectedLanguage?Language::Japanese:Language::English;
            if (!japaneseFont_) ImGui::TextWrapped("A Japanese Windows font is unavailable. Install Japanese supplemental fonts for this panel.");
            ImGui::Spacing();
            if(ImGui::BeginTabBar("DebugTabs")) {
            if(ImGui::BeginTabItem(t("Profile","プロフィール"))) {
            static std::array<char,65> usernameDraft{};
            static std::array<char,21> applicationDraft{};
            static std::string usernameSource,applicationSource;
            const auto& profile=f.presentation.settings;
            if(usernameSource!=profile.username) {
                usernameDraft.fill(0); std::copy_n(profile.username.begin(),std::min(profile.username.size(),usernameDraft.size()-1),usernameDraft.begin());
                usernameSource=profile.username;
            }
            if(applicationSource!=profile.discordApplicationId) {
                applicationDraft.fill(0); std::copy_n(profile.discordApplicationId.begin(),std::min(profile.discordApplicationId.size(),applicationDraft.size()-1),applicationDraft.begin());
                applicationSource=profile.discordApplicationId;
            }
            ImGui::TextUnformatted(t("PLAYER PROFILE","プレイヤープロフィール"));
            ImGui::InputText(t("Username","ユーザー名"),usernameDraft.data(),usernameDraft.size());
            const bool usernameValid=validUsername(usernameDraft.data());
            ImGui::BeginDisabled(!usernameValid);
            if(ImGui::Button(t("Apply username","ユーザー名を適用"),{-1,30})) debugActions_.username=std::string(usernameDraft.data());
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s",t("1–64 UTF-8 bytes. Your name can be changed at any time.",
                "1〜64バイトのUTF-8文字列。ユーザー名はいつでも変更できます。"));
            if(!usernameValid) ImGui::TextWrapped("%s",t("Enter a name without control characters or line breaks.","改行・制御文字を含まない名前を入力してください。"));
            ImGui::Separator();
            ImGui::TextUnformatted("DISCORD RICH PRESENCE");
            bool discordEnabled=profile.discordEnabled;
            if(ImGui::Checkbox(t("Show activity in Discord","Discordにプレイ状況を表示"),&discordEnabled)) debugActions_.discordEnabled=discordEnabled;
            ImGui::TextWrapped("%s",t("Star's RNG is configured. Start Discord desktop to show your username and roll count.",
                "Star's RNG用の設定済みです。Discordのデスクトップ版を起動すると、ユーザー名とロール回数を表示します。"));
            ImGui::TextWrapped("%s",f.discordStatus.c_str());
            if(ImGui::CollapsingHeader(t("Advanced Discord settings","Discordの詳細設定"))) {
            ImGui::InputText(t("Application ID","Application ID"),applicationDraft.data(),applicationDraft.size(),ImGuiInputTextFlags_CharsDecimal);
            const bool applicationValid=validDiscordApplicationId(applicationDraft.data());
            ImGui::BeginDisabled(!applicationValid);
            if(ImGui::Button(t("Apply Application ID","Application IDを適用"),{-1,30})) debugActions_.discordApplicationId=std::string(applicationDraft.data());
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s",t("The default Application ID is already set. Only change it when using a different Discord application.",
                "初期設定のApplication IDを使用できます。別のDiscordアプリと連携するときだけ変更してください。"));
            }
            ImGui::Separator();
            ImGui::TextUnformatted(t("ROLL COUNTER / DEBUG","ロール回数 / デバッグ"));
            static std::uint64_t rollDraft{},rollSource{};
            static bool rollInitialized{},rollDirty{};
            if(!rollInitialized || rollSource!=f.progress.rolls) {
                if(!rollDirty) rollDraft=f.progress.rolls;
                rollSource=f.progress.rolls; rollInitialized=true;
            }
            if(ImGui::InputScalar(t("Total rolls","総ロール回数"),ImGuiDataType_U64,&rollDraft)) {
                rollDraft=std::min<std::uint64_t>(rollDraft,1'000'000'000'000'000ULL); rollDirty=true;
            }
            if(ImGui::Button(t("Apply roll count","ロール回数を適用"),{-1,30})) {
                debugActions_.rollCount=rollDraft; rollDirty=false;
            }
            if(ImGui::SmallButton(t("Use current count","現在の回数に戻す"))) {rollDraft=f.progress.rolls;rollDirty=false;}
            ImGui::TextWrapped("%s",t("Changes the saved total and Discord activity. Inventory and existing roll history are preserved; this action adds no roll history entry. Range: 0–1,000,000,000,000,000.",
                "保存される総回数とDiscord表示を変更します。在庫・既存履歴はそのままで、この操作は履歴に追加しません。入力範囲: 0〜1,000,000,000,000,000。"));
            ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem(t("World","ワールド"))) {
            ImGui::BeginDisabled(f.cooldown>0);
            if (ImGui::Button(t("Roll##debugRoll","ロール##debugRoll"),{120,32})) debugActions_.roll=true;
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button(f.autoRoll?t("Stop AutoRoll##debugAuto","自動ロール停止##debugAuto"):
                t("Start AutoRoll##debugAuto","自動ロール開始##debugAuto"),{220,32})) debugActions_.toggleAuto=true;
            ImGui::Text(t("Cooldown: %.2f s","クールタイム: %.2f 秒"),f.cooldown);
            ImGui::Separator();
            double luck=f.roller.luck(); const double minLuck=1,maxLuck=1'000'000;
            if(ImGui::DragScalar(t("Luck multiplier","Luck倍率"),ImGuiDataType_Double,&luck,.25f,&minLuck,&maxLuck,"%.2fx",ImGuiSliderFlags_AlwaysClamp)) debugActions_.luck=luck;
            if(ImGui::SmallButton(t("Reset Luck / 1x","Luckを1倍に戻す"))) debugActions_.luck=1;
            ImGui::TextWrapped("%s",t("1x uses the base odds. Lx has the rarity distribution of the best of L rolls. One result is recorded per Roll.",
                "1倍は基本確率です。L倍ではL回分の最もレアな結果と同じ確率になります。ロール1回につき結果は1個です。"));
            int biome=static_cast<int>(f.presentation.settings.biome);
            const auto currentStage=stageDisplayName(f.presentation.settings.biome,language);
            if(ImGui::BeginCombo(t("Planet / stage","惑星 / ステージ"),currentStage.c_str())) {
                for(unsigned i=0;i<StageCount;++i) {
                    const auto destination=static_cast<Biome>(i);
                    const bool selected=biome==static_cast<int>(i);
                    if(ImGui::Selectable(stageDisplayName(destination,language).c_str(),selected)) debugActions_.biome=destination;
                    if(selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("%s",t("Debug changes the scenery immediately. Use PLANETS in the game view to travel by rocket.",
                "デバッグでは景観をすぐに変更します。ゲーム画面の「惑星を選択」ではロケットで移動します。"));
            ImGui::Separator();
            ImGui::TextUnformatted(t("CURRENT ODDS / WITH LUCK","現在の排出確率 / Luck反映後"));
            for(std::size_t i=0;i<f.roller.catalog().size();++i) {
                if(!availableInBiome(f.roller.catalog()[i],f.presentation.settings.biome)) continue;
                const auto tickets=f.roller.tickets(i);
                const double percent=100.*static_cast<double>(tickets)/TicketPool;
                ImGui::Text("%s: %.9g%%",displayName(f.roller.catalog()[i],language).c_str(),percent);
            }
            ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem(t("Avatar","アバター"))) {
            int avatar=static_cast<int>(f.presentation.settings.avatar);
            const char* avatars[]={t("Block avatar","ブロックアバター"),t("VRM avatar","VRMアバター")};
            if(ImGui::Combo(t("Avatar type","アバター形式"),&avatar,avatars,2)) debugActions_.avatar=static_cast<Avatar>(avatar);
            if(ImGui::Button(t("Choose VRM...","VRMを選択..."),{-1,32})) {
                if(auto path=chooseAsset(window_,true)) {debugActions_.vrmPath=*path; debugActions_.avatar=Avatar::Vrm;}
            }
            ImGui::TextWrapped("%s",f.presentation.settings.vrmPath.c_str());
            ImGui::TextWrapped("%s",vrmStatus_.c_str());
            ImGui::TextWrapped("%s",t("VRM 0.x / 1.0: standing and walking animations. Fixed aura poses stay active while moving. Block avatar is shown on import error.",
                "VRM 0.x / 1.0: 立ち姿と歩行に対応。固定ポーズがあるオーラは移動中も姿勢を保ちます。読み込み失敗時はブロックを表示します。"));
            ImGui::Separator();
            ImGui::TextUnformatted(t("BLOCK AVATAR / SHIRT","ブロックアバター / 服の色"));
            const auto shirt=f.presentation.settings.shirt;
            float rgb[]={shirt.r,shirt.g,shirt.b};
            if (ImGui::ColorEdit3(t("Shirt color","服の色"),rgb,ImGuiColorEditFlags_Float|ImGuiColorEditFlags_PickerHueWheel))
                debugActions_.shirt=Color{rgb[0],rgb[1],rgb[2]};
            if (ImGui::SmallButton(t("Reset shirt color","服の色をリセット"))) debugActions_.shirt=Settings{}.shirt;
            ImGui::TextWrapped("%s",t("Singularity gives both avatars the same floating pose, including while moving.","Singularity表示時は両アバターが同じ無重力ポーズになり、移動中も維持します。"));
            ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem(t("Auras","オーラ"))) {
            ImGui::Separator();
            ImGui::TextUnformatted(t("AURA PREVIEW / ALL AURAS","オーラプレビュー / 全種類"));
            ImGui::TextWrapped("%s",t("Includes unowned auras. Preview stops AutoRoll and does not add inventory or history.",
                "未所持のオーラも表示できます。選択時に自動ロールを停止し、所持数や履歴には追加しません。"));
            for(std::size_t i=0;i<f.roller.catalog().size();++i) if(f.roller.catalog()[i].id=="illusionary") {
                ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.11f,.13f,.42f,1));
                if(ImGui::Button(t("Preview Illusionary","Illusionary を表示"),{-1,36})) debugActions_.preview=i;
                ImGui::PopStyleColor();
                ImGui::TextWrapped("%s",t("Extra Stage exclusive. Reclining avatar, solid checker floor and orbiting white flares. Preview adds no history.",
                    "Extra Stage限定。横向きの浮遊、床のあるチェック模様、周回する白い光。プレビューは履歴を増やしません。"));
            }
            ImGui::TextDisabled("%s",t("Desert auras / Desert stage only","砂漠テーマのオーラ / 砂漠ステージ限定"));
            for(std::size_t i=0;i<f.roller.catalog().size();++i) {
                const auto& aura=f.roller.catalog()[i];
                if(aura.id!="dune" && aura.id!="mirage" && aura.id!="pharaoh") continue;
                const auto button=std::string(t("Preview ","表示: "))+displayName(aura,language)+" / "+oddsLabel(f.roller.catalog(),i);
                if(ImGui::Button(button.c_str(),{-1,28})) debugActions_.preview=i;
            }
            for(std::size_t i=0;i<f.roller.catalog().size();++i) if(f.roller.catalog()[i].id=="nanally") {
                ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.06f,.32f,.30f,1));
                if(ImGui::Button(t("Preview Nanally / prototype","Nanally を表示 / 試作"),{-1,36})) debugActions_.preview=i;
                ImGui::PopStyleColor();
                ImGui::TextWrapped("%s",t("A giant dark companion and fists behind you, red flames, and tiny teal electrical sparks. Preview only; excluded from rolls.",
                    "背後に大きな黒い生物と左右の拳、赤い炎。プレイヤーの周囲に小さな青緑の電撃。試作のためロール抽選の対象外です。"));
            }
            for(std::size_t i=0;i<f.roller.catalog().size();++i) if(f.roller.catalog()[i].id=="pixelation") {
                ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.04f,.35f,.27f,1));
                if(ImGui::Button(t("Preview Pixelation / prototype","Pixelation を表示 / 試作"),{-1,36})) debugActions_.preview=i;
                ImGui::PopStyleColor();
                ImGui::TextWrapped("%s",t("Pixel fragments, holograms and a wireframe cube. Obtainable in Extra Stage.",
                    "ピクセル片、ホログラム、ワイヤー立方体。Extra Stageで入手できます。"));
                const auto actualPixelationLevel=f.progress.auraLevel(i,f.roller.catalog());
                int pixelationLevel=static_cast<int>(f.presentation.pixelationLevel.value_or(actualPixelationLevel));
                if(ImGui::SliderInt("Pixelation / Aura Level",&pixelationLevel,0,3)) debugActions_.pixelationLevel=static_cast<unsigned>(pixelationLevel);
                if(ImGui::SmallButton(t("Use Collection level##pixelation","所持数に応じたレベルへ戻す##pixelation"))) debugActions_.pixelationActualLevel=true;
                ImGui::TextWrapped("%s",t("0: original / 1: faster + Redline Shift / 2: exchange of fire + short synthesized effects / 3: faster again + Censored. Impact flashes, shock rings and fragments. Debug levels are temporary.",
                    "0: 通常 / 1: 速度アップ＋Redline Shift / 2: 打ち合い＋短い合成効果音 / 3: さらに加速＋Censored。着弾に閃光・衝撃波・破片を表示。デバッグレベルは一時的です。"));
            }
            for(std::size_t i=0;i<f.roller.catalog().size();++i) if(f.roller.catalog()[i].id=="equinox") {
                ImGui::Separator();
                if(ImGui::Button(t("Preview Equinox","Equinox を表示"),{-1,34})) debugActions_.preview=i;
                ImGui::TextWrapped("%s",t("The cutscene fades into an aura preview. No inventory or roll history is added; Collection equipment is preserved.",
                    "終了時にオーラのプレビューへフェードします。所持数・履歴・Collectionの実装備は変更しません。"));
                if(ImGui::Button(t("Play cutscene only##equinox","カットシーンのみ再生##equinox"))) debugActions_.playEquinoxCutscene=true;
                ImGui::SameLine();
                if(ImGui::Button(t("Stop cutscene##equinox","カットシーン停止##equinox"))) debugActions_.stopCutscene=true;
            }
            for(std::size_t i=0;i<f.roller.catalog().size();++i) if(f.roller.catalog()[i].id=="breakthrough") {
                ImGui::Separator();
                if(ImGui::Button(t("Preview BreakThrough","BreakThrough を表示"),{-1,34})) debugActions_.preview=i;
                ImGui::TextWrapped("%s",t("Obtainable in Extra Stage. Cutscene preview adds no inventory or history. Esc restores the previous view.",
                    "Extra Stageで入手可能です。演出プレビューは所持数と履歴を増やしません。Escで直前の表示へ戻ります。"));
                if(ImGui::Button(t("Play cutscene only##breakthrough","カットシーンのみ再生##breakthrough"))) debugActions_.playBreakthroughCutscene=true;
                ImGui::SameLine();
                if(ImGui::Button(t("Stop cutscene##breakthrough","カットシーン停止##breakthrough"))) debugActions_.stopCutscene=true;
                if(ImGui::CollapsingHeader(t("Caption timing / seconds##breakthrough","字幕タイミング / 秒##breakthrough"))) {
                    // 音楽そのものをずらすのではなく、字幕の時刻だけ変更します。
                    // マイナスならサックスより先へ、プラスなら後ろへ。変更は保存されます。
                    float offset=f.presentation.settings.breakthroughCaptionOffset;
                    if(ImGui::DragFloat(t("All words offset","全単語の時差"),&offset,.01f,-5.f,5.f,"%+.2f s",ImGuiSliderFlags_AlwaysClamp))
                        debugActions_.breakthroughCaptionOffset=offset;
                    ImGui::TextWrapped("%s",t("Negative: earlier. Positive: later. Changes are saved; no rebuild is needed. Replay to check against the saxophone.",
                        "負の値で早く、正の値で遅く表示します。自動保存され、再ビルドは不要です。再生し直してサックスと合わせてください。"));
                    if(f.cutsceneTime>=0 && f.cutsceneKind==CutsceneKind::Breakthrough)
                        ImGui::Text(t("Cutscene clock: %.2f s","カットシーンの現在時刻: %.2f 秒"),f.cutsceneTime);
                    else ImGui::TextDisabled("%s",t("Play the cutscene above to see its clock.","上のボタンで再生すると、現在時刻を確認できます。"));
                    auto times=f.presentation.settings.breakthroughWordTimes;
                    constexpr std::array<const char*,8> words={"how","much","do","you","believe","in","this","world?"};
                    bool changed=false;
                    ImGui::TextWrapped("%s",t("Each time is measured from the start of the cutscene. The offset above is added to every word.",
                        "各値はカットシーン開始からの秒数です。上の時差をすべての単語に加算します。"));
                    for(std::size_t word=0;word<times.size();++word) {
                        ImGui::PushID(static_cast<int>(word));
                        // 前後の単語を追い越さない範囲で、1/100秒単位に調整できます。
                        const float earliest=word?times[word-1]+.01f:0.f;
                        const float latest=word+1<times.size()?times[word+1]-.01f:BreakthroughCutsceneDuration-.01f;
                        if(ImGui::DragFloat(words[word],&times[word],.01f,earliest,latest,"%.2f s",ImGuiSliderFlags_AlwaysClamp)) changed=true;
                        ImGui::PopID();
                    }
                    if(changed) debugActions_.breakthroughWordTimes=times;
                    if(ImGui::Button(t("Reset caption timing##breakthrough","字幕タイミングを初期値へ##breakthrough"),{-1,30})) {
                        debugActions_.breakthroughCaptionOffset=0.f;
                        debugActions_.breakthroughWordTimes=tuning::cutscene::BreakthroughWordTimes;
                    }
                    ImGui::TextWrapped("%s",t("F1 toggles this panel during playback. Esc skips the cutscene.",
                        "再生中もF1でこのパネルを開閉できます。Escでカットシーンを終了します。"));
                }
            }
            if (ImGui::Button(t("Restore Collection aura","コレクションの装備に戻す"),{-1,30})) debugActions_.restore=true;
            ImGui::Separator();
            unsigned actualLevel=0;
            for(std::size_t i=0;i<f.roller.catalog().size();++i) if(f.roller.catalog()[i].id=="singularity") actualLevel=f.progress.auraLevel(i,f.roller.catalog());
            int level=static_cast<int>(f.presentation.singularityLevel.value_or(actualLevel));
            if(ImGui::SliderInt("Singularity / Aura Level",&level,0,3)) debugActions_.auraLevel=static_cast<unsigned>(level);
            ImGui::Text(t("Collection level: %u / 3","所持分のレベル: %u / 3"),actualLevel);
            if(ImGui::SmallButton(t("Use Collection level","所持数に応じたレベルへ戻す"))) debugActions_.actualLevel=true;
            ImGui::TextWrapped("%s",t("0: original  /  1: stars  /  2: small black hole  /  3: large black hole absorbs stars. Debug level is temporary; no rolls are added.",
                "0: 初期 / 1: 星 / 2: 小ブラックホール / 3: 大ブラックホールが星を吸収。デバッグ変更は一時的で、履歴には追加しません。"));
            if (ImGui::BeginChild("AuraPreviewList",{0,std::max(120.f,ImGui::GetContentRegionAvail().y-34)},ImGuiChildFlags_Borders)) {
                const auto& catalog=f.roller.catalog();
                for (std::size_t i=0;i<catalog.size();++i) {
                    const auto& d=catalog[i];
                    const auto row=displayName(d,language)+"  |  "+oddsLabel(catalog,i,f.presentation.settings.biome)+"  |  x"+
                        formatNumber(f.progress.counts[i])+"###preview-"+d.id;
                    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(d.color.r,d.color.g,d.color.b,1));
                    if (ImGui::Selectable(row.c_str(),f.presentation.shown(f.progress)==i)) debugActions_.preview=i;
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem(t("Audio","音楽"))) {
                ImGui::TextWrapped("%s",t("BGM follows the displayed aura and its level, including Debug Preview. Choose local files or restore each default track.",
                    "表示オーラとレベルに応じてBGMを切り替えます。デバッグプレビューにも対応します。ローカルファイルの選択と各曲の初期設定への復帰ができます。"));
                float volume=f.presentation.settings.volume;
                if(ImGui::SliderFloat(t("Music / SFX volume","音楽・効果音の音量"),&volume,0,1,"%.2f")) debugActions_.volume=volume;
                ImGui::TextWrapped("%s",f.musicStatus.c_str());
                ImGui::Separator(); ImGui::TextUnformatted("Singularity");
                if(ImGui::Button(t("Choose Singularity BGM...","Singularity BGMを選択..."),{-1,32})) if(auto path=chooseAsset(window_,false)) debugActions_.bgmPath=*path;
                ImGui::TextWrapped("%s",f.presentation.settings.bgmPath.empty()?t("Bundled: Tides of the Outer Reach","同梱BGM: Tides of the Outer Reach"):f.presentation.settings.bgmPath.c_str());
                if(ImGui::SmallButton(t("Use default track##singularity","初期設定の曲を使用##singularity"))) debugActions_.bgmPath=std::string{};
                ImGui::Separator(); ImGui::TextUnformatted("Pixelation / Lv 1-2");
                if(ImGui::Button(t("Choose Redline Shift BGM...","Redline Shift用BGMを選択..."),{-1,32})) if(auto path=chooseAsset(window_,false)) debugActions_.pixelationBgmPath=*path;
                ImGui::TextWrapped("%s",f.presentation.settings.pixelationBgmPath.empty()?t("Default: Redline Shift (bundled or local Downloads)",
                    "初期設定: Redline Shift（同梱またはローカルDownloads）"):f.presentation.settings.pixelationBgmPath.c_str());
                if(ImGui::SmallButton(t("Use default track##pixelation","初期設定の曲を使用##pixelationAudio"))) debugActions_.pixelationBgmPath=std::string{};
                ImGui::Separator(); ImGui::TextUnformatted("Pixelation / Lv 3");
                if(ImGui::Button(t("Choose Censored BGM...","Censored用BGMを選択..."),{-1,32})) if(auto path=chooseAsset(window_,false)) debugActions_.pixelationFinalBgmPath=*path;
                ImGui::TextWrapped("%s",f.presentation.settings.pixelationFinalBgmPath.empty()?t("Default: Censored (bundled or local Downloads)",
                    "初期設定: Censored（同梱またはローカルDownloads）"):f.presentation.settings.pixelationFinalBgmPath.c_str());
                if(ImGui::SmallButton(t("Use default track##pixelationFinal","初期設定の曲を使用##pixelationFinal"))) debugActions_.pixelationFinalBgmPath=std::string{};
                ImGui::Separator(); ImGui::TextUnformatted(t("PIXELATION / CUBE HEARTBEAT","PIXELATION / キューブの鼓動"));
                bool autoBeat=f.presentation.settings.pixelationBpm==0;
                if(ImGui::Checkbox(t("Estimate BPM from music","音楽からBPMを推定"),&autoBeat))
                    debugActions_.pixelationBpm=autoBeat?0.f:120.f;
                ImGui::Text(t("Detected tempo: %.1f BPM","推定テンポ: %.1f BPM"),f.musicBpm);
                if(!autoBeat) {
                    float bpm=f.presentation.settings.pixelationBpm;
                    if(ImGui::SliderFloat("BPM##pixelationBeat",&bpm,30,300,"%.1f")) debugActions_.pixelationBpm=bpm;
                }
                float beatOffset=f.presentation.settings.pixelationBeatOffset;
                if(ImGui::DragFloat(t("Beat offset / seconds","拍の時差 / 秒"),&beatOffset,.01f,-2,2,"%.2f",ImGuiSliderFlags_AlwaysClamp))
                    debugActions_.pixelationBeatOffset=beatOffset;
                if(ImGui::SmallButton(t("Reset heartbeat settings","鼓動の設定を初期値へ"))) {
                    debugActions_.pixelationBpm=0.f; debugActions_.pixelationBeatOffset=0.f;
                }
                ImGui::TextWrapped("%s",t("Uses music playback time. If the estimate feels half/double speed, enter the BPM manually. Positive offset delays the pulse. No music means no automatic pulse.",
                    "音楽の実再生時刻に同期します。推定が半分・倍に感じるときはBPMを手動指定。正の時差で鼓動が遅れます。音楽のない場合は自動で鼓動しません。"));
                ImGui::Separator(); ImGui::TextUnformatted("EQUINOX / BPM");
                if(ImGui::Button(t("Choose Equinox BGM...","Equinox BGMを選択..."),{-1,32})) if(auto path=chooseAsset(window_,false)) debugActions_.equinoxBgmPath=*path;
                ImGui::TextWrapped("%s",f.presentation.settings.equinoxBgmPath.empty()?"Koraii / Layers Cut":f.presentation.settings.equinoxBgmPath.c_str());
                if(ImGui::SmallButton(t("Use default track##equinox","初期設定の曲を使用##equinox"))) debugActions_.equinoxBgmPath=std::string{};
                float equinoxBpm=f.presentation.settings.equinoxBpm,equinoxOffset=f.presentation.settings.equinoxBeatOffset;
                if(ImGui::SliderFloat(t("Aura BPM##eq","通常オーラのBPM##eq"),&equinoxBpm,30,300,"%.2f")) debugActions_.equinoxBpm=equinoxBpm;
                if(ImGui::DragFloat(t("Aura beat offset / seconds##eq","通常の拍の時差 / 秒##eq"),&equinoxOffset,.005f,-2,2,"%.3f",ImGuiSliderFlags_AlwaysClamp))
                    debugActions_.equinoxBeatOffset=equinoxOffset;
                float cutBpm=f.presentation.settings.equinoxCutsceneBpm,cutOffset=f.presentation.settings.equinoxCutsceneBeatOffset;
                if(ImGui::SliderFloat(t("CutScene BPM##eq","CutSceneのBPM##eq"),&cutBpm,30,300,"%.2f")) debugActions_.equinoxCutsceneBpm=cutBpm;
                if(ImGui::DragFloat(t("CutScene beat offset / seconds##eq","CutSceneの拍の時差 / 秒##eq"),&cutOffset,.005f,-2,2,"%.3f",ImGuiSliderFlags_AlwaysClamp))
                    debugActions_.equinoxCutsceneBeatOffset=cutOffset;
                if(ImGui::SmallButton(t("Reset Equinox beat settings","Equinoxの拍設定を初期値へ"))) {
                    const Settings defaults;
                    debugActions_.equinoxBpm=defaults.equinoxBpm; debugActions_.equinoxBeatOffset=defaults.equinoxBeatOffset;
                    debugActions_.equinoxCutsceneBpm=defaults.equinoxCutsceneBpm; debugActions_.equinoxCutsceneBeatOffset=defaults.equinoxCutsceneBeatOffset;
                }
                ImGui::TextWrapped("%s",t("Aura and CutScene use separate music clocks. Positive offset delays the beat. Every 4 beats: Yin, Yang, Balance, Nothing.",
                    "通常とCutSceneは別々の音源に同期します。正の時差で拍を遅くします。床は4拍ごとに陰→陽→衡→無です。"));
                ImGui::Separator(); ImGui::TextUnformatted("Illusionary");
                if(ImGui::Button(t("Choose Illusionary BGM...","Illusionary BGMを選択..."),{-1,32})) if(auto path=chooseAsset(window_,false)) debugActions_.illusionaryBgmPath=*path;
                ImGui::TextWrapped("%s",f.presentation.settings.illusionaryBgmPath.empty()?"The Weirdos / Ogg Vorbis":f.presentation.settings.illusionaryBgmPath.c_str());
                if(ImGui::SmallButton(t("Use default track##illusionary","初期設定の曲を使用##illusionary"))) debugActions_.illusionaryBgmPath=std::string{};
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            }
            ImGui::TextDisabled("%s",t("Settings are saved automatically.","設定は自動保存されます。"));
        }
        ImGui::End();
    }
    ImGui::Render();
}
void Renderer::createStar() {
    checked(d2dFactory_->CreatePathGeometry(&starGeometry_),"Star geometry");
    ComPtr<ID2D1GeometrySink> sink; checked(starGeometry_->Open(&sink),"Star sink");
    D2D1_POINT_2F points[10];
    for (int i=0;i<10;++i) {
        const float a=-Pi/2+i*Pi/5,r=i%2?.49f:1.f;
        points[i]={std::cos(a)*r,std::sin(a)*r};
    }
    auto mix=[](D2D1_POINT_2F a,D2D1_POINT_2F b,float t){return D2D1::Point2F(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t);};
    sink->BeginFigure(mix(points[0],points[9],.23f),D2D1_FIGURE_BEGIN_FILLED);
    for (int i=0;i<10;++i) {
        const auto p=points[i],in=mix(p,points[(i+9)%10],.23f),out=mix(p,points[(i+1)%10],.23f);
        if (i) sink->AddLine(in);
        sink->AddBezier(D2D1::BezierSegment(mix(in,p,2.f/3),mix(out,p,2.f/3),out));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED); checked(sink->Close(),"Close star geometry");
    auto props=D2D1::StrokeStyleProperties(); props.startCap=props.endCap=D2D1_CAP_STYLE_ROUND; props.lineJoin=D2D1_LINE_JOIN_ROUND;
    checked(d2dFactory_->CreateStrokeStyle(props,nullptr,0,&roundStroke_),"Round stroke");
}
void Renderer::star(float x,float y,float radius,D2D1_COLOR_F color,bool filled,float angle) {
    D2D1_MATRIX_3X2_F previous; d2d_->GetTransform(&previous);
    const auto transform=D2D1::Matrix3x2F::Scale(radius,radius)*D2D1::Matrix3x2F::Rotation(angle)*
        D2D1::Matrix3x2F::Translation(x,y)*D2D1::Matrix3x2F(previous._11,previous._12,previous._21,previous._22,previous._31,previous._32);
    d2d_->SetTransform(transform); brush_->SetColor(color);
    if (filled) d2d_->FillGeometry(starGeometry_.Get(),brush_.Get());
    else d2d_->DrawGeometry(starGeometry_.Get(),brush_.Get(),1.6f/radius,roundStroke_.Get());
    d2d_->SetTransform(previous);
}
void Renderer::cooldown(float x,float y,float width,float remaining) {
    const auto visual=cooldownVisual(remaining);
    if (!visual.visible || visual.opacity<=0) return;
    auto path=[&](float end,D2D1_COLOR_F color,float thickness) {
        ComPtr<ID2D1PathGeometry> geometry; checked(d2dFactory_->CreatePathGeometry(&geometry),"Cooldown wave");
        ComPtr<ID2D1GeometrySink> sink; checked(geometry->Open(&sink),"Cooldown sink");
        sink->BeginFigure({x,y},D2D1_FIGURE_BEGIN_HOLLOW);
        const int steps=std::max(1,static_cast<int>(end*64));
        for (int i=1;i<=steps;++i) {
            const float p=end*static_cast<float>(i)/steps;
            sink->AddLine({x+width*p,y+std::sin(p*4*Pi)*3});
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN); checked(sink->Close(),"Close cooldown wave");
        brush_->SetColor(color); d2d_->DrawGeometry(geometry.Get(),brush_.Get(),thickness,roundStroke_.Get());
    };
    auto track=Border; track.a=visual.opacity*.8f; path(1,track,2);
    auto trail=Mint; trail.a=visual.opacity; path(visual.progress,trail,2.3f);
    star(x+width*visual.progress,y+visual.waveY,7.5f,trail,true,std::sin(visual.progress*2*Pi)*12);
}
void Renderer::ui(const Frame& f) {
    const float w=logicalW_,h=logicalH_,right=w-314;
    const auto& defs=f.roller.catalog(); const auto shown=f.presentation.shown(f.progress); const auto& equipped=defs[shown];
    const auto language=f.presentation.settings.language;
    auto t=[&](const char* en,const char* ja){return std::string(tr(language,en,ja));};
    auto name=[&](std::size_t i){return displayName(defs[i],language);};
    rect(0,0,w,80,D2D1::ColorF(.035f,.053f,.075f));
    star(40,40,18,Mint,false); text(L"STAR'S RNG",70,22,225,38,27,White,true);
    rect(281,31,92,23,Panel,11); text(L"ALPHA 0.1",292,35,80,20,10,Mint,true);
    rect(w-330,26,112,28,Panel,12); label(t("DEBUG / F1","デバッグ / F1"),w-316,32,104,21,11,Mint,true);
    rect(w-206,26,104,28,Panel,12); text(L"DX11 / NATIVE",w-194,33,100,20,10,Muted);
    label((f.fps>0?std::to_string(static_cast<int>(f.fps)):"--")+" FPS",w-86,31,70,24,12,Muted);
    rect(24,96,216,h-280,Panel,14); stroke(24,96,216,h-280,Border,14);
    label(t("COLLECTION","コレクション"),42,114,180,24,12,Muted,true);
    std::size_t collectionTotal=0,collectionDiscovered=0;
    for(std::size_t i=0;i<defs.size();++i) if(!defs[i].previewOnly) {
        ++collectionTotal; if(f.progress.counts[i]) ++collectionDiscovered;
    }
    label(std::to_string(collectionDiscovered)+" / "+std::to_string(collectionTotal),42,146,170,42,30,White,true);
    label(t("Auras discovered","発見したオーラ"),42,187,170,22,12,Muted);
    rect(42,220,180,3,Border,1);
    rect(42,220,collectionTotal?180*static_cast<float>(collectionDiscovered)/static_cast<float>(collectionTotal):0,3,Mint,1);
    // フォルダーを切り替えて、長い一覧をテーマごとに読みやすくします。
    constexpr const char* folders[]={"Normal","Desert","Extra","Administrator"};
    for(unsigned category=0;category<4;++category) {
        const float x=34+(category%2)*100.f,y=235+(category/2)*29.f;
        const bool selected=static_cast<unsigned>(f.collectionCategory)==category;
        rect(x,y,96,24,selected?D2D1::ColorF(.11f,.24f,.25f):D2D1::ColorF(.055f,.09f,.12f),5);
        stroke(x,y,96,24,selected?Mint:Border,5);
        label(folders[category],x+3,y+5,90,18,category==3?9.f:11.f,selected?Mint:Muted,selected,true);
    }
    const auto items=collectionItems(f);
    const int visible=std::max(1,static_cast<int>((h-500)/48));
    const int start=std::clamp(f.collectionScroll,0,std::max(0,static_cast<int>(items.size())-visible));
    for (int row=0;row<visible && start+row<static_cast<int>(items.size());++row) {
        const auto i=items[start+row]; const float y=306+row*48.f;
        const bool owned=!defs[i].previewOnly && f.progress.counts[i]!=0,selected=!defs[i].previewOnly && i==f.progress.equipped;
        if (selected) rect(34,y-5,196,43,D2D1::ColorF(.09f,.15f,.17f),6);
        star(47,y+11,7.5f,tint(defs[i].color,owned?1.f:.42f),owned);
        label(name(i),64,y,155,21,14,owned?White:Muted,selected);
        label(oddsLabel(defs,i,f.presentation.settings.biome)+(defs[i].previewOnly?"":owned?"   x"+formatNumber(f.progress.counts[i]):"   "+t("LOCKED","未所持")),64,y+22,157,18,9,Muted);
    }
    if (static_cast<int>(items.size())>visible) {
        rect(234,306,2,h-510,Border,1);
        rect(234,306+(h-510)*static_cast<float>(start)/static_cast<float>(items.size()),2,
            (h-510)*static_cast<float>(visible)/static_cast<float>(items.size()),Mint,1);
    }
    stroke(256,96,w-596,h-280,Border,14);
    label(t("PLANET SANCTUARY","惑星のサンクチュアリ"),276,116,240,25,12,White,true);
    label(stageDisplayName(f.presentation.settings.biome,language),276,140,300,20,10,Muted);
    rect(w-487,114,130,26,D2D1::ColorF(.05f,.10f,.12f,.90f),13);
    const bool stageHover=inside(f.mouseX/scale_,f.mouseY/scale_,w-487,114,130,26);
    stroke(w-487,114,130,26,stageHover?Mint:Border,13);
    label(t("PLANETS / TRAVEL","惑星を選択 / 移動"),w-481,120,118,20,10,Mint,true,true);
    const float titleY=h-281;
    label(f.presentation.preview?t("DEBUG PREVIEW","デバッグプレビュー"):t("EQUIPPED AURA","装備中のオーラ"),276,titleY,300,20,10,Muted,true);
    label(name(shown),276,titleY+23,w-650,45,32,tint(equipped.color),true);
    label(oddsLabel(defs,shown,f.presentation.settings.biome)+((equipped.id=="singularity" || equipped.id=="pixelation")?"   /   Aura Level "+std::to_string(f.presentation.level(f.progress,defs))+" / 3":""),276,titleY+67,w-650,25,12,Muted);
    if(nameplateVisible_) {
        d2d_->PushAxisAlignedClip(D2D1::RectF(258,172,w-342,h-294),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float width=std::min(440.f,w-624),x=std::clamp(nameplate_.x-width*.5f,264.f,w-348-width),y=nameplate_.y;
        if(equipped.id!="equinox") {
            label(name(shown),x+1,y+1,width,32,23,D2D1::ColorF(0,0,0,.8f),true,true);
            label(name(shown),x,y,width,32,23,tint(equipped.color),true,true);
        }
        label(oddsInLabel(defs,shown,f.presentation.settings.biome),x,y+(equipped.id=="equinox"?0:31),width,25,14,White,false,true);
        if(equipped.id=="singularity" || equipped.id=="pixelation") label("Aura Level "+std::to_string(f.presentation.level(f.progress,defs)),x,y+56,width,24,12,Mint,true,true);
        d2d_->PopAxisAlignedClip();
    }
    rect(right,96,290,h-280,Panel,14); stroke(right,96,290,h-280,Border,14);
    label(t("ROLL HISTORY","ロール履歴"),right+20,114,240,24,12,Muted,true);
    star(right+23,155,4,Mint); label(t("THIS SESSION / UTC","今回の起動中 / UTC"),right+35,147,220,20,10,Mint);
    const int logVisible=std::max(1,static_cast<int>((h-402)/56));
    const int logStart=std::clamp(f.logScroll,0,std::max(0,static_cast<int>(f.logs.size())-logVisible));
    if (f.logs.empty()) {
        label(t("Your first aura is waiting.","最初のオーラが待っています。"),right+20,207,250,25,16,White);
        label(t("Press ROLL to begin.","ロールを押して始めましょう。"),right+20,237,250,24,12,Muted);
    }
    for (int row=0;row<logVisible && row+logStart<static_cast<int>(f.logs.size());++row) {
        const auto& e=f.logs[f.logs.size()-1-static_cast<std::size_t>(row+logStart)]; const auto& d=defs[e.aura]; const float y=182+row*56.f;
        star(right+23,y+12,5.5f,tint(d.color)); label(name(e.aura),right+38,y,169,22,15,tint(d.color),true);
        label("#"+formatNumber(e.roll),right+214,y+3,58,19,10,Muted);
        label(oddsLabel(defs,e.aura),right+38,y+24,169,18,10,Muted);
        label(e.time.size()>=19?e.time.substr(11,8):"",right+210,y+24,65,18,10,Muted);
        rect(right+20,y+49,250,1,D2D1::ColorF(.08f,.12f,.15f));
    }
    rect(24,h-166,w-48,100,Panel,14); stroke(24,h-166,w-48,100,Border,14);
    label(t("TOTAL ROLLS","総ロール数"),44,h-148,140,20,10,Muted,true);
    label(formatNumber(f.progress.rolls),44,h-125,170,40,30,White,true);
    label(t("BEST DISCOVERY","最もレアな発見"),230,h-148,180,20,10,Muted,true);
    label(f.progress.discovered()?name(f.progress.best):t("Waiting for a roll","最初のロールを待っています"),230,h-121,205,26,17,White,true);
    const float bx=w*.5f-105,by=h-146; const bool ready=f.cooldown<=0;
    const bool hover=inside(f.mouseX/scale_,f.mouseY/scale_,bx,by,210,62);
    rect(bx,by,210,62,ready?(hover?D2D1::ColorF(.68f,1,.88f):Mint):D2D1::ColorF(.18f,.31f,.32f),10);
    label(ready?t("ROLL","ロール"):t("ROLLING...","ロール中..."),bx+42,by+13,160,36,24,ready?Panel:White,true);
    cooldown(bx+12,by+70,186,f.cooldown);
    const float ax=bx+228;
    rect(ax,by,134,62,f.autoRoll?D2D1::ColorF(.10f,.22f,.20f):D2D1::ColorF(.08f,.12f,.15f),10);
    stroke(ax,by,134,62,f.autoRoll?Mint:Border,10);
    label(t("AUTO ROLL","自動ロール"),ax+21,by+12,110,22,12,f.autoRoll?Mint:White,true);
    label(f.autoRoll?t("ON / F","オン / F"):t("OFF / F","オフ / F"),ax+35,by+34,100,20,10,Muted);
    rect(w-179,by,130,62,D2D1::ColorF(.08f,.12f,.15f),10);
    label(t("OPEN LOGS","ログを開く"),w-158,by+14,110,24,12,White,true);
    text(L"CSV / UTC",w-147,by+36,100,18,10,Muted);
    label(t("SPACE / ROLL   WASD / MOVE   RIGHT DRAG / ORBIT   WHEEL / ZOOM   M / PLANETS   F1 / DEBUG",
        "SPACE / ロール   WASD / 移動   右ドラッグ / 回転   ホイール / ズーム   M / 惑星   F1 / デバッグ"),24,h-39,w-200,23,11,Muted);
    std::ostringstream luckText; luckText<<t("LUCK ","幸運 ")<<std::fixed<<std::setprecision(f.roller.luck()>=1000?0:2)<<f.roller.luck()<<"x";
    label(luckText.str(),w-150,h-39,134,22,11,Mint,true);
    if (!f.notice.empty()) {
        rect(276,174,w-650,56,D2D1::ColorF(.23f,.13f,.10f,.94f),8);
        label(f.notice,288,188,w-680,28,12,D2D1::ColorF(1,.75f,.56f));
    }
    if(f.stageSelectOpen) stageSelectUi(f);
    if(f.stageTravelProgress>=0) stageTravelUi(f);
}
void Renderer::stageSelectUi(const Frame& f) {
    // この画面は描画だけを担当します。クリック判定はRenderer::hit、移動はAppが担当。
    // 描画と判定でstageSelectLayout()を共有し、ボタン位置のずれを防ぎます。
    const auto language=f.presentation.settings.language;
    auto t=[&](const char* en,const char* ja){return std::string(tr(language,en,ja));};
    const auto layout=stageSelectLayout(logicalW_,logicalH_);
    const float x=layout.x,y=layout.y,w=layout.width;
    rect(0,80,logicalW_,logicalH_-80,D2D1::ColorF(.005f,.012f,.025f,.68f));
    rect(x,y,w,layout.height,D2D1::ColorF(.025f,.047f,.068f,.99f),16);
    stroke(x,y,w,layout.height,D2D1::ColorF(.19f,.36f,.37f),16);
    star(x+32,y+31,8,Mint,false);
    label(t("ROCKET / FLIGHT TERMINAL","ロケット / 惑星ターミナル"),x+50,y+20,w-122,30,19,White,true);
    const bool closeHover=inside(f.mouseX/scale_,f.mouseY/scale_,x+w-48,y+16,30,30);
    rect(x+w-48,y+16,30,30,closeHover?D2D1::ColorF(.16f,.28f,.30f):Panel,8);
    text(L"×",x+w-48,y+18,30,26,20,White,false,true);
    label(t("Choose your next planet. Your aura travels with you.","次の惑星を選んで出発。装備したオーラも一緒に移動します。"),x+22,y+60,w-44,23,12,Muted);
    label(t("DESTINATION","目的地"),x+24,y+89,w-48,18,9,Mint,true);
    constexpr std::array<Color,StageCount> colors={Color{.34f,.85f,.62f},Color{.95f,.73f,.38f},Color{.22f,.68f,.95f},Color{.32f,.69f,.35f},Color{1.f,.35f,.15f},Color{1.f,.83f,.20f}};
    const std::array<std::string,StageCount> descriptions={
        t("A quiet garden beneath the stars","星空の下、穏やかな庭園"),t("Golden dunes and sunlit ruins","黄金の砂丘と、陽光に包まれた遺跡"),
        t("Blue waves around a sandy island","青い波に囲まれた砂浜の島"),t("A woodland of towering green trees","大きな木々に囲まれた緑の森"),t("Volcanic rock and glowing lava","黒い岩と、赤く輝く溶岩"),t("A golden star / four special aura drops","黄色い星の上 / 4種の特別なオーラ")};
    for(unsigned i=0;i<StageCount;++i) {
        const auto destination=static_cast<Biome>(i);
        const bool current=destination==f.presentation.settings.biome;
        const float rowY=y+112+i*64.f;
        const bool hover=!current && inside(f.mouseX/scale_,f.mouseY/scale_,x+20,rowY,w-40,56);
        rect(x+20,rowY,w-40,56,hover?D2D1::ColorF(.08f,.17f,.20f):D2D1::ColorF(.043f,.080f,.105f),9);
        stroke(x+20,rowY,w-40,56,hover?Mint:current?D2D1::ColorF(.20f,.39f,.35f):Border,9);
        if(destination==Biome::Extra) star(x+49,rowY+28,18,tint(colors[i]),true);
        else {
        brush_->SetColor(tint(colors[i],.18f)); d2d_->FillEllipse(D2D1::Ellipse({x+49,rowY+28},20,20),brush_.Get());
        brush_->SetColor(tint(colors[i])); d2d_->FillEllipse(D2D1::Ellipse({x+49,rowY+28},13,13),brush_.Get());
        brush_->SetColor(D2D1::ColorF(.02f,.05f,.08f,.5f)); d2d_->FillEllipse(D2D1::Ellipse({x+54,rowY+24},10,13),brush_.Get());
        star(x+40,rowY+19,2.3f,White);
        }
        label(stageDisplayName(destination,language),x+81,rowY+8,w-244,23,14,White,true);
        label(descriptions[i],x+81,rowY+33,w-244,18,10,Muted);
        label(current?t("YOU ARE HERE","現在地"):t("LAUNCH  >","出発  >"),x+w-149,rowY+19,104,23,11,current?Muted:Mint,true,true);
    }
    label(t("M / ESC / CLOSE     ·     No roll or inventory changes during travel","M / ESC / 閉じる     ・     移動で所持オーラやロール回数は変わりません"),x+22,y+112+StageCount*64.f+20,w-44,23,10,Muted,false,true);
}
void Renderer::stageTravelUi(const Frame& f) {
    // 半分の時点でAppが惑星を交換します。その付近は背景を完全に覆い、切替を隠します。
    // pは0=離陸、0.5=宇宙を移動、1=着陸。所要時間はtuning/stages.hppを参照。
    const float p=std::clamp(f.stageTravelProgress,0.f,1.f),w=logicalW_,h=logicalH_;
    const float opacity=std::clamp(std::min(p/.10f,(1-p)/.12f),0.f,1.f);
    if(opacity<=0) return;
    const auto language=f.presentation.settings.language;
    auto t=[&](const char* en,const char* ja){return std::string(tr(language,en,ja));};
    rect(0,0,w,h,D2D1::ColorF(.009f,.018f,.040f,opacity));
    // 低コストな2Dの星空。ロケットと反対方向へ流れることで移動感を出します。
    for(unsigned i=0;i<70;++i) {
        const float sx=std::fmod(i*193.73f+91.f,w),sy=std::fmod(i*97.21f+p*h*2.f,h);
        const float glow=(.25f+.50f*(.5f+.5f*std::sin(i*3.1f+f.time*1.4f)))*opacity;
        star(sx,sy,i%5==0?3.f:1.5f,D2D1::ColorF(.65f,.82f,1.f,glow));
        if(p>.22f && p<.72f && i%3==0) {
            brush_->SetColor(D2D1::ColorF(.43f,.70f,.94f,glow*.35f));
            d2d_->DrawLine({sx,sy+6},{sx,sy+26},brush_.Get(),1,roundStroke_.Get());
        }
    }
    const float cx=w*.5f;
    const float lift=std::clamp(p/.27f,0.f,1.f),land=std::clamp((p-.76f)/.24f,0.f,1.f);
    const float cy=h*(.65f-.25f*std::sin(lift*Pi*.5f)+.25f*land*land)+std::sin(f.time*5.f)*2;
    // 形は単純な図形の組合せ。縦長の白い機体、青緑の窓、左右の翼、エンジンの炎。
    auto polygon=[&](std::initializer_list<D2D1_POINT_2F> points,D2D1_COLOR_F color) {
        ComPtr<ID2D1PathGeometry> geometry; checked(d2dFactory_->CreatePathGeometry(&geometry),"Rocket path");
        ComPtr<ID2D1GeometrySink> sink; checked(geometry->Open(&sink),"Rocket sink");
        auto first=points.begin(); sink->BeginFigure(*first,D2D1_FIGURE_BEGIN_FILLED);
        for(auto it=first+1;it!=points.end();++it) sink->AddLine(*it);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED); checked(sink->Close(),"Close rocket");
        brush_->SetColor(color); d2d_->FillGeometry(geometry.Get(),brush_.Get());
    };
    const float flame=34+9*std::sin(f.time*37.f);
    if(p<.93f) {
        polygon({{cx-13,cy+30},{cx,cy+73+flame},{cx+13,cy+30}},D2D1::ColorF(.36f,.95f,.85f,opacity*.25f));
        polygon({{cx-8,cy+30},{cx,cy+52+flame*.45f},{cx+8,cy+30}},D2D1::ColorF(.80f,1,.91f,opacity*.9f));
    }
    polygon({{cx-17,cy+2},{cx-34,cy+36},{cx-14,cy+25}},D2D1::ColorF(.27f,.78f,.72f,opacity));
    polygon({{cx+17,cy+2},{cx+34,cy+36},{cx+14,cy+25}},D2D1::ColorF(.27f,.78f,.72f,opacity));
    rect(cx-17,cy-24,34,57,D2D1::ColorF(.91f,.95f,.96f,opacity),15);
    polygon({{cx-16,cy-15},{cx,cy-54},{cx+16,cy-15}},D2D1::ColorF(.94f,.98f,1.f,opacity));
    brush_->SetColor(D2D1::ColorF(.05f,.20f,.29f,opacity)); d2d_->FillEllipse(D2D1::Ellipse({cx,cy-2},10,10),brush_.Get());
    brush_->SetColor(D2D1::ColorF(.48f,.96f,.84f,opacity)); d2d_->FillEllipse(D2D1::Ellipse({cx-2,cy-4},6,6),brush_.Get());
    rect(cx-11,cy+29,22,8,D2D1::ColorF(.13f,.24f,.31f,opacity),2);
    label(p<.27f?t("LIFT OFF","離陸中"):p<.76f?t("CROSSING THE STARS","星の海を移動中"):t("ARRIVING","着陸中"),cx-270,h*.19f,540,32,19,D2D1::ColorF(.76f,.96f,.94f,opacity),true,true);
    label(stageDisplayName(f.stageTravelTarget,language),cx-320,h*.19f+43,640,40,28,D2D1::ColorF(.96f,.98f,1,opacity),true,true);
    rect(cx-160,h*.82f,320,2,D2D1::ColorF(.15f,.28f,.37f,opacity),1);
    rect(cx-160,h*.82f,320*p,2,D2D1::ColorF(.50f,.96f,.83f,opacity),1);
    star(cx-160+320*p,h*.82f,6,D2D1::ColorF(.60f,1,.90f,opacity),true,p*240);
}
} // namespace aura
