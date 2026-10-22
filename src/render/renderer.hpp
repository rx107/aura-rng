#pragma once
#include "core/presentation.hpp"
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <span>
#include <future>
#include "render/vrm.hpp"

struct ImGuiContext;

namespace aura {
using Microsoft::WRL::ComPtr;
struct Frame {
    const Roller& roller;
    const Progress& progress;
    const Presentation& presentation;
    const std::vector<LogEntry>& logs;
    float time{}, cooldown{}, avatarX{}, avatarZ{}, facing{}, walk{}, cameraYaw{.45f}, cameraDistance{11};
    float mouseX{-1}, mouseY{-1}, fps{};
    bool autoRoll{};
    int collectionScroll{}, logScroll{};
    std::string notice;
    std::string musicStatus;
    float cameraPitch{.52f};
    float movement{};
    float effectTime{-1},cutsceneTime{-1};
    CutsceneKind cutsceneKind{CutsceneKind::Equinox};
    std::string discordStatus;
    float actionTime{-1},shotOrbitTime{-1};
    bool stageSelectOpen{};
    float stageTravelProgress{-1};
    Biome stageTravelTarget{Biome::Normal};
    AuraCategory collectionCategory{AuraCategory::Normal};
    float cutsceneBeat{}; // 実音声の再生位置に同期した拍0〜1。音声がなければ0。
    float auraBeat{},musicBpm{}; // BGMの実再生時刻に同期した拍0〜1と、推定BPM。
    float equinoxBeat{-1}; // 通常Equinoxの連続した経過拍数。負なら設定BPMで代替。
};
enum class Action { None, Roll, Auto, Logs, Equip, Debug, Stages, Travel, CloseStages, CollectionCategory };
struct Hit { Action action{Action::None}; std::size_t index{}; };
// 描く場所とクリック判定は同じ設計図を使います。座標は1440×900基準の論理ピクセル。
// 惑星カード i は {x+20, y+112+i*64, width-40, 56}、閉じるは右上の30×30です。
struct StageSelectLayout { float x,y,width,height; };
inline StageSelectLayout stageSelectLayout(float w,float h) {
    const float width=std::min(760.f,w-636.f),height=184.f+StageCount*64.f;
    return {256+(w-596-width)*.5f,96+(h-280-height)*.5f,width,height};
}
// Collectionはデータの並び順を変えず、選んだフォルダーに属する番号だけ表示します。
// 表示とクリック判定でこの関数を共有し、「見えている行と装備番号」のずれを防ぎます。
inline std::vector<std::size_t> collectionItems(const Frame& frame) {
    std::vector<std::size_t> result;
    for(std::size_t i=0;i<frame.roller.catalog().size();++i)
        if(auraCategory(frame.roller.catalog()[i])==frame.collectionCategory) result.push_back(i);
    return result;
}
class Renderer {
public:
    ~Renderer();
    void initialize(HWND window, bool warp);
    void resize(unsigned width, unsigned height);
    void draw(const Frame& frame, bool present = true);
    void capture(const std::filesystem::path& path);
    Hit hit(float pixelX, float pixelY, const Frame& frame) const;
    int region(float pixelX, float pixelY) const; // 1=collection, 2=log, 0=world
    std::string adapterName() const { return adapterName_; }
    std::size_t instanceCount() const { return instances_.size()+starInstances_.size(); }
    std::size_t starCount() const { return starInstances_.size(); }
    std::size_t pixelationCount() const { return pixelationVisible_?pixelationInstances_.size():0; }
    std::size_t equinoxCount() const { return equinoxVisible_?pixelationInstances_.size()+equinoxOrbs_.size()+equinoxTitles_.size():0; }
    std::size_t breakthroughCount() const { return breakthroughVisible_?pixelationInstances_.size():0; }
    std::size_t nanallyCount() const { return nanallyVisible_?nanallyInstances_.size()+pixelationInstances_.size():0; }
    std::size_t illusionaryCount() const { return illusionaryVisible_?illusionaryInk_.size()+pixelationInstances_.size():0; }
    std::size_t desertAuraCount() const { return desertAuraVisible_?pixelationInstances_.size():0; }
    bool blackHoleVisible() const { return holeVisible_; }
    DirectX::XMFLOAT3 vrmBone(int role) const { return vrm_?vrm_->bonePosition(role):DirectX::XMFLOAT3{}; }
    float scale() const { return scale_; }
    bool handleMessage(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept;
    bool wantsKeyboard() const;
    void toggleDebug() { debugOpen_ = !debugOpen_; }
    bool debugOpen() const { return debugOpen_; }
    DebugActions takeDebugActions();
    bool vrmReady() const { return vrm_!=nullptr && loadedVrm_==requestedVrm_; }
    bool vrmLoading() const { return vrmFuture_.valid(); }
    void reloadVrm() { requestedVrm_.clear(); }
    std::string vrmStatus() const { return vrmStatus_; }
    std::size_t vrmVertices() const { return vrm_?vrm_->vertexCount():0; }
    std::size_t vrmDraws() const { return vrm_?vrm_->drawCount():0; }
    bool canOrbit(float x,float y) const;
private:
    struct Vertex { DirectX::XMFLOAT3 position, normal; };
    struct Instance { DirectX::XMFLOAT4X4 world; DirectX::XMFLOAT4 color; };
    void createTargets();
    void createPipeline();
    void createEffects();
    void createPixelation();
    void buildPixelation(const Frame& frame);
    void createEquinox();
    void createCutscene();
    void buildEquinox(const Frame& frame);
    void drawCutscene(const Frame& frame);
    void cutsceneUi(const Frame& frame);
    void createBreakthrough();
    void buildBreakthrough(const Frame& frame);
    void drawBreakthroughCutscene(const Frame& frame);
    void breakthroughCutsceneUi(const Frame& frame);
    void createNanally();
    void buildNanally(const Frame& frame);
    void buildIllusionary(const Frame& frame);
    void buildDesert(const Frame& frame);
    void createPixelationTargets();
    void compositePixelation();
    void pixelQuad(DirectX::FXMMATRIX transform,Color color,unsigned style,float opacity);
    void pixelBillboard(float x,float y,float z,float width,float height,Color color,unsigned style,float opacity,const Frame& frame,float rotation=0);
    void pixelLine(DirectX::FXMVECTOR from,DirectX::FXMVECTOR to,float width,Color color,float opacity,const Frame& frame);
    void pixelText(const char* value,float x,float y,float z,float size,Color color,const Frame& frame);
    void drawEffects(const Frame& frame);
    void particleStar(float x,float y,float z,float size,Color color,float rotation,const Frame& frame);
    void box(float x, float y, float z, float sx, float sy, float sz, Color color, float rotation = 0, float glow = 0);
    void world(const Frame& frame);
    void updateVrm(const Frame& frame);
    void ui(const Frame& frame);
    void stageSelectUi(const Frame& frame);
    void stageTravelUi(const Frame& frame);
    void initializeDebug();
    void debugUi(const Frame& frame);
    void createStar();
    void star(float x, float y, float radius, D2D1_COLOR_F color, bool filled = true, float angle = 0);
    void cooldown(float x, float y, float width, float remaining);
    void rect(float x, float y, float w, float h, D2D1_COLOR_F c, float radius = 0);
    void stroke(float x, float y, float w, float h, D2D1_COLOR_F c, float radius = 0);
    void text(const std::wstring& value, float x, float y, float w, float h, float size, D2D1_COLOR_F color, bool bold = false,bool center=false);
    void label(const std::string& value, float x, float y, float w, float h, float size, D2D1_COLOR_F color, bool bold = false,bool center=false);
    HWND window_{};
    bool warp_{};
    unsigned width_{}, height_{};
    float scale_{1}, logicalW_{1440}, logicalH_{900};
    std::string adapterName_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swap_;
    ComPtr<ID3D11RenderTargetView> target_;
    ComPtr<ID3D11DepthStencilView> depth_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11PixelShader> holePs_;
    ComPtr<ID3D11PixelShader> pixelationPs_;
    ComPtr<ID3D11PixelShader> equinoxOrbPs_,cutscenePs_;
    ComPtr<ID3D11PixelShader> breakthroughCutscenePs_;
    ComPtr<ID3D11PixelShader> breakthroughPs_;
    ComPtr<ID3D11PixelShader> nanallyPs_,nanallyGlowPs_;
    ComPtr<ID3D11ShaderResourceView> breakthroughLettering_;
    ComPtr<ID3D11Buffer> breakthroughConstants_;
    ComPtr<ID3D11Buffer> cutsceneConstants_;
    ComPtr<IDWriteTextFormat> cutsceneFont_,cutsceneKanjiFont_;
    ComPtr<ID3D11ShaderResourceView> equinoxLettering_;
    ComPtr<ID3D11VertexShader> bloomVs_;
    ComPtr<ID3D11PixelShader> blurPs_,compositePs_;
    ComPtr<ID3D11Buffer> blurConstants_;
    ComPtr<ID3D11SamplerState> bloomSampler_;
    std::array<ComPtr<ID3D11RenderTargetView>,3> bloomTargets_;
    std::array<ComPtr<ID3D11ShaderResourceView>,3> bloomSources_;
    unsigned bloomWidth_{},bloomHeight_{};
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vertices_, indices_, instanceBuffer_, constants_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> depthState_;
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<ID2D1RenderTarget> d2d_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1PathGeometry> starGeometry_;
    ComPtr<ID2D1StrokeStyle> roundStroke_;
    ComPtr<IDWriteFactory> write_;
    struct Font { float size; bool bold; ComPtr<IDWriteTextFormat> format; };
    std::vector<Font> fonts_;
    std::vector<Instance> instances_;
    std::vector<Instance> starInstances_;
    std::vector<Instance> pixelationInstances_;
    std::vector<Instance> equinoxOrbs_;
    std::vector<Instance> equinoxTitles_;
    std::vector<Instance> nanallyInstances_;
    std::vector<Instance> illusionaryInk_; // 黒い帯は発光バッチから分け、黒色を保ちます。
    ComPtr<ID3D11Buffer> starVertices_,starIndices_,holeVertices_,holeIndices_;
    ComPtr<ID3D11BlendState> effectBlend_;
    ComPtr<ID3D11BlendState> additiveBlend_;
    ComPtr<ID3D11DepthStencilState> effectDepth_;
    UINT starIndexCount_{};
    bool holeVisible_{},nameplateVisible_{},pixelationVisible_{},equinoxVisible_{},breakthroughVisible_{};
    bool nanallyVisible_{};
    bool illusionaryVisible_{},desertAuraVisible_{};
    Instance blackHole_;
    DirectX::XMFLOAT2 nameplate_{};
    ImGuiContext* imguiContext_{};
    bool win32Backend_{}, dx11Backend_{}, debugOpen_{}, japaneseFont_{};
    DebugActions debugActions_;
    std::unique_ptr<VrmAvatar> vrm_;
    std::future<std::unique_ptr<VrmAvatar>> vrmFuture_;
    std::string requestedVrm_,loadedVrm_,loadingVrm_,vrmStatus_{"Choose a VRM file"};
    DirectX::XMFLOAT4 debugBounds_{};
};
std::wstring widen(const std::string& utf8);
Color pixelationColor(float time);
void checked(HRESULT result, const char* operation);
} // namespace aura
