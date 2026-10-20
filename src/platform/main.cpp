#include "core/aura.hpp"
#include "core/animation.hpp"
#include "render/renderer.hpp"
#include "platform/audio.hpp"
#include "platform/discord.hpp"
#include "tuning/game.hpp"
#include "tuning/stages.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>

namespace {
// このファイルはゲームの進行役です。
// 入力 → action/applyDebugActions → update（時間を進める）→ render（描く）の順。
// 数値の改造は tuning/game.hpp、絵の作り方は render/*.cpp から始めてください。
using Clock=std::chrono::steady_clock;
struct Options {
    bool smoke{},warp{},debug{};
    std::filesystem::path capture, report, saveDir;
    std::filesystem::path vrm,bgm,pixelationBgm,pixelationFinalBgm,illusionaryBgm;
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
        else if(a.starts_with(L"--pixelation-bgm=")) result.pixelationBgm=a.substr(17);
        else if(a.starts_with(L"--pixelation-final-bgm=")) result.pixelationFinalBgm=a.substr(23);
        else if(a.starts_with(L"--illusionary-bgm=")) result.illusionaryBgm=a.substr(18);
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
std::filesystem::path downloadsFolder() {
    PWSTR folder{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_Downloads,0,nullptr,&folder))) return {};
    std::filesystem::path result(folder); CoTaskMemFree(folder); return result;
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
        if(!opts_.pixelationBgm.empty()) presentation_.settings.pixelationBgmPath=utf8(opts_.pixelationBgm);
        if(!opts_.pixelationFinalBgm.empty()) presentation_.settings.pixelationFinalBgmPath=utf8(opts_.pixelationFinalBgm);
        if(!opts_.illusionaryBgm.empty()) presentation_.settings.illusionaryBgmPath=utf8(opts_.illusionaryBgm);
        downloads_=downloadsFolder();
        if(!downloads_.empty()) cutsceneAudioPath_=downloads_/L"EquinoxReworkedCutscene.mp4.mp4";
        roller_.setLuck(presentation_.settings.luck);
        roller_.setBiome(presentation_.settings.biome);
    }
    aura::Frame frame() const {
        // Rendererに渡す「今のゲームのスナップショット」。ここでは抽選や装備変更をしません。
        std::string notice=notice_;
        if(presentation_.settings.language==aura::Language::Japanese) {
            if(notice_=="Save could not be read. A backup was kept in the log folder.") notice="セーブを読めませんでした。ログフォルダーにバックアップを保持しています。";
            else if(notice_=="Settings could not be read. Defaults are in use.") notice="設定を読めなかったため初期設定で起動しています。";
            else if(notice_=="Progress could not be saved. Check the log folder / free disk space.") notice="保存できませんでした。保存先とディスクの空き容量を確認してください。";
            else if(notice_=="Roll log could not be written. Check the log folder / free disk space.") notice="ロール履歴を保存できませんでした。保存先と空き容量を確認してください。";
            else if(notice_=="Could not open the log folder.") notice="ログフォルダーを開けませんでした。";
            else if(notice_=="Settings could not be saved.") notice="言語と服の色の設定を保存できませんでした。";
        }
        const auto audioStatus=cutsceneActive()?(cutsceneAudioAvailable_?std::string("Cutscene: ")+cutsceneMusic_.status():std::string("Cutscene / visual only"))+
            " / Aura BGM: "+music_.status():music_.status();
        return {roller_,progress_,presentation_,log_,time_,cooldown_,x_,z_,facing_,walk_,yaw_,distance_,
            mouseX_,mouseY_,fps_,auto_,collectionScroll_,logScroll_,notice,audioStatus,pitch_,movement_,effectTime(),cutsceneTime_,cutsceneKind_,discord_.status(),actionTime(),shotOrbitTime(),
            stageSelectOpen_,stageTravel_.progress(),stageTravel_.target(),collectionCategory_,
            cutsceneMusic_.bassPulse(cutsceneTime_)};
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
        // 実行中はこのループを繰り返します。dtは前回から経過した秒数です。
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
    bool cutsceneActive() const { return cutsceneTime_>=0; }
    bool worldInputBlocked() const { return cutsceneActive() || stageSelectOpen_ || stageTravel_.active(); }
    void savePlayerSettings() {
        try {aura::saveSettings(saves_/L"settings.txt",presentation_.settings);}
        catch(const std::exception&) {notice_="Settings could not be saved.";}
    }
    void clearWorldInput() {
        auto_=false; keys_.fill(false); movement_=0; dragging_=false;
        ReleaseCapture(); pending_.reset(); sfxBaseline_=false;
    }
    void selectStages() {
        if(cutsceneActive() || stageTravel_.active()) return;
        stageSelectOpen_=!stageSelectOpen_; clearWorldInput();
    }
    void travelTo(aura::Biome destination) {
        if(cutsceneActive() || !stageTravel_.start(destination,presentation_.settings.biome)) return;
        stageSelectOpen_=false; clearWorldInput();
    }
    float effectTime() const {
        return opts_.smoke?aura::pixelationMotionTime(time_,presentation_.level(progress_,roller_.catalog())):effectClock_;
    }
    float actionTime() const {
        return opts_.smoke?aura::pixelationActionTime(time_,presentation_.level(progress_,roller_.catalog())):actionClock_;
    }
    float shotOrbitTime() const {
        if(!opts_.smoke) return shotOrbitClock_;
        const auto level=presentation_.level(progress_,roller_.catalog());
        const float cycleStart=std::floor(actionTime()/aura::PixelationVolleyPeriod)*aura::PixelationVolleyPeriod;
        return cycleStart*aura::pixelationMotionSpeed(level)/aura::pixelationActionSpeed(level);
    }
    std::filesystem::path audioFile(const std::string& custom,const wchar_t* filename) const {
        if(!custom.empty()) return std::filesystem::path(aura::widen(custom));
        const auto bundled=root_/L"data"/L"audio"/filename;
        std::error_code error;
        if(std::filesystem::is_regular_file(bundled,error) || downloads_.empty()) return bundled;
        const auto local=downloads_/filename;
        return std::filesystem::is_regular_file(local,error)?local:bundled;
    }
    void startCutscene(aura::CutsceneKind kind=aura::CutsceneKind::Equinox) {
        // 実際の装備と所持数は維持し、演出の最後に対応オーラを一時表示します。
        // Esc/停止なら直前のプレビューに戻し、自然終了なら結果を見たままにします。
        stageSelectOpen_=false; stageTravel_.finish();
        if(!cutsceneActive()) {
            cutsceneDebugWasOpen_=renderer_->debugOpen();
            cutscenePreviousPreview_=presentation_.preview;
        }
        if(renderer_->debugOpen()) renderer_->toggleDebug();
        cutsceneKind_=kind;
        for(std::size_t i=0;i<roller_.catalog().size();++i)
            if(roller_.catalog()[i].id==(kind==aura::CutsceneKind::Breakthrough?"breakthrough":"equinox"))
                presentation_.showPreview(i,roller_.catalog().size(),auto_);
        cutsceneAudioPath_=downloads_.empty()?std::filesystem::path{}:downloads_/(kind==aura::CutsceneKind::Breakthrough?L"BreakthroughCutsceneV2.mp4.mp4":L"EquinoxReworkedCutscene.mp4.mp4");
        // 配布版は動画ではなく、演出の範囲だけ切り出した音声を読みます。
        // 同梱音声は既にトリミング済みなので、開始位置は0秒です。
        const auto bundled=root_/L"data"/L"audio"/(kind==aura::CutsceneKind::Breakthrough?L"Breakthrough_Cutscene.m4a":L"Equinox_Cutscene.m4a");
        std::error_code bundleError;
        cutsceneAudioStart_=kind==aura::CutsceneKind::Breakthrough?aura::BreakthroughAudioStart:0.f;
        if(std::filesystem::is_regular_file(bundled,bundleError)) {cutsceneAudioPath_=bundled; cutsceneAudioStart_=0;}
        cutsceneTime_=0; auto_=false; keys_.fill(false); movement_=0; dragging_=false;
        ReleaseCapture(); pending_.reset(); sfxBaseline_=false; stopCutsceneRequested_=false;
        std::error_code error;
        cutsceneAudioAvailable_=!cutsceneAudioPath_.empty() && std::filesystem::is_regular_file(cutsceneAudioPath_,error);
        cutsceneMusic_.reload();
    }
    void stopCutscene(bool revealAura=false) {
        if(!cutsceneActive()) return;
        if(!revealAura) presentation_.preview=cutscenePreviousPreview_;
        cutsceneTime_=-1; keys_.fill(false); dragging_=false; ReleaseCapture(); pending_.reset();
        cutsceneMusic_.reload();
        if(renderer_->debugOpen()!=cutsceneDebugWasOpen_) renderer_->toggleDebug();
        sfxBaseline_=false; stopCutsceneRequested_=false;
    }
    void persist() {
        try { aura::saveProgress(saves_/L"progress.txt",progress_,roller_.catalog()); }
        catch(const std::exception&) { notice_="Progress could not be saved. Check the log folder / free disk space."; }
    }
    void roll() {
        // 1. 待ち時間が残っている間とCutScene中はロールしない。
        if(cooldown_>0 || worldInputBlocked()) return;
        // 2. 抽選 → 所持数と総回数へ追加。表示中のオーラは変更しない。
        roller_.setBiome(presentation_.settings.biome);
        const auto result=roller_.roll();
        progress_.record(result,roller_.catalog());
        aura::LogEntry entry{progress_.rolls,aura::utcNow(),result};
        if(log_.size()==200) log_.erase(log_.begin());
        log_.push_back(entry); logScroll_=0; cooldown_=aura::RollCooldown;
        // 3. CSVへ1行追加し、進行データを保存する。
        try { aura::appendLog(saves_/L"rolls.csv",entry,roller_.catalog()[result]); }
        catch(const std::exception&) { notice_="Roll log could not be written. Check the log folder / free disk space."; }
        persist();
        if(roller_.catalog()[result].id=="equinox") startCutscene();
        if(roller_.catalog()[result].id=="breakthrough") startCutscene(aura::CutsceneKind::Breakthrough);
    }
    void action(aura::Hit hit) {
        // 画面のボタン・キー入力をゲームの操作へ変換する場所。
        if(cutsceneActive() && hit.action!=aura::Action::Debug && hit.action!=aura::Action::Logs) return;
        if(stageTravel_.active() && hit.action!=aura::Action::Debug) return;
        if(stageSelectOpen_ && hit.action!=aura::Action::Stages && hit.action!=aura::Action::Travel &&
            hit.action!=aura::Action::CloseStages && hit.action!=aura::Action::Debug) return;
        switch(hit.action) {
        case aura::Action::Stages: selectStages(); break;
        case aura::Action::CloseStages: stageSelectOpen_=false; clearWorldInput(); break;
        case aura::Action::Travel:
            if(hit.index<aura::StageCount) travelTo(static_cast<aura::Biome>(hit.index));
            break;
        case aura::Action::CollectionCategory:
            if(hit.index<4) {collectionCategory_=static_cast<aura::AuraCategory>(hit.index); collectionScroll_=0;}
            break;
        case aura::Action::Roll: roll(); break;
        case aura::Action::Auto: auto_=!auto_; break;
        case aura::Action::Logs: {
            const auto result=ShellExecuteW(window_,L"open",saves_.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            if(reinterpret_cast<INT_PTR>(result)<=32) notice_="Could not open the log folder.";
            break;
        }
        case aura::Action::Equip:
            if(hit.index<progress_.counts.size() && hit.index<roller_.catalog().size() &&
                !roller_.catalog()[hit.index].previewOnly && progress_.counts[hit.index]) {
                progress_.equipped=hit.index; presentation_.preview.reset(); presentation_.singularityLevel.reset(); presentation_.pixelationLevel.reset();
                sfxBaseline_=false; persist();
            }
            break;
        case aura::Action::Debug:
            renderer_->toggleDebug(); keys_.fill(false); dragging_=false; ReleaseCapture();
            break;
        default: break;
        }
    }
    void applyDebugActions(const aura::DebugActions& commands) {
        // F1パネルから届く変更を反映。プレビューと実際の所持・装備は分けて扱います。
        if(commands.stopCutscene) stopCutscene();
        if(!worldInputBlocked()) {
        if(commands.preview) presentation_.showPreview(*commands.preview,roller_.catalog().size(),auto_);
        if(commands.restore) {presentation_.preview.reset(); presentation_.singularityLevel.reset(); presentation_.pixelationLevel.reset();}
        if(commands.auraLevel) {
            presentation_.singularityLevel=std::min(3u,*commands.auraLevel);
            const auto& catalog=roller_.catalog();
            for(std::size_t i=0;i<catalog.size();++i) if(catalog[i].id=="singularity") presentation_.showPreview(i,catalog.size(),auto_);
        }
        if(commands.actualLevel) presentation_.singularityLevel.reset();
        if(commands.pixelationLevel) {
            presentation_.pixelationLevel=std::min(3u,*commands.pixelationLevel);
            const auto& catalog=roller_.catalog();
            for(std::size_t i=0;i<catalog.size();++i) if(catalog[i].id=="pixelation") presentation_.showPreview(i,catalog.size(),auto_);
        }
        if(commands.pixelationActualLevel) presentation_.pixelationLevel.reset();
        if(commands.preview || commands.restore || commands.auraLevel || commands.actualLevel || commands.pixelationLevel || commands.pixelationActualLevel)
            sfxBaseline_=false;
        if(commands.roll) roll();
        if(commands.toggleAuto && !cutsceneActive()) auto_=!auto_;
        }
        if(commands.language) presentation_.settings.language=*commands.language;
        if(commands.username && aura::validUsername(*commands.username)) presentation_.settings.username=*commands.username;
        if(commands.discordApplicationId && aura::validDiscordApplicationId(*commands.discordApplicationId))
            presentation_.settings.discordApplicationId=*commands.discordApplicationId;
        if(commands.discordEnabled) presentation_.settings.discordEnabled=*commands.discordEnabled;
        if(commands.rollCount) {progress_.setRollCount(*commands.rollCount); auto_=false; persist();}
        if(commands.shirt) presentation_.settings.shirt=*commands.shirt;
        if(commands.luck) {roller_.setLuck(*commands.luck); presentation_.settings.luck=roller_.luck();}
        if(commands.avatar) presentation_.settings.avatar=*commands.avatar;
        if(commands.biome && aura::validBiome(*commands.biome) && !cutsceneActive() && !stageTravel_.active()) {
            presentation_.settings.biome=*commands.biome; x_=z_=0; clearWorldInput();
            roller_.setBiome(*commands.biome);
        }
        if(commands.breakthroughCaptionOffset && std::isfinite(*commands.breakthroughCaptionOffset))
            presentation_.settings.breakthroughCaptionOffset=std::clamp(*commands.breakthroughCaptionOffset,-5.f,5.f);
        if(commands.breakthroughWordTimes) {
            const auto& times=*commands.breakthroughWordTimes;
            bool valid=true; float previous=-1;
            for(const float second:times) {
                if(!std::isfinite(second) || second<0 || second>=aura::BreakthroughCutsceneDuration || second<=previous) valid=false;
                previous=second;
            }
            if(valid) presentation_.settings.breakthroughWordTimes=times;
        }
        if(commands.vrmPath) {presentation_.settings.vrmPath=*commands.vrmPath; renderer_->reloadVrm();}
        if(commands.bgmPath) {presentation_.settings.bgmPath=*commands.bgmPath; music_.reload();}
        if(commands.pixelationBgmPath) {presentation_.settings.pixelationBgmPath=*commands.pixelationBgmPath; music_.reload();}
        if(commands.pixelationFinalBgmPath) {presentation_.settings.pixelationFinalBgmPath=*commands.pixelationFinalBgmPath; music_.reload();}
        if(commands.illusionaryBgmPath) {presentation_.settings.illusionaryBgmPath=*commands.illusionaryBgmPath; music_.reload();}
        if(commands.volume) presentation_.settings.volume=*commands.volume;
        if(commands.username || commands.discordApplicationId || commands.discordEnabled || commands.language || commands.shirt || commands.luck || commands.avatar || commands.biome || commands.vrmPath || commands.bgmPath ||
            commands.pixelationBgmPath || commands.pixelationFinalBgmPath || commands.illusionaryBgmPath || commands.volume || commands.breakthroughCaptionOffset || commands.breakthroughWordTimes) {
            savePlayerSettings();
        }
        if(commands.playEquinoxCutscene) startCutscene();
        if(commands.playBreakthroughCutscene) startCutscene(aura::CutsceneKind::Breakthrough);
    }
    void update(float dt) {
        // 毎フレームの更新。例: dt=.016なら、およそ1/60秒ぶん進めます。
        dt=std::max(0.f,dt);
        time_+=dt; cooldown_=std::max(0.f,cooldown_-dt);
        if(pending_) { action(*pending_); pending_.reset(); }
        if(stopCutsceneRequested_) stopCutscene();
        if(cutsceneActive()) {
            // 実際に音声出力されたサンプル数を映像の時計にします。
            // MP4の初回デコードや重いフレームがあっても字幕と音が離れません。
            const auto played=cutsceneMusic_.playbackSeconds();
            if(!opts_.smoke && cutsceneAudioAvailable_ && played) cutsceneTime_=*played;
            else cutsceneTime_+=dt;
            if(cutsceneTime_>=aura::cutsceneDuration(cutsceneKind_)) stopCutscene(true);
        }
        if(const auto destination=stageTravel_.update(dt,aura::tuning::stages::TravelDurationSeconds)) {
            // 画面が宇宙に覆われた中間点で惑星と着地点を切り替えます。
            presentation_.settings.biome=*destination; x_=aura::tuning::stages::RespawnX; z_=aura::tuning::stages::RespawnZ; facing_=0; savePlayerSettings();
            roller_.setBiome(*destination);
        }
        if(stageTravel_.arrived()) stageTravel_.finish();
        if(auto_ && cooldown_<=0) roll();
        const auto& s=presentation_.settings;
        discord_.update(!opts_.smoke && s.discordEnabled,s.discordApplicationId,s.username,progress_.rolls,dt);
        const auto shown=presentation_.shown(progress_); const auto& id=roller_.catalog()[shown].id;
        const auto level=presentation_.level(progress_,roller_.catalog());
        if(shown!=effectAura_) {effectAura_=shown; effectClock_=actionClock_=shotOrbitClock_=0; shotOrbitCycle_=-1; sfxBaseline_=false;}
        effectClock_+=dt*(id=="pixelation"?aura::pixelationMotionSpeed(level):1.f);
        actionClock_+=dt*(id=="pixelation"?aura::pixelationActionSpeed(level):1.f);
        if(id=="pixelation") {
            const auto cycle=static_cast<std::int64_t>(std::floor(actionClock_/aura::PixelationVolleyPeriod));
            if(cycle!=shotOrbitCycle_) {
                const float age=actionClock_-static_cast<float>(cycle)*aura::PixelationVolleyPeriod;
                shotOrbitClock_=effectClock_-age*aura::pixelationMotionSpeed(level)/aura::pixelationActionSpeed(level);
                shotOrbitCycle_=cycle;
            }
        }
        bool musicActive=false;
        if(!cutsceneActive() && id=="singularity") {audioPath_=audioFile(s.bgmPath,L"Tides_of_the_Outer_Reach.mp3"); musicActive=true;}
        if(!cutsceneActive() && id=="illusionary") {audioPath_=audioFile(s.illusionaryBgmPath,L"The_Weirdos.ogg"); musicActive=true;}
        if(!cutsceneActive() && id=="pixelation" && level>=1) {
            audioPath_=level>=3?audioFile(s.pixelationFinalBgmPath,L"Censored.mp3.mpeg"):audioFile(s.pixelationBgmPath,L"Redline_Shift.mp3");
            musicActive=true;
        }
        music_.update(audioPath_,musicActive,s.volume,dt);
        // 最後の映像の透明度と同じカーブで音量も落とします。通常BGMは終了後に開始。
        const float cinematicGain=cutsceneActive()?aura::tuning::cutscene::endingOpacity(cutsceneTime_,aura::cutsceneDuration(cutsceneKind_)):0;
        cutsceneMusic_.update(cutsceneAudioPath_,cutsceneActive() && cutsceneAudioAvailable_,s.volume*cinematicGain,dt,
            cutsceneAudioStart_,aura::cutsceneDuration(cutsceneKind_));
        if(!cutsceneActive() && id=="pixelation" && level>=2) {
            const auto volley=aura::pixelationVolley(actionTime());
            if(sfxBaseline_) {
                if(volley.shotCycle>lastShotCycle_) sfx_.playShot(s.volume);
                if(volley.impactCycle>=0 && volley.impactCycle>lastImpactCycle_) sfx_.playImpact(s.volume);
            }
            lastShotCycle_=volley.shotCycle; lastImpactCycle_=volley.impactCycle; sfxBaseline_=true;
        } else sfxBaseline_=false;
        bool moving=false;
        if(!worldInputBlocked() && (foreground_ || opts_.smoke) && !renderer_->wantsKeyboard()) {
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
                // 方向×経過秒×速度で移動。clampはステージの端で止める処理です。
                x_=std::clamp(x_+dx*dt*aura::tuning::game::WalkSpeed,-aura::StageWalkLimit,aura::StageWalkLimit);
                z_=std::clamp(z_+dz*dt*aura::tuning::game::WalkSpeed,-aura::StageWalkLimit,aura::StageWalkLimit);
                aura::clampStagePosition(s.biome,x_,z_,aura::StageHalfExtent,aura::tuning::world::EdgeWalkPadding);
                moving=std::abs(x_-beforeX)+std::abs(z_-beforeZ)>.0001f;
                facing_=std::atan2(-dx,-dz);
                walk_+=dt*aura::tuning::game::WalkPhaseSpeed;
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
        // 開発者用の自動確認。標準値での回帰確認用で、通常プレイでは呼ばれません。
        const auto findAura=[&](const char* id) {
            const auto& catalog=roller_.catalog();
            for(std::size_t i=0;i<catalog.size();++i) if(catalog[i].id==id) return i;
            throw std::runtime_error(std::string("Smoke aura is missing: ")+id);
        };
        const auto singularity=findAura("singularity"),pixelation=findAura("pixelation"),equinox=findAura("equinox"),breakthrough=findAura("breakthrough"),nanally=findAura("nanally");
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
            if(!dragging_ || yaw_>=yawBefore || pitch_<=pitchBefore) throw std::runtime_error("Inverted right-drag orbit failed");
            SendMessageW(window_,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(540,360));
            if(std::abs(yaw_-yawBefore)>.00001f) throw std::runtime_error("Left-drag did not reverse horizontal rotation");
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
        // 4フォルダーを実際にクリックし、表示番号とクリック判定の一致を確認。
        const auto categoryRolls=progress_.rolls,categoryLogs=log_.size();
        const auto categoryCounts=progress_.counts;
        constexpr std::array<std::size_t,4> categorySizes={13,3,4,1};
        for(unsigned category=0;category<4;++category) {
            const float scale=renderer_->scale();
            SendMessageW(window_,WM_LBUTTONUP,0,MAKELPARAM(static_cast<int>((82+(category%2)*100)*scale),
                static_cast<int>((247+(category/2)*29)*scale))); update(0); render(false);
            const auto items=aura::collectionItems(frame());
            if(static_cast<unsigned>(collectionCategory_)!=category || items.size()!=categorySizes[category])
                throw std::runtime_error("Collection category button / membership failed");
            const auto hit=renderer_->hit(100*scale,316*scale,frame());
            if(hit.action!=aura::Action::Equip || hit.index!=items.front()) throw std::runtime_error("Collection folder row hit mismatch");
            if(!opts_.capture.empty()) renderer_->capture(opts_.capture.parent_path()/(L"collection-"+std::to_wstring(category)+L".png"));
        }
        action({aura::Action::CollectionCategory,0});
        if(progress_.rolls!=categoryRolls || log_.size()!=categoryLogs || progress_.counts!=categoryCounts || progress_.equipped!=chosen)
            throw std::runtime_error("Collection navigation changed progress");
        for(std::size_t i=0;i<roller_.catalog().size();++i) {
            aura::DebugActions command; command.preview=i; applyDebugActions(command); render(false);
        }
        const auto beforePreview=progress_.counts;
        const auto logSize=log_.size();
        const auto logBytes=std::filesystem::file_size(saves_/L"rolls.csv");
        auto_=true; aura::DebugActions previewCommand; previewCommand.preview=singularity; applyDebugActions(previewCommand);
        if(auto_ || progress_.counts!=beforePreview || log_.size()!=logSize ||
            std::filesystem::file_size(saves_/L"rolls.csv")!=logBytes || progress_.equipped!=chosen)
            throw std::runtime_error("Debug preview modified collection/history or did not stop AutoRoll");
        cooldown_=0; roll();
        if(presentation_.shown(progress_)!=singularity || progress_.equipped!=chosen)
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
            previewCommand.preview=singularity; applyDebugActions(previewCommand);
            if(!renderer_->debugOpen()) { SendMessageW(window_,WM_KEYDOWN,VK_F1,0); update(0); }
            render(false); render(false); captureVariant(L"-debug");
            if(renderer_->debugOpen()) { SendMessageW(window_,WM_KEYDOWN,VK_F1,0); update(0); }
            presentation_.preview.reset(); cooldown_=0; render(false); captureVariant(L"-complete");
        }
        auto captureExtra=[&](const wchar_t* suffix) {
            if(opts_.capture.empty()) return;
            const float measuredFps=fps_; fps_=0; render(false);
            renderer_->capture(opts_.capture.parent_path()/(opts_.capture.stem().wstring()+suffix+L".png"));
            fps_=measuredFps;
        };
        const auto pixelationCounts=progress_.counts;
        const auto pixelationRolls=progress_.rolls; const auto pixelationLogs=log_.size();
        const auto pixelationLogBytes=std::filesystem::file_size(saves_/L"rolls.csv");
        const auto previewDistance=distance_,previewPitch=pitch_,previewYaw=yaw_,previewTime=time_;
        if(roller_.tickets(pixelation)) throw std::runtime_error("Pixelation prototype entered the roll pool");
        aura::DebugActions pixels; pixels.preview=pixelation; auto_=true; applyDebugActions(pixels);
        time_=0; movement_=0; walk_=0; distance_=11; pitch_=.25f; yaw_=.15f; render(false);
        if(auto_ || presentation_.shown(progress_)!=pixelation || !renderer_->pixelationCount() ||
            progress_.counts!=pixelationCounts || progress_.rolls!=pixelationRolls || log_.size()!=pixelationLogs ||
            std::filesystem::file_size(saves_/L"rolls.csv")!=pixelationLogBytes || progress_.equipped!=chosen)
            throw std::runtime_error("Pixelation preview modified progress/history or did not stop AutoRoll");
        cooldown_=0; roll();
        if(presentation_.shown(progress_)!=pixelation || progress_.equipped!=chosen ||
            progress_.rolls!=pixelationRolls+1 || log_.size()!=std::min<std::size_t>(200,pixelationLogs+1) ||
            progress_.counts[pixelation]!=pixelationCounts[pixelation])
            throw std::runtime_error("Roll replaced Pixelation preview or rolled the preview-only aura");
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1000,700)); render(false);
        if(!renderer_->pixelationCount()) throw std::runtime_error("Pixelation resize to 1000x700 failed");
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1440,900)); render(false);
        if(!renderer_->pixelationCount()) throw std::runtime_error("Pixelation resize back to 1440x900 failed");
        const auto pixelationBegin=Clock::now();
        for(int i=0;i<60;++i) {time_+=1.f/60; render(false);}
        const double meanPixelationMs=std::chrono::duration<double,std::milli>(Clock::now()-pixelationBegin).count()/60;
        const auto pixelationInstances=renderer_->pixelationCount();
        cooldown_=0; time_=.25f; render(false); captureExtra(L"-pixelation");
        for(int i=0;i<24;++i) {
            time_=i/12.f; render(false);
            if(!renderer_->pixelationCount()) throw std::runtime_error("Pixelation animation lost its effect geometry");
            const auto suffix=L"-pixelation-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
        }
        std::array<std::size_t,4> pixelationLevelInstances{};
        std::size_t redlineDecoded=0,finalDecoded=0;
        aura::DebugActions speed; speed.pixelationLevel=0; applyDebugActions(speed); update(0);
        const float normalClock=effectClock_; update(.1f);
        if(std::abs(effectClock_-normalClock-.1f)>.0001f) throw std::runtime_error("Pixelation normal motion clock failed");
        const float switchingClock=effectClock_; speed.pixelationLevel=1; applyDebugActions(speed); update(0);
        if(std::abs(effectClock_-switchingClock)>.0001f) throw std::runtime_error("Pixelation level switch reset its motion clock");
        update(.1f);
        if(std::abs(effectClock_-switchingClock-.16f)>.0001f) throw std::runtime_error("Pixelation accelerated motion clock failed");
        speed.pixelationLevel=3; applyDebugActions(speed); const float finalClock=effectClock_,finalAction=actionClock_,shotBeforeSwitch=shotOrbitClock_; update(.1f);
        if(std::abs(effectClock_-finalClock-.1f*aura::pixelationMotionSpeed(3))>.0001f || aura::pixelationMotionSpeed(3)<=aura::pixelationMotionSpeed(2))
            throw std::runtime_error("Pixelation final level did not accelerate further");
        if(std::abs(actionClock_-finalAction-.16f)>.0001f || std::abs(shotOrbitClock_-shotBeforeSwitch)>.0001f)
            throw std::runtime_error("Pixelation final action speed / launch anchor changed");
        for(unsigned level=0;level<=3;++level) {
            aura::DebugActions effect; effect.pixelationLevel=level; auto_=true; applyDebugActions(effect);
            time_=.25f; update(.4f); render(false);
            pixelationLevelInstances[level]=renderer_->pixelationCount();
            if(auto_ || presentation_.shown(progress_)!=pixelation || presentation_.level(progress_,roller_.catalog())!=level || !pixelationLevelInstances[level])
                throw std::runtime_error("Pixelation Aura Level switch failed");
            if(!level && music_.playing()) throw std::runtime_error("Pixelation Lv0 played music");
            if(level>=1 && level<=2 && !opts_.pixelationBgm.empty()) {
                if(audioPath_!=opts_.pixelationBgm || !music_.playing() || !music_.decodedBytes()) throw std::runtime_error("Pixelation Redline BGM failed: "+music_.status());
                redlineDecoded=music_.decodedBytes();
            }
            if(level==3 && !opts_.pixelationFinalBgm.empty()) {
                if(audioPath_!=opts_.pixelationFinalBgm || !music_.playing() || !music_.decodedBytes()) throw std::runtime_error("Pixelation final BGM failed: "+music_.status());
                finalDecoded=music_.decodedBytes();
            }
            const auto suffix=L"-pixelation-level"+std::to_wstring(level); captureExtra(suffix.c_str());
        }
        aura::DebugActions volleyLevel; volleyLevel.pixelationLevel=2; applyDebugActions(volleyLevel);
        time_=0; update(0); const auto baselineSfx=sfx_.playCount();
        time_=.39f; update(0); render(false); captureExtra(L"-pixelation-before-impact");
        const auto impactsBefore=sfx_.impactCount();
        time_=.4f; update(0); render(false); captureExtra(L"-pixelation-impact");
        if(sfx_.available() && sfx_.impactCount()!=impactsBefore+1) throw std::runtime_error("Pixelation collision did not play one impact");
        const auto collisionSfx=sfx_.playCount();
        for(int i=0;i<8;++i) update(0);
        if(sfx_.playCount()!=collisionSfx) throw std::runtime_error("Pixelation repeated an impact in the same cycle");
        time_=.43f; update(0); render(false); captureExtra(L"-pixelation-after-impact");
        const auto shotsBefore=sfx_.shotCount(); time_=1.01f; update(0);
        if(sfx_.available() && sfx_.shotCount()!=shotsBefore+1) throw std::runtime_error("Pixelation volley did not play one shot");
        const auto volleySfx=sfx_.playCount();
        for(int i=0;i<8;++i) update(0);
        if(!opts_.warp && !opts_.capture.empty()) {
            for(int i=0;i<36;++i) {
                time_=i/18.f;
                const auto suffix=L"-volley-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
            }
        }
        time_=10; applyDebugActions(pixels); update(0);
        volleyLevel.pixelationLevel=3; applyDebugActions(volleyLevel); update(0);
        if(sfx_.playCount()!=volleySfx || collisionSfx<baselineSfx) throw std::runtime_error("Pixelation preview / level switch replayed old sound events");
        if(!opts_.warp && !opts_.capture.empty()) for(int i=0;i<42;++i) {
            time_=i/30.f;
            const auto suffix=L"-volley3-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
        }
        volleyLevel.pixelationLevel=2; applyDebugActions(volleyLevel); time_=.46f; render(false); captureExtra(L"-pixelation-matched-level2");
        const auto matchedPose2=aura::pixelationPose(frame().actionTime);
        const auto matchedVolley2=aura::pixelationVolley(frame().actionTime,frame().effectTime,frame().shotOrbitTime);
        volleyLevel.pixelationLevel=3; applyDebugActions(volleyLevel); render(false); captureExtra(L"-pixelation-matched-level3");
        const auto matchedPose3=aura::pixelationPose(frame().actionTime);
        const auto matchedVolley3=aura::pixelationVolley(frame().actionTime,frame().effectTime,frame().shotOrbitTime);
        if(matchedPose2.lift!=matchedPose3.lift || matchedPose2.bodyRoll!=matchedPose3.bodyRoll ||
            matchedVolley2.impactAge!=matchedVolley3.impactAge || matchedVolley2.flightProgress!=matchedVolley3.flightProgress)
            throw std::runtime_error("Pixelation Lv2 / Lv3 action timing diverged");
        aura::DebugActions commonAudio; commonAudio.preview=0; applyDebugActions(commonAudio); update(.6f);
        if(music_.playing()) throw std::runtime_error("Pixelation music did not stop on another aura");
        volleyLevel.pixelationLevel=0; applyDebugActions(volleyLevel); update(.6f);
        if(music_.playing()) throw std::runtime_error("Pixelation music did not stop at Lv0");
        aura::DebugActions actualPixelation; actualPixelation.pixelationActualLevel=true; applyDebugActions(actualPixelation);
        applyDebugActions(previewCommand); render(false);
        if(renderer_->pixelationCount()) throw std::runtime_error("Pixelation geometry remained after switching aura");
        action({aura::Action::Equip,chosen});
        distance_=previewDistance; pitch_=previewPitch; yaw_=previewYaw; time_=previewTime;
        const auto beforeSettingsRolls=progress_.rolls; const auto beforeSettingsLogs=log_.size();
        double vrmFrameMs=0;
        aura::DebugActions world; world.luck=25; world.biome=aura::Biome::Desert; applyDebugActions(world);
        if(roller_.luck()!=25 || presentation_.settings.biome!=aura::Biome::Desert) throw std::runtime_error("World settings actions failed");
        render(false); captureExtra(L"-desert");
        world.biome=aura::Biome::Normal; applyDebugActions(world); render(false);
        // 惑星選択は通常のクリック入力で確認。移動中にロールや視点操作が漏れないことも確認。
        const bool stageDebugWasOpen=renderer_->debugOpen();
        if(stageDebugWasOpen) renderer_->toggleDebug();
        const auto stageCounts=progress_.counts; const auto stageRolls=progress_.rolls,stageLogs=log_.size();
        const auto stageEquipped=progress_.equipped;
        const float stageDistance=distance_,stagePitch=pitch_,stageYaw=yaw_;
        aura::DebugActions stagePreview; stagePreview.preview=0; applyDebugActions(stagePreview);
        distance_=26; pitch_=.64f; yaw_=.45f; x_=z_=0;
        for(unsigned destination=0;destination<aura::StageCount;++destination) {
            if(destination==0) {render(false); captureExtra(L"-planet-garden"); continue;}
            auto_=true;
            SendMessageW(window_,WM_KEYDOWN,'M',0); update(0);
            if(!stageSelectOpen_ || auto_) throw std::runtime_error("Planet menu did not stop AutoRoll");
            render(false); if(destination==1) captureExtra(L"-planet-select");
            if(destination==1) {
                SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1000,700)); render(false); captureExtra(L"-planet-select-small");
                const float smallScale=renderer_->scale();
                const auto smallMenu=aura::stageSelectLayout(1000/smallScale,700/smallScale);
                SendMessageW(window_,WM_LBUTTONUP,0,MAKELPARAM(static_cast<int>((smallMenu.x+smallMenu.width-33)*smallScale),
                    static_cast<int>((smallMenu.y+31)*smallScale))); update(0);
                if(stageSelectOpen_) throw std::runtime_error("Scaled planet-menu close button failed");
                SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1440,900)); render(false);
                SendMessageW(window_,WM_KEYDOWN,'M',0); update(0);
            }
            const auto menu=aura::stageSelectLayout(1440,900);
            const auto scale=renderer_->scale();
            SendMessageW(window_,WM_LBUTTONUP,0,MAKELPARAM(static_cast<int>((menu.x+menu.width*.5f)*scale),
                static_cast<int>((menu.y+112+destination*64.f+28)*scale)));
            update(0);
            if(stageSelectOpen_ || !stageTravel_.active() || auto_) throw std::runtime_error("Planet card did not launch rocket");
            const float lockedYaw=yaw_; const auto originalPlanet=presentation_.settings.biome;
            SendMessageW(window_,WM_KEYDOWN,'W',0); SendMessageW(window_,WM_KEYDOWN,'R',0); SendMessageW(window_,WM_KEYDOWN,'F',0);
            SendMessageW(window_,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(540,340));
            SendMessageW(window_,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(580,360));
            update(aura::tuning::stages::TravelDurationSeconds*.25f);
            if(x_!=0 || z_!=0 || yaw_!=lockedYaw || dragging_ || auto_ || progress_.rolls!=stageRolls || presentation_.settings.biome!=originalPlanet)
                throw std::runtime_error("Rocket launch leaked gameplay or switched planet early");
            render(false); if(destination==2) captureExtra(L"-rocket-launch");
            update(aura::tuning::stages::TravelDurationSeconds*.26f);
            if(presentation_.settings.biome!=static_cast<aura::Biome>(destination)) throw std::runtime_error("Planet swap missed midpoint");
            render(false); if(destination==2) captureExtra(L"-rocket-space");
            update(aura::tuning::stages::TravelDurationSeconds*.40f);
            render(false); if(destination==2) captureExtra(L"-rocket-landing");
            update(aura::tuning::stages::TravelDurationSeconds*.10f); render(false);
            if(stageTravel_.active() || aura::loadSettings(saves_/L"settings.txt").biome!=static_cast<aura::Biome>(destination))
                throw std::runtime_error("Planet arrival was not saved or unlocked");
            const wchar_t* suffixes[]={L"-planet-garden",L"-planet-desert",L"-planet-ocean",L"-planet-forest",L"-planet-volcano",L"-planet-extra"};
            captureExtra(suffixes[destination]);
            if(destination==static_cast<unsigned>(aura::Biome::Extra)) {
                distance_=78; pitch_=1.25f; render(false); captureExtra(L"-planet-extra-overview");
                distance_=26; pitch_=.64f;
            }
            if(renderer_->instanceCount()>4096) throw std::runtime_error("Planet exceeded scene instance budget");
        }
        SendMessageW(window_,WM_KEYDOWN,'M',0); update(0);
        SendMessageW(window_,WM_KEYDOWN,VK_ESCAPE,0); update(0);
        if(stageSelectOpen_ || quitting_) throw std::runtime_error("Planet menu Escape did not close safely");
        if(progress_.counts!=stageCounts || progress_.rolls!=stageRolls || log_.size()!=stageLogs || progress_.equipped!=stageEquipped)
            throw std::runtime_error("Planet travel changed inventory or roll history");
        distance_=stageDistance; pitch_=stagePitch; yaw_=stageYaw;
        if(stageDebugWasOpen) renderer_->toggleDebug();
        world.biome=aura::Biome::Desert; world.luck=1; applyDebugActions(world);
        previewCommand.preview=singularity; applyDebugActions(previewCommand); update(.4f); render(false); captureExtra(L"-space");
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
        const auto cutCounts=progress_.counts;
        const auto cutRolls=progress_.rolls; const auto cutLogs=log_.size();
        const auto cutLogBytes=std::filesystem::file_size(saves_/L"rolls.csv");
        aura::DebugActions equinoxPreview; equinoxPreview.preview=equinox; auto_=true; applyDebugActions(equinoxPreview);
        time_=2; distance_=11; pitch_=.27f; movement_=0; walk_=0; render(false); captureExtra(L"-equinox");
        if(auto_ || !renderer_->equinoxCount() || renderer_->pixelationCount()) throw std::runtime_error("Equinox preview failed");
        const auto equinoxBegin=Clock::now();
        for(int i=0;i<60;++i) {time_+=1.f/60; render(false);}
        const double meanEquinoxMs=std::chrono::duration<double,std::milli>(Clock::now()-equinoxBegin).count()/60;
        const auto equinoxInstances=renderer_->equinoxCount();
        for(float second:{.60f,.95f,2.8f}) {
            time_=second; render(false);
            const auto suffix=L"-equinox-polarity-"+std::to_wstring(static_cast<int>(second*100)); captureExtra(suffix.c_str());
        }
        pitch_=.75f; render(false); captureExtra(L"-equinox-floor"); pitch_=.27f;
        if(!opts_.warp && !opts_.capture.empty()) for(int i=0;i<40;++i) {
            time_=i*.10f;
            const auto suffix=L"-equinox-cloud-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
        }
        if(!opts_.warp && !opts_.capture.empty()) {
            for(int i=0;i<36;++i) {
                time_=i*(1.565f/36);
                const auto suffix=L"-equinox-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
            }
        }
        applyDebugActions(previewCommand); render(false);
        if(renderer_->equinoxCount()) throw std::runtime_error("Equinox remained after aura switch");
        const auto cutShown=presentation_.shown(progress_); const auto cutPreview=presentation_.preview;
        const auto cutSingularityLevel=presentation_.singularityLevel,cutPixelationLevel=presentation_.pixelationLevel;
        const bool debugBeforeCut=renderer_->debugOpen();
        if(!renderer_->debugOpen()) renderer_->toggleDebug();
        auto_=true; keys_['W']=true;
        aura::DebugActions playCutscene; playCutscene.playEquinoxCutscene=true; applyDebugActions(playCutscene);
        if(!cutsceneActive() || auto_ || renderer_->debugOpen() || keys_['W'] || dragging_) throw std::runtime_error("Cutscene did not suspend gameplay / debug");
        const auto cutX=x_,cutZ=z_,cutYaw=yaw_,cutPitch=pitch_,cutDistance=distance_;
        cooldown_=0; roll(); action({aura::Action::Auto,0});
        SendMessageW(window_,WM_KEYDOWN,VK_SPACE,0); SendMessageW(window_,WM_KEYDOWN,'F',0); SendMessageW(window_,WM_KEYDOWN,'W',0);
        SendMessageW(window_,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(540,340)); SendMessageW(window_,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(600,370));
        SendMessageW(window_,WM_RBUTTONUP,0,MAKELPARAM(600,370)); SendMessageW(window_,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
        update(.1f);
        if(auto_ || x_!=cutX || z_!=cutZ || yaw_!=cutYaw || pitch_!=cutPitch || distance_!=cutDistance || progress_.rolls!=cutRolls)
            throw std::runtime_error("Cutscene accepted gameplay input");
        std::size_t cutsceneDecoded=0;
        if(cutsceneAudioAvailable_) {
            if(!cutsceneMusic_.playing() || !cutsceneMusic_.decodedBytes()) throw std::runtime_error("Local MP4 cutscene audio failed: "+cutsceneMusic_.status());
            cutsceneDecoded=cutsceneMusic_.decodedBytes();
        }
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1000,700)); render(false); captureExtra(L"-cutscene-small");
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1440,900)); render(false);
        update(.6f);
        if(music_.playing()) throw std::runtime_error("Aura BGM did not fade during cutscene");
        cutsceneTime_=1.5f; render(false);
        constexpr int CutsceneBenchmarkFrames=20;
        const auto cutsceneBegin=Clock::now();
        for(int i=0;i<CutsceneBenchmarkFrames;++i) render(false);
        const double meanCutsceneMs=std::chrono::duration<double,std::milli>(Clock::now()-cutsceneBegin).count()/CutsceneBenchmarkFrames;
        constexpr float cutTimes[]={.5f,1.5f,3.5f,6,8.5f,10.5f};
        for(const float cutSecond:cutTimes) {
            cutsceneTime_=cutSecond; render(false);
            const auto suffix=L"-cutscene-"+std::to_wstring(static_cast<int>(cutSecond*10)); captureExtra(suffix.c_str());
        }
        for(int i=0;i<60;++i) {
            cutsceneTime_=i*.18f; render(false);
            const auto suffix=L"-cutscene-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
        }
        for(float remaining:{.8f,.4f,.05f}) {
            cutsceneTime_=aura::EquinoxCutsceneDuration-remaining; render(false);
            if(remaining<.8f && !renderer_->equinoxCount()) throw std::runtime_error("Equinox fade did not reveal underlying aura");
            const auto suffix=L"-cutscene-fade-"+std::to_wstring(static_cast<int>(remaining*100)); captureExtra(suffix.c_str());
        }
        cutsceneTime_=4.1f;
        for(float bass:{0.f,1.f}) {
            auto testFrame=frame(); testFrame.cutsceneBass=bass; renderer_->draw(testFrame,false);
            captureExtra(bass?L"-cutscene-bass1":L"-cutscene-bass0");
        }
        cutsceneTime_=aura::EquinoxCutsceneDuration-.01f; update(.02f);
        if(cutsceneActive() || cutsceneMusic_.playing() || !renderer_->debugOpen()) throw std::runtime_error("Cutscene timeout did not stop audio / restore debug state");
        if(presentation_.shown(progress_)!=equinox || progress_.equipped!=chosen) throw std::runtime_error("Cutscene natural end did not reveal Equinox preview");
        presentation_.preview=cutPreview; // 以下のEsc確認は、直前のプレビューから再開。
        applyDebugActions(playCutscene);
        if(cutsceneMusic_.playing() || cutsceneMusic_.decodedBytes()) throw std::runtime_error("Cutscene restart did not reset audio to its beginning");
        update(.05f);
        if(cutsceneAudioAvailable_ && (!cutsceneMusic_.playing() || cutsceneMusic_.decodedBytes()!=cutsceneDecoded))
            throw std::runtime_error("Local cutscene audio did not restart");
        SendMessageW(window_,WM_KEYDOWN,VK_ESCAPE,0); update(0);
        if(cutsceneActive() || cutsceneMusic_.playing() || quitting_ || !renderer_->debugOpen()) throw std::runtime_error("Cutscene Escape cancellation failed");
        applyDebugActions(playCutscene); SendMessageW(window_,WM_KEYDOWN,VK_F1,0); update(0);
        if(!cutsceneActive() || !renderer_->debugOpen()) throw std::runtime_error("F1 did not reopen debug during cutscene");
        aura::DebugActions stop; stop.stopCutscene=true; applyDebugActions(stop);
        if(cutsceneActive() || cutsceneMusic_.playing() || !renderer_->debugOpen()) throw std::runtime_error("Cutscene debug stop failed");
        if(renderer_->debugOpen()!=debugBeforeCut) renderer_->toggleDebug();
        if(progress_.counts!=cutCounts || progress_.rolls!=cutRolls || log_.size()!=cutLogs ||
            std::filesystem::file_size(saves_/L"rolls.csv")!=cutLogBytes || progress_.equipped!=chosen ||
            presentation_.shown(progress_)!=cutShown || presentation_.preview!=cutPreview ||
            presentation_.singularityLevel!=cutSingularityLevel || presentation_.pixelationLevel!=cutPixelationLevel)
            throw std::runtime_error("Equinox / cutscene changed collection, levels or roll history");

        aura::DebugActions breakthroughPreview; breakthroughPreview.preview=breakthrough;
        auto_=true; applyDebugActions(breakthroughPreview); time_=2; pitch_=.27f; distance_=13; movement_=0; render(false);
        captureExtra(L"-breakthrough");
        if(auto_ || roller_.tickets(breakthrough) || !renderer_->breakthroughCount() || renderer_->pixelationCount() || renderer_->equinoxCount())
            throw std::runtime_error("BreakThrough preview / effect isolation failed");
        const auto breakthroughInstances=renderer_->breakthroughCount();
        for(float second:{.8f,2.6f}) {
            time_=second; render(false);
            const auto suffix=L"-breakthrough-float-"+std::to_wstring(static_cast<int>(second*10)); captureExtra(suffix.c_str());
        }
        const auto breakthroughBegin=Clock::now();
        for(int i=0;i<60;++i) {time_+=1.f/60; render(false);}
        const double meanBreakthroughMs=std::chrono::duration<double,std::milli>(Clock::now()-breakthroughBegin).count()/60;
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1000,700)); render(false);
        if(!renderer_->breakthroughCount()) throw std::runtime_error("BreakThrough resize failed");
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1440,900)); render(false);
        if(!opts_.warp && !opts_.capture.empty()) for(int i=0;i<49;++i) {
            time_=i*.1f; render(false);
            const auto suffix=L"-breakthrough-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
        }
        const auto breakthroughShown=presentation_.shown(progress_);
        const bool debugBeforeBreakthrough=renderer_->debugOpen();
        if(!renderer_->debugOpen()) renderer_->toggleDebug();
        aura::DebugActions playBreakthrough; playBreakthrough.playBreakthroughCutscene=true;
        auto_=true; keys_['W']=true; applyDebugActions(playBreakthrough);
        if(!cutsceneActive() || cutsceneKind_!=aura::CutsceneKind::Breakthrough || auto_ || keys_['W'] || renderer_->debugOpen())
            throw std::runtime_error("BreakThrough cutscene did not suspend gameplay");
        const auto breakX=x_,breakZ=z_,breakYaw=yaw_;
        cooldown_=0; roll(); action({aura::Action::Auto,0});
        SendMessageW(window_,WM_KEYDOWN,'W',0); SendMessageW(window_,WM_KEYDOWN,VK_SPACE,0);
        SendMessageW(window_,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(540,340));
        SendMessageW(window_,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(600,370));
        SendMessageW(window_,WM_RBUTTONUP,0,MAKELPARAM(600,370));
        update(.1f); render(false);
        if(auto_ || x_!=breakX || z_!=breakZ || yaw_!=breakYaw || renderer_->breakthroughCount() || renderer_->equinoxCount())
            throw std::runtime_error("BreakThrough cinematic accepted input / retained world geometry");
        std::size_t breakthroughAudioBytes=0;
        if(cutsceneAudioAvailable_) {
            breakthroughAudioBytes=cutsceneMusic_.decodedBytes();
            if(!cutsceneMusic_.playing() || breakthroughAudioBytes<1'000'000 || breakthroughAudioBytes>12'000'000 ||
                (cutsceneAudioPath_.filename()!=L"BreakthroughCutsceneV2.mp4.mp4" && cutsceneAudioPath_.filename()!=L"Breakthrough_Cutscene.m4a"))
                throw std::runtime_error("BreakThrough local clipped audio failed: "+cutsceneMusic_.status());
        }
        aura::DebugActions captionTuning;
        captionTuning.breakthroughCaptionOffset=-.1f;
        captionTuning.breakthroughWordTimes=presentation_.settings.breakthroughWordTimes;
        (*captionTuning.breakthroughWordTimes)[0]=22.4f;
        applyDebugActions(captionTuning);
        const auto savedCaption=aura::loadSettings(saves_/L"settings.txt");
        if(savedCaption.breakthroughCaptionOffset!=-.1f || savedCaption.breakthroughWordTimes[0]!=22.4f)
            throw std::runtime_error("Live cutscene caption settings were not applied and saved");
        captionTuning.breakthroughCaptionOffset=0.f;
        captionTuning.breakthroughWordTimes=aura::tuning::cutscene::BreakthroughWordTimes;
        applyDebugActions(captionTuning);
        cutsceneTime_=24.2f; render(false); captureExtra(L"-breakthrough-words");
        constexpr float breakCutTimes[]={1,4,9,13,14.5f,15.5f,18,22,26,28.5f,29.5f};
        for(float second:breakCutTimes) {
            cutsceneTime_=second; render(false);
            const auto suffix=L"-breakthrough-cutscene-"+std::to_wstring(static_cast<int>(second*10)); captureExtra(suffix.c_str());
        }
        if(!opts_.warp && !opts_.capture.empty()) for(int i=0;i<100;++i) {
            cutsceneTime_=i*.3f; render(false);
            const auto suffix=L"-breakthrough-cutscene-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
        }
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1000,700)); render(false); captureExtra(L"-breakthrough-cutscene-small");
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1440,900)); render(false);
        cutsceneTime_=aura::BreakthroughCutsceneDuration-.01f; update(.02f);
        if(cutsceneActive() || cutsceneMusic_.playing() || !renderer_->debugOpen() || presentation_.shown(progress_)!=breakthroughShown)
            throw std::runtime_error("BreakThrough timeout failed to restore display / debug");
        applyDebugActions(playBreakthrough); update(.05f);
        if(cutsceneAudioAvailable_ && (!cutsceneMusic_.playing() || cutsceneMusic_.decodedBytes()!=breakthroughAudioBytes))
            throw std::runtime_error("BreakThrough audio restart failed");
        applyDebugActions(playCutscene);
        if(cutsceneKind_!=aura::CutsceneKind::Equinox || cutsceneTime_!=0 || cutsceneMusic_.playing())
            throw std::runtime_error("Switching cinematic did not reset kind / audio");
        applyDebugActions(playBreakthrough); update(.05f);
        SendMessageW(window_,WM_KEYDOWN,VK_ESCAPE,0); update(0);
        if(cutsceneActive() || cutsceneMusic_.playing() || quitting_ || !renderer_->debugOpen())
            throw std::runtime_error("BreakThrough Escape cancellation failed");
        applyDebugActions(playBreakthrough); SendMessageW(window_,WM_KEYDOWN,VK_F1,0); update(0); applyDebugActions(stop);
        if(cutsceneActive() || cutsceneMusic_.playing() || !renderer_->debugOpen()) throw std::runtime_error("BreakThrough debug stop failed");
        if(renderer_->debugOpen()!=debugBeforeBreakthrough) renderer_->toggleDebug();
        if(progress_.counts!=cutCounts || progress_.rolls!=cutRolls || log_.size()!=cutLogs ||
            std::filesystem::file_size(saves_/L"rolls.csv")!=cutLogBytes || progress_.equipped!=chosen ||
            presentation_.shown(progress_)!=breakthroughShown || presentation_.singularityLevel!=cutSingularityLevel || presentation_.pixelationLevel!=cutPixelationLevel)
            throw std::runtime_error("BreakThrough preview / cinematic changed inventory or history");
        applyDebugActions(previewCommand); render(false);
        if(renderer_->breakthroughCount()) throw std::runtime_error("BreakThrough remained after aura switching");
        aura::DebugActions nanallyPreview; nanallyPreview.preview=nanally;
        auto_=true; applyDebugActions(nanallyPreview);
        time_=.35f; movement_=0; walk_=0; facing_=0; distance_=12; pitch_=.23f; yaw_=.15f;
        world.biome=aura::Biome::Normal; applyDebugActions(world); render(false); captureExtra(L"-nanally");
        if(auto_ || roller_.tickets(nanally) || !renderer_->nanallyCount() || renderer_->breakthroughCount() || renderer_->equinoxCount() || renderer_->pixelationCount())
            throw std::runtime_error("Nanally preview / isolation failed");
        const auto nanallyInstances=renderer_->nanallyCount();
        const auto nanallyBegin=Clock::now();
        for(int i=0;i<60;++i) {time_+=1.f/60; render(false);}
        const double meanNanallyMs=std::chrono::duration<double,std::milli>(Clock::now()-nanallyBegin).count()/60;
        for(float camera:{-.8f,.8f,2.8f}) {
            yaw_=camera; render(false);
            const auto suffix=L"-nanally-angle-"+std::to_wstring(static_cast<int>(camera*10)); captureExtra(suffix.c_str());
        }
        yaw_=.15f;
        if(!opts_.warp && !opts_.capture.empty()) for(int i=0;i<48;++i) {
            time_=i/24.f;
            const auto suffix=L"-nanally-frame-"+std::to_wstring(i); captureExtra(suffix.c_str());
        }
        const float nanallyX=x_,nanallyZ=z_;
        x_+=2; z_-=1; facing_=.65f; movement_=1; walk_=1; render(false); captureExtra(L"-nanally-moving");
        if(!renderer_->nanallyCount()) throw std::runtime_error("Nanally lost geometry while moving");
        x_=nanallyX; z_=nanallyZ; facing_=0; movement_=0; walk_=0;
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1000,700)); render(false);
        if(!renderer_->nanallyCount()) throw std::runtime_error("Nanally resize failed");
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1440,900)); render(false);
        applyDebugActions(playBreakthrough); render(false);
        if(renderer_->nanallyCount()) throw std::runtime_error("Nanally remained during cutscene");
        applyDebugActions(stop); render(false);
        if(presentation_.shown(progress_)!=nanally || !renderer_->nanallyCount()) throw std::runtime_error("Cutscene did not restore Nanally");
        if(progress_.counts!=cutCounts || progress_.rolls!=cutRolls || log_.size()!=cutLogs ||
            std::filesystem::file_size(saves_/L"rolls.csv")!=cutLogBytes || progress_.equipped!=chosen)
            throw std::runtime_error("Nanally changed inventory or history");
        applyDebugActions(previewCommand); render(false);
        if(renderer_->nanallyCount()) throw std::runtime_error("Nanally remained after switching aura");
        const auto illusionary=findAura("illusionary"),dune=findAura("dune"),mirage=findAura("mirage"),pharaoh=findAura("pharaoh");
        aura::DebugActions illusionaryPreview; illusionaryPreview.preview=illusionary;
        auto_=true; applyDebugActions(illusionaryPreview);
        time_=.35f; distance_=11; pitch_=.23f; yaw_=.15f; movement_=0; walk_=0;
        world.biome=aura::Biome::Normal; applyDebugActions(world); render(false); captureExtra(L"-illusionary-at-spawn");
        x_=10; z_=-2; render(false); captureExtra(L"-illusionary");
        if(auto_ || roller_.tickets(illusionary) || !renderer_->illusionaryCount() || renderer_->pixelationCount() || renderer_->nanallyCount())
            throw std::runtime_error("Illusionary preview isolation failed");
        const auto illusionaryInstances=renderer_->illusionaryCount();
        update(.1f);
        if(!music_.playing() || !music_.decodedBytes()) throw std::runtime_error("Illusionary Ogg BGM failed: "+music_.status());
        const auto illusionaryAudioBytes=music_.decodedBytes();
        pitch_=1.25f; render(false); captureExtra(L"-illusionary-overhead");
        pitch_=.05f; yaw_=0; render(false); captureExtra(L"-illusionary-front");
        pitch_=.23f;
        for(float angle:{-.9f,.9f,2.8f}) {yaw_=angle; render(false);}
        yaw_=.15f;
        for(float second:{.5f,1.3f,2.15f,3.2f,4.2f}) {
            time_=second; render(false);
            const auto suffix=L"-illusionary-phase-"+std::to_wstring(static_cast<int>(second*100)); captureExtra(suffix.c_str());
        }
        x_+=2; facing_=.6f; movement_=1; walk_=2; render(false);
        if(!renderer_->illusionaryCount()) throw std::runtime_error("Illusionary did not follow movement");
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1000,700)); render(false); captureExtra(L"-illusionary-small");
        SendMessageW(window_,WM_SIZE,SIZE_RESTORED,MAKELPARAM(1440,900)); render(false);
        applyDebugActions(playBreakthrough); render(false);
        if(renderer_->illusionaryCount()) throw std::runtime_error("Illusionary remained during cutscene");
        applyDebugActions(stop); render(false);
        if(!renderer_->illusionaryCount()) throw std::runtime_error("Cutscene did not restore Illusionary");
        world.biome=aura::Biome::Desert; applyDebugActions(world);
        for(const auto index:{dune,mirage,pharaoh}) {
            aura::DebugActions desert; desert.preview=index; auto_=true; applyDebugActions(desert);
            x_=9; z_=-2; time_=1.2f; yaw_=.15f; distance_=10; pitch_=.3f; movement_=0; walk_=0; facing_=0;
            render(false);
            if(auto_ || !renderer_->desertAuraCount() || renderer_->desertAuraCount()>300 || renderer_->illusionaryCount())
                throw std::runtime_error("Lightweight desert aura preview/switch failed");
            const auto suffix=L"-desert-aura-"+aura::widen(roller_.catalog()[index].id); captureExtra(suffix.c_str());
            time_+=.6f; movement_=1; walk_=2; render(false);
        }
        update(.6f);
        if(music_.playing()) throw std::runtime_error("Illusionary BGM did not stop after aura switch");
        if(progress_.counts!=cutCounts || progress_.rolls!=cutRolls || log_.size()!=cutLogs ||
            std::filesystem::file_size(saves_/L"rolls.csv")!=cutLogBytes || progress_.equipped!=chosen)
            throw std::runtime_error("New aura previews changed inventory or history");
        aura::DebugActions spawnCommon; spawnCommon.preview=0; applyDebugActions(spawnCommon);
        x_=aura::tuning::stages::RespawnX; z_=aura::tuning::stages::RespawnZ;
        time_=1; distance_=14; pitch_=.3f; movement_=0; walk_=0; render(false); captureExtra(L"-rocket-respawn");
        if(aura::tuning::stages::RocketX!=x_ || aura::tuning::stages::RocketZ!=z_)
            throw std::runtime_error("Rocket is not at the respawn position");
        if(renderer_->illusionaryCount() || renderer_->desertAuraCount()) throw std::runtime_error("New aura geometry remained after switch");
        if(!opts_.vrm.empty()) {
            aura::DebugActions avatar; avatar.avatar=aura::Avatar::Vrm; applyDebugActions(avatar);
            const auto deadline=Clock::now()+std::chrono::seconds(45);
            do {render(false); Sleep(10);} while(!renderer_->vrmReady() && Clock::now()<deadline && renderer_->vrmLoading());
            if(!renderer_->vrmReady() || renderer_->vrmVertices()==0) throw std::runtime_error("VRM import failed: "+renderer_->vrmStatus());
            applyDebugActions(pixels); movement_=0; walk_=0; time_=.25f; distance_=11; pitch_=.25f; render(false); captureExtra(L"-vrm-pixelation");
            if(!renderer_->pixelationCount() || !renderer_->vrmReady()) throw std::runtime_error("Pixelation VRM preview failed");
            const auto pixelationFixedA=renderer_->vrmBone(4);
            movement_=1; walk_=2; render(false); const auto pixelationFixedB=renderer_->vrmBone(4);
            if(std::abs(pixelationFixedA.x-pixelationFixedB.x)+std::abs(pixelationFixedA.y-pixelationFixedB.y)+
                std::abs(pixelationFixedA.z-pixelationFixedB.z)>.00001f)
                throw std::runtime_error("Moving changed the fixed Pixelation VRM pose");
            applyDebugActions(equinoxPreview); movement_=0; walk_=0; distance_=11; pitch_=.27f; render(false); captureExtra(L"-vrm-equinox");
            const auto equinoxFixedA=renderer_->vrmBone(4);
            movement_=1; walk_=2; render(false); const auto equinoxFixedB=renderer_->vrmBone(4);
            if(!renderer_->equinoxCount() || std::abs(equinoxFixedA.x-equinoxFixedB.x)+std::abs(equinoxFixedA.y-equinoxFixedB.y)+
                std::abs(equinoxFixedA.z-equinoxFixedB.z)>.00001f) throw std::runtime_error("Moving changed the fixed Equinox VRM pose");
            applyDebugActions(breakthroughPreview); movement_=0; walk_=0; distance_=13; pitch_=.27f; render(false); captureExtra(L"-vrm-breakthrough");
            const auto breakthroughFixedA=renderer_->vrmBone(4);
            movement_=1; walk_=2; render(false); const auto breakthroughFixedB=renderer_->vrmBone(4);
            if(!renderer_->breakthroughCount() || std::abs(breakthroughFixedA.x-breakthroughFixedB.x)+std::abs(breakthroughFixedA.y-breakthroughFixedB.y)+
                std::abs(breakthroughFixedA.z-breakthroughFixedB.z)>.00001f) throw std::runtime_error("Moving changed the fixed BreakThrough VRM pose");
            applyDebugActions(nanallyPreview); movement_=0; walk_=0; facing_=0; distance_=12; pitch_=.23f; yaw_=.15f;
            render(false); captureExtra(L"-vrm-nanally");
            if(!renderer_->nanallyCount() || !renderer_->vrmReady()) throw std::runtime_error("Nanally VRM preview failed");
            applyDebugActions(illusionaryPreview); x_=8; z_=-2; movement_=0; walk_=0; time_=.35f; render(false); captureExtra(L"-vrm-illusionary");
            const auto illusionaryFixedA=renderer_->vrmBone(4);
            movement_=1; walk_=2; render(false); const auto illusionaryFixedB=renderer_->vrmBone(4);
            if(!renderer_->illusionaryCount() || std::abs(illusionaryFixedA.x-illusionaryFixedB.x)+
                std::abs(illusionaryFixedA.y-illusionaryFixedB.y)+std::abs(illusionaryFixedA.z-illusionaryFixedB.z)>.00001f)
                throw std::runtime_error("Moving changed fixed Illusionary VRM pose");
            aura::DebugActions common; common.preview=0; applyDebugActions(common);
            movement_=0; walk_=0; distance_=7; pitch_=.20f; yaw_=.15f;
            render(false); captureExtra(L"-vrm");
            if(renderer_->pixelationCount() || renderer_->nanallyCount()) throw std::runtime_error("Custom effect remained after VRM aura switch");
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
        const auto profileCounts=progress_.counts; const auto profileRolls=progress_.rolls;
        const auto profileBest=progress_.best,profileLogs=log_.size();
        const auto profileLogBytes=std::filesystem::file_size(saves_/L"rolls.csv");
        const auto originalUsername=presentation_.settings.username;
        const auto originalApplicationId=presentation_.settings.discordApplicationId;
        const auto originalDiscordEnabled=presentation_.settings.discordEnabled;
        aura::DebugActions profileEdit; profileEdit.username="星のプレイヤー";
        profileEdit.discordApplicationId="1560569853184573590"; profileEdit.discordEnabled=false;
        applyDebugActions(profileEdit);
        const auto profileSettings=aura::loadSettings(saves_/L"settings.txt");
        if(profileSettings.username!=*profileEdit.username || profileSettings.discordApplicationId!=*profileEdit.discordApplicationId || profileSettings.discordEnabled)
            throw std::runtime_error("Profile / Discord settings did not save");
        for(std::uint64_t count:{0ULL,123'456'789ULL,profileRolls}) {
            aura::DebugActions counterEdit; counterEdit.rollCount=count; auto_=true; applyDebugActions(counterEdit);
            const auto savedCounter=aura::loadProgress(saves_/L"progress.txt",roller_.catalog());
            if(auto_ || progress_.rolls!=count || savedCounter.rolls!=count || savedCounter.counts!=profileCounts ||
                progress_.counts!=profileCounts || progress_.best!=profileBest || log_.size()!=profileLogs ||
                std::filesystem::file_size(saves_/L"rolls.csv")!=profileLogBytes)
                throw std::runtime_error("Editing roll counter changed inventory / history or failed to save");
        }
        profileEdit.username=originalUsername; profileEdit.discordApplicationId=originalApplicationId; profileEdit.discordEnabled=originalDiscordEnabled;
        applyDebugActions(profileEdit); update(0);
        if(discord_.state()!=aura::DiscordPresenceState::Disabled) throw std::runtime_error("Smoke test must never publish a real Discord activity");
        // 実際のApp::rollを高Luckで64回通し、Extraの4種の取得・演出発火を確認。
        const auto beforeExtra=progress_.rolls; const auto extraEquipment=progress_.equipped;
        const auto originalStage=presentation_.settings.biome; const auto originalLuck=roller_.luck();
        aura::DebugActions extraWorld; extraWorld.biome=aura::Biome::Extra; extraWorld.luck=1'000'000; applyDebugActions(extraWorld);
        std::array<bool,4> obtained{};
        for(unsigned i=0;i<64;++i) {
            if(cutsceneActive()) stopCutscene();
            cooldown_=0; roll();
            const auto result=log_.back().aura;
            if(!aura::availableInBiome(roller_.catalog()[result],aura::Biome::Extra)) throw std::runtime_error("Offstage aura acquired in Extra");
            const std::array<std::size_t,4> ids={pixelation,equinox,breakthrough,illusionary};
            for(std::size_t a=0;a<ids.size();++a) if(result==ids[a]) obtained[a]=true;
            if((result==equinox || result==breakthrough) && (!cutsceneActive() ||
                cutsceneKind_!=(result==equinox?aura::CutsceneKind::Equinox:aura::CutsceneKind::Breakthrough)))
                throw std::runtime_error("Acquired Extra aura did not trigger its CutScene");
        }
        if(cutsceneActive()) stopCutscene();
        if(progress_.rolls!=beforeExtra+64 || progress_.equipped!=extraEquipment ||
            !std::all_of(obtained.begin(),obtained.end(),[](bool got){return got;})) throw std::runtime_error("Extra real roll acquisition failed");
        extraWorld.biome=originalStage; extraWorld.luck=originalLuck; applyDebugActions(extraWorld);
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
            out<<"Effects: PASS / levels 0..3, stars, black holes, no history mutation\nStage: "
               <<aura::StageHalfExtent*2<<" x "<<aura::StageHalfExtent*2<<" world units / configured in tuning/world.hpp\n";
            out<<"Planets: PASS / garden, desert, ocean, forest, volcano, extra, menu/card input, rocket phases, midpoint swap, saved arrival, Escape, gameplay/input isolation, scene budget <=4096\n";
            out<<"Collection: PASS / Normal13 Desert3 Extra4 Administrator1, folder button and row click mapping, navigation preserves progress\nExtra acquisition: PASS / 64 actual rolls, all four exclusive auras acquired, CutScene triggers, equipment preserved\n";
            out<<"Profile: PASS / UTF-8 username and Discord settings save/restore; editable total 0 / 123456789 / original, AutoRoll stop, inventory/history/best unchanged\n"
               <<"Discord integration: disabled during game smoke; transport and payload covered by isolated fake IPC tests\n";
            out<<"Pixelation: PASS / preview isolation, no progress/history mutation, AutoRoll stop, rolls preserve preview, 24 animated frames, clean aura switching, resize 1000x700 -> 1440x900\n"
               <<"Pixelation instances: "<<pixelationInstances<<"\nMean Pixelation frame submission time: "<<meanPixelationMs
               <<" ms/frame (60 offscreen frames, no vsync)\n";
            out<<"Pixelation levels: PASS / 0..3, split motion/action clocks; Lv2/Lv3 matching collision/explosion/float timing, fixed launch anchor during level switch; synchronized collision, one sound event per cycle\n"
               <<"Pixelation level instances: "<<pixelationLevelInstances[0]<<", "<<pixelationLevelInstances[1]<<", "<<pixelationLevelInstances[2]<<", "<<pixelationLevelInstances[3]
               <<"\nSFX: "<<sfx_.shotCount()<<" shots / "<<sfx_.impactCount()<<" impacts / "<<sfx_.status()<<"\n"
               <<"Equinox: PASS / preview, clean switching, no progress/history mutation\n"
               <<"Cutscene: PASS / gameplay suppression, resize, 60 frames, natural end reveals target preview, fade reveals underlying aura, Escape restores prior view\n";
            out<<"Equinox instances: "<<equinoxInstances<<"\nMean Equinox frame submission time: "<<meanEquinoxMs
               <<" ms/frame (60 offscreen frames, no vsync)\n"
               <<"Cutscene instances: 0 / one fullscreen triangle, procedural shader\nMean Cutscene frame submission time: "<<meanCutsceneMs
               <<" ms/frame ("<<CutsceneBenchmarkFrames<<" offscreen frames at t=1.5, no vsync)\n";
            out<<"BreakThrough: PASS / preview isolation, AutoRoll stop, clean switching, resize, inventory/history unchanged\n"
               <<"BreakThrough instances: "<<breakthroughInstances<<"\nMean BreakThrough frame submission time: "<<meanBreakthroughMs
               <<" ms/frame (60 offscreen frames, no vsync)\n"
               <<"BreakThrough cutscene: PASS / static pillar positions, 30 seconds, input suppression, timeout, Escape, debug stop, kind switch, restored display\n";
            out<<"BreakThrough captions: PASS / eight measured word times, live offset/per-word changes saved during playback, reset to defaults\n";
            out<<"Illusionary: PASS / "<<illusionaryInstances<<" opaque/glow instances, preview isolation, reclining pose, solid floor, orbit lights, movement, resize, cutscene restore and clean switching\n"
               <<"Desert auras: PASS / Dune 1 in 50, Mirage 1 in 5000, Pharaoh 1 in 250000, <=300 glow instances each, history isolation\n"
               <<"Rocket: PASS / shared respawn XZ, boarding clearance, all six planets\n";
            out<<"Illusionary Ogg audio: PASS / "<<illusionaryAudioBytes<<" decoded PCM bytes, loop and stop\n";
            out<<"Nanally: PASS / preview-only, AutoRoll stop, clean switching, resize, movement and camera angles, cutscene restore, inventory/history unchanged\n"
               <<"Nanally instances: "<<nanallyInstances<<"\nMean Nanally frame submission time: "<<meanNanallyMs
               <<" ms/frame (60 offscreen frames, no vsync)\n";
            if(breakthroughAudioBytes) out<<"BreakThrough audio: PASS / "<<breakthroughAudioBytes<<" PCM bytes / 30s non-looping clip, bundled trimmed M4A or local MP4 fallback, restart and stop\n";
            else out<<"BreakThrough local audio: visual-only / reference MP4 unavailable\n";
            if(cutsceneDecoded) out<<"Equinox audio: PASS / "<<cutsceneDecoded<<" decoded PCM bytes / 10.9s clip, measured bass, restart from beginning and stop\n";
            else out<<"Local cutscene audio: visual-only / reference MP4 unavailable\n";
            if(!opts_.pixelationBgm.empty()) out<<"Redline Shift: PASS / "<<redlineDecoded<<" decoded PCM bytes / Lv1-2 loop and stop\n";
            if(!opts_.pixelationFinalBgm.empty()) out<<"Censored: PASS / "<<finalDecoded<<" decoded PCM bytes / Lv3 switch and stop\n";
            if(!opts_.vrm.empty()) out<<"VRM: PASS / "<<renderer_->vrmVertices()<<" vertices / "<<renderer_->vrmDraws()<<" additional draw calls / "<<vrmFrameMs
                <<" ms/frame submission / standing, walking, Singularity / Pixelation / Equinox fixed poses during movement, textures, reload and missing-file fallback\n";
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
        if(app->cutsceneActive() && message==WM_KEYDOWN && wp==VK_ESCAPE) {
            if(!(lp & (1LL<<30))) app->stopCutsceneRequested_=true;
            return 0;
        }
        // Once a drag starts on the stage it owns the mouse until release or
        // capture loss. ImGui's previous-frame capture flag must not interrupt it.
        if(message==WM_RBUTTONDOWN && !app->worldInputBlocked() && app->renderer_ && app->renderer_->canOrbit(
            static_cast<float>(GET_X_LPARAM(lp)),static_cast<float>(GET_Y_LPARAM(lp)))) {
            app->lastMouseX_=GET_X_LPARAM(lp); app->lastMouseY_=GET_Y_LPARAM(lp);
            app->dragging_=true; SetFocus(window); SetCapture(window); return 0;
        }
        if(message==WM_MOUSEMOVE && app->dragging_) {
            const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            app->mouseX_=static_cast<float>(x); app->mouseY_=static_cast<float>(y);
            // マウスを右へ動かすと左回転。左右だけ反転し、上下の操作は従来通りです。
            app->yaw_-=(x-app->lastMouseX_)*aura::tuning::game::OrbitHorizontalSensitivity;
            app->pitch_=std::clamp(app->pitch_+(y-app->lastMouseY_)*aura::tuning::game::OrbitVerticalSensitivity,
                aura::tuning::game::MinimumCameraPitch,aura::tuning::game::MaximumCameraPitch);
            app->lastMouseX_=x; app->lastMouseY_=y; return 0;
        }
        if(message==WM_RBUTTONUP && app->dragging_) {
            app->dragging_=false; ReleaseCapture(); return 0;
        }
        if(message==WM_CAPTURECHANGED) app->dragging_=false;
        if(app->renderer_ && app->renderer_->handleMessage(window,message,wp,lp)) return 0;
        if((app->stageSelectOpen_ || app->stageTravel_.active()) && message==WM_KEYDOWN) {
            if((wp==VK_ESCAPE || wp=='M') && app->stageSelectOpen_ && !(lp & (1LL<<30))) app->pending_=aura::Hit{aura::Action::CloseStages,0};
            return 0;
        }
        if((app->stageSelectOpen_ || app->stageTravel_.active()) &&
            (message==WM_RBUTTONDOWN || message==WM_RBUTTONUP || message==WM_MOUSEWHEEL)) return 0;
        if(app->cutsceneActive()) {
            if(message==WM_KEYDOWN) return 0;
            if(message==WM_LBUTTONUP || message==WM_RBUTTONDOWN || message==WM_RBUTTONUP || message==WM_MOUSEWHEEL) return 0;
        }
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
                if(wp=='M') app->pending_=aura::Hit{aura::Action::Stages,0};
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
            else app->distance_=std::clamp(app->distance_-delta*aura::tuning::game::ZoomStep,
                aura::tuning::game::MinimumCameraDistance,aura::tuning::game::MaximumCameraDistance);
            return 0;
        }
        default: return DefWindowProcW(window,message,wp,lp);
        }
    }
    Options opts_; std::filesystem::path root_,saves_,downloads_,audioPath_,cutsceneAudioPath_;
    aura::Roller roller_; aura::Progress progress_; aura::Presentation presentation_; std::vector<aura::LogEntry> log_;
    aura::Music music_,cutsceneMusic_; aura::SoundEffects sfx_;
    aura::DiscordPresence discord_;
    HWND window_{}; std::unique_ptr<aura::Renderer> renderer_; std::array<bool,256> keys_{};
    std::optional<aura::Hit> pending_;
    std::optional<std::size_t> cutscenePreviousPreview_;
    aura::AuraCategory collectionCategory_{aura::AuraCategory::Normal};
    bool quitting_{},minimized_{},foreground_{true},auto_{},dragging_{};
    bool cutsceneDebugWasOpen_{},stopCutsceneRequested_{},sfxBaseline_{};
    bool cutsceneAudioAvailable_{};
    bool stageSelectOpen_{};
    aura::StageTravel stageTravel_;
    aura::CutsceneKind cutsceneKind_{aura::CutsceneKind::Equinox};
    float effectClock_{},actionClock_{},shotOrbitClock_{},cutsceneTime_{-1};
    float cutsceneAudioStart_{};
    std::int64_t shotOrbitCycle_{-1};
    std::size_t effectAura_{std::numeric_limits<std::size_t>::max()};
    std::int64_t lastShotCycle_{-1},lastImpactCycle_{-1};
    unsigned pendingWidth_{},pendingHeight_{},fpsFrames_{};
    int collectionScroll_{},logScroll_{},lastMouseX_{},lastMouseY_{};
    float pitch_{aura::tuning::game::InitialCameraPitch};
    float movement_{};
    float time_{},cooldown_{},x_{aura::tuning::stages::RespawnX},z_{aura::tuning::stages::RespawnZ},facing_{},walk_{};
    float yaw_{aura::tuning::game::InitialCameraYaw},distance_{aura::tuning::game::InitialCameraDistance};
    float mouseX_{-1},mouseY_{-1},fps_{};
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
