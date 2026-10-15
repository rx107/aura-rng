#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "core/animation.hpp"

namespace aura {
// Default morphs are baked once. Compact meshes, bones and bind matrices remain
// for GPU skinning; the original file and unused morph targets are released.
class VrmAvatar {
public:
    static std::unique_ptr<VrmAvatar> load(ID3D11Device* device,const std::filesystem::path& path);
    void draw(ID3D11DeviceContext* context,DirectX::FXMMATRIX vp,DirectX::CXMMATRIX model,const AvatarPose& pose);
    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t drawCount() const { return meshes_.size(); }
    std::string title;
    DirectX::XMFLOAT3 bonePosition(int role) const;
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Mesh {
        Ptr<ID3D11Buffer> vertices,indices;
        Ptr<ID3D11ShaderResourceView> texture;
        UINT count{};
        DirectX::XMFLOAT4 color{1,1,1,1};
        float cutoff{-1}; bool blend{};
        int skin{-1}; std::size_t node{};
    };
    struct Node {
        DirectX::XMFLOAT4X4 local,rest,parentInverse,animated;
        int parent{-1},role{-1}; float side{};
    };
    struct Skin { std::vector<std::size_t> joints; std::vector<DirectX::XMFLOAT4X4> inverse; };
    std::vector<Node> nodes_;
    std::vector<Skin> skins_;
    std::vector<unsigned char> poseState_;
    DirectX::XMFLOAT4X4 normalization_;
    bool legacy_{};
    void animate(const AvatarPose& pose);
    std::vector<Mesh> meshes_;
    std::size_t vertexCount_{};
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11InputLayout> layout_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11BlendState> blend_;
    Ptr<ID3D11DepthStencilState> depth_,transparentDepth_;
};
}
