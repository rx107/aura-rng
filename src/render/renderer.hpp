#pragma once
#include "core/aura.hpp"
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <array>
#include <span>

namespace aura {
using Microsoft::WRL::ComPtr;
struct Frame {
    const Roller& roller;
    const Progress& progress;
    const std::vector<LogEntry>& logs;
    float time{}, cooldown{}, avatarX{}, avatarZ{}, facing{}, walk{}, cameraYaw{.45f}, cameraDistance{11};
    float mouseX{-1}, mouseY{-1}, fps{};
    bool autoRoll{};
    int collectionScroll{}, logScroll{};
    std::string notice;
};
enum class Action { None, Roll, Auto, Logs, Equip };
struct Hit { Action action{Action::None}; std::size_t index{}; };
class Renderer {
public:
    void initialize(HWND window, bool warp);
    void resize(unsigned width, unsigned height);
    void draw(const Frame& frame, bool present = true);
    void capture(const std::filesystem::path& path);
    Hit hit(float pixelX, float pixelY, const Frame& frame) const;
    int region(float pixelX, float pixelY) const; // 1=collection, 2=log, 0=world
    std::string adapterName() const { return adapterName_; }
    std::size_t instanceCount() const { return instances_.size(); }
    float scale() const { return scale_; }
private:
    struct Vertex { DirectX::XMFLOAT3 position, normal; };
    struct Instance { DirectX::XMFLOAT4X4 world; DirectX::XMFLOAT4 color; };
    void createTargets();
    void createPipeline();
    void box(float x, float y, float z, float sx, float sy, float sz, Color color, float rotation = 0, float glow = 0);
    void world(const Frame& frame);
    void ui(const Frame& frame);
    void rect(float x, float y, float w, float h, D2D1_COLOR_F c, float radius = 0);
    void stroke(float x, float y, float w, float h, D2D1_COLOR_F c, float radius = 0);
    void text(const std::wstring& value, float x, float y, float w, float h, float size, D2D1_COLOR_F color, bool bold = false);
    void label(const std::string& value, float x, float y, float w, float h, float size, D2D1_COLOR_F color, bool bold = false);
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
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vertices_, indices_, instanceBuffer_, constants_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> depthState_;
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<ID2D1RenderTarget> d2d_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> write_;
    struct Font { float size; bool bold; ComPtr<IDWriteTextFormat> format; };
    std::vector<Font> fonts_;
    std::vector<Instance> instances_;
};
std::wstring widen(const std::string& utf8);
void checked(HRESULT result, const char* operation);
} // namespace aura
