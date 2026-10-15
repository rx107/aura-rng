#pragma once
#include "core/presentation.hpp"
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <array>
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
};
enum class Action { None, Roll, Auto, Logs, Equip, Debug };
struct Hit { Action action{Action::None}; std::size_t index{}; };
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
    std::size_t equinoxCount() const { return equinoxVisible_?pixelationInstances_.size()+equinoxOrbs_.size():0; }
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
    ComPtr<ID3D11Buffer> starVertices_,starIndices_,holeVertices_,holeIndices_;
    ComPtr<ID3D11BlendState> effectBlend_;
    ComPtr<ID3D11BlendState> additiveBlend_;
    ComPtr<ID3D11DepthStencilState> effectDepth_;
    UINT starIndexCount_{};
    bool holeVisible_{},nameplateVisible_{},pixelationVisible_{},equinoxVisible_{};
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
