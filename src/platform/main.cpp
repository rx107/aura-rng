#include "core/aura.hpp"
#include "render/renderer.hpp"
#include "platform/audio.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {
using Clock=std::chrono::steady_clock;
struct Options {
    bool smoke{},warp{},debug{};
    std::filesystem::path capture, report, saveDir;
    std::filesystem::path vrm,bgm;
    std::uint64_t seed{};
};
Options options() {
    Options result; std::random_device random;
    result.seed=(static_cast<std::uint64_t>(random())<<32)^random();
    int count{}; LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args) throw std::runtime_error("Cannot parse command line");
    for(int i=1;i<count;++i) {
        std::wstring a=args[i];
        if(a==L"--smoke-test") result.smoke=true;
        else if(a==L"--warp") result.warp=true;
        else if(a==L"--debug") result.debug=true;
        else if(a.starts_with(L"--vrm=")) result.vrm=a.substr(6);
        else if(a.starts_with(L"--bgm=")) result.bgm=a.substr(6);
        else if(a.starts_with(L"--capture=")) result.capture=a.substr(10);
        else if(a.starts_with(L"--report=")) result.report=a.substr(9);
        else if(a.starts_with(L"--save-dir=")) result.saveDir=a.substr(11);
        else if(a.starts_with(L"--seed=")) result.seed=std::stoull(a.substr(7));
        else { LocalFree(args); throw std::runtime_error("Unknown command line option"); }
    }
    LocalFree(args);
    if(result.smoke && result.saveDir.empty()) throw std::runtime_error("Smoke test requires an isolated --save-dir");
    return result;
}
std::filesystem::path executableDir() {
    std::wstring buffer(32768,L'\0');
    const DWORD size=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!size || size==buffer.size()) throw std::runtime_error("Cannot locate executable");
    buffer.resize(size); return std::filesystem::path(buffer).parent_path();
}
std::filesystem::path localData() {
    PWSTR folder{}; aura::checked(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder),"Local app data");
    std::filesystem::path result=std::filesystem::path(folder)/L"AuraRng"; CoTaskMemFree(folder); return result;
}
class App {
public:
    App(Options opts):opts_(std::move(opts)),root_(executableDir()),
        saves_(opts_.saveDir.empty()?localData():opts_.saveDir),
        roller_(aura::loadCatalog(root_/L"data"/L"auras.csv"),opts_.seed),progress_(roller_.catalog().size()) {
        std::filesystem::create_directories(saves_);
        // Validate UTF-8 at startup before entering the window procedure.
        for(const auto& d:roller_.catalog()) aura::widen(d.name);
        try { progress_=aura::loadProgress(saves_/L"progress.txt",roller_.catalog()); }
        catch(const std::exception&) {
            auto backup=saves_/L"progress.txt"; backup+=L".corrupt-"+std::to_wstring(GetTickCount64());
            std::filesystem::copy_file(saves_/L"progress.txt",backup);
            notice_="Save could not be read. A backup was kept in the log folder.";
        }
        log_.reserve(200);
        try { presentation_.settings=aura::loadSettings(saves_/L"settings.txt"); }
        catch(const std::exception&) { notice_="Settings could not be read. Defaults are in use."; }
        auto utf8=[](const std::filesystem::path& p) {const auto u=p.u8string(); return std::string(reinterpret_cast<const char*>(u.data()),u.size());};
        if(!opts_.vrm.empty()) presentation_.settings.vrmPath=utf8(opts_.vrm);
        if(!opts_.bgm.empty()) presentation_.settings.bgmPath=utf8(opts_.bgm);
        roller_.setLuck(presentation_.settings.luck);
    }
    aura::Frame frame() const {
        std::string notice=notice_;
        if(presentation_.settings.language==aura::Language::Japanese) {
            if(notice_=="Save could not be read. A backup was kept in the log folder.") notice="セーブを読めませんでした。ログフォルダーにバックアップを保持しています。";
            else if(notice_=="Settings could not be read. Defaults are in use.") notice="設定を読めなかったため初期設定で起動しています。";
            else if(notice_=="Progress could not be saved. Check the log folder / free disk space.") notice="保存できませんでした。保存先とディスクの空き容量を確認してください。";
            else if(notice_=="Roll log could not be written. Check the log folder / free disk space.") notice="ロール履歴を保存できませんでした。保存先と空き容量を確認してください。";
            else if(notice_=="Could not open the log folder.") notice="ログフォルダーを開けませんでした。";
            else if(notice_=="Settings could not be saved.") notice="言語と服の色の設定を保存できませんでした。";
        }
        return {roller_,progress_,presentation_,log_,time_,cooldown_,x_,z_,facing_,walk_,yaw_,distance_,
            mouseX_,mouseY_,fps_,auto_,collectionScroll_,logScroll_,notice,music_.status(),pitch_,movement_};
    }
    void create(HINSTANCE instance) {
        WNDCLASSEXW wc{sizeof(wc)}; wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=proc;
        wc.hInstance=instance; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION); wc.lpszClassName=L"AuraRngWindow";
        if(!RegisterClassExW(&wc)) throw std::runtime_error("Cannot register window");
        RECT r{0,0,1440,900}; AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0);
        window_=CreateWindowExW(0,wc.lpszClassName,L"Star's RNG | Alpha 0.1",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,
            r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,this);
        if(!window_) throw std::runtime_error("Cannot create window");
        renderer_=std::make_unique<aura::Renderer>(); renderer_->initialize(window_,opts_.warp);
        if(opts_.debug) renderer_->toggleDebug();
        pendingWidth_=pendingHeight_=0;
        if(!opts_.smoke) ShowWindow(window_,SW_SHOW);
        last_=Clock::now();
    }
    int run() {
        if(opts_.smoke) return smoke();
        MSG msg{};
        while(!quitting_) {
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                if(msg.message==WM_QUIT) { quitting_=true; break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if(quitting_) break;
            const auto now=Clock::now(); const float dt=std::chrono::duration<float>(now-last_).count(); last_=now;
            update(std::min(dt,.25f));
            if(minimized_) { MsgWaitForMultipleObjects(0,nullptr,FALSE,auto_?100:250,QS_ALLINPUT); continue; }
            if(!foreground_) {
                const auto elapsed=std::chrono::duration<float>(now-lastBackground_).count();
                if(elapsed<1.f/15.f) { MsgWaitForMultipleObjects(0,nullptr,FALSE,30,QS_ALLINPUT); continue; }
                lastBackground_=now;
            }
            render(true);
            if(!opts_.capture.empty()) { renderer_->capture(opts_.capture); opts_.capture.clear(); }
        }
        persist(); return 0;
    }
    ~App() { renderer_.reset(); if(window_ && IsWindow(window_)) DestroyWindow(window_); }
private:
    void persist() {
        try { aura::saveProgress(saves_/L"progress.txt",progress_,roller_.catalog()); }
        catch(const std::exception&) { notice_="Progress could not be saved. Check the log folder / free disk space."; }
    }
    void roll() {
        if(cooldown_>0) return;
        const auto result=roller_.roll(); progress_.record(result,roller_.catalog());
        aura::LogEntry entry{progress_.rolls,aura::utcNow(),result};
        if(log_.size()==200) log_.erase(log_.begin());
        log_.push_back(entry); logScroll_=0; cooldown_=aura::RollCooldown;
        try { aura::appendLog(saves_/L"rolls.csv",entry,roller_.catalog()[result]); }
        catch(const std::exception&) { notice_="Roll log could not be written. Check the log folder / free disk space."; }
        persist();
    }
    void action(aura::Hit hit) {
        switch(hit.action) {
        case aura::Action::Roll: roll(); break;
        case aura::Action::Auto: auto_=!auto_; break;
        case aura::Action::Logs: {
            const auto result=ShellExecuteW(window_,L"open",saves_.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            if(reinterpret_cast<INT_PTR>(result)<=32) notice_="Could not open the log folder.";
            break;
        }
        case aura::Action::Equip:
            if(hit.index<progress_.counts.size() && progress_.counts[hit.index]) {
                progress_.equipped=hit.index; presentation_.preview.reset(); presentation_.singularityLevel.reset(); persist();
            }
            break;
        case aura::Action::Debug:
            renderer_->toggleDebug(); keys_.fill(false); dragging_=false; ReleaseCapture();
            break;
        default: break;
        }
    }
    void applyDebugActions(const aura::DebugActions& commands) {
        if(commands.preview) presentation_.showPreview(*commands.preview,roller_.catalog().size(),auto_);
        if(commands.restore) {presentation_.preview.reset(); presentation_.singularityLevel.reset();}
        if(commands.auraLevel) {
            presentation_.singularityLevel=std::min(3u,*commands.auraLevel);
            const auto& catalog=roller_.catalog();
            for(std::size_t i=0;i<catalog.size();++i) if(catalog[i].id=="singularity") presentation_.showPreview(i,catalog.size(),auto_);
        }
        if(commands.actualLevel) presentation_.singularityLevel.reset();
        if(commands.roll) roll();
        if(commands.toggleAuto) auto_=!auto_;
        if(commands.language) presentation_.settings.language=*commands.language;
        if(commands.shirt) presentation_.settings.shirt=*commands.shirt;
        if(commands.luck) {roller_.setLuck(*commands.luck); presentation_.settings.luck=roller_.luck();}
        if(commands.avatar) presentation_.settings.avatar=*commands.avatar;
        if(commands.biome) presentation_.settings.biome=*commands.biome;
        if(commands.vrmPath) {presentation_.settings.vrmPath=*commands.vrmPath; renderer_->reloadVrm();}
        if(commands.bgmPath) {presentation_.settings.bgmPath=*commands.bgmPath; music_.reload();}
        if(commands.volume) presentation_.settings.volume=*commands.volume;
        if(commands.language || commands.shirt || commands.luck || commands.avatar || commands.biome || commands.vrmPath || commands.bgmPath || commands.volume) {
            try { aura::saveSettings(saves_/L"settings.txt",presentation_.settings); }
            catch(const std::exception&) { notice_="Settings could not be saved."; }
        }
    }
    void update(float dt) {
        time_+=dt; cooldown_=std::max(0.f,cooldown_-dt);
        if(pending_) { action(*pending_); pending_.reset(); }
        if(auto_ && cooldown_<=0) roll();
        const auto& s=presentation_.settings;
        const auto bgm=s.bgmPath.empty()?root_/L"data"/L"audio"/L"Tides_of_the_Outer_Reach.mp3":std::filesystem::path(aura::widen(s.bgmPath));
        music_.update(bgm,roller_.catalog()[presentation_.shown(progress_)].id=="singularity",s.volume,dt);
        bool moving=false;
        if((foreground_ || opts_.smoke) && !renderer_->wantsKeyboard()) {
            float mx=0,mz=0;
            if(keys_['W'] || keys_[VK_UP]) mz+=1;
            if(keys_['S'] || keys_[VK_DOWN]) mz-=1;
            if(keys_['A'] || keys_[VK_LEFT]) mx-=1;
            if(keys_['D'] || keys_[VK_RIGHT]) mx+=1;
            if(mx || mz) {
                const float length=std::sqrt(mx*mx+mz*mz); mx/=length; mz/=length;
                const float dx=mx*std::cos(yaw_)-mz*std::sin(yaw_);
                const float dz=mx*std::sin(yaw_)+mz*std::cos(yaw_);
                const float beforeX=x_,beforeZ=z_;
                x_=std::clamp(x_+dx*dt*3.f,-aura::StageWalkLimit,aura::StageWalkLimit); z_=std::clamp(z_+dz*dt*3.f,-aura::StageWalkLimit,aura::StageWalkLimit);
                moving=std::abs(x_-beforeX)+std::abs(z_-beforeZ)>.0001f;
                facing_=std::atan2(-dx,-dz); walk_+=dt*3;
            }
        }
        const float target=moving?1.f:0.f;
        movement_+=std::clamp(target-movement_,-dt*8,dt*8);
    }
    void render(bool present) {
        try {
            if(pendingWidth_ && pendingHeight_) {
                renderer_->resize(pendingWidth_,pendingHeight_); pendingWidth_=pendingHeight_=0;
            }
            renderer_->draw(frame(),present);
        }
        catch(const std::exception&) {
            // Rebuild both D3D and D2D resources after device loss; game state survives.
            const bool debugWasOpen=renderer_->debugOpen(); renderer_.reset();
            renderer_=std::make_unique<aura::Renderer>(); renderer_->initialize(window_,opts_.warp); renderer_->draw(frame(),present);
            if(debugWasOpen) renderer_->toggleDebug();
        }
        applyDebugActions(renderer_->takeDebugActions());
        ++fpsFrames_; const auto now=Clock::now(); const float seconds=std::chrono::duration<float>(now-fpsStart_).count();
        if(seconds>=.5f) { fps_=fpsFrames_/seconds; fpsFrames_=0; fpsStart_=now; }
    }
    int smoke() {
        const auto startingRolls=progress_.rolls;
        const auto initialEquipped=progress_.equipped;
        // Exercise the actual window input path, cooldown, auto toggle and resizing.
        RECT client{}; GetClientRect(window_,&client);
        const int buttonX=client.right/2;
        const int buttonY=static_cast<int>(client.bottom-115.f*renderer_->scale());
        SendMessageW(window_,WM_LBUTTONUP,0,MAKELPARAM(buttonX,buttonY)); update(.01f);
        if(progress_.rolls!=startingRolls+1) throw std::runtime_error("Manual roll smoke check failed");
        SendMessageW(window_,WM_KEYDOWN,VK_SPACE,0); update(.01f);
        if(progress_.rolls!=startingRolls+1) throw std::runtime_error("Cooldown smoke check failed");
        SendMessageW(window_,WM_KEYDOWN,'F',0); update(1.4f);
        if(!auto_ || progress_.rolls!=startingRolls+2) throw std::runtime_error("Auto roll smoke check failed");
        if(progress_.equipped!=initialEquipped) throw std::runtime_error("Rolling changed the Collection aura");
        SendMessageW(window_,WM_KEYDOWN,'F',0); update(.01f);
        SendMessageW(window_,WM_KEYUP,'F',0);
        SendMessageW(window_,WM_KEYDOWN,'W',0); update(.2f); SendMessageW(window_,WM_KEYUP,'W',0);
        if(std::abs(x_)+std::abs(z_)<.1f) throw std::runtime_error("Movement smoke check failed");
        x_=12; z_=0; SendMessageW(window_,WM_KEYDOWN,'D',0); update(.2f); SendMessageW(window_,WM_KEYUP,'D',0);
        if(x_<12) throw std::runtime_error("Expanded stage still uses the old movement boundary");
        x_=z_=0;
        auto checkOrbit=[&] {
            const float yawBefore=yaw_,pitchBefore=pitch_;
            SendMessageW(window_,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(540,340));
            SendMessageW(window_,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(580,360));
            if(!dragging_ || yaw_<=yawBefore || pitch_<=pitchBefore) throw std::runtime_error("Right-drag orbit failed");
            SendMessageW(window_,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(1000,410)); // Continue over the debug window.
            SendMessageW(window_,WM_RBUTTONUP,0,MAKELPARAM(1000,410));
            const float released=yaw_;
            SendMessageW(window_,WM_MOUSEMOVE,0,MAKELPARAM(1020,420));
            if(dragging_ || yaw_!=released) throw std::runtime_error("Right-drag release failed");
            yaw_=yawBefore; pitch_=pitchBefore;
        };
        checkOrbit();
        if(!renderer_->debugOpen()) renderer_->toggleDebug();
        render(false); render(false); checkOrbit();
        SendMessageW(window_,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(540,340));
        SendMessageW(window_,WM_CAPTURECHANGED,0,0);
        if(dragging_) throw std::runtime_error("Orbit capture loss did not cancel drag");
        renderer_->toggleDebug();
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1000,700)); render(false);
        if(!opts_.capture.empty()) {
            auto captureSmall=opts_.capture.parent_path()/(opts_.capture.stem().wstring()+L"-small.png"); renderer_->capture(captureSmall);
        }
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1440,900)); render(false);
        for(int i=0;i<18;++i) { cooldown_=0; roll(); }
        action({aura::Action::Equip,progress_.best});
        const auto chosen=progress_.equipped;
        for(std::size_t i=0;i<roller_.catalog().size();++i) {
            aura::DebugActions command; command.preview=i; applyDebugActions(command); render(false);
        }
        const auto beforePreview=progress_.counts;
        const auto logSize=log_.size();
        const auto logBytes=std::filesystem::file_size(saves_/L"rolls.csv");
        auto_=true; aura::DebugActions previewCommand; previewCommand.preview=roller_.catalog().size()-1; applyDebugActions(previewCommand);
        if(auto_ || progress_.counts!=beforePreview || log_.size()!=logSize ||
            std::filesystem::file_size(saves_/L"rolls.csv")!=logBytes || progress_.equipped!=chosen)
            throw std::runtime_error("Debug preview modified collection/history or did not stop AutoRoll");
        cooldown_=0; roll();
        if(presentation_.shown(progress_)!=roller_.catalog().size()-1 || progress_.equipped!=chosen)
            throw std::runtime_error("Roll replaced the preview / Collection selection");
        action({aura::Action::Equip,chosen});
        if(presentation_.preview) throw std::runtime_error("Collection did not restore equipped display");
        time_=3.8f; x_=0; z_=0; facing_=0; cooldown_=0; walk_=0;
        fps_=0; fpsFrames_=0; fpsStart_=Clock::now();
        const auto begin=Clock::now();
        for(int i=0;i<60;++i) { time_+=1.f/60; render(false); }
        const float seconds=std::chrono::duration<float>(Clock::now()-begin).count();
        fps_=0;
        render(false);
        if(!opts_.capture.empty()) renderer_->capture(opts_.capture);
        if(!opts_.capture.empty()) {
            auto captureVariant=[&](const wchar_t* suffix) {
                renderer_->capture(opts_.capture.parent_path()/(opts_.capture.stem().wstring()+suffix+L".png"));
            };
            cooldown_=aura::RollCooldown*.55f; time_+=.2f; render(false); captureVariant(L"-cooldown");
            aura::DebugActions settings; settings.language=aura::Language::Japanese; settings.shirt=aura::Color{.95f,.38f,.32f}; applyDebugActions(settings);
            render(false); captureVariant(L"-japanese");
            previewCommand.preview=roller_.catalog().size()-1; applyDebugActions(previewCommand);
            if(!renderer_->debugOpen()) { SendMessageW(window_,WM_KEYDOWN,VK_F1,0); update(0); }
            render(false); render(false); captureVariant(L"-debug");
            if(renderer_->debugOpen()) { SendMessageW(window_,WM_KEYDOWN,VK_F1,0); update(0); }
            presentation_.preview.reset(); cooldown_=0; render(false); captureVariant(L"-complete");
        }
        auto captureExtra=[&](const wchar_t* suffix) {
            if(!opts_.capture.empty()) renderer_->capture(opts_.capture.parent_path()/(opts_.capture.stem().wstring()+suffix+L".png"));
        };
        const auto beforeSettingsRolls=progress_.rolls; const auto beforeSettingsLogs=log_.size();
        double vrmFrameMs=0;
        aura::DebugActions world; world.luck=25; world.biome=aura::Biome::Desert; applyDebugActions(world);
        if(roller_.luck()!=25 || presentation_.settings.biome!=aura::Biome::Desert) throw std::runtime_error("World settings actions failed");
        render(false); captureExtra(L"-desert");
        world.biome=aura::Biome::Normal; applyDebugActions(world); render(false);
        world.biome=aura::Biome::Desert; world.luck=1; applyDebugActions(world);
        previewCommand.preview=roller_.catalog().size()-1; applyDebugActions(previewCommand); update(.4f); render(false); captureExtra(L"-space");
        for(unsigned level=0;level<=3;++level) {
            aura::DebugActions effect; effect.auraLevel=level; auto_=true; applyDebugActions(effect); render(false);
            if(auto_ || presentation_.level(progress_,roller_.catalog())!=level ||
                (renderer_->starCount()>0)!=(level>0) || renderer_->blackHoleVisible()!=(level>=2)) throw std::runtime_error("Aura Level effect switch failed");
            const auto suffix=L"-level"+std::to_wstring(level); captureExtra(suffix.c_str());
        }
        if(!opts_.bgm.empty()) {
            if(!music_.playing() || !music_.decodedBytes()) throw std::runtime_error("BGM playback failed: "+music_.status());
            aura::DebugActions common; common.preview=0; applyDebugActions(common); update(.6f);
            if(music_.playing()) throw std::runtime_error("BGM did not stop after leaving Singularity");
            aura::DebugActions bundled; bundled.bgmPath=std::string{}; applyDebugActions(bundled);
            applyDebugActions(previewCommand); update(.4f);
            if(!music_.playing()) throw std::runtime_error("BGM did not restart on Singularity");
        }
        if(!opts_.vrm.empty()) {
            aura::DebugActions avatar; avatar.avatar=aura::Avatar::Vrm; applyDebugActions(avatar);
            const auto deadline=Clock::now()+std::chrono::seconds(45);
            do {render(false); Sleep(10);} while(!renderer_->vrmReady() && Clock::now()<deadline && renderer_->vrmLoading());
            if(!renderer_->vrmReady() || renderer_->vrmVertices()==0) throw std::runtime_error("VRM import failed: "+renderer_->vrmStatus());
            aura::DebugActions common; common.preview=0; applyDebugActions(common);
            movement_=0; walk_=0; distance_=7; pitch_=.20f; yaw_=.15f;
            render(false); captureExtra(L"-vrm");
            movement_=1; walk_=.5f; render(false); const auto kneeA=renderer_->vrmBone(4); captureExtra(L"-vrm-walk-a");
            walk_=1.5f; render(false); const auto kneeB=renderer_->vrmBone(4); captureExtra(L"-vrm-walk-b");
            if(!opts_.capture.empty() && renderer_->vrmVertices()>4) {
                for(int i=0;i<24;++i) {
                    time_=5+i*.07f; walk_=i*.21f; render(false);
                    const auto suffix=L"-walk-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
                }
            }
            if(renderer_->vrmVertices()>4 && std::abs(kneeA.z-kneeB.z)<.01f) throw std::runtime_error("VRM walking did not animate leg bones");
            aura::DebugActions space; space.auraLevel=3; applyDebugActions(space); distance_=12; pitch_=.32f;
            walk_=0; movement_=0; render(false); const auto fixedA=renderer_->vrmBone(4); captureExtra(L"-vrm-space");
            movement_=1; walk_=2; render(false); const auto fixedB=renderer_->vrmBone(4);
            if(std::abs(fixedA.x-fixedB.x)+std::abs(fixedA.y-fixedB.y)+std::abs(fixedA.z-fixedB.z)>.00001f)
                throw std::runtime_error("Moving changed the fixed VRM pose");
            const auto vrmBegin=Clock::now();
            for(int i=0;i<60;++i) {time_+=1.f/60; render(false);}
            vrmFrameMs=std::chrono::duration<double,std::milli>(Clock::now()-vrmBegin).count()/60;
            avatar.avatar=aura::Avatar::Block; applyDebugActions(avatar); render(false);
            avatar.avatar=aura::Avatar::Vrm; applyDebugActions(avatar); render(false);
            if(!renderer_->vrmReady()) throw std::runtime_error("VRM switch-back failed");
            avatar.vrmPath=presentation_.settings.vrmPath+".missing"; applyDebugActions(avatar);
            do {render(false); Sleep(10);} while(renderer_->vrmLoading() && Clock::now()<deadline);
            if(renderer_->vrmReady() || renderer_->vrmLoading()) throw std::runtime_error("Missing VRM fallback failed");
            const auto u=opts_.vrm.u8string(); avatar.vrmPath=std::string(reinterpret_cast<const char*>(u.data()),u.size()); applyDebugActions(avatar);
            do {render(false); Sleep(10);} while(!renderer_->vrmReady() && Clock::now()<deadline && renderer_->vrmLoading());
            if(!renderer_->vrmReady()) throw std::runtime_error("VRM reload failed");
        }
        if(progress_.rolls!=beforeSettingsRolls || log_.size()!=beforeSettingsLogs) throw std::runtime_error("World/avatar/audio settings modified roll history");
        persist();
        const auto restored=aura::loadProgress(saves_/L"progress.txt",roller_.catalog());
        if(restored.counts!=progress_.counts) throw std::runtime_error("Progress smoke check failed");
        if(restored.equipped!=chosen) throw std::runtime_error("Collection equipment was not saved");
        if(!opts_.report.empty()) {
            std::ofstream out(opts_.report);
            out<<"PASS: D3D11 + D2D + Dear ImGui, resize, roll, cooldown, AutoRoll, movement, save restore, fixed equipment, preview isolation, Luck, biome switching, right-drag orbit (debug open/closed, release/capture loss)\n"
               <<"Adapter: "<<renderer_->adapterName()<<"\nResolution: 1440x900\nBlock world draw calls: 1\nFinal instances: "
               <<renderer_->instanceCount()<<"\nMean frame submission time: "<<seconds/60*1000
               <<" ms/frame (60 offscreen frames, no vsync)\nRolls: "<<progress_.rolls<<"\n";
            out<<"Effects: PASS / levels 0..3, stars, black holes, no history mutation\nStage: 64 x 64 world units / four times each original dimension\n";
            if(!opts_.vrm.empty()) out<<"VRM: PASS / "<<renderer_->vrmVertices()<<" vertices / "<<renderer_->vrmDraws()<<" additional draw calls / "<<vrmFrameMs
                <<" ms/frame submission / standing, walking, fixed pose during movement, textures, reload and missing-file fallback\n";
            if(!opts_.bgm.empty()) out<<"BGM: PASS / "<<music_.decodedBytes()<<" decoded PCM bytes / supplied and bundled tracks, loop, stop and restart\n";
            if(!out) throw std::runtime_error("Cannot write smoke report");
        }
        return 0;
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        App* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
        }
        if(!app) return DefWindowProcW(window,message,wp,lp);
        if(message==WM_KEYUP && wp<256) app->keys_[wp]=false;
        if(message==WM_KEYDOWN && wp==VK_F1 && !(lp & (1LL<<30))) { app->pending_=aura::Hit{aura::Action::Debug,0}; return 0; }
        // Once a drag starts on the stage it owns the mouse until release or
        // capture loss. ImGui's previous-frame capture flag must not interrupt it.
        if(message==WM_RBUTTONDOWN && app->renderer_ && app->renderer_->canOrbit(
            static_cast<float>(GET_X_LPARAM(lp)),static_cast<float>(GET_Y_LPARAM(lp)))) {
            app->lastMouseX_=GET_X_LPARAM(lp); app->lastMouseY_=GET_Y_LPARAM(lp);
            app->dragging_=true; SetFocus(window); SetCapture(window); return 0;
        }
        if(message==WM_MOUSEMOVE && app->dragging_) {
            const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            app->mouseX_=static_cast<float>(x); app->mouseY_=static_cast<float>(y);
            app->yaw_+=(x-app->lastMouseX_)*.008f;
            app->pitch_=std::clamp(app->pitch_+(y-app->lastMouseY_)*.006f,.12f,1.28f);
            app->lastMouseX_=x; app->lastMouseY_=y; return 0;
        }
        if(message==WM_RBUTTONUP && app->dragging_) {
            app->dragging_=false; ReleaseCapture(); return 0;
        }
        if(message==WM_CAPTURECHANGED) app->dragging_=false;
        if(app->renderer_ && app->renderer_->handleMessage(window,message,wp,lp)) return 0;
        // No disk I/O or throwing renderer calls are made inside the window procedure.
        switch(message) {
        case WM_CLOSE: app->quitting_=true; return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_SIZE: app->minimized_=wp==SIZE_MINIMIZED; app->pendingWidth_=LOWORD(lp); app->pendingHeight_=HIWORD(lp); return 0;
        case WM_GETMINMAXINFO: {
            auto* limits=reinterpret_cast<MINMAXINFO*>(lp); const UINT dpi=GetDpiForWindow(window);
            limits->ptMinTrackSize={MulDiv(960,static_cast<int>(dpi),96),MulDiv(640,static_cast<int>(dpi),96)}; return 0;
        }
        case WM_DPICHANGED: {
            auto* r=reinterpret_cast<RECT*>(lp); SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); return 0;
        }
        case WM_ACTIVATE:
            app->foreground_=LOWORD(wp)!=WA_INACTIVE;
            if(!app->foreground_) { app->keys_.fill(false); app->dragging_=false; ReleaseCapture(); }
            return 0;
        case WM_KEYDOWN:
            if(wp<256) app->keys_[wp]=true;
            if(!(lp & (1LL<<30))) {
                if(wp==VK_SPACE || wp=='R') app->pending_=aura::Hit{aura::Action::Roll,0};
                if(wp=='F') app->pending_=aura::Hit{aura::Action::Auto,0};
                if(wp==VK_ESCAPE) app->quitting_=true;
            }
            return 0;
        case WM_KEYUP: if(wp<256) app->keys_[wp]=false; return 0;
        case WM_LBUTTONUP:
            if(app->renderer_) app->pending_=app->renderer_->hit(static_cast<float>(GET_X_LPARAM(lp)),static_cast<float>(GET_Y_LPARAM(lp)),app->frame());
            return 0;
        case WM_RBUTTONDOWN: return 0;
        case WM_RBUTTONUP: app->dragging_=false; ReleaseCapture(); return 0;
        case WM_CAPTURECHANGED: app->dragging_=false; return 0;
        case WM_MOUSEMOVE:
            app->mouseX_=static_cast<float>(GET_X_LPARAM(lp)); app->mouseY_=static_cast<float>(GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ScreenToClient(window,&p);
            const int delta=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA;
            const int region=app->renderer_?app->renderer_->region(static_cast<float>(p.x),static_cast<float>(p.y)):0;
            if(region==1) app->collectionScroll_=std::clamp(app->collectionScroll_-delta,0,static_cast<int>(app->roller_.catalog().size())-1);
            else if(region==2) app->logScroll_=std::clamp(app->logScroll_-delta,0,std::max(0,static_cast<int>(app->log_.size())-1));
            else app->distance_=std::clamp(app->distance_-delta*.6f,6.f,34.f);
            return 0;
        }
        default: return DefWindowProcW(window,message,wp,lp);
        }
    }
    Options opts_; std::filesystem::path root_,saves_;
    aura::Roller roller_; aura::Progress progress_; aura::Presentation presentation_; std::vector<aura::LogEntry> log_;
    aura::Music music_;
    HWND window_{}; std::unique_ptr<aura::Renderer> renderer_; std::array<bool,256> keys_{};
    std::optional<aura::Hit> pending_;
    bool quitting_{},minimized_{},foreground_{true},auto_{},dragging_{};
    unsigned pendingWidth_{},pendingHeight_{},fpsFrames_{};
    int collectionScroll_{},logScroll_{},lastMouseX_{},lastMouseY_{};
    float pitch_{.52f};
    float movement_{};
    float time_{},cooldown_{},x_{},z_{},facing_{},walk_{},yaw_{.45f},distance_{11},mouseX_{-1},mouseY_{-1},fps_{};
    std::string notice_;
    Clock::time_point last_{},fpsStart_{Clock::now()},lastBackground_{};
};
} // namespace
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=1; HANDLE mutex{};
    try {
        aura::checked(com,"COM initialization");
        auto opts=options();
        if(!opts.smoke) {
            mutex=CreateMutexW(nullptr,FALSE,L"Local\\AuraRng.v0.1");
            if(!mutex) throw std::runtime_error("Cannot create single instance guard");
            if(GetLastError()==ERROR_ALREADY_EXISTS) throw std::runtime_error("Star's RNG is already running. Close it before opening another instance.");
        }
        App app(std::move(opts)); app.create(instance); result=app.run();
    } catch(const std::exception& error) {
        // Write diagnostics next to --report for unattended smoke tests without a modal dialog.
        bool smoke=false; std::filesystem::path report;
        try { const auto opts=options(); smoke=opts.smoke; report=opts.report; } catch(...) {}
        if(smoke) { if(!report.empty()) { std::ofstream out(report); out<<"FAIL: "<<error.what()<<'\n'; } }
        else MessageBoxW(nullptr,aura::widen(error.what()).c_str(),L"Star's RNG",MB_OK|MB_ICONERROR);
    }
    if(mutex) CloseHandle(mutex);
    if(SUCCEEDED(com)) CoUninitialize(); return result;
}
