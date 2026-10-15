#include "core/aura.hpp"
#include "render/renderer.hpp"
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
            mouseX_,mouseY_,fps_,auto_,collectionScroll_,logScroll_,notice};
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
                progress_.equipped=hit.index; presentation_.preview.reset(); persist();
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
        if(commands.restore) presentation_.preview.reset();
        if(commands.roll) roll();
        if(commands.toggleAuto) auto_=!auto_;
        if(commands.language) presentation_.settings.language=*commands.language;
        if(commands.shirt) presentation_.settings.shirt=*commands.shirt;
        if(commands.language || commands.shirt) {
            try { aura::saveSettings(saves_/L"settings.txt",presentation_.settings); }
            catch(const std::exception&) { notice_="Settings could not be saved."; }
        }
    }
    void update(float dt) {
        time_+=dt; cooldown_=std::max(0.f,cooldown_-dt);
        if(pending_) { action(*pending_); pending_.reset(); }
        if(auto_ && cooldown_<=0) roll();
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
                x_=std::clamp(x_+dx*dt*3.f,-6.8f,6.8f); z_=std::clamp(z_+dz*dt*3.f,-6.8f,6.8f);
                facing_=std::atan2(-dx,-dz); walk_+=dt*3;
            } else walk_=0;
        }
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
        persist();
        const auto restored=aura::loadProgress(saves_/L"progress.txt",roller_.catalog());
        if(restored.counts!=progress_.counts) throw std::runtime_error("Progress smoke check failed");
        if(restored.equipped!=chosen) throw std::runtime_error("Collection equipment was not saved");
        if(!opts_.report.empty()) {
            std::ofstream out(opts_.report);
            out<<"PASS: D3D11 + D2D + Dear ImGui, resize, roll, cooldown, AutoRoll, movement, save restore, fixed equipment, preview isolation\n"
               <<"Adapter: "<<renderer_->adapterName()<<"\nResolution: 1440x900\nWorld draw calls: 1\nInstances: "
               <<renderer_->instanceCount()<<"\nMean frame submission time: "<<seconds/60*1000
               <<" ms/frame (60 offscreen frames, no vsync)\nRolls: "<<progress_.rolls<<"\n";
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
        case WM_RBUTTONDOWN:
            if(app->renderer_ && app->renderer_->region(static_cast<float>(GET_X_LPARAM(lp)),static_cast<float>(GET_Y_LPARAM(lp)))==0) {
                app->dragging_=true; app->lastMouseX_=GET_X_LPARAM(lp); SetCapture(window);
            }
            return 0;
        case WM_RBUTTONUP: app->dragging_=false; ReleaseCapture(); return 0;
        case WM_CAPTURECHANGED: app->dragging_=false; return 0;
        case WM_MOUSEMOVE:
            app->mouseX_=static_cast<float>(GET_X_LPARAM(lp)); app->mouseY_=static_cast<float>(GET_Y_LPARAM(lp));
            if(app->dragging_) { app->yaw_+=(app->mouseX_-app->lastMouseX_)*.008f; app->lastMouseX_=GET_X_LPARAM(lp); }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ScreenToClient(window,&p);
            const int delta=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA;
            const int region=app->renderer_?app->renderer_->region(static_cast<float>(p.x),static_cast<float>(p.y)):0;
            if(region==1) app->collectionScroll_=std::clamp(app->collectionScroll_-delta,0,static_cast<int>(app->roller_.catalog().size())-1);
            else if(region==2) app->logScroll_=std::clamp(app->logScroll_-delta,0,std::max(0,static_cast<int>(app->log_.size())-1));
            else app->distance_=std::clamp(app->distance_-delta*.6f,6.f,17.f);
            return 0;
        }
        default: return DefWindowProcW(window,message,wp,lp);
        }
    }
    Options opts_; std::filesystem::path root_,saves_;
    aura::Roller roller_; aura::Progress progress_; aura::Presentation presentation_; std::vector<aura::LogEntry> log_;
    HWND window_{}; std::unique_ptr<aura::Renderer> renderer_; std::array<bool,256> keys_{};
    std::optional<aura::Hit> pending_;
    bool quitting_{},minimized_{},foreground_{true},auto_{},dragging_{};
    unsigned pendingWidth_{},pendingHeight_{},fpsFrames_{};
    int collectionScroll_{},logScroll_{},lastMouseX_{};
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
