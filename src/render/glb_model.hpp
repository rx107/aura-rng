#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <cstddef>

namespace aura {
// オーラの小物用GLB。頂点・画像は一度だけGPUへ送り、複数の蝶で共有します。
// VRMの人型ボーンとは分離し、GLBに入った通常ノードの移動・回転・拡縮を再生します。
class GlbModel {
public:
    static std::unique_ptr<GlbModel> load(ID3D11Device* device,const std::filesystem::path& path);
    void draw(ID3D11DeviceContext* context,DirectX::FXMMATRIX vp,DirectX::CXMMATRIX model,float seconds);
    std::size_t vertexCount() const noexcept {return vertexCount_;}
    std::size_t drawCount() const noexcept {return meshes_.size();}
    std::size_t nodeCount() const noexcept {return nodes_.size();}
    float animationDuration() const noexcept {return duration_;}
    std::size_t animationTrackCount() const noexcept {return tracks_.size();}
    // 検証用: アニメーション時刻のノード行列。正規化前のGLB座標で返します。
    DirectX::XMFLOAT4X4 nodeTransform(std::size_t node,float seconds);
private:
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Pose {DirectX::XMFLOAT3 translation{},scale{1,1,1};DirectX::XMFLOAT4 rotation{0,0,0,1};};
    struct Node {Pose rest;DirectX::XMFLOAT4X4 local{};int parent{-1};bool matrix{};};
    enum class Path {Translation,Rotation,Scale};
    struct Track {std::size_t node{};Path path{};bool step{};std::vector<float> times;std::vector<DirectX::XMFLOAT4> values;};
    struct Mesh {
        Ptr<ID3D11Buffer> vertices,indices;
        Ptr<ID3D11ShaderResourceView> texture;
        std::size_t node{};UINT count{};
        DirectX::XMFLOAT4 color{1,1,1,1};
        float cutoff{-1},roughness{.6f},specular{.15f};bool blend{},unlit{};
    };
    void evaluate(float seconds);
    std::vector<Node> nodes_;
    std::vector<std::size_t> order_;
    std::vector<Pose> pose_;
    std::vector<DirectX::XMFLOAT4X4> world_;
    std::vector<Track> tracks_;
    std::vector<Mesh> meshes_;
    DirectX::XMFLOAT4X4 normalization_{};
    std::size_t vertexCount_{};float duration_{};
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11InputLayout> layout_;Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> sampler_;Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11BlendState> blend_;Ptr<ID3D11DepthStencilState> depth_,transparentDepth_;
};
} // namespace aura
