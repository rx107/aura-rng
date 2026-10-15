#include "render/renderer.hpp"
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

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace aura {
namespace {
constexpr float Pi = 3.14159265359f;
const D2D1_COLOR_F White = D2D1::ColorF(.91f,.95f,.98f), Muted = D2D1::ColorF(.48f,.57f,.65f);
const D2D1_COLOR_F Mint = D2D1::ColorF(.55f,.96f,.82f), Panel = D2D1::ColorF(.047f,.070f,.094f);
const D2D1_COLOR_F Border = D2D1::ColorF(.13f,.19f,.23f);
D2D1_COLOR_F tint(Color c, float alpha=1) { return {c.r,c.g,c.b,alpha}; }
bool inside(float x,float y,float rx,float ry,float w,float h) { return x>=rx && x<rx+w && y>=ry && y<ry+h; }
std::optional<std::string> chooseAsset(HWND owner,bool vrm) {
    ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) return {};
    const COMDLG_FILTERSPEC filter=vrm?COMDLG_FILTERSPEC{L"VRM avatar (*.vrm)",L"*.vrm"}:COMDLG_FILTERSPEC{L"Audio (*.mp3;*.wav;*.m4a)",L"*.mp3;*.wav;*.m4a"};
    dialog->SetFileTypes(1,&filter); dialog->SetTitle(vrm?L"Select VRM avatar / VRMアバターを選択":L"Select Singularity BGM / BGMを選択");
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
            const char* biomes[]={t("Normal / Quiet Garden","通常 / 静かな庭園"),t("Desert / Sunlit Dunes","砂漠 / 陽光の砂丘")};
            if(ImGui::Combo(t("Biome","バイオーム"),&biome,biomes,2)) debugActions_.biome=static_cast<Biome>(biome);
            ImGui::TextDisabled("%s",t("Biome currently changes the scenery.","現在のバイオーム変更は景観に反映されます。"));
            ImGui::Separator();
            ImGui::TextUnformatted(t("CURRENT ODDS / WITH LUCK","現在の排出確率 / Luck反映後"));
            for(std::size_t i=0;i<f.roller.catalog().size();++i) {
                if(f.roller.catalog()[i].previewOnly) continue;
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
            for(std::size_t i=0;i<f.roller.catalog().size();++i) if(f.roller.catalog()[i].id=="pixelation") {
                ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.04f,.35f,.27f,1));
                if(ImGui::Button(t("Preview Pixelation / prototype","Pixelation を表示 / 試作"),{-1,36})) debugActions_.preview=i;
                ImGui::PopStyleColor();
                ImGui::TextWrapped("%s",t("Animated pixel fragments, hologram panels and a rotating wireframe cube. Visual prototype: excluded from rolls.",
                    "色が移り変わるピクセル片、ホログラムパネル、回転するワイヤー立方体。見た目の試作のため、ロールの抽選対象外です。"));
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
                    const auto row=displayName(d,language)+"  |  "+oddsLabel(catalog,i)+"  |  x"+
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
                ImGui::TextWrapped("%s",t("The selected track loops only while Singularity is displayed, including Debug Preview.","選択した曲はSingularity表示中のみループします。デバッグプレビューにも対応します。"));
                if(ImGui::Button(t("Choose BGM...","BGMを選択..."),{-1,32})) if(auto path=chooseAsset(window_,false)) debugActions_.bgmPath=*path;
                ImGui::TextWrapped("%s",f.presentation.settings.bgmPath.empty()?t("Bundled: Tides of the Outer Reach","同梱BGM: Tides of the Outer Reach"):f.presentation.settings.bgmPath.c_str());
                float volume=f.presentation.settings.volume;
                if(ImGui::SliderFloat(t("Volume","音量"),&volume,0,1,"%.2f")) debugActions_.volume=volume;
                ImGui::TextWrapped("%s",f.musicStatus.c_str());
                if(ImGui::Button(t("Use bundled BGM","同梱BGMを使用"))) debugActions_.bgmPath=std::string{};
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
    const int visible=std::max(1,static_cast<int>((h-430)/48));
    const int start=std::clamp(f.collectionScroll,0,std::max(0,static_cast<int>(defs.size())-visible));
    for (int row=0;row<visible && start+row<static_cast<int>(defs.size());++row) {
        const auto i=static_cast<std::size_t>(start+row); const float y=242+row*48.f;
        const bool owned=!defs[i].previewOnly && f.progress.counts[i]!=0,selected=!defs[i].previewOnly && i==f.progress.equipped;
        if (selected) rect(34,y-5,196,43,D2D1::ColorF(.09f,.15f,.17f),6);
        star(47,y+11,7.5f,tint(defs[i].color,owned?1.f:.42f),owned);
        label(name(i),64,y,155,21,14,owned?White:Muted,selected);
        label(oddsLabel(defs,i)+(defs[i].previewOnly?"":owned?"   x"+formatNumber(f.progress.counts[i]):"   "+t("LOCKED","未所持")),64,y+22,157,18,9,Muted);
    }
    if (static_cast<int>(defs.size())>visible) {
        rect(234,244,2,h-448,Border,1);
        rect(234,244+(h-448)*static_cast<float>(start)/static_cast<float>(defs.size()),2,
            (h-448)*static_cast<float>(visible)/static_cast<float>(defs.size()),Mint,1);
    }
    stroke(256,96,w-596,h-280,Border,14);
    label(t("SANCTUARY","サンクチュアリ"),276,116,190,25,12,White,true);
    const bool desert=f.presentation.settings.biome==Biome::Desert;
    label(desert?t("02 / SUNLIT DUNES","02 / 陽光の砂丘"):t("01 / THE QUIET GARDEN","01 / 静かな庭園"),276,140,240,20,10,Muted);
    rect(w-487,114,130,26,D2D1::ColorF(.05f,.10f,.12f,.90f),13);
    label(desert?t("BIOME / DESERT","バイオーム / 砂漠"):t("BIOME / NORMAL","バイオーム / 通常"),w-475,121,125,18,10,Mint);
    const float titleY=h-281;
    label(f.presentation.preview?t("DEBUG PREVIEW","デバッグプレビュー"):t("EQUIPPED AURA","装備中のオーラ"),276,titleY,300,20,10,Muted,true);
    label(name(shown),276,titleY+23,w-650,45,32,tint(equipped.color),true);
    label(oddsLabel(defs,shown)+(equipped.id=="singularity"?"   /   Aura Level "+std::to_string(f.presentation.level(f.progress,defs))+" / 3":""),276,titleY+67,w-650,25,12,Muted);
    if(nameplateVisible_) {
        d2d_->PushAxisAlignedClip(D2D1::RectF(258,172,w-342,h-294),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float width=std::min(440.f,w-624),x=std::clamp(nameplate_.x-width*.5f,264.f,w-348-width),y=nameplate_.y;
        label(name(shown),x+1,y+1,width,32,23,D2D1::ColorF(0,0,0,.8f),true,true);
        label(name(shown),x,y,width,32,23,tint(equipped.color),true,true);
        label(oddsInLabel(defs,shown),x,y+31,width,25,14,White,false,true);
        if(equipped.id=="singularity") label("Aura Level "+std::to_string(f.presentation.level(f.progress,defs)),x,y+56,width,24,12,Mint,true,true);
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
    label(f.progress.rolls?name(f.progress.best):t("Waiting for a roll","最初のロールを待っています"),230,h-121,205,26,17,White,true);
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
    label(t("SPACE / ROLL   WASD / MOVE   RIGHT DRAG / ORBIT   WHEEL / ZOOM   F1 / DEBUG",
        "SPACE / ロール   WASD / 移動   右ドラッグ / 回転   ホイール / ズーム   F1 / デバッグ"),24,h-39,w-200,23,11,Muted);
    std::ostringstream luckText; luckText<<t("LUCK ","幸運 ")<<std::fixed<<std::setprecision(f.roller.luck()>=1000?0:2)<<f.roller.luck()<<"x";
    label(luckText.str(),w-150,h-39,134,22,11,Mint,true);
    if (!f.notice.empty()) {
        rect(276,174,w-650,56,D2D1::ColorF(.23f,.13f,.10f,.94f),8);
        label(f.notice,288,188,w-680,28,12,D2D1::ColorF(1,.75f,.56f));
    }
}
} // namespace aura
